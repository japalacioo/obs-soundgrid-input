#include "AudioScope.h"

#include <algorithm>
#include <cstdio>

namespace sg::monitor {

double ampToDb(double amplitude)
{
    return amplitude > 0 ? 20.0 * std::log10(amplitude) : kNegInf;
}

double dbToFraction(double db, double minDb)
{
    if (!(db > minDb)) // también -inf y NaN
        return 0.0;
    if (db >= 0.0)
        return 1.0;
    return 1.0 - db / minDb;
}

std::string formatDb(double db)
{
    if (!std::isfinite(db) || db < -99.95)
        return "\xE2\x88\x92\xE2\x88\x9E"; // "−∞"
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", db);
    std::string s = buf;
    if (s == "-0.0")
        return "0.0";
    if (!s.empty() && s[0] == '-')
        s.replace(0, 1, "\xE2\x88\x92"); // U+2212 (signo menos)
    return s;
}

// ------------------------------------------------------------------ AudioScopeTap

AudioScopeTap::AudioScopeTap(TapConfig cfg) : cfg_(cfg)
{
    const int64_t bucket = std::max<int64_t>(cfg_.bucketNs, 1);
    ring_.resize(static_cast<std::size_t>(cfg_.historyNs / bucket) + 1);
}

void AudioScopeTap::flushCurrent()
{
    if (!curOpen_)
        return;
    curOpen_ = false;
    if (cur_.frames == 0 || ring_.empty())
        return;
    cur_.endNs = bucketEnd(cur_);
    ring_[head_] = cur_;
    head_ = (head_ + 1) % ring_.size();
    count_ = std::min(count_ + 1, ring_.size());
}

void AudioScopeTap::push(const float *const *planes, uint32_t channels, uint32_t frames, uint32_t sampleRate,
                         uint64_t firstFrameNs)
{
    if (!planes || !channels || !planes[0] || !frames || !sampleRate)
        return;
    std::lock_guard lk(mu_);
    if (sampleRate != rate_) {
        flushCurrent();
        anchored_ = false; // con otra frecuencia, los frames ya no miden el mismo tiempo
        rate_ = sampleRate;
        bucketFrames_ = static_cast<uint32_t>(
            std::max<int64_t>(1, static_cast<int64_t>(static_cast<double>(sampleRate) * cfg_.bucketNs / 1e9)));
    }
    inChannels_ = channels;
    const float *ch[2] = {planes[0], channels > 1 && planes[1] ? planes[1] : planes[0]};
    const int64_t ts = static_cast<int64_t>(firstFrameNs);
    const double nsPerFrame = 1e9 / sampleRate;
    // Los tiempos se cuentan en frames desde un ancla: cubos contiguos y sin el jitter de los
    // timestamps de cada bloque. Se re-ancla solo ante un hueco o salto de reloj.
    auto frameTime = [&](uint64_t f) { return anchorNs_ + std::llround(static_cast<double>(f) * nsPerFrame); };
    if (!anchored_ || std::llabs(ts - frameTime(anchorFrames_)) > cfg_.gapToleranceNs) {
        flushCurrent(); // el cubo en curso no mezcla audio de dos instantes
        anchored_ = true;
        anchorNs_ = ts;
        anchorFrames_ = 0;
    }
    for (uint32_t i = 0; i < frames; ++i) {
        if (!curOpen_) {
            cur_ = ScopeBucket{};
            cur_.startNs = frameTime(anchorFrames_ + i);
            curStartFrame_ = anchorFrames_ + i;
            curOpen_ = true;
        }
        for (int c = 0; c < 2; ++c) {
            const float x = ch[c][i];
            const float a = x < 0 ? -x : x;
            if (cur_.frames == 0) {
                cur_.min[c] = x;
                cur_.max[c] = x;
            } else {
                cur_.min[c] = std::min(cur_.min[c], x);
                cur_.max[c] = std::max(cur_.max[c], x);
            }
            cur_.peak[c] = std::max(cur_.peak[c], a);
            cur_.sumSq[c] += static_cast<double>(x) * x;
        }
        if (++cur_.frames >= bucketFrames_)
            flushCurrent();
    }
    anchorFrames_ += frames;
    lastDataNs_ = frameTime(anchorFrames_);
    totalFrames_ += frames;
}

int64_t AudioScopeTap::bucketEnd(const ScopeBucket &b) const
{
    // Fin exacto respecto del ancla (sin acumular redondeos).
    const double nsPerFrame = 1e9 / rate_;
    return anchorNs_ + std::llround(static_cast<double>(curStartFrame_ + b.frames) * nsPerFrame);
}

AudioScopeTap::Snapshot AudioScopeTap::snapshot(int64_t fromNs) const
{
    std::lock_guard lk(mu_);
    Snapshot s;
    s.lastDataNs = lastDataNs_;
    s.sampleRate = rate_;
    s.inputChannels = inChannels_;
    s.totalFrames = totalFrames_;
    s.buckets.reserve(count_ + 1);
    const std::size_t cap = ring_.size();
    for (std::size_t k = 0; k < count_; ++k) {
        const ScopeBucket &b = ring_[(head_ + cap - count_ + k) % cap];
        if (b.endNs > fromNs)
            s.buckets.push_back(b);
    }
    if (curOpen_ && cur_.frames > 0 && rate_) {
        ScopeBucket b = cur_;
        b.endNs = bucketEnd(b);
        if (b.endNs > fromNs)
            s.buckets.push_back(b);
    }
    return s;
}

void AudioScopeTap::reset()
{
    std::lock_guard lk(mu_);
    head_ = count_ = 0;
    curOpen_ = false;
    anchored_ = false;
    lastDataNs_ = 0;
    totalFrames_ = 0;
}

Levels computeLevels(const std::vector<ScopeBucket> &buckets, int64_t fromNs, int64_t toNs)
{
    Levels l;
    std::array<float, 2> peak{0, 0};
    std::array<double, 2> ss{0, 0};
    for (const auto &b : buckets) {
        const int64_t center = b.startNs + (b.endNs - b.startNs) / 2;
        if (center <= fromNs || center > toNs)
            continue;
        for (int c = 0; c < 2; ++c) {
            peak[c] = std::max(peak[c], b.peak[c]);
            ss[c] += b.sumSq[c];
        }
        l.frames += b.frames;
    }
    if (!l.frames)
        return l;
    for (int c = 0; c < 2; ++c) {
        l.peakDb[c] = ampToDb(peak[c]);
        l.rmsDb[c] = ampToDb(std::sqrt(ss[c] / static_cast<double>(l.frames)));
    }
    return l;
}

// ------------------------------------------------------------------ MeterBallistics

void MeterBallistics::update(int64_t nowNs, double peakDb, double rmsDb)
{
    const double dt = lastNs_ ? std::max(0.0, static_cast<double>(nowNs - lastNs_) / 1e9) : 0.0;
    lastNs_ = nowNs;
    const double fall = cfg_.decayDbPerSec * dt;
    auto ballistic = [&](double shown, double input) {
        if (!std::isfinite(shown) || input >= shown)
            return input; // el ataque es instantáneo (como OBS)
        const double v = std::max(input, shown - fall);
        return v < cfg_.minDb - 1.0 ? input : v; // por debajo de la escala: se suelta
    };
    peak_ = ballistic(peak_, peakDb);
    rms_ = ballistic(rms_, rmsDb);

    const bool holdExpired = static_cast<double>(nowNs - holdSinceNs_) / 1e9 > cfg_.peakHoldSec;
    if (!std::isfinite(hold_) || peakDb >= hold_ || holdExpired) {
        hold_ = peakDb;
        holdSinceNs_ = nowNs;
    }

    const bool clipping = peakDb >= cfg_.clipDb;
    if (clipping) {
        lastClipNs_ = nowNs;
        if (!wasClipping_)
            ++clipEvents_;
    }
    wasClipping_ = clipping;
}

void MeterBallistics::reset()
{
    *this = MeterBallistics(cfg_);
}

bool MeterBallistics::clipActive(int64_t nowNs) const
{
    if (lastClipNs_ == std::numeric_limits<int64_t>::min())
        return false;
    return static_cast<double>(nowNs - lastClipNs_) / 1e9 <= cfg_.clipHoldSec;
}

Zone zoneFor(double db)
{
    if (db >= kErrorDb)
        return Zone::Error;
    if (db >= kWarningDb)
        return Zone::Warning;
    return Zone::Nominal;
}

std::vector<ZoneSpan> meterZones(double minDb)
{
    const double w = dbToFraction(kWarningDb, minDb);
    const double e = dbToFraction(kErrorDb, minDb);
    return {{Zone::Nominal, 0.0, w}, {Zone::Warning, w, e}, {Zone::Error, e, 1.0}};
}

std::vector<ZoneSpan> litZones(double displayPeakDb, double minDb)
{
    const double f = dbToFraction(displayPeakDb, minDb);
    std::vector<ZoneSpan> out;
    for (const auto &z : meterZones(minDb)) {
        if (f <= z.from)
            break;
        out.push_back({z.zone, z.from, std::min(f, z.to)});
    }
    return out;
}

std::vector<Tick> scaleTicks(double minDb, double majorStepDb, double minorStepDb)
{
    std::vector<Tick> out;
    const int major = std::max(1, static_cast<int>(std::lround(majorStepDb)));
    const int minor = std::max(1, static_cast<int>(std::lround(minorStepDb)));
    for (int d = 0; d >= static_cast<int>(std::lround(minDb)); d -= minor) {
        Tick t;
        t.db = d;
        t.fraction = d == 0 ? 1.0 : dbToFraction(d, minDb);
        if (d == static_cast<int>(std::lround(minDb)))
            t.fraction = 0.0;
        t.major = (-d) % major == 0;
        if (t.major)
            t.label = d == 0 ? "0" : "\xE2\x88\x92" + std::to_string(-d);
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<std::array<Column, 2>> waveformColumns(const std::vector<ScopeBucket> &buckets, int64_t nowNs,
                                                   int64_t spanNs, uint32_t width)
{
    std::vector<std::array<Column, 2>> cols(width);
    if (!width || spanNs <= 0)
        return cols;
    const int64_t origin = nowNs - spanNs;
    const double colNs = static_cast<double>(spanNs) / width;
    for (const auto &b : buckets) {
        if (b.endNs <= origin || b.startNs >= nowNs)
            continue;
        const double s = static_cast<double>(std::max(b.startNs, origin) - origin);
        const double e = static_cast<double>(std::min(b.endNs, nowNs) - origin);
        auto c0 = static_cast<int64_t>(s / colNs);
        auto c1 = static_cast<int64_t>(std::ceil(e / colNs)) - 1;
        c0 = std::clamp<int64_t>(c0, 0, width - 1);
        c1 = std::clamp<int64_t>(c1, c0, width - 1);
        for (int64_t c = c0; c <= c1; ++c) {
            for (int ch = 0; ch < 2; ++ch) {
                Column &col = cols[static_cast<std::size_t>(c)][ch];
                if (!col.has) {
                    col = {true, b.min[ch], b.max[ch]};
                } else {
                    col.min = std::min(col.min, b.min[ch]);
                    col.max = std::max(col.max, b.max[ch]);
                }
            }
        }
    }
    return cols;
}

// ------------------------------------------------------------------ distribución

MonitorLayout computeLayout(float W, float H)
{
    MonitorLayout l;
    l.canvas = {0, 0, W, H};
    const float m = std::max(2.0f, std::min(W, H) * 0.035f);
    l.border = std::max(1.0f, H * 0.004f);
    // En el Multiview el recuadro se ve a 1/4 (o menos): textos grandes para leerlos a distancia.
    l.fontPx = std::min(H * 0.07f, W * 0.04f);
    l.smallFontPx = std::min(H * 0.048f, W * 0.027f);
    const float textRow = l.smallFontPx * 1.5f;

    l.header = {m, m, W - 2 * m, std::max(l.fontPx * 1.6f, H * 0.1f)};
    const float clipW = std::min(W * 0.14f, l.header.h * 2.6f);
    l.clipBadge = {l.header.right() - clipW, l.header.y, clipW, l.header.h};
    l.label = {l.header.x, l.header.y, l.clipBadge.x - l.header.x - m, l.header.h};

    const float bodyTop = l.header.bottom() + m;
    const float bodyBottom = H * 0.83f; // debajo, el nombre de la escena del Multiview
    const float bodyH = std::max(1.0f, bodyBottom - bodyTop);

    l.meterPanel = {m, bodyTop, std::max(W * 0.24f, 1.0f), bodyH};
    const float barW = l.meterPanel.w * 0.28f;
    const float scaleW = l.meterPanel.w * 0.32f;
    const float gap = (l.meterPanel.w - 2 * barW - scaleW) / 4.0f;
    const float barTop = l.meterPanel.y + textRow;
    const float barH = std::max(1.0f, l.meterPanel.h - 2 * textRow);
    l.meters[0] = {l.meterPanel.x + gap, barTop, barW, barH};
    l.scale = {l.meters[0].right() + gap, barTop, scaleW, barH};
    l.meters[1] = {l.scale.right() + gap, barTop, barW, barH};
    for (int i = 0; i < 2; ++i) {
        l.holdText[i] = {l.meters[i].x, l.meterPanel.y, barW, textRow};
        l.channelText[i] = {l.meters[i].x, l.meters[i].bottom(), barW, textRow};
    }

    const float sx = l.meterPanel.right() + m;
    l.scopePanel = {sx, bodyTop, std::max(1.0f, W - m - sx), bodyH};
    l.timeAxis = {sx, l.scopePanel.bottom() - textRow, l.scopePanel.w, textRow};
    const float laneGap = m * 0.5f;
    const float half = (l.scopePanel.h - textRow - laneGap) / 2.0f;
    for (int i = 0; i < 2; ++i) {
        const float y = l.scopePanel.y + i * (half + laneGap);
        l.readout[i] = {sx, y, l.scopePanel.w, textRow};
        l.lanes[i] = {sx, y + textRow, l.scopePanel.w, std::max(1.0f, half - textRow)};
    }
    return l;
}

} // namespace sg::monitor
