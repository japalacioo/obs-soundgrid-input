// Monitor de audio del Multiview: núcleo sin libobs (src/monitor/).
#include "monitor/AudioScope.h"
#include "monitor/SceneAudioPicker.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace sg::monitor;

namespace {

constexpr int64_t kMs = 1'000'000;
constexpr int64_t kSec = 1'000'000'000;
constexpr uint32_t kRate = 48000;

// Empuja `ms` milisegundos de seno (amplitudes por canal) en bloques de 256 frames desde t0.
int64_t pushSine(AudioScopeTap &tap, int64_t t0, int ms, float ampL, float ampR, uint32_t channels = 2,
                 double hz = 1000.0)
{
    const uint32_t total = static_cast<uint32_t>(kRate * ms / 1000);
    std::vector<float> l(256), r(256);
    uint32_t done = 0;
    while (done < total) {
        const uint32_t n = std::min<uint32_t>(256, total - done);
        for (uint32_t i = 0; i < n; ++i) {
            const double ph = 2 * std::numbers::pi * hz * (done + i) / kRate;
            l[i] = static_cast<float>(ampL * std::sin(ph));
            r[i] = static_cast<float>(ampR * std::sin(ph));
        }
        const float *planes[2] = {l.data(), r.data()};
        tap.push(planes, channels, n, kRate, static_cast<uint64_t>(t0 + int64_t(done) * kSec / kRate));
        done += n;
    }
    return t0 + int64_t(total) * kSec / kRate;
}

} // namespace

// ------------------------------------------------------------------ escalas y formato

TEST(AudioMonitor_Scale, DbToFractionIsLinearInDbLikeObs)
{
    EXPECT_DOUBLE_EQ(dbToFraction(0.0), 1.0);
    EXPECT_DOUBLE_EQ(dbToFraction(-60.0), 0.0);
    EXPECT_DOUBLE_EQ(dbToFraction(-30.0), 0.5);
    EXPECT_DOUBLE_EQ(dbToFraction(+3.0), 1.0);
    EXPECT_DOUBLE_EQ(dbToFraction(kNegInf), 0.0);
    EXPECT_DOUBLE_EQ(dbToFraction(std::nan("")), 0.0);
}

TEST(AudioMonitor_Scale, AmplitudeToDbfs)
{
    EXPECT_NEAR(ampToDb(1.0), 0.0, 1e-9);
    EXPECT_NEAR(ampToDb(0.5), -6.0206, 1e-3);
    EXPECT_TRUE(std::isinf(ampToDb(0.0)));
}

TEST(AudioMonitor_Scale, FormatUsesTypographicMinusAndInfinity)
{
    EXPECT_EQ(formatDb(-12.34), "\xE2\x88\x92" "12.3");
    EXPECT_EQ(formatDb(-0.01), "0.0");
    EXPECT_EQ(formatDb(1.25), "1.2");
    EXPECT_EQ(formatDb(kNegInf), "\xE2\x88\x92\xE2\x88\x9E");
    EXPECT_EQ(formatDb(-150.0), "\xE2\x88\x92\xE2\x88\x9E");
}

TEST(AudioMonitor_Scale, TicksEvery5dBWithLabelsAndMinorTicks)
{
    const auto t = scaleTicks();
    ASSERT_EQ(t.size(), 61u); // 0..-60 de 1 en 1 dB
    int majors = 0;
    for (const auto &k : t) {
        EXPECT_GE(k.fraction, 0.0);
        EXPECT_LE(k.fraction, 1.0);
        if (k.major) {
            ++majors;
            EXPECT_FALSE(k.label.empty());
        } else {
            EXPECT_TRUE(k.label.empty());
        }
    }
    EXPECT_EQ(majors, 13);
    EXPECT_EQ(t.front().label, "0");
    EXPECT_DOUBLE_EQ(t.front().fraction, 1.0);
    EXPECT_EQ(t.back().label, "\xE2\x88\x92" "60");
    EXPECT_DOUBLE_EQ(t.back().fraction, 0.0);
    EXPECT_EQ(t[20].label, "\xE2\x88\x92" "20");
}

TEST(AudioMonitor_Scale, ZonesAreGreenYellowRedAtObsLevels)
{
    EXPECT_EQ(zoneFor(-30), Zone::Nominal);
    EXPECT_EQ(zoneFor(-20), Zone::Warning);
    EXPECT_EQ(zoneFor(-12), Zone::Warning);
    EXPECT_EQ(zoneFor(-9), Zone::Error);
    EXPECT_EQ(zoneFor(0), Zone::Error);

    const auto z = meterZones();
    ASSERT_EQ(z.size(), 3u);
    EXPECT_EQ(z[0].zone, Zone::Nominal);
    EXPECT_DOUBLE_EQ(z[0].from, 0.0);
    EXPECT_NEAR(z[0].to, dbToFraction(-20), 1e-12);
    EXPECT_EQ(z[1].zone, Zone::Warning);
    EXPECT_NEAR(z[1].to, dbToFraction(-9), 1e-12);
    EXPECT_EQ(z[2].zone, Zone::Error);
    EXPECT_DOUBLE_EQ(z[2].to, 1.0);
}

TEST(AudioMonitor_Scale, LitZonesFollowThePeak)
{
    EXPECT_TRUE(litZones(kNegInf).empty());
    auto g = litZones(-30);
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].zone, Zone::Nominal);
    EXPECT_NEAR(g[0].to, 0.5, 1e-12);

    auto y = litZones(-15);
    ASSERT_EQ(y.size(), 2u);
    EXPECT_EQ(y[1].zone, Zone::Warning);
    EXPECT_NEAR(y[1].to, dbToFraction(-15), 1e-12);

    auto r = litZones(-3);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[2].zone, Zone::Error);
    EXPECT_NEAR(r[2].to, dbToFraction(-3), 1e-12);
}

// ------------------------------------------------------------------ historial y niveles

TEST(AudioMonitor_Tap, SineLevelsMatchTheory)
{
    AudioScopeTap tap;
    const int64_t t0 = 10 * kSec;
    const int64_t end = pushSine(tap, t0, 1000, 0.5f, 0.1f);
    const auto snap = tap.snapshot(0);
    EXPECT_EQ(snap.sampleRate, kRate);
    EXPECT_EQ(snap.inputChannels, 2u);
    EXPECT_EQ(snap.lastDataNs, end);
    EXPECT_EQ(snap.totalFrames, kRate);

    const Levels lv = computeLevels(snap.buckets, end - 300 * kMs, end);
    EXPECT_NEAR(lv.peakDb[0], -6.02, 0.05);
    EXPECT_NEAR(lv.peakDb[1], -20.0, 0.05);
    EXPECT_NEAR(lv.rmsDb[0], -6.02 - 3.01, 0.05); // seno: RMS = pico − 3,01 dB
    EXPECT_NEAR(lv.rmsDb[1], -20.0 - 3.01, 0.05);
    EXPECT_NEAR(static_cast<double>(lv.frames), kRate * 0.3, kRate * 0.01);
}

TEST(AudioMonitor_Tap, BucketsCover5msAndKeepMinMax)
{
    AudioScopeTap tap;
    const int64_t end = pushSine(tap, 0, 100, 0.8f, 0.4f);
    const auto snap = tap.snapshot(0);
    ASSERT_EQ(snap.buckets.size(), 20u); // 100 ms / 5 ms
    for (std::size_t i = 0; i < snap.buckets.size(); ++i) {
        const auto &b = snap.buckets[i];
        EXPECT_EQ(b.frames, 240u);
        EXPECT_EQ(b.endNs - b.startNs, 5 * kMs);
        EXPECT_NEAR(b.max[0], 0.8f, 0.01f);
        EXPECT_NEAR(b.min[0], -0.8f, 0.01f);
        EXPECT_NEAR(b.max[1], 0.4f, 0.01f);
        if (i)
            EXPECT_EQ(b.startNs, snap.buckets[i - 1].endNs);
    }
    EXPECT_EQ(snap.buckets.back().endNs, end);
}

TEST(AudioMonitor_Tap, MonoIsShownOnBothChannels)
{
    AudioScopeTap tap;
    const int64_t end = pushSine(tap, 0, 200, 0.25f, 0.0f, /*channels=*/1);
    const auto snap = tap.snapshot(0);
    EXPECT_EQ(snap.inputChannels, 1u);
    const Levels lv = computeLevels(snap.buckets, 0, end);
    EXPECT_NEAR(lv.peakDb[0], lv.peakDb[1], 1e-9);
    EXPECT_NEAR(lv.peakDb[0], -12.04, 0.05);
}

TEST(AudioMonitor_Tap, HistoryIsBoundedAndSnapshotFiltersByTime)
{
    TapConfig cfg;
    cfg.historyNs = 1 * kSec;
    AudioScopeTap tap(cfg);
    const int64_t end = pushSine(tap, 0, 3000, 0.5f, 0.5f);
    const auto all = tap.snapshot(0);
    EXPECT_LE(all.buckets.size(), 201u);
    EXPECT_GE(all.buckets.front().startNs, end - 1 * kSec - 5 * kMs);
    const auto last = tap.snapshot(end - 100 * kMs);
    EXPECT_EQ(last.buckets.size(), 20u);
}

TEST(AudioMonitor_Tap, GapStartsANewBucketAndLeavesEmptyTime)
{
    AudioScopeTap tap;
    int64_t t = pushSine(tap, 0, 12, 0.5f, 0.5f); // 2 cubos y medio
    const int64_t gapStart = t;
    t = pushSine(tap, t + 500 * kMs, 10, 0.5f, 0.5f);
    const auto snap = tap.snapshot(0);
    bool sawGap = false;
    for (std::size_t i = 1; i < snap.buckets.size(); ++i) {
        EXPECT_LE(snap.buckets[i - 1].endNs, snap.buckets[i].startNs);
        if (snap.buckets[i].startNs - snap.buckets[i - 1].endNs >= 400 * kMs)
            sawGap = true;
    }
    EXPECT_TRUE(sawGap);
    const Levels none = computeLevels(snap.buckets, gapStart + 50 * kMs, gapStart + 450 * kMs);
    EXPECT_EQ(none.frames, 0u);
    EXPECT_TRUE(std::isinf(none.peakDb[0]));
    EXPECT_TRUE(std::isinf(none.rmsDb[1]));
}

TEST(AudioMonitor_Tap, PartialBucketIsVisibleImmediately)
{
    AudioScopeTap tap;
    std::vector<float> s(64, 0.9f);
    const float *p[1] = {s.data()};
    tap.push(p, 1, 64, kRate, 1000);
    const auto snap = tap.snapshot(0);
    ASSERT_EQ(snap.buckets.size(), 1u);
    EXPECT_EQ(snap.buckets[0].frames, 64u);
    EXPECT_NEAR(snap.buckets[0].peak[1], 0.9f, 1e-6);
}

TEST(AudioMonitor_Tap, IgnoresInvalidInputAndResets)
{
    AudioScopeTap tap;
    tap.push(nullptr, 2, 10, kRate, 0);
    std::vector<float> s(10, 0.1f);
    const float *p[1] = {s.data()};
    tap.push(p, 1, 10, 0, 0);
    tap.push(p, 0, 10, kRate, 0);
    EXPECT_EQ(tap.snapshot(0).totalFrames, 0u);
    tap.push(p, 1, 10, kRate, 0);
    EXPECT_EQ(tap.snapshot(0).totalFrames, 10u);
    tap.reset();
    EXPECT_TRUE(tap.snapshot(0).buckets.empty());
    EXPECT_EQ(tap.snapshot(0).lastDataNs, 0);
}

// ------------------------------------------------------------------ medidor

TEST(AudioMonitor_Ballistics, AttackIsInstantAndDecayFollowsObsRate)
{
    MeterBallistics m;
    int64_t t = kSec;
    m.update(t, -6.0, -9.0);
    EXPECT_DOUBLE_EQ(m.displayPeakDb(), -6.0);
    EXPECT_DOUBLE_EQ(m.displayRmsDb(), -9.0);
    t += kSec; // 1 s de silencio: cae 11,76 dB
    m.update(t, kNegInf, kNegInf);
    EXPECT_NEAR(m.displayPeakDb(), -6.0 - 11.76, 1e-9);
    m.update(t + 100 * kMs, -3.0, -6.0);
    EXPECT_DOUBLE_EQ(m.displayPeakDb(), -3.0);
}

TEST(AudioMonitor_Ballistics, DecayStopsAtTheInputLevel)
{
    MeterBallistics m;
    m.update(kSec, -6.0, -9.0);
    m.update(5 * kSec, -30.0, -33.0);
    EXPECT_DOUBLE_EQ(m.displayPeakDb(), -30.0);
}

TEST(AudioMonitor_Ballistics, FallsOffTheBottomOfTheScale)
{
    MeterBallistics m;
    m.update(kSec, -50.0, -53.0);
    m.update(3 * kSec, kNegInf, kNegInf);
    EXPECT_TRUE(std::isinf(m.displayPeakDb()));
    EXPECT_DOUBLE_EQ(dbToFraction(m.displayPeakDb()), 0.0);
}

TEST(AudioMonitor_Ballistics, PeakHoldLasts20SecondsThenFollows)
{
    MeterBallistics m;
    m.update(kSec, -4.0, -7.0);
    m.update(2 * kSec, -30.0, -33.0);
    EXPECT_DOUBLE_EQ(m.holdDb(), -4.0);
    m.update(20 * kSec, -30.0, -33.0);
    EXPECT_DOUBLE_EQ(m.holdDb(), -4.0);
    m.update(22 * kSec, -30.0, -33.0); // 21 s después del pico
    EXPECT_DOUBLE_EQ(m.holdDb(), -30.0);
    m.update(23 * kSec, -10.0, -13.0); // un pico mayor reemplaza el hold al instante
    EXPECT_DOUBLE_EQ(m.holdDb(), -10.0);
}

TEST(AudioMonitor_Ballistics, ClipLatchesForHoldTimeAndCountsEvents)
{
    MeterBallistics m;
    m.update(kSec, -1.0, -4.0);
    EXPECT_FALSE(m.clipActive(kSec));
    m.update(2 * kSec, -0.05, -3.0); // por encima de −0,1 dBFS
    EXPECT_TRUE(m.clipActive(2 * kSec));
    m.update(3 * kSec, -0.0, -3.0); // mismo episodio
    m.update(4 * kSec, -20.0, -23.0);
    EXPECT_TRUE(m.clipActive(7 * kSec + 900 * kMs)); // 5 s tras el último clip (3 s)
    EXPECT_FALSE(m.clipActive(8 * kSec + 100 * kMs));
    EXPECT_EQ(m.clipEvents(), 1u);
    m.update(9 * kSec, 0.0, -3.0);
    EXPECT_EQ(m.clipEvents(), 2u);
    m.reset();
    EXPECT_FALSE(m.clipActive(9 * kSec));
    EXPECT_EQ(m.clipEvents(), 0u);
}

// ------------------------------------------------------------------ osciloscopio

TEST(AudioMonitor_Waveform, NewestAudioIsAtTheRightEdge)
{
    AudioScopeTap tap;
    // 4 s de silencio y luego 1 s de señal fuerte.
    int64_t t = pushSine(tap, 0, 4000, 0.0f, 0.0f);
    t = pushSine(tap, t, 1000, 0.9f, 0.3f);
    const auto cols = waveformColumns(tap.snapshot(0).buckets, t, 5 * kSec, 500);
    ASSERT_EQ(cols.size(), 500u);
    for (int c = 0; c < 390; ++c)
        EXPECT_NEAR(cols[c][0].max, 0.0f, 1e-6) << c;
    for (int c = 405; c < 500; ++c) {
        ASSERT_TRUE(cols[c][0].has) << c;
        EXPECT_GT(cols[c][0].max, 0.85f) << c;
        EXPECT_LT(cols[c][0].min, -0.85f) << c;
        EXPECT_NEAR(cols[c][1].max, 0.3f, 0.02f) << c;
    }
}

TEST(AudioMonitor_Waveform, ScrollsRightToLeftAsTimeAdvances)
{
    AudioScopeTap tap;
    int64_t t = pushSine(tap, 0, 3000, 0.0f, 0.0f);
    const int64_t burst = t;
    t = pushSine(tap, t, 100, 0.9f, 0.9f); // ráfaga de 100 ms
    t = pushSine(tap, t, 900, 0.0f, 0.0f);
    const auto snap = tap.snapshot(0);
    auto burstColumn = [&](int64_t now) {
        const auto cols = waveformColumns(snap.buckets, now, 5 * kSec, 1000);
        for (int c = 0; c < 1000; ++c)
            if (cols[c][0].has && cols[c][0].max > 0.5f)
                return c;
        return -1;
    };
    const int a = burstColumn(t);
    const int b = burstColumn(t + 1 * kSec);
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    EXPECT_NEAR(a, (burst - (t - 5 * kSec)) * 1000 / (5 * kSec), 1);
    EXPECT_NEAR(a - b, 200, 1); // 1 s = 1/5 del ancho
}

TEST(AudioMonitor_Waveform, NoAudioMeansEmptyColumnsThatKeepMoving)
{
    AudioScopeTap tap;
    const int64_t end = pushSine(tap, 0, 1000, 0.5f, 0.5f);
    // 2 s después sin audio: los últimos 2/5 de la pantalla están vacíos.
    const auto cols = waveformColumns(tap.snapshot(0).buckets, end + 2 * kSec, 5 * kSec, 500);
    for (int c = 301; c < 500; ++c)
        EXPECT_FALSE(cols[c][0].has) << c;
    EXPECT_TRUE(cols[250][0].has);
    EXPECT_FALSE(cols[100][0].has); // antes de que empezara el audio
}

TEST(AudioMonitor_Waveform, NarrowColumnsHaveNoHoles)
{
    AudioScopeTap tap;
    const int64_t end = pushSine(tap, 0, 2000, 0.5f, 0.5f);
    // 2 s en 3000 columnas (0,67 ms por columna, menos que un cubo de 5 ms).
    const auto cols = waveformColumns(tap.snapshot(0).buckets, end, 2 * kSec, 3000);
    for (int c = 0; c < 3000; ++c)
        EXPECT_TRUE(cols[c][0].has) << c;
}

TEST(AudioMonitor_Waveform, DegenerateArguments)
{
    EXPECT_TRUE(waveformColumns({}, 0, kSec, 0).empty());
    const auto c = waveformColumns({}, 0, 0, 10);
    ASSERT_EQ(c.size(), 10u);
    EXPECT_FALSE(c[0][0].has);
}

// ------------------------------------------------------------------ distribución

TEST(AudioMonitor_Layout, FitsCommonCanvasesWithoutOverlaps)
{
    const std::pair<float, float> sizes[] = {{1920, 1080}, {1280, 720}, {3840, 2160}, {1080, 1920}, {640, 360}};
    for (auto [w, h] : sizes) {
        SCOPED_TRACE(std::to_string(int(w)) + "x" + std::to_string(int(h)));
        const MonitorLayout l = computeLayout(w, h);
        const Rect all[] = {l.header, l.label, l.clipBadge, l.meterPanel, l.meters[0], l.meters[1], l.scale,
                            l.scopePanel, l.lanes[0], l.lanes[1], l.readout[0], l.readout[1], l.timeAxis,
                            l.holdText[0], l.holdText[1], l.channelText[0], l.channelText[1]};
        for (const Rect &r : all) {
            EXPECT_TRUE(l.canvas.contains(r));
            EXPECT_GT(r.w, 0);
            EXPECT_GT(r.h, 0);
        }
        EXPECT_TRUE(l.header.contains(l.label));
        EXPECT_TRUE(l.header.contains(l.clipBadge));
        EXPECT_FALSE(l.label.overlaps(l.clipBadge));
        EXPECT_FALSE(l.meterPanel.overlaps(l.scopePanel));
        EXPECT_FALSE(l.meters[0].overlaps(l.scale));
        EXPECT_FALSE(l.meters[1].overlaps(l.scale));
        EXPECT_LT(l.meters[0].x, l.meters[1].x); // L a la izquierda
        EXPECT_FALSE(l.lanes[0].overlaps(l.lanes[1]));
        EXPECT_LT(l.lanes[0].y, l.lanes[1].y); // L arriba
        for (int i = 0; i < 2; ++i) {
            EXPECT_TRUE(l.scopePanel.contains(l.lanes[i]));
            EXPECT_TRUE(l.meterPanel.contains(l.meters[i]));
            EXPECT_FALSE(l.readout[i].overlaps(l.lanes[i]));
        }
        // El Multiview dibuja el nombre de la escena abajo: zona libre.
        EXPECT_LE(l.meterPanel.bottom(), h * 0.83f + 0.5f);
        EXPECT_LE(l.scopePanel.bottom(), h * 0.83f + 0.5f);
        EXPECT_GT(l.fontPx, l.smallFontPx);
        EXPECT_GT(l.smallFontPx, 0);
    }
}

TEST(AudioMonitor_Layout, ScopeGetsMostOfTheWidth)
{
    const MonitorLayout l = computeLayout(1920, 1080);
    EXPECT_GT(l.scopePanel.w, 1920 * 0.6f);
    EXPECT_GT(l.meters[0].h, 1080 * 0.4f);
}

// ------------------------------------------------------------------ selección por escena

namespace {
SceneItemNode item(const std::string &id, const std::string &name, bool visible = true,
                   std::shared_ptr<SceneNode> child = nullptr)
{
    return {id, name, visible, std::move(child)};
}
} // namespace

TEST(AudioMonitor_Picker, PicksTheTopmostVisibleSoundGridSource)
{
    SceneNode s{"Cámara 1",
                {item("image_source", "Logo"), item("soundgrid_input", "SG Cámara 1"),
                 item("soundgrid_input", "SG Stems")}};
    auto r = pickSoundGridSource(s);
    EXPECT_EQ(r.sourceName, "SG Cámara 1");
    EXPECT_EQ(r.matches, 2);
}

TEST(AudioMonitor_Picker, SkipsHiddenItems)
{
    SceneNode s{"Entrevista", {item("soundgrid_input", "SG oculto", false), item("soundgrid_input", "SG visible")}};
    EXPECT_EQ(pickSoundGridSource(s).sourceName, "SG visible");
    EXPECT_EQ(pickSoundGridSource(s).matches, 1);
}

TEST(AudioMonitor_Picker, FindsSourcesInsideGroupsAndNestedScenes)
{
    auto group = std::make_shared<SceneNode>(SceneNode{"Grupo", {item("soundgrid_input", "SG en grupo")}});
    auto nested = std::make_shared<SceneNode>(SceneNode{"Base", {item("soundgrid_input", "SG anidado")}});
    SceneNode s{"Show", {item("group", "Grupo", true, group), item("scene", "Base", true, nested)}};
    auto r = pickSoundGridSource(s);
    EXPECT_EQ(r.sourceName, "SG en grupo");
    EXPECT_EQ(r.matches, 2);

    SceneNode hiddenGroup{"Show", {item("group", "Grupo", false, group), item("scene", "Base", true, nested)}};
    EXPECT_EQ(pickSoundGridSource(hiddenGroup).sourceName, "SG anidado");
}

TEST(AudioMonitor_Picker, NoSourceAndCycles)
{
    SceneNode empty{"Vacía", {item("color_source", "Fondo")}};
    EXPECT_TRUE(pickSoundGridSource(empty).sourceName.empty());
    EXPECT_EQ(pickSoundGridSource(empty).matches, 0);

    auto a = std::make_shared<SceneNode>();
    a->name = "A";
    a->items.push_back(item("scene", "A", true, a)); // ciclo
    EXPECT_TRUE(pickSoundGridSource(*a).sourceName.empty());
    a->items.clear(); // romper el ciclo de shared_ptr
}

// ------------------------------------------------------------------ Multiview

TEST(AudioMonitor_Multiview, SlotsPerObsLayout)
{
    EXPECT_EQ(multiviewSceneSlots(0), 8);
    EXPECT_EQ(multiviewSceneSlots(1), 8);
    EXPECT_EQ(multiviewSceneSlots(2), 8);
    EXPECT_EQ(multiviewSceneSlots(3), 8);
    EXPECT_EQ(multiviewSceneSlots(4), 24);
    EXPECT_EQ(multiviewSceneSlots(5), 18);
    EXPECT_EQ(multiviewSceneSlots(6), 4);
    EXPECT_EQ(multiviewSceneSlots(7), 9);
    EXPECT_EQ(multiviewSceneSlots(8), 16);
    EXPECT_EQ(multiviewSceneSlots(9), 25);
    EXPECT_EQ(multiviewSceneSlots(42), 8);
}

namespace {
std::vector<SceneEntry> scenes(int n, int monitorAt = -1)
{
    std::vector<SceneEntry> v;
    for (int i = 0; i < n; ++i)
        v.push_back({"Escena " + std::to_string(i + 1), true, false});
    if (monitorAt >= 0)
        v.insert(v.begin() + monitorAt, SceneEntry{"Monitor", true, true});
    return v;
}
} // namespace

TEST(AudioMonitor_Multiview, NewMonitorAtTheEndOfTenScenesGoesToPosition8)
{
    const auto v = scenes(10, 10); // creado al final de la lista
    const auto p = planMonitorPlacement(v, 0);
    EXPECT_EQ(p.slots, 8);
    EXPECT_EQ(p.currentCell, 0); // fuera del Multiview
    EXPECT_EQ(p.targetPosition, 8);
    EXPECT_TRUE(p.enoughScenes);
    EXPECT_FALSE(p.alreadyPlaced);
    EXPECT_EQ(p.displaced, "Escena 8");
    EXPECT_NE(describePlan(p, 0, "Monitor").find("posición 8"), std::string::npos);
}

TEST(AudioMonitor_Multiview, AlreadyInTheLastCell)
{
    const auto p = planMonitorPlacement(scenes(10, 7), 0);
    EXPECT_EQ(p.currentCell, 8);
    EXPECT_TRUE(p.alreadyPlaced);
    EXPECT_NE(describePlan(p, 0, "Monitor").find("ya ocupa"), std::string::npos);
}

TEST(AudioMonitor_Multiview, HiddenScenesDoNotCountAsCells)
{
    auto v = scenes(10, 10);
    v[1].showInMultiview = false; // "Escena 2" oculta en el Multiview
    const auto p = planMonitorPlacement(v, 0);
    EXPECT_EQ(p.targetPosition, 9); // detrás de la "Escena 8", 7.ª visible
    EXPECT_EQ(p.displaced, "Escena 9");
}

TEST(AudioMonitor_Multiview, FewerScenesThanCells)
{
    const auto p = planMonitorPlacement(scenes(5, 5), 0);
    EXPECT_FALSE(p.enoughScenes);
    EXPECT_EQ(p.currentCell, 6);
    EXPECT_TRUE(p.alreadyPlaced);
    EXPECT_NE(describePlan(p, 0, "Monitor").find("menos escenas"), std::string::npos);
}

TEST(AudioMonitor_Multiview, OtherLayoutsAndWarnings)
{
    const auto p = planMonitorPlacement(scenes(30, 0), 9); // 25 recuadros, monitor primero
    EXPECT_EQ(p.slots, 25);
    EXPECT_EQ(p.currentCell, 1);
    EXPECT_EQ(p.targetPosition, 25);
    EXPECT_FALSE(p.alreadyPlaced);
    EXPECT_NE(describePlan(planMonitorPlacement(scenes(9, 8), 1), 1, "M").find("no es la esquina"), std::string::npos);
    EXPECT_EQ(describePlan(planMonitorPlacement(scenes(9, 8), 0), 0, "M").find("no es la esquina"), std::string::npos);
}
