#pragma once
// Monitor de audio para el Multiview de OBS: fuente de video "soundgrid_audio_monitor" con dos vistas
// del mismo estilo: la clásica (medidores L/R, osciloscopio, Peak/RMS, aviso de clip y nombre de la
// fuente SoundGrid de la escena al aire) y la de sonoridad (ITU-R BS.1770-4, ADR-017). OBS no permite a un plugin cambiar un recuadro del Multiview, así que el
// monitor vive en una escena propia, marcada por el plugin, que se crea sola al abrir OBS (ajuste
// global, activo por defecto) y nunca puede salir al aire (ADR-016).
#include <obs-module.h>
#include <obs-frontend-api.h>

namespace sg {

constexpr const char *kAudioMonitorSourceId = "soundgrid_audio_monitor";

void registerAudioMonitorSource();
// Herramientas > "Monitor de audio en el Multiview": crea la escena si no existe y explica dónde
// colocarla según el diseño de Multiview actual.
void setupAudioMonitorScene();
// Eventos del frontend: sigue la escena de programa/vista previa y evita que la escena del monitor
// salga al aire si alguien la pulsa en el Multiview.
void audioMonitorFrontendEvent(enum obs_frontend_event event);
void stopAudioMonitor(); // al cerrar OBS / descargar el módulo
// Herramientas: cambia todos los monitores entre la vista clásica y la de sonoridad.
void toggleAudioMonitorView();
// Herramientas: reinicia integrated, LRA, máximos e historial de todos los monitores.
void resetAudioMonitorLoudness();

} // namespace sg
