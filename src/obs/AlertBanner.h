#pragma once
// Aviso visible en la ventana principal de OBS cuando una fuente SoundGrid deja de recibir audio:
// barra roja bajo el menú (ámbar mientras se verifica la recuperación, verde al recuperarse).
// Se crea y destruye en el hilo de interfaz de OBS. En plataformas sin implementación no hace nada
// (las alertas siguen en el panel y en el log).
namespace sg {

void startAlertBanner(); // OBS_FRONTEND_EVENT_FINISHED_LOADING
void stopAlertBanner();  // OBS_FRONTEND_EVENT_EXIT y obs_module_unload (idempotente)
bool alertBannerAvailable();

} // namespace sg
