#pragma once
// Vigila que el servicio de Waves (SGDawNodeService) y el SoundGrid Driver Control Panel estén
// abiertos mientras alguna fuente use el driver Waves con la opción activada, y los reabre si se
// cierran (minimizados y sin quitar el foco). Hilo propio de baja frecuencia (cada 2 s).
#include <string>

namespace sg {

void startWavesWatchdog();
void stopWavesWatchdog(); // idempotente; antes de cerrar OBS para no abrir nada durante el cierre
// Línea de estado para el panel ("" si Waves no está instalado o la plataforma no lo admite).
std::string wavesWatchdogStatusText();
// Botón del panel: abre el Control Panel (o lo trae al frente) y anula un "se dejó de reabrir".
std::string openWavesControlPanelNow();

} // namespace sg
