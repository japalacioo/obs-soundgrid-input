// Medidor de sonoridad (src/monitor/Loudness.*): ITU-R BS.1770-4, EBU Tech 3341 (momentary,
// short-term, integrated y true peak) y EBU Tech 3342 (LRA). Las señales se sintetizan aquí.
#include "monitor/Loudness.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <vector>

using namespace sg::loudness;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Segment {
    double dbfs;    // amplitud de pico del seno (dBFS); <= −200 = silencio
    double seconds;
    double freq = 1000.0;
};

// Alimenta el medidor con senos (igual en todos los canales indicados) en bloques de `block` frames.
void feed(LoudnessMeter &m, const std::vector<Segment> &segs, const std::vector<bool> &active, uint32_t block = 480,
          double phase0 = 0.0)
{
    const double fs = m.config().sampleRate;
    const std::size_t nch = m.config().roles.size();
    std::vector<std::vector<float>> buf(nch, std::vector<float>(block));
    std::vector<const float *> planes(nch);
    double phase = phase0;
    for (const Segment &s : segs) {
        const double amp = s.dbfs <= -200 ? 0.0 : std::pow(10.0, s.dbfs / 20.0);
        auto left = static_cast<uint64_t>(std::llround(s.seconds * fs));
        const double inc = 2 * kPi * s.freq / fs;
        while (left) {
            const auto n = static_cast<uint32_t>(std::min<uint64_t>(left, block));
            for (uint32_t i = 0; i < n; ++i) {
                const auto v = static_cast<float>(amp * std::sin(phase));
                phase += inc;
                for (std::size_t c = 0; c < nch; ++c)
                    buf[c][i] = active.empty() || active[c] ? v : 0.0f;
            }
            for (std::size_t c = 0; c < nch; ++c)
                planes[c] = buf[c].data();
            m.process(planes.data(), n);
            left -= n;
        }
    }
}

LoudnessMeter stereo(double fs = 48000, int tp = 4)
{
    MeterConfig c;
    c.sampleRate = fs;
    c.roles = {Role::Front, Role::Front};
    c.truePeakFactor = tp;
    return LoudnessMeter(c);
}

} // namespace

TEST(Loudness, KWeightingMatchesBs1770TableAt48k)
{
    const KWeighting k = kWeightingFor(48000);
    EXPECT_NEAR(k.shelf.b0, 1.53512485958697, 1e-9);
    EXPECT_NEAR(k.shelf.b1, -2.69169618940638, 1e-9);
    EXPECT_NEAR(k.shelf.b2, 1.19839281085285, 1e-9);
    EXPECT_NEAR(k.shelf.a1, -1.69065929318241, 1e-9);
    EXPECT_NEAR(k.shelf.a2, 0.73248077421585, 1e-9);
    EXPECT_NEAR(k.highpass.a1, -1.99004745483398, 1e-9);
    EXPECT_NEAR(k.highpass.a2, 0.99007225036621, 1e-9);
    EXPECT_DOUBLE_EQ(k.highpass.b0, 1.0);
    EXPECT_DOUBLE_EQ(k.highpass.b1, -2.0);
    EXPECT_DOUBLE_EQ(k.highpass.b2, 1.0);
}

TEST(Loudness, ChannelWeights)
{
    EXPECT_DOUBLE_EQ(channelWeight(Role::Front), 1.0);
    EXPECT_DOUBLE_EQ(channelWeight(Role::Surround), 1.41);
    EXPECT_DOUBLE_EQ(channelWeight(Role::Lfe), 0.0);
    EXPECT_NEAR(energyToLufs(lufsToEnergy(-23.0)), -23.0, 1e-12);
    EXPECT_EQ(energyToLufs(0.0), kNotMeasured);
}

// EBU Tech 3341, caso 1: seno estéreo de 1 kHz a −23 dBFS -> M, S, I = −23,0 ±0,1.
TEST(Loudness, Tech3341Case1Minus23)
{
    auto m = stereo();
    feed(m, {{-23.0, 20.0}}, {});
    EXPECT_NEAR(m.readings().momentary, -23.0, 0.1);
    EXPECT_NEAR(m.readings().shortTerm, -23.0, 0.1);
    EXPECT_NEAR(m.readings().integrated, -23.0, 0.1);
}

// Caso 2: −33 dBFS -> −33,0 ±0,1.
TEST(Loudness, Tech3341Case2Minus33)
{
    auto m = stereo();
    feed(m, {{-33.0, 20.0}}, {});
    EXPECT_NEAR(m.readings().momentary, -33.0, 0.1);
    EXPECT_NEAR(m.readings().shortTerm, -33.0, 0.1);
    EXPECT_NEAR(m.readings().integrated, -33.0, 0.1);
}

// Caso 3: −36 / −23 / −36 dBFS durante 10 / 60 / 10 s -> I = −23,0 ±0,1 (puerta relativa).
TEST(Loudness, Tech3341Case3RelativeGate)
{
    auto m = stereo();
    feed(m, {{-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}}, {});
    EXPECT_NEAR(m.readings().integrated, -23.0, 0.1);
}

// Caso 4: −72 / −36 / −23 / −36 / −72 dBFS durante 10 / 10 / 60 / 10 / 10 s -> I = −23,0 ±0,1.
TEST(Loudness, Tech3341Case4AbsoluteGate)
{
    auto m = stereo();
    feed(m, {{-72.0, 10.0}, {-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}, {-72.0, 10.0}}, {});
    EXPECT_NEAR(m.readings().integrated, -23.0, 0.1);
}

// Caso 5: −26 / −20 / −26 dBFS durante 20 / 20,1 / 20 s -> I = −23,0 ±0,1.
TEST(Loudness, Tech3341Case5)
{
    auto m = stereo();
    feed(m, {{-26.0, 20.0}, {-20.0, 20.1}, {-26.0, 20.0}}, {});
    EXPECT_NEAR(m.readings().integrated, -23.0, 0.1);
}

// EBU Tech 3342: LRA con señales escalonadas (tolerancia ±1 LU).
TEST(Loudness, Tech3342Case1Lra10)
{
    auto m = stereo();
    feed(m, {{-20.0, 20.0}, {-30.0, 20.0}}, {});
    EXPECT_NEAR(m.readings().lra, 10.0, 1.0);
}

TEST(Loudness, Tech3342Case2Lra5)
{
    auto m = stereo();
    feed(m, {{-20.0, 20.0}, {-15.0, 20.0}}, {});
    EXPECT_NEAR(m.readings().lra, 5.0, 1.0);
}

TEST(Loudness, Tech3342Case3Lra20)
{
    auto m = stereo();
    feed(m, {{-40.0, 20.0}, {-20.0, 20.0}}, {});
    EXPECT_NEAR(m.readings().lra, 20.0, 1.0);
}

TEST(Loudness, Tech3342Case4Lra15)
{
    auto m = stereo();
    feed(m, {{-50.0, 20.0}, {-35.0, 20.0}, {-20.0, 20.0}, {-35.0, 20.0}, {-50.0, 20.0}}, {});
    EXPECT_NEAR(m.readings().lra, 15.0, 1.0);
}

TEST(Loudness, OtherSampleRatesGiveSameLoudness)
{
    for (double fs : {44100.0, 96000.0, 192000.0}) {
        auto m = stereo(fs, 2);
        feed(m, {{-23.0, 10.0}}, {});
        EXPECT_NEAR(m.readings().integrated, -23.0, 0.1) << fs;
        EXPECT_NEAR(m.readings().momentary, -23.0, 0.1) << fs;
    }
}

// Un solo canal a −23 dBFS: 3 dB menos que el mismo seno en estéreo.
TEST(Loudness, MonoIsOneChannel)
{
    MeterConfig c;
    c.roles = {Role::Front};
    LoudnessMeter m(c);
    feed(m, {{-23.0, 10.0}}, {});
    EXPECT_NEAR(m.readings().integrated, -26.0, 0.1);
}

// 5.1 (orden de OBS: FL FR FC LFE RL RR): envolvente ponderado 1,41; LFE excluido.
TEST(Loudness, FivePointOneWeights)
{
    MeterConfig c;
    c.roles = {Role::Front, Role::Front, Role::Front, Role::Lfe, Role::Surround, Role::Surround};
    {
        LoudnessMeter m(c);
        feed(m, {{-23.0, 10.0}}, {false, false, false, false, true, false});
        EXPECT_NEAR(m.readings().integrated, -26.0 + 10 * std::log10(1.41), 0.1);
    }
    {
        LoudnessMeter m(c);
        feed(m, {{-23.0, 10.0}}, {false, false, false, true, false, false});
        EXPECT_EQ(m.readings().integrated, kNotMeasured) << "el LFE no cuenta";
    }
    {
        LoudnessMeter m(c);
        feed(m, {{-23.0, 10.0}}, {true, true, true, false, false, false});
        EXPECT_NEAR(m.readings().integrated, -26.0 + 10 * std::log10(3.0), 0.1);
    }
}

TEST(Loudness, NotMeasuredUntilEnoughAudio)
{
    auto m = stereo();
    EXPECT_EQ(m.readings().integrated, kNotMeasured);
    feed(m, {{-23.0, 0.3}}, {});
    EXPECT_EQ(m.readings().momentary, kNotMeasured) << "menos de 400 ms";
    EXPECT_EQ(m.readings().integrated, kNotMeasured);
    feed(m, {{-23.0, 0.2}}, {});
    EXPECT_TRUE(std::isfinite(m.readings().momentary));
    EXPECT_EQ(m.readings().shortTerm, kNotMeasured) << "menos de 3 s";
    feed(m, {{-23.0, 3.0}}, {});
    EXPECT_NEAR(m.readings().shortTerm, -23.0, 0.1);
}

TEST(Loudness, SilenceNeverEntersIntegrated)
{
    auto m = stereo();
    feed(m, {{-300.0, 30.0}}, {});
    EXPECT_EQ(m.readings().integrated, kNotMeasured);
    EXPECT_EQ(m.readings().gatedBlocks, 0u);
    EXPECT_EQ(m.readings().lra, kNotMeasured);
    feed(m, {{-80.0, 10.0}}, {}); // por debajo de la puerta absoluta
    EXPECT_EQ(m.readings().integrated, kNotMeasured);
    feed(m, {{-23.0, 10.0}, {-300.0, 60.0}}, {});
    EXPECT_NEAR(m.readings().integrated, -23.0, 0.15) << "el silencio posterior no baja el integrated";
}

TEST(Loudness, ResetClearsEverything)
{
    auto m = stereo();
    feed(m, {{-10.0, 10.0}}, {});
    ASSERT_TRUE(std::isfinite(m.readings().integrated));
    m.reset();
    const Readings &r = m.readings();
    EXPECT_EQ(r.integrated, kNotMeasured);
    EXPECT_EQ(r.lra, kNotMeasured);
    EXPECT_EQ(r.truePeakMax, kNotMeasured);
    EXPECT_EQ(r.momentaryMax, kNotMeasured);
    EXPECT_EQ(r.dr, kNotMeasured);
    EXPECT_EQ(m.historySize(), 0u);
    EXPECT_EQ(m.subBlocks(), 0u);
    feed(m, {{-30.0, 5.0}}, {});
    EXPECT_NEAR(m.readings().integrated, -30.0, 0.1) << "nada del audio anterior al reset";
    EXPECT_NEAR(m.readings().truePeakMax, -30.0, 0.2);
}

TEST(Loudness, BlockSizeDoesNotChangeResults)
{
    double ref = 0, refTp = 0;
    for (uint32_t block : {1u, 7u, 480u, 4096u}) {
        auto m = stereo();
        feed(m, {{-18.0, 4.0, 997.0}, {-30.0, 3.0, 3000.0}}, {}, block);
        if (block == 1) {
            ref = m.readings().integrated;
            refTp = m.readings().truePeakMax;
        } else {
            EXPECT_NEAR(m.readings().integrated, ref, 1e-6) << block;
            EXPECT_NEAR(m.readings().truePeakMax, refTp, 1e-6) << block;
        }
    }
}

// True peak: un seno a fs/4 desfasado 45° tiene muestras a 0,707 (−3,01 dBFS) y pico real 0 dBTP.
// Tolerancia de EBU Tech 3341 para el sobremuestreo ×4: +0,2 / −0,4 dB.
TEST(Loudness, TruePeakFindsInterSamplePeak)
{
    for (int factor : {4, 8}) {
        auto m = stereo(48000, factor);
        feed(m, {{0.0, 1.0, 12000.0}}, {}, 480, kPi / 4);
        EXPECT_NEAR(m.readings().samplePeakMax, -3.01, 0.05);
        EXPECT_GE(m.readings().truePeakMax, -0.4) << factor;
        EXPECT_LE(m.readings().truePeakMax, 0.2) << factor;
    }
    auto off = stereo(48000, 1);
    feed(off, {{0.0, 1.0, 12000.0}}, {}, 480, kPi / 4);
    EXPECT_NEAR(off.readings().truePeakMax, -3.01, 0.05) << "sin sobremuestreo, true peak = sample peak";
}

TEST(Loudness, TruePeakAcrossFrequencies)
{
    for (double f : {100.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0})
        for (double ph : {0.0, 0.3, 1.1}) {
            auto m = stereo(48000, 4);
            // 4 s y lectura de los últimos 3 s: sin el transitorio del arranque brusco del seno.
            feed(m, {{-6.0, 4.0, f}}, {}, 480, ph);
            EXPECT_GE(m.readings().truePeakRecent, -6.4) << f << " Hz";
            EXPECT_LE(m.readings().truePeakRecent, -5.8) << f << " Hz";
        }
}

TEST(Loudness, TruePeakFilterPhasesHaveUnityDcGain)
{
    TruePeakDetector d;
    d.configure(4, 16);
    ASSERT_EQ(d.phases().size(), 4u);
    for (const auto &ph : d.phases()) {
        double s = 0;
        for (float c : ph)
            s += c;
        EXPECT_NEAR(s, 1.0, 1e-5);
    }
    std::vector<float> dc(1000, 0.5f);
    d.process(dc.data(), 1000); // el escalón inicial puede sobrepasar (Gibbs)
    EXPECT_NEAR(d.process(dc.data(), 1000), 0.5f, 1e-3) << "continua estable: sin ganancia";
}

// Con un seno estable: true peak ≈ short-term + 0 dB -> PSR y PLR ≈ 0; DR = media de PSR.
TEST(Loudness, DynamicsReadings)
{
    auto m = stereo();
    feed(m, {{-23.0, 10.0}}, {});
    const Readings &r = m.readings();
    EXPECT_NEAR(r.truePeakMax, -23.0, 0.2);
    EXPECT_NEAR(r.plr, r.truePeakMax - r.integrated, 1e-9);
    EXPECT_NEAR(r.plr, 0.0, 0.3);
    EXPECT_NEAR(r.psr, 0.0, 0.3);
    EXPECT_NEAR(r.dr, 0.0, 0.3);
    EXPECT_NEAR(r.momentaryMax, -23.0, 0.1);
    EXPECT_NEAR(r.shortTermMax, -23.0, 0.1);
    EXPECT_NEAR(r.measuredSeconds, 10.0, 1e-9);
}

TEST(Loudness, PsrFollowsRecentPeakAndShortTerm)
{
    auto m = stereo();
    feed(m, {{-6.0, 5.0}, {-30.0, 5.0}}, {});
    const Readings &r = m.readings();
    EXPECT_NEAR(r.truePeakMax, -6.0, 0.2) << "el máximo se conserva";
    EXPECT_NEAR(r.truePeakRecent, -30.0, 0.2) << "los últimos 3 s ya no tienen el pico de −6";
    EXPECT_NEAR(r.psr, r.truePeakRecent - r.shortTerm, 1e-9);
}

TEST(Loudness, HistoryAndDistribution)
{
    MeterConfig c;
    c.historySeconds = 10;
    LoudnessMeter m(c);
    feed(m, {{-22.5, 15.0}}, {});
    EXPECT_EQ(m.historySize(), 100u) << "anillo de 10 s a 10 puntos por segundo";
    const auto h = m.history(1000);
    ASSERT_EQ(h.size(), 100u);
    EXPECT_NEAR(h.back().momentary, -22.5, 0.1);
    EXPECT_NEAR(h.back().shortTerm, -22.5, 0.1);
    EXPECT_NEAR(h.back().integrated, -22.5, 0.1);
    EXPECT_EQ(m.history(5).size(), 5u);
    const auto d = m.distribution(-60.0, 0.0);
    ASSERT_EQ(d.size(), 60u);
    uint64_t total = 0;
    for (auto v : d)
        total += v;
    EXPECT_EQ(total, 15u * 10u - 29u) << "un short-term cada 100 ms desde los 3 s";
    EXPECT_EQ(d[37], total) << "banda de −23 a −22 LUFS";
}

TEST(Loudness, GatedHistogramPercentiles)
{
    GatedHistogram h;
    for (int i = 0; i < 100; ++i)
        h.add(lufsToEnergy(-40.0 + i * 0.2)); // −40 … −20,2
    EXPECT_EQ(h.count(), 100u);
    EXPECT_NEAR(h.range(-100.0, 0.10, 0.95), 16.8, 0.05) << "P95 = −21,2; P10 = −38,0";
    h.add(lufsToEnergy(-75.0));
    EXPECT_EQ(h.count(), 100u) << "puerta absoluta";
    h.clear();
    EXPECT_EQ(h.gatedLoudness(-10.0), kNotMeasured);
}

TEST(Loudness, PresetsAndZones)
{
    ASSERT_NE(findPreset("ebu_r128"), nullptr);
    EXPECT_DOUBLE_EQ(findPreset("ebu_r128")->target, -23.0);
    EXPECT_DOUBLE_EQ(findPreset("ebu_r128")->truePeak, -1.0);
    EXPECT_DOUBLE_EQ(findPreset("atsc_a85")->target, -24.0);
    EXPECT_TRUE(findPreset("atsc_a85")->lkfs);
    EXPECT_DOUBLE_EQ(findPreset("streaming")->target, -14.0);
    EXPECT_DOUBLE_EQ(findPreset("youtube")->target, -14.0);
    EXPECT_DOUBLE_EQ(findPreset("podcast")->target, -16.0);
    EXPECT_DOUBLE_EQ(findPreset("netflix")->target, -27.0);
    EXPECT_NE(findPreset("custom"), nullptr);
    EXPECT_EQ(findPreset("nope"), nullptr);
    EXPECT_EQ(targetZone(-30.0, -23.0, 1.0), TargetZone::Below);
    EXPECT_EQ(targetZone(-23.4, -23.0, 1.0), TargetZone::Within);
    EXPECT_EQ(targetZone(-21.5, -23.0, 1.0), TargetZone::Above);
    EXPECT_EQ(targetZone(kNotMeasured, -23.0, 1.0), TargetZone::Below);
}

TEST(Loudness, Formatting)
{
    EXPECT_EQ(formatLoudness(-23.04), "\xE2\x88\x92" "23.0");
    EXPECT_EQ(formatLoudness(1.26, true), "+1.3");
    EXPECT_EQ(formatLoudness(0.0, true), "0.0");
    EXPECT_EQ(formatLoudness(kNotMeasured), "\xE2\x80\x94");
}

// Bajo consumo: 10 minutos de estéreo a 48 kHz con true peak ×4 en mucho menos que tiempo real.
TEST(Loudness, FastEnoughForRealTime)
{
    auto m = stereo(48000, 4);
    const auto t0 = std::chrono::steady_clock::now();
    feed(m, {{-20.0, 600.0}}, {}, 1024);
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(s, 60.0) << "10 min de audio en " << s << " s";
    RecordProperty("seconds_for_10_min", std::to_string(s));
    EXPECT_NEAR(m.readings().integrated, -20.0, 0.1);
}
