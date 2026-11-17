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
    const ChannelMap &map = maps_[active_.load(std::memory_order_acquire)];
    const uint32_t outCh = map.outputChannels();
    uint32_t offset = 0;
    uint64_t chunkTs = ts;
    while (offset < b.frames) {
        const uint32_t n = std::min(b.frames - offset, ring_.maxFrames());
        PcmBlock *blk = ring_.acquireWrite();
        if (!blk) {
            stats_.overflows.fetch_add(1, std::memory_order_relaxed);
            pendingDiscontinuity_ = true; // el siguiente bloque entregado marca el hueco
            return;
        }
        applyChannelMap(map, b.planes, b.channels, n, offset, blk->planes);
        blk->frames = n;
        blk->channels = outCh;
        blk->sampleRate = b.sampleRate;
        blk->timestampNs = chunkTs;
        blk->captureTimeNs = b.callbackTimeNs;
        blk->flags = b.flags | extraFlags | (pendingDiscontinuity_ ? kBlockDiscontinuity : 0u);
        if (blk->flags & kBlockDiscontinuity)
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
    for (std::size_t i = 0; i < kMaxSubs; ++i) {
        if (!owners_[i]) {
            auto s = std::make_shared<Subscription>(nextSubId_++, map);
            owners_[i] = s;
            rtSubs_[i].store(s.get(), std::memory_order_release);
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
            rtSubs_[i].store(nullptr, std::memory_order_release);
            waitForCallbackBoundary(); // el RT ya no puede tocar la suscripción
            owners_[i].reset();
        }
    }
    wantRunning_ = std::any_of(owners_.begin(), owners_.end(), [](auto &o) { return o != nullptr; });
}

void DeviceHub::updateMap(const std::shared_ptr<Subscription> &s, const ChannelMap &map)
{
    if (!s)
        return;
    std::lock_guard lk(mu_);
    const int cur = s->active_.load(std::memory_order_acquire);
    const int next = cur ^ 1;
    s->maps_[next] = map;
    s->active_.store(next, std::memory_order_release);
    // Antes de que otra actualización reescriba maps_[cur], el RT debe haber terminado
    // cualquier callback que lo estuviera usando.
    waitForCallbackBoundary();
}

std::size_t DeviceHub::subscriberCount() const
{
    std::lock_guard lk(mu_);
    return static_cast<std::size_t>(std::count_if(owners_.begin(), owners_.end(), [](auto &o) { return o != nullptr; }));
}

void DeviceHub::waitForCallbackBoundary()
{
    if (!inCallback_.load(std::memory_order_acquire))
        return;
    const uint64_t epoch = callbackEpoch_.load(std::memory_order_acquire);
    const int64_t deadline = SteadyClock::instance().nowNs() + 200 * kNsPerMs;
    while (inCallback_.load(std::memory_order_acquire) && callbackEpoch_.load(std::memory_order_acquire) == epoch) {
        if (SteadyClock::instance().nowNs() > deadline)
            break; // driver colgado dentro del callback: no bloquear OBS indefinidamente
        std::this_thread::yield();
    }
}

void DeviceHub::onCapture(const CaptureBlockView &b) noexcept
{
    inCallback_.store(true, std::memory_order_release);
    // El callback llega al final del bloque: el primer frame se capturó ~frames/rate antes.
    const int64_t blockNs =
        b.sampleRate ? static_cast<int64_t>(static_cast<double>(b.frames) * 1e9 / b.sampleRate) : 0;
    const auto ts = smoother_.next(b.callbackTimeNs - blockNs, b.frames, b.sampleRate);
    if (ts.discontinuity)
        resyncs_.fetch_add(1, std::memory_order_relaxed);
    const uint32_t extra = ts.discontinuity ? kBlockDiscontinuity : 0u;
    for (auto &slot : rtSubs_) {
        if (Subscription *s = slot.load(std::memory_order_acquire))
            s->writeRt(b, ts.timestampNs, extra);
    }
    callbackEpoch_.fetch_add(1, std::memory_order_acq_rel);
    inCallback_.store(false, std::memory_order_release);
}

Result DeviceHub::openAndStart()
{
    StreamConfig cfg;
    cfg.deviceId = deviceId_;
    Result r = transport_->open(cfg);
    if (!r.ok())
        return r;
    smoother_.reset();
    r = transport_->start(this);
    if (!r.ok()) {
        transport_->close();
        return r;
    }
    running_ = true;
    lastCallbacksSeen_ = transport_->snapshot().callbacks;
    lastProgressNs_ = clock_.nowNs();
    return r;
}

void DeviceHub::stopAndClose()
{
    if (running_)
        transport_->stop(); // al volver no hay más callbacks
    transport_->close();
    running_ = false;
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
    std::lock_guard lk(mu_);
    const int64_t now = clock_.nowNs();

    if (!wantRunning_) {
        if (running_)
            stopAndClose();
        inRetry_ = false;
        attempt_ = 0;
        return;
    }

    if (running_) {
        const uint32_t ev = transport_->takeEvents();
        const TransportStatus st = transport_->snapshot();
        bool needReopen = false;
        if (ev & (kEvtDeviceLost | kEvtResetRequested))
            needReopen = true;
        if (st.state == TransportState::Error)
            needReopen = true;
        // Bloqueo: sin callbacks nuevos durante stallTimeoutMs estando en marcha.
        if (st.callbacks != lastCallbacksSeen_) {
            lastCallbacksSeen_ = st.callbacks;
            lastProgressNs_ = now;
        } else if (now - lastProgressNs_ > policy_.stallTimeoutMs * kNsPerMs) {
            needReopen = true;
        }
        if (ev & kEvtFormatChanged) {
            // La frecuencia viaja en cada bloque; OBS remuestrea. Reanclar timestamps.
            // (El smoother detecta el cambio de frecuencia por sí mismo.)
        }
        if (needReopen) {
            stopAndClose();
            inRetry_ = true;
            attempt_ = 0;
            nextRetryNs_ = now + computeBackoffMs(attempt_) * kNsPerMs;
        }
        return;
    }

    // No está en marcha pero se desea: abrir (primera vez) o reintentar con backoff.
    if (inRetry_ && now < nextRetryNs_)
        return;
    if (inRetry_ && attempt_ >= policy_.maxAttempts)
        return; // requiere acción manual (requestReconnect)

    if (inRetry_)
        ++reconnectAttempts_;
    Result r = openAndStart();
    if (r.ok()) {
        if (inRetry_)
            ++successfulReconnects_;
        inRetry_ = false;
        attempt_ = 0;
        lastOpenError_ = ErrorCode::Ok;
        lastOpenErrorMessage_.clear();
    } else {
        lastOpenError_ = r.code;
        lastOpenErrorMessage_ = r.message;
        if (!inRetry_) {
            inRetry_ = true;
            attempt_ = 0;
        } else {
            ++attempt_;
        }
        nextRetryNs_ = now + computeBackoffMs(attempt_) * kNsPerMs;
    }
}

void DeviceHub::requestReconnect()
{
    std::lock_guard lk(mu_);
    if (running_)
        stopAndClose();
    inRetry_ = true;
    attempt_ = 0;
    nextRetryNs_ = 0; // inmediato en el siguiente poll()
}

void DeviceHub::shutdown()
{
    std::lock_guard lk(mu_);
    wantRunning_ = false;
    if (running_)
        stopAndClose();
    else
        transport_->close();
}

HubStatus DeviceHub::status() const
{
    std::lock_guard lk(mu_);
    HubStatus s;
    s.deviceId = deviceId_;
    s.backend = transport_->backendName();
    s.transport = transport_->snapshot();
    s.format = transport_->format();
    s.caps = transport_->capabilities();
    s.state = s.transport.state;
    if (inRetry_ && !running_)
        s.state = TransportState::Reconnecting;
    s.lastOpenError = lastOpenError_;
    s.lastOpenErrorMessage = lastOpenErrorMessage_;
    s.reconnectAttempts = reconnectAttempts_;
    s.successfulReconnects = successfulReconnects_;
    s.nextRetryNs = inRetry_ ? nextRetryNs_ : 0;
    s.wantRunning = wantRunning_;
    s.timestampResyncs = resyncs_.load();
    return s;
}

} // namespace sg
