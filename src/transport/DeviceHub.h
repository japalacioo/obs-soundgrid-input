#pragma once
// DeviceHub: abre UN dispositivo una sola vez y reparte su audio a varias suscripciones
// (fuentes OBS). Motivos: ASIO admite un único driver por proceso y muchos drivers un
// único cliente; OBS limita cada fuente a 8 canales, así que los "stems" se resuelven con
// varias fuentes sobre el mismo stream sin reabrir el driver.
//
// Plano de datos (tiempo real): onCapture() -> mapa de canales -> PcmBlockRing por
// suscripción + medidores. Plano de control: subscribe/updateMap/poll/start/stop.
#include "IAudioTransport.h"
#include "../audio/ChannelMap.h"
#include "../audio/Metering.h"
#include "../audio/PcmBlockRing.h"
#include "../audio/TimestampSmoother.h"
#include "../common/Clock.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sg {

// Estadísticas de una suscripción (todas atómicas: escritas en RT, leídas por el monitor).
struct SubscriptionStats {
    std::atomic<uint64_t> blocksWritten{0};
    std::atomic<uint64_t> framesWritten{0};
    std::atomic<uint64_t> overflows{0};       // bloques descartados por anillo lleno
    std::atomic<uint64_t> discontinuities{0}; // saltos de timestamp / pérdidas
    std::atomic<int64_t> lastWriteNs{0};
    std::atomic<uint64_t> blocksDelivered{0}; // entregados a OBS por el consumidor
    std::atomic<int64_t> lastHandoffLatencyNs{-1}; // callback -> entrega a OBS (medido)
    std::atomic<int64_t> maxHandoffLatencyNs{0};
};

class Subscription {
public:
    static constexpr uint32_t kSlots = 64;
    static constexpr uint32_t kMaxFramesPerSlot = 2048;

    explicit Subscription(uint32_t id, const ChannelMap &map);

    uint32_t id() const { return id_; }
    ChannelMap map() const; // copia (hilo de control)

    // --- Consumidor (trabajador que entrega a OBS) ---
    const PcmBlock *peek() { return ring_.peekRead(); }
    // Libera el bloque y registra la latencia medida captura->entrega.
    void release(int64_t handoffNowNs);
    std::size_t queuedBlocks() const { return ring_.queuedBlocks(); }
    // Espera sin locks (atomic::wait) a que el productor publique datos nuevos.
    uint32_t dataSeq() const { return dataSeq_.load(std::memory_order_acquire); }
    void waitForData(uint32_t seen) const { dataSeq_.wait(seen, std::memory_order_acquire); }
    void wakeConsumer()
    {
        dataSeq_.fetch_add(1, std::memory_order_acq_rel);
        dataSeq_.notify_all();
    }

    MeterBank &meters() { return meters_; }
    SubscriptionStats &stats() { return stats_; }
    const SubscriptionStats &stats() const { return stats_; }

private:
    friend class DeviceHub;
    void writeRt(const CaptureBlockView &b, uint64_t ts, uint32_t extraFlags) noexcept;

    uint32_t id_;
    PcmBlockRing ring_;
    MeterBank meters_;
    SubscriptionStats stats_;
    // Doble buffer del mapa: el control escribe el inactivo y conmuta el índice; el RT lee
    // el activo. DeviceHub espera un callback completo antes de volver a escribir.
    std::array<ChannelMap, 2> maps_;
    std::atomic<int> active_{0};
    std::atomic<uint32_t> dataSeq_{0};
    bool pendingDiscontinuity_ = false; // solo RT
    int64_t lastCaptureNs_ = 0;         // solo consumidor
};

struct HubStatus {
    std::string deviceId;
    std::string backend;
    TransportState state = TransportState::Unavailable;
    StreamFormat format;
    TransportStatus transport;
    TransportCapabilities caps;
    ErrorCode lastOpenError = ErrorCode::Ok;
    std::string lastOpenErrorMessage;
    uint64_t reconnectAttempts = 0;
    uint64_t successfulReconnects = 0;
    int64_t nextRetryNs = 0;
    bool wantRunning = false;
    bool gaveUp = false; // reintentos agotados: requiere "Reconectar"
    uint64_t timestampResyncs = 0;
};

// Política de reintento con backoff exponencial acotado y jitter (SDD-10).
struct RetryPolicy {
    int64_t initialDelayMs = 1000;
    int64_t maxDelayMs = 30000;
    double multiplier = 2.0;
    double jitter = 0.2;           // ±20 %
    uint32_t maxAttempts = 50;     // por racha de fallos; luego se requiere "Reconectar" manual
    int64_t stallTimeoutMs = 3000; // sin callbacks estando en marcha => reabrir receptor
    int64_t stableResetMs = 30000; // un incidente tras este tiempo estable reinicia el backoff
};

class DeviceHub final : public ICaptureSink {
public:
    DeviceHub(std::unique_ptr<IAudioTransport> transport, std::string deviceId, IClock &clock,
              RetryPolicy policy = {});
    ~DeviceHub() override;

    const std::string &deviceId() const { return deviceId_; }
    IAudioTransport &transport() { return *transport_; }

    // --- Control (cualquier hilo salvo el de audio). Nunca bloquean por llamadas al driver. ---
    std::shared_ptr<Subscription> subscribe(const ChannelMap &map);
    void unsubscribe(const std::shared_ptr<Subscription> &s);
    void updateMap(const std::shared_ptr<Subscription> &s, const ChannelMap &map);
    std::size_t subscriberCount() const;
    bool isShutdown() const;

    // Operaciones con el driver (hilo de control). Serializadas por opMu_, que NO se toma en
    // las funciones de estado: la UI y el monitor no esperan a open()/init() lentos.
    void poll();
    void requestReconnect(); // acción manual del operador ("Reconectar")
    void shutdown();         // detiene y cierra (definitivo)

    HubStatus status() const;

    // --- Tiempo real ---
    void onCapture(const CaptureBlockView &block) noexcept override;

private:
    static constexpr std::size_t kMaxSubs = 32;

    Result openAndStart();
    void stopAndClose();
    bool waitForCallbackBoundary(); // false si venció el plazo
    int64_t computeBackoffMs(uint32_t attempt);

    std::unique_ptr<IAudioTransport> transport_;
    std::string deviceId_;
    IClock &clock_;
    RetryPolicy policy_;

    std::mutex opMu_;       // serializa operaciones con el driver
    mutable std::mutex mu_; // estado y suscripciones (secciones cortas)
    std::array<std::shared_ptr<Subscription>, kMaxSubs> owners_{};
    std::array<std::atomic<Subscription *>, kMaxSubs> rtSubs_{};
    std::vector<std::shared_ptr<Subscription>> parked_; // retiradas sin barrera confirmada
    uint32_t nextSubId_ = 1;

    std::atomic<bool> inCallback_{false};
    std::atomic<uint64_t> callbackEpoch_{0};
    std::atomic<bool> markNextDiscontinuity_{false}; // tras una reapertura
    TimestampSmoother smoother_; // solo RT (reiniciado con el stream detenido)
    std::atomic<uint64_t> resyncs_{0};

    // Estado de control (protegido por mu_)
    bool running_ = false;
    bool wantRunning_ = false;
    bool inRetry_ = false;
    bool gaveUp_ = false;
    bool shutdown_ = false;
    bool everOpened_ = false;
    uint32_t attempt_ = 0;
    int64_t nextRetryNs_ = 0;
    int64_t lastIncidentNs_ = -1;
    uint64_t reconnectAttempts_ = 0;
    uint64_t successfulReconnects_ = 0;
    ErrorCode lastOpenError_ = ErrorCode::Ok;
    std::string lastOpenErrorMessage_;
    // Solo bajo opMu_
    uint64_t lastCallbacksSeen_ = 0;
    int64_t lastProgressNs_ = 0;
    uint32_t rng_ = 0x9e3779b9u;
};

} // namespace sg
