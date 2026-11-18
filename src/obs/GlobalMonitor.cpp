#include "GlobalMonitor.h"

#include "Diagnostics.h"
#include "ObsSoundGridSource.h"
#include "PluginSettings.h"
#include "platform/INetworkTelemetry.h"
#include "platform/WavesDriverPrefs.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <memory>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace sg {

namespace {

std::mutex g_mu;
std::shared_ptr<SourceSession> g_session;
SourceConfig g_applied;
bool g_started = false; // entre FINISHED_LOADING y EXIT
int64_t g_lastAttemptNs = 0;
std::string g_lastProblem;
bool g_paused = false; // una fuente visible ya vigila el driver

// Primer driver Waves SoundGrid (no simulador) y la NIC elegida en el Control Panel de Waves.
std::optional<SourceConfig> detectConfig(CoreRuntime &rt)
{
    SourceConfig c;
    for (const auto &d : rt.transports().enumerateAll()) {
        if (d.looksLikeWavesSoundGrid && d.backend != "mock") {
            c.deviceId = d.id;
            break;
        }
    }
    if (c.deviceId.empty())
        return std::nullopt;
    if (const auto mac = wavesDriverPreferredMac()) {
        for (const auto &n : rt.nic().listInterfaces()) {
            if (!n.mac.empty() && n.mac == *mac) {
                c.nicId = n.id;
                c.nicName = n.displayName;
                break;
            }
        }
    }
    c.map = ChannelMap::stereo(0, 1);
    c.armed = globalSilenceEnabled();
    c.alerts = AlertConfig::defaults(AlertProfile::Transmision);
    return c;
}

bool sameConfig(const SourceConfig &a, const SourceConfig &b)
{
    return a.deviceId == b.deviceId && a.nicId == b.nicId && a.armed == b.armed;
}

void stopLocked(std::shared_ptr<CoreRuntime> rt)
{
    if (!g_session)
        return;
    if (rt)
        rt->monitor().remove(g_session.get());
    g_session->shutdown();
    g_session.reset();
    blog(LOG_INFO, "[soundgrid] vigilancia global del driver Waves detenida");
}

} // namespace

bool globalMonitorEnabled()
{
    return settings::getBool(settings::kGlobalMonitor, true);
}

void setGlobalMonitorEnabled(bool on)
{
    settings::setBool(settings::kGlobalMonitor, on);
    maintainGlobalMonitor();
}

bool globalSilenceEnabled()
{
    return settings::getBool(settings::kGlobalSilence, true);
}

void setGlobalSilenceEnabled(bool on)
{
    settings::setBool(settings::kGlobalSilence, on);
    maintainGlobalMonitor();
}

void startGlobalMonitor()
{
    {
        std::lock_guard lk(g_mu);
        g_started = true;
        g_lastAttemptNs = 0;
    }
    maintainGlobalMonitor();
}

void stopGlobalMonitor()
{
    std::lock_guard lk(g_mu);
    g_started = false;
    stopLocked(pluginRuntime());
}

void maintainGlobalMonitor()
{
    auto rt = pluginRuntime();
    std::lock_guard lk(g_mu);
    if (!g_started || !rt)
        return;
    if (!globalMonitorEnabled()) {
        stopLocked(rt);
        g_lastProblem.clear();
        g_paused = false;
        return;
    }
    // Una fuente visible ya vigila el driver Waves: pausa (sin alertas dobles). Se reanuda sola.
    if (visibleWavesSourceExists()) {
        if (!g_paused) {
            g_paused = true;
            stopLocked(rt);
            blog(LOG_INFO, "[soundgrid] vigilancia global en pausa: una fuente SoundGrid ya vigila el driver Waves");
        }
        return;
    }
    if (g_paused) {
        g_paused = false;
        g_lastAttemptNs = 0;
        blog(LOG_INFO, "[soundgrid] ninguna fuente SoundGrid vigila el driver Waves: se reanuda la vigilancia global");
    }
    const int64_t now = static_cast<int64_t>(os_gettime_ns());
    // Sin driver Waves: reintentar cada 10 s (p. ej. si Waves se instala con OBS abierto).
    if (!g_session && g_lastAttemptNs && now - g_lastAttemptNs < 10'000'000'000LL)
        return;
    g_lastAttemptNs = now;
    const auto cfg = detectConfig(*rt);
    if (!cfg) {
        stopLocked(rt);
        if (g_lastProblem.empty())
            blog(LOG_INFO, "[soundgrid] vigilancia global: no hay driver Waves SoundGrid instalado");
        g_lastProblem = "no se encontró el driver Waves SoundGrid";
        return;
    }
    g_lastProblem.clear();
    if (g_session && sameConfig(*cfg, g_applied))
        return;
    if (!g_session) {
        g_session = std::make_shared<SourceSession>(*rt, kGlobalMonitorName, [](const PcmBlock &) {
            // Solo se mide: este audio no se entrega a OBS.
        });
        rt->monitor().add(g_session);
        blog(LOG_INFO, "[soundgrid] vigilancia global activa: driver '%s', entradas 1–2%s%s", cfg->deviceId.c_str(),
             cfg->nicName.empty() ? "" : (", red '" + cfg->nicName + "'").c_str(),
             cfg->armed ? ", con aviso de silencio prolongado" : "");
    }
    g_session->apply(*cfg);
    g_applied = *cfg;
}

std::optional<BannerSourceInput> globalMonitorBannerInput()
{
    std::lock_guard lk(g_mu);
    if (!g_session)
        return std::nullopt;
    const SessionStatus st = g_session->status();
    BannerSourceInput in;
    in.sourceName = kGlobalMonitorName;
    in.notificationsMuted = st.notificationsMuted;
    in.openIncidents = st.openIncidents;
    return in;
}

std::string globalMonitorStatusText()
{
    std::lock_guard lk(g_mu);
    std::string s = std::string("Vigilancia global del driver Waves: ") +
                    (globalMonitorEnabled() ? "ACTIVADA" : "desactivada") +
                    (globalSilenceEnabled() ? " (incluye silencio prolongado)" : " (sin aviso de silencio)") + "\n";
    if (g_paused)
        s += "En pausa: una fuente SoundGrid visible ya vigila el driver Waves.\n";
    if (!g_lastProblem.empty())
        s += "Problema: " + g_lastProblem + "\n";
    if (g_session)
        s += "Driver: " + g_applied.deviceId + " · entradas 1–2\n\n" + formatStatusText(g_session->status());
    return s;
}

void showGlobalMonitorDialog()
{
    const bool on = globalMonitorEnabled();
    const std::string text = globalMonitorStatusText() +
                             "\n\nNo hay fuentes \"SoundGrid Input (Waves)\" en esta colección de escenas: la "
                             "vigilancia global escucha el driver Waves para avisar si se pierde el audio.\n\n" +
                             (on ? "¿Quieres DESACTIVAR la vigilancia global?" : "¿Quieres ACTIVAR la vigilancia global?");
#if defined(_WIN32)
    auto widen = [](const std::string &u) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
        return w;
    };
    HWND owner = static_cast<HWND>(obs_frontend_get_main_window_handle());
    const int r = MessageBoxW(owner, widen(text).c_str(), L"SoundGrid Monitor",
                              MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2);
    if (r == IDYES)
        setGlobalMonitorEnabled(!on);
#else
    blog(LOG_INFO, "[soundgrid] %s", text.c_str());
#endif
}

} // namespace sg
