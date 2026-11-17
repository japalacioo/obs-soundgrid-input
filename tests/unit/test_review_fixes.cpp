// Regresiones de la revisión de código del 17-nov-2026 (ver docs/REVIEW_2026-11-17.md).
#include "TestHelpers.h"
#include "core/SourceSession.h"
#include "transport/DeviceHub.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace sg;
using namespace sgtest;

// R2: tras shutdown() la sesión no vuelve a llamar al callback de salida aunque otro hilo
// (el monitor) conserve una referencia.
TEST(Review_R2, ShutdownStopsOutputEvenIfReferencesRemain)
{
    CoreRuntime rt(SteadyClock::instance(), std::make_unique<MockNetworkTelemetry>(), "", true);
    MockConfig mc;
    mc.realtime = true;
    rt.transports().setMockConfig(mc);
    std::atomic<uint64_t> calls{0};
    auto s = std::make_shared<SourceSession>(rt, "r2", [&](const PcmBlock &) { calls.fetch_add(1); });
    rt.monitor().add(s);
    SourceConfig cfg;
    cfg.deviceId = "mock:default";
    s->apply(cfg);
    for (int i = 0; i < 200 && calls.load() < 10; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_GE(calls.load(), 10u);
    std::shared_ptr<SourceSession> keep = s; // simula la copia que tiene el monitor
    s->shutdown();
    const uint64_t after = calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(calls.load(), after);
    EXPECT_TRUE(keep->tick(SteadyClock::instance().nowNs()).empty()); // monitor inerte
    keep->apply(cfg);                                                 // no reabre tras el cierre
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(calls.load(), after);
    rt.monitor().remove(s.get());
    keep.reset();
    s.reset();
    rt.shutdown();
}

// R9: un dispositivo intermitente (abre y se bloquea una y otra vez) hace crecer el backoff y
// termina en "reintentos agotados" en lugar de reabrirse cada pocos segundos para siempre.
TEST(Review_R9, FlappingDeviceBacksOffAndEventuallyGivesUp)
{
    VirtualClock clock;
    MockConfig mc;
    mc.realtime = false;
    RetryPolicy pol;
    pol.initialDelayMs = 1000;
    pol.maxDelayMs = 8000;
    pol.jitter = 0;
    pol.stallTimeoutMs = 3000;
    pol.maxAttempts = 4;
    auto t = std::make_unique<MockTransport>(mc, &clock);
    MockTransport *mock = t.get();
    DeviceHub hub(std::move(t), "mock:default", clock, pol);
    auto sub = hub.subscribe(ChannelMap::stereo(0, 1));
    hub.poll();
    mock->setFault(MockFault::Stall); // abre bien pero nunca entrega audio
    std::vector<int64_t> delays;
    int64_t prevNext = 0;
    for (int i = 0; i < 1200 && !hub.status().gaveUp; ++i) { // hasta 120 s virtuales
        clock.advanceMs(100);
        hub.poll();
        const int64_t n = hub.status().nextRetryNs;
        if (n && n != prevNext) {
            delays.push_back((n - clock.nowNs()) / kNsPerMs);
            prevNext = n;
        }
    }
    ASSERT_GE(delays.size(), 3u);
    EXPECT_NEAR(static_cast<double>(delays[0]), 1000, 1);
    EXPECT_NEAR(static_cast<double>(delays[1]), 2000, 1);
    EXPECT_NEAR(static_cast<double>(delays[2]), 4000, 1);
    EXPECT_TRUE(hub.status().gaveUp);
    EXPECT_EQ(hub.status().state, TransportState::Error);
    EXPECT_NE(hub.status().lastOpenErrorMessage.find("Reconectar"), std::string::npos);
    mock->setFault(MockFault::None);
    hub.requestReconnect();
    hub.poll();
    EXPECT_EQ(hub.status().state, TransportState::Streaming);
    EXPECT_FALSE(hub.status().gaveUp);
}

// R12: un desborde se cuenta una sola vez (overflows), aunque el bloque siguiente lleve la marca
// de discontinuidad para OBS; y tras una REapertura el primer bloque se marca como discontinuo.
TEST(Review_R12, OverflowCountedOnceAndReopenMarked)
{
    VirtualClock clock;
    MockConfig mc;
    mc.realtime = false;
    RetryPolicy pol;
    pol.initialDelayMs = 50;
    pol.jitter = 0;
    auto t = std::make_unique<MockTransport>(mc, &clock);
    MockTransport *mock = t.get();
    DeviceHub hub(std::move(t), "mock:default", clock, pol);
    auto sub = hub.subscribe(ChannelMap::stereo(0, 1));
    hub.poll();
    mock->pump(Subscription::kSlots + 5);
    EXPECT_EQ(sub->stats().overflows.load(), 5u);
    auto drain = [&] {
        uint32_t f = 0;
        while (const PcmBlock *b = sub->peek()) {
            f |= b->flags;
            sub->release(0);
        }
        return f;
    };
    drain();
    mock->pump(1);
    EXPECT_TRUE(drain() & kBlockDiscontinuity);           // OBS sabe que hubo hueco
    EXPECT_EQ(sub->stats().discontinuities.load(), 0u);    // pero no se cuenta dos veces
    hub.requestReconnect();
    hub.poll();
    mock->pump(1);
    EXPECT_TRUE(drain() & kBlockDiscontinuity);            // primer bloque tras reabrir
    EXPECT_EQ(sub->stats().discontinuities.load(), 1u);
}

// R5: un incidente que reaparece dentro del cooldown como PRINCIPAL se notifica si antes no se
// había notificado (por ser secundario).
TEST(Review_R5, ReopenAsPrimaryAfterSecondaryIsNotified)
{
    AlertEngine eng(AlertConfig::defaults());
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.transportState = TransportState::Error; // A03 raíz
    sim.framesFlowing = false;                    // A02 consecuencia
    run(eng, sim, log, 2000);
    ASSERT_EQ(log.count(RuleId::A02_StreamInterrupted, "ACTIVATED", 0), 1);
    sim.s.transportState = TransportState::Streaming;
    sim.framesFlowing = true;
    run(eng, sim, log, 6000); // ambos se resuelven
    sim.framesFlowing = false; // ahora el flujo se corta SIN fallo del driver (< cooldown)
    run(eng, sim, log, 1500);
    EXPECT_EQ(log.count(RuleId::A02_StreamInterrupted, "REOPENED", 1), 1);
}

// R6: si la causa raíz se cierra y la consecuencia sigue mal, la consecuencia pasa a principal.
TEST(Review_R6, OrphanedConsequenceIsPromoted)
{
    AlertEngine eng(AlertConfig::defaults());
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic.linkUp = false;
    sim.framesFlowing = false;
    run(eng, sim, log, 3000);
    ASSERT_EQ(log.count(RuleId::A04_NoLink, "ACTIVATED", 1), 1);
    sim.s.nic.linkUp = true; // vuelve el enlace, pero el audio NO
    run(eng, sim, log, 6000);
    EXPECT_EQ(log.count(RuleId::A04_NoLink, "RESOLVED"), 1);
    EXPECT_EQ(log.count(RuleId::A02_StreamInterrupted, "PROMOTED", 1), 1);
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::AudioInterrupted);
}

// R7: restart() durante la recuperación no permite saltarse la histéresis.
TEST(Review_R7, RestartKeepsHysteresisForRecoveringIncidents)
{
    AlertEngine eng(AlertConfig::defaults());
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.transportState = TransportState::Error;
    run(eng, sim, log, 1000);
    sim.s.transportState = TransportState::Streaming;
    run(eng, sim, log, 1000); // en recuperación
    eng.restart(sim.s.nowNs);
    run(eng, sim, log, 3500); // gracia (3 s) + 0,5 s: aún dentro de los 5 s
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "RESOLVED"), 0);
    run(eng, sim, log, 2000);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "RESOLVED"), 1);
}
