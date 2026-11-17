#pragma once
#include "core/SourceSession.h"

#include <string>

namespace sg {

// Texto multilínea para el panel de estado (propiedades de la fuente / SoundGrid Monitor).
std::string formatStatusText(const SessionStatus &st);
// Una línea para la cabecera del panel ("Audio OK · Driver: STREAMING · Audio: ...").
std::string formatStatusSummary(const SessionStatus &st);
// Exportación de diagnóstico en JSON. Sin IP/MAC ni nombres de sesión de la consola.
std::string diagnosticsJson(const std::string &sourceName, const SourceConfig &cfg, const SessionStatus &st,
                            const std::string &obsVersion, const std::string &pluginVersion);

} // namespace sg
