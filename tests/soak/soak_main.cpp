// Prueba de resistencia (Fase 5, versión reducida de T17/T18 sin hardware): N minutos con el
// transporte simulado en tiempo real, varias fuentes sobre un mismo dispositivo, monitor y
// alertas activos. Mide continuidad, desbordes, latencia medida, memoria y alertas.
//   sg_soak [minutos=10] [buffer=128] [canales=32] [fuentes=4] [cpu_load_threads=0]
#include "core/SourceSession.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace sg;

namespace {

double workingSetMb()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize / 1048576.0;
#endif
    return -1;
}

struct SourceStats {
    std::atomic<uint64_t> frames{0};
    std::atomic<uint64_t> gaps{0}; // salto de timestamp > 1 ms respecto a lo esperado
    std::atomic<uint64_t> flaggedDiscontinuities{0};
    uint64_t lastEnd = 0; // solo hilo de salida
};

} // namespace

int main(int argc, char **argv)
{
    const double minutes = argc > 1 ? std::atof(argv[1]) : 10.0;
    const uint32_t buffer = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 128;
    const uint32_t channels = argc > 3 ? static_cast<uint32_t>(std::atoi(argv[3])) : 32;
    const int nSources = argc > 4 ? std::atoi(argv[4]) : 4;
    const int loadThreads = argc > 5 ? std::atoi(argv[5]) : 0;

    std::printf("SOAK: %.1f min · buffer %u · %u canales · %d fuentes · %d hilos de carga CPU\n", minutes, buffer,
                channels, nSources, loadThreads);
    auto nic = std::make_unique<MockNetworkTelemetry>();
    nic->current.present = true;
    nic->current.linkUp = true;
    nic->current.speedBps = 1'000'000'000ull;
    nic->current.inErrors = 0;
    CoreRuntime rt(SteadyClock::instance(), std::move(nic), "", true);
    MockConfig mc;
    mc.realtime = true;
    mc.bufferFrames = buffer;
    mc.channels = channels;
    rt.transports().setMockConfig(mc);

    std::atomic<bool> stopLoad{false};
    std::vector<std::thread> load;
    for (int i = 0; i < loadThreads; ++i)
        load.emplace_back([&] {
            volatile double x = 0;
            while (!stopLoad)
                for (int k = 0; k < 100000; ++k)
                    x = x + k * 1e-9;
        });

    std::vector<std::unique_ptr<SourceStats>> stats;
    std::vector<std::shared_ptr<SourceSession>> sessions;
    for (int i = 0; i < nSources; ++i) {
        stats.push_back(std::make_unique<SourceStats>());
        SourceStats *st = stats.back().get();
        auto s = std::make_shared<SourceSession>(rt, "soak" + std::to_string(i), [st](const PcmBlock &b) {
            const uint64_t dur = static_cast<uint64_t>(1e9 * b.frames / b.sampleRate);
            if (st->lastEnd) {
                const int64_t err = static_cast<int64_t>(b.timestampNs) - static_cast<int64_t>(st->lastEnd);
                if (err > 1'000'000 || err < -1'000'000)
                    st->gaps.fetch_add(1);
            }
            if (b.flags & kBlockDiscontinuity)
                st->flaggedDiscontinuities.fetch_add(1);
            st->lastEnd = b.timestampNs + dur;
            st->frames.fetch_add(b.frames);
        });
        rt.monitor().add(s);
        SourceConfig cfg;
        cfg.deviceId = "mock:default";
        cfg.nicId = "mock-nic";
        cfg.armed = true;
        if (i % 4 == 0)
            cfg.map = ChannelMap::stereo(0, 1);
        else if (i % 4 == 1)
            cfg.map = ChannelMap::monoSum({2, 3});
        else if (i % 4 == 2)
            cfg.map = ChannelMap::multi({0, 1, 2, 3, 4, 5, 6, 7});
        else
            cfg.map = ChannelMap::stereo(static_cast<int16_t>(channels - 2), static_cast<int16_t>(channels - 1));
        s->apply(cfg);
        sessions.push_back(s);
    }

    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    const double memWarm = workingSetMb();
    double memMax = memWarm;
    double latMax = 0;
    const auto end = t0 + std::chrono::milliseconds(static_cast<int64_t>(minutes * 60000));
    int lastPrint = -1;
    while (std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        memMax = std::max(memMax, workingSetMb());
        for (auto &s : sessions) {
            const auto st = s->status();
            if (st.last.pluginLatencyMs.known())
                latMax = std::max(latMax, *st.last.pluginLatencyMs.value);
        }
        if (static_cast<int>(el) / 30 != lastPrint) {
            lastPrint = static_cast<int>(el) / 30;
            const auto st = sessions[0]->status();
            std::printf("t=%6.0fs frames[0]=%llu desb=%llu disc=%llu lat_max=%.2fms mem=%.1fMB estado=%s\n", el,
                        static_cast<unsigned long long>(stats[0]->frames.load()),
                        static_cast<unsigned long long>(st.last.overflows),
                        static_cast<unsigned long long>(st.last.discontinuities), latMax, workingSetMb(),
                        globalStatusText(st.global));
            std::fflush(stdout);
        }
    }
    stopLoad = true;
    for (auto &t : load)
        t.join();

    rt.dispatcher().flush();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int fails = 0;
    std::printf("\n==== RESUMEN (%.1f s) ====\n", secs);
    for (int i = 0; i < nSources; ++i) {
        const auto st = sessions[static_cast<std::size_t>(i)]->status();
        const double rate = stats[i]->frames.load() / secs;
        std::printf("fuente %d: %.0f frames/s · huecos de timestamp %llu · discontinuidades marcadas %llu · desbordes %llu · estado %s\n",
                    i, rate, static_cast<unsigned long long>(stats[i]->gaps.load()),
                    static_cast<unsigned long long>(stats[i]->flaggedDiscontinuities.load()),
                    static_cast<unsigned long long>(st.last.overflows), globalStatusText(st.global));
        if (st.last.overflows || stats[i]->gaps.load() > stats[i]->flaggedDiscontinuities.load())
            ++fails;
    }
    int notified = 0;
    for (const auto &e : rt.history().recent(500))
        notified += e.notify ? 1 : 0;
    const double growth = workingSetMb() - memWarm;
    std::printf("latencia medida máx (callback->entrega): %.2f ms\n", latMax);
    std::printf("memoria: tras calentamiento %.1f MB · máx %.1f MB · final %.1f MB (crecimiento %.1f MB)\n", memWarm,
                memMax, workingSetMb(), growth);
    std::printf("alertas notificadas: %d · eventos descartados por cola llena: %llu\n", notified,
                static_cast<unsigned long long>(rt.dispatcher().dropped()));
    if (loadThreads == 0 && (notified > 0 || growth > 10.0))
        ++fails;
    sessions.clear();
    rt.shutdown();
    std::printf("RESULTADO: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
