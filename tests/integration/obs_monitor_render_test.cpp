// Prueba de integración del monitor de audio del Multiview con el libobs REAL y render por GPU.
// Crea una fuente SoundGrid con el dispositivo simulado, el monitor "soundgrid_audio_monitor" y una
// escena privada en la salida de vídeo; recibe los fotogramas finales de OBS (BGRA) y comprueba por
// píxeles: medidores verde/amarillo/rojo, forma de onda, aviso de clip, cabecera "sin señal" y textos
// (vista clásica, MON01–MON10) y la vista de sonoridad, el cambio de vista y los temas (MON11–MON17).
// Guarda capturas BMP (monitor_*.bmp) en SG_SNAPSHOT_DIR para revisarlas a ojo.
#include "monitor/AudioScope.h"
#include "monitor/MonitorStyle.h"

#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near // windows.h define near/far como macros vacías
#undef far
#endif

using namespace sg::monitor;

namespace {

int g_fail = 0;
#define CHECK(cond, id, msg)                                                       \
    do {                                                                           \
        const bool ok_ = (cond);                                                   \
        std::printf("[%s] %s: %s\n", ok_ ? "PASS" : "FAIL", id, msg);              \
        if (!ok_)                                                                  \
            ++g_fail;                                                              \
    } while (0)

constexpr uint32_t kW = 1280, kH = 720;

struct Frame {
    std::mutex mu;
    std::vector<uint8_t> rgba = std::vector<uint8_t>(static_cast<size_t>(kW) * kH * 4);
    uint64_t count = 0;
};

void rawVideo(void *param, struct video_data *f)
{
    auto *fr = static_cast<Frame *>(param);
    std::lock_guard lk(fr->mu);
    for (uint32_t y = 0; y < kH; ++y)
        std::memcpy(&fr->rgba[static_cast<size_t>(y) * kW * 4], f->data[0] + static_cast<size_t>(y) * f->linesize[0],
                    kW * 4);
    ++fr->count;
}

struct Image {
    std::vector<uint8_t> px;
    struct Rgb {
        int r, g, b;
    };
    Rgb at(float x, float y) const
    {
        const int ix = std::clamp(static_cast<int>(x), 0, static_cast<int>(kW) - 1);
        const int iy = std::clamp(static_cast<int>(y), 0, static_cast<int>(kH) - 1);
        const uint8_t *p = &px[(static_cast<size_t>(iy) * kW + ix) * 4];
        return {p[2], p[1], p[0]}; // BGRA
    }
    static bool near(Rgb c, uint32_t argb, int tol = 18)
    {
        const int r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
        return std::abs(c.r - r) <= tol && std::abs(c.g - g) <= tol && std::abs(c.b - b) <= tol;
    }
    int count(const Rect &r, uint32_t argb, int tol = 18) const
    {
        int n = 0;
        for (int y = static_cast<int>(r.y); y < static_cast<int>(r.bottom()); ++y)
            for (int x = static_cast<int>(r.x); x < static_cast<int>(r.right()); ++x)
                n += near(at(static_cast<float>(x), static_cast<float>(y)), argb, tol);
        return n;
    }
    int countNot(const Rect &r, uint32_t argb, int tol = 40) const // píxeles que no son el fondo (texto)
    {
        int n = 0;
        for (int y = static_cast<int>(r.y); y < static_cast<int>(r.bottom()); ++y)
            for (int x = static_cast<int>(r.x); x < static_cast<int>(r.right()); ++x)
                n += !near(at(static_cast<float>(x), static_cast<float>(y)), argb, tol);
        return n;
    }
    int countBright(const Rect &r) const // texto blanco
    {
        int n = 0;
        for (int y = static_cast<int>(r.y); y < static_cast<int>(r.bottom()); ++y)
            for (int x = static_cast<int>(r.x); x < static_cast<int>(r.right()); ++x) {
                const Rgb c = at(static_cast<float>(x), static_cast<float>(y));
                n += c.r > 200 && c.g > 200 && c.b > 200;
            }
        return n;
    }
    std::string describe(float x, float y) const
    {
        const Rgb c = at(x, y);
        char b[64];
        std::snprintf(b, sizeof(b), " (rgb %d,%d,%d)", c.r, c.g, c.b);
        return b;
    }
};

Image grab(Frame &fr, int waitMs)
{
    uint64_t start;
    {
        std::lock_guard lk(fr.mu);
        start = fr.count;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
    for (int i = 0; i < 100; ++i) { // al menos dos fotogramas nuevos
        {
            std::lock_guard lk(fr.mu);
            if (fr.count >= start + 2)
                return Image{fr.rgba};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::lock_guard lk(fr.mu);
    return Image{fr.rgba};
}

void saveBmp(const Image &img, const std::string &path)
{
    std::ofstream f(path, std::ios::binary);
    const uint32_t row = kW * 3, size = 54 + row * kH;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
    f.write("BM", 2);
    u32(size);
    u32(0);
    u32(54);
    u32(40);
    u32(kW);
    u32(kH);
    u16(1);
    u16(24);
    u32(0);
    u32(row * kH);
    u32(2835);
    u32(2835);
    u32(0);
    u32(0);
    for (int y = static_cast<int>(kH) - 1; y >= 0; --y)
        for (uint32_t x = 0; x < kW; ++x) {
            const uint8_t *p = &img.px[(static_cast<size_t>(y) * kW + x) * 4];
            const char bgr[3] = {static_cast<char>(p[0]), static_cast<char>(p[1]), static_cast<char>(p[2])};
            f.write(bgr, 3);
        }
}

bool loadModule(const std::string &bin, const std::string &data)
{
    obs_module_t *m = nullptr;
    if (obs_open_module(&m, bin.c_str(), data.c_str()) != MODULE_SUCCESS)
        return false;
    return obs_init_module(m);
}

obs_data_t *sgSettings(const char *device, const char *signal)
{
    obs_data_t *s = obs_data_create();
    obs_data_set_string(s, "device", device);
    obs_data_set_int(s, "mode", 1);
    obs_data_set_int(s, "ch_1", 0);
    obs_data_set_int(s, "ch_2", 1);
    obs_data_set_string(s, "mock_signal", signal);
    obs_data_set_bool(s, "autodetect_done", true);
    return s;
}

Rect rightPart(const Rect &r, float fraction)
{
    return {r.right() - r.w * fraction, r.y, r.w * fraction, r.h};
}

} // namespace

int main()
{
    namespace fs = std::filesystem;
    const fs::path cfg = fs::temp_directory_path() / "sg_obs_monitor_test";
    fs::remove_all(cfg);
    fs::create_directories(cfg);
    const std::string snapDir = std::getenv("SG_SNAPSHOT_DIR") ? std::getenv("SG_SNAPSHOT_DIR") : SG_SNAPSHOT_DIR;

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
    // libobs carga libobs-d3d11 y los módulos con LOAD_LIBRARY_SEARCH_*: no busca en PATH. Se añade la
    // carpeta de obs.dll, como hace OBS con la suya (igual que obs_host_test.cpp).
    if (HMODULE h = GetModuleHandleW(L"obs.dll")) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(h, path, MAX_PATH);
        std::wstring dir(path);
        dir = dir.substr(0, dir.find_last_of(L"\\/"));
        AddDllDirectory(dir.c_str());
    }
#endif

    obs_video_info ovi{};
    ovi.graphics_module = SG_GRAPHICS_MODULE;
    ovi.fps_num = 30;
    ovi.fps_den = 1;
    ovi.base_width = ovi.output_width = kW;
    ovi.base_height = ovi.output_height = kH;
    // libobs usa texturas BGRA para la salida de 8 bits (obs_init_textures).
    ovi.output_format = VIDEO_FORMAT_BGRA;
    ovi.colorspace = VIDEO_CS_SRGB;
    ovi.range = VIDEO_RANGE_FULL;
    ovi.scale_type = OBS_SCALE_BICUBIC;
    ovi.gpu_conversion = true;
    const int vr = obs_reset_video(&ovi);
    CHECK(vr == OBS_VIDEO_SUCCESS, "ENV", ("obs_reset_video con " + std::string(SG_GRAPHICS_MODULE)).c_str());
    if (vr != OBS_VIDEO_SUCCESS) {
        obs_shutdown();
        std::printf("RESULTADO: FAIL\n");
        return 1;
    }

    CHECK(loadModule(SG_PLUGIN_PATH, SG_PLUGIN_DATA), "MON01", "carga obs-soundgrid-input");
    const bool haveText = loadModule(SG_TEXT_MODULE_BIN, SG_TEXT_MODULE_DATA);
    std::printf("[INFO] módulo de texto de OBS: %s\n", haveText ? "cargado" : "no disponible (sin textos)");
    CHECK(obs_get_source_output_flags("soundgrid_audio_monitor") & OBS_SOURCE_VIDEO, "MON01",
          "fuente de vídeo soundgrid_audio_monitor registrada");

    obs_data_t *ss = sgSettings("mock:default", "sine"); // tonos a −20 dBFS
    obs_source_t *sg = obs_source_create("soundgrid_input", "SG Camara 1", ss, nullptr);
    obs_data_release(ss);

    obs_data_t *ms = obs_data_create();
    obs_data_set_int(ms, "follow", 2);
    obs_data_set_string(ms, "fixed_source", "SG Camara 1");
    obs_data_set_int(ms, "window_s", 5);
    obs_source_t *mon = obs_source_create("soundgrid_audio_monitor", "Monitor", ms, nullptr);
    obs_data_release(ms);

    obs_scene_t *scene = obs_scene_create_private("monitor-test");
    obs_scene_add(scene, mon);
    obs_set_output_source(0, obs_scene_get_source(scene));
    Frame frame;
    obs_add_raw_video_callback(nullptr, rawVideo, &frame);

    const MonitorLayout L = computeLayout(static_cast<float>(kW), static_cast<float>(kH));
    const Theme &D = theme(ThemeId::Dark); // tema por defecto de las dos vistas
    const float t = L.border;
    auto meterAt = [&](int ch, double frac) {
        const Rect &m = L.meters[ch];
        return std::pair<float, float>{m.x + m.w / 2, m.bottom() - static_cast<float>(frac) * m.h};
    };
    const float headerX = L.clipBadge.x - 4 * t, headerY = L.header.y + 3 * t;
    const float clipX = L.clipBadge.x + 5 * t, clipY = L.clipBadge.y + 5 * t;

    // ---------------------------------------------------------------- 1. tonos a −20 dBFS
    Image a = grab(frame, 2500);
    saveBmp(a, snapDir + "/monitor_1_tonos.bmp");
    CHECK(obs_source_get_width(mon) == kW && obs_source_get_height(mon) == kH, "MON02",
          "el monitor ocupa todo el lienzo (encaja en el recuadro del Multiview)");
    for (int ch = 0; ch < 2; ++ch) {
        const char *nm = ch ? "R" : "L";
        auto [x1, y1] = meterAt(ch, dbToFraction(-42));
        CHECK(Image::near(a.at(x1, y1), 0xFF4CFF4C), "MON03",
              (std::string("medidor ") + nm + ": verde brillante por debajo del nivel (−42 dB)" + a.describe(x1, y1)).c_str());
        auto [x2, y2] = meterAt(ch, dbToFraction(-15));
        CHECK(Image::near(a.at(x2, y2), 0xFF7F7F26), "MON03",
              (std::string("medidor ") + nm + ": zona amarilla apagada por encima del nivel (−15 dB)" + a.describe(x2, y2)).c_str());
        auto [x3, y3] = meterAt(ch, dbToFraction(-4));
        CHECK(Image::near(a.at(x3, y3), 0xFF7F2626), "MON03",
              (std::string("medidor ") + nm + ": zona roja apagada (−4 dB)" + a.describe(x3, y3)).c_str());
    }
    {
        const Rect lane = rightPart(L.lanes[0], 0.3f);
        const int green = a.count(lane, D.wave);
        CHECK(green > 200, "MON04", ("osciloscopio L con forma de onda reciente: " + std::to_string(green) + " px").c_str());
        const float half = L.lanes[0].h / 2;
        const Rect above{lane.x, L.lanes[0].y + 2 * t, lane.w, half * 0.6f};
        CHECK(a.count(above, D.wave) == 0, "MON04", "amplitud −20 dBFS: la onda no llega a −6 dBFS");
        CHECK(a.count(rightPart(L.lanes[1], 0.3f), D.wave) > 200, "MON04", "osciloscopio R con forma de onda");
    }
    CHECK(Image::near(a.at(headerX, headerY), D.header), "MON05", ("cabecera normal con señal" + a.describe(headerX, headerY)).c_str());
    CHECK(Image::near(a.at(clipX, clipY), D.badgeOff), "MON06", ("indicador de clip apagado" + a.describe(clipX, clipY)).c_str());
    if (haveText) {
        CHECK(a.countBright(L.label) > 100, "MON07", "etiqueta de la fuente dibujada");
        CHECK(a.countBright(L.scale) > 100, "MON07", "números de la escala dBFS dibujados");
        CHECK(a.countBright(L.readout[0]) > 100 && a.countBright(L.readout[1]) > 100, "MON07",
              "lecturas Peak/RMS de L y R dibujadas");
        CHECK(a.countBright(L.holdText[0]) > 20, "MON07", "valor del peak hold dibujado");
    }

    // ---------------------------------------------------------------- 2. señal saturada
    obs_data_t *clip = obs_data_create();
    obs_data_set_string(clip, "mock_signal", "clipped");
    obs_source_update(sg, clip);
    obs_data_release(clip);
    Image b = grab(frame, 1200);
    saveBmp(b, snapDir + "/monitor_2_clip.bmp");
    CHECK(Image::near(b.at(clipX, clipY), D.alert), "MON08", ("indicador de clip encendido" + b.describe(clipX, clipY)).c_str());
    {
        auto [x, y] = meterAt(0, dbToFraction(-2));
        CHECK(Image::near(b.at(x, y), 0xFFFF4C4C), "MON08", ("medidor L en rojo brillante (−2 dB)" + b.describe(x, y)).c_str());
        const int red = b.count(rightPart(L.lanes[0], 0.15f), D.alert);
        CHECK(red > 200, "MON08", ("forma de onda con clip en rojo: " + std::to_string(red) + " px").c_str());
        const Rect &m = L.meters[0];
        CHECK(Image::near(b.at(m.x - 1.5f * t, m.y + m.h / 2), D.alert), "MON08", "marco rojo de clip en el medidor L");
    }

    // ---------------------------------------------------------------- 3. sin señal
    obs_data_t *none = sgSettings("", "clipped");
    obs_source_update(sg, none);
    obs_data_release(none);
    Image c = grab(frame, 1500);
    saveBmp(c, snapDir + "/monitor_3_sin_senal.bmp");
    CHECK(Image::near(c.at(headerX, headerY), D.alert), "MON09", ("cabecera roja \"SIN SEÑAL\"" + c.describe(headerX, headerY)).c_str());
    {
        const Rect recent = rightPart(L.lanes[0], 0.15f); // último 0,75 s
        const int wave = recent.w > 0 ? c.count(recent, D.wave) + c.count(recent, D.alert) : 0;
        CHECK(wave == 0, "MON09", ("sin audio, la forma de onda reciente queda vacía: " + std::to_string(wave) + " px").c_str());
        const int older = c.count(L.lanes[0], D.alert);
        CHECK(older > 100, "MON09", "el audio anterior sigue visible y se desplaza a la izquierda");
    }

    // ---------------------------------------------------------------- 4. fuente fija inexistente
    obs_data_t *missing = obs_data_create();
    obs_data_set_string(missing, "fixed_source", "No existe");
    obs_source_update(mon, missing);
    obs_data_release(missing);
    Image d = grab(frame, 800);
    CHECK(Image::near(d.at(headerX, headerY), D.alert), "MON10", "fuente inexistente: cabecera roja, sin fallo");
    {
        auto [x, y] = meterAt(0, dbToFraction(-42));
        CHECK(Image::near(d.at(x, y), 0xFF267F26), "MON10", ("medidor reiniciado al cambiar de fuente" + d.describe(x, y)).c_str());
    }

    // ================================================================ vista de sonoridad (LUFS)
    auto setMon = [&](void (*fn)(obs_data_t *)) {
        obs_data_t *u = obs_data_create();
        fn(u);
        obs_source_update(mon, u);
        obs_data_release(u);
    };
    // Vuelve el audio (tonos del simulador) y la fuente fija; cambia a la vista de sonoridad.
    obs_data_t *back = sgSettings("mock:default", "sine");
    obs_source_update(sg, back);
    obs_data_release(back);
    setMon([](obs_data_t *u) {
        obs_data_set_string(u, "fixed_source", "SG Camara 1");
        obs_data_set_int(u, "view", 1);
        obs_data_set_string(u, "lufs_preset", "ebu_r128");
        obs_data_set_int(u, "lufs_history", 30);
        obs_data_set_bool(u, "lufs_dynamics", true);
    });
    const LoudnessLayout LL = computeLoudnessLayout(static_cast<float>(kW), static_cast<float>(kH), false, true);
    auto anyZone = [&](const Image &img, float x, float y, bool dim) {
        const Image::Rgb c = img.at(x, y);
        for (auto z : {sg::loudness::TargetZone::Below, sg::loudness::TargetZone::Within, sg::loudness::TargetZone::Above})
            if (Image::near(c, zoneColor(D, z, dim), 12))
                return true;
        return false;
    };
    Image e = grab(frame, 5500); // short-term necesita 3 s
    saveBmp(e, snapDir + "/monitor_5_sonoridad.bmp");
    {
        const float bx = LL.bar.x + LL.bar.w / 2;
        const float by = LL.bar.bottom() - 0.05f * LL.bar.h; // −57 LUFS: siempre dentro de la barra
        CHECK(Image::near(e.at(bx, by), D.barFill), "MON11", ("barra de momentary gris por debajo del objetivo" + e.describe(bx, by)).c_str());
        const float yT = LL.bar.bottom() - static_cast<float>(lufsFraction(-23.0)) * LL.bar.h;
        CHECK(Image::near(e.at(bx, yT), D.target, 24), "MON11",
              ("objetivo (−23 LUFS) en naranja sobre la barra" + e.describe(bx, yT)).c_str());
        const float gT = LL.graph.bottom() - static_cast<float>(lufsFraction(-23.0)) * LL.graph.h;
        CHECK(Image::near(e.at(LL.graph.x + 4 * t, gT), D.target, 24), "MON11", "objetivo en naranja en el historial");
        const Rect recent = rightPart(LL.graph, 0.5f);
        int area = 0;
        for (auto z : {sg::loudness::TargetZone::Below, sg::loudness::TargetZone::Within, sg::loudness::TargetZone::Above})
            area += e.count(recent, zoneColor(D, z, true), 10);
        CHECK(area > 500, "MON11", ("historial: área de short-term " + std::to_string(area) + " px").c_str());
        // Con un tono estable, momentary = integrated: la línea de integrated (encima) tapa la de momentary.
        const int mom = e.count(recent, D.momentaryLine, 12);
        const int integ = e.count(recent, D.integratedLine, 12);
        CHECK(integ > 50, "MON11", ("historial: línea de integrated " + std::to_string(integ) + " px").c_str());
        CHECK(mom + integ > 100, "MON11", ("historial: momentary + integrated " + std::to_string(mom + integ) + " px").c_str());
        int hist = 0;
        for (auto z : {sg::loudness::TargetZone::Below, sg::loudness::TargetZone::Within, sg::loudness::TargetZone::Above})
            hist += e.count(LL.histogram, zoneColor(D, z, true), 12);
        CHECK(hist > 20, "MON11", ("distribución de short-term " + std::to_string(hist) + " px").c_str());
        CHECK(Image::near(e.at(LL.dynamics.x + LL.dynamics.w / 2, LL.dynamics.bottom() - 3 * t), D.panelAlt), "MON11",
              "panel de dinámica (PSR) bajo el historial");
        CHECK(Image::near(e.at(LL.badge.x + 5 * t, LL.badge.y + 5 * t), D.badgeOff), "MON11", "sin alerta de true peak a −20 dBFS");
        CHECK(Image::near(e.at(headerX, headerY), D.header), "MON11", "cabecera normal con señal (mismo estilo que la clásica)");
        const float chipX = LL.statusChip.x + LL.statusChip.w / 2, chipY = LL.statusChip.y + LL.statusChip.h * 0.12f;
        CHECK(anyZone(e, chipX, chipY, false), "MON11", ("estado del integrated respecto al objetivo" + e.describe(chipX, chipY)).c_str());
        CHECK(Image::near(e.at(LL.bigCard.x + LL.bigCard.w / 2, LL.bigCard.bottom() - 2 * t), D.cardHi), "MON11",
              "tarjeta de integrated resaltada");
        if (haveText) {
            CHECK(e.countNot(LL.bigValue, D.cardHi) > 150, "MON11", "valor grande de integrated dibujado");
            int rows = 0;
            for (const Rect &r : LL.rows)
                rows += e.countNot(cardValue(r), D.card) > 40 && e.countNot(cardLabel(r), D.card) > 20;
            CHECK(rows == kReadoutRows, "MON11", ("tarjetas con valor y nombre: " + std::to_string(rows) + " de 8").c_str());
            CHECK(e.countBright(LL.label) > 100, "MON11", "etiqueta de la fuente y del objetivo");
        }
    }

    // ---------------------------------------------------------------- true peak por encima del umbral
    obs_data_t *hot = obs_data_create();
    obs_data_set_string(hot, "mock_signal", "clipped");
    obs_source_update(sg, hot);
    obs_data_release(hot);
    {
        bool badge = false, row = false;
        Image last;
        for (int i = 0; i < 8; ++i) { // parpadea: alguna captura con el rojo encendido
            last = grab(frame, i ? 150 : 1200);
            badge = badge || Image::near(last.at(LL.badge.x + 5 * t, LL.badge.y + 5 * t), D.alert);
            const Rect &tr = LL.rows[2];
            row = row || Image::near(last.at(tr.x + t, tr.y + tr.h / 2), D.above);
        }
        saveBmp(last, snapDir + "/monitor_6_true_peak.bmp");
        CHECK(badge, "MON12", "distintivo TP en rojo (true peak > −1 dBTP en los últimos 3 s)");
        CHECK(row, "MON12", "tarjeta TRUE PEAK MAX con borde rojo");
        if (haveText) {
            int red = 0;
            for (int i = 0; i < 4 && !red; ++i)
                red = grab(frame, 120).count(cardValue(LL.rows[2]), D.above, 30);
            CHECK(red > 30, "MON12", ("valor de true peak en rojo: " + std::to_string(red) + " px").c_str());
        }
        // Señal saturada: el true peak queda por encima del short-term -> barras de PSR.
        const int dyn = last.count(LL.dynamics, D.dynamics, 12);
        CHECK(dyn > 20, "MON12", ("dinámica (PSR) dibujada: " + std::to_string(dyn) + " px").c_str());
    }

    // ---------------------------------------------------------------- reinicio
    obs_data_t *quiet = sgSettings("", "sine"); // sin dispositivo: sin audio
    obs_source_update(sg, quiet);
    obs_data_release(quiet);
    {
        obs_properties_t *props = obs_source_properties(mon);
        obs_property_t *btn = props ? obs_properties_get(props, "lufs_reset") : nullptr;
        CHECK(btn != nullptr, "MON13", "botón «Reiniciar medición» en las propiedades");
        if (btn)
            obs_property_button_clicked(btn, mon);
        obs_properties_destroy(props);
        Image r = grab(frame, 1200);
        saveBmp(r, snapDir + "/monitor_7_reinicio.bmp");
        const float chipX = LL.statusChip.x + LL.statusChip.w / 2, chipY = LL.statusChip.y + LL.statusChip.h * 0.12f;
        CHECK(Image::near(r.at(chipX, chipY), D.badgeOff), "MON13",
              ("integrated sin medir tras el reinicio" + r.describe(chipX, chipY)).c_str());
        const int integ = r.count(LL.graph, D.integratedLine, 12);
        CHECK(integ < 10, "MON13", ("historial vacío tras el reinicio: " + std::to_string(integ) + " px").c_str());
        const Rect &tr = LL.rows[2];
        CHECK(Image::near(r.at(tr.x + t, tr.y + tr.h / 2), D.card), "MON13", "alerta de true peak reiniciada");
        CHECK(Image::near(r.at(headerX, headerY), D.alert), "MON13", "cabecera roja: sin señal");
    }

    // ---------------------------------------------------------------- mezcla maestra y modo compacto
    obs_scene_add(scene, sg); // el audio de la fuente entra en la mezcla (pista 1)
    obs_data_t *mix = sgSettings("mock:default", "sine");
    obs_source_update(sg, mix);
    obs_data_release(mix);
    setMon([](obs_data_t *u) {
        obs_data_set_int(u, "follow", 3);
        obs_data_set_int(u, "lufs_layout", 1);
    });
    {
        Image cpt = grab(frame, 2500);
        saveBmp(cpt, snapDir + "/monitor_8_compacto_mezcla.bmp");
        const LoudnessLayout C = computeLoudnessLayout(static_cast<float>(kW), static_cast<float>(kH), true, true);
        CHECK(Image::near(cpt.at(headerX, headerY), D.header), "MON14", "mezcla maestra: llega audio (cabecera normal)");
        CHECK(Image::near(cpt.at(C.bar.x + C.bar.w / 2, C.bar.bottom() - 0.05f * C.bar.h), D.barFill), "MON14",
              "modo compacto: barra de momentary");
        if (haveText) {
            CHECK(cpt.countNot(C.bigValue, D.cardHi) > 300, "MON14", "modo compacto: integrated grande");
            int rows = 0;
            for (const Rect &r : C.rows)
                rows += cpt.countNot(cardValue(r), D.card) > 40;
            CHECK(rows == 3, "MON14", "modo compacto: short-term, momentary y true peak");
        }
    }

    // ---------------------------------------------------------------- tema claro en las dos vistas
    setMon([](obs_data_t *u) {
        obs_data_set_int(u, "theme", 1);
        obs_data_set_int(u, "lufs_layout", 0);
    });
    const Theme &Lt = theme(ThemeId::Light);
    {
        Image lt = grab(frame, 1500);
        saveBmp(lt, snapDir + "/monitor_9_sonoridad_claro.bmp");
        CHECK(Image::near(lt.at(2, 2), Lt.canvas), "MON15", ("tema claro: fondo" + lt.describe(2, 2)).c_str());
        CHECK(Image::near(lt.at(LL.readPanel.x + LL.readPanel.w / 2, LL.readPanel.bottom() - 2), Lt.panel), "MON15",
              "tema claro: panel");
        CHECK(Image::near(lt.at(LL.rows[3].x + LL.rows[3].w / 2, LL.rows[3].y + 2 * t), Lt.card), "MON15", "tema claro: tarjeta");
        if (haveText) {
            int dark = 0;
            const Rect lb = cardLabel(LL.rows[3]);
            for (int y = static_cast<int>(lb.y); y < static_cast<int>(lb.bottom()); ++y)
                for (int x = static_cast<int>(lb.x); x < static_cast<int>(lb.right()); ++x) {
                    const Image::Rgb c = lt.at(static_cast<float>(x), static_cast<float>(y));
                    dark += c.r < 150 && c.g < 150 && c.b < 150;
                }
            CHECK(dark > 30, "MON15", ("tema claro: texto oscuro en las tarjetas: " + std::to_string(dark) + " px").c_str());
        }
    }
    // Vuelta a la vista clásica (el conmutador): mismos medidores de siempre con el tema claro.
    setMon([](obs_data_t *u) {
        obs_data_set_int(u, "view", 0);
        obs_data_set_int(u, "follow", 2);
    });
    {
        Image cl = grab(frame, 1500);
        saveBmp(cl, snapDir + "/monitor_10_clasica_claro.bmp");
        auto [x1, y1] = meterAt(0, dbToFraction(-42));
        CHECK(Image::near(cl.at(x1, y1), 0xFF4CFF4C), "MON16", ("vista clásica de nuevo: medidor verde" + cl.describe(x1, y1)).c_str());
        CHECK(Image::near(cl.at(L.meterPanel.x + L.meterPanel.w / 2, L.meterPanel.bottom() - 2), Lt.panel), "MON16",
              "vista clásica con el tema claro (estilo común)");
        CHECK(cl.count(rightPart(L.lanes[0], 0.3f), Lt.wave) > 200, "MON16", "forma de onda en el color del tema");
    }
    // Atajo/menú: el conmutador también funciona por ajuste (toggle) sin perder la configuración.
    setMon([](obs_data_t *u) { obs_data_set_int(u, "view", 1); });
    {
        Image tg = grab(frame, 1200);
        obs_data_t *st = obs_source_get_settings(mon);
        CHECK(std::string(obs_data_get_string(st, "lufs_preset")) == "ebu_r128" &&
                  obs_data_get_int(st, "lufs_history") == 30 && obs_data_get_int(st, "theme") == 1,
              "MON17", "al cambiar de vista se conserva la configuración de las dos");
        obs_data_release(st);
        CHECK(Image::near(tg.at(LL.readPanel.x + LL.readPanel.w / 2, LL.readPanel.bottom() - 2), Lt.panel), "MON17",
              "de vuelta en la vista de sonoridad");
    }
    std::printf("[INFO] fotogramas retrasados por el render: %u de %u\n", obs_get_lagged_frames(), obs_get_total_frames());

    obs_remove_raw_video_callback(rawVideo, &frame);
    obs_set_output_source(0, nullptr);
    obs_scene_release(scene);
    obs_source_release(mon);
    obs_source_release(sg);
    obs_shutdown();
    std::printf("[INFO] asignaciones de libobs sin liberar tras obs_shutdown: %ld\n", bnum_allocs());
    std::printf("capturas en %s\n", snapDir.c_str());
    std::printf("RESULTADO: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
