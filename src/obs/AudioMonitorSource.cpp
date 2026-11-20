// Monitor de audio para el Multiview (ver AudioMonitorSource.h y docs/ADR/ADR-016-monitor-multiview.md).
//
// Hilos:
//  - Interfaz de OBS: create/update/destroy/propiedades y eventos del frontend (escena al aire).
//  - Hilo de gráficos de OBS: video_tick (niveles, textos) y video_render (dibujo). El audio llega por
//    AudioScopeTap, que la fuente SoundGrid alimenta desde su hilo de salida.
#include "AudioMonitorSource.h"

#include "ObsSoundGridSource.h"
#include "monitor/AudioScope.h"
#include "monitor/SceneAudioPicker.h"

#include <graphics/graphics.h>
#include <graphics/vec3.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <algorithm>
#include <array>
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
enum Follow { FollowProgram = 0, FollowPreview = 1, FollowFixed = 2 };

// Colores 0xAARRGGBB. Medidor: valores por defecto de VolumeMeter de OBS. Marco: Multiview de OBS.
constexpr uint32_t kBgNominal = 0xFF267F26, kBgWarning = 0xFF7F7F26, kBgError = 0xFF7F2626;
constexpr uint32_t kFgNominal = 0xFF4CFF4C, kFgWarning = 0xFFFFFF4C, kFgError = 0xFFFF4C4C;
constexpr uint32_t kClipWhite = 0xFFFFFFFF, kMagnitude = 0xFF000000;
constexpr uint32_t kCanvas = 0xFF0B0B0B, kPanel = 0xFF161616, kHeader = 0xFF262626, kLine = 0xFF3A3A3A;
constexpr uint32_t kGrid = 0xFF262626, kTickMajor = 0xFFBBBBBB, kTickMinor = 0xFF5A5A5A;
constexpr uint32_t kProgramRed = 0xFFD00000, kMutedAmber = 0xFF7F6A00, kClipOff = 0xFF2B2B2B;
constexpr uint32_t kWave = 0xFF4CFF4C, kWaveClip = 0xFFFF4C4C;
constexpr uint32_t kTextWhite = 0xFFFFFF, kTextGray = 0x8A8A8A;

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

bool isMonitorScene(obs_source_t *sceneSrc)
{
    if (!sceneSrc)
        return false;
    obs_scene_t *scene = obs_scene_from_source(sceneSrc);
    if (!scene)
        return false;
    obs_data_t *ps = obs_source_get_private_settings(sceneSrc);
    const bool flagged = obs_data_get_bool(ps, kMonitorFlag);
    obs_data_release(ps);
    return flagged || sceneHasMonitorItem(scene);
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
    uint32_t rgb = kTextWhite;
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
    // Solo blanco y grises: el orden de bytes (RGB/BGR) de cada fuente de texto no importa.
    obs_data_set_int(s, "color1", 0xFF000000 | l.rgb);
    obs_data_set_int(s, "color2", 0xFF000000 | l.rgb);
    obs_data_set_int(s, "color", l.rgb);
    obs_data_set_int(s, "opacity", 100);
    return s;
}

void createText(TextLabel &l, const char *text, bool bold, bool mono)
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

// --------------------------------------------------------------- la fuente

struct Config {
    int follow = FollowProgram;
    std::string fixed;
    int windowSec = 5;
};

class AudioMonitor {
public:
    explicit AudioMonitor(obs_data_t *settings)
    {
        update(settings);
        createText(header_, "", true, false);
        createText(clip_, "CLIP", true, false);
        for (const auto &t : scaleTicks())
            if (t.major) {
                scaleLabels_.emplace_back();
                createText(scaleLabels_.back(), t.label.c_str(), false, false);
                scaleFractions_.push_back(t.fraction);
            }
        for (int i = 0; i < 2; ++i) {
            createText(hold_[i], "", false, true);
            createText(chan_[i], i ? "R" : "L", true, false);
            createText(readout_[i], "", false, true);
        }
        createText(timeLeft_, "", false, false);
        createText(timeRight_, "0 s", false, false);
    }

    ~AudioMonitor()
    {
        for (TextLabel *l : allLabels())
            obs_source_release(l->src);
        if (vb_) {
            obs_enter_graphics();
            gs_vertexbuffer_destroy(vb_);
            obs_leave_graphics();
        }
    }

    void update(obs_data_t *s)
    {
        Config c;
        c.follow = static_cast<int>(obs_data_get_int(s, kFollow));
        c.fixed = obs_data_get_string(s, kFixed);
        c.windowSec = std::clamp(static_cast<int>(obs_data_get_int(s, kWindow)), 1, 30);
        std::lock_guard lk(cfgMu_);
        cfg_ = c;
        cfgDirty_ = true;
    }

    uint32_t width() const { return cx_; }
    uint32_t height() const { return cy_; }

    void tick()
    {
        const int64_t now = static_cast<int64_t>(os_gettime_ns());
        obs_video_info ovi{};
        if (obs_get_video_info(&ovi) && (ovi.base_width != cx_ || ovi.base_height != cy_)) {
            cx_ = ovi.base_width;
            cy_ = ovi.base_height;
            layout_ = computeLayout(static_cast<float>(cx_), static_cast<float>(cy_));
            restyle();
        }
        bool dirty;
        {
            std::lock_guard lk(cfgMu_);
            dirty = cfgDirty_;
            cfgDirty_ = false;
            run_ = cfg_;
        }
        if (dirty || now - lastPickNs_ >= 250 * kNsPerMsM) {
            pick();
            lastPickNs_ = now;
        }
        spanNs_ = static_cast<int64_t>(run_.windowSec) * kNsPerSecM;
        if (tap_) {
            snap_ = tap_->snapshot(now - spanNs_ - 200 * kNsPerMsM);
        } else {
            snap_ = AudioScopeTap::Snapshot{};
        }
        nowNs_ = now;
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
        setText(header_, headerText());
        clipOn_ = meters_[0].clipActive(now) || meters_[1].clipActive(now);
        setStyle(clip_, clip_.size, clipOn_ ? kTextWhite : kTextGray);
    }

    void render()
    {
        if (!cx_ || !cy_)
            return;
        if (!rendered_) {
            rendered_ = true;
            blog(LOG_DEBUG, "[soundgrid] monitor de audio: primer fotograma %ux%u", cx_, cy_);
        }
        const MonitorLayout &L = layout_;
        gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
        Painter p{gs_effect_get_param_by_name(solid, "color")};
        gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
        gs_technique_begin(tech);
        gs_technique_begin_pass(tech, 0);

        const float t = L.border;
        p.rect(L.canvas, kCanvas);
        // Cabecera: rojo (como el programa del Multiview) si no llega audio; ámbar si OBS la silencia.
        p.rect(L.header, noSignal_ ? kProgramRed : muted_ ? kMutedAmber : kHeader);
        p.rect(L.clipBadge, clipOn_ ? kProgramRed : kClipOff);
        p.frame(L.clipBadge, t, clipOn_ ? kClipWhite : kLine);
        p.rect(L.meterPanel, kPanel);
        p.rect(L.scopePanel, kPanel);

        drawMeters(p);
        drawScope(p);

        gs_technique_end_pass(tech);
        gs_technique_end(tech);

        // Textos (cada uno con su propio efecto).
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

private:
    static std::string pad(const std::string &s)
    {
        // Ancho fijo con fuente monoespaciada (el signo y el ∞ cuentan como un carácter).
        std::size_t glyphs = 0;
        for (unsigned char c : s)
            glyphs += (c & 0xC0) != 0x80;
        return glyphs < 6 ? std::string(6 - glyphs, ' ') + s : s;
    }

    std::vector<TextLabel *> allLabels()
    {
        std::vector<TextLabel *> v{&header_, &clip_, &timeLeft_, &timeRight_};
        for (auto &l : scaleLabels_)
            v.push_back(&l);
        for (int i = 0; i < 2; ++i) {
            v.push_back(&hold_[i]);
            v.push_back(&chan_[i]);
            v.push_back(&readout_[i]);
        }
        return v;
    }

    void restyle()
    {
        const auto big = static_cast<int>(std::lround(layout_.fontPx));
        const auto small = static_cast<int>(std::lround(layout_.smallFontPx));
        setStyle(header_, big, kTextWhite);
        setStyle(clip_, static_cast<int>(big * 0.9), clipOn_ ? kTextWhite : kTextGray);
        for (auto &l : scaleLabels_)
            setStyle(l, static_cast<int>(small * 0.8), kTextWhite);
        for (int i = 0; i < 2; ++i) {
            setStyle(hold_[i], static_cast<int>(small * 0.85), kTextWhite);
            setStyle(chan_[i], small, kTextWhite);
            setStyle(readout_[i], small, kTextWhite);
        }
        setStyle(timeLeft_, static_cast<int>(small * 0.8), kTextGray);
        setStyle(timeRight_, static_cast<int>(small * 0.8), kTextGray);
    }

    std::string headerText() const
    {
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
        if (snap_.inputChannels == 1)
            s += "  \xC2\xB7  mono";
        if (noSignal_)
            s += "  \xC2\xB7  " + std::string(T_("AudioMonitorNoSignal"));
        else if (muted_)
            s += "  \xC2\xB7  " + std::string(T_("AudioMonitorMuted"));
        return s;
    }

    void pick()
    {
        obs_source_t *src = nullptr;
        std::string scene;
        int matches = 0;
        if (run_.follow == FollowFixed) {
            if (!run_.fixed.empty())
                src = obs_get_source_by_name(run_.fixed.c_str());
            matches = src ? 1 : 0;
        } else {
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
        const std::string shown = tap_ ? obs_source_get_name(src) : "";
        if (shown != shownSource_ || scene != shownScene_)
            blog(LOG_INFO, "[soundgrid] monitor de audio: %s%s%s", shown.empty() ? "sin fuente SoundGrid" : ("muestra '" + shown + "'").c_str(),
                 scene.empty() ? "" : " · escena ", scene.c_str());
        shownSource_ = shown;
        shownScene_ = scene;
        matches_ = matches;
        muted_ = src && obs_source_muted(src);
        obs_source_release(src);
    }

    void drawMeters(const Painter &p)
    {
        const MonitorLayout &L = layout_;
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
                p.frame({m.x - 2 * t, m.y - 2 * t, m.w + 4 * t, m.h + 4 * t}, 2 * t, kProgramRed);
        }
        // Escala: marcas a ambos lados (junto a cada barra) cada 1 dB, mayores cada 5 dB.
        const Rect &s = L.scale;
        const float th = std::max(1.0f, t * 0.6f);
        for (const auto &k : scaleTicks()) {
            const float y = s.bottom() - static_cast<float>(k.fraction) * s.h - th / 2;
            const float len = s.w * (k.major ? 0.16f : 0.08f);
            const uint32_t c = k.major ? kTickMajor : kTickMinor;
            p.rect(s.x, y, len, th, c);
            p.rect(s.right() - len, y, len, th, c);
        }
    }

    void ensureVertexBuffer(std::size_t verts)
    {
        if (vb_ && vbCap_ >= verts)
            return;
        if (vb_)
            gs_vertexbuffer_destroy(vb_);
        gs_vb_data *d = gs_vbdata_create();
        d->num = verts;
        d->points = static_cast<vec3 *>(bmalloc(sizeof(vec3) * verts));
        std::memset(d->points, 0, sizeof(vec3) * verts);
        vb_ = gs_vertexbuffer_create(d, GS_DYNAMIC);
        vbCap_ = vb_ ? verts : 0;
    }

    void drawScope(const Painter &p)
    {
        const MonitorLayout &L = layout_;
        const float t = L.border;
        const auto cols = static_cast<uint32_t>(std::max(1.0f, std::floor(L.lanes[0].w)));
        const auto wave = waveformColumns(snap_.buckets, nowNs_, spanNs_, cols);
        for (const Rect &lane : L.lanes) {
            p.rect(lane, 0xFF101010);
            const float half = lane.h / 2;
            const float cy = lane.y + half;
            for (int s = 1; s < run_.windowSec; ++s) // rejilla de 1 s, anclada al borde derecho (ahora)
                p.rect(lane.right() - lane.w * s / run_.windowSec, lane.y, std::max(1.0f, t * 0.5f), lane.h, kGrid);
            p.rect(lane.x, cy - half * 0.5f, lane.w, std::max(1.0f, t * 0.5f), kGrid); // −6 dBFS
            p.rect(lane.x, cy + half * 0.5f, lane.w, std::max(1.0f, t * 0.5f), kGrid);
            p.rect(lane.x, cy - std::max(1.0f, t * 0.5f) / 2, lane.w, std::max(1.0f, t * 0.5f), kLine);
            p.frame(lane, std::max(1.0f, t * 0.5f), kLine);
        }

        // Forma de onda: una línea vertical mín→máx por columna; las columnas con clip, en rojo.
        ensureVertexBuffer(static_cast<std::size_t>(cols) * 2 * 2);
        if (!vb_)
            return;
        vec3 *pts = gs_vertexbuffer_get_data(vb_)->points;
        std::size_t nGood = 0, nClip = 0;
        const std::size_t cap = vbCap_;
        const float clipLin = static_cast<float>(std::pow(10.0, kClipDb / 20.0));
        // Primero las normales desde el principio y las de clip desde el final.
        for (int ch = 0; ch < 2; ++ch) {
            const Rect &lane = L.lanes[ch];
            const float half = lane.h / 2 - t;
            const float cy = lane.y + lane.h / 2;
            for (uint32_t x = 0; x < cols; ++x) {
                const Column &c = wave[x][ch];
                if (!c.has)
                    continue;
                float y0 = cy - std::clamp(c.max, -1.0f, 1.0f) * half;
                float y1 = cy - std::clamp(c.min, -1.0f, 1.0f) * half;
                if (y1 - y0 < 1.0f) { // silencio: se ve la línea, el monitor está vivo
                    y0 -= 0.5f;
                    y1 += 0.5f;
                }
                const float px = lane.x + static_cast<float>(x) + 0.5f;
                const bool clipped = std::max(std::fabs(c.max), std::fabs(c.min)) >= clipLin;
                vec3 *dst = clipped ? &pts[cap - 2 * (++nClip)] : &pts[2 * nGood++];
                vec3_set(&dst[0], px, y0, 0.0f);
                vec3_set(&dst[1], px, y1, 0.0f);
            }
        }
        if (!nGood && !nClip)
            return;
        gs_vertexbuffer_flush(vb_);
        gs_load_vertexbuffer(vb_);
        gs_load_indexbuffer(nullptr);
        gs_effect_set_color(p.color, kWave);
        if (nGood)
            gs_draw(GS_LINES, 0, static_cast<uint32_t>(2 * nGood));
        if (nClip) {
            gs_effect_set_color(p.color, kWaveClip);
            gs_draw(GS_LINES, static_cast<uint32_t>(cap - 2 * nClip), static_cast<uint32_t>(2 * nClip));
        }
    }

    std::mutex cfgMu_;
    Config cfg_;
    bool cfgDirty_ = true;

    // Solo hilo de gráficos
    Config run_;
    uint32_t cx_ = 0, cy_ = 0;
    MonitorLayout layout_;
    std::shared_ptr<AudioScopeTap> tap_;
    AudioScopeTap::Snapshot snap_;
    std::array<MeterBallistics, 2> meters_;
    std::string shownSource_, shownScene_;
    int matches_ = 0;
    bool muted_ = false;
    bool noSignal_ = true;
    bool clipOn_ = false;
    bool rendered_ = false;
    int64_t nowNs_ = 0, spanNs_ = 5 * kNsPerSecM, lastPickNs_ = 0, lastTextNs_ = 0;
    gs_vertbuffer_t *vb_ = nullptr;
    std::size_t vbCap_ = 0;

    TextLabel header_, clip_, timeLeft_, timeRight_;
    std::vector<TextLabel> scaleLabels_;
    std::vector<double> scaleFractions_;
    std::array<TextLabel, 2> hold_, chan_, readout_;
};

// --------------------------------------------------------------- callbacks de libobs

const char *getName(void *)
{
    return T_("AudioMonitor");
}

void *create(obs_data_t *settings, obs_source_t *)
{
    return new AudioMonitor(settings);
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
    obs_data_set_default_int(s, kFollow, FollowProgram);
    obs_data_set_default_string(s, kFixed, "");
    obs_data_set_default_int(s, kWindow, 5);
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

bool followChanged(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
    obs_property_set_visible(obs_properties_get(props, kFixed), obs_data_get_int(s, kFollow) == FollowFixed);
    return true;
}

bool onPlace(obs_properties_t *, obs_property_t *, void *)
{
    setupAudioMonitorScene();
    return false;
}

obs_properties_t *properties(void *)
{
    obs_properties_t *props = obs_properties_create();
    obs_property_t *f =
        obs_properties_add_list(props, kFollow, T_("AudioMonitorFollow"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(f, T_("AudioMonitorFollowProgram"), FollowProgram);
    obs_property_list_add_int(f, T_("AudioMonitorFollowPreview"), FollowPreview);
    obs_property_list_add_int(f, T_("AudioMonitorFollowFixed"), FollowFixed);
    obs_property_set_modified_callback(f, followChanged);

    obs_property_t *fx =
        obs_properties_add_list(props, kFixed, T_("AudioMonitorFixedSource"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_enum_sources(
        [](void *p, obs_source_t *s) {
            const char *id = obs_source_get_id(s);
            if (id && std::string(id) == kSourceId)
                obs_property_list_add_string(static_cast<obs_property_t *>(p), obs_source_get_name(s),
                                             obs_source_get_name(s));
            return true;
        },
        fx);
    obs_properties_add_int_slider(props, kWindow, T_("AudioMonitorWindow"), 1, 30, 1);
    obs_properties_add_button2(props, "place", T_("AudioMonitorPlace"), onPlace, nullptr);
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

void setupAudioMonitorScene()
{
    const std::string sceneName = T_("AudioMonitorSceneName");
    obs_source_t *existing = obs_get_source_by_name(sceneName.c_str());
    bool created = false;
    if (!existing) {
        obs_scene_t *scene = obs_scene_create(sceneName.c_str());
        obs_source_t *sceneSrc = obs_scene_get_source(scene);
        obs_data_t *ps = obs_source_get_private_settings(sceneSrc);
        obs_data_set_bool(ps, kMonitorFlag, true);
        obs_data_release(ps);
        obs_source_t *mon = obs_source_create(kAudioMonitorSourceId, T_("AudioMonitor"), nullptr, nullptr);
        if (obs_sceneitem_t *item = obs_scene_add(scene, mon))
            obs_sceneitem_set_locked(item, true);
        obs_source_release(mon);
        obs_scene_release(scene); // la lista de escenas de OBS conserva su referencia
        created = true;
        blog(LOG_INFO, "[soundgrid] escena '%s' creada con el monitor de audio", sceneName.c_str());
    } else {
        obs_source_release(existing);
    }

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
    const MultiviewPlan plan = planMonitorPlacement(scenes, layout);
    std::string msg = std::string(created ? T_("AudioMonitorCreated") : T_("AudioMonitorExists")) + "\n\n" +
                      describePlan(plan, layout, sceneName) + "\n\n" + T_("AudioMonitorSafety");
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
