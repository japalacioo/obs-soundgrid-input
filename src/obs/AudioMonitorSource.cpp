// Monitor de audio para el Multiview (ver AudioMonitorSource.h, ADR-016 y ADR-017).
//
// Dos vistas con el mismo estilo (MonitorStyle): la clásica (medidores L/R, osciloscopio, Peak/RMS y
// clip) y la de sonoridad (ITU-R BS.1770-4: momentary, short-term, integrated, true peak, LRA, PLR,
// PSR y DR, con historial, distribución, objetivo y presets). Se cambia en las propiedades, en
// Herramientas o con un atajo.
//
// Hilos:
//  - Interfaz de OBS: create/update/destroy/propiedades y eventos del frontend (escena al aire).
//  - Hilo de gráficos de OBS: video_tick (niveles, textos) y video_render (dibujo). La vista clásica
//    lee AudioScopeTap, que la fuente SoundGrid alimenta desde su hilo de salida.
//  - Hilo de audio de OBS: la vista de sonoridad recibe el audio de la fuente medida
//    (obs_source_add_audio_capture_callback, como los medidores del mezclador de OBS) o de la mezcla
//    maestra (obs_add_raw_audio_callback). Solo mide: nunca modifica el audio.
#include "AudioMonitorSource.h"

#include "ObsSoundGridSource.h"
#include "PluginSettings.h"
#include "monitor/AudioScope.h"
#include "monitor/Loudness.h"
#include "monitor/MonitorStyle.h"
#include "monitor/SceneAudioPicker.h"

#include <graphics/graphics.h>
#include <graphics/vec3.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define T_(s) obs_module_text(s)

namespace sg {

namespace {

using namespace monitor;

constexpr const char *kMonitorFlag = "sg_audio_monitor_scene"; // ajuste privado de la escena del monitor
constexpr const char *kFollow = "follow";
constexpr const char *kFixed = "fixed_source";
constexpr const char *kWindow = "window_s";
constexpr const char *kAutoScene = "global.auto_scene"; // ajuste de todo OBS, mostrado en el monitor
constexpr const char *kView = "view";
constexpr const char *kTheme = "theme";
constexpr const char *kTextScale = "text_scale";
constexpr const char *kMasterTrack = "master_track";
constexpr const char *kLoudnessGroup = "lufs";
constexpr const char *kPreset = "lufs_preset";
constexpr const char *kTarget = "lufs_target";
constexpr const char *kTolerance = "lufs_tolerance";
constexpr const char *kTpLimit = "lufs_tp_limit";
constexpr const char *kLraMax = "lufs_lra_max";
constexpr const char *kUnits = "lufs_units";
constexpr const char *kChannels = "lufs_channels";
constexpr const char *kRate = "lufs_rate";
constexpr const char *kHistory = "lufs_history";
constexpr const char *kOversample = "lufs_oversample";
constexpr const char *kLayout = "lufs_layout";
constexpr const char *kDynamics = "lufs_dynamics";
constexpr const char *kResetOnSwitch = "lufs_reset_on_switch";
enum Follow { FollowProgram = 0, FollowPreview = 1, FollowFixed = 2, FollowMaster = 3 };
enum View { ViewClassic = 0, ViewLoudness = 1 };

// Medidor clásico: valores por defecto de VolumeMeter de OBS (su significado no cambia con el tema).
constexpr uint32_t kBgNominal = 0xFF267F26, kBgWarning = 0xFF7F7F26, kBgError = 0xFF7F2626;
constexpr uint32_t kFgNominal = 0xFF4CFF4C, kFgWarning = 0xFFFFFF4C, kFgError = 0xFFFF4C4C;
constexpr uint32_t kClipWhite = 0xFFFFFFFF, kMagnitude = 0xFF000000;



#if defined(_WIN32)
constexpr const char *kTextId = "text_gdiplus";
constexpr const char *kSans = "Arial";
constexpr const char *kMono = "Consolas";
#elif defined(__APPLE__)
constexpr const char *kTextId = "text_ft2_source";
constexpr const char *kSans = "Helvetica";
constexpr const char *kMono = "Menlo";
#else
constexpr const char *kTextId = "text_ft2_source";
constexpr const char *kSans = "Sans Serif";
constexpr const char *kMono = "Monospace";
#endif

// --------------------------------------------------------------- escena seguida (hilo de UI -> gráficos)

std::mutex g_sceneMu;
obs_weak_source_t *g_program = nullptr; // última escena de programa que no es el monitor
obs_weak_source_t *g_preview = nullptr; // ídem para la vista previa (solo modo estudio)

void storeWeak(obs_weak_source_t *&slot, obs_source_t *src)
{
    std::lock_guard lk(g_sceneMu);
    obs_weak_source_release(slot);
    slot = src ? obs_source_get_weak_source(src) : nullptr;
}

obs_source_t *loadStrong(obs_weak_source_t *slot) // referencia nueva o nullptr
{
    std::lock_guard lk(g_sceneMu);
    return slot ? obs_weak_source_get_source(slot) : nullptr;
}

bool sceneHasMonitorItem(obs_scene_t *scene)
{
    bool found = false;
    obs_scene_enum_items(
        scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *p) {
            const char *id = obs_source_get_id(obs_sceneitem_get_source(item));
            if (id && std::string(id) == kAudioMonitorSourceId) {
                *static_cast<bool *>(p) = true;
                return false;
            }
            return true;
        },
        &found);
    return found;
}

// Solo la escena que creó el plugin (marcada con kMonitorFlag) es "la escena del monitor". Una escena
// del director que contenga el monitor NO lo es: si lo fuera, el plugin la sacaría del aire.
bool isMonitorScene(obs_source_t *sceneSrc)
{
    if (!sceneSrc || !obs_scene_from_source(sceneSrc))
        return false;
    obs_data_t *ps = obs_source_get_private_settings(sceneSrc);
    const bool flagged = obs_data_get_bool(ps, kMonitorFlag);
    obs_data_release(ps);
    return flagged;
}

// Escena del monitor de la colección actual (referencia nueva) o nullptr.
obs_source_t *findMonitorScene()
{
    obs_source_t *found = nullptr;
    obs_frontend_source_list list = {};
    obs_frontend_get_scenes(&list);
    for (size_t i = 0; i < list.sources.num && !found; ++i)
        if (isMonitorScene(list.sources.array[i]))
            found = obs_source_get_ref(list.sources.array[i]);
    obs_frontend_source_list_free(&list);
    return found;
}

struct RevertTask {
    obs_weak_source_t *target;
    bool preview;
};

void revertOnUi(void *p)
{
    auto *t = static_cast<RevertTask *>(p);
    if (obs_source_t *s = obs_weak_source_get_source(t->target)) {
        blog(LOG_INFO, "[soundgrid] monitor de audio: %s vuelve a '%s'", t->preview ? "vista previa" : "programa",
             obs_source_get_name(s));
        if (t->preview) {
            obs_frontend_set_current_preview_scene(s);
        } else if (obs_frontend_preview_program_mode_active()) {
            // Modo estudio: la escena del monitor llegó a programa (p. ej. doble clic en el Multiview).
            obs_frontend_set_current_preview_scene(s);
            obs_frontend_preview_program_trigger_transition();
        } else {
            obs_frontend_set_current_scene(s);
        }
        obs_source_release(s);
    }
    obs_weak_source_release(t->target);
    delete t;
}

// Si la escena del monitor llega a programa/vista previa (p. ej. un clic en el Multiview), se vuelve a
// la anterior: el monitor nunca debe salir en la emisión.
void guardScene(obs_source_t *current, obs_weak_source_t *&slot, bool preview)
{
    if (!current)
        return;
    if (!isMonitorScene(current)) {
        storeWeak(slot, current);
        return;
    }
    obs_source_t *back = loadStrong(slot);
    if (!back) { // p. ej. OBS arrancó con el monitor al aire: la primera escena que no sea el monitor
        obs_frontend_source_list list = {};
        obs_frontend_get_scenes(&list);
        for (size_t i = 0; i < list.sources.num && !back; ++i)
            if (!isMonitorScene(list.sources.array[i]))
                back = obs_source_get_ref(list.sources.array[i]);
        obs_frontend_source_list_free(&list);
    }
    if (back && back != current) {
        blog(LOG_WARNING, "[soundgrid] la escena del monitor de audio no puede ir a %s: se vuelve a '%s'",
             preview ? "vista previa" : "programa", obs_source_get_name(back));
        obs_queue_task(OBS_TASK_UI, revertOnUi, new RevertTask{obs_source_get_weak_source(back), preview}, false);
    }
    obs_source_release(back);
}

void refreshFollowed()
{
    obs_source_t *prog = obs_frontend_get_current_scene();
    guardScene(prog, g_program, false);
    obs_source_release(prog);
    if (obs_frontend_preview_program_mode_active()) {
        obs_source_t *prev = obs_frontend_get_current_preview_scene();
        guardScene(prev, g_preview, true);
        obs_source_release(prev);
    } else {
        storeWeak(g_preview, nullptr);
    }
}


// --------------------------------------------------------------- textos (fuentes privadas, como el Multiview)

struct TextLabel {
    obs_source_t *src = nullptr;
    std::string text;
    int size = 0;
    bool bold = false;
    bool mono = false;
    uint32_t rgb = 0xFFFFFF;
};

obs_data_t *textSettings(const TextLabel &l)
{
    obs_data_t *s = obs_data_create();
    obs_data_t *font = obs_data_create();
    obs_data_set_string(font, "face", l.mono ? kMono : kSans);
    obs_data_set_int(font, "size", std::max(6, l.size));
    obs_data_set_int(font, "flags", l.bold ? 1 : 0);
    obs_data_set_string(font, "style", l.bold ? "Bold" : "Regular");
    obs_data_set_obj(s, "font", font);
    obs_data_release(font);
    obs_data_set_string(s, "text", l.text.c_str());
    obs_data_set_bool(s, "outline", false);
    obs_data_set_bool(s, "drop_shadow", false);
    // l.rgb es 0xRRGGBB; las propiedades de color de OBS (text_ft2_source y text_gdiplus) son 0xAABBGGRR.
    const uint32_t bgr = ((l.rgb & 0xFF) << 16) | (l.rgb & 0xFF00) | ((l.rgb >> 16) & 0xFF);
    obs_data_set_int(s, "color1", 0xFF000000 | bgr);
    obs_data_set_int(s, "color2", 0xFF000000 | bgr);
    obs_data_set_int(s, "color", bgr);
    obs_data_set_int(s, "opacity", 100);
    return s;
}

void createText(TextLabel &l, const std::string &text, bool bold, bool mono)
{
    l.text = text;
    l.bold = bold;
    l.mono = mono;
    l.size = 12;
    obs_data_t *s = textSettings(l);
    l.src = obs_source_create_private(kTextId, "soundgrid-monitor-text", s);
    obs_data_release(s);
}

// Los cambios se aplican en el siguiente tick de OBS (las fuentes privadas también se procesan).
void setText(TextLabel &l, const std::string &text)
{
    if (!l.src || l.text == text)
        return;
    l.text = text;
    obs_data_t *s = obs_data_create();
    obs_data_set_string(s, "text", text.c_str());
    obs_source_update(l.src, s);
    obs_data_release(s);
}

void setStyle(TextLabel &l, int size, uint32_t rgb)
{
    size = std::max(6, size);
    if (!l.src || (l.size == size && l.rgb == rgb))
        return;
    l.size = size;
    l.rgb = rgb;
    obs_data_t *s = textSettings(l);
    obs_source_update(l.src, s);
    obs_data_release(s);
}

// --------------------------------------------------------------- dibujo

struct Painter {
    gs_eparam_t *color = nullptr;
    void rect(float x, float y, float w, float h, uint32_t argb) const
    {
        if (w <= 0 || h <= 0)
            return;
        gs_effect_set_color(color, argb);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_matrix_scale3f(w, h, 1.0f);
        gs_draw_sprite(nullptr, 0, 1, 1);
        gs_matrix_pop();
    }
    void rect(const Rect &r, uint32_t argb) const { rect(r.x, r.y, r.w, r.h, argb); }
    // Rectángulo con esquinas redondeadas (cuarto de círculo aproximado con franjas de ~1,5 px), como las
    // tarjetas de Youlean.
    void round(const Rect &r, float radius, uint32_t argb) const
    {
        const float rad = std::min(radius, std::min(r.w, r.h) / 2);
        if (rad < 1.5f) {
            rect(r, argb);
            return;
        }
        rect(r.x, r.y + rad, r.w, r.h - 2 * rad, argb);
        const int n = std::clamp(static_cast<int>(std::ceil(rad / 1.5f)), 4, 24); // ~1,5 px por franja
        const float sh = rad / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const float dy = rad - (i + 0.5f) * sh;
            const float inset = rad - std::sqrt(std::max(0.0f, rad * rad - dy * dy));
            rect(r.x + inset, r.y + i * sh, r.w - 2 * inset, sh + 0.5f, argb);
            rect(r.x + inset, r.bottom() - (i + 1) * sh - 0.5f, r.w - 2 * inset, sh + 0.5f, argb);
        }
    }
    void frame(const Rect &r, float t, uint32_t argb) const
    {
        rect(r.x, r.y, r.w, t, argb);
        rect(r.x, r.bottom() - t, r.w, t, argb);
        rect(r.x, r.y, t, r.h, argb);
        rect(r.right() - t, r.y, t, r.h, argb);
    }
};

// align: 0 izquierda, 0.5 centro, 1 derecha. Reduce el texto si no cabe en maxW.
void drawText(const TextLabel &l, float x, float centerY, float align, float maxW = 0)
{
    if (!l.src)
        return;
    const float w = static_cast<float>(obs_source_get_width(l.src));
    const float h = static_cast<float>(obs_source_get_height(l.src));
    if (w <= 0 || h <= 0)
        return;
    const float k = maxW > 0 && w > maxW ? maxW / w : 1.0f;
    gs_matrix_push();
    gs_matrix_translate3f(x - w * k * align, centerY - h * k / 2.0f, 0.0f);
    gs_matrix_scale3f(k, k, 1.0f);
    obs_source_video_render(l.src);
    gs_matrix_pop();
}

uint32_t argb(uint32_t rgb)
{
    return 0xFF000000 | rgb;
}

uint32_t bgColor(Zone z)
{
    return z == Zone::Nominal ? kBgNominal : z == Zone::Warning ? kBgWarning : kBgError;
}

uint32_t fgColor(Zone z)
{
    return z == Zone::Nominal ? kFgNominal : z == Zone::Warning ? kFgWarning : kFgError;
}

// Color del peak hold como en OBS (VolumeMeter::getPeakColor).
uint32_t holdColor(double db)
{
    if (db < -50.0)
        return kBgNominal;
    if (db < kWarningDb)
        return kFgNominal;
    if (db < kErrorDb)
        return kFgWarning;
    if (db < 0.0)
        return kFgError;
    return kClipWhite;
}

// Segmentos de línea agrupados por color sobre un único vertex buffer dinámico (una llamada a gs_draw
// por color: miles de columnas sin miles de llamadas).
class LineBatch {
public:
    ~LineBatch()
    {
        if (vb_) {
            obs_enter_graphics();
            gs_vertexbuffer_destroy(vb_);
            obs_leave_graphics();
        }
    }
    void begin() { groups_.clear(); }
    void add(uint32_t color, float x0, float y0, float x1, float y1)
    {
        if (groups_.empty() || groups_.back().color != color)
            groups_.push_back({color, {}});
        auto &v = groups_.back().pts;
        v.push_back({x0, y0});
        v.push_back({x1, y1});
    }
    void draw(gs_eparam_t *colorParam)
    {
        std::size_t total = 0;
        for (const auto &g : groups_)
            total += g.pts.size();
        if (!total)
            return;
        ensure(total);
        if (!vb_)
            return;
        vec3 *pts = gs_vertexbuffer_get_data(vb_)->points;
        std::size_t i = 0;
        for (const auto &g : groups_)
            for (const auto &p : g.pts)
                vec3_set(&pts[i++], p[0], p[1], 0.0f);
        gs_vertexbuffer_flush(vb_);
        gs_load_vertexbuffer(vb_);
        gs_load_indexbuffer(nullptr);
        std::size_t start = 0;
        for (const auto &g : groups_) {
            gs_effect_set_color(colorParam, g.color);
            gs_draw(GS_LINES, static_cast<uint32_t>(start), static_cast<uint32_t>(g.pts.size()));
            start += g.pts.size();
        }
        gs_load_vertexbuffer(nullptr);
    }

private:
    struct Group {
        uint32_t color;
        std::vector<std::array<float, 2>> pts;
    };
    void ensure(std::size_t verts)
    {
        if (vb_ && cap_ >= verts)
            return;
        if (vb_)
            gs_vertexbuffer_destroy(vb_);
        const std::size_t n = std::max<std::size_t>(verts, 4096);
        gs_vb_data *d = gs_vbdata_create();
        d->num = n;
        d->points = static_cast<vec3 *>(bmalloc(sizeof(vec3) * n));
        std::memset(d->points, 0, sizeof(vec3) * n);
        vb_ = gs_vertexbuffer_create(d, GS_DYNAMIC);
        cap_ = vb_ ? n : 0;
    }
    std::vector<Group> groups_;
    gs_vertbuffer_t *vb_ = nullptr;
    std::size_t cap_ = 0;
};

// --------------------------------------------------------------- escena -> árbol para el selector

std::shared_ptr<SceneNode> buildTree(obs_scene_t *scene, int depth)
{
    auto node = std::make_shared<SceneNode>();
    node->name = obs_source_get_name(obs_scene_get_source(scene));
    struct Ctx {
        std::vector<SceneItemNode> items;
        int depth;
    } ctx{{}, depth};
    obs_scene_enum_items(
        scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *p) {
            auto *c = static_cast<Ctx *>(p);
            obs_source_t *src = obs_sceneitem_get_source(item);
            SceneItemNode n;
            const char *id = obs_source_get_id(src);
            const char *name = obs_source_get_name(src);
            n.sourceId = id ? id : "";
            n.sourceName = name ? name : "";
            n.visible = obs_sceneitem_visible(item);
            if (c->depth < 8)
                if (obs_scene_t *child = obs_group_or_scene_from_source(src))
                    n.child = buildTree(child, c->depth + 1);
            c->items.push_back(std::move(n));
            return true;
        },
        &ctx);
    // OBS enumera de abajo arriba; la lista de fuentes se lee de arriba abajo.
    node->items.assign(ctx.items.rbegin(), ctx.items.rend());
    return node;
}

// --------------------------------------------------------------- configuración

struct Config {
    int view = ViewClassic;
    int theme = 0; // ThemeId
    float textScale = 1.0f;
    int follow = FollowProgram;
    std::string fixed;
    int masterTrack = 1;
    int windowSec = 5;
    // Sonoridad
    std::string preset = "ebu_r128";
    double target = -23.0, tolerance = 1.0, tpLimit = -1.0, lraMax = 0.0;
    int units = 0;       // 0 según el preset, 1 LUFS, 2 LKFS
    int channels = 0;    // 0 automático, 1 mono, 2 estéreo, 3 5.1
    int rateHz = 10;
    int historySec = 300;
    int oversample = 4;
    bool compact = false;
    bool dynamics = false;
    bool resetOnSwitch = false;
};

// Objetivo efectivo: el del preset o, con "custom", los valores del usuario.
struct Target {
    std::string name;
    double target, tolerance, tpLimit, lraMax;
    bool lkfs;
};

Target effectiveTarget(const Config &c)
{
    const loudness::Preset *p = loudness::findPreset(c.preset);
    if (!p)
        p = loudness::findPreset("ebu_r128");
    Target t{p->name, p->target, p->tolerance, p->truePeak, p->lraMax, p->lkfs};
    if (std::string(p->id) == "custom") {
        t.name = T_("LufsPresetCustom");
        t.target = c.target;
        t.tolerance = c.tolerance;
        t.tpLimit = c.tpLimit;
        t.lraMax = c.lraMax;
    }
    if (c.units == 1)
        t.lkfs = false;
    else if (c.units == 2)
        t.lkfs = true;
    return t;
}

// Papeles de BS.1770-4 para los canales de la salida de audio de OBS (orden de libobs).
struct ChannelPlan {
    std::vector<int> index;
    std::vector<loudness::Role> roles;
};

ChannelPlan channelPlan(int mode, uint32_t obsChannels, speaker_layout speakers)
{
    using loudness::Role;
    ChannelPlan p;
    auto add = [&](int idx, Role r) {
        if (idx < static_cast<int>(obsChannels)) {
            p.index.push_back(idx);
            p.roles.push_back(r);
        }
    };
    if (mode == 1 || obsChannels == 1) { // mono
        add(0, Role::Front);
        return p;
    }
    if (mode == 2) { // estéreo
        add(0, Role::Front);
        add(1, Role::Front);
        return p;
    }
    if (mode == 3 && obsChannels >= 6) // 5.1: FL FR FC LFE RL RR
        speakers = SPEAKERS_5POINT1;
    switch (speakers) {
    case SPEAKERS_2POINT1: // FL FR LFE
        add(0, Role::Front), add(1, Role::Front), add(2, Role::Lfe);
        break;
    case SPEAKERS_4POINT0: // FL FR FC RC
        add(0, Role::Front), add(1, Role::Front), add(2, Role::Front), add(3, Role::Surround);
        break;
    case SPEAKERS_4POINT1: // FL FR FC LFE RC
        add(0, Role::Front), add(1, Role::Front), add(2, Role::Front), add(3, Role::Lfe), add(4, Role::Surround);
        break;
    case SPEAKERS_5POINT1:
        add(0, Role::Front), add(1, Role::Front), add(2, Role::Front), add(3, Role::Lfe);
        add(4, Role::Surround), add(5, Role::Surround);
        break;
    case SPEAKERS_7POINT1: // FL FR FC LFE RL RR SL SR
        add(0, Role::Front), add(1, Role::Front), add(2, Role::Front), add(3, Role::Lfe);
        for (int i = 4; i < 8; ++i)
            add(i, Role::Surround);
        break;
    default:
        add(0, Role::Front), add(1, Role::Front);
        break;
    }
    return p;
}

std::string historyText(int sec)
{
    return "\xE2\x88\x92" + (sec >= 60 ? std::to_string(sec / 60) + " min" : std::to_string(sec) + " s");
}

// --------------------------------------------------------------- la fuente

class AudioMonitor {
public:
    explicit AudioMonitor(obs_data_t *settings)
    {
        update(settings);
        // Comunes y vista clásica
        createText(header_, "", true, false);
        createText(clip_, "CLIP", true, false);
        for (const auto &t : scaleTicks())
            if (t.major) {
                scaleLabels_.emplace_back();
                createText(scaleLabels_.back(), t.label, false, false);
                scaleFractions_.push_back(t.fraction);
            }
        for (int i = 0; i < 2; ++i) {
            createText(hold_[i], "", false, true);
            createText(chan_[i], i ? "R" : "L", true, false);
            createText(readout_[i], "", false, true);
        }
        createText(timeLeft_, "", false, false);
        createText(timeRight_, "0 s", false, false);
        // Vista de sonoridad
        createText(tpBadge_, "TP", true, false);
        createText(barTitle_, "M", true, false);
        createText(barUnit_, "LUFS", false, false);
        for (const auto &t : loudnessTicks())
            if (t.major) {
                lScale_.emplace_back();
                createText(lScale_.back(), t.label, false, false);
                lScaleFractions_.push_back(t.fraction);
            }
        createText(bigLabel_, "INTEGRATED", true, false);
        createText(bigValue_, "", true, false);
        createText(bigUnit_, "LUFS", false, false);
        createText(status_, "", true, false);
        static const char *kRowNames[loudness_rows] = {"SHORT TERM", "MOMENTARY", "TRUE PEAK MAX", "LOUDNESS RANGE",
                                                       "PLR",        "PSR",       "DR",            "MOMENTARY MAX"};
        for (int i = 0; i < loudness_rows; ++i) {
            createText(rowLabel_[i], kRowNames[i], true, false);
            createText(rowValue_[i], "", true, false);
            createText(rowUnit_[i], "", false, false);
        }
        createText(lTimeLeft_, "", false, false);
        createText(dynLabel_, "PSR", false, false);
        createText(presetPill_, "", true, false);
        createText(chanPill_, "", true, false);
        createText(targetLabel_, "", true, false);
    }

    ~AudioMonitor()
    {
        detachAll();
        for (TextLabel *l : allLabels())
            obs_source_release(l->src);
    }

    void update(obs_data_t *s)
    {
        Config c;
        c.view = static_cast<int>(obs_data_get_int(s, kView));
        c.theme = static_cast<int>(obs_data_get_int(s, kTheme));
        c.textScale = static_cast<float>(std::clamp(obs_data_get_int(s, kTextScale), 50LL, 200LL)) / 100.0f;
        c.follow = static_cast<int>(obs_data_get_int(s, kFollow));
        c.fixed = obs_data_get_string(s, kFixed);
        c.masterTrack = static_cast<int>(std::clamp(obs_data_get_int(s, kMasterTrack), 1LL, 6LL));
        c.windowSec = std::clamp(static_cast<int>(obs_data_get_int(s, kWindow)), 1, 30);
        c.preset = obs_data_get_string(s, kPreset);
        c.target = obs_data_get_double(s, kTarget);
        c.tolerance = std::max(0.1, obs_data_get_double(s, kTolerance));
        c.tpLimit = obs_data_get_double(s, kTpLimit);
        c.lraMax = std::max(0.0, obs_data_get_double(s, kLraMax));
        c.units = static_cast<int>(obs_data_get_int(s, kUnits));
        c.channels = static_cast<int>(obs_data_get_int(s, kChannels));
        c.rateHz = std::clamp(static_cast<int>(obs_data_get_int(s, kRate)), 1, 60);
        c.historySec = std::clamp(static_cast<int>(obs_data_get_int(s, kHistory)), 10, 3600);
        c.oversample = obs_data_get_int(s, kOversample) >= 8 ? 8 : 4;
        c.compact = obs_data_get_int(s, kLayout) == 1;
        c.dynamics = obs_data_get_bool(s, kDynamics);
        c.resetOnSwitch = obs_data_get_bool(s, kResetOnSwitch);
        std::lock_guard lk(cfgMu_);
        cfg_ = c;
        cfgDirty_ = true;
    }

    // Reiniciar integrated, LRA, máximos e historial (botón, menú Herramientas o atajo).
    void requestReset() { resetReq_.store(true); }

    uint32_t width() const { return cx_; }
    uint32_t height() const { return cy_; }

    void tick()
    {
        const int64_t now = static_cast<int64_t>(os_gettime_ns());
        bool dirty;
        {
            std::lock_guard lk(cfgMu_);
            dirty = cfgDirty_;
            cfgDirty_ = false;
            run_ = cfg_;
        }
        obs_video_info ovi{};
        const bool resized = obs_get_video_info(&ovi) && (ovi.base_width != cx_ || ovi.base_height != cy_);
        if (resized) {
            cx_ = ovi.base_width;
            cy_ = ovi.base_height;
        }
        if (resized || dirty) {
            layout_ = computeLayout(static_cast<float>(cx_), static_cast<float>(cy_), run_.textScale);
            lLayout_ = computeLoudnessLayout(static_cast<float>(cx_), static_cast<float>(cy_), run_.compact,
                                             run_.dynamics, run_.textScale);
            restyle();
            colsDirty_ = true;
        }
        if (dirty || now - lastPickNs_ >= 250 * kNsPerMsM) {
            pick();
            lastPickNs_ = now;
        }
        nowNs_ = now;
        if (run_.view == ViewLoudness)
            tickLoudness(now, dirty);
        else
            tickClassic(now);
    }

    void render()
    {
        if (!cx_ || !cy_)
            return;
        if (!rendered_) {
            rendered_ = true;
            blog(LOG_DEBUG, "[soundgrid] monitor de audio: primer fotograma %ux%u", cx_, cy_);
        }
        if (run_.view == ViewLoudness)
            renderLoudness();
        else
            renderClassic();
    }

    // Audio de la fuente medida (hilo de audio de OBS). Solo mide: no toca los datos.
    void feed(const audio_data *d)
    {
        if (!d || !d->frames)
            return;
        lastFeedNs_.store(static_cast<int64_t>(os_gettime_ns()));
        std::lock_guard lk(meterMu_);
        if (resetReq_.exchange(false))
            meter_.reset();
        std::array<const float *, MAX_AV_PLANES> planes{};
        const std::size_t n = std::min<std::size_t>(plan_.index.size(), MAX_AV_PLANES);
        for (std::size_t i = 0; i < n; ++i)
            planes[i] = reinterpret_cast<const float *>(d->data[plan_.index[i]]);
        meter_.process(planes.data(), d->frames);
    }

private:
    static constexpr int loudness_rows = kReadoutRows;
    enum Row { RowShort = 0, RowMomentary, RowTruePeak, RowLra, RowPlr, RowPsr, RowDr, RowMomentaryMax };

    const Theme &th() const { return theme(run_.theme == 1 ? ThemeId::Light : ThemeId::Dark); }

    static std::string pad(const std::string &s, std::size_t width = 6)
    {
        // Ancho fijo con fuente monoespaciada (el signo y el ∞ cuentan como un carácter).
        std::size_t glyphs = 0;
        for (unsigned char c : s)
            glyphs += (c & 0xC0) != 0x80;
        return glyphs < width ? std::string(width - glyphs, ' ') + s : s;
    }

    std::vector<TextLabel *> allLabels()
    {
        std::vector<TextLabel *> v{&header_, &clip_, &timeLeft_, &timeRight_, &tpBadge_, &barTitle_, &barUnit_,
                                   &bigLabel_, &bigValue_, &status_, &lTimeLeft_, &dynLabel_};
        for (auto &l : scaleLabels_)
            v.push_back(&l);
        for (auto &l : lScale_)
            v.push_back(&l);
        for (int i = 0; i < 2; ++i) {
            v.push_back(&hold_[i]);
            v.push_back(&chan_[i]);
            v.push_back(&readout_[i]);
        }
        for (int i = 0; i < loudness_rows; ++i) {
            v.push_back(&rowLabel_[i]);
            v.push_back(&rowValue_[i]);
            v.push_back(&rowUnit_[i]);
        }
        for (TextLabel *l : {&bigUnit_, &presetPill_, &chanPill_, &targetLabel_})
            v.push_back(l);
        return v;
    }

    // Tamaños y colores de los textos (cambian con el tamaño del lienzo, el tema y la escala).
    void restyle()
    {
        const Theme &t = th();
        const auto big = static_cast<int>(std::lround(layout_.fontPx));
        const auto small = static_cast<int>(std::lround(layout_.smallFontPx));
        setStyle(clip_, static_cast<int>(big * 0.9), clip_.rgb == t.textOnAccent ? t.textOnAccent : t.textDim);
        for (auto &l : scaleLabels_)
            setStyle(l, static_cast<int>(small * 0.8), t.text);
        for (int i = 0; i < 2; ++i) {
            setStyle(hold_[i], static_cast<int>(small * 0.85), t.text);
            setStyle(chan_[i], small, t.text);
            setStyle(readout_[i], small, t.text);
        }
        setStyle(timeLeft_, static_cast<int>(small * 0.8), t.textDim);
        setStyle(timeRight_, static_cast<int>(small * 0.8), t.textDim);

        const LoudnessLayout &L = lLayout_;
        const auto lsmall = static_cast<int>(std::lround(L.smallFontPx));
        setStyle(tpBadge_, static_cast<int>(big * 0.9), tpBadge_.rgb == t.textOnAccent ? t.textOnAccent : t.textDim);
        setStyle(barTitle_, lsmall, t.text);
        setStyle(barUnit_, static_cast<int>(lsmall * 0.75), t.textDim);
        for (auto &l : lScale_)
            setStyle(l, static_cast<int>(lsmall * 0.72), t.textDim);
        const int bigPx = static_cast<int>(std::min(L.bigFontPx, L.bigValue.h * 0.95f));
        setStyle(bigLabel_, static_cast<int>(std::min(lsmall * 0.78f, L.bigLabel.h * 0.8f)), t.textDim);
        setStyle(status_, static_cast<int>(std::min(lsmall * 0.72f, L.statusChip.h * 0.7f)), t.textOnAccent);
        setStyle(bigValue_, bigPx, bigValue_.rgb == t.textDim ? t.textDim : bigValue_.rgb);
        setStyle(bigUnit_, static_cast<int>(bigPx * 0.36f), t.textDim);
        const float rowH = L.rows.empty() ? 0.0f : L.rows[0].h;
        const int valuePx = static_cast<int>(std::min(L.compact ? L.fontPx * 1.2f : L.smallFontPx * 1.05f,
                                                      cardValue(Rect{0, 0, 1, rowH}).h * 0.95f));
        const int labelPx = static_cast<int>(std::min(lsmall * 0.62f, cardLabel(Rect{0, 0, 1, rowH}).h * 0.8f));
        for (int i = 0; i < loudness_rows; ++i) {
            setStyle(rowLabel_[i], labelPx, t.textDim);
            setStyle(rowValue_[i], valuePx, rowValue_[i].rgb);
            setStyle(rowUnit_[i], static_cast<int>(valuePx * 0.5f), t.textDim);
        }
        setStyle(lTimeLeft_, static_cast<int>(lsmall * 0.8), t.textDim);
        setStyle(dynLabel_, static_cast<int>(lsmall * 0.7), t.textDim);
        const int pillPx = static_cast<int>(std::min(lsmall * 0.8f, L.presetPill.h * 0.55f));
        setStyle(presetPill_, pillPx, t.text);
        setStyle(chanPill_, pillPx, t.text);
        setStyle(targetLabel_, static_cast<int>(lsmall * 0.72), t.target & 0xFFFFFF);
        // header_ depende del estado (rojo = texto blanco): ver tick*.
    }

    void styleHeader(bool accent)
    {
        const Theme &t = th();
        const auto size = static_cast<int>(std::lround(run_.view == ViewLoudness ? lLayout_.fontPx : layout_.fontPx));
        setStyle(header_, size, accent ? t.textOnAccent : t.text);
    }

    // Nombre de lo que se mide (o el motivo de no medir nada).
    std::string sourceText() const
    {
        if (run_.follow == FollowMaster)
            return std::string(T_("AudioMonitorMaster")) + " " + std::to_string(run_.masterTrack);
        if (run_.follow == FollowFixed && run_.fixed.empty())
            return T_("AudioMonitorChooseSource");
        if (shownSource_.empty()) {
            if (run_.follow == FollowFixed)
                return std::string(T_("AudioMonitorSourceMissing")) + ": " + run_.fixed;
            return shownScene_.empty() ? std::string(T_("AudioMonitorNoScene"))
                                       : std::string(T_("AudioMonitorNoSource")) + " \xC2\xAB" + shownScene_ + "\xC2\xBB";
        }
        std::string s = shownSource_;
        if (matches_ > 1)
            s += " (+" + std::to_string(matches_ - 1) + ")";
        if (!shownScene_.empty())
            s += "  \xC2\xB7  " + shownScene_;
        return s;
    }

    // Elige la fuente: la SoundGrid de la escena seguida, una fija o la mezcla maestra.
    void pick()
    {
        obs_source_t *src = nullptr;
        std::string scene;
        int matches = 0;
        if (run_.follow == FollowFixed) {
            if (!run_.fixed.empty())
                src = obs_get_source_by_name(run_.fixed.c_str());
            matches = src ? 1 : 0;
        } else if (run_.follow != FollowMaster) {
            obs_source_t *sceneSrc = nullptr;
            if (run_.follow == FollowPreview)
                sceneSrc = loadStrong(g_preview);
            if (!sceneSrc)
                sceneSrc = loadStrong(g_program);
            if (obs_scene_t *sc = sceneSrc ? obs_scene_from_source(sceneSrc) : nullptr) {
                scene = obs_source_get_name(sceneSrc);
                const PickResult r = pickSoundGridSource(*buildTree(sc, 0));
                matches = r.matches;
                if (!r.sourceName.empty())
                    src = obs_get_source_by_name(r.sourceName.c_str());
            }
            obs_source_release(sceneSrc);
        }
        auto tap = src ? soundGridScopeTap(src) : nullptr;
        if (tap != tap_)
            for (auto &m : meters_)
                m.reset(); // otra fuente: no arrastrar picos ni clips de la anterior
        tap_ = std::move(tap);
        isSoundGrid_ = tap_ != nullptr;
        const std::string shown = src ? obs_source_get_name(src) : "";
        if (shown != shownSource_ || scene != shownScene_)
            blog(LOG_INFO, "[soundgrid] monitor de audio: %s%s%s",
                 run_.follow == FollowMaster ? "mezcla maestra"
                 : shown.empty()             ? "sin fuente"
                                             : ("muestra '" + shown + "'").c_str(),
                 scene.empty() ? "" : " · escena ", scene.c_str());
        shownSource_ = shown;
        shownScene_ = scene;
        matches_ = matches;
        muted_ = src && obs_source_muted(src);

        // Captura para la sonoridad: solo con esa vista activa (sin coste en la clásica).
        if (run_.view == ViewLoudness)
            attach(src);
        else
            detachAll();
        obs_source_release(src);
    }

    // ------------------------------------------------------------------ captura (sonoridad)

    static void captureCb(void *param, obs_source_t *, const audio_data *d, bool)
    {
        static_cast<AudioMonitor *>(param)->feed(d);
    }
    static void mixCb(void *param, size_t, audio_data *d) { static_cast<AudioMonitor *>(param)->feed(d); }

    void attach(obs_source_t *src)
    {
        if (run_.follow == FollowMaster) {
            if (capSrc_)
                detachSource();
            const auto mix = static_cast<size_t>(run_.masterTrack - 1);
            if (masterOn_ && masterMix_ == mix)
                return;
            detachMaster();
            obs_add_raw_audio_callback(mix, nullptr, mixCb, this);
            masterOn_ = true;
            masterMix_ = mix;
            onSwitch("mezcla maestra");
            return;
        }
        detachMaster();
        if (src == capSrc_)
            return;
        detachSource();
        if (!src)
            return;
        capSrc_ = obs_source_get_ref(src);
        obs_source_add_audio_capture_callback(capSrc_, captureCb, this);
        onSwitch(obs_source_get_name(src));
    }

    void onSwitch(const char *what)
    {
        blog(LOG_INFO, "[soundgrid] monitor de audio (sonoridad): mide %s", what);
        if (run_.resetOnSwitch)
            requestReset();
    }

    void detachSource()
    {
        if (!capSrc_)
            return;
        obs_source_remove_audio_capture_callback(capSrc_, captureCb, this);
        obs_source_release(capSrc_);
        capSrc_ = nullptr;
    }

    void detachMaster()
    {
        if (!masterOn_)
            return;
        obs_remove_raw_audio_callback(masterMix_, mixCb, this);
        masterOn_ = false;
    }

    void detachAll()
    {
        detachSource();
        detachMaster();
    }

    // ------------------------------------------------------------------ vista clásica

    void tickClassic(int64_t now)
    {
        spanNs_ = static_cast<int64_t>(run_.windowSec) * kNsPerSecM;
        if (tap_) {
            snap_ = tap_->snapshot(now - spanNs_ - 200 * kNsPerMsM);
        } else {
            snap_ = AudioScopeTap::Snapshot{};
        }
        const Levels lv = computeLevels(snap_.buckets, now - 300 * kNsPerMsM, now);
        for (int i = 0; i < 2; ++i)
            meters_[i].update(now, lv.peakDb[i], lv.rmsDb[i]);
        noSignal_ = !tap_ || snap_.lastDataNs == 0 || now - snap_.lastDataNs > 500 * kNsPerMsM;

        if (now - lastTextNs_ >= 200 * kNsPerMsM) { // lecturas legibles: 5 por segundo
            lastTextNs_ = now;
            for (int i = 0; i < 2; ++i) {
                setText(hold_[i], formatDb(meters_[i].holdDb()));
                setText(readout_[i], std::string(i ? "R" : "L") + "  PEAK " + pad(formatDb(lv.peakDb[i])) +
                                         " dBFS   RMS " + pad(formatDb(lv.rmsDb[i])) + " dBFS");
            }
        }
        setText(timeLeft_, "\xE2\x88\x92" + std::to_string(run_.windowSec) + " s");
        std::string h;
        if (!isSoundGrid_ && run_.follow == FollowMaster)
            h = T_("AudioMonitorClassicNeedsSoundGrid");
        else if (!isSoundGrid_ && !shownSource_.empty())
            h = "\xC2\xAB" + shownSource_ + "\xC2\xBB " + T_("AudioMonitorNotSoundGrid");
        else {
            h = sourceText();
            if (snap_.inputChannels == 1)
                h += "  \xC2\xB7  mono";
            if (noSignal_ && !shownSource_.empty())
                h += "  \xC2\xB7  " + std::string(T_("AudioMonitorNoSignal"));
            else if (muted_)
                h += "  \xC2\xB7  " + std::string(T_("AudioMonitorMuted"));
        }
        setText(header_, h);
        styleHeader(noSignal_ || muted_);
        clipOn_ = meters_[0].clipActive(now) || meters_[1].clipActive(now);
        setStyle(clip_, clip_.size, clipOn_ ? th().textOnAccent : th().textDim);
    }

    void beginSolid(gs_technique_t *&tech, Painter &p)
    {
        gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
        p.color = gs_effect_get_param_by_name(solid, "color");
        tech = gs_effect_get_technique(solid, "Solid");
        gs_technique_begin(tech);
        gs_technique_begin_pass(tech, 0);
    }

    static void endSolid(gs_technique_t *tech)
    {
        gs_technique_end_pass(tech);
        gs_technique_end(tech);
    }

    // Cabecera y distintivo comunes a las dos vistas.
    void drawHeader(const Painter &p, const Rect &header, const Rect &badge, bool badgeOn, float t) const
    {
        const Theme &T = th();
        const float rad = std::max(2.0f, t * 3.0f);
        p.round(header, rad, noSignal_ ? T.alert : muted_ ? T.muted : T.header);
        const Rect inner{badge.x + t * 2, badge.y + t * 2, badge.w - t * 4, badge.h - t * 4};
        if (badgeOn)
            p.round(inner, rad, argb(T.textOnAccent));
        p.round(badgeOn ? Rect{inner.x + t, inner.y + t, inner.w - 2 * t, inner.h - 2 * t} : inner, rad,
                badgeOn ? T.alert : T.badgeOff);
    }

    void renderClassic()
    {
        const MonitorLayout &L = layout_;
        const Theme &T = th();
        Painter p;
        gs_technique_t *tech = nullptr;
        beginSolid(tech, p);
        const float t = L.border;
        p.rect(L.canvas, T.canvas);
        drawHeader(p, L.header, L.clipBadge, clipOn_, t);
        const float rad = std::max(2.0f, t * 3.0f);
        p.round(L.meterPanel, rad, T.panel);
        p.round(L.scopePanel, rad, T.panel);
        for (const Rect &r : L.readout) // lecturas Peak/RMS en tarjetas, como la vista de sonoridad
            p.round({r.x, r.y + t, r.w, r.h - 2 * t}, rad, T.card);
        drawMeters(p);
        drawScope(p);
        endSolid(tech);

        drawText(header_, L.label.x + L.border * 4, L.label.y + L.label.h / 2, 0.0f, L.label.w - L.border * 8);
        drawText(clip_, L.clipBadge.x + L.clipBadge.w / 2, L.clipBadge.y + L.clipBadge.h / 2, 0.5f,
                 L.clipBadge.w * 0.9f);
        const float scx = L.scale.x + L.scale.w / 2;
        for (std::size_t k = 0; k < scaleLabels_.size(); ++k)
            drawText(scaleLabels_[k], scx, L.scale.bottom() - static_cast<float>(scaleFractions_[k]) * L.scale.h, 0.5f,
                     L.scale.w * 0.62f);
        for (int i = 0; i < 2; ++i) {
            const Rect &m = L.meters[i];
            drawText(hold_[i], m.x + m.w / 2, L.holdText[i].y + L.holdText[i].h / 2, 0.5f, m.w * 1.6f);
            drawText(chan_[i], m.x + m.w / 2, L.channelText[i].y + L.channelText[i].h / 2, 0.5f);
            drawText(readout_[i], L.readout[i].x + L.border * 3, L.readout[i].y + L.readout[i].h / 2, 0.0f,
                     L.readout[i].w - L.border * 6);
        }
        drawText(timeLeft_, L.timeAxis.x, L.timeAxis.y + L.timeAxis.h / 2, 0.0f);
        drawText(timeRight_, L.timeAxis.right(), L.timeAxis.y + L.timeAxis.h / 2, 1.0f);
    }

    void drawMeters(const Painter &p)
    {
        const MonitorLayout &L = layout_;
        const Theme &T = th();
        const float t = L.border;
        for (int i = 0; i < 2; ++i) {
            const Rect &m = L.meters[i];
            const MeterBallistics &b = meters_[i];
            auto yOf = [&](double frac) { return m.bottom() - static_cast<float>(frac) * m.h; };
            for (const auto &z : meterZones())
                p.rect(m.x, yOf(z.to), m.w, static_cast<float>(z.to - z.from) * m.h, bgColor(z.zone));
            for (const auto &z : litZones(b.displayPeakDb()))
                p.rect(m.x, yOf(z.to), m.w, static_cast<float>(z.to - z.from) * m.h, fgColor(z.zone));
            const float mark = std::max(2.0f, t * 1.5f);
            const double rmsF = dbToFraction(b.displayRmsDb());
            if (rmsF > 0)
                p.rect(m.x, yOf(rmsF), m.w, mark, kMagnitude); // RMS: marca negra (como OBS)
            const double holdF = dbToFraction(b.holdDb());
            if (holdF > 0)
                p.rect(m.x, yOf(holdF), m.w, mark, holdColor(b.holdDb()));
            if (b.clipActive(nowNs_))
                p.frame({m.x - 2 * t, m.y - 2 * t, m.w + 4 * t, m.h + 4 * t}, 2 * t, T.alert);
        }
        // Escala: marcas a ambos lados (junto a cada barra) cada 1 dB, mayores cada 5 dB.
        const Rect &s = L.scale;
        const float th = std::max(1.0f, t * 0.6f);
        for (const auto &k : scaleTicks()) {
            const float y = s.bottom() - static_cast<float>(k.fraction) * s.h - th / 2;
            const float len = s.w * (k.major ? 0.16f : 0.08f);
            const uint32_t c = k.major ? T.tickMajor : T.tickMinor;
            p.rect(s.x, y, len, th, c);
            p.rect(s.right() - len, y, len, th, c);
        }
    }

    void drawScope(const Painter &p)
    {
        const MonitorLayout &L = layout_;
        const Theme &T = th();
        const float t = L.border;
        const auto cols = static_cast<uint32_t>(std::max(1.0f, std::floor(L.lanes[0].w)));
        const auto wave = waveformColumns(snap_.buckets, nowNs_, spanNs_, cols);
        const float hair = std::max(1.0f, t * 0.5f);
        for (const Rect &lane : L.lanes) {
            p.rect(lane, T.panelAlt);
            const float half = lane.h / 2;
            const float cy = lane.y + half;
            for (int s = 1; s < run_.windowSec; ++s) // rejilla de 1 s, anclada al borde derecho (ahora)
                p.rect(lane.right() - lane.w * s / run_.windowSec, lane.y, hair, lane.h, T.grid);
            p.rect(lane.x, cy - half * 0.5f, lane.w, hair, T.grid); // −6 dBFS
            p.rect(lane.x, cy + half * 0.5f, lane.w, hair, T.grid);
            p.rect(lane.x, cy - hair / 2, lane.w, hair, T.line);
            p.frame(lane, hair, T.line);
        }

        // Forma de onda: una línea vertical mín→máx por columna; las columnas con clip, en rojo.
        const float clipLin = static_cast<float>(std::pow(10.0, kClipDb / 20.0));
        lines_.begin();
        for (int pass = 0; pass < 2; ++pass) // primero las normales y luego las de clip (encima)
            for (int ch = 0; ch < 2; ++ch) {
                const Rect &lane = L.lanes[ch];
                const float half = lane.h / 2 - t;
                const float cy = lane.y + lane.h / 2;
                for (uint32_t x = 0; x < cols; ++x) {
                    const Column &c = wave[x][ch];
                    if (!c.has)
                        continue;
                    const bool clipped = std::max(std::fabs(c.max), std::fabs(c.min)) >= clipLin;
                    if (clipped != (pass == 1))
                        continue;
                    float y0 = cy - std::clamp(c.max, -1.0f, 1.0f) * half;
                    float y1 = cy - std::clamp(c.min, -1.0f, 1.0f) * half;
                    if (y1 - y0 < 1.0f) { // silencio: se ve la línea, el monitor está vivo
                        y0 -= 0.5f;
                        y1 += 0.5f;
                    }
                    const float px = lane.x + static_cast<float>(x) + 0.5f;
                    lines_.add(clipped ? T.alert : T.wave, px, y0, px, y1);
                }
            }
        lines_.draw(p.color);
    }

    // ------------------------------------------------------------------ vista de sonoridad

    void tickLoudness(int64_t now, bool cfgChanged)
    {
        // (Re)configura el medidor si cambian los canales, la frecuencia o el sobremuestreo.
        audio_t *audio = obs_get_audio();
        const audio_output_info *ai = audio ? audio_output_get_info(audio) : nullptr;
        const uint32_t rate = audio ? audio_output_get_sample_rate(audio) : 48000;
        const auto chans = static_cast<uint32_t>(audio ? audio_output_get_channels(audio) : 2);
        const ChannelPlan plan = channelPlan(run_.channels, chans, ai ? ai->speakers : SPEAKERS_STEREO);
        if (cfgChanged || rate != meterRate_ || plan.index != planIndex_) {
            loudness::MeterConfig mc;
            mc.sampleRate = rate;
            mc.roles = plan.roles;
            mc.truePeakFactor = run_.oversample;
            mc.historySeconds = run_.historySec;
            std::lock_guard lk(meterMu_);
            const auto &cur = meter_.config();
            if (cur.sampleRate != mc.sampleRate || cur.roles != mc.roles || cur.truePeakFactor != mc.truePeakFactor ||
                cur.historySeconds != mc.historySeconds) {
                meter_.configure(mc);
                plan_ = plan;
                blog(LOG_INFO, "[soundgrid] monitor de audio (sonoridad): %u Hz, %zu canales, true peak x%d",
                     rate, plan.index.size(), run_.oversample);
            }
            meterRate_ = rate;
            planIndex_ = plan.index;
        }
        target_ = effectiveTarget(run_);

        std::vector<loudness::HistoryPoint> pts;
        {
            std::lock_guard lk(meterMu_);
            if (resetReq_.exchange(false))
                meter_.reset();
            rd_ = meter_.readings();
            const uint64_t sub = meter_.subBlocks();
            if (sub != lastSub_ || colsDirty_) {
                pts = meter_.history(static_cast<std::size_t>(run_.historySec) * loudness::kSubBlocksPerSecond);
                dist_ = meter_.distribution(kLufsMin, kLufsMax);
                lastSub_ = sub;
                colsDirty_ = true;
            }
        }
        if (colsDirty_ && !lLayout_.compact) {
            const auto w = static_cast<uint32_t>(std::max(1.0f, std::floor(lLayout_.graph.w)));
            cols_ = historyColumns(pts, static_cast<std::size_t>(run_.historySec) * loudness::kSubBlocksPerSecond, w);
        }
        colsDirty_ = false;

        const int64_t last = lastFeedNs_.load();
        const bool hasSource = run_.follow == FollowMaster || !shownSource_.empty();
        noSignal_ = hasSource && (last == 0 || now - last > 500 * kNsPerMsM);
        if (!hasSource)
            noSignal_ = true;
        tpAlertNow_ = std::isfinite(rd_.truePeakRecent) && rd_.truePeakRecent > target_.tpLimit;
        tpAlertEver_ = std::isfinite(rd_.truePeakMax) && rd_.truePeakMax > target_.tpLimit;
        blinkOn_ = (now / (300 * kNsPerMsM)) % 2 == 0;

        const std::string unit = target_.lkfs ? "LKFS" : "LUFS";
        std::string h = sourceText();
        if (noSignal_ && hasSource)
            h += "  \xC2\xB7  " + std::string(T_("AudioMonitorNoSignal"));
        else if (muted_)
            h += "  \xC2\xB7  " + std::string(T_("AudioMonitorMuted"));
        setText(header_, h);
        styleHeader(noSignal_ || muted_);
        setStyle(tpBadge_, tpBadge_.size, tpAlertNow_ && blinkOn_ ? th().textOnAccent : th().textDim);

        const int64_t every = kNsPerSecM / std::max(1, run_.rateHz);
        if (now - lastTextNs_ >= every || cfgChanged) {
            lastTextNs_ = now;
            using loudness::formatLoudness;
            const Theme &T = th();
            setText(barUnit_, unit);
            setText(bigUnit_, unit);
            setText(presetPill_, target_.name + "  " + formatLoudness(target_.target));
            setText(chanPill_, channelsText());
            setText(targetLabel_, formatLoudness(target_.target));
            zone_ = loudness::targetZone(rd_.integrated, target_.target, target_.tolerance);
            // Integrated: rojo por encima del objetivo y verde dentro (como Youlean), normal por debajo.
            const bool measuredI = std::isfinite(rd_.integrated);
            setText(bigValue_, formatLoudness(rd_.integrated));
            setStyle(bigValue_, bigValue_.size,
                     !measuredI                                 ? T.textDim
                     : zone_ == loudness::TargetZone::Above  ? (T.above & 0xFFFFFF)
                     : zone_ == loudness::TargetZone::Within ? (T.within & 0xFFFFFF)
                                                             : T.text);
            setText(status_, !measuredI                                 ? T_("LufsStatusNotMeasured")
                             : zone_ == loudness::TargetZone::Below  ? T_("LufsStatusBelow")
                             : zone_ == loudness::TargetZone::Within ? T_("LufsStatusWithin")
                                                                     : T_("LufsStatusAbove"));
            const double values[loudness_rows] = {rd_.shortTerm, rd_.momentary, rd_.truePeakMax, rd_.lra,
                                                  rd_.plr,       rd_.psr,       rd_.dr,          rd_.momentaryMax};
            const std::string units[loudness_rows] = {unit, unit, "dBTP", "LU", "dB", "dB", "dB", unit};
            for (int i = 0; i < loudness_rows; ++i) {
                const bool m = std::isfinite(values[i]);
                setText(rowValue_[i], formatLoudness(values[i]));
                setText(rowUnit_[i], m ? units[i] : ""); // sin medir: solo "—"
                uint32_t c = m ? T.text : T.textDim;
                if (i == RowTruePeak && tpAlertEver_)
                    c = T.above & 0xFFFFFF; // true peak por encima del umbral: en rojo
                if (i == RowLra && target_.lraMax > 0 && m && rd_.lra > target_.lraMax)
                    c = T.above & 0xFFFFFF;
                setStyle(rowValue_[i], rowValue_[i].size, c);
            }
            setText(lTimeLeft_, historyText(run_.historySec));
        }
    }

    std::string channelsText() const
    {
        if (run_.follow == FollowMaster)
            return "MIX " + std::to_string(run_.masterTrack);
        const auto n = plan_.index.size();
        return n == 1 ? "MONO" : n == 2 ? "STEREO" : n == 6 ? "5.1" : n == 8 ? "7.1" : std::to_string(n) + " CH";
    }

    void renderLoudness()
    {
        const LoudnessLayout &L = lLayout_;
        const Theme &T = th();
        Painter p;
        gs_technique_t *tech = nullptr;
        beginSolid(tech, p);
        const float t = L.border;
        p.rect(L.canvas, T.canvas);
        drawHeader(p, L.header, L.badge, tpAlertNow_ && blinkOn_, t);
        p.round(L.presetPill, L.presetPill.h / 2, T.card);
        p.round(L.chanPill, L.chanPill.h / 2, T.card);
        p.round(L.barPanel, L.radius, T.panel);
        p.round(L.readPanel, L.radius, T.panel);
        drawLoudnessBar(p);
        if (!L.compact) {
            p.round(L.graphPanel, L.radius, T.panel);
            drawHistory(p);
        }
        drawCards(p);
        endSolid(tech);
        drawLoudnessTexts();
    }

    float yOfLufs(const Rect &r, double lufs) const { return r.bottom() - static_cast<float>(lufsFraction(lufs)) * r.h; }
    double lo() const { return target_.target - target_.tolerance; }
    double hi() const { return target_.target + target_.tolerance; }

    // Barra M (como Youlean): gris hasta el objetivo, verde dentro de la tolerancia y rojo por encima.
    void drawLoudnessBar(const Painter &p)
    {
        const LoudnessLayout &L = lLayout_;
        const Theme &T = th();
        const float t = L.border;
        const Rect &b = L.bar;
        p.rect(b, T.panelAlt);
        if (std::isfinite(rd_.momentary)) {
            const double m = rd_.momentary;
            auto seg = [&](double from, double to, uint32_t c) {
                if (to <= from)
                    return;
                const float y0 = yOfLufs(b, to), y1 = yOfLufs(b, from);
                p.rect(b.x, y0, b.w, y1 - y0, c);
            };
            seg(kLufsMin, std::min(m, lo()), T.barFill);
            seg(lo(), std::min(m, hi()), T.within);
            seg(hi(), m, T.above);
        }
        if (std::isfinite(rd_.momentaryMax)) { // máximo de momentary
            const float y = yOfLufs(b, rd_.momentaryMax);
            p.rect(b.x, y - t * 0.6f, b.w, std::max(1.5f, t * 1.2f), T.tickMajor);
        }
        p.frame(b, std::max(1.0f, t * 0.5f), T.line);
        // Corchetes del objetivo ([ ] de Youlean) a los lados de la barra, en naranja.
        const float yHi = yOfLufs(b, hi()), yLo = yOfLufs(b, lo()), yT = yOfLufs(b, target_.target);
        const float bw = std::max(2.0f, t * 1.2f), arm = b.w * 0.22f;
        for (float x : {b.x - bw * 2, b.right() + bw}) {
            p.rect(x, yHi, bw, yLo - yHi, T.target);
            const float ax = x < b.x ? x : x - arm + bw;
            p.rect(ax, yHi, arm, bw, T.target);
            p.rect(ax, yLo - bw, arm, bw, T.target);
        }
        p.rect(b.x, yT - bw / 2, b.w, bw, T.target);
        // Triángulo de short-term (blanco, apunta a la barra).
        if (std::isfinite(rd_.shortTerm)) {
            const Rect &m = L.stMarker;
            const float cy = yOfLufs(b, rd_.shortTerm);
            const float tw = m.w * 0.7f, half = tw * 0.55f;
            const int steps = 12;
            for (int i = 0; i < steps; ++i) {
                const float frac = static_cast<float>(i + 1) / steps;
                p.rect(m.x + bw * 2.5f + tw * (1.0f - frac), cy - half * frac, tw / steps + 0.5f, 2 * half * frac,
                       T.momentaryLine);
            }
        }
        drawScaleTicks(p, L.barScale);
    }

    void drawScaleTicks(const Painter &p, const Rect &s) const
    {
        const Theme &T = th();
        const float hair = std::max(1.0f, lLayout_.border * 0.6f);
        for (const auto &k : loudnessTicks()) {
            const float y = s.bottom() - static_cast<float>(k.fraction) * s.h - hair / 2;
            const float len = s.w * (k.major ? 0.16f : 0.08f);
            p.rect(s.right() - len, y, len, hair, k.major ? T.tickMajor : T.tickMinor);
        }
        const float yT = s.bottom() - static_cast<float>(lufsFraction(target_.target)) * s.h;
        p.rect(s.right() - s.w * 0.2f, yT - hair, s.w * 0.2f, hair * 2, T.target);
    }

    void drawHistory(const Painter &p)
    {
        const LoudnessLayout &L = lLayout_;
        const Theme &T = th();
        const float t = L.border;
        const float hair = std::max(1.0f, t * 0.5f);
        const Rect &g = L.graph;
        p.rect(g, T.panelAlt);
        p.rect(g.x, yOfLufs(g, hi()), g.w, yOfLufs(g, lo()) - yOfLufs(g, hi()), T.targetBand);
        for (const auto &k : loudnessTicks())
            if (k.major)
                p.rect(g.x, g.bottom() - static_cast<float>(k.fraction) * g.h - hair / 2, g.w, hair, T.grid);
        for (int i = 1; i < 6; ++i) // rejilla de tiempo: 6 divisiones
            p.rect(g.right() - g.w * i / 6.0f, g.y, hair, g.h, T.grid);
        if (L.dynamics.h > 0)
            p.rect(L.dynamics, T.panelAlt);

        const auto w = static_cast<uint32_t>(cols_.size());
        lines_.begin();
        // Área de short-term: azul por debajo del objetivo, verde en la tolerancia y roja por encima.
        const float yLo = yOfLufs(g, lo()), yHi = yOfLufs(g, hi());
        for (int band = 0; band < 3; ++band)
            for (uint32_t x = 0; x < w; ++x) {
                const HistoryColumn &c = cols_[x];
                if (!c.hasShortTerm)
                    continue;
                const float px = g.x + static_cast<float>(x) + 0.5f, ys = yOfLufs(g, c.shortTerm);
                if (band == 0)
                    lines_.add(T.belowDim, px, std::max(ys, yLo), px, g.bottom());
                else if (band == 1 && ys < yLo)
                    lines_.add(T.withinDim, px, std::max(ys, yHi), px, yLo);
                else if (band == 2 && ys < yHi)
                    lines_.add(T.aboveDim, px, ys, px, yHi);
            }
        // Momentary: línea blanca continua (como Youlean).
        int prev = -1;
        for (uint32_t x = 0; x < w; ++x) {
            const HistoryColumn &c = cols_[x];
            if (!c.has) {
                prev = -1;
                continue;
            }
            const float px = g.x + static_cast<float>(x) + 0.5f;
            const float y0 = yOfLufs(g, c.momentaryMax), y1 = yOfLufs(g, c.momentaryMin);
            if (y1 - y0 >= 1.0f)
                lines_.add(T.momentaryLine, px, y0, px, y1);
            if (prev >= 0) {
                const float qx = g.x + static_cast<float>(prev) + 0.5f;
                const float qy = yOfLufs(g, cols_[prev].momentaryMax);
                lines_.add(T.momentaryLine, qx, qy, px, y0);
                lines_.add(T.momentaryLine, qx, qy + 1, px, y0 + 1);
            }
            prev = static_cast<int>(x);
        }
        // Integrated: línea amarilla.
        prev = -1;
        for (uint32_t x = 0; x < w; ++x) {
            const HistoryColumn &c = cols_[x];
            if (!c.hasIntegrated) {
                prev = -1;
                continue;
            }
            const float px = g.x + static_cast<float>(x) + 0.5f, py = yOfLufs(g, c.integrated);
            if (prev >= 0) {
                const float qx = g.x + static_cast<float>(prev) + 0.5f, qy = yOfLufs(g, cols_[prev].integrated);
                for (float off = 0; off < std::max(2.0f, t); off += 1.0f)
                    lines_.add(T.integratedLine, qx, qy + off, px, py + off);
            }
            prev = static_cast<int>(x);
        }
        if (L.dynamics.h > 0) {
            const Rect &d = L.dynamics;
            for (uint32_t x = 0; x < w; ++x) {
                const HistoryColumn &c = cols_[x];
                if (!c.hasPsr)
                    continue;
                const float px = d.x + static_cast<float>(x) + 0.5f;
                const float f = std::clamp(c.psr / 30.0f, 0.0f, 1.0f); // escala 0–30 dB
                lines_.add(T.dynamics, px, d.bottom() - f * d.h, px, d.bottom());
            }
        }
        lines_.draw(p.color);
        const float yT = yOfLufs(g, target_.target);
        p.rect(g.x, yT - hair, g.w, 2 * hair, T.target); // objetivo en naranja
        p.frame(g, hair, T.line);
        if (L.dynamics.h > 0)
            p.frame(L.dynamics, hair, T.line);
        drawScaleTicks(p, L.graphScale);

        // Distribución de short-term: crece hacia la izquierda desde el borde del historial.
        const Rect &hRect = L.histogram;
        uint64_t mx = 0;
        for (auto v : dist_)
            mx = std::max(mx, v);
        if (mx) {
            const float band = hRect.h / static_cast<float>(dist_.size());
            for (std::size_t i = 0; i < dist_.size(); ++i) {
                if (!dist_[i])
                    continue;
                const double center = kLufsMin + static_cast<double>(i) + 0.5;
                const float len = hRect.w * static_cast<float>(dist_[i]) / static_cast<float>(mx);
                const float y = hRect.bottom() - band * static_cast<float>(i + 1);
                const auto z = loudness::targetZone(center, target_.target, target_.tolerance);
                p.rect(hRect.right() - len, y, len, band + 0.5f, zoneColor(T, z, true));
            }
        }
        p.rect(hRect.right() - hair, hRect.y, hair, hRect.h, T.line);
        p.rect(hRect.x, yT - hair, hRect.w, 2 * hair, T.target);
    }

    void drawCards(const Painter &p)
    {
        const LoudnessLayout &L = lLayout_;
        const Theme &T = th();
        const float t = L.border;
        p.round(L.bigCard, L.radius, T.cardHi);
        p.round(L.statusChip, L.statusChip.h / 2,
                std::isfinite(rd_.integrated) ? zoneColor(T, zone_) : T.badgeOff);
        for (std::size_t i = 0; i < L.rows.size(); ++i) {
            const Rect &r = L.rows[i];
            if (static_cast<int>(i) == RowTruePeak && tpAlertEver_) { // borde rojo (parpadea si es reciente)
                p.round(r, L.radius, tpAlertNow_ && !blinkOn_ ? T.card : T.above);
                p.round({r.x + 2 * t, r.y + 2 * t, r.w - 4 * t, r.h - 4 * t}, L.radius * 0.8f, T.card);
            } else {
                p.round(r, L.radius, T.card);
            }
        }
    }

    // Valor y unidad (más pequeña) centrados en un rectángulo, como las tarjetas de Youlean.
    static void drawValueUnit(const TextLabel &v, const TextLabel &u, const Rect &r, float align = 0.5f)
    {
        const float wv = v.src ? static_cast<float>(obs_source_get_width(v.src)) : 0.0f;
        const float wu = u.src && !u.text.empty() ? static_cast<float>(obs_source_get_width(u.src)) : 0.0f;
        const float gap = wu > 0 ? static_cast<float>(u.size) * 0.35f : 0.0f;
        const float total = wv + gap + wu, maxW = r.w * 0.94f;
        const float k = total > maxW && total > 0 ? maxW / total : 1.0f;
        const float x0 = align < 0.75f ? r.x + (r.w - total * k) * align : r.right() - total * k - r.w * 0.04f;
        const float cy = r.y + r.h / 2;
        drawText(v, x0, cy, 0.0f, wv * k);
        if (wu > 0) {
            const float hv = static_cast<float>(obs_source_get_height(v.src)) * k;
            const float hu = static_cast<float>(obs_source_get_height(u.src)) * k;
            drawText(u, x0 + (wv + gap) * k, cy + (hv - hu) * 0.32f, 0.0f, wu * k);
        }
    }

    void drawLoudnessTexts()
    {
        const LoudnessLayout &L = lLayout_;
        const float t = L.border;
        drawText(header_, L.label.x + t * 4, L.label.y + L.label.h / 2, 0.0f, L.label.w - t * 6);
        drawText(presetPill_, L.presetPill.x + L.presetPill.w / 2, L.presetPill.y + L.presetPill.h / 2, 0.5f,
                 L.presetPill.w * 0.88f);
        drawText(chanPill_, L.chanPill.x + L.chanPill.w / 2, L.chanPill.y + L.chanPill.h / 2, 0.5f, L.chanPill.w * 0.86f);
        drawText(tpBadge_, L.badge.x + L.badge.w / 2, L.badge.y + L.badge.h / 2, 0.5f, L.badge.w * 0.9f);
        drawText(barTitle_, L.bar.x + L.bar.w / 2, L.barTitle.y + L.barTitle.h / 2, 0.5f);
        drawText(barUnit_, L.bar.x + L.bar.w / 2, L.barUnit.y + L.barUnit.h / 2, 0.5f, L.barPanel.w * 0.9f);
        drawScaleLabels(L.barScale);
        drawValueUnit(bigValue_, bigUnit_, L.bigValue, 1.0f);
        drawText(bigLabel_, L.bigLabel.x + L.bigCard.w * 0.05f, L.bigLabel.y + L.bigLabel.h / 2, 0.0f, L.bigLabel.w * 0.92f);
        drawText(status_, L.statusChip.x + L.statusChip.w / 2, L.statusChip.y + L.statusChip.h / 2, 0.5f,
                 L.statusChip.w * 0.9f);
        for (std::size_t i = 0; i < L.rows.size(); ++i) {
            const Rect &r = L.rows[i];
            drawValueUnit(rowValue_[i], rowUnit_[i], cardValue(r));
            const Rect lb = cardLabel(r);
            drawText(rowLabel_[i], lb.x + lb.w / 2, lb.y + lb.h / 2, 0.5f, lb.w * 0.92f);
        }
        if (!L.compact) {
            drawScaleLabels(L.graphScale);
            drawText(lTimeLeft_, L.timeAxis.x, L.timeAxis.y + L.timeAxis.h / 2, 0.0f);
            drawText(timeRight_, L.timeAxis.right(), L.timeAxis.y + L.timeAxis.h / 2, 1.0f);
            if (L.dynamics.h > 0)
                drawText(dynLabel_, L.dynamics.x + t * 3, L.dynamics.y + L.dynamics.h * 0.3f, 0.0f);
        }
    }

    // Números de la escala a la derecha de su columna; el objetivo, en naranja (y sin el número de la
    // escala que quede encima).
    void drawScaleLabels(const Rect &s) const
    {
        const float x = s.right() - s.w * 0.24f;
        for (std::size_t k = 0; k < lScale_.size(); ++k) {
            const double lufs = kLufsMin + lScaleFractions_[k] * (kLufsMax - kLufsMin);
            if (std::fabs(lufs - target_.target) < 3.0)
                continue;
            drawText(lScale_[k], x, s.bottom() - static_cast<float>(lScaleFractions_[k]) * s.h, 1.0f, s.w * 0.74f);
        }
        drawText(targetLabel_, x, yOfLufs(s, target_.target), 1.0f, s.w * 0.74f);
    }

    std::mutex cfgMu_;
    Config cfg_;
    bool cfgDirty_ = true;

    // Solo hilo de gráficos
    Config run_;
    uint32_t cx_ = 0, cy_ = 0;
    MonitorLayout layout_;
    LoudnessLayout lLayout_;
    std::shared_ptr<AudioScopeTap> tap_;
    AudioScopeTap::Snapshot snap_;
    std::array<MeterBallistics, 2> meters_;
    std::string shownSource_, shownScene_;
    int matches_ = 0;
    bool isSoundGrid_ = false;
    bool muted_ = false;
    bool noSignal_ = true;
    bool clipOn_ = false;
    bool rendered_ = false;
    int64_t nowNs_ = 0, spanNs_ = 5 * kNsPerSecM, lastPickNs_ = 0, lastTextNs_ = 0;
    LineBatch lines_;

    // Sonoridad: el medidor lo alimenta el hilo de audio (feed) y lo lee el de gráficos (tick).
    std::mutex meterMu_;
    loudness::LoudnessMeter meter_;
    ChannelPlan plan_{{0, 1}, {loudness::Role::Front, loudness::Role::Front}};
    std::atomic<bool> resetReq_{false};
    std::atomic<int64_t> lastFeedNs_{0};
    obs_source_t *capSrc_ = nullptr;
    bool masterOn_ = false;
    size_t masterMix_ = 0;
    uint32_t meterRate_ = 0;
    std::vector<int> planIndex_;
    loudness::Readings rd_;
    Target target_{"", -23, 1, -1, 0, false};
    loudness::TargetZone zone_ = loudness::TargetZone::Below;
    std::vector<HistoryColumn> cols_;
    std::vector<uint64_t> dist_;
    uint64_t lastSub_ = ~0ULL;
    bool colsDirty_ = true;
    bool tpAlertNow_ = false, tpAlertEver_ = false, blinkOn_ = false;

    TextLabel header_, clip_, timeLeft_, timeRight_;
    std::vector<TextLabel> scaleLabels_;
    std::vector<double> scaleFractions_;
    std::array<TextLabel, 2> hold_, chan_, readout_;
    TextLabel tpBadge_, barTitle_, barUnit_, bigLabel_, bigValue_, status_, lTimeLeft_, dynLabel_;
    std::vector<TextLabel> lScale_;
    std::vector<double> lScaleFractions_;
    std::array<TextLabel, kReadoutRows> rowLabel_, rowValue_, rowUnit_;
    TextLabel bigUnit_, presetPill_, chanPill_, targetLabel_;
};

// --------------------------------------------------------------- callbacks de libobs

const char *getName(void *)
{
    return T_("AudioMonitor");
}

void toggleViewOf(obs_source_t *src)
{
    obs_data_t *s = obs_source_get_settings(src);
    const int v = static_cast<int>(obs_data_get_int(s, kView));
    obs_data_set_int(s, kView, v == ViewLoudness ? ViewClassic : ViewLoudness);
    obs_source_update(src, s);
    obs_data_release(s);
}

void *create(obs_data_t *settings, obs_source_t *source)
{
    obs_data_erase(settings, kAutoScene); // ajuste global: su valor vive en settings.json, no en la fuente
    auto *m = new AudioMonitor(settings);
    obs_hotkey_register_source(
        source, "sg_audio_monitor.toggle_view", T_("AudioMonitorToggleView"),
        [](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
            if (pressed)
                toggleViewOf(static_cast<obs_source_t *>(data));
        },
        source);
    obs_hotkey_register_source(
        source, "sg_audio_monitor.reset_loudness", T_("LufsReset"),
        [](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
            if (pressed)
                static_cast<AudioMonitor *>(data)->requestReset();
        },
        m);
    return m;
}

void destroy(void *data)
{
    delete static_cast<AudioMonitor *>(data);
}

void update(void *data, obs_data_t *settings)
{
    static_cast<AudioMonitor *>(data)->update(settings);
}

void defaults(obs_data_t *s)
{
    obs_data_set_default_int(s, kView, ViewClassic);
    obs_data_set_default_int(s, kTheme, 0);
    obs_data_set_default_int(s, kTextScale, 100);
    obs_data_set_default_int(s, kFollow, FollowProgram);
    obs_data_set_default_string(s, kFixed, "");
    obs_data_set_default_int(s, kMasterTrack, 1);
    obs_data_set_default_int(s, kWindow, 5);
    obs_data_set_default_bool(s, kAutoScene, settings::getBool(settings::kAudioMonitorScene, true));
    obs_data_set_default_string(s, kPreset, "ebu_r128");
    obs_data_set_default_double(s, kTarget, -23.0);
    obs_data_set_default_double(s, kTolerance, 1.0);
    obs_data_set_default_double(s, kTpLimit, -1.0);
    obs_data_set_default_double(s, kLraMax, 0.0);
    obs_data_set_default_int(s, kUnits, 0);
    obs_data_set_default_int(s, kChannels, 0);
    obs_data_set_default_int(s, kRate, 10);
    obs_data_set_default_int(s, kHistory, 300);
    obs_data_set_default_int(s, kOversample, 4);
    obs_data_set_default_int(s, kLayout, 0);
    obs_data_set_default_bool(s, kDynamics, false);
    obs_data_set_default_bool(s, kResetOnSwitch, false);
}

void tick(void *data, float)
{
    static_cast<AudioMonitor *>(data)->tick();
}

void render(void *data, gs_effect_t *)
{
    static_cast<AudioMonitor *>(data)->render();
}

uint32_t width(void *data)
{
    return static_cast<AudioMonitor *>(data)->width();
}

uint32_t height(void *data)
{
    return static_cast<AudioMonitor *>(data)->height();
}

bool refreshVisibility(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
    const bool loud = obs_data_get_int(s, kView) == ViewLoudness;
    const int follow = static_cast<int>(obs_data_get_int(s, kFollow));
    obs_property_set_visible(obs_properties_get(props, kFixed), follow == FollowFixed);
    obs_property_set_visible(obs_properties_get(props, kMasterTrack), follow == FollowMaster);
    obs_property_set_visible(obs_properties_get(props, kWindow), !loud);
    obs_property_set_visible(obs_properties_get(props, kLoudnessGroup), loud);
    const bool custom = std::string(obs_data_get_string(s, kPreset)) == "custom";
    for (const char *k : {kTarget, kTolerance, kTpLimit, kLraMax})
        obs_property_set_visible(obs_properties_get(props, k), custom);
    return true;
}

bool onPlace(obs_properties_t *, obs_property_t *, void *)
{
    setupAudioMonitorScene();
    return false;
}

bool onReset(obs_properties_t *, obs_property_t *, void *data)
{
    if (data)
        static_cast<AudioMonitor *>(data)->requestReset();
    return false;
}

bool autoSceneChanged(obs_properties_t *, obs_property_t *, obs_data_t *s)
{
    settings::setBool(settings::kAudioMonitorScene, obs_data_get_bool(s, kAutoScene));
    return false;
}

obs_properties_t *properties(void *data)
{
    obs_properties_t *props = obs_properties_create();
    obs_property_t *v =
        obs_properties_add_list(props, kView, T_("AudioMonitorView"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(v, T_("AudioMonitorViewClassic"), ViewClassic);
    obs_property_list_add_int(v, T_("AudioMonitorViewLoudness"), ViewLoudness);
    obs_property_set_long_description(v, T_("AudioMonitorViewHelp"));
    obs_property_set_modified_callback(v, refreshVisibility);

    obs_property_t *f =
        obs_properties_add_list(props, kFollow, T_("AudioMonitorFollow"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(f, T_("AudioMonitorFollowProgram"), FollowProgram);
    obs_property_list_add_int(f, T_("AudioMonitorFollowPreview"), FollowPreview);
    obs_property_list_add_int(f, T_("AudioMonitorFollowFixed"), FollowFixed);
    obs_property_list_add_int(f, T_("AudioMonitorFollowMaster"), FollowMaster);
    obs_property_set_modified_callback(f, refreshVisibility);

    obs_property_t *fx =
        obs_properties_add_list(props, kFixed, T_("AudioMonitorFixedSource"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    // Primero las fuentes SoundGrid (las únicas que muestra la vista clásica) y luego el resto con audio.
    for (int pass = 0; pass < 2; ++pass) {
        struct Ctx {
            obs_property_t *list;
            int pass;
        } ctx{fx, pass};
        obs_enum_sources(
            [](void *p, obs_source_t *s) {
                auto *c = static_cast<Ctx *>(p);
                const char *id = obs_source_get_id(s);
                const bool sg = id && std::string(id) == kSourceId;
                const bool audio = (obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO) != 0;
                if ((c->pass == 0 && sg) || (c->pass == 1 && !sg && audio))
                    obs_property_list_add_string(c->list, obs_source_get_name(s), obs_source_get_name(s));
                return true;
            },
            &ctx);
    }
    obs_properties_add_int(props, kMasterTrack, T_("AudioMonitorMasterTrack"), 1, 6, 1);
    obs_properties_add_int_slider(props, kWindow, T_("AudioMonitorWindow"), 1, 30, 1);

    // Sonoridad (ITU-R BS.1770-4)
    obs_properties_t *lp = obs_properties_create();
    obs_property_t *pr = obs_properties_add_list(lp, kPreset, T_("LufsPreset"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    for (const auto &p : loudness::presets()) {
        const std::string id = p.id;
        std::string name = id == "custom" ? std::string(T_("LufsPresetCustom")) : std::string(p.name);
        if (id == "streaming")
            name = T_("LufsPresetStreaming");
        if (id != "custom")
            name += "  (" + loudness::formatLoudness(p.target) + (p.lkfs ? " LKFS, " : " LUFS, ") +
                    loudness::formatLoudness(p.truePeak) + " dBTP)";
        obs_property_list_add_string(pr, name.c_str(), p.id);
    }
    obs_property_set_long_description(pr, T_("LufsPresetHelp"));
    obs_property_set_modified_callback(pr, refreshVisibility);
    obs_properties_add_float_slider(lp, kTarget, T_("LufsTarget"), -40.0, -5.0, 0.5);
    obs_properties_add_float_slider(lp, kTolerance, T_("LufsTolerance"), 0.5, 5.0, 0.5);
    obs_properties_add_float_slider(lp, kTpLimit, T_("LufsTpLimit"), -6.0, 0.0, 0.5);
    obs_properties_add_float_slider(lp, kLraMax, T_("LufsLraMax"), 0.0, 30.0, 1.0);
    obs_property_t *ch = obs_properties_add_list(lp, kChannels, T_("LufsChannels"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(ch, T_("LufsChannelsAuto"), 0);
    obs_property_list_add_int(ch, T_("LufsChannelsMono"), 1);
    obs_property_list_add_int(ch, T_("LufsChannelsStereo"), 2);
    obs_property_list_add_int(ch, "5.1", 3);
    obs_property_t *un = obs_properties_add_list(lp, kUnits, T_("LufsUnits"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(un, T_("LufsUnitsAuto"), 0);
    obs_property_list_add_int(un, "LUFS", 1);
    obs_property_list_add_int(un, "LKFS", 2);
    obs_property_t *rt = obs_properties_add_list(lp, kRate, T_("LufsRate"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    for (int hz : {10, 20, 30, 60})
        obs_property_list_add_int(rt, (std::to_string(hz) + " Hz").c_str(), hz);
    obs_property_t *hs = obs_properties_add_list(lp, kHistory, T_("LufsHistory"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    for (int sec : {30, 60, 120, 300, 600, 1800, 3600})
        obs_property_list_add_int(hs, historyText(sec).substr(3).c_str(), sec);
    obs_property_t *os = obs_properties_add_list(lp, kOversample, T_("LufsOversample"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(os, "4x", 4);
    obs_property_list_add_int(os, "8x", 8);
    obs_property_t *ly = obs_properties_add_list(lp, kLayout, T_("LufsLayout"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(ly, T_("LufsLayoutFull"), 0);
    obs_property_list_add_int(ly, T_("LufsLayoutCompact"), 1);
    obs_properties_add_bool(lp, kDynamics, T_("LufsDynamics"));
    obs_properties_add_bool(lp, kResetOnSwitch, T_("LufsResetOnSwitch"));
    obs_properties_add_button2(lp, "lufs_reset", T_("LufsReset"), onReset, data);
    obs_properties_add_group(props, kLoudnessGroup, T_("LufsGroup"), OBS_GROUP_NORMAL, lp);

    // Aspecto (las dos vistas)
    obs_property_t *tm = obs_properties_add_list(props, kTheme, T_("AudioMonitorTheme"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(tm, T_("AudioMonitorThemeDark"), 0);
    obs_property_list_add_int(tm, T_("AudioMonitorThemeLight"), 1);
    obs_properties_add_int_slider(props, kTextScale, T_("AudioMonitorTextScale"), 50, 200, 5);

    obs_properties_add_button2(props, "place", T_("AudioMonitorPlace"), onPlace, nullptr);
    obs_property_t *autoScene = obs_properties_add_bool(props, kAutoScene, T_("AudioMonitorAutoScene"));
    obs_property_set_long_description(autoScene, T_("AudioMonitorAutoSceneHelp"));
    obs_property_set_modified_callback(autoScene, autoSceneChanged);
    obs_property_t *info = obs_properties_add_text(props, "info", T_("AudioMonitorInfoTitle"), OBS_TEXT_INFO);
    obs_property_set_long_description(info, T_("AudioMonitorInfo"));
    return props;
}

// --------------------------------------------------------------- avisos al operador

void showMessage(const std::string &title, const std::string &text)
{
    blog(LOG_INFO, "[soundgrid] %s: %s", title.c_str(), text.c_str());
#if defined(__APPLE__)
    CFStringRef t = CFStringCreateWithCString(nullptr, title.c_str(), kCFStringEncodingUTF8);
    CFStringRef m = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
    CFOptionFlags resp = 0;
    CFUserNotificationDisplayAlert(0, kCFUserNotificationNoteAlertLevel, nullptr, nullptr, nullptr, t, m, nullptr,
                                   nullptr, nullptr, &resp);
    CFRelease(t);
    CFRelease(m);
#elif defined(_WIN32)
    auto widen = [](const std::string &u) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
        return w;
    };
    MessageBoxW(static_cast<HWND>(obs_frontend_get_main_window_handle()), widen(text).c_str(),
                widen(title).c_str(), MB_OK | MB_ICONINFORMATION);
#endif
}

} // namespace

// --------------------------------------------------------------- API del módulo

void registerAudioMonitorSource()
{
    obs_source_info info{};
    info.id = kAudioMonitorSourceId;
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = getName;
    info.create = create;
    info.destroy = destroy;
    info.update = update;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.video_tick = tick;
    info.video_render = render;
    info.get_width = width;
    info.get_height = height;
    info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
    obs_register_source(&info);
}

namespace {

void flagAsMonitorScene(obs_source_t *sceneSrc)
{
    obs_data_t *ps = obs_source_get_private_settings(sceneSrc);
    obs_data_set_bool(ps, kMonitorFlag, true);
    obs_data_release(ps);
}

void addMonitorItem(obs_scene_t *scene)
{
    obs_source_t *mon = obs_source_create(kAudioMonitorSourceId, T_("AudioMonitor"), nullptr, nullptr);
    if (obs_sceneitem_t *item = obs_scene_add(scene, mon))
        obs_sceneitem_set_locked(item, true);
    obs_source_release(mon);
}

// Garantiza que la colección actual tenga la escena del monitor. Devuelve true si la creó (o adoptó).
// sceneName recibe el nombre de la escena del monitor.
bool ensureMonitorScene(std::string &sceneName)
{
    if (obs_source_t *mine = findMonitorScene()) {
        sceneName = obs_source_get_name(mine);
        obs_source_release(mine);
        return false;
    }
    sceneName = T_("AudioMonitorSceneName");
    // Una escena con ese nombre que contiene el monitor (p. ej. de una versión anterior): se adopta.
    if (obs_source_t *same = obs_get_source_by_name(sceneName.c_str())) {
        obs_scene_t *scene = obs_scene_from_source(same);
        const bool adopt = scene && sceneHasMonitorItem(scene);
        if (adopt)
            flagAsMonitorScene(same);
        obs_source_release(same);
        if (adopt) {
            blog(LOG_INFO, "[soundgrid] escena '%s' marcada como escena del monitor de audio", sceneName.c_str());
            return true;
        }
        // El nombre lo usa otra cosa del director: no se toca, se busca un nombre libre.
        for (int n = 2;; ++n) {
            std::string candidate = sceneName + " (" + std::to_string(n) + ")";
            obs_source_t *taken = obs_get_source_by_name(candidate.c_str());
            if (!taken) {
                sceneName = candidate;
                break;
            }
            obs_source_release(taken);
        }
    }
    obs_scene_t *scene = obs_scene_create(sceneName.c_str());
    flagAsMonitorScene(obs_scene_get_source(scene));
    addMonitorItem(scene);
    obs_scene_release(scene); // la lista de escenas de OBS conserva su referencia
    blog(LOG_INFO, "[soundgrid] escena '%s' creada con el monitor de audio", sceneName.c_str());
    return true;
}

// Texto con la posición recomendada en la lista de escenas según el diseño de Multiview actual.
std::string placementAdvice(const std::string &sceneName)
{
    std::vector<SceneEntry> scenes;
    obs_frontend_source_list list = {};
    obs_frontend_get_scenes(&list);
    for (size_t i = 0; i < list.sources.num; ++i) {
        obs_source_t *s = list.sources.array[i];
        obs_data_t *ps = obs_source_get_private_settings(s);
        obs_data_set_default_bool(ps, "show_in_multiview", true);
        scenes.push_back({obs_source_get_name(s), obs_data_get_bool(ps, "show_in_multiview"), isMonitorScene(s)});
        obs_data_release(ps);
    }
    obs_frontend_source_list_free(&list);
    // La escena recién creada aún puede no estar en la lista del frontend: va al final.
    if (std::none_of(scenes.begin(), scenes.end(), [](const SceneEntry &e) { return e.isMonitor; }))
        scenes.push_back({sceneName, true, true});

    config_t *cfg = obs_frontend_get_user_config();
    const int layout = cfg ? static_cast<int>(config_get_int(cfg, "BasicWindow", "MultiviewLayout")) : 0;
    return describePlan(planMonitorPlacement(scenes, layout), layout, sceneName);
}

// Al abrir OBS o cambiar de colección: crea la escena del monitor si falta (ajuste global, activo por
// defecto), sin ventanas: el director no tiene que hacer nada para verlo en el Multiview.
void autoCreateMonitorScene()
{
    if (!settings::getBool(settings::kAudioMonitorScene, true))
        return;
    std::string sceneName;
    if (ensureMonitorScene(sceneName))
        blog(LOG_INFO, "[soundgrid] monitor de audio en el Multiview listo: %s", placementAdvice(sceneName).c_str());
}

} // namespace

// Todas las fuentes del monitor (Herramientas > cambiar vista / reiniciar sonoridad).
void forEachMonitor(void (*fn)(obs_source_t *))
{
    struct Ctx {
        void (*fn)(obs_source_t *);
    } ctx{fn};
    obs_enum_sources(
        [](void *p, obs_source_t *s) {
            const char *id = obs_source_get_id(s);
            if (id && std::string(id) == kAudioMonitorSourceId)
                static_cast<Ctx *>(p)->fn(s);
            return true;
        },
        &ctx);
}

void toggleAudioMonitorView()
{
    forEachMonitor([](obs_source_t *s) {
        toggleViewOf(s);
        obs_data_t *d = obs_source_get_settings(s);
        blog(LOG_INFO, "[soundgrid] monitor de audio '%s': vista %s", obs_source_get_name(s),
             obs_data_get_int(d, kView) == ViewLoudness ? "de sonoridad" : "clásica");
        obs_data_release(d);
    });
}

void resetAudioMonitorLoudness()
{
    forEachMonitor([](obs_source_t *s) {
        if (auto *m = static_cast<AudioMonitor *>(obs_obj_get_data(s)))
            m->requestReset();
    });
    blog(LOG_INFO, "[soundgrid] monitor de audio: medición de sonoridad reiniciada");
}

void setupAudioMonitorScene()
{
    std::string sceneName;
    const bool created = ensureMonitorScene(sceneName);
    std::string msg = std::string(created ? T_("AudioMonitorCreated") : T_("AudioMonitorExists")) + "\n\n" +
                      placementAdvice(sceneName) + "\n\n" + T_("AudioMonitorSafety");
    showMessage(T_("AudioMonitorMenu"), msg);
}

void audioMonitorFrontendEvent(enum obs_frontend_event event)
{
    switch (event) {
    case OBS_FRONTEND_EVENT_SCENE_CHANGED: {
        obs_source_t *prog = obs_frontend_get_current_scene();
        guardScene(prog, g_program, false);
        obs_source_release(prog);
        break;
    }
    case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED: {
        obs_source_t *prev = obs_frontend_get_current_preview_scene();
        guardScene(prev, g_preview, true);
        obs_source_release(prev);
        break;
    }
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
        autoCreateMonitorScene();
        refreshFollowed();
        break;
    case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
        refreshFollowed();
        break;
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
    case OBS_FRONTEND_EVENT_EXIT:
        stopAudioMonitor();
        break;
    default:
        break;
    }
}

void stopAudioMonitor()
{
    storeWeak(g_program, nullptr);
    storeWeak(g_preview, nullptr);
}

} // namespace sg
