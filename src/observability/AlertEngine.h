#pragma once
// Motor de reglas A01–A10 con debounce, histéresis, cooldown, correlación de causa raíz,
// reconocimiento y silencio de notificaciones (plan §4A.2). Determinista: el tiempo viene
// de HealthSnapshot::nowNs, de modo que las pruebas usan un reloj virtual.
#include "AlertTypes.h"
#include "HealthSnapshot.h"

#include <deque>
#include <optional>
#include <vector>

namespace sg {

enum class GlobalStatus {
    AudioOk,
    SilenceDetected,
    AudioInterrupted,
    DriverUnavailable,
    NoLink,
    DegradedLink,
    NetworkErrors,
    HighLatency,
    Dropouts,
    Clipping,
    DataUnavailable,
    Initializing,
};
const char *globalStatusText(GlobalStatus s);

struct RuleStatus {
    RuleEvalState state = RuleEvalState::Initializing;
    std::string observed; // último valor observado (o "NO DISPONIBLE")
    uint64_t incidentId = 0;
    bool secondary = false;
};

class AlertEngine {
public:
    explicit AlertEngine(AlertConfig cfg = AlertConfig::defaults());

    void setConfig(const AlertConfig &cfg); // conserva incidentes abiertos
    const AlertConfig &config() const { return cfg_; }

    // Inicia el periodo de gracia (creación/reinicio de la fuente).
    void restart(int64_t nowNs);

    // Evalúa una instantánea y devuelve los eventos generados (posiblemente ninguno).
    std::vector<AlertEvent> evaluate(const HealthSnapshot &s);

    // Reconocer un incidente: NO desactiva la detección.
    std::optional<AlertEvent> acknowledge(uint64_t incidentId, int64_t nowNs);
    // Silenciar notificaciones (los eventos se siguen registrando con notify=false).
    void muteNotificationsUntil(int64_t nowNs) { mutedUntilNs_ = nowNs; }
    bool notificationsMuted(int64_t nowNs) const { return nowNs < mutedUntilNs_; }

    RuleStatus ruleStatus(RuleId r) const;
    std::vector<AlertEvent> openIncidents() const; // activos, reconocidos o recuperándose
    GlobalStatus globalStatus() const { return global_; }

private:
    enum class Cond { Good, Bad, Unknown, NotApplicable };
    struct Eval {
        Cond cond = Cond::Unknown;
        AlertEvidence ev;
    };
    struct Runtime {
        RuleEvalState state = RuleEvalState::Initializing;
        int64_t badSinceNs = -1;
        int64_t goodSinceNs = -1;
        int64_t lastResolvedNs = -1;
        bool wasAcknowledged = false;
        AlertEvent incident; // id != 0 mientras haya incidente abierto
        uint64_t lastResolvedId = 0;
        bool lastNotified = false;
        std::string observed;
    };

    Eval evalRule(RuleId r, const HealthSnapshot &s);
    void step(RuleId r, const Eval &e, const HealthSnapshot &s, std::vector<AlertEvent> &out);
    AlertEvent makeEvent(RuleId r, const Eval &e, const HealthSnapshot &s, const char *transition);
    bool shouldNotify(const AlertEvent &e, int64_t nowNs) const;
    void updateGlobal(const HealthSnapshot &s);
    bool isOpen(RuleId r) const;
    Runtime &rt(RuleId r) { return rt_[static_cast<std::size_t>(r)]; }
    const Runtime &rt(RuleId r) const { return rt_[static_cast<std::size_t>(r)]; }

    AlertConfig cfg_;
    std::array<Runtime, kRuleCount> rt_{};
    int64_t graceUntilNs_ = 0;
    bool started_ = false;
    int64_t mutedUntilNs_ = 0;
    uint64_t nextIncidentId_ = 1;
    GlobalStatus global_ = GlobalStatus::Initializing;

    // Estado derivado entre ticks
    uint64_t lastFrames_ = 0;
    int64_t lastProgressNs_ = 0;
    bool framesAdvancedThisTick_ = false;
    std::deque<std::pair<int64_t, uint64_t>> nicErrHist_;
    std::deque<std::pair<int64_t, uint64_t>> dropoutHist_;
    std::deque<int64_t> clipTicks_;
};

} // namespace sg
