// Host de integración: carga el libobs REAL instalado (obs.dll de OBS 33) sin interfaz
// gráfica, abre el módulo compilado y ejercita la fuente "soundgrid_input" con el
// dispositivo simulado. No toca la configuración ni las escenas del usuario (usa una
// carpeta temporal). Cubre T01, T02 (vía OBS), T03, T09, T10 y T40 (persistencia OBS).
#include "../unit/TestHelpers.h"

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

int g_fail = 0;
#define CHECK(cond, id, msg)                                                       \
    do {                                                                           \
        const bool ok_ = (cond);                                                   \
        std::printf("[%s] %s: %s\n", ok_ ? "PASS" : "FAIL", id, msg);              \
        if (!ok_)                                                                  \
            ++g_fail;                                                              \
    } while (0)

struct Capture {
    std::mutex mu;
    std::vector<float> l, r;
    std::atomic<uint64_t> calls{0};
    uint64_t lastTs = 0;
    bool monotonic = true;
};

void captureCb(void *param, obs_source_t *, const audio_data *audio, bool muted)
{
    auto *c = static_cast<Capture *>(param);
    if (muted || !audio || !audio->data[0])
        return;
    std::lock_guard lk(c->mu);
    const auto *l = reinterpret_cast<const float *>(audio->data[0]);
    c->l.insert(c->l.end(), l, l + audio->frames);
    if (audio->data[1]) {
        const auto *r = reinterpret_cast<const float *>(audio->data[1]);
        c->r.insert(c->r.end(), r, r + audio->frames);
    }
    if (c->lastTs && audio->timestamp <= c->lastTs)
        c->monotonic = false;
    c->lastTs = audio->timestamp;
    c->calls++;
}

obs_data_t *mockSettings(int left, int right)
{
    obs_data_t *s = obs_data_create();
    obs_data_set_string(s, "device", "mock:default");
    obs_data_set_int(s, "mode", 1);
    obs_data_set_int(s, "ch_1", left);
    obs_data_set_int(s, "ch_2", right);
    return s;
}

bool waitSamples(Capture &c, size_t n, int ms)
{
    for (int i = 0; i < ms / 10; ++i) {
        {
            std::lock_guard lk(c.mu);
            if (c.l.size() >= n)
                return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const fs::path cfg = fs::temp_directory_path() / "sg_obs_host_test";
    fs::remove_all(cfg);
    fs::create_directories(cfg);

    if (!obs_startup("es-ES", cfg.string().c_str(), nullptr)) {
        std::printf("[FAIL] obs_startup\n");
        return 2;
    }
    std::printf("libobs %s\n", obs_get_version_string());
    obs_audio_info oai{};
    oai.samples_per_sec = 48000;
    oai.speakers = SPEAKERS_STEREO;
    CHECK(obs_reset_audio(&oai), "ENV", "obs_reset_audio 48 kHz estéreo");

#if defined(_WIN32)
    // libobs carga módulos con LOAD_LIBRARY_SEARCH_*: no busca en PATH. En OBS real la carpeta
    // de la aplicación ya contiene obs-frontend-api.dll; aquí se añade la carpeta de obs.dll.
    if (HMODULE h = GetModuleHandleW(L"obs.dll")) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(h, path, MAX_PATH);
        std::wstring dir(path);
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
        AddDllDirectory(dir.c_str());
    }
#endif
    obs_module_t *mod = nullptr;
    const int r = obs_open_module(&mod, SG_PLUGIN_PATH, SG_PLUGIN_DATA);
    CHECK(r == MODULE_SUCCESS, "T01", "obs_open_module carga obs-soundgrid-input");
    if (r != MODULE_SUCCESS) {
        obs_shutdown();
        return 1;
    }
    CHECK(obs_init_module(mod), "T01", "obs_module_load devuelve true");
    const char *display = obs_source_get_display_name("soundgrid_input");
    CHECK(display != nullptr, "T01", (std::string("fuente registrada: ") + (display ? display : "(null)")).c_str());
    CHECK(obs_get_source_output_flags("soundgrid_input") & OBS_SOURCE_AUDIO, "T01", "fuente de audio");
    {
        const char *author = obs_get_module_author(mod);
        CHECK(author && std::string(author) == "Joan Palacio", "T01",
              (std::string("autor del módulo: ") + (author ? author : "(null)")).c_str());
        obs_properties_t *pp = obs_get_source_properties("soundgrid_input");
        CHECK(pp && obs_properties_get(pp, "credits") != nullptr, "T01", "propiedad de créditos presente");
        obs_properties_destroy(pp);
    }

    // --- T02/T03: el simulador llega a OBS y el mapeo L/R es correcto ---
    obs_data_t *s = mockSettings(0, 1);
    obs_source_t *src = obs_source_create("soundgrid_input", "SG Main L/R", s, nullptr);
    obs_data_release(s);
    CHECK(src != nullptr, "T02", "obs_source_create");
    Capture cap;
    obs_source_add_audio_capture_callback(src, captureCb, &cap);
    const bool got = waitSamples(cap, 48000 * 2, 8000);
    CHECK(got, "T02", "2 s de audio entregados por obs_source_output_audio");
    {
        std::lock_guard lk(cap.mu);
        const size_t n = std::min<size_t>(cap.l.size(), 96000);
        std::vector<float> l(cap.l.begin(), cap.l.begin() + n), rr(cap.r.begin(), cap.r.begin() + std::min(n, cap.r.size()));
        const double fl = sgtest::dominant(l, {1000, 2000, 3000}, 48000);
        const double fr = sgtest::dominant(rr, {1000, 2000, 3000}, 48000);
        const double db = sgtest::rmsDb(l);
        char msg[160];
        std::snprintf(msg, sizeof(msg), "L=%.0f Hz, R=%.0f Hz, RMS L=%.1f dBFS (esperado 1 kHz / 2 kHz / -23 dBFS)", fl, fr, db);
        CHECK(fl == 1000.0 && fr == 2000.0 && db > -24.0 && db < -22.0, "T02/T03", msg);
        CHECK(cap.monotonic, "T08", "timestamps crecientes en la salida de OBS");
    }
    obs_source_remove_audio_capture_callback(src, captureCb, &cap);

    // --- T03: swap L/R aplicado en caliente (sin reabrir) ---
    obs_data_t *upd = mockSettings(1, 0);
    obs_source_update(src, upd);
    obs_data_release(upd);
    Capture cap2;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    obs_source_add_audio_capture_callback(src, captureCb, &cap2);
    waitSamples(cap2, 48000, 5000);
    {
        std::lock_guard lk(cap2.mu);
        const double fl = sgtest::dominant(cap2.l, {1000, 2000, 3000}, 48000);
        CHECK(fl == 2000.0, "T03", "tras el swap, L lleva la entrada 2 (2 kHz)");
    }
    obs_source_remove_audio_capture_callback(src, captureCb, &cap2);

    // --- Propiedades: se construyen sin error ---
    obs_properties_t *props = obs_source_properties(src);
    int count = 0;
    for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p))
        ++count;
    CHECK(props && count >= 4, "UI", "obs_source_properties construye los grupos");
    obs_properties_destroy(props);
    obs_data_t *after = obs_source_get_settings(src);
    const std::string status = obs_data_get_string(after, "status_text");
    obs_data_release(after);
    std::printf("---- estado mostrado en el panel ----\n%s\n-------------------------------------\n", status.c_str());
    CHECK(status.find("Simulador") != std::string::npos && status.find("No identificada") != std::string::npos,
          "T50", "el panel distingue simulador y no afirma una LV1");

    // --- T09/T40: guardar y restaurar la fuente (persistencia nativa de OBS) ---
    obs_data_t *saved = obs_save_source(src);
    const char *json = obs_data_get_json(saved);
    obs_source_release(src);
    obs_source_t *restored = obs_load_source(saved);
    obs_data_release(saved);
    obs_data_t *rs = obs_source_get_settings(restored);
    CHECK(std::string(obs_data_get_string(rs, "device")) == "mock:default" && obs_data_get_int(rs, "ch_1") == 1 &&
              obs_data_get_int(rs, "ch_2") == 0,
          "T09", "la fuente restaurada conserva dispositivo y mapa de canales");
    CHECK(obs_data_get_double(rs, "alerts.A01.threshold") == -60.0 && !obs_data_get_bool(rs, "armed"),
          "T40", "umbrales por defecto y A01 desarmada");
    obs_data_release(rs);
    (void)json;
    obs_source_release(restored);

    // --- T10: crear/eliminar 100 veces ---
    const auto t0 = std::chrono::steady_clock::now();
    bool alive = true;
    for (int i = 0; i < 100 && alive; ++i) {
        obs_data_t *d = mockSettings(i % 4, (i + 1) % 4);
        obs_source_t *x = obs_source_create("soundgrid_input", ("SG " + std::to_string(i)).c_str(), d, nullptr);
        obs_data_release(d);
        alive = x != nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        obs_source_release(x);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char m10[96];
    std::snprintf(m10, sizeof(m10), "100 ciclos crear/eliminar sin fallo (%.2f s)", secs);
    CHECK(alive, "T10", m10);

    // --- HIL opcional: un driver real (p. ej. SG_HIL_DEVICE="asio:Waves SoundGrid") hasta OBS ---
    // Solo lectura: el plugin nunca cambia frecuencia, buffer, reloj ni la consola.
    if (const char *hil = std::getenv("SG_HIL_DEVICE"); hil && *hil) {
        obs_data_t *hs = obs_data_create();
        obs_data_set_string(hs, "device", hil);
        obs_data_set_int(hs, "mode", 1);
        obs_data_set_int(hs, "ch_1", 0);
        obs_data_set_int(hs, "ch_2", 1);
        obs_source_t *hsrc = obs_source_create("soundgrid_input", "SG HIL", hs, nullptr);
        obs_data_release(hs);
        Capture hc;
        obs_source_add_audio_capture_callback(hsrc, captureCb, &hc);
        const bool hgot = waitSamples(hc, 48000 * 5, 15000);
        obs_source_remove_audio_capture_callback(hsrc, captureCb, &hc);
        {
            std::lock_guard lk(hc.mu);
            float pl = 0, pr = 0;
            for (float v : hc.l)
                pl = std::max(pl, std::fabs(v));
            for (float v : hc.r)
                pr = std::max(pr, std::fabs(v));
            char mh[200];
            std::snprintf(mh, sizeof(mh), "%s -> OBS: %.1f s recibidos · L pico %.1f / RMS %.1f dBFS · R pico %.1f / RMS %.1f dBFS",
                          hil, hc.l.size() / 48000.0, pl > 0 ? 20 * std::log10(pl) : -180.0, sgtest::rmsDb(hc.l),
                          pr > 0 ? 20 * std::log10(pr) : -180.0, sgtest::rmsDb(hc.r));
            CHECK(hgot && pl > 1e-5f, "HIL-T14", mh);
            CHECK(hc.monotonic, "HIL-T08", "timestamps crecientes en la salida de OBS con el driver real");
        }
        obs_data_t *ha = obs_source_get_settings(hsrc);
        obs_properties_t *hp = obs_source_properties(hsrc); // refresca el texto de estado
        obs_properties_destroy(hp);
        obs_data_release(ha);
        ha = obs_source_get_settings(hsrc);
        std::printf("---- estado mostrado en el panel (HIL) ----\n%s\n-------------------------------------------\n",
                    obs_data_get_string(ha, "status_text"));
        obs_data_release(ha);
        obs_source_release(hsrc);
    }

    obs_shutdown();
    std::printf("bnum_allocs al cerrar: %ld\n", bnum_allocs());
    std::printf("%s (%d fallos)\n", g_fail ? "RESULTADO: FAIL" : "RESULTADO: PASS", g_fail);
    return g_fail ? 1 : 0;
}
