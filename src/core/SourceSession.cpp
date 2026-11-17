#include "SourceSession.h"

#include "../platform/INetworkTelemetry.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace sg {

// ================================================================ SourceSession

SourceSession::SourceSession(CoreRuntime &rt, std::string name, OutputFn out)
    : rt_(rt), name_(std::move(name)), out_(std::move(out))
{
    engine_.restart(rt_.clock().nowNs());
}

SourceSession::~SourceSession()
{
    stopOutput();
    std::shared_ptr<DeviceHub> hub;
    {
        std::lock_guard lk(mu_);
        if (hub_ && sub_)
            hub_->unsubscribe(sub_);
        hub = hub_;
        hub_.reset();
        sub_.reset();
    }
    rt_.transports().releaseIfUnused(hub);
}

SourceConfig SourceSession::config() const
{
    std::lock_guard lk(mu_);
    return cfg_;
}

void SourceSession::rename(const std::string &name)
{
    std::lock_guard lk(mu_);
    name_ = name;
}

std::shared_ptr<DeviceHub> SourceSession::hub() const
{
    std::lock_guard lk(mu_);
    return hub_;
}

std::shared_ptr<Subscription> SourceSession::subscription() const
{
    std::lock_guard lk(mu_);
    return sub_;
}

std::vector<ChannelInfo> SourceSession::deviceChannels() const
{
    auto h = hub();
    return h ? h->transport().channels() : std::vector<ChannelInfo>{};
}

void SourceSession::apply(const SourceConfig &cfg)
{
    std::shared_ptr<DeviceHub> oldHub;
    bool restartOutput = false;
    {
        std::lock_guard lk(mu_);
        const bool deviceChanged = cfg.deviceId != cfg_.deviceId || (!cfg.enabled) != (!cfg_.enabled) || !hub_;
        const bool restartGrace = deviceChanged;
        engine_.setConfig(cfg.alerts);

        std::string why;
        const uint32_t devCh = hub_ ? hub_->status().format.channels : 0;
        configError_.clear();
        if (devCh && !cfg.map.validate(devCh, &why))
            configError_ = why;

        if (deviceChanged) {
            restartOutput = true;
            if (hub_ && sub_)
                hub_->unsubscribe(sub_);
            oldHub = hub_;
            hub_.reset();
            sub_.reset();
            if (cfg.enabled && !cfg.deviceId.empty()) {
                hub_ = rt_.transports().acquire(cfg.deviceId);
                if (hub_)
                    sub_ = hub_->subscribe(cfg.map);
                deviceName_.clear();
                deviceLooksSoundGrid_ = false;
                for (const auto &d : rt_.transports().enumerate(TransportManager::backendOf(cfg.deviceId)))
                    if (d.id == cfg.deviceId) {
                        deviceName_ = d.name;
                        deviceLooksSoundGrid_ = d.looksLikeWavesSoundGrid;
                    }
                if (!hub_)
                    configError_ = "backend no disponible en esta plataforma: " + cfg.deviceId;
                else if (!sub_)
                    configError_ = "demasiadas fuentes sobre el mismo dispositivo";
            }
        } else if (hub_ && sub_ && !(cfg.map == cfg_.map)) {
            hub_->updateMap(sub_, cfg.map); // sin reabrir el driver (T52)
        }
        cfg_ = cfg;
        if (restartGrace)
            engine_.restart(rt_.clock().nowNs());
    }
    if (restartOutput) {
        stopOutput();
        startOutput();
    }
    if (oldHub)
        rt_.transports().releaseIfUnused(oldHub);
}

void SourceSession::reconnect()
{
    if (auto h = hub())
        h->requestReconnect();
}

void SourceSession::startOutput()
{
    auto sub = subscription();
    if (!sub)
        return;
    outStop_ = false;
    outSub_ = sub;
    outThread_ = std::thread([this, sub] { outputLoop(sub); });
}

void SourceSession::stopOutput()
{
    outStop_ = true;
    // Despertar la suscripción en la que ESPERA el hilo (puede no ser la actual: al cambiar
    // de dispositivo, apply() ya sustituyó sub_ y la antigua no volverá a recibir datos).
    if (outSub_)
        outSub_->wakeConsumer();
    if (outThread_.joinable())
        outThread_.join();
    outSub_.reset();
}

void SourceSession::outputLoop(std::shared_ptr<Subscription> sub)
{
    // Trabajador de audio: entrega bloques a OBS fuera del callback del driver.
    while (!outStop_.load(std::memory_order_acquire)) {
        const uint32_t seen = sub->dataSeq();
        while (const PcmBlock *b = sub->peek()) {
            if (out_ && b->frames)
                out_(*b);
            sub->release(rt_.clock().nowNs());
            if (outStop_.load(std::memory_order_acquire))
                break;
        }
        if (outStop_.load(std::memory_order_acquire))
            break;
        sub->waitForData(seen);
    }
}

HealthSnapshot SourceSession::buildSnapshot(int64_t now)
{
    HealthSnapshot s;
    s.nowNs = now;
    s.sourceName = name_;
    s.sourceRunning = cfg_.enabled && !cfg_.deviceId.empty();
    s.armed = cfg_.armed;
    s.nicSelected = !cfg_.nicId.empty();
    s.nicName = cfg_.nicName;
    s.expectedSpeedBps = cfg_.alerts.expectedLinkBps;
    s.endToEndLatencyMs = Observed<double>::unknown("sin instrumentación de extremo a extremo");

    if (hub_) {
        const HubStatus hs = hub_->status();
        s.backend = hs.backend;
        s.transportState = hs.state;
        s.lastError = hs.lastOpenError != ErrorCode::Ok ? hs.lastOpenError : hs.transport.lastError;
        s.lastErrorMessage = !hs.lastOpenErrorMessage.empty() ? hs.lastOpenErrorMessage : hs.transport.lastErrorMessage;
        s.reconnecting = hs.state == TransportState::Reconnecting;
        s.reconnectAttempts = hs.reconnectAttempts;
        s.sampleRate = hs.format.sampleRate;
        s.driverOverloads = hs.transport.overloads;
        s.driverOverloadsObservable = hs.transport.overloadsObservable;
        if (hs.format.driverInputLatencyMs >= 0)
            s.driverLatencyMs = Observed<double>::estimated(hs.format.driverInputLatencyMs, "informado por el driver");
        s.deviceName = deviceName_.empty() ? cfg_.deviceId : deviceName_;
        s.deviceLooksSoundGrid = deviceLooksSoundGrid_;
        // Validar el mapa cuando el dispositivo ya expone su número de canales.
        std::string why;
        if (hs.format.channels && !cfg_.map.validate(hs.format.channels, &why))
            configError_ = why;
        else if (hs.format.channels && configError_.find("fuera de rango") != std::string::npos)
            configError_.clear();
    } else if (s.sourceRunning) {
        s.transportState = TransportState::Unavailable;
        s.lastError = ErrorCode::NotFound;
        s.lastErrorMessage = configError_;
    }
    if (sub_) {
        const auto &st = sub_->stats();
        s.framesWritten = st.framesWritten.load();
        s.lastFrameNs = st.lastWriteNs.load();
        s.overflows = st.overflows.load();
        s.discontinuities = st.discontinuities.load();
        s.meter = sub_->meters().takeWindow();
        s.outChannels = cfg_.map.outputChannels();
        const uint64_t delivered = st.blocksDelivered.load();
        const int64_t lat = st.lastHandoffLatencyNs.load();
        if (delivered != lastDelivered_ && lat >= 0)
            s.pluginLatencyMs = Observed<double>::measured(static_cast<double>(lat) / 1e6, "callback->OBS");
        lastDelivered_ = delivered;
        if (s.sampleRate) {
            const double blockMs = s.framesWritten && st.blocksWritten.load()
                                       ? 1000.0 * (static_cast<double>(s.framesWritten) / st.blocksWritten.load()) /
                                             s.sampleRate
                                       : 0.0;
            s.queueDelayMs = Observed<double>::estimated(blockMs * sub_->queuedBlocks(), "ocupación de cola");
        }
    }
    if (s.nicSelected)
        s.nic = rt_.sampleNic(cfg_.nicId);
    return s;
}

std::vector<AlertEvent> SourceSession::tick(int64_t nowNs)
{
    std::lock_guard lk(mu_);
    last_ = buildSnapshot(nowNs);
    return engine_.evaluate(last_);
}

std::optional<AlertEvent> SourceSession::acknowledge(uint64_t incidentId)
{
    std::lock_guard lk(mu_);
    return engine_.acknowledge(incidentId, rt_.clock().nowNs());
}

void SourceSession::muteNotifications(int64_t forMs)
{
    std::lock_guard lk(mu_);
    engine_.muteNotificationsUntil(rt_.clock().nowNs() + forMs * kNsPerMs);
}

namespace {
std::string f1(const char *fmt, double v)
{
    char b[96];
    std::snprintf(b, sizeof(b), fmt, v);
    return b;
}
} // namespace

ConnectivityIndicators SourceSession::indicators(const HealthSnapshot &s) const
{
    ConnectivityIndicators i;
    if (!s.nicSelected)
        i.nic = "No supervisada (elige la NIC SoundGrid)";
    else if (!s.nic.present)
        i.nic = "NIC no presente";
    else if (!s.nic.linkUp)
        i.nic = "NO DISPONIBLE";
    else if (!*s.nic.linkUp)
        i.nic = "DOWN (sin enlace)";
    else
        i.nic = s.nic.speedBps ? "UP " + f1("%.0f Mbps", *s.nic.speedBps / 1e6) : "UP (velocidad NO DISPONIBLE)";

    if (s.backend.empty())
        i.driver = "Sin dispositivo";
    else {
        i.driver = transportStateName(s.transportState);
        if (s.lastError != ErrorCode::Ok && s.transportState != TransportState::Streaming)
            i.driver += std::string(" · ") + s.lastErrorMessage;
    }
    i.deviceKind = s.backend == "mock"            ? "Simulador (NO es una conexión Waves)"
                   : s.deviceLooksSoundGrid       ? "Driver Waves SoundGrid (identificado por nombre)"
                   : s.backend.empty()            ? "-"
                                                  : "Otro dispositivo de audio";
    i.patch = "No verificado (requiere confirmación en la LV1 o prueba HIL)";
    i.lv1 = "No identificada (no existe API autorizada)";
    double rmsMax = 0;
    for (uint32_t c = 0; c < s.meter.channels && c < 8; ++c)
        rmsMax = std::max(rmsMax, s.meter.rms[c]);
    if (s.meter.samples > 0)
        i.audio = "Recibiendo PCM · " + (rmsMax > 0 ? f1("%.1f dBFS RMS", linearToDbfs(rmsMax)) : "silencio digital");
    else
        i.audio = s.sourceRunning ? "Sin frames" : "Fuente detenida";
    i.latency = (s.pluginLatencyMs.known() ? f1("Plugin (medido): %.1f ms", *s.pluginLatencyMs.value)
                                           : std::string("Plugin: NO DISPONIBLE")) +
                (s.driverLatencyMs.known() ? f1(" · Driver (informado): %.1f ms", *s.driverLatencyMs.value) : "") +
                " · Extremo a extremo: NO DISPONIBLE";
    return i;
}

SessionStatus SourceSession::status() const
{
    std::lock_guard lk(mu_);
    SessionStatus st;
    st.global = engine_.globalStatus();
    st.last = last_;
    st.indicators = indicators(last_);
    st.openIncidents = engine_.openIncidents();
    st.configError = configError_;
    st.blocksDelivered = sub_ ? sub_->stats().blocksDelivered.load() : 0;
    return st;
}

// ================================================================ HealthMonitor

HealthMonitor::HealthMonitor(CoreRuntime &rt, int64_t periodMs) : rt_(rt), periodMs_(periodMs) {}

HealthMonitor::~HealthMonitor()
{
    stop();
}

void HealthMonitor::add(const std::shared_ptr<SourceSession> &s)
{
    std::lock_guard lk(mu_);
    sessions_.push_back(s);
}

void HealthMonitor::remove(const SourceSession *s)
{
    std::lock_guard lk(mu_);
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [s](const std::weak_ptr<SourceSession> &w) {
                                       auto p = w.lock();
                                       return !p || p.get() == s;
                                   }),
                    sessions_.end());
}

void HealthMonitor::tickOnce()
{
    std::vector<std::shared_ptr<SourceSession>> live;
    {
        std::lock_guard lk(mu_);
        for (auto &w : sessions_)
            if (auto p = w.lock())
                live.push_back(p);
    }
    const int64_t now = rt_.clock().nowNs();
    for (auto &s : live)
        for (const auto &e : s->tick(now))
            rt_.dispatcher().publish(e);
}

void HealthMonitor::start()
{
    if (thread_.joinable())
        return;
    stop_ = false;
    thread_ = std::thread([this] {
        while (!stop_) {
            tickOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(periodMs_));
        }
    });
}

void HealthMonitor::stop()
{
    stop_ = true;
    if (thread_.joinable())
        thread_.join();
}

// ================================================================ CoreRuntime

CoreRuntime::CoreRuntime(IClock &clock, std::unique_ptr<INetworkTelemetry> nic, std::string alertLogPath,
                         bool startThreads)
    : clock_(clock), nic_(std::move(nic)), transports_(clock), dispatcher_(1024, startThreads),
      history_(std::make_shared<AlertHistory>()), monitor_(*this), threads_(startThreads)
{
    dispatcher_.addSink(history_);
    if (!alertLogPath.empty())
        dispatcher_.addSink(std::make_shared<JsonlAlertLog>(alertLogPath));
    if (threads_) {
        transports_.startControlThread();
        monitor_.start();
    }
}

CoreRuntime::~CoreRuntime()
{
    shutdown();
}

void CoreRuntime::shutdown()
{
    monitor_.stop();
    transports_.stopControlThread();
    transports_.shutdownAll();
    dispatcher_.flush();
}

NicSample CoreRuntime::sampleNic(const std::string &nicId)
{
    const int64_t now = clock_.nowNs();
    std::lock_guard lk(nicMu_);
    auto it = nicCache_.find(nicId);
    if (it != nicCache_.end() && now - it->second.first < 200 * kNsPerMs)
        return it->second.second;
    NicSample s = nic_ ? nic_->sample(nicId) : NicSample{};
    nicCache_[nicId] = {now, s};
    return s;
}

} // namespace sg
