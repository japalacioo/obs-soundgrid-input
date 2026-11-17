#pragma once
// Persistencia versionada de la configuración de alertas a través de un almacén clave-valor
// abstracto (en OBS: obs_data_t; en pruebas: mapa en memoria). No guarda contraseñas.
#include "../observability/AlertTypes.h"

#include <map>
#include <optional>
#include <string>

namespace sg {

class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;
    virtual std::optional<double> getNumber(const std::string &key) const = 0;
    virtual std::optional<bool> getBool(const std::string &key) const = 0;
    virtual std::optional<std::string> getString(const std::string &key) const = 0;
    virtual void setNumber(const std::string &key, double v) = 0;
    virtual void setBool(const std::string &key, bool v) = 0;
    virtual void setString(const std::string &key, const std::string &v) = 0;
};

class MapSettingsStore final : public ISettingsStore {
public:
    std::optional<double> getNumber(const std::string &k) const override
    {
        auto it = nums.find(k);
        return it == nums.end() ? std::nullopt : std::optional<double>(it->second);
    }
    std::optional<bool> getBool(const std::string &k) const override
    {
        auto it = bools.find(k);
        return it == bools.end() ? std::nullopt : std::optional<bool>(it->second);
    }
    std::optional<std::string> getString(const std::string &k) const override
    {
        auto it = strs.find(k);
        return it == strs.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    void setNumber(const std::string &k, double v) override { nums[k] = v; }
    void setBool(const std::string &k, bool v) override { bools[k] = v; }
    void setString(const std::string &k, const std::string &v) override { strs[k] = v; }

    std::map<std::string, double> nums;
    std::map<std::string, bool> bools;
    std::map<std::string, std::string> strs;
};

// Claves: "<prefix>schema", "<prefix>profile", "<prefix>A01.enabled", "<prefix>A01.threshold", ...
void saveAlertConfig(const AlertConfig &cfg, ISettingsStore &store, const std::string &prefix = "alerts.");
// Carga con valores por defecto seguros: claves ausentes, de tipo incorrecto o fuera de
// rango se sustituyen por el valor del perfil; versiones antiguas se migran.
AlertConfig loadAlertConfig(const ISettingsStore &store, const std::string &prefix = "alerts.");

const char *profileKey(AlertProfile p);
std::optional<AlertProfile> profileFromKey(const std::string &k);

} // namespace sg
