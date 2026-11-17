// Pruebas contra el hardware y los drivers REALES de la máquina que ejecuta las pruebas.
// No hay consola LV1 ni red SoundGrid: estas pruebas validan las APIs del sistema operativo
// (NIC, enumeración y ciclo de vida de drivers) y nunca se cuentan como prueba HIL.
#include "core/SourceSession.h"
#include "platform/INetworkTelemetry.h"
#if defined(SG_HAVE_ASIO)
#include "platform/windows/AsioTransport.h"
#endif
#if defined(SG_HAVE_COREAUDIO)
#include "platform/macos/CoreAudioTransport.h"
#endif

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace sg;

TEST(Platform_NIC, ListsEthernetInterfacesAndSamplesLinkState)
{
    auto nic = createPlatformNetworkTelemetry();
    const auto list = nic->listInterfaces();
    std::printf("API: %s · %zu interfaces Ethernet\n", nic->apiName(), list.size());
    if (list.empty())
        GTEST_SKIP() << "sin interfaces Ethernet en esta máquina";
    bool anyKnownLink = false;
    for (const auto &n : list) {
        const NicSample s = nic->sample(n.id);
        EXPECT_TRUE(s.present) << n.displayName;
        std::printf("  [%s] %s (%s) fisica=%d link=%s speed=%s inErr=%s\n", n.id.c_str(), n.displayName.c_str(),
                    n.description.c_str(), n.isPhysical ? 1 : 0,
                    s.linkUp ? (*s.linkUp ? "UP" : "DOWN") : "UNKNOWN",
                    s.speedBps ? std::to_string(*s.speedBps / 1000000).append(" Mbps").c_str() : "UNKNOWN",
                    s.inErrors ? std::to_string(*s.inErrors).c_str() : "UNKNOWN");
        anyKnownLink |= s.linkUp.has_value();
        if (s.linkUp.has_value() && !*s.linkUp) {
            EXPECT_FALSE(s.speedBps.has_value()) << "sin enlace no debe informarse velocidad";
        }
    }
    EXPECT_TRUE(anyKnownLink);
    EXPECT_FALSE(nic->sample("{00000000-0000-0000-0000-000000000000}").present);
}

#if defined(SG_HAVE_ASIO)
TEST(Platform_ASIO, EnumeratesRegistryWithoutLoadingDrivers)
{
    const auto drivers = enumerateAsioDrivers();
    std::printf("Drivers ASIO registrados: %zu\n", drivers.size());
    for (const auto &d : drivers)
        std::printf("  %s · \"%s\" · SoundGrid(por nombre)=%d\n", d.id.c_str(), d.name.c_str(),
                    d.looksLikeWavesSoundGrid ? 1 : 0);
    SUCCEED();
}

TEST(Platform_ASIO, OpenStartStopEveryInstalledDriverSafely)
{
    const auto drivers = enumerateAsioDrivers();
    if (drivers.empty())
        GTEST_SKIP() << "no hay drivers ASIO instalados";
    struct Sink : ICaptureSink {
        std::atomic<uint64_t> blocks{0};
        std::atomic<uint64_t> frames{0};
        void onCapture(const CaptureBlockView &b) noexcept override
        {
            blocks.fetch_add(1);
            frames.fetch_add(b.frames);
        }
    };
    for (const auto &d : drivers) {
        auto t = createAsioTransport();
        StreamConfig cfg;
        cfg.deviceId = d.id;
        const Result r = t->open(cfg);
        if (!r.ok()) {
            // Fallo limpio y explicable: aceptable (driver sin hardware/host, ocupado...).
            std::printf("  %s: open() -> %s: %s\n", d.name.c_str(), errorCodeName(r.code), r.message.c_str());
            EXPECT_NE(r.code, ErrorCode::Ok);
            EXPECT_FALSE(r.message.empty());
            t->close();
            continue;
        }
        const StreamFormat f = t->format();
        std::printf("  %s: %u canales, %u Hz, buffer %u, formato %s, latencia entrada %.2f ms\n", d.name.c_str(),
                    f.channels, f.sampleRate, f.bufferFrames, sampleFormatName(f.driverFormat),
                    f.driverInputLatencyMs);
        EXPECT_GT(f.channels, 0u);
        EXPECT_GT(f.sampleRate, 0u);
        Sink sink;
        const Result rs = t->start(&sink);
        if (rs.ok()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            t->stop();
            const TransportStatus ts = t->snapshot();
            const double ratio = static_cast<double>(sink.frames.load()) / (1.5 * f.sampleRate);
            std::printf("    ritmo observado = %.2f x tiempo real · mitades no alternas=%llu · timeInfo=%llu\n", ratio,
                        static_cast<unsigned long long>(ts.duplicateBuffers),
                        static_cast<unsigned long long>(ts.timeInfoCallbacks));
            if (ratio > 1.25 || ratio < 0.75)
                std::printf("    AVISO: el driver no avanza a tiempo real (normal en ReaRoute sin REAPER);"
                            " el plugin lo reflejaría como discontinuidades/A08\n");
            const uint64_t after = sink.blocks.load();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            EXPECT_EQ(sink.blocks.load(), after) << "callbacks después de stop()";
            std::printf("    capturados %llu bloques / %llu frames en 1.5 s (%s)\n",
                        static_cast<unsigned long long>(sink.blocks.load()),
                        static_cast<unsigned long long>(sink.frames.load()),
                        sink.blocks.load() ? "el driver entrega audio" : "el driver no avanza sin su host");
        } else {
            std::printf("    start() -> %s\n", rs.message.c_str());
        }
        t->close();
    }
}
#endif

#if defined(SG_HAVE_COREAUDIO)
TEST(Platform_CoreAudio, EnumeratesInputDevices)
{
    const auto devs = enumerateCoreAudioInputDevices();
    for (const auto &d : devs)
        std::printf("  %s · \"%s\" · %u canales\n", d.id.c_str(), d.name.c_str(), d.inputChannels);
    SUCCEED();
}
#endif
