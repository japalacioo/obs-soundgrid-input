#pragma once
// Sesión por fuente OBS: conecta una suscripción de un DeviceHub compartido, el hilo de
// salida hacia OBS (vía callback, para poder probarlo sin libobs), el motor de alertas y el
// estado que ve el operador. No incluye cabeceras de libobs ni del sistema operativo.
#include "../common/Clock.h"
#include "../observability/AlertEngine.h"
#include "../observability/AlertSink.h"
#include "../transport/TransportManager.h"

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace sg {

class INetworkTelemetry;

struct SourceConfig {
    std::string deviceId; // "" = sin dispositivo
    ChannelMap map = ChannelMap::stereo(0, 1);
    bool enabled = true;
    bool armed = false;   // supervisión de silencio A01 (desactivada por defecto)
    std::string nicId;    // "" = sin supervisión de red
    std::string nicName;
    AlertConfig alerts = AlertConfig::defaults();
};

// Indicadores independientes (plan §3.6 / §6): nunca un único LED "Conectado".
struct ConnectivityIndicators {
    std::string nic;         // "UP 1000 Mbps" / "DOWN" / "NO DISPONIBLE" / "No supervisada"
    std::string driver;      // "RUNNING" / "DISCOVERED" / "ERROR: ..." / "Sin dispositivo"
    std::string patch;       // siempre "No verificado" salvo prueba HIL/API autorizada
    std::string audio;       // "Recibiendo (−18.2 dBFS)" / "Sin frames"
    std::string lv1;         // "No identificada (sin API autorizada)"
    std::string latency;     // tramo medido + "extremo a extremo: NO DISPONIBLE"
    std::string deviceKind;  // "Driver Waves SoundGrid (por nombre)" / "Otro dispositivo" / "Simulador"
};

struct SessionStatus {
    GlobalStatus global = GlobalStatus::Initializing;
    ConnectivityIndicators indicators;
    std::vector<AlertEvent> openIncidents;
    HealthSnapshot last;
    std::string configError; // mapa de canales inválido, etc.
    uint64_t blocksDelivered = 0;
    bool notificationsMuted = false; // "Silenciar avisos": la detección sigue activa
};

class CoreRuntime;

class SourceSession {
public:
    using OutputFn = std::function<void(const PcmBlock &)>;

    SourceSession(CoreRuntime &rt, std::string name, OutputFn out);
    ~SourceSession();

    void apply(const SourceConfig &cfg); // hilo de UI
    SourceConfig config() const;
    void rename(const std::string &name);
    void reconnect();                    // acción "Reconectar" (solo receptor; nunca la consola)
    // Cierre síncrono e idempotente: detiene la salida y retira la suscripción. Tras volver, la
    // sesión no vuelve a llamar a OutputFn aunque otros hilos conserven una referencia.
    void shutdown();

    // Monitor (hilo de monitorización): instantánea + evaluación de reglas.
    std::vector<AlertEvent> tick(int64_t nowNs);

    std::optional<AlertEvent> acknowledge(uint64_t incidentId);
    void muteNotifications(int64_t forMs);
    SessionStatus status() const;
    const std::string &name() const { return name_; }

    std::shared_ptr<DeviceHub> hub() const;
    std::shared_ptr<Subscription> subscription() const;
    std::vector<ChannelInfo> deviceChannels() const;

private:
    void startOutput();
    void stopOutput();
    void outputLoop(std::shared_ptr<Subscription> sub);
    HealthSnapshot buildSnapshot(int64_t nowNs);
    ConnectivityIndicators indicators(const HealthSnapshot &s) const;

    CoreRuntime &rt_;
    std::string name_;
    OutputFn out_;

    mutable std::mutex mu_;
    SourceConfig cfg_;
    std::shared_ptr<DeviceHub> hub_;
    std::shared_ptr<Subscription> sub_;
    std::string configError_;
    bool shut_ = false;
    bool applied_ = false;
    std::string deviceName_;
    bool deviceLooksSoundGrid_ = false;
    AlertEngine engine_;
    HealthSnapshot last_;
    uint64_t lastDelivered_ = 0;

    std::thread outThread_;
    std::shared_ptr<Subscription> outSub_; // suscripción que consume outThread_ (solo hilo de UI)
    std::atomic<bool> outStop_{false};
};

// Monitor periódico de todas las sesiones (hilo de baja prioridad, nunca el de audio).
class HealthMonitor {
public:
    HealthMonitor(CoreRuntime &rt, int64_t periodMs = 250);
    ~HealthMonitor();
    void add(const std::shared_ptr<SourceSession> &s);
    void remove(const SourceSession *s);
    void tickOnce();
    void start();
    void stop();
    int64_t periodMs() const { return periodMs_; }

private:
    CoreRuntime &rt_;
    int64_t periodMs_;
    std::mutex mu_;
    std::vector<std::weak_ptr<SourceSession>> sessions_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

// Servicios compartidos del plugin (uno por proceso OBS).
class CoreRuntime {
public:
    CoreRuntime(IClock &clock, std::unique_ptr<INetworkTelemetry> nic, std::string alertLogPath = "",
                bool startThreads = true);
    ~CoreRuntime();

    IClock &clock() { return clock_; }
    TransportManager &transports() { return transports_; }
    AlertDispatcher &dispatcher() { return dispatcher_; }
    AlertHistory &history() { return *history_; }
    HealthMonitor &monitor() { return monitor_; }
    INetworkTelemetry &nic() { return *nic_; }
    NicSample sampleNic(const std::string &nicId); // con caché de 200 ms
    void shutdown();

private:
    IClock &clock_;
    std::unique_ptr<INetworkTelemetry> nic_;
    TransportManager transports_;
    AlertDispatcher dispatcher_;
    std::shared_ptr<AlertHistory> history_;
    HealthMonitor monitor_;
    std::mutex nicMu_;
    std::map<std::string, std::pair<int64_t, NicSample>> nicCache_;
    bool threads_;
};

} // namespace sg
