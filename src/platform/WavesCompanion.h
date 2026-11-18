#pragma once
// Mantener en marcha las aplicaciones de Waves de las que depende el driver SoundGrid:
//  - SGDawNodeService: conecta el driver con la red SoundGrid (Waves lo arranca al iniciar sesión).
//  - SoundGrid Driver Control Panel: ajustes del driver (tarjeta de red, buffer).
// Solo se ARRANCAN con el mismo programa y argumentos que registra Waves; nunca se cambia su
// configuración, nunca se pide elevación (UAC) y la ventana se abre minimizada y sin foco.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace sg {

struct LaunchTarget {
    std::string label;       // nombre para el log ("SoundGrid Driver Control Panel")
    std::string processName; // nombre del ejecutable para buscarlo ("SGDawNodeService.exe")
    std::string exePath;     // ruta completa (UTF-8)
    std::string args;        // argumentos ("runasservice")
};

class IProcessOps {
public:
    virtual ~IProcessOps() = default;
    virtual bool isRunning(const std::string &processName) = 0;
    // Arranca sin elevar y minimizado, sin quitar el foco. false + error si no se pudo.
    virtual bool launch(const LaunchTarget &t, std::string &error) = 0;
};

// Windows: Toolhelp + CreateProcess. Otras plataformas: no observa ni arranca nada.
std::unique_ptr<IProcessOps> createPlatformProcessOps();
// Rutas leídas del registro de Waves (Windows). nullopt si Waves no está instalado.
std::optional<LaunchTarget> wavesControlPanelTarget();
std::optional<LaunchTarget> wavesNodeServiceTarget();
// Acción manual (botón): si ya está abierto, trae su ventana al frente; si no, lo abre en primer plano.
bool showOrLaunch(IProcessOps &ops, const LaunchTarget &t, std::string &error);

struct KeepAliveStatus {
    bool checked = false; // se ha comprobado al menos una vez
    bool running = false;
    int relaunches = 0;   // veces que el plugin lo abrió
    bool gaveUp = false;  // demasiados cierres seguidos: se dejó de reabrir
    std::string lastError;
};

// Lógica sin E/S (probada con un IProcessOps falso y tiempos virtuales).
class KeepAlive {
public:
    struct Policy {
        int64_t settleMs = 15000;  // tras abrirlo, tiempo de gracia para que aparezca el proceso
        int64_t stableMs = 120000; // si sigue abierto este tiempo, se olvidan los fallos previos
        int maxQuickFailures = 5;  // cierres/fallos seguidos antes de rendirse
    };
    explicit KeepAlive(LaunchTarget target) : KeepAlive(std::move(target), Policy{}) {}
    KeepAlive(LaunchTarget target, Policy policy) : t_(std::move(target)), p_(policy) {}

    // Comprueba y, si hace falta, lo abre. Devuelve un mensaje para el log si ocurrió algo.
    std::optional<std::string> poll(IProcessOps &ops, bool enabled, int64_t nowNs);
    KeepAliveStatus status() const { return st_; }
    const LaunchTarget &target() const { return t_; }
    void resetGiveUp();

private:
    LaunchTarget t_;
    Policy p_;
    KeepAliveStatus st_;
    bool launched_ = false;
    int64_t lastLaunchNs_ = 0;
    bool tracking_ = false; // lo vimos abierto y seguimos su cierre
    int64_t runningSinceNs_ = 0;
    bool seenSinceLaunch_ = true;
    bool launchedByUs_ = false;
    int quickFailures_ = 0;
};

} // namespace sg
