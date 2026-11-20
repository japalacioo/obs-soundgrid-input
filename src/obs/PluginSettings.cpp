#include "PluginSettings.h"

#include <obs-module.h>
#include <util/platform.h>

#include <mutex>

namespace sg::settings {

namespace {

std::mutex g_mu;
obs_data_t *g_data = nullptr;

void ensureLoaded()
{
    if (g_data)
        return;
    if (char *path = obs_module_config_path("settings.json")) {
        g_data = obs_data_create_from_json_file_safe(path, "bak");
        bfree(path);
    }
    if (!g_data)
        g_data = obs_data_create();
}

void save()
{
    if (char *dir = obs_module_config_path("")) {
        os_mkdirs(dir);
        bfree(dir);
    }
    if (char *path = obs_module_config_path("settings.json")) {
        obs_data_save_json_safe(g_data, path, "tmp", "bak");
        bfree(path);
    }
}

} // namespace

void release()
{
    std::lock_guard lk(g_mu);
    obs_data_release(g_data);
    g_data = nullptr;
}

bool getBool(const char *key, bool def)
{
    std::lock_guard lk(g_mu);
    ensureLoaded();
    obs_data_set_default_bool(g_data, key, def);
    return obs_data_get_bool(g_data, key);
}

void setBool(const char *key, bool value)
{
    std::lock_guard lk(g_mu);
    ensureLoaded();
    if (obs_data_has_user_value(g_data, key) && obs_data_get_bool(g_data, key) == value)
        return;
    obs_data_set_bool(g_data, key, value);
    save();
}

} // namespace sg::settings
