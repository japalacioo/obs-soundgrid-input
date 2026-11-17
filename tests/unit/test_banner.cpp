// Aviso rojo "SE PERDIÓ EL AUDIO" en OBS: modelo sin interfaz + lectura de la preferencia de Waves.
#include "TestHelpers.h"
#include "observability/AudioLossBanner.h"
#include "platform/WavesDriverPrefs.h"

#include <gtest/gtest.h>

using namespace sg;
using namespace sgtest;

namespace {
AlertEvent incident(RuleId r, AlertState st = AlertState::Active, bool secondary = false)
{
    AlertEvent e;
    e.rule = r;
    e.state = st;
    e.secondary = secondary;
    e.wallTime = "18:20:05";
    return e;
}
BannerSourceInput source(const std::string &name, std::vector<AlertEvent> inc)
{
    BannerSourceInput s;
    s.sourceName = name;
    s.openIncidents = std::move(inc);
    return s;
}
constexpr int64_t kSec = 1'000'000'000;
} // namespace

TEST(Banner, HiddenWithoutIncidents)
{
    AudioLossBannerModel m;
    EXPECT_EQ(m.update({source("Main", {})}, 0).level, BannerLevel::Hidden);
    EXPECT_EQ(m.update({}, 0).level, BannerLevel::Hidden);
}

TEST(Banner, StreamInterruptedShowsRedWithSourceAndTime)
{
    AudioLossBannerModel m;
    const BannerState st = m.update({source("Main L/R", {incident(RuleId::A02_StreamInterrupted)})}, 0);
    EXPECT_EQ(st.level, BannerLevel::Lost);
    EXPECT_EQ(st.headline, "SE PERDIÓ EL AUDIO");
    EXPECT_NE(st.detail.find("'Main L/R'"), std::string::npos);
    EXPECT_NE(st.detail.find("18:20:05"), std::string::npos);
    EXPECT_EQ(st.sourceToOpen, "Main L/R");
}

TEST(Banner, ShowsRootCauseNotConsequences)
{
    AudioLossBannerModel m;
    const BannerState st = m.update(
        {source("Main", {incident(RuleId::A02_StreamInterrupted, AlertState::Active, true),
                         incident(RuleId::A04_NoLink)})},
        0);
    EXPECT_EQ(st.rule, RuleId::A04_NoLink);
    EXPECT_NE(st.detail.find("cable"), std::string::npos);
}

// Visto en OBS real con el simulador: el driver se reabre (A03 recuperándose) pero siguen sin
// llegar muestras (A02 secundaria activa). No debe decir "restablecido".
TEST(Banner, StaysRedWhileFramesAreStillMissingDuringReconnect)
{
    AudioLossBannerModel m;
    const BannerState st = m.update({source("Main", {incident(RuleId::A03_DriverLost, AlertState::Recovering),
                                                     incident(RuleId::A02_StreamInterrupted, AlertState::Active, true)})},
                                    0);
    EXPECT_EQ(st.level, BannerLevel::Lost);
    EXPECT_EQ(st.rule, RuleId::A03_DriverLost); // se muestra la causa raíz
}

TEST(Banner, ShowsLocalClockTime)
{
    EXPECT_EQ(localClockFromIso("18:20:05"), "18:20:05");
    const std::string t = localClockFromIso("2026-11-17T23:39:21.651Z");
    ASSERT_EQ(t.size(), 8u);
    EXPECT_EQ(t.substr(2, 1), ":");
    EXPECT_EQ(t.substr(5, 3), ":21");
}

TEST(Banner, MutedOrDisabledSourcesDoNotShow)
{
    AudioLossBannerModel m;
    auto s = source("Main", {incident(RuleId::A03_DriverLost)});
    s.notificationsMuted = true;
    EXPECT_EQ(m.update({s}, 0).level, BannerLevel::Hidden);
    s.notificationsMuted = false;
    s.bannerEnabled = false;
    EXPECT_EQ(m.update({s}, 0).level, BannerLevel::Hidden);
}

TEST(Banner, NonAudioAlertsDoNotShow)
{
    AudioLossBannerModel m;
    EXPECT_EQ(m.update({source("Main", {incident(RuleId::A05_SlowLink), incident(RuleId::A09_Clipping),
                                        incident(RuleId::A08_Dropouts)})},
                       0)
                  .level,
              BannerLevel::Hidden);
}

TEST(Banner, LostBeatsRecoveringAcrossSources)
{
    AudioLossBannerModel m;
    const BannerState st = m.update({source("A", {incident(RuleId::A04_NoLink, AlertState::Recovering)}),
                                     source("B", {incident(RuleId::A02_StreamInterrupted)})},
                                    0);
    EXPECT_EQ(st.level, BannerLevel::Lost);
    EXPECT_EQ(st.sourceToOpen, "B");
    EXPECT_EQ(st.affectedSources, 2);
}

TEST(Banner, RecoveredShowsGreenForFiveSecondsThenHides)
{
    AudioLossBannerModel m(5000);
    m.update({source("Main", {incident(RuleId::A02_StreamInterrupted)})}, 0);
    EXPECT_EQ(m.update({source("Main", {incident(RuleId::A02_StreamInterrupted, AlertState::Recovering)})}, 1 * kSec)
                  .level,
              BannerLevel::Recovering);
    const BannerState rec = m.update({source("Main", {})}, 2 * kSec);
    EXPECT_EQ(rec.level, BannerLevel::Recovered);
    EXPECT_EQ(rec.headline, "AUDIO RECUPERADO");
    EXPECT_EQ(m.update({source("Main", {})}, 6 * kSec).level, BannerLevel::Recovered);
    EXPECT_EQ(m.update({source("Main", {})}, 7 * kSec + 1).level, BannerLevel::Hidden);
    EXPECT_EQ(m.update({source("Main", {})}, 8 * kSec).level, BannerLevel::Hidden);
}

// Con el motor real: el flujo se corta → rojo; vuelve → ámbar (histéresis) → verde → oculto.
TEST(Banner, EndToEndWithAlertEngine)
{
    AlertEngine eng(AlertConfig::defaults());
    SnapSim sim;
    eng.restart(sim.s.nowNs);
    EventLog log;
    AudioLossBannerModel m;
    auto banner = [&] { return m.update({source("Main", eng.openIncidents())}, sim.s.nowNs); };
    run(eng, sim, log, 4000);
    EXPECT_EQ(banner().level, BannerLevel::Hidden);
    sim.framesFlowing = false;
    run(eng, sim, log, 2000);
    const BannerState lost = banner();
    EXPECT_EQ(lost.level, BannerLevel::Lost);
    EXPECT_EQ(lost.rule, RuleId::A02_StreamInterrupted);
    sim.framesFlowing = true;
    run(eng, sim, log, 1000);
    EXPECT_EQ(banner().level, BannerLevel::Recovering);
    run(eng, sim, log, 5000);
    EXPECT_EQ(banner().level, BannerLevel::Recovered);
    run(eng, sim, log, 6000);
    EXPECT_EQ(banner().level, BannerLevel::Hidden);
}

TEST(WavesPrefs, ParsesLocalLanPort)
{
    const std::string xml = "<Preferences><Configuration><LocalLanPort>02:1A:2B:3C:4D:5E</LocalLanPort>"
                            "</Configuration></Preferences>";
    EXPECT_EQ(parseWavesLocalLanPort(xml).value_or(""), "02:1a:2b:3c:4d:5e");
    EXPECT_FALSE(parseWavesLocalLanPort("<LocalLanPort>00:00:00:00:00:00</LocalLanPort>"));
    EXPECT_FALSE(parseWavesLocalLanPort("<LocalLanPort></LocalLanPort>"));
    EXPECT_FALSE(parseWavesLocalLanPort("<Preferences/>"));
    EXPECT_EQ(normalizeMac("02-1A-2B-3C-4D-5E"), "02:1a:2b:3c:4d:5e");
    EXPECT_EQ(normalizeMac("zz:73:e7:30:a2:86"), "");
}
