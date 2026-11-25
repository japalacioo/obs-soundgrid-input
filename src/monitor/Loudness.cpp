#include "Loudness.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sg::loudness {

namespace {
constexpr double kPi = 3.14159265358979323846;
const char *const kMinus = "\xE2\x88\x92"; // −
const char *const kDash = "\xE2\x80\x94";  // —

bool measured(double v)
{
    return std::isfinite(v);
}

// Bessel modificada de primera especie, orden 0 (ventana de Kaiser).
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1e-12)
            break;
    }
    return sum;
}

double ampToDb(double lin)
{
    return lin > 0 ? 20.0 * std::log10(lin) : kNotMeasured;
}
} // namespace

double channelWeight(Role r)
{
    switch (r) {
    case Role::Front:
        return 1.0;
    case Role::Surround:
        return 1.41;
    case Role::Lfe:
        return 0.0;
    }
    return 1.0;
}

// Coeficientes de BS.1770-4 recalculados para cualquier frecuencia (mismo método que libebur128):
// a 48 kHz coinciden con la tabla de la norma.
KWeighting kWeightingFor(double fs)
{
    KWeighting k;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        k.shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        k.shelf.b1 = 2.0 * (K * K - Vh) / a0;
        k.shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        k.shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        k.shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        k.highpass.b0 = 1.0;
        k.highpass.b1 = -2.0;
        k.highpass.b2 = 1.0;
        k.highpass.a1 = 2.0 * (K * K - 1.0) / a0;
        k.highpass.a2 = (1.0 - K / Q + K * K) / a0;
    }
    return k;
}

double energyToLufs(double energy)
{
    return energy > 0 ? -0.691 + 10.0 * std::log10(energy) : kNotMeasured;
}

double lufsToEnergy(double lufs)
{
    return measured(lufs) ? std::pow(10.0, (lufs + 0.691) / 10.0) : 0.0;
}

// ------------------------------------------------------------------ true peak

void TruePeakDetector::configure(int factor, int tapsPerPhase)
{
    factor_ = std::clamp(factor, 1, 8);
    taps_ = std::max(2, tapsPerPhase);
    phases_.assign(static_cast<std::size_t>(factor_), std::vector<float>(static_cast<std::size_t>(taps_), 0.0f));
    if (factor_ > 1) {
        // Interpolador: sinc con corte en la Nyquist original y ventana de Kaiser (β = 8).
        const int len = factor_ * taps_;
        const double c = (len - 1) / 2.0, beta = 8.0, i0b = besselI0(beta);
        std::vector<double> h(static_cast<std::size_t>(len));
        for (int n = 0; n < len; ++n) {
            const double t = (n - c) / factor_;
            const double sinc = std::fabs(t) < 1e-12 ? 1.0 : std::sin(kPi * t) / (kPi * t);
            const double r = (n - c) / c;
            h[static_cast<std::size_t>(n)] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        }
        for (int p = 0; p < factor_; ++p) {
            double sum = 0;
            for (int k = 0; k < taps_; ++k)
                sum += h[static_cast<std::size_t>(p + k * factor_)];
            for (int k = 0; k < taps_; ++k) // cada fase con ganancia 1 en continua
                phases_[static_cast<std::size_t>(p)][static_cast<std::size_t>(k)] =
                    static_cast<float>(h[static_cast<std::size_t>(p + k * factor_)] / sum);
        }
    } else {
        phases_[0][0] = 1.0f;
    }
    reset();
}

void TruePeakDetector::reset()
{
    hist_.assign(static_cast<std::size_t>(taps_) * 2, 0.0f);
    pos_ = 0;
}

float TruePeakDetector::process(const float *x, uint32_t frames)
{
    float peak = 0.0f;
    if (!x)
        return peak;
    if (phases_.empty())
        configure(factor_, taps_);
    const std::size_t T = static_cast<std::size_t>(taps_);
    for (uint32_t i = 0; i < frames; ++i) {
        const float s = x[i];
        peak = std::max(peak, std::fabs(s));
        if (factor_ == 1)
            continue;
        // Historial duplicado: hist_[pos_..pos_+T) son las últimas T muestras, la más nueva primero.
        pos_ = (pos_ == 0 ? T : pos_) - 1;
        hist_[pos_] = s;
        hist_[pos_ + T] = s;
        const float *h = &hist_[pos_];
        for (const auto &ph : phases_) {
            float y = 0.0f;
            for (std::size_t k = 0; k < T; ++k)
                y += ph[k] * h[k];
            peak = std::max(peak, std::fabs(y));
        }
    }
    return peak;
}

// ------------------------------------------------------------------ histogramas

GatedHistogram::GatedHistogram()
    : counts_(static_cast<std::size_t>(kBins), 0), energy_(static_cast<std::size_t>(kBins), 0.0)
{
}

int GatedHistogram::binOf(double lufs)
{
    const int b = static_cast<int>(std::floor((lufs - kMin) / kStep));
    return std::clamp(b, 0, kBins - 1);
}

void GatedHistogram::add(double energy)
{
    const double l = energyToLufs(energy);
    if (!measured(l) || l <= kMin) // puerta absoluta: solo bloques por encima de −70 LUFS
        return;
    const auto b = static_cast<std::size_t>(binOf(l));
    ++counts_[b];
    energy_[b] += energy;
    ++total_;
    energySum_ += energy;
}

void GatedHistogram::clear()
{
    std::fill(counts_.begin(), counts_.end(), 0);
    std::fill(energy_.begin(), energy_.end(), 0.0);
    total_ = 0;
    energySum_ = 0;
}

double GatedHistogram::relativeThreshold(double relativeGate) const
{
    return energyToLufs(energySum_ / static_cast<double>(total_)) + relativeGate;
}

double GatedHistogram::gatedLoudness(double relativeGate) const
{
    if (!total_)
        return kNotMeasured;
    const double thr = relativeThreshold(relativeGate);
    int start = binOf(thr);
    if (kMin + (start + 0.5) * kStep <= thr) // la banda del umbral cuenta si su centro está por encima
        ++start;
    double e = 0;
    uint64_t n = 0;
    for (int b = std::max(0, start); b < kBins; ++b) {
        e += energy_[static_cast<std::size_t>(b)];
        n += counts_[static_cast<std::size_t>(b)];
    }
    return n ? energyToLufs(e / static_cast<double>(n)) : kNotMeasured;
}

double GatedHistogram::range(double relativeGate, double lowPct, double highPct) const
{
    if (!total_)
        return kNotMeasured;
    const double thr = relativeThreshold(relativeGate);
    int start = binOf(thr);
    if (kMin + (start + 0.5) * kStep <= thr)
        ++start;
    start = std::max(0, start);
    uint64_t n = 0;
    for (int b = start; b < kBins; ++b)
        n += counts_[static_cast<std::size_t>(b)];
    if (!n)
        return kNotMeasured;
    auto percentile = [&](double pct) {
        // Rango más cercano (EBU Tech 3342): el valor en la posición round((n − 1)·pct).
        const auto rank = static_cast<uint64_t>(std::llround(static_cast<double>(n - 1) * pct));
        uint64_t cum = 0;
        for (int b = start; b < kBins; ++b) {
            cum += counts_[static_cast<std::size_t>(b)];
            if (cum > rank)
                return kMin + (b + 0.5) * kStep;
        }
        return kMax;
    };
    return percentile(highPct) - percentile(lowPct);
}

std::vector<uint64_t> GatedHistogram::coarse(double fromLufs, double toLufs) const
{
    const int bands = std::max(1, static_cast<int>(std::lround(toLufs - fromLufs)));
    std::vector<uint64_t> out(static_cast<std::size_t>(bands), 0);
    for (int b = 0; b < kBins; ++b) {
        const uint64_t c = counts_[static_cast<std::size_t>(b)];
        if (!c)
            continue;
        const double center = kMin + (b + 0.5) * kStep;
        const int band = static_cast<int>(std::floor(center - fromLufs));
        if (band >= 0 && band < bands)
            out[static_cast<std::size_t>(band)] += c;
    }
    return out;
}

// ------------------------------------------------------------------ medidor

LoudnessMeter::LoudnessMeter(MeterConfig cfg)
{
    configure(cfg);
}

void LoudnessMeter::configure(const MeterConfig &cfg)
{
    cfg_ = cfg;
    if (cfg_.sampleRate < 8000)
        cfg_.sampleRate = 48000;
    if (cfg_.roles.empty())
        cfg_.roles = {Role::Front};
    cfg_.historySeconds = std::clamp(cfg_.historySeconds, 10, 3600);
    k_ = kWeightingFor(cfg_.sampleRate);
    weight_.clear();
    for (Role r : cfg_.roles)
        weight_.push_back(channelWeight(r));
    tp_.assign(cfg_.roles.size(), TruePeakDetector{});
    for (auto &d : tp_)
        d.configure(cfg_.truePeakFactor);
    subBlockFrames_ = static_cast<uint32_t>(std::lround(cfg_.sampleRate / kSubBlocksPerSecond));
    hist_.assign(static_cast<std::size_t>(cfg_.historySeconds) * kSubBlocksPerSecond, HistoryPoint{});
    reset();
}

void LoudnessMeter::reset()
{
    state_.assign(cfg_.roles.size(), ChannelState{});
    for (auto &d : tp_)
        d.reset();
    inSubBlock_ = 0;
    acc_ = 0;
    subBlockTp_ = subBlockSp_ = 0;
    ring_.fill(0.0);
    tpRing_.fill(0.0f);
    subBlocks_ = 0;
    intHist_.clear();
    lraHist_.clear();
    psrSum_ = 0;
    psrCount_ = 0;
    tpMaxLin_ = spMaxLin_ = 0;
    std::fill(hist_.begin(), hist_.end(), HistoryPoint{});
    histHead_ = histCount_ = 0;
    r_ = Readings{};
}

void LoudnessMeter::process(const float *const *planes, uint32_t frames)
{
    const std::size_t nch = cfg_.roles.size();
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t chunk = std::min(frames - done, subBlockFrames_ - inSubBlock_);
        for (std::size_t c = 0; c < nch; ++c) {
            const float *x = planes ? planes[c] : nullptr;
            if (!x) { // canal ausente: silencio (el filtro se vacía igualmente)
                static const float zeros[512] = {};
                for (uint32_t i = 0; i < chunk;) {
                    const uint32_t n = std::min<uint32_t>(512, chunk - i);
                    tp_[c].process(zeros, n);
                    i += n;
                }
            } else {
                const float tp = tp_[c].process(x + done, chunk);
                subBlockTp_ = std::max(subBlockTp_, tp);
            }
            const double w = weight_[c];
            ChannelState &st = state_[c];
            const Biquad &s1 = k_.shelf, &s2 = k_.highpass;
            double sum = 0;
            for (uint32_t i = 0; i < chunk; ++i) {
                const double in = x ? static_cast<double>(x[done + i]) : 0.0;
                if (x)
                    subBlockSp_ = std::max(subBlockSp_, std::fabs(static_cast<float>(in)));
                const double y1 = s1.b0 * in + s1.b1 * st.shelf.x1 + s1.b2 * st.shelf.x2 - s1.a1 * st.shelf.y1 -
                                  s1.a2 * st.shelf.y2;
                st.shelf.x2 = st.shelf.x1;
                st.shelf.x1 = in;
                st.shelf.y2 = st.shelf.y1;
                st.shelf.y1 = y1;
                const double y2 = s2.b0 * y1 + s2.b1 * st.highpass.x1 + s2.b2 * st.highpass.x2 -
                                  s2.a1 * st.highpass.y1 - s2.a2 * st.highpass.y2;
                st.highpass.x2 = st.highpass.x1;
                st.highpass.x1 = y1;
                st.highpass.y2 = st.highpass.y1;
                st.highpass.y1 = y2;
                sum += y2 * y2;
            }
            // Evita desnormales tras un silencio largo.
            if (std::fabs(st.shelf.y1) < 1e-30)
                st.shelf = BiquadState{};
            if (std::fabs(st.highpass.y1) < 1e-30)
                st.highpass = BiquadState{};
            acc_ += w * sum;
        }
        inSubBlock_ += chunk;
        done += chunk;
        if (inSubBlock_ >= subBlockFrames_)
            closeSubBlock();
    }
}

double LoudnessMeter::windowEnergy(int blocks) const
{
    double e = 0;
    for (int i = 0; i < blocks; ++i)
        e += ring_[static_cast<std::size_t>((subBlocks_ - 1 - static_cast<uint64_t>(i)) % kShortTermBlocks)];
    return e / blocks;
}

void LoudnessMeter::closeSubBlock()
{
    const double e = acc_ / static_cast<double>(subBlockFrames_);
    ring_[static_cast<std::size_t>(subBlocks_ % kShortTermBlocks)] = e;
    tpRing_[static_cast<std::size_t>(subBlocks_ % kShortTermBlocks)] = subBlockTp_;
    ++subBlocks_;
    tpMaxLin_ = std::max(tpMaxLin_, static_cast<double>(subBlockTp_));
    spMaxLin_ = std::max(spMaxLin_, static_cast<double>(subBlockSp_));
    acc_ = 0;
    inSubBlock_ = 0;
    subBlockTp_ = subBlockSp_ = 0;

    r_.measuredSeconds = static_cast<double>(subBlocks_) / kSubBlocksPerSecond;
    if (subBlocks_ >= static_cast<uint64_t>(kMomentaryBlocks)) {
        const double em = windowEnergy(kMomentaryBlocks);
        r_.momentary = energyToLufs(em);
        intHist_.add(em); // bloque de 400 ms con 75 % de solapamiento
        if (measured(r_.momentary))
            r_.momentaryMax = measured(r_.momentaryMax) ? std::max(r_.momentaryMax, r_.momentary) : r_.momentary;
    }
    if (subBlocks_ >= static_cast<uint64_t>(kShortTermBlocks)) {
        const double es = windowEnergy(kShortTermBlocks);
        r_.shortTerm = energyToLufs(es);
        lraHist_.add(es); // LRA: short-term cada 100 ms
        if (measured(r_.shortTerm))
            r_.shortTermMax = measured(r_.shortTermMax) ? std::max(r_.shortTermMax, r_.shortTerm) : r_.shortTerm;
    }
    r_.gatedBlocks = intHist_.count();
    r_.integrated = intHist_.gatedLoudness(kRelativeGateIntegrated);
    r_.lra = lraHist_.count() ? lraHist_.range(kRelativeGateLra) : kNotMeasured;
    r_.truePeakMax = ampToDb(tpMaxLin_);
    r_.samplePeakMax = ampToDb(spMaxLin_);
    float recent = 0;
    const auto n = static_cast<std::size_t>(std::min<uint64_t>(subBlocks_, kShortTermBlocks));
    for (std::size_t i = 0; i < n; ++i)
        recent = std::max(recent, tpRing_[i]);
    r_.truePeakRecent = ampToDb(recent);
    r_.plr = measured(r_.truePeakMax) && measured(r_.integrated) ? r_.truePeakMax - r_.integrated : kNotMeasured;
    r_.psr = measured(r_.truePeakRecent) && measured(r_.shortTerm) ? r_.truePeakRecent - r_.shortTerm : kNotMeasured;
    if (measured(r_.psr) && r_.shortTerm > kAbsoluteGate) {
        psrSum_ += r_.psr;
        ++psrCount_;
    }
    r_.dr = psrCount_ ? psrSum_ / static_cast<double>(psrCount_) : kNotMeasured;

    HistoryPoint p;
    p.momentary = static_cast<float>(r_.momentary);
    p.shortTerm = static_cast<float>(r_.shortTerm);
    p.integrated = static_cast<float>(r_.integrated);
    p.psr = static_cast<float>(r_.psr);
    hist_[histHead_] = p;
    histHead_ = (histHead_ + 1) % hist_.size();
    histCount_ = std::min(histCount_ + 1, hist_.size());
}

std::vector<HistoryPoint> LoudnessMeter::history(std::size_t n) const
{
    n = std::min(n, histCount_);
    std::vector<HistoryPoint> out;
    out.reserve(n);
    const std::size_t cap = hist_.size();
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(hist_[(histHead_ + cap - n + i) % cap]);
    return out;
}

// ------------------------------------------------------------------ objetivos

const std::vector<Preset> &presets()
{
    static const std::vector<Preset> p = {
        {"streaming", "Streaming / m\xC3\xBAsica", -14.0, 1.0, -1.0, 0.0, false},
        {"youtube", "YouTube", -14.0, 1.0, -1.0, 0.0, false},
        {"podcast", "Podcast", -16.0, 1.0, -1.0, 0.0, false},
        {"ebu_r128", "EBU R128", -23.0, 1.0, -1.0, 0.0, false},
        {"atsc_a85", "ATSC A/85", -24.0, 2.0, -2.0, 0.0, true},
        {"netflix", "Netflix", -27.0, 2.0, -2.0, 0.0, false},
        {"custom", "Personalizado", -23.0, 1.0, -1.0, 0.0, false},
    };
    return p;
}

const Preset *findPreset(const std::string &id)
{
    for (const auto &p : presets())
        if (id == p.id)
            return &p;
    return nullptr;
}

TargetZone targetZone(double lufs, double target, double tolerance)
{
    if (!measured(lufs) || lufs < target - tolerance)
        return TargetZone::Below;
    if (lufs > target + tolerance)
        return TargetZone::Above;
    return TargetZone::Within;
}

std::string formatLoudness(double v, bool explicitPlus)
{
    if (!measured(v))
        return kDash;
    char buf[32];
    const double r = std::round(v * 10.0) / 10.0;
    std::snprintf(buf, sizeof buf, "%.1f", std::fabs(r));
    if (r < 0)
        return std::string(kMinus) + buf;
    if (explicitPlus && r > 0)
        return std::string("+") + buf;
    return buf;
}

} // namespace sg::loudness
