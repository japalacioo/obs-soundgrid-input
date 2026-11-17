#pragma once
// Transporte simulado para CI y pruebas: genera silencio, tonos distinguibles por canal,
// ruido rosa e impulsos, e inyecta fallos (dropout, bloqueo, desconexión, deriva,
// clipping, cambio de frecuencia, driver ocupado). NO demuestra conexión Waves.
#include "IAudioTransport.h"
#include "../common/Clock.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace sg {

enum class MockSignal { Silence, Sine, PinkNoise, Impulse, ClippedSine };
const char *mockSignalName(MockSignal s);

struct MockConfig {
    uint32_t channels = 8;          // 1..128
    uint32_t sampleRate = 48000;
    uint32_t bufferFrames = 256;
    MockSignal signal = MockSignal::Sine;
    double levelDbfs = -20.0;
    // Frecuencia del canal c: baseFrequency * (c + 1). Canal 1 = 1 kHz por defecto.
    double baseFrequency = 1000.0;
    bool realtime = true;           // false: bloques solo con pump() (reloj virtual)
};

enum class MockFault {
    None,
    Stall,          // el driver deja de llamar al callback (A02)
    Disconnect,     // dispositivo perdido (A03)
    BusyOnOpen,     // open() falla con Busy
    NotFoundOnOpen, // open() falla con NotFound
};

class MockTransport final : public IAudioTransport {
public:
    explicit MockTransport(MockConfig cfg = {}, IClock *clock = nullptr);
    ~MockTransport() override;

    const char *backendName() const override { return "mock"; }
    TransportCapabilities capabilities() const override;
    std::vector<DeviceInfo> enumerateDevices() override;
    std::vector<ChannelInfo> channels() const override;
    Result open(const StreamConfig &cfg) override;
    Result start(ICaptureSink *sink) override;
    void stop() override;
    void close() override;
    StreamFormat format() const override;
    TransportStatus snapshot() const override;
    uint32_t takeEvents() override;

    // --- Control de simulación (hilo de control o pruebas) ---
    void setSignal(MockSignal s, double levelDbfs);
    void setFault(MockFault f);
    void setDriftPpm(double ppm);                 // deriva del reloj del "driver"
    void changeSampleRate(uint32_t newRate);      // emite kEvtFormatChanged
    void signalOverload();                        // xrun reportado por el driver
    // Genera n bloques de forma síncrona (modo realtime=false). Avanza el reloj virtual si
    // el reloj inyectado es VirtualClock.
    void pump(uint32_t blocks);
    uint64_t generatedBlocks() const { return generated_.load(); }
    uint64_t opens() const { return opens_.load(); }

private:
    void threadMain();
    void produceBlock(int64_t callbackTimeNs);
    float nextSample(uint32_t ch);

    MockConfig cfg_;
    IClock *clock_;
    std::atomic<MockSignal> signal_;
    std::atomic<double> levelLinear_;
    std::atomic<MockFault> fault_{MockFault::None};
    std::atomic<double> driftPpm_{0.0};
    std::atomic<uint32_t> pendingRate_{0};

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> open_{false};
    std::atomic<ICaptureSink *> sink_{nullptr};
    std::atomic<uint32_t> events_{0};
    std::atomic<uint32_t> rate_{48000};

    // Estado del generador (solo hilo productor)
    std::vector<std::vector<float>> buffers_;
    std::vector<const float *> planes_;
    std::vector<double> phase_;
    std::vector<std::array<double, 7>> pink_;
    uint32_t rng_ = 0x12345678u;
    uint64_t sampleCounter_ = 0;

    // Estado observable
    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> frames_{0};
    std::atomic<int64_t> lastCallbackNs_{0};
    std::atomic<uint64_t> overloads_{0};
    std::atomic<uint64_t> generated_{0};
    std::atomic<uint64_t> opens_{0};
    std::atomic<TransportState> state_{TransportState::Discovered};
};

} // namespace sg
