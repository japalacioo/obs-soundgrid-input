#pragma once
#include "core/SourceSession.h"
#include "observability/AudioLossBanner.h"

#include <obs-module.h>

#include <memory>
#include <string>
#include <vector>

namespace sg {

namespace monitor {
class AudioScopeTap;
}

constexpr const char *kSourceId = "soundgrid_input";

// Servicios compartidos del plugin (creados en obs_module_load).
std::shared_ptr<CoreRuntime> pluginRuntime();
void setPluginRuntime(std::shared_ptr<CoreRuntime> rt);

void registerSoundGridSource();
// Abre las propiedades (panel "SoundGrid Monitor") de la primera fuente SoundGrid.
void openSoundGridMonitor();
// Estado de alertas de todas las fuentes vivas, para el aviso de la ventana principal.
std::vector<BannerSourceInput> collectBannerInputs();
// ¿Hay una fuente visible con el driver Waves y el aviso activado? (entonces la vigilancia global
// se pausa para no duplicar alertas).
bool visibleWavesSourceExists();
// Abre las propiedades de la fuente con ese nombre (o la primera SoundGrid si no existe).
void openSourcePropertiesByName(const std::string &name);
// Historial de audio de una fuente SoundGrid viva (nullptr si no lo es). Para el monitor del Multiview.
std::shared_ptr<monitor::AudioScopeTap> soundGridScopeTap(obs_source_t *source);

} // namespace sg
