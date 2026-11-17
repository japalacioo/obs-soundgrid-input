#pragma once
// Genera timestamps monotónicos y continuos para OBS a partir del número de frames,
// re-anclando al reloj del sistema solo cuando la deriva supera una tolerancia (lo que
// se registra como discontinuidad). Evita "jitter" de callback en los timestamps.
#include <cstdint>

namespace sg {

class TimestampSmoother {
public:
    struct Result {
        uint64_t timestampNs;
        bool discontinuity;
    };

    explicit TimestampSmoother(int64_t toleranceNs = 40'000'000, double correctionAlpha = 0.01)
        : tolerance_(toleranceNs), alpha_(correctionAlpha)
    {
    }

    void reset() noexcept { anchored_ = false; }

    // measuredStartNs: estimación del instante del primer frame del bloque según el reloj
    // monotónico (callbackTime - duración del bloque). Tiempo real: sin asignaciones.
    Result next(int64_t measuredStartNs, uint32_t frames, uint32_t sampleRate) noexcept
    {
        Result r{};
        if (sampleRate == 0) {
            r.timestampNs = static_cast<uint64_t>(measuredStartNs < 0 ? 0 : measuredStartNs);
            return r;
        }
        if (!anchored_ || sampleRate != rate_) {
            r.discontinuity = anchored_; // cambio de frecuencia = discontinuidad
            anchor(measuredStartNs, sampleRate);
        } else {
            const double error = static_cast<double>(measuredStartNs) - expected_;
            if (error > static_cast<double>(tolerance_) || error < -static_cast<double>(tolerance_)) {
                anchor(measuredStartNs, sampleRate);
                r.discontinuity = true;
                ++resyncs_;
            } else {
                // Corrección lenta de deriva: imperceptible, mantiene continuidad.
                expected_ += error * alpha_;
                driftNs_ = error;
            }
        }
        r.timestampNs = static_cast<uint64_t>(expected_ < 0 ? 0 : expected_);
        expected_ += static_cast<double>(frames) * 1e9 / static_cast<double>(sampleRate);
        return r;
    }

    uint64_t resyncs() const noexcept { return resyncs_; }
    double lastDriftNs() const noexcept { return driftNs_; }

private:
    void anchor(int64_t t, uint32_t rate) noexcept
    {
        anchored_ = true;
        rate_ = rate;
        expected_ = static_cast<double>(t);
        driftNs_ = 0;
    }

    int64_t tolerance_;
    double alpha_;
    bool anchored_ = false;
    uint32_t rate_ = 0;
    double expected_ = 0;
    double driftNs_ = 0;
    uint64_t resyncs_ = 0;
};

} // namespace sg
