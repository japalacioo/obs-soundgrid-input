// Salida de alertas y persistencia: T40 (persistencia), T43 (JSONL/rotación/privacidad), T53 (cola acotada).
#include "common/Settings.h"
#include "observability/AlertSink.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>

using namespace sg;
namespace fs = std::filesystem;

namespace {
AlertEvent sampleEvent(uint64_t id)
{
    AlertEvent e;
    e.id = id;
    e.rule = RuleId::A04_NoLink;
    e.severity = Severity::Critical;
    e.transition = "ACTIVATED";
    e.wallTime = "2026-11-17T03:00:00.000Z";
    e.message = "A04 · Sin conexión de red";
    e.evidence.observed = "enlace DOWN";
    e.evidence.cause = "Cable \"SoundGrid\" desconectado\nlínea 2";
    e.evidence.measurementSource = "NIC del SO (Windows)";
    e.affectedSource = "Main L/R";
    return e;
}

fs::path tempDir(const char *name)
{
    fs::path d = fs::temp_directory_path() / "sg_tests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
} // namespace

TEST(T43_Logs, JsonlLinesAreValidAndEscaped)
{
    const std::string j = alertEventToJson(sampleEvent(7));
    EXPECT_EQ(j.front(), '{');
    EXPECT_EQ(j.back(), '}');
    EXPECT_EQ(j.find('\n'), std::string::npos); // una línea por evento
    EXPECT_NE(j.find("\\\"SoundGrid\\\""), std::string::npos);
    EXPECT_NE(j.find("\\n"), std::string::npos);
    EXPECT_NE(j.find("\"rule\":\"A04\""), std::string::npos);
    EXPECT_NE(j.find("\"confidence\":\"medido\""), std::string::npos);
}

TEST(T43_Logs, RotationBySizeKeepsBoundedFileCount)
{
    const fs::path d = tempDir("rotation");
    JsonlAlertLog log((d / "alerts.jsonl").string(), 2000, 3);
    for (int i = 0; i < 200; ++i)
        log.onEvent(sampleEvent(static_cast<uint64_t>(i)));
    EXPECT_GT(log.rotations(), 3u);
    EXPECT_TRUE(fs::exists(d / "alerts.jsonl"));
    EXPECT_TRUE(fs::exists(d / "alerts.1.jsonl"));
    EXPECT_TRUE(fs::exists(d / "alerts.3.jsonl"));
    EXPECT_FALSE(fs::exists(d / "alerts.4.jsonl"));
    EXPECT_LE(fs::file_size(d / "alerts.jsonl"), 2000u);
    std::ifstream f(d / "alerts.jsonl");
    std::string line;
    int lines = 0;
    while (std::getline(f, line)) {
        ++lines;
        EXPECT_EQ(line.front(), '{');
        EXPECT_EQ(line.back(), '}');
    }
    EXPECT_GT(lines, 0);
}

TEST(T43_Logs, NoIpOrMacAddressesInAlertOutput)
{
    const std::string j = alertEventToJson(sampleEvent(1));
    EXPECT_FALSE(std::regex_search(j, std::regex(R"(\b\d{1,3}(\.\d{1,3}){3}\b)")));
    EXPECT_FALSE(std::regex_search(j, std::regex(R"(([0-9a-fA-F]{2}[:-]){5}[0-9a-fA-F]{2})")));
}

TEST(T53_Queue, SaturatedDispatcherDropsAndCountsWithoutGrowing)
{
    AlertDispatcher d(16, /*startThread=*/false);
    auto hist = std::make_shared<AlertHistory>(1000);
    d.addSink(hist);
    for (int i = 0; i < 1000; ++i)
        d.publish(sampleEvent(static_cast<uint64_t>(i)));
    EXPECT_EQ(d.queued(), 16u);
    EXPECT_EQ(d.dropped(), 984u);
    EXPECT_EQ(d.drain(), 16u);
    EXPECT_EQ(hist->recent(1000).size(), 16u);
}

TEST(T53_Queue, ThreadedDispatcherDeliversAndSurvivesThrowingSink)
{
    AlertDispatcher d(1024, true);
    auto hist = std::make_shared<AlertHistory>(100);
    d.addSink(std::make_shared<CallbackSink>([](const AlertEvent &) { throw std::runtime_error("sink roto"); }));
    d.addSink(hist);
    for (int i = 0; i < 50; ++i)
        d.publish(sampleEvent(static_cast<uint64_t>(i)));
    d.flush();
    EXPECT_EQ(d.delivered(), 50u);
    EXPECT_EQ(hist->recent(100).size(), 50u);
    EXPECT_EQ(hist->recent(1)[0].id, 49u); // más reciente primero
}

TEST(T40_Settings, RoundTripKeepsThresholdsAndProfile)
{
    AlertConfig c = AlertConfig::defaults(AlertProfile::Grabacion);
    c.rule(RuleId::A01_Silence).threshold = -55;
    c.rule(RuleId::A01_Silence).windowMs = 15000;
    c.rule(RuleId::A07_Latency).enabled = false;
    c.graceMs = 5000;
    MapSettingsStore s;
    saveAlertConfig(c, s);
    const AlertConfig r = loadAlertConfig(s);
    EXPECT_EQ(r.profile, AlertProfile::Grabacion);
    EXPECT_DOUBLE_EQ(r.rule(RuleId::A01_Silence).threshold, -55);
    EXPECT_EQ(r.rule(RuleId::A01_Silence).windowMs, 15000);
    EXPECT_FALSE(r.rule(RuleId::A07_Latency).enabled);
    EXPECT_EQ(r.graceMs, 5000);
    EXPECT_DOUBLE_EQ(r.rule(RuleId::A08_Dropouts).threshold, 0.0); // perfil Grabación
}

TEST(T40_Settings, CorruptOrMissingValuesFallBackToSafeDefaults)
{
    MapSettingsStore s;
    EXPECT_EQ(loadAlertConfig(s).profile, AlertProfile::Transmision); // vacío -> defaults
    s.setNumber("alerts.schema", 1);
    s.setString("alerts.profile", "desconocido");
    s.setNumber("alerts.A01.window_ms", -50);          // fuera de rango
    s.setNumber("alerts.A04.severity", 99);            // fuera de rango
    s.setNumber("alerts.A07.threshold", std::nan("")); // NaN
    const AlertConfig r = loadAlertConfig(s);
    const AlertConfig d = AlertConfig::defaults();
    EXPECT_EQ(r.profile, AlertProfile::Transmision);
    EXPECT_EQ(r.rule(RuleId::A01_Silence).windowMs, d.rule(RuleId::A01_Silence).windowMs);
    EXPECT_EQ(r.rule(RuleId::A04_NoLink).severity, Severity::Critical);
    EXPECT_DOUBLE_EQ(r.rule(RuleId::A07_Latency).threshold, 80.0);
}

TEST(T40_Settings, LegacyUnversionedDataMigratesToProfileDefaults)
{
    MapSettingsStore s;
    s.setString("alerts.profile", "ensayo");
    s.setNumber("alerts.A01.threshold", -10); // sin "schema": se ignora (anterior a v1)
    const AlertConfig r = loadAlertConfig(s);
    EXPECT_EQ(r.profile, AlertProfile::Ensayo);
    EXPECT_DOUBLE_EQ(r.rule(RuleId::A01_Silence).threshold, -60.0);
}
