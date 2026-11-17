// Integración del núcleo sin OBS: sesión de fuente + hub + monitor + alertas.
// T02 (extremo a extremo con mock), T09/T52 (cambio de configuración), T10 (100 ciclos),
// T44/T45 simulados (cable y driver), T50 (estados diferenciados, sin falsa detección LV1).
#include "TestHelpers.h"
#include "core/SourceSession.h"

#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <thread>

using namespace sg;
using namespace sgtest;

namespace {

struct Collector {
    std::mutex mu;
    std::vector<float> left, right;
    uint32_t rate = 0;
    uint64_t lastTs = 0;
    bool monotonic = true;
    void operator()(const PcmBlock &b)
    {
        std::lock_guard lk(mu);
        left.insert(left.end(), b.planes[0], b.planes[0] + b.frames);
        if (b.channels > 1)
            right.insert(right.end(), b.planes[1], b.planes[1] + b.frames);
        rate = b.sampleRate;
        if (lastTs && b.timestampNs <= lastTs)
            monotonic = false;
        lastTs = b.timestampNs;
    }
    std::size_t size()
    {
        std::lock_guard lk(mu);
        return left.size();
    }
};

struct World {
    VirtualClock clock;
    MockNetworkTelemetry *nic = nullptr;
    std::unique_ptr<CoreRuntime> rt;
    World()
    {
        auto n = std::make_unique<MockNetworkTelemetry>();
        nic = n.get();
        nic->current.present = true;
        nic->current.linkUp = true;
        nic->current.speedBps = 1'000'000'000ull;
        nic->current.inErrors = 0;
        rt = std::make_unique<CoreRuntime>(clock, std::move(n), "", /*startThreads=*/false);
        MockConfig mc;
        mc.realtime = false;
        mc.channels = 16;
        rt->transports().setMockConfig(mc);
    }
    MockTransport *mockOf(SourceSession &s) { return dynamic_cast<MockTransport *>(&s.hub()->transport()); }
};

bool waitFor(const std::function<bool()> &pred, int ms = 3000)
{
    for (int i = 0; i < ms / 5; ++i) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

} // namespace

TEST(T02_Session, MockSineFlowsThroughOutputThreadWithMonotonicTimestamps)
{
    World w;
    Collector col;
    auto s = std::make_shared<SourceSession>(*w.rt, "Main L/R", [&](const PcmBlock &b) { col(b); });
    SourceConfig cfg;
    cfg.deviceId = "mock:default";
    cfg.map = ChannelMap::stereo(0, 1);
    s->apply(cfg);
    w.rt->transports().pollOnce();
    w.mockOf(*s)->pump(200);
    ASSERT_TRUE(waitFor([&] { return col.size() >= 48000; }));
    std::lock_guard lk(col.mu);
    EXPECT_EQ(dominant(col.left, {1000, 2000, 3000}, 48000), 1000.0);
    EXPECT_EQ(dominant(col.right, {1000, 2000, 3000}, 48000), 2000.0);
    EXPECT_EQ(col.rate, 48000u);
    EXPECT_TRUE(col.monotonic);
}

TEST(T52_Session, MapChangeAppliesWithoutReopeningDriver)
{
    World w;
    Collector col;
    auto s = std::make_shared<SourceSession>(*w.rt, "Bus", [&](const PcmBlock &b) { col(b); });
    SourceConfig cfg;
    cfg.deviceId = "mock:default";
    s->apply(cfg);
    w.rt->transports().pollOnce();
    MockTransport *m = w.mockOf(*s);
    m->pump(10);
    cfg.map = ChannelMap::stereo(6, 7);
    s->apply(cfg);
    w.rt->transports().pollOnce();
    EXPECT_EQ(m->opens(), 1u);
    {
        std::lock_guard lk(col.mu);
        col.left.clear();
        col.right.clear();
    }
    ASSERT_TRUE(waitFor([&] { return col.size() == 0; }));
    m->pump(100);
    ASSERT_TRUE(waitFor([&] { return col.size() >= 25000; }));
    std::lock_guard lk(col.mu);
    EXPECT_EQ(dominant(col.left, {1000, 7000, 8000}, 48000), 7000.0);
}

TEST(T44_Simulated, CablePullRaisesOneCorrelatedIncidentAndRecovers)
{
    World w;
    auto s = std::make_shared<SourceSession>(*w.rt, "Main L/R", nullptr);
    w.rt->monitor().add(s);
    SourceConfig cfg;
    cfg.deviceId = "mock:default";
    cfg.nicId = "mock-nic";
    cfg.nicName = "Mock Ethernet";
    s->apply(cfg);
    w.rt->transports().pollOnce();
    MockTransport *m = w.mockOf(*s);
    auto stepMs = [&](int ms, bool audio) {
        for (int t = 0; t < ms; t += 250) {
            const int64_t before = w.clock.nowNs();
            if (audio)
                m->pump(47); // ~250 ms de audio: avanza el reloj virtual si el stream está abierto
            if (w.clock.nowNs() == before)
                w.clock.advanceMs(250); // transporte cerrado (reconectando): el tiempo sigue
            w.rt->transports().pollOnce();
            w.rt->monitor().tickOnce();
        }
    };
    stepMs(5000, true);
    EXPECT_EQ(s->status().global, GlobalStatus::AudioOk);

    // Se desconecta el cable: la NIC cae y el driver deja de entregar audio.
    w.nic->current.linkUp = false;
    w.nic->current.speedBps.reset();
    m->setFault(MockFault::Stall);
    stepMs(4000, false);
    w.rt->dispatcher().drain();
    const auto hist = w.rt->history().recent(100);
    int primaries = 0;
    for (auto &e : hist)
        if (e.notify)
            ++primaries;
    EXPECT_EQ(primaries, 1);
    EXPECT_EQ(s->status().global, GlobalStatus::NoLink);
    EXPECT_NE(s->status().indicators.nic.find("DOWN"), std::string::npos);

    // Se reconecta.
    w.nic->current.linkUp = true;
    w.nic->current.speedBps = 1'000'000'000ull;
    m->setFault(MockFault::None);
    stepMs(12000, true);
    w.rt->dispatcher().drain();
    bool recovered = false;
    for (auto &e : w.rt->history().recent(200))
        recovered |= e.rule == RuleId::A10_Recovered && e.notify;
    EXPECT_TRUE(recovered);
    EXPECT_EQ(s->status().global, GlobalStatus::AudioOk);
    w.rt->monitor().remove(s.get());
}

TEST(T45_Simulated, DriverLossIsDistinguishedFromNetworkLoss)
{
    World w;
    auto s = std::make_shared<SourceSession>(*w.rt, "Main L/R", nullptr);
    w.rt->monitor().add(s);
    SourceConfig cfg;
    cfg.deviceId = "mock:default";
    cfg.nicId = "mock-nic";
    s->apply(cfg);
    w.rt->transports().pollOnce();
    MockTransport *m = w.mockOf(*s);
    for (int t = 0; t < 20; ++t) {
        m->pump(47);
        w.rt->transports().pollOnce();
        w.rt->monitor().tickOnce();
    }
    m->setFault(MockFault::Disconnect); // NIC sigue UP
    for (int t = 0; t < 12; ++t) {
        w.clock.advanceMs(250);
        w.rt->transports().pollOnce();
        w.rt->monitor().tickOnce();
    }
    w.rt->dispatcher().drain();
    bool a03 = false, a04 = false;
    for (auto &e : w.rt->history().recent(100)) {
        a03 |= e.rule == RuleId::A03_DriverLost && e.transition == "ACTIVATED" && e.notify;
        a04 |= e.rule == RuleId::A04_NoLink;
    }
    EXPECT_TRUE(a03);
    EXPECT_FALSE(a04);
    EXPECT_EQ(s->status().global, GlobalStatus::DriverUnavailable);
}

TEST(T50_Indicators, NicUpWithoutDriverNeverClaimsConsole)
{
    World w;
    auto s = std::make_shared<SourceSession>(*w.rt, "Sin dispositivo", nullptr);
    SourceConfig cfg;
    cfg.nicId = "mock-nic";
    s->apply(cfg);
    s->tick(w.clock.nowNs());
    const auto st = s->status();
    EXPECT_NE(st.indicators.nic.find("UP"), std::string::npos);
    EXPECT_EQ(st.indicators.driver, "Sin dispositivo");
    EXPECT_NE(st.indicators.lv1.find("No identificada"), std::string::npos);
    EXPECT_NE(st.indicators.patch.find("No verificado"), std::string::npos);

    cfg.deviceId = "mock:default";
    s->apply(cfg);
    w.rt->transports().pollOnce();
    s->tick(w.clock.nowNs());
    const auto st2 = s->status();
    EXPECT_NE(st2.indicators.deviceKind.find("Simulador"), std::string::npos);
    EXPECT_NE(st2.indicators.lv1.find("No identificada"), std::string::npos);
    EXPECT_NE(st2.indicators.latency.find("Extremo a extremo: NO DISPONIBLE"), std::string::npos);
}

TEST(T08_Session, InvalidMapIsReportedNotCrashing)
{
    World w;
    auto s = std::make_shared<SourceSession>(*w.rt, "x", nullptr);
    SourceConfig cfg;
    cfg.deviceId = "mock:default";
    cfg.map = ChannelMap::stereo(0, 40); // el mock tiene 16 canales
    s->apply(cfg);
    w.rt->transports().pollOnce();
    w.mockOf(*s)->pump(5);
    s->tick(w.clock.nowNs());
    EXPECT_NE(s->status().configError.find("fuera de rango"), std::string::npos);
}

TEST(T10_Session, HundredCreateDestroyCyclesWithRealtimeMock)
{
    SteadyClock &clock = SteadyClock::instance();
    auto rt = std::make_unique<CoreRuntime>(clock, std::make_unique<MockNetworkTelemetry>(), "", true);
    MockConfig mc;
    mc.realtime = true;
    rt->transports().setMockConfig(mc);
    std::atomic<uint64_t> blocks{0};
    for (int i = 0; i < 100; ++i) {
        auto s = std::make_shared<SourceSession>(*rt, "f" + std::to_string(i),
                                                 [&](const PcmBlock &) { blocks.fetch_add(1); });
        rt->monitor().add(s);
        SourceConfig cfg;
        cfg.deviceId = "mock:default";
        cfg.map = ChannelMap::stereo(static_cast<int16_t>(i % 7), static_cast<int16_t>(i % 7 + 1));
        s->apply(cfg);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        rt->monitor().remove(s.get());
    }
    // cada ciclo libera el dispositivo (el monitor puede soltar la última referencia un tick después)
    EXPECT_TRUE(waitFor([&] { return rt->transports().hubs().empty(); }));
    rt->shutdown();
}
