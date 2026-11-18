#pragma once
#include "core/SourceSession.h"
#include "observability/AudioLossBanner.h"

#include <obs-module.h>

#include <memory>
#include <string>
#include <vector>

namespace sg {

constexpr const char *kSourceId = "soundgrid_input";

// Servicios compartidos del plugin (creados en obs_module_load).
std::shared_ptr<CoreRuntime> pluginRuntime();
void setPluginRuntime(std::shared_ptr<CoreRuntime> rt);

void registerSoundGridSource();
// Abre las propiedades (panel "SoundGrid Monitor") de la primera fuente SoundGrid.
void openSoundGridMonitor();
// Estado de alertas de todas las fuentes vivas, para el aviso de la ventana principal.
std::vector<BannerSourceInput> collectBannerInputs();
// ¿Alguna fuente usa el driver Waves con "Mantener abiertas las aplicaciones de Waves"?
bool wavesKeepAliveWanted();
// Abre las propiedades de la fuente con ese nombre (o la primera SoundGrid si no existe).
void openSourcePropertiesByName(const std::string &name);

} // namespace sg
