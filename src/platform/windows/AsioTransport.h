#pragma once
// Adaptador Windows: captura desde un driver ASIO (objetivo: Waves SoundGrid ASIO V17).
// - Nunca cambia la frecuencia de muestreo, el reloj ni el patch: usa la configuración que
//   el driver/sesión ya tienen (ADR-006).
// - Todas las llamadas al driver se hacen en un hilo COM STA dedicado con bomba de
//   mensajes; el callback bufferSwitch solo convierte y copia (tiempo real).
// - Hasta 128 canales de entrada (límite documentado del driver SoundGrid V17).
#include "../../transport/IAudioTransport.h"
#include "../../common/Clock.h"

#include <memory>

namespace sg {

std::unique_ptr<IAudioTransport> createAsioTransport(IClock *clock = nullptr);
// Enumeración sin cargar drivers (solo registro HKLM\SOFTWARE\ASIO).
std::vector<DeviceInfo> enumerateAsioDrivers();

} // namespace sg
