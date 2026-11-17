#include "DeviceHub.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace sg {

// ---------------------------------------------------------------- Subscription

Subscription::Subscription(uint32_t id, const ChannelMap &map)
    : id_(id), ring_(kSlots, kMaxFramesPerSlot, PcmBlockRing::kMaxChannels)
{
    maps_[0] = map;
    maps_[1] = map;
}

ChannelMap Subscription::map() const
{
    return maps_[active_.load(std::memory_order_acquire)];
}

void Subscription::release(int64_t handoffNowNs)
{
    if (const PcmBlock *b = ring_.peekRead()) {
        const int64_t lat = handoffNowNs - b->captureTimeNs;
        stats_.lastHandoffLatencyNs.store(lat, std::memory_order_relaxed);
        int64_t prev = stats_.maxHandoffLatencyNs.load(std::memory_order_relaxed);
        while (lat > prev && !stats_.maxHandoffLatencyNs.compare_exchange_weak(prev, lat)) {
        }
        stats_.blocksDelivered.fetch_add(1, std::memory_order_relaxed);
    }
    ring_.releaseRead();
}

void Subscription::writeRt(const CaptureBlockView &b, uint64_t ts, uint32_t extraFlags) noexcept
{
    const ChannelMap &map = maps_[active_.load(std::memory_order_seq_cst)];
    const uint32_t outCh = map.outputChannels();
    uint32_t offset = 0;
    uint64_t chunkTs = ts;
    while (offset < b.frames) {
        const uint32_t n = std::min(b.frames - offset, ring_.maxFrames());
        PcmBlock *blk = ring_.acquireWrite();
        if (!blk) {
            // Desborde: se cuenta UNA vez (overflows). El siguiente bloque entregado lleva la
            // marca de discontinuidad para OBS, pero no se vuelve a contar en A08.
            stats_.overflows.fetch_add(1, std::memory_order_relaxed);
            pendingDiscontinuity_ = true;
            break;
        }
        applyChannelMap(map, b.planes, b.channels, n, offset, blk->planes);
        blk->frames = n;
        blk->channels = outCh;
        blk->sampleRate = b.sampleRate;
        blk->timestampNs = chunkTs;
        blk->captureTimeNs = b.callbackTimeNs;
        const uint32_t own = b.flags | extraFlags; // discontinuidades de origen (driver/timestamps)
        blk->flags = own | (pendingDiscontinuity_ ? kBlockDiscontinuity : 0u);
        if (own & kBlockDiscontinuity)
            stats_.discontinuities.fetch_add(1, std::memory_order_relaxed);
        pendingDiscontinuity_ = false;
        extraFlags = 0;
        blk->sequence = stats_.blocksWritten.fetch_add(1, std::memory_order_relaxed);
        meters_.accumulate(blk->planes, outCh, n);
        ring_.commitWrite();
        stats_.framesWritten.fetch_add(n, std::memory_order_relaxed);
        offset += n;
        chunkTs += static_cast<uint64_t>(static_cast<double>(n) * 1e9 / std::max<uint32_t>(b.sampleRate, 1));
    }
    stats_.lastWriteNs.store(b.callbackTimeNs, std::memory_order_relaxed);
    // Despertar al trabajador de salida (no bloqueante: WakeByAddress / __ulock_wake).
    dataSeq_.fetch_add(1, std::memory_order_release);
    dataSeq_.notify_one();
}

// ---------------------------------------------------------------- DeviceHub

DeviceHub::DeviceHub(std::unique_ptr<IAudioTransport> transport, std::string deviceId, IClock &clock,
                     RetryPolicy policy)
    : transport_(std::move(transport)), deviceId_(std::move(deviceId)), clock_(clock), policy_(policy)
{
    for (auto &s : rtSubs_)
        s.store(nullptr);
}

DeviceHub::~DeviceHub()
{
    shutdown();
}

std::shared_ptr<Subscription> DeviceHub::subscribe(const ChannelMap &map)
{
    std::lock_guard lk(mu_);
    if (shutdown_)
        return nullptr;
    for (std::size_t i = 0; i < kMaxSubs; ++i) {
        if (!owners_[i]) {
            auto s = std::make_shared<Subscription>(nextSubId_++, map);
            owners_[i] = s;
            rtSubs_[i].store(s.get(), std::memory_order_seq_cst);
            wantRunning_ = true;
            return s;
        }
    }
    return nullptr; // límite de suscripciones alcanzado
}

void DeviceHub::unsubscribe(const std::shared_ptr<Subscription> &s)
{
    if (!s)
        return;
    std::lock_guard lk(mu_);
    for (std::size_t i = 0; i < kMaxSubs; ++i) {
        if (owners_[i] == s) {
            rtSubs_[i].store(nullptr, std::memory_order_seq_cst);
            // Solo se libera si el callback en curso (si lo hay) terminó. Si el driver está
            // colgado dentro del callback, la suscripción queda aparcada hasta que el stream se
            // detenga (stopAndClose) y nunca se libera memoria que el RT pueda estar usando.
            if (!waitForCallbackBoundary())
                parked_.push_back(owners_[i]);
            owners_[i].reset();
        }
    }
    s->wakeConsumer(); // por si un consumidor sigue esperando datos de esta suscripción
    wantRunning_ = std::any_of(owners_.begin(), owners_.end(), [](auto &o) { return o != nullptr; });
}

void DeviceHub::updateMap(const std::shared_ptr<Subscription> &s, const ChannelMap &map)
{
    if (!s)
        return;
    std::lock_guard lk(mu_);
    const int cur = s->active_.load(std::memory_order_seq_cst);
    const int next = cur ^ 1;
    // maps_[next] puede estar en uso si la barrera anterior venció: esperar antes de escribir.
    if (!waitForCallbackBoundary())
        return; // driver colgado: no tocar el mapa (se reintenta en el siguiente update)
    s->maps_[next] = map;
    s->active_.store(next, std::memory_order_seq_cst);
    waitForCallbackBoundary();
}

std::size_t DeviceHub::subscriberCount() const
{
    std::lock_guard lk(mu_);
    return static_cast<std::size_t>(std::count_if(owners_.begin(), owners_.end(), [](auto &o) { return o != nullptr; }));
}

bool DeviceHub::isShutdown() const
{
    std::lock_guard lk(mu_);
    return shutdown_;
}

bool DeviceHub::waitForCallbackBoundary()
{
    // Protocolo tipo Dekker con seq_cst: el control publica el cambio (store seq_cst) y luego lee
    // inCallback_; el RT marca inCallback_ (store seq_cst) y luego lee las ranuras. El orden
    // total seq_cst impide que ambos lean el valor antiguo.
    if (!inCallback_.load(std::memory_order_seq_cst))
        return true;
    const uint64_t epoch = callbackEpoch_.load(std::memory_order_seq_cst);
    const int64_t deadline = SteadyClock::instance().nowNs() + 200 * kNsPerMs;
    while (inCallback_.load(std::memory_order_seq_cst) &&
           callbackEpoch_.load(std::memory_order_seq_cst) == epoch) {
        if (SteadyClock::instance().nowNs() > deadline)
            return false; // driver colgado dentro del callback
        std::this_thread::yield();
    }
    return true;
}

void DeviceHub::onCapture(const CaptureBlockView &b) noexcept
{
    inCallback_.store(true, std::memory_order_seq_cst);
    // El callback llega al final del bloque: el primer frame se capturó ~frames/rate antes.
    const int64_t blockNs =
        b.sampleRate ? static_cast<int64_t>(static_cast<double>(b.frames) * 1e9 / b.sampleRate) : 0;
    const auto ts = smoother_.next(b.callbackTimeNs - blockNs, b.frames, b.sampleRate);
    bool disc = ts.discontinuity;
    if (markNextDiscontinuity_.load(std::memory_order_relaxed) && markNextDiscontinuity_.exchange(false))
        disc = true; // primer bloque tras una reapertura: hubo un hueco
    if (ts.discontinuity)
        resyncs_.fetch_add(1, std::memory_order_relaxed);
    const uint32_t extra = disc ? kBlockDiscontinuity : 0u;
    for (auto &slot : rtSubs_) {
        if (Subscription *s = slot.load(std::memory_order_seq_cst))
            s->writeRt(b, ts.timestampNs, extra);
    }
    callbackEpoch_.fetch_add(1, std::memory_order_seq_cst);
    inCallback_.store(false, std::memory_order_seq_cst);
}

Result DeviceHub::openAndStart()
{
    StreamConfig cfg;
    cfg.deviceId = deviceId_;
    Result r = transport_->open(cfg);
    if (!r.ok())
        return r;
    smoother_.reset(); // el stream está detenido: el RT no lo usa ahora
    bool reopen;
    {
        std::lock_guard lk(mu_);
        reopen = everOpened_;
    }
    markNextDiscontinuity_.store(reopen);
    r = transport_->start(this);
    if (!r.ok()) {
        transport_->close();
        return r;
    }
    lastCallbacksSeen_ = transport_->snapshot().callbacks;
    lastProgressNs_ = clock_.nowNs();
    return r;
}

void DeviceHub::stopAndClose()
{
    transport_->stop(); // al volver no hay más callbacks
    transport_->close();
    std::lock_guard lk(mu_);
    parked_.clear(); // ya no hay callbacks: las suscripciones aparcadas se pueden liberar
}

int64_t DeviceHub::computeBackoffMs(uint32_t attempt)
{
    double d = static_cast<double>(policy_.initialDelayMs) * std::pow(policy_.multiplier, attempt);
    d = std::min(d, static_cast<double>(policy_.maxDelayMs));
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    const double u = (static_cast<double>(rng_) / 4294967295.0) * 2.0 - 1.0; // [-1, 1]
    d *= 1.0 + policy_.jitter * u;
    return static_cast<int64_t>(std::max(d, 50.0));
}

void DeviceHub::poll()
{
    std::lock_guard op(opMu_);
    const int64_t now = clock_.nowNs();
    bool want, running, inRetry, gaveUp;
    uint32_t attempt;
    int64_t nextRetry;
    {
        std::lock_guard lk(mu_);
        if (shutdown_)
            return;
        want = wantRunning_;
        running = running_;
        inRetry = inRetry_;
        gaveUp = gaveUp_;
        attempt = attempt_;
        nextRetry = nextRetryNs_;
    }

    if (!want) {
        if (running)
            stopAndClose();
        std::lock_guard lk(mu_);
        running_ = false;
        inRetry_ = false;
        gaveUp_ = false;
        attempt_ = 0;
        return;
    }

    if (running) {
        const uint32_t ev = transport_->takeEvents();
        const TransportStatus st = transport_->snapshot();
        bool needReopen = (ev & (kEvtDeviceLost | kEvtResetRequested)) || st.state == TransportState::Error;
        if (st.callbacks != lastCallbacksSeen_) {
            lastCallbacksSeen_ = st.callbacks;
            lastProgressNs_ = now;
        } else if (now - lastProgressNs_ > policy_.stallTimeoutMs * kNsPerMs) {
            needReopen = true; // bloqueo: sin callbacks estando en marcha
        }
        // (kEvtFormatChanged: la frecuencia viaja en cada bloque y el suavizador re-ancla.)
        if (needReopen) {
            stopAndClose();
            std::lock_guard lk(mu_);
            // Un fallo poco después del anterior incidente (dispositivo intermitente) hace crecer
            // el backoff; tras un periodo estable se vuelve a empezar desde el retardo inicial.
            const bool flapping = lastIncidentNs_ >= 0 && now - lastIncidentNs_ < policy_.stableResetMs * kNsPerMs;
            attempt_ = flapping ? attempt_ + 1 : 0;
            lastIncidentNs_ = now;
            running_ = false;
            inRetry_ = true;
            if (attempt_ >= policy_.maxAttempts)
                gaveUp_ = true;
            nextRetryNs_ = now + computeBackoffMs(attempt_) * kNsPerMs;
        }
        return;
    }

    // No está en marcha pero se desea: abrir (primera vez) o reintentar con backoff.
    if (gaveUp)
        return; // requiere acción manual (requestReconnect)
    if (inRetry && now < nextRetry)
        return;

    Result r = openAndStart(); // sin mu_: la UI y el monitor no esperan al driver
    std::lock_guard lk(mu_);
    if (inRetry)
        ++reconnectAttempts_;
    if (r.ok()) {
        if (inRetry)
            ++successfulReconnects_;
        running_ = true;
        everOpened_ = true;
        inRetry_ = false;
        lastOpenError_ = ErrorCode::Ok;
        lastOpenErrorMessage_.clear();
    } else {
        lastOpenError_ = r.code;
        lastOpenErrorMessage_ = r.message;
        if (!inRetry) {
            inRetry_ = true;
            attempt_ = 0;
        } else {
            attempt_ = attempt + 1;
        }
        if (attempt_ >= policy_.maxAttempts)
            gaveUp_ = true;
        nextRetryNs_ = now + computeBackoffMs(attempt_) * kNsPerMs;
    }
}

void DeviceHub::requestReconnect()
{
    std::lock_guard op(opMu_);
    bool running;
    {
        std::lock_guard lk(mu_);
        if (shutdown_)
            return;
        running = running_;
    }
    if (running)
        stopAndClose();
    std::lock_guard lk(mu_);
    running_ = false;
    inRetry_ = true;
    gaveUp_ = false;
    attempt_ = 0;
    lastIncidentNs_ = -1;
    nextRetryNs_ = 0; // inmediato en el siguiente poll()
}

void DeviceHub::shutdown()
{
    std::lock_guard op(opMu_);
    {
        std::lock_guard lk(mu_);
        if (shutdown_)
            return;
        shutdown_ = true;
        wantRunning_ = false;
    }
    stopAndClose(); // idempotente en los transportes
    std::lock_guard lk(mu_);
    running_ = false;
    inRetry_ = false;
}

HubStatus DeviceHub::status() const
{
    HubStatus s;
    s.deviceId = deviceId_;
    s.backend = transport_->backendName();
    s.transport = transport_->snapshot(); // atómicos / mutex interno corto del transporte
    s.format = transport_->format();
    s.caps = transport_->capabilities();
    std::lock_guard lk(mu_);
    s.state = s.transport.state;
    if (inRetry_ && !running_)
        s.state = gaveUp_ ? TransportState::Error : TransportState::Reconnecting;
    s.lastOpenError = lastOpenError_;
    s.lastOpenErrorMessage = lastOpenErrorMessage_;
    if (gaveUp_)
        s.lastOpenErrorMessage = "reintentos agotados (" + std::to_string(attempt_) +
                                 "); pulsa \"Reconectar\"" +
                                 (lastOpenErrorMessage_.empty() ? std::string() : " · " + lastOpenErrorMessage_);
    s.reconnectAttempts = reconnectAttempts_;
    s.successfulReconnects = successfulReconnects_;
    s.nextRetryNs = inRetry_ && !gaveUp_ ? nextRetryNs_ : 0;
    s.wantRunning = wantRunning_;
    s.gaveUp = gaveUp_;
    s.timestampResyncs = resyncs_.load();
    return s;
}

} // namespace sg
