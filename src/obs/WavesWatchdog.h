#pragma once
// Vigila que el servicio de Waves (SGDawNodeService) y el SoundGrid Driver Control Panel estén
// abiertos y los reabre si se cierran (minimizados y sin quitar el foco). Es un ajuste GLOBAL del
// plugin (no depende de que haya fuentes SoundGrid): se guarda en la carpeta de configuración del
// plugin y está activado por defecto. Hilo propio de baja frecuencia (cada 2 s).
#include <string>

namespace sg {

void startWavesWatchdog();
void stopWavesWatchdog(); // idempotente; antes de cerrar OBS para no abrir nada durante el cierre

bool wavesKeepAliveEnabled();
void setWavesKeepAliveEnabled(bool on); // persiste el ajuste

// Línea de estado para el panel ("" si Waves no está instalado o la plataforma no lo admite).
std::string wavesWatchdogStatusText();
// Botón / menú: abre el Control Panel (o lo trae al frente) y anula un "se dejó de reabrir".
std::string openWavesControlPanelNow();

} // namespace sg
