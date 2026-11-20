#pragma once
// Ajustes globales del plugin (válidos para todo OBS, no para una fuente). Se guardan en
// %APPDATA%\obs-studio\plugin_config\obs-soundgrid-input\settings.json. Seguro entre hilos.
namespace sg::settings {

constexpr const char *kKeepWavesApps = "keep_waves_apps_open";  // reabrir Control Panel y servicio
constexpr const char *kGlobalMonitor = "global_monitor";        // vigilar el driver sin fuentes
constexpr const char *kGlobalSilence = "global_monitor_silence"; // incluir silencio prolongado (A01)

bool getBool(const char *key, bool def);
void setBool(const char *key, bool value); // persiste al instante
// Libera los ajustes en memoria (al descargar el módulo; se recargan del disco si se vuelven a pedir).
void release();

} // namespace sg::settings
