#pragma once
// Medidores Peak/RMS por canal. El hilo de audio acumula con operaciones atómicas
// (sin locks ni asignaciones); el monitor toma y reinicia la ventana periódicamente.
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace sg {

struct MeterWindow {
    uint32_t channels = 0;
    std::array<float, 8> peak{};   // lineal
    std::array<double, 8> rms{};   // lineal
    std::array<uint32_t, 8> clippedSamples{};
    uint64_t samples = 0;          // muestras por canal en la ventana
};

class MeterBank {
public:
    static constexpr uint32_t kMaxChannels = 8;
    // Umbral de clipping digital (−0.1 dBFS ≈ 0.98855).
    static constexpr float kClipLinear = 0.98855309f;

    void setClipThresholdLinear(float v) noexcept { clip_.store(v, std::memory_order_relaxed); }

    // Tiempo real.
    void accumulate(const float *const *planes, uint32_t channels, uint32_t frames) noexcept
    {
        if (channels > kMaxChannels)
            channels = kMaxChannels;
        const float clip = clip_.load(std::memory_order_relaxed);
        for (uint32_t c = 0; c < channels; ++c) {
            const float *p = planes[c];
            float pk = 0.0f;
            double ss = 0.0;
            uint32_t clipped = 0;
            for (uint32_t i = 0; i < frames; ++i) {
                const float a = p[i] < 0 ? -p[i] : p[i];
                if (a > pk)
                    pk = a;
                if (a >= clip)
                    ++clipped;
                ss += static_cast<double>(p[i]) * p[i];
            }
            auto &ch = ch_[c];
            atomicMaxFloat(ch.peakBits, pk);
            atomicAddDouble(ch.sumSqBits, ss);
            if (clipped)
                ch.clipped.fetch_add(clipped, std::memory_order_relaxed);
        }
        channels_.store(channels, std::memory_order_relaxed);
        samples_.fetch_add(frames, std::memory_order_relaxed);
    }

    // Monitor: devuelve la ventana acumulada desde la última llamada y la reinicia.
    MeterWindow takeWindow() noexcept
    {
        MeterWindow w;
        w.channels = channels_.load(std::memory_order_relaxed);
        w.samples = samples_.exchange(0, std::memory_order_acq_rel);
        for (uint32_t c = 0; c < w.channels; ++c) {
            auto &ch = ch_[c];
            w.peak[c] = bitsToFloat(ch.peakBits.exchange(0, std::memory_order_acq_rel));
            const double ss = bitsToDouble(ch.sumSqBits.exchange(0, std::memory_order_acq_rel));
            w.rms[c] = w.samples ? std::sqrt(ss / static_cast<double>(w.samples)) : 0.0;
            w.clippedSamples[c] = ch.clipped.exchange(0, std::memory_order_acq_rel);
        }
        return w;
    }

private:
    struct Channel {
        std::atomic<uint32_t> peakBits{0};
        std::atomic<uint64_t> sumSqBits{0};
        std::atomic<uint32_t> clipped{0};
    };

    static float bitsToFloat(uint32_t b) noexcept
    {
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    static uint32_t floatToBits(float f) noexcept
    {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    }
    static double bitsToDouble(uint64_t b) noexcept
    {
        double d;
        std::memcpy(&d, &b, 8);
        return d;
    }
    static uint64_t doubleToBits(double d) noexcept
    {
        uint64_t b;
        std::memcpy(&b, &d, 8);
        return b;
    }
    // Para floats positivos, el orden de los bits coincide con el orden numérico.
    static void atomicMaxFloat(std::atomic<uint32_t> &a, float v) noexcept
    {
        const uint32_t nb = floatToBits(v);
        uint32_t cur = a.load(std::memory_order_relaxed);
        while (nb > cur && !a.compare_exchange_weak(cur, nb, std::memory_order_acq_rel)) {
        }
    }
    static void atomicAddDouble(std::atomic<uint64_t> &a, double v) noexcept
    {
        uint64_t cur = a.load(std::memory_order_relaxed);
        while (!a.compare_exchange_weak(cur, doubleToBits(bitsToDouble(cur) + v), std::memory_order_acq_rel)) {
        }
    }

    std::array<Channel, kMaxChannels> ch_{};
    std::atomic<uint32_t> channels_{0};
    std::atomic<uint64_t> samples_{0};
    std::atomic<float> clip_{kClipLinear};
};

} // namespace sg
