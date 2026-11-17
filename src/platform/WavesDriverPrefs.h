#pragma once
// Lectura pasiva de la preferencia del SoundGrid Driver Control Panel: qué puerto de red usa el
// driver. Sirve para preseleccionar la NIC supervisada. Nunca se escribe ese archivo.
#include <optional>
#include <string>

namespace sg {

// Extrae <LocalLanPort>aa:bb:cc:dd:ee:ff</LocalLanPort> y lo devuelve en minúsculas con ':'.
// nullopt si falta, está vacío o es 00:00:00:00:00:00 ("None").
std::optional<std::string> parseWavesLocalLanPort(const std::string &xml);

// Windows: %APPDATA%\Waves Audio\Preferences\SoundGrid.Driver.preferences. Otros: nullopt
// (la ubicación en macOS no está verificada).
std::optional<std::string> wavesDriverPreferredMac();

// Normaliza una MAC a "aa:bb:cc:dd:ee:ff" (acepta '-' o ':'). "" si no es válida.
std::string normalizeMac(const std::string &mac);

} // namespace sg
