#pragma once
// Tipos del sistema de alertas A01–A10 (plan §4A, SDD-09).
#include "../common/Types.h"

#include <array>
#include <cstdint>
#include <string>

namespace sg {

enum class RuleId : uint8_t {
    A01_Silence = 0,
    A02_StreamInterrupted,
    A03_DriverLost,
    A04_NoLink,
    A05_SlowLink,
    A06_NicErrors,
    A07_Latency,
    A08_Dropouts,
    A09_Clipping,
    A10_Recovered,
    Count
};
constexpr std::size_t kRuleCount = static_cast<std::size_t>(RuleId::Count);

const char *ruleCode(RuleId r);  // "A01"...
const char *ruleTitle(RuleId r); // título en español

// Estados de un AlertEvent (SDD §2.1). RECOVERED es la etiqueta visible de A10.
enum class AlertState { Active, Acknowledged, Recovering, Resolved };
const char *alertStateName(AlertState s);

// Estado interno de evaluación de cada regla (no genera evento por sí mismo).
enum class RuleEvalState { Disabled, Initializing, Healthy, Pending, Active, Acknowledged, Recovering, Unknown };
const char *ruleEvalStateName(RuleEvalState s);

struct RuleConfig {
    bool enabled = true;
    double threshold = 0;     // significado por regla (ver AlertEngine.cpp)
    int64_t windowMs = 0;     // condición sostenida antes de activar (debounce)
    int64_t trailingMs = 0;   // ventana deslizante de conteo (A06, A08, A09)
    int64_t clearMs = 5000;   // condición sana sostenida antes de resolver (histéresis)
    int64_t cooldownMs = 30000; // reaparición dentro de este plazo reabre el mismo incidente
    Severity severity = Severity::Warn;
};

enum class AlertProfile { Ensayo, Transmision, Grabacion };
const char *alertProfileName(AlertProfile p);

struct AlertConfig {
    static constexpr int kSchemaVersion = 1;
    AlertProfile profile = AlertProfile::Transmision;
    std::array<RuleConfig, kRuleCount> rules{};
    int64_t graceMs = 3000;          // gracia tras crear/reiniciar la fuente
    Severity notifyMinSeverity = Severity::Warn;
    uint64_t expectedLinkBps = 1'000'000'000;

    static AlertConfig defaults(AlertProfile p = AlertProfile::Transmision);
    RuleConfig &rule(RuleId r) { return rules[static_cast<std::size_t>(r)]; }
    const RuleConfig &rule(RuleId r) const { return rules[static_cast<std::size_t>(r)]; }
};

struct AlertEvidence {
    std::string observed;  // valor observado legible ("-72.4 dBFS", "NO DISPONIBLE")
    std::string threshold; // umbral legible
    std::string cause;     // causa observada
    std::string action;    // pasos recomendados
    Confidence confidence = Confidence::Measured;
    std::string measurementSource; // "Windows NIC", "Driver", "PCM", ...
};

struct AlertEvent {
    uint64_t id = 0;            // identificador del incidente (estable entre transiciones)
    RuleId rule = RuleId::A01_Silence;
    Severity severity = Severity::Warn;
    AlertState state = AlertState::Active;
    std::string transition;     // "ACTIVATED", "ACKNOWLEDGED", "RECOVERING", "RESOLVED", "REOPENED", "RECOVERED"
    int64_t startedAtNs = 0;
    int64_t endedAtNs = 0;      // 0 mientras siga abierto
    std::string wallTime;       // hora civil del evento (presentación)
    std::string message;        // mensaje para el operador
    AlertEvidence evidence;
    uint64_t correlationId = 0; // id del incidente principal si es consecuencia
    bool secondary = false;     // consecuencia de otra causa raíz
    bool notify = true;         // false: solo log (silenciado, secundario o bajo perfil)
    Platform platform = currentPlatform();
    std::string affectedSource;
};

} // namespace sg
