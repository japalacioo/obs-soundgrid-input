// Punto de entrada del módulo OBS "obs-soundgrid-input".
#include "AlertBanner.h"
#include "ObsSoundGridSource.h"
#include "WavesWatchdog.h"

#include "platform/INetworkTelemetry.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-soundgrid-input", "en-US")
OBS_MODULE_AUTHOR(SG_PLUGIN_AUTHOR)

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Captura audio de Waves eMotion LV1 Classic V17 a través del driver SoundGrid "
           "(ASIO en Windows, Core Audio en macOS), con monitoreo y alertas.";
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return "OBS SoundGrid Input";
}

namespace {

// Reloj de libobs: los timestamps de audio deben estar en la base de os_gettime_ns().
class ObsClock final : public sg::IClock {
public:
    int64_t nowNs() const noexcept override { return static_cast<int64_t>(os_gettime_ns()); }
};
ObsClock g_clock;

void logAlert(const sg::AlertEvent &e)
{
    const int level = e.notify ? (e.severity == sg::Severity::Critical ? LOG_ERROR : LOG_WARNING) : LOG_INFO;
    blog(level, "[soundgrid] [%s] %s %s · fuente '%s' · observado: %s · umbral: %s · medida: %s%s",
         sg::severityName(e.severity), e.transition.c_str(), e.message.c_str(), e.affectedSource.c_str(),
         e.evidence.observed.c_str(), e.evidence.threshold.c_str(), e.evidence.measurementSource.c_str(),
         e.secondary ? " · consecuencia de otra causa" : "");
}

void toolsMenu(void *)
{
    sg::openSoundGridMonitor();
}

// El aviso rojo se crea cuando la ventana principal ya existe y se retira antes de cerrarla.
void onFrontendEvent(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        sg::startAlertBanner();
        sg::startWavesWatchdog();
    } else if (event == OBS_FRONTEND_EVENT_EXIT) {
        sg::stopWavesWatchdog(); // al cerrar OBS no se reabre nada
        sg::stopAlertBanner();
    }
}

} // namespace

bool obs_module_load(void)
{
    char *logPath = obs_module_config_path("alerts.jsonl");
    auto rt = std::make_shared<sg::CoreRuntime>(g_clock, sg::createPlatformNetworkTelemetry(),
                                                logPath ? logPath : "", /*startThreads=*/true);
    if (logPath)
        bfree(logPath);
    rt->dispatcher().addSink(std::make_shared<sg::CallbackSink>(logAlert));
    sg::MockConfig mock; // simulador: 32 canales para probar mapas, stems y alertas sin hardware
    mock.channels = 32;
    rt->transports().setMockConfig(mock);
    sg::setPluginRuntime(rt);
    sg::registerSoundGridSource();
    obs_frontend_add_tools_menu_item(obs_module_text("SoundGridMonitor"), toolsMenu, nullptr);
    obs_frontend_add_event_callback(onFrontendEvent, nullptr);

    std::string backends;
    for (const auto &b : rt->transports().backends())
        backends += (backends.empty() ? "" : ", ") + b;
    blog(LOG_INFO,
         "[soundgrid] OBS SoundGrid Input %s por %s cargado (%s; backends: %s). Validación con LV1 V17 real: NO PROBADO EN HARDWARE.",
         SG_PLUGIN_VERSION, SG_PLUGIN_AUTHOR, sg::platformName(sg::currentPlatform()), backends.c_str());
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
    sg::stopWavesWatchdog();
    sg::stopAlertBanner();
    auto rt = sg::pluginRuntime();
    sg::setPluginRuntime(nullptr);
    if (rt)
        rt->shutdown();
    blog(LOG_INFO, "[soundgrid] descargado");
}
