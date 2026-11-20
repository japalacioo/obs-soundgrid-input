// Prueba de integración del monitor de audio del Multiview con el libobs REAL y render por GPU.
// Crea una fuente SoundGrid con el dispositivo simulado, el monitor "soundgrid_audio_monitor" y una
// escena privada en la salida de vídeo; recibe los fotogramas finales de OBS (BGRA) y comprueba por
// píxeles: medidores verde/amarillo/rojo, forma de onda, aviso de clip, cabecera "sin señal" y textos.
// Guarda capturas BMP (monitor_*.bmp) en SG_SNAPSHOT_DIR para revisarlas a ojo.
#include "monitor/AudioScope.h"

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
        const int green = a.count(lane, 0xFF4CFF4C);
        CHECK(green > 200, "MON04", ("osciloscopio L con forma de onda reciente: " + std::to_string(green) + " px").c_str());
        const float half = L.lanes[0].h / 2;
        const Rect above{lane.x, L.lanes[0].y + 2 * t, lane.w, half * 0.6f};
        CHECK(a.count(above, 0xFF4CFF4C) == 0, "MON04", "amplitud −20 dBFS: la onda no llega a −6 dBFS");
        CHECK(a.count(rightPart(L.lanes[1], 0.3f), 0xFF4CFF4C) > 200, "MON04", "osciloscopio R con forma de onda");
    }
    CHECK(Image::near(a.at(headerX, headerY), 0xFF262626), "MON05", ("cabecera normal con señal" + a.describe(headerX, headerY)).c_str());
    CHECK(Image::near(a.at(clipX, clipY), 0xFF2B2B2B), "MON06", ("indicador de clip apagado" + a.describe(clipX, clipY)).c_str());
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
    CHECK(Image::near(b.at(clipX, clipY), 0xFFD00000), "MON08", ("indicador de clip encendido" + b.describe(clipX, clipY)).c_str());
    {
        auto [x, y] = meterAt(0, dbToFraction(-2));
        CHECK(Image::near(b.at(x, y), 0xFFFF4C4C), "MON08", ("medidor L en rojo brillante (−2 dB)" + b.describe(x, y)).c_str());
        const int red = b.count(rightPart(L.lanes[0], 0.15f), 0xFFFF4C4C);
        CHECK(red > 200, "MON08", ("forma de onda con clip en rojo: " + std::to_string(red) + " px").c_str());
        const Rect &m = L.meters[0];
        CHECK(Image::near(b.at(m.x - 1.5f * t, m.y + m.h / 2), 0xFFD00000), "MON08", "marco rojo de clip en el medidor L");
    }

    // ---------------------------------------------------------------- 3. sin señal
    obs_data_t *none = sgSettings("", "clipped");
    obs_source_update(sg, none);
    obs_data_release(none);
    Image c = grab(frame, 1500);
    saveBmp(c, snapDir + "/monitor_3_sin_senal.bmp");
    CHECK(Image::near(c.at(headerX, headerY), 0xFFD00000), "MON09", ("cabecera roja \"SIN SEÑAL\"" + c.describe(headerX, headerY)).c_str());
    {
        const Rect recent = rightPart(L.lanes[0], 0.15f); // último 0,75 s
        const int wave = recent.w > 0 ? c.count(recent, 0xFF4CFF4C) + c.count(recent, 0xFFFF4C4C) : 0;
        CHECK(wave == 0, "MON09", ("sin audio, la forma de onda reciente queda vacía: " + std::to_string(wave) + " px").c_str());
        const int older = c.count(L.lanes[0], 0xFFFF4C4C);
        CHECK(older > 100, "MON09", "el audio anterior sigue visible y se desplaza a la izquierda");
    }

    // ---------------------------------------------------------------- 4. fuente fija inexistente
    obs_data_t *missing = obs_data_create();
    obs_data_set_string(missing, "fixed_source", "No existe");
    obs_source_update(mon, missing);
    obs_data_release(missing);
    Image d = grab(frame, 800);
    CHECK(Image::near(d.at(headerX, headerY), 0xFFD00000), "MON10", "fuente inexistente: cabecera roja, sin fallo");
    {
        auto [x, y] = meterAt(0, dbToFraction(-42));
        CHECK(Image::near(d.at(x, y), 0xFF267F26), "MON10", ("medidor reiniciado al cambiar de fuente" + d.describe(x, y)).c_str());
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
