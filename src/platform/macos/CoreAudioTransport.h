#pragma once
// Adaptador macOS: captura desde un dispositivo Core Audio (objetivo: Waves SoundGrid Core
// Audio V17). No cambia frecuencia, buffer ni reloj del dispositivo.
// ESTADO: escrito para macOS (Intel/Apple Silicon); NO compilado ni probado en este entorno.
#include "../../transport/IAudioTransport.h"
#include "../../common/Clock.h"

#include <memory>

namespace sg {

std::unique_ptr<IAudioTransport> createCoreAudioTransport(IClock *clock = nullptr);
std::vector<DeviceInfo> enumerateCoreAudioInputDevices();

} // namespace sg
