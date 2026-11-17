// Sonda HIL de solo lectura: abre un driver ASIO (sin cambiar frecuencia, buffer ni reloj),
// captura N segundos y muestra por canal pico, RMS y clipping, más el ritmo y el jitter del
// callback. No envía nada a la consola.
//   sg_asio_probe [texto del nombre del driver] [segundos]
//   sg_asio_probe SoundGrid 10
#include "platform/windows/AsioTransport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace sg;

namespace {
constexpr uint32_t kMaxCh = 128;

struct ProbeSink final : ICaptureSink {
    // Solo escribe el hilo del callback; se lee después de stop().
    double sumsq[kMaxCh] = {};
    float peak[kMaxCh] = {};
    uint64_t clips[kMaxCh] = {};
    uint64_t frames = 0;
    uint64_t blocks = 0;
    int64_t firstNs = 0;
    int64_t lastNs = 0;
    int64_t maxGapNs = 0;
    uint32_t channels = 0;
    std::atomic<uint64_t> live{0};

    void onCapture(const CaptureBlockView &b) noexcept override
    {
        const uint32_t ch = std::min(b.channels, kMaxCh);
        channels = ch;
        for (uint32_t c = 0; c < ch; ++c) {
            const float *p = b.planes[c];
            if (!p)
                continue;
            double s = 0;
            float pk = peak[c];
            uint64_t cl = 0;
            for (uint32_t i = 0; i < b.frames; ++i) {
                const float v = std::fabs(p[i]);
                s += static_cast<double>(v) * v;
                pk = v > pk ? v : pk;
                cl += v >= 0.999f ? 1u : 0u;
            }
            sumsq[c] += s;
            peak[c] = pk;
            clips[c] += cl;
        }
        if (blocks == 0)
            firstNs = b.callbackTimeNs;
        else
            maxGapNs = std::max(maxGapNs, b.callbackTimeNs - lastNs);
        lastNs = b.callbackTimeNs;
        frames += b.frames;
        ++blocks;
        live.store(blocks, std::memory_order_relaxed);
    }
};

double dbfs(double lin) { return lin > 1e-9 ? 20.0 * std::log10(lin) : -180.0; }

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
}
} // namespace

int main(int argc, char **argv)
{
    const std::string want = lower(argc > 1 ? argv[1] : "soundgrid");
    const int secs = argc > 2 ? std::max(1, std::atoi(argv[2])) : 10;

    DeviceInfo dev;
    bool found = false;
    for (const auto &d : enumerateAsioDrivers()) {
        std::printf("driver: %s (%s)%s\n", d.name.c_str(), d.id.c_str(), d.looksLikeWavesSoundGrid ? " [Waves]" : "");
        if (!found && (lower(d.name).find(want) != std::string::npos || lower(d.id).find(want) != std::string::npos)) {
            dev = d;
            found = true;
        }
    }
    if (!found) {
        std::printf("RESULTADO: NO_DRIVER (ningún driver contiene \"%s\")\n", want.c_str());
        return 2;
    }

    auto t = createAsioTransport();
    StreamConfig cfg;
    cfg.deviceId = dev.id;
    const Result r = t->open(cfg);
    if (!r.ok()) {
        std::printf("open(%s) -> %s: %s\nRESULTADO: OPEN_FAILED\n", dev.name.c_str(), errorCodeName(r.code),
                    r.message.c_str());
        return 3;
    }
    const StreamFormat f = t->format();
    std::printf("\n%s: %u canales · %u Hz · buffer %u · %s · latencia de entrada informada %.2f ms\n", dev.name.c_str(),
                f.channels, f.sampleRate, f.bufferFrames, sampleFormatName(f.driverFormat), f.driverInputLatencyMs);
    for (const auto &c : t->channels())
        std::printf("  entrada %u: %s\n", c.index + 1, c.name.c_str());

    ProbeSink sink;
    const Result rs = t->start(&sink);
    if (!rs.ok()) {
        std::printf("start() -> %s\nRESULTADO: START_FAILED\n", rs.message.c_str());
        t->close();
        return 4;
    }
    std::printf("\nCapturando %d s (solo lectura)...\n", secs);
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(secs));
    t->stop();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const TransportStatus st = t->snapshot();
    t->close();

    const double expectedGapMs = 1000.0 * f.bufferFrames / f.sampleRate;
    std::printf("\nRitmo: %llu bloques · %llu frames · %.3f x tiempo real · intervalo máx entre callbacks %.2f ms "
                "(nominal %.2f ms) · buffers repetidos %llu · xruns informados %llu\n",
                static_cast<unsigned long long>(sink.blocks), static_cast<unsigned long long>(sink.frames),
                sink.frames / (wall * f.sampleRate), sink.maxGapNs / 1e6, expectedGapMs,
                static_cast<unsigned long long>(st.duplicateBuffers), static_cast<unsigned long long>(st.overloads));

    std::printf("\n%-8s %12s %12s %8s\n", "Entrada", "Pico dBFS", "RMS dBFS", "Clips");
    bool anySignal = false;
    for (uint32_t c = 0; c < sink.channels; ++c) {
        const double rms = sink.frames ? std::sqrt(sink.sumsq[c] / sink.frames) : 0.0;
        const bool sig = sink.peak[c] > 1e-5f; // > -100 dBFS
        anySignal = anySignal || sig;
        std::printf("%-8u %12.1f %12.1f %8llu %s\n", c + 1, dbfs(sink.peak[c]), dbfs(rms),
                    static_cast<unsigned long long>(sink.clips[c]), sig ? "" : "(silencio digital)");
    }
    const bool rateOk = sink.blocks > 0 && std::fabs(sink.frames / (wall * f.sampleRate) - 1.0) < 0.05;
    std::printf("\nRESULTADO: %s\n", !rateOk ? "RITMO_ANOMALO" : anySignal ? "AUDIO_RECIBIDO" : "STREAM_SIN_SENAL");
    return 0;
}
