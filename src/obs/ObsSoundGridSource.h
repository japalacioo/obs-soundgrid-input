#pragma once
#include "core/SourceSession.h"

#include <obs-module.h>

#include <memory>

namespace sg {

constexpr const char *kSourceId = "soundgrid_input";

// Servicios compartidos del plugin (creados en obs_module_load).
std::shared_ptr<CoreRuntime> pluginRuntime();
void setPluginRuntime(std::shared_ptr<CoreRuntime> rt);

void registerSoundGridSource();
// Abre las propiedades (panel "SoundGrid Monitor") de la primera fuente SoundGrid.
void openSoundGridMonitor();

} // namespace sg
