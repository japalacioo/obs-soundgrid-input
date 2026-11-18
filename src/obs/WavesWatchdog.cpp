#include "WavesWatchdog.h"

#include "ObsSoundGridSource.h"
#include "platform/WavesCompanion.h"

#include <obs-module.h>
#include <util/platform.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace sg {

namespace {

constexpr auto kPeriod = std::chrono::seconds(2);
constexpr auto kStartDelay = std::chrono::seconds(3); // deja cargar las escenas antes de decidir

std::mutex g_mu; // protege todo lo de abajo salvo el hilo
std::unique_ptr<IProcessOps> g_ops;
std::optional<KeepAlive> g_service;
std::optional<KeepAlive> g_panel;
bool g_lastWanted = false;

std::thread g_thread;
std::mutex g_waitMu;
std::condition_variable g_cv;
bool g_stop = false;

void pollOnce()
{
    const bool wanted = wavesKeepAliveWanted();
    std::lock_guard lk(g_mu);
    if (!g_ops)
        return;
    const int64_t now = static_cast<int64_t>(os_gettime_ns());
    if (wanted != g_lastWanted) {
        g_lastWanted = wanted;
        if (wanted)
            blog(LOG_INFO, "[soundgrid] vigilancia de las aplicaciones de Waves activa (se reabren si se cierran)");
    }
    // Primero el servicio (lleva el audio), después el panel de ajustes.
    for (auto *k : {&g_service, &g_panel}) {
        if (!*k)
            continue;
        if (const auto msg = (*k)->poll(*g_ops, wanted, now)) {
            const bool problem = msg->find("no se pudo") != std::string::npos ||
                                 msg->find("se deja de reabrir") != std::string::npos;
            blog(problem ? LOG_WARNING : LOG_INFO, "[soundgrid] %s", msg->c_str());
        }
    }
}

void run()
{
    std::unique_lock lk(g_waitMu);
    if (g_cv.wait_for(lk, kStartDelay, [] { return g_stop; }))
        return;
    while (!g_stop) {
        lk.unlock();
        pollOnce();
        lk.lock();
        g_cv.wait_for(lk, kPeriod, [] { return g_stop; });
    }
}

} // namespace

void startWavesWatchdog()
{
    {
        std::lock_guard lk(g_mu);
        if (g_ops)
            return;
        g_ops = createPlatformProcessOps();
        if (auto t = wavesNodeServiceTarget())
            g_service.emplace(*t);
        if (auto t = wavesControlPanelTarget())
            g_panel.emplace(*t);
        if (!g_service && !g_panel) {
            g_ops.reset(); // Waves no instalado o plataforma sin soporte: nada que vigilar
            return;
        }
    }
    {
        std::lock_guard lk(g_waitMu);
        g_stop = false;
    }
    g_thread = std::thread(run);
}

void stopWavesWatchdog()
{
    {
        std::lock_guard lk(g_waitMu);
        g_stop = true;
    }
    g_cv.notify_all();
    if (g_thread.joinable())
        g_thread.join();
    std::lock_guard lk(g_mu);
    g_service.reset();
    g_panel.reset();
    g_ops.reset();
}

std::string wavesWatchdogStatusText()
{
    std::lock_guard lk(g_mu);
    if (!g_ops)
        return {};
    auto describe = [](const std::optional<KeepAlive> &k, const char *what, const char *open) -> std::string {
        if (!k)
            return std::string(what) + ": no instalado";
        const KeepAliveStatus s = k->status();
        std::string r = std::string(what) + ": " + (!s.checked ? "sin comprobar" : s.running ? open : "CERRADO");
        if (s.relaunches)
            r += " (reabierto " + std::to_string(s.relaunches) + (s.relaunches == 1 ? " vez)" : " veces)");
        if (s.gaveUp)
            r += " · se dejó de reabrir";
        if (!s.lastError.empty())
            r += " · " + s.lastError;
        return r;
    };
    return "Aplicaciones Waves: " + describe(g_service, "servicio", "en marcha") + " · " +
           describe(g_panel, "Control Panel", "abierto") + (g_lastWanted ? "" : " · vigilancia desactivada");
}

std::string openWavesControlPanelNow()
{
    std::lock_guard lk(g_mu);
    std::unique_ptr<IProcessOps> tmp;
    IProcessOps *ops = g_ops.get();
    if (!ops) {
        tmp = createPlatformProcessOps();
        ops = tmp.get();
    }
    std::optional<LaunchTarget> t = g_panel ? std::optional<LaunchTarget>(g_panel->target()) : wavesControlPanelTarget();
    if (!t)
        return "No se encontró el SoundGrid Driver Control Panel (¿está instalado el driver de Waves?)";
    if (g_panel)
        g_panel->resetGiveUp();
    if (g_service)
        g_service->resetGiveUp();
    std::string err;
    if (!showOrLaunch(*ops, *t, err))
        return "No se pudo abrir el SoundGrid Driver Control Panel: " + err;
    return "SoundGrid Driver Control Panel abierto";
}

} // namespace sg
