#pragma once
// Vigilancia global del audio del driver Waves SoundGrid, sin necesidad de fuentes en OBS.
// Es una sesión interna del plugin (no una fuente de OBS: no entrega audio a OBS) sobre el primer
// driver Waves SoundGrid, entradas 1–2, y la tarjeta de red de su Control Panel. Comparte el stream
// del driver con las fuentes SoundGrid (no lo abre dos veces). Sus alertas alimentan la barra roja.
// Todas las funciones se llaman desde el hilo de interfaz de OBS salvo las de estado (seguras).
#include "observability/AudioLossBanner.h"

#include <optional>
#include <string>

namespace sg {

constexpr const char *kGlobalMonitorName = "Driver Waves (vigilancia global)";

void startGlobalMonitor(); // OBS_FRONTEND_EVENT_FINISHED_LOADING
void stopGlobalMonitor();  // OBS_FRONTEND_EVENT_EXIT / descarga (idempotente)
// Reintenta abrir si falló o aplica cambios de ajustes. Barato: llamar cada pocos segundos.
void maintainGlobalMonitor();

bool globalMonitorEnabled();
void setGlobalMonitorEnabled(bool on);
bool globalSilenceEnabled();
void setGlobalSilenceEnabled(bool on);

// Entrada para el aviso de audio perdido (nullopt si la vigilancia global no está en marcha).
std::optional<BannerSourceInput> globalMonitorBannerInput();
std::string globalMonitorStatusText();
// Herramientas > SoundGrid Monitor sin fuentes, o clic en el aviso de la vigilancia global.
void showGlobalMonitorDialog();

} // namespace sg
