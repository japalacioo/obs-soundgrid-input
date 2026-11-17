#pragma once
// Adaptador de ISettingsStore sobre obs_data_t (persistencia nativa de OBS por fuente).
#include "common/Settings.h"

#include <obs-module.h>

namespace sg {

class ObsSettingsStore final : public ISettingsStore {
public:
    // defaults=true escribe valores por defecto (get_defaults) en lugar de valores.
    ObsSettingsStore(obs_data_t *d, bool defaults = false) : d_(d), defaults_(defaults) {}

    std::optional<double> getNumber(const std::string &k) const override
    {
        if (!obs_data_has_user_value(d_, k.c_str()) && !obs_data_has_default_value(d_, k.c_str()))
            return std::nullopt;
        return obs_data_get_double(d_, k.c_str());
    }
    std::optional<bool> getBool(const std::string &k) const override
    {
        if (!obs_data_has_user_value(d_, k.c_str()) && !obs_data_has_default_value(d_, k.c_str()))
            return std::nullopt;
        return obs_data_get_bool(d_, k.c_str());
    }
    std::optional<std::string> getString(const std::string &k) const override
    {
        if (!obs_data_has_user_value(d_, k.c_str()) && !obs_data_has_default_value(d_, k.c_str()))
            return std::nullopt;
        const char *s = obs_data_get_string(d_, k.c_str());
        return std::string(s ? s : "");
    }
    void setNumber(const std::string &k, double v) override
    {
        defaults_ ? obs_data_set_default_double(d_, k.c_str(), v) : obs_data_set_double(d_, k.c_str(), v);
    }
    void setBool(const std::string &k, bool v) override
    {
        defaults_ ? obs_data_set_default_bool(d_, k.c_str(), v) : obs_data_set_bool(d_, k.c_str(), v);
    }
    void setString(const std::string &k, const std::string &v) override
    {
        defaults_ ? obs_data_set_default_string(d_, k.c_str(), v.c_str())
                  : obs_data_set_string(d_, k.c_str(), v.c_str());
    }

private:
    obs_data_t *d_;
    bool defaults_;
};

} // namespace sg
