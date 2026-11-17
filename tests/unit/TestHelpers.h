#pragma once
#include "common/Clock.h"
#include "observability/AlertEngine.h"
#include "observability/HealthSnapshot.h"

#include <cmath>
#include <vector>

namespace sgtest {

// Potencia de una frecuencia en un bloque (algoritmo de Goertzel).
inline double goertzel(const float *x, std::size_t n, double freq, double rate)
{
    const double w = 2.0 * 3.14159265358979323846 * freq / rate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// Devuelve la frecuencia dominante entre candidatas.
inline double dominant(const std::vector<float> &x, const std::vector<double> &cands, double rate)
{
    double best = 0, bestF = 0;
    for (double f : cands) {
        const double p = goertzel(x.data(), x.size(), f, rate);
        if (p > best) {
            best = p;
            bestF = f;
        }
    }
    return bestF;
}

inline double rmsDb(const std::vector<float> &x)
{
    double ss = 0;
    for (float v : x)
        ss += double(v) * v;
    return sg::linearToDbfs(std::sqrt(ss / std::max<std::size_t>(x.size(), 1)));
}

// Simulador de instantáneas para el motor de alertas con reloj virtual.
struct SnapSim {
    sg::HealthSnapshot s;
    int64_t tickMs = 250;
    bool framesFlowing = true;
    double rmsDb = -20.0;
    double peakDb = -10.0;
    bool clipping = false;

    SnapSim()
    {
        s.nowNs = 10 * sg::kNsPerSec;
        s.sourceName = "Main L/R";
        s.sourceRunning = true;
        s.backend = "mock";
        s.transportState = sg::TransportState::Streaming;
        s.sampleRate = 48000;
        s.outChannels = 2;
        s.nicSelected = true;
        s.nicName = "Ethernet SoundGrid";
        s.nic.present = true;
        s.nic.linkUp = true;
        s.nic.speedBps = 1'000'000'000ull;
        s.nic.inErrors = 0;
        s.nic.inDiscards = 0;
        s.nic.outErrors = 0;
        s.nic.source = "mock";
        s.pluginLatencyMs = sg::Observed<double>::measured(5.0, "test");
    }

    // Avanza un tick y devuelve la instantánea.
    const sg::HealthSnapshot &next()
    {
        s.nowNs += tickMs * sg::kNsPerMs;
        s.meter = {};
        if (framesFlowing) {
            const uint64_t frames = static_cast<uint64_t>(48 * tickMs);
            s.framesWritten += frames;
            s.lastFrameNs = s.nowNs;
            s.meter.channels = 2;
            s.meter.samples = frames;
            for (int c = 0; c < 2; ++c) {
                s.meter.rms[c] = sg::dbfsToLinear(rmsDb);
                s.meter.peak[c] = static_cast<float>(sg::dbfsToLinear(clipping ? 0.0 : peakDb));
                s.meter.clippedSamples[c] = clipping ? 10 : 0;
            }
        }
        return s;
    }
};

struct EventLog {
    std::vector<sg::AlertEvent> all;
    void add(const std::vector<sg::AlertEvent> &v) { all.insert(all.end(), v.begin(), v.end()); }
    int count(sg::RuleId r, const std::string &transition, int notify = -1) const
    {
        int n = 0;
        for (auto &e : all)
            if (e.rule == r && e.transition == transition && (notify < 0 || e.notify == (notify == 1)))
                ++n;
        return n;
    }
    int notified() const
    {
        int n = 0;
        for (auto &e : all)
            n += e.notify ? 1 : 0;
        return n;
    }
};

// Ejecuta el motor durante 'ms' milisegundos de tiempo virtual.
inline void run(sg::AlertEngine &eng, SnapSim &sim, EventLog &log, int64_t ms)
{
    for (int64_t t = 0; t < ms; t += sim.tickMs)
        log.add(eng.evaluate(sim.next()));
}

} // namespace sgtest
