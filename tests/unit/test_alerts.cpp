// Motor de alertas con reloj virtual: T25–T39, T51, T54 y anti-falsos positivos (plan §4A.5).
#include "TestHelpers.h"

#include <gtest/gtest.h>

using namespace sg;
using namespace sgtest;

namespace {
AlertEngine makeEngine(AlertProfile p = AlertProfile::Transmision)
{
    AlertEngine e(AlertConfig::defaults(p));
    return e;
}
} // namespace

TEST(Alerts, GracePeriodSuppressesEverything)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    sim.framesFlowing = false;
    sim.s.nic.linkUp = false;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 2750);
    EXPECT_TRUE(log.all.empty());
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::Initializing);
}

TEST(T25_A01, SilenceOnArmedSourceAfterTenSeconds)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    sim.s.armed = true;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000); // gracia + señal
    sim.rmsDb = -72.0;
    run(eng, sim, log, 9500);
    EXPECT_EQ(log.count(RuleId::A01_Silence, "ACTIVATED"), 0);
    run(eng, sim, log, 1000);
    ASSERT_EQ(log.count(RuleId::A01_Silence, "ACTIVATED", 1), 1);
    const AlertEvent &e = log.all.back();
    EXPECT_NE(e.evidence.observed.find("dBFS"), std::string::npos);
    EXPECT_EQ(e.evidence.confidence, Confidence::Measured);
    EXPECT_EQ(log.count(RuleId::A02_StreamInterrupted, "ACTIVATED"), 0); // silencio != corte
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::SilenceDetected);
}

TEST(T26_A01, SixtySecondsOfIntentionalSilenceWithoutArmIsNotAFault)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    sim.s.armed = false;
    eng.restart(sim.s.nowNs);
    sim.rmsDb = -120.0;
    EventLog log;
    run(eng, sim, log, 60000);
    EXPECT_TRUE(log.all.empty()) << log.all.size() << " eventos inesperados";
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::AudioOk);
}

TEST(T27_A02, FramesStopTriggersInterruptionNotSilence)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    sim.s.armed = true;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.framesFlowing = false;
    run(eng, sim, log, 1000);
    EXPECT_EQ(log.count(RuleId::A02_StreamInterrupted, "ACTIVATED"), 0); // margen de 1 s
    run(eng, sim, log, 500);
    EXPECT_EQ(log.count(RuleId::A02_StreamInterrupted, "ACTIVATED", 1), 1);
    run(eng, sim, log, 20000);
    EXPECT_EQ(log.count(RuleId::A01_Silence, "ACTIVATED"), 0); // sin PCM no hay A01
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::AudioInterrupted);
}

TEST(T28_A03_T37_A10, DriverLossDebouncedAndRecoveryAfterHysteresis)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.transportState = TransportState::Error;
    sim.s.lastError = ErrorCode::NotFound;
    sim.s.lastErrorMessage = "simulado";
    run(eng, sim, log, 250);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "ACTIVATED"), 0); // debounce 500 ms
    run(eng, sim, log, 500);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "ACTIVATED", 1), 1);

    sim.s.transportState = TransportState::Streaming;
    sim.s.lastError = ErrorCode::Ok;
    run(eng, sim, log, 4500);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "RECOVERING"), 1);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "RESOLVED"), 0); // aún en histéresis
    run(eng, sim, log, 1000);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "RESOLVED"), 1);
    EXPECT_EQ(log.count(RuleId::A10_Recovered, "RECOVERED", 1), 1);
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::AudioOk);
}

TEST(T37_A10, FlappingInsideHysteresisDoesNotDuplicateIncidents)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    for (int i = 0; i < 10; ++i) {
        sim.s.transportState = TransportState::Error;
        run(eng, sim, log, 1000);
        sim.s.transportState = TransportState::Streaming;
        run(eng, sim, log, 2000); // < 5 s: no se resuelve
    }
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "ACTIVATED"), 1);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "RESOLVED"), 0);
    EXPECT_EQ(log.notified(), 1);
}

TEST(T29_A04, NicDownAfterOneSecond)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic.linkUp = false;
    sim.s.nic.speedBps.reset();
    run(eng, sim, log, 1250);
    ASSERT_EQ(log.count(RuleId::A04_NoLink, "ACTIVATED", 1), 1);
    for (auto &e : log.all)
        if (e.rule == RuleId::A04_NoLink)
            EXPECT_NE(e.evidence.measurementSource.find("NIC del SO"), std::string::npos);
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::NoLink);
}

TEST(T29_A04, NicRemovedCountsAsNoLink)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic = NicSample{};
    run(eng, sim, log, 1500);
    EXPECT_EQ(log.count(RuleId::A04_NoLink, "ACTIVATED"), 1);
}

TEST(T30_A05, HundredMegabitWarnsWithoutClaimingPacketLoss)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic.speedBps = 100'000'000ull;
    run(eng, sim, log, 4750);
    EXPECT_EQ(log.count(RuleId::A05_SlowLink, "ACTIVATED"), 0);
    run(eng, sim, log, 500);
    ASSERT_EQ(log.count(RuleId::A05_SlowLink, "ACTIVATED", 1), 1);
    for (auto &e : log.all) {
        EXPECT_EQ(e.evidence.observed.find("pérdida"), std::string::npos);
        EXPECT_NE(e.evidence.cause.find("no prueba pérdida"), std::string::npos);
    }
    EXPECT_EQ(log.count(RuleId::A06_NicErrors, "ACTIVATED"), 0);
}

TEST(T31_A06, NicErrorIncreaseWithSourceAndDelta)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    for (int i = 0; i < 20; ++i) {
        *sim.s.nic.inErrors += 2;
        log.add(eng.evaluate(sim.next()));
    }
    ASSERT_EQ(log.count(RuleId::A06_NicErrors, "ACTIVATED", 1), 1);
    for (auto &e : log.all)
        if (e.rule == RuleId::A06_NicErrors && e.transition == "ACTIVATED") {
            EXPECT_NE(e.evidence.observed.find("errores"), std::string::npos);
            EXPECT_FALSE(e.evidence.measurementSource.empty());
        }
}

TEST(T32_A06, UnavailableCountersAreUnknownNeverFakeLoss)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    sim.s.nic.inErrors.reset();
    sim.s.nic.inDiscards.reset();
    sim.s.nic.outErrors.reset();
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 20000);
    EXPECT_EQ(log.count(RuleId::A06_NicErrors, "ACTIVATED"), 0);
    EXPECT_EQ(eng.ruleStatus(RuleId::A06_NicErrors).state, RuleEvalState::Unknown);
    EXPECT_NE(eng.ruleStatus(RuleId::A06_NicErrors).observed.find("NO DISPONIBLES"), std::string::npos);
}

TEST(T33_A07, MeasuredLatencyAboveLimitForFiveSeconds)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.pluginLatencyMs = Observed<double>::measured(120.0, "callback->OBS");
    sim.s.queueDelayMs = Observed<double>::estimated(40.0, "cola");
    run(eng, sim, log, 5250);
    ASSERT_EQ(log.count(RuleId::A07_Latency, "ACTIVATED", 1), 1);
    const AlertEvent *e = nullptr;
    for (auto &x : log.all)
        if (x.rule == RuleId::A07_Latency)
            e = &x;
    ASSERT_NE(e, nullptr);
    EXPECT_NE(e->evidence.observed.find("120.0 ms"), std::string::npos);
    EXPECT_NE(e->evidence.observed.find("extremo a extremo: NO DISPONIBLE"), std::string::npos);
    EXPECT_NE(e->evidence.measurementSource.find("Tramo medido"), std::string::npos);
    EXPECT_NE(e->evidence.threshold.find("80 ms"), std::string::npos);
}

TEST(T34_A07, NoReliableTimestampsMeansNotAvailable)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    sim.s.pluginLatencyMs = Observed<double>::unknown();
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 20000);
    EXPECT_EQ(log.count(RuleId::A07_Latency, "ACTIVATED"), 0);
    EXPECT_EQ(eng.ruleStatus(RuleId::A07_Latency).state, RuleEvalState::Unknown);
    EXPECT_NE(eng.ruleStatus(RuleId::A07_Latency).observed.find("NO DISPONIBLE"), std::string::npos);
}

TEST(T35_A08, DropoutsAboveThresholdPerMinuteAreCritical)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    for (int i = 0; i < 3; ++i) {
        ++sim.s.discontinuities;
        run(eng, sim, log, 1000);
    }
    EXPECT_EQ(log.count(RuleId::A08_Dropouts, "ACTIVATED"), 0); // 3 no supera "> 3"
    ++sim.s.overflows;
    run(eng, sim, log, 250);
    ASSERT_EQ(log.count(RuleId::A08_Dropouts, "ACTIVATED", 1), 1);
    for (auto &e : log.all)
        if (e.rule == RuleId::A08_Dropouts)
            EXPECT_EQ(e.severity, Severity::Critical);
}

TEST(T35_A08, RecordingProfileFlagsSingleDropout)
{
    AlertEngine eng = makeEngine(AlertProfile::Grabacion);
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    ++sim.s.discontinuities;
    run(eng, sim, log, 250);
    EXPECT_EQ(log.count(RuleId::A08_Dropouts, "ACTIVATED", 1), 1);
}

TEST(T36_A09, SustainedClippingWarnsIsolatedPeakDoesNot)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.clipping = true;
    run(eng, sim, log, 250); // pico aislado
    sim.clipping = false;
    run(eng, sim, log, 3000);
    EXPECT_EQ(log.count(RuleId::A09_Clipping, "ACTIVATED"), 0);
    sim.clipping = true;
    run(eng, sim, log, 750); // 3 ventanas en 2 s
    EXPECT_EQ(log.count(RuleId::A09_Clipping, "ACTIVATED", 1), 1);
}

TEST(T38_Correlation, NicDownDominatesStreamAndDriverAlerts)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    // Se cae el cable: sin enlace, sin frames y el driver falla.
    sim.s.nic.linkUp = false;
    sim.s.nic.speedBps.reset();
    sim.framesFlowing = false;
    sim.s.transportState = TransportState::Error;
    run(eng, sim, log, 5000);
    ASSERT_EQ(log.count(RuleId::A04_NoLink, "ACTIVATED", 1), 1);
    uint64_t a04 = 0;
    for (auto &e : log.all)
        if (e.rule == RuleId::A04_NoLink && e.transition == "ACTIVATED")
            a04 = e.id;
    int secondaries = 0;
    for (auto &e : log.all) {
        if ((e.rule == RuleId::A02_StreamInterrupted || e.rule == RuleId::A03_DriverLost) && e.transition != "RESOLVED") {
            EXPECT_TRUE(e.secondary);
            EXPECT_FALSE(e.notify);
            EXPECT_EQ(e.correlationId, a04);
            ++secondaries;
        }
    }
    EXPECT_GE(secondaries, 1);
    EXPECT_EQ(log.notified(), 1); // un incidente principal, sin tormenta
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::NoLink);
}

TEST(T38_Correlation, RootCauseOpenedLaterReclassifiesConsequences)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.framesFlowing = false; // primero se detiene el audio
    run(eng, sim, log, 1500);
    ASSERT_EQ(log.count(RuleId::A02_StreamInterrupted, "ACTIVATED", 1), 1);
    sim.s.nic.linkUp = false; // después se detecta la caída del enlace
    run(eng, sim, log, 1500);
    EXPECT_EQ(log.count(RuleId::A02_StreamInterrupted, "CORRELATED"), 1);
    EXPECT_EQ(eng.globalStatus(), GlobalStatus::NoLink);
}

TEST(T51_Correlation, HundredCablePullsGiveOnePrimaryEach)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    for (int i = 0; i < 100; ++i) {
        sim.s.nic.linkUp = false;
        sim.framesFlowing = false;
        sim.s.transportState = TransportState::Reconnecting;
        run(eng, sim, log, 3000);
        sim.s.nic.linkUp = true;
        sim.framesFlowing = true;
        sim.s.transportState = TransportState::Streaming;
        run(eng, sim, log, 40000); // > histéresis + cooldown
    }
    EXPECT_EQ(log.count(RuleId::A04_NoLink, "ACTIVATED", 1), 100);
    EXPECT_EQ(log.count(RuleId::A02_StreamInterrupted, "ACTIVATED", 1), 0);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "ACTIVATED", 1), 0);
    EXPECT_EQ(log.count(RuleId::A10_Recovered, "RECOVERED", 1), 100);
}

TEST(T39_Ack, AcknowledgeKeepsDetectionAndMuteKeepsLogging)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic.linkUp = false;
    run(eng, sim, log, 1500);
    auto open = eng.openIncidents();
    ASSERT_EQ(open.size(), 1u);
    auto ack = eng.acknowledge(open[0].id, sim.s.nowNs);
    ASSERT_TRUE(ack.has_value());
    EXPECT_EQ(ack->state, AlertState::Acknowledged);
    EXPECT_EQ(eng.ruleStatus(RuleId::A04_NoLink).state, RuleEvalState::Acknowledged);
    run(eng, sim, log, 5000);
    EXPECT_EQ(eng.openIncidents().size(), 1u); // sigue vigilando
    sim.s.nic.linkUp = true;
    run(eng, sim, log, 6000);
    EXPECT_EQ(log.count(RuleId::A04_NoLink, "RESOLVED"), 1);

    // Silenciar notificaciones: el evento se registra con notify=false.
    eng.muteNotificationsUntil(sim.s.nowNs + 600 * kNsPerSec);
    sim.s.transportState = TransportState::Error;
    run(eng, sim, log, 1000);
    EXPECT_EQ(log.count(RuleId::A03_DriverLost, "ACTIVATED", 0), 1);
}

TEST(T54_Robustness, ClockGoingBackwardsAndBadLatencyDoNotInventAlerts)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.pluginLatencyMs = Observed<double>::unknown();
    for (int i = 0; i < 50; ++i) {
        sim.s.nowNs -= 500 * kNsPerMs; // reloj que retrocede
        log.add(eng.evaluate(sim.next()));
    }
    EXPECT_EQ(log.count(RuleId::A07_Latency, "ACTIVATED"), 0);
    EXPECT_EQ(log.notified(), 0);
}

TEST(Alerts, DisablingAnActiveRuleClosesItQuietly)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic.speedBps = 100'000'000ull;
    run(eng, sim, log, 6000);
    ASSERT_EQ(log.count(RuleId::A05_SlowLink, "ACTIVATED"), 1);
    AlertConfig c = eng.config();
    c.rule(RuleId::A05_SlowLink).enabled = false;
    eng.setConfig(c);
    run(eng, sim, log, 500);
    EXPECT_EQ(log.count(RuleId::A05_SlowLink, "RESOLVED", 0), 1);
    EXPECT_EQ(log.count(RuleId::A10_Recovered, "RECOVERED"), 0);
    EXPECT_EQ(eng.ruleStatus(RuleId::A05_SlowLink).state, RuleEvalState::Disabled);
}

TEST(Alerts, EnsayoProfileOnlyNotifiesCritical)
{
    AlertEngine eng = makeEngine(AlertProfile::Ensayo);
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic.speedBps = 100'000'000ull; // WARN
    run(eng, sim, log, 6000);
    EXPECT_EQ(log.count(RuleId::A05_SlowLink, "ACTIVATED", 0), 1);
    sim.s.nic.linkUp = false; // CRITICAL
    run(eng, sim, log, 1500);
    EXPECT_EQ(log.count(RuleId::A04_NoLink, "ACTIVATED", 1), 1);
}

TEST(Alerts, ReopenWithinCooldownIsNotRenotified)
{
    AlertEngine eng = makeEngine();
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    run(eng, sim, log, 4000);
    sim.s.nic.linkUp = false;
    run(eng, sim, log, 1500);
    sim.s.nic.linkUp = true;
    run(eng, sim, log, 6000);
    sim.s.nic.linkUp = false; // vuelve a caer a los ~6 s (< cooldown 30 s)
    run(eng, sim, log, 1500);
    EXPECT_EQ(log.count(RuleId::A04_NoLink, "ACTIVATED", 1), 1);
    EXPECT_EQ(log.count(RuleId::A04_NoLink, "REOPENED", 0), 1);
}
