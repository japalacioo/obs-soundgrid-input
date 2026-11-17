#include "Settings.h"

#include <cmath>

namespace sg {

const char *profileKey(AlertProfile p)
{
    switch (p) {
    case AlertProfile::Ensayo: return "ensayo";
    case AlertProfile::Transmision: return "transmision";
    case AlertProfile::Grabacion: return "grabacion";
    }
    return "transmision";
}

std::optional<AlertProfile> profileFromKey(const std::string &k)
{
    if (k == "ensayo")
        return AlertProfile::Ensayo;
    if (k == "transmision")
        return AlertProfile::Transmision;
    if (k == "grabacion")
        return AlertProfile::Grabacion;
    return std::nullopt;
}

void saveAlertConfig(const AlertConfig &cfg, ISettingsStore &s, const std::string &p)
{
    s.setNumber(p + "schema", AlertConfig::kSchemaVersion);
    s.setString(p + "profile", profileKey(cfg.profile));
    s.setNumber(p + "grace_ms", static_cast<double>(cfg.graceMs));
    s.setNumber(p + "notify_min_severity", static_cast<double>(cfg.notifyMinSeverity));
    for (std::size_t i = 0; i < kRuleCount; ++i) {
        const RuleConfig &r = cfg.rules[i];
        const std::string k = p + ruleCode(static_cast<RuleId>(i)) + ".";
        s.setBool(k + "enabled", r.enabled);
        s.setNumber(k + "threshold", r.threshold);
        s.setNumber(k + "window_ms", static_cast<double>(r.windowMs));
        s.setNumber(k + "trailing_ms", static_cast<double>(r.trailingMs));
        s.setNumber(k + "clear_ms", static_cast<double>(r.clearMs));
        s.setNumber(k + "cooldown_ms", static_cast<double>(r.cooldownMs));
        s.setNumber(k + "severity", static_cast<double>(r.severity));
    }
}

AlertConfig loadAlertConfig(const ISettingsStore &s, const std::string &p)
{
    AlertProfile profile = AlertProfile::Transmision;
    if (auto v = s.getString(p + "profile"))
        if (auto pr = profileFromKey(*v))
            profile = *pr;
    AlertConfig cfg = AlertConfig::defaults(profile);

    const double schema = s.getNumber(p + "schema").value_or(0);
    if (schema < 1) {
        // Sin versión (instalación nueva o datos previos a v1): valores del perfil.
        return cfg;
    }
    // Versiones futuras desconocidas: se leen las claves conocidas y se ignora el resto.

    auto num = [&](const std::string &k, double lo, double hi, double def) {
        auto v = s.getNumber(k);
        if (!v || !std::isfinite(*v) || *v < lo || *v > hi)
            return def;
        return *v;
    };
    cfg.graceMs = static_cast<int64_t>(num(p + "grace_ms", 0, 60000, static_cast<double>(cfg.graceMs)));
    cfg.notifyMinSeverity = static_cast<Severity>(
        static_cast<int>(num(p + "notify_min_severity", 0, 2, static_cast<double>(cfg.notifyMinSeverity))));
    for (std::size_t i = 0; i < kRuleCount; ++i) {
        RuleConfig &r = cfg.rules[i];
        const std::string k = p + ruleCode(static_cast<RuleId>(i)) + ".";
        if (auto b = s.getBool(k + "enabled"))
            r.enabled = *b;
        r.threshold = num(k + "threshold", -1e12, 1e12, r.threshold);
        r.windowMs = static_cast<int64_t>(num(k + "window_ms", 0, 600000, static_cast<double>(r.windowMs)));
        r.trailingMs = static_cast<int64_t>(num(k + "trailing_ms", 0, 3600000, static_cast<double>(r.trailingMs)));
        r.clearMs = static_cast<int64_t>(num(k + "clear_ms", 0, 600000, static_cast<double>(r.clearMs)));
        r.cooldownMs = static_cast<int64_t>(num(k + "cooldown_ms", 0, 3600000, static_cast<double>(r.cooldownMs)));
        r.severity = static_cast<Severity>(static_cast<int>(num(k + "severity", 0, 2, static_cast<double>(r.severity))));
    }
    return cfg;
}

} // namespace sg
