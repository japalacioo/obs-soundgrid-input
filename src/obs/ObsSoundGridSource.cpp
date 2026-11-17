// Fuente de audio "SoundGrid Input" para OBS (libobs, API C). Sin Qt: el panel de estado y
// alertas se muestra en las propiedades de la fuente y se abre desde Herramientas.
#include "ObsSoundGridSource.h"

#include "Diagnostics.h"
#include "ObsSettingsStore.h"
#include "platform/INetworkTelemetry.h"
#include "platform/WavesDriverPrefs.h"

#include <obs-frontend-api.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#define T_(s) obs_module_text(s)

namespace sg {

namespace {

std::mutex g_rtMu;
std::shared_ptr<CoreRuntime> g_rt;

constexpr const char *kDevice = "device";
constexpr const char *kNic = "nic";
constexpr const char *kMode = "mode";
constexpr const char *kCount = "ch_count";
constexpr const char *kArmed = "armed";
constexpr const char *kProfile = "alerts.profile";
constexpr const char *kProfileApplied = "alerts.profile_applied";
constexpr const char *kA05Mbps = "alerts.A05.threshold_mbps";
constexpr const char *kMockSignal = "mock_signal";
constexpr const char *kStatus = "status_text";
constexpr const char *kSummary = "summary_text";
constexpr const char *kBanner = "banner_enabled";
constexpr const char *kShowAdvanced = "alerts.show_advanced";
constexpr const char *kAutoDone = "autodetect_done";
constexpr const char *kNone = "";

std::string chKey(int i)
{
    return "ch_" + std::to_string(i);
}

enum Mode { ModeMono = 0, ModeStereo = 1, ModeMulti = 2 };

speaker_layout layoutFor(uint32_t ch)
{
    switch (ch) {
    case 1: return SPEAKERS_MONO;
    case 2: return SPEAKERS_STEREO;
    case 3: return SPEAKERS_2POINT1;
    case 4: return SPEAKERS_4POINT0;
    case 5: return SPEAKERS_4POINT1;
    case 6: return SPEAKERS_5POINT1;
    default: return SPEAKERS_7POINT1; // 7 se completa con un canal en silencio
    }
}

struct SgSource {
    obs_source_t *source = nullptr;
    std::shared_ptr<CoreRuntime> rt;
    std::shared_ptr<SourceSession> session;
    std::vector<float> zeros = std::vector<float>(Subscription::kMaxFramesPerSlot, 0.0f);
    bool streamFault = false; // botón de simulación (solo dispositivo simulado)
    std::atomic<bool> bannerEnabled{true};
};

// Fuentes vivas, para el aviso de audio perdido de la ventana principal.
std::mutex g_srcMu;
std::vector<SgSource *> g_sources;

// Preselecciona el primer driver Waves SoundGrid y la NIC que usa su Control Panel.
// force=false solo rellena lo que esté vacío. Devuelve una descripción de lo detectado.
std::string autodetect(CoreRuntime &rt, obs_data_t *s, bool force)
{
    std::string found;
    const std::string dev = obs_data_get_string(s, kDevice);
    if (force || dev.empty()) {
        for (const auto &d : rt.transports().enumerateAll()) {
            if (d.looksLikeWavesSoundGrid && d.backend != "mock") {
                obs_data_set_string(s, kDevice, d.id.c_str());
                found += "driver '" + d.name + "'";
                break;
            }
        }
    }
    const std::string nic = obs_data_get_string(s, kNic);
    if (force || nic.empty()) {
        if (const auto mac = wavesDriverPreferredMac()) {
            for (const auto &n : rt.nic().listInterfaces()) {
                if (!n.mac.empty() && n.mac == *mac) {
                    obs_data_set_string(s, kNic, n.id.c_str());
                    found += std::string(found.empty() ? "" : " y ") + "NIC '" + n.displayName + "'";
                    break;
                }
            }
        }
    }
    return found;
}

SourceConfig configFromSettings(obs_data_t *s)
{
    SourceConfig c;
    c.deviceId = obs_data_get_string(s, kDevice);
    c.nicId = obs_data_get_string(s, kNic);
    c.armed = obs_data_get_bool(s, kArmed);
    if (auto rt = pluginRuntime(); !c.nicId.empty() && rt)
        for (const auto &n : rt->nic().listInterfaces())
            if (n.id == c.nicId)
                c.nicName = n.displayName;

    const int mode = static_cast<int>(obs_data_get_int(s, kMode));
    const int count = std::clamp(static_cast<int>(obs_data_get_int(s, kCount)), 1, 8);
    auto ch = [&](int i) { return static_cast<int16_t>(obs_data_get_int(s, chKey(i).c_str())); };
    if (mode == ModeStereo) {
        c.map = ChannelMap::stereo(ch(1), ch(2));
    } else {
        ChannelMap m;
        m.mode = mode == ModeMono ? ChannelMode::Mono : ChannelMode::Multi;
        m.count = static_cast<uint8_t>(count);
        m.inputs.fill(ChannelMap::kUnused);
        for (int i = 0; i < count; ++i)
            m.inputs[static_cast<std::size_t>(i)] = ch(i + 1);
        c.map = m;
    }
    ObsSettingsStore store(s);
    c.alerts = loadAlertConfig(store);
    c.alerts.rule(RuleId::A05_SlowLink).threshold = obs_data_get_double(s, kA05Mbps) * 1e6;
    c.alerts.expectedLinkBps = static_cast<uint64_t>(c.alerts.rule(RuleId::A05_SlowLink).threshold);
    return c;
}

MockTransport *mockOf(SgSource *src)
{
    auto hub = src->session ? src->session->hub() : nullptr;
    return hub ? dynamic_cast<MockTransport *>(&hub->transport()) : nullptr;
}

void applyMockSignal(SgSource *src, obs_data_t *s)
{
    if (MockTransport *m = mockOf(src)) {
        const std::string sig = obs_data_get_string(s, kMockSignal);
        MockSignal ms = MockSignal::Sine;
        double level = -20.0;
        if (sig == "pink")
            ms = MockSignal::PinkNoise;
        else if (sig == "silence")
            ms = MockSignal::Silence;
        else if (sig == "impulse")
            ms = MockSignal::Impulse;
        else if (sig == "clipped")
            ms = MockSignal::ClippedSine, level = 0.0;
        m->setSignal(ms, level);
    }
}

void refreshStatusText(SgSource *src, obs_data_t *settings)
{
    if (!src->session)
        return;
    const SessionStatus st = src->session->status();
    obs_data_set_default_string(settings, kStatus, formatStatusText(st).c_str());
    obs_data_set_default_string(settings, kSummary, formatStatusSummary(st).c_str());
}

// ------------------------------------------------------------------ callbacks de libobs

const char *getName(void *)
{
    return T_("SoundGridInput");
}

void outputBlock(obs_source_t *source, const std::vector<float> &zeros, const PcmBlock &b)
{
    obs_source_audio a{};
    const uint32_t ch = std::min<uint32_t>(b.channels, 8);
    for (uint32_t c = 0; c < ch; ++c)
        a.data[c] = reinterpret_cast<const uint8_t *>(b.planes[c]);
    if (ch == 7) // OBS no tiene disposición de 7 canales
        a.data[7] = reinterpret_cast<const uint8_t *>(zeros.data());
    a.frames = b.frames;
    a.speakers = layoutFor(ch);
    a.format = AUDIO_FORMAT_FLOAT_PLANAR;
    a.samples_per_sec = b.sampleRate; // OBS remuestrea a la frecuencia del proyecto
    a.timestamp = b.timestampNs;
    obs_source_output_audio(source, &a);
}

void *create(obs_data_t *settings, obs_source_t *source)
{
    auto *src = new SgSource;
    src->source = source;
    src->rt = pluginRuntime();
    if (!src->rt) {
        blog(LOG_ERROR, "[soundgrid] runtime no inicializado");
        return src;
    }
    // Los textos informativos del panel se guardan como valores por defecto: así no se escriben en
    // la escena ni hacen que OBS pregunte por "cambios sin guardar". Se borran los de versiones previas.
    for (const char *k : {kStatus, kSummary, "setup_info", "ch_info", "alerts_info", "credits"})
        obs_data_erase(settings, k);
    // Fuente nueva: preselecciona el driver Waves y la NIC del Control Panel (una sola vez, para
    // no deshacer una elección posterior del operador).
    if (!obs_data_get_bool(settings, kAutoDone)) {
        const std::string found = autodetect(*src->rt, settings, false);
        obs_data_set_bool(settings, kAutoDone, true);
        if (!found.empty())
            blog(LOG_INFO, "[soundgrid] configuración automática: %s", found.c_str());
    }
    src->bannerEnabled = obs_data_get_bool(settings, kBanner);
    const std::vector<float> *zeros = &src->zeros;
    src->session = std::make_shared<SourceSession>(*src->rt, obs_source_get_name(source),
                                                   [source, zeros](const PcmBlock &b) { outputBlock(source, *zeros, b); });
    src->rt->monitor().add(src->session);
    src->session->apply(configFromSettings(settings));
    applyMockSignal(src, settings);
    {
        std::lock_guard lk(g_srcMu);
        g_sources.push_back(src);
    }
    blog(LOG_INFO, "[soundgrid] fuente '%s' creada (dispositivo '%s')", obs_source_get_name(source),
         obs_data_get_string(settings, kDevice));
    return src;
}

void destroy(void *data)
{
    auto *src = static_cast<SgSource *>(data);
    {
        std::lock_guard lk(g_srcMu);
        g_sources.erase(std::remove(g_sources.begin(), g_sources.end(), src), g_sources.end());
    }
    if (src->session && src->rt)
        src->rt->monitor().remove(src->session.get());
    // Cierre síncrono: el hilo de salida (que usa 'source' y 'zeros') termina AQUÍ, aunque el
    // hilo del monitor conserve todavía una referencia a la sesión durante un tick.
    if (src->session)
        src->session->shutdown();
    src->session.reset();
    delete src;
}

void update(void *data, obs_data_t *settings)
{
    auto *src = static_cast<SgSource *>(data);
    src->bannerEnabled = obs_data_get_bool(settings, kBanner);
    if (!src->session)
        return;
    src->session->rename(obs_source_get_name(src->source));
    src->session->apply(configFromSettings(settings));
    applyMockSignal(src, settings);
}

void getDefaults(obs_data_t *s)
{
    obs_data_set_default_string(s, kDevice, kNone);
    obs_data_set_default_string(s, kNic, kNone);
    obs_data_set_default_int(s, kMode, ModeStereo);
    obs_data_set_default_int(s, kCount, 2);
    for (int i = 1; i <= 8; ++i)
        obs_data_set_default_int(s, chKey(i).c_str(), i - 1);
    obs_data_set_default_bool(s, kArmed, false); // A01 desactivada hasta que el operador la arme
    obs_data_set_default_bool(s, kBanner, true);  // aviso rojo en OBS al perder el audio
    obs_data_set_default_bool(s, kShowAdvanced, false);
    obs_data_set_default_string(s, kMockSignal, "sine");
    ObsSettingsStore defs(s, /*defaults=*/true);
    const AlertConfig d = AlertConfig::defaults(AlertProfile::Transmision);
    saveAlertConfig(d, defs);
    obs_data_set_default_string(s, kProfileApplied, profileKey(AlertProfile::Transmision));
    obs_data_set_default_double(s, kA05Mbps, d.rule(RuleId::A05_SlowLink).threshold / 1e6);
}

// --- propiedades -------------------------------------------------------------

uint32_t deviceChannelCount(SgSource *src, const std::string &deviceId, std::vector<std::string> &names)
{
    names.clear();
    if (src->session && src->session->config().deviceId == deviceId) {
        for (const auto &c : src->session->deviceChannels())
            names.push_back(c.name);
        if (!names.empty())
            return static_cast<uint32_t>(names.size());
    }
    if (src->rt)
        for (const auto &d : src->rt->transports().enumerate(TransportManager::backendOf(deviceId)))
            if (d.id == deviceId && d.channelsKnown)
                return d.inputChannels;
    return deviceId.empty() ? 0 : 128; // desconocido sin abrir el driver: hasta 128 (SoundGrid V17)
}

bool modeChanged(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
    const int mode = static_cast<int>(obs_data_get_int(s, kMode));
    const int count = mode == ModeStereo ? 2 : std::clamp(static_cast<int>(obs_data_get_int(s, kCount)), 1, 8);
    obs_property_set_visible(obs_properties_get(props, kCount), mode != ModeStereo);
    for (int i = 1; i <= 8; ++i) {
        obs_property_t *p = obs_properties_get(props, chKey(i).c_str());
        obs_property_set_visible(p, i <= count);
        std::string label;
        if (mode == ModeStereo)
            label = i == 1 ? T_("ChannelLeft") : T_("ChannelRight");
        else if (mode == ModeMono)
            label = std::string(T_("ChannelMonoInput")) + " " + std::to_string(i);
        else
            label = std::string(T_("ChannelOutput")) + " " + std::to_string(i);
        obs_property_set_description(p, label.c_str());
    }
    return true;
}

// Rellena las 8 listas de canal con las entradas del dispositivo indicado. Si el driver aún no
// se ha abierto, los nombres no se conocen y solo se numeran (nunca se reutilizan los de otro).
void fillChannelLists(SgSource *src, obs_properties_t *props, const std::string &deviceId)
{
    std::vector<std::string> names;
    const uint32_t nch = src ? deviceChannelCount(src, deviceId, names) : 0;
    for (int i = 1; i <= 8; ++i) {
        obs_property_t *c = obs_properties_get(props, chKey(i).c_str());
        if (!c)
            continue;
        obs_property_list_clear(c);
        for (uint32_t k = 0; k < std::max<uint32_t>(nch, 1); ++k) {
            std::string label = std::to_string(k + 1);
            if (k < names.size() && !names[k].empty())
                label += " · " + names[k] + " (" + T_("TagDriverPort") + ")";
            obs_property_list_add_int(c, label.c_str(), k);
        }
    }
}

bool deviceChanged(void *priv, obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
    const std::string dev = obs_data_get_string(s, kDevice);
    obs_property_set_visible(obs_properties_get(props, kMockSignal), dev.rfind("mock:", 0) == 0);
    obs_property_set_visible(obs_properties_get(props, "simulate_fault"), dev.rfind("mock:", 0) == 0);
    fillChannelLists(static_cast<SgSource *>(priv), props, dev);
    return true;
}

bool profileChanged(obs_properties_t *, obs_property_t *, obs_data_t *s)
{
    // Solo al CAMBIAR de perfil se cargan sus valores (no al abrir las propiedades).
    const std::string prof = obs_data_get_string(s, kProfile);
    if (prof == obs_data_get_string(s, kProfileApplied))
        return false;
    const auto p = profileFromKey(prof).value_or(AlertProfile::Transmision);
    const AlertConfig d = AlertConfig::defaults(p);
    ObsSettingsStore store(s);
    saveAlertConfig(d, store);
    obs_data_set_double(s, kA05Mbps, d.rule(RuleId::A05_SlowLink).threshold / 1e6);
    obs_data_set_string(s, kProfileApplied, prof.c_str());
    return true;
}

bool onRefresh(obs_properties_t *, obs_property_t *, void *data)
{
    auto *src = static_cast<SgSource *>(data);
    obs_data_t *s = obs_source_get_settings(src->source);
    refreshStatusText(src, s);
    obs_data_release(s);
    return true;
}

bool onReconnect(obs_properties_t *props, obs_property_t *p, void *data)
{
    auto *src = static_cast<SgSource *>(data);
    if (src->session) {
        blog(LOG_INFO, "[soundgrid] '%s': reconexión manual del receptor (la consola no se modifica)",
             obs_source_get_name(src->source));
        src->session->reconnect();
    }
    return onRefresh(props, p, data);
}

bool onAckAll(obs_properties_t *props, obs_property_t *p, void *data)
{
    auto *src = static_cast<SgSource *>(data);
    if (src->session && src->rt)
        for (const auto &e : src->session->status().openIncidents)
            if (auto ack = src->session->acknowledge(e.id))
                src->rt->dispatcher().publish(*ack);
    return onRefresh(props, p, data);
}

bool onMute(obs_properties_t *props, obs_property_t *p, void *data)
{
    auto *src = static_cast<SgSource *>(data);
    if (src->session) {
        src->session->muteNotifications(10 * 60 * 1000);
        blog(LOG_INFO, "[soundgrid] '%s': notificaciones silenciadas 10 min (la detección sigue activa)",
             obs_source_get_name(src->source));
    }
    return onRefresh(props, p, data);
}

bool onExport(obs_properties_t *props, obs_property_t *p, void *data)
{
    auto *src = static_cast<SgSource *>(data);
    if (!src->session)
        return false;
    char *path = obs_module_config_path("diagnostics.json");
    if (char *dir = obs_module_config_path("")) {
        os_mkdirs(dir);
        bfree(dir);
    }
    if (path) {
        std::ofstream f(path, std::ios::binary);
        f << diagnosticsJson(obs_source_get_name(src->source), src->session->config(), src->session->status(),
                             obs_get_version_string(), SG_PLUGIN_VERSION);
        blog(LOG_INFO, "[soundgrid] diagnóstico exportado a %s", path);
        bfree(path);
    }
    return onRefresh(props, p, data);
}

bool onAutodetect(obs_properties_t *, obs_property_t *, void *data)
{
    auto *src = static_cast<SgSource *>(data);
    if (!src->rt || !src->source)
        return false;
    obs_data_t *s = obs_source_get_settings(src->source);
    obs_data_t *changes = obs_data_create();
    obs_data_set_string(changes, kDevice, obs_data_get_string(s, kDevice));
    obs_data_set_string(changes, kNic, obs_data_get_string(s, kNic));
    const std::string found = autodetect(*src->rt, changes, true);
    blog(LOG_INFO, "[soundgrid] '%s': detección automática: %s", obs_source_get_name(src->source),
         found.empty() ? "no se encontró driver Waves ni NIC del Control Panel" : found.c_str());
    obs_source_update(src->source, changes); // aplica y guarda; el panel se reconstruye al volver
    obs_data_release(changes);
    refreshStatusText(src, s);
    obs_data_release(s);
    return true;
}

// Reglas con umbral o interruptor propio: solo visibles con "Mostrar ajustes avanzados".
const RuleId kAdvancedRules[] = {RuleId::A01_Silence, RuleId::A02_StreamInterrupted, RuleId::A03_DriverLost,
                                 RuleId::A04_NoLink,  RuleId::A05_SlowLink,          RuleId::A06_NicErrors,
                                 RuleId::A07_Latency, RuleId::A08_Dropouts,          RuleId::A09_Clipping,
                                 RuleId::A10_Recovered};

std::string thresholdKey(RuleId r)
{
    return r == RuleId::A05_SlowLink ? std::string(kA05Mbps) : "alerts." + std::string(ruleCode(r)) + ".threshold";
}

bool advancedChanged(obs_properties_t *props, obs_property_t *, obs_data_t *s)
{
    const bool show = obs_data_get_bool(s, kShowAdvanced);
    for (RuleId r : kAdvancedRules) {
        const std::string code = ruleCode(r);
        obs_property_set_visible(obs_properties_get(props, ("alerts." + code + ".enabled").c_str()), show);
        if (obs_property_t *t = obs_properties_get(props, thresholdKey(r).c_str()))
            obs_property_set_visible(t, show);
    }
    obs_property_set_visible(obs_properties_get(props, kProfile), show);
    return true;
}

bool onSimulateFault(obs_properties_t *props, obs_property_t *p, void *data)
{
    auto *src = static_cast<SgSource *>(data);
    if (MockTransport *m = mockOf(src)) {
        src->streamFault = !src->streamFault;
        m->setFault(src->streamFault ? MockFault::Stall : MockFault::None);
        blog(LOG_INFO, "[soundgrid] simulador: fallo de flujo %s", src->streamFault ? "ACTIVADO" : "desactivado");
    }
    return onRefresh(props, p, data);
}

obs_properties_t *getProperties(void *data)
{
    auto *src = static_cast<SgSource *>(data);
    obs_properties_t *props = obs_properties_create();
    obs_data_t *settings = src && src->source ? obs_source_get_settings(src->source) : nullptr;
    const std::string currentDev = settings ? obs_data_get_string(settings, kDevice) : "";
    if (src && settings)
        refreshStatusText(src, settings);
    bool bad = false;
    if (src && src->session) {
        const auto ss = src->session->status();
        bad = !ss.openIncidents.empty() || !ss.configError.empty();
    }

    // --- Resumen en una línea (lo primero que ve el operador) ---
    obs_property_t *summary = obs_properties_add_text(props, kSummary, T_("SummaryLabel"), OBS_TEXT_INFO);
    obs_property_text_set_info_type(summary, bad ? OBS_TEXT_INFO_ERROR : OBS_TEXT_INFO_NORMAL);

    // --- 1 · Conexión ---
    obs_properties_t *cn = obs_properties_create();
    obs_property_t *dev = obs_properties_add_list(cn, kDevice, T_("Device"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(dev, T_("DeviceNone"), kNone);
    bool currentListed = currentDev.empty();
    if (src && src->rt) {
        auto devices = src->rt->transports().enumerateAll();
        // Primero los drivers Waves SoundGrid, después otros drivers y al final el simulador.
        auto rank = [](const DeviceInfo &d) { return d.backend == "mock" ? 2 : d.looksLikeWavesSoundGrid ? 0 : 1; };
        std::stable_sort(devices.begin(), devices.end(),
                         [&](const DeviceInfo &a, const DeviceInfo &b) { return rank(a) < rank(b); });
        for (const auto &d : devices) {
            std::string label = d.name;
            if (d.backend == "mock")
                label += std::string(" — ") + T_("TagSimulator");
            else if (d.looksLikeWavesSoundGrid)
                label += std::string(" — ") + T_("TagWaves");
            label += " [" + d.backend + "]";
            obs_property_list_add_string(dev, label.c_str(), d.id.c_str());
            currentListed |= d.id == currentDev;
        }
    }
    if (!currentListed) {
        const size_t idx = obs_property_list_add_string(dev, (currentDev + " — " + T_("TagMissing")).c_str(),
                                                        currentDev.c_str());
        obs_property_list_item_disable(dev, idx, true);
    }
    obs_property_set_modified_callback2(dev, deviceChanged, src);
    obs_property_set_long_description(dev, T_("DeviceHelp"));
    obs_properties_add_button2(cn, "autodetect", T_("AutoDetect"), onAutodetect, src);

    obs_property_t *nic = obs_properties_add_list(cn, kNic, T_("Nic"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(nic, T_("NicNone"), kNone);
    if (src && src->rt) {
        auto list = src->rt->nic().listInterfaces();
        std::stable_sort(list.begin(), list.end(), [](const NicInfo &a, const NicInfo &b) { return a.isPhysical > b.isPhysical; });
        const auto wavesMac = wavesDriverPreferredMac();
        for (const auto &n : list) {
            const NicSample smp = src->rt->sampleNic(n.id);
            std::string label = n.displayName + " · " + n.description;
            if (smp.linkUp)
                label += *smp.linkUp ? " · UP" : " · DOWN";
            if (smp.speedBps)
                label += " " + std::to_string(*smp.speedBps / 1000000) + " Mbps";
            if (!n.isPhysical)
                label += std::string(" · ") + T_("TagVirtual");
            if (wavesMac && !n.mac.empty() && n.mac == *wavesMac)
                label += std::string(" · ") + T_("TagWavesNic");
            obs_property_list_add_string(nic, label.c_str(), n.id.c_str());
        }
    }
    obs_property_set_long_description(nic, T_("NicHelp"));
    obs_property_t *mock = obs_properties_add_list(cn, kMockSignal, T_("MockSignal"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(mock, T_("MockSine"), "sine");
    obs_property_list_add_string(mock, T_("MockPink"), "pink");
    obs_property_list_add_string(mock, T_("MockSilence"), "silence");
    obs_property_list_add_string(mock, T_("MockImpulse"), "impulse");
    obs_property_list_add_string(mock, T_("MockClipped"), "clipped");
    obs_property_set_visible(mock, currentDev.rfind("mock:", 0) == 0);
    obs_property_t *setup = obs_properties_add_text(cn, "setup_info", T_("SetupInfoTitle"), OBS_TEXT_INFO);
    obs_property_set_long_description(setup, T_("SetupInfo"));
    if (settings)
        obs_data_set_default_string(settings, "setup_info", T_("SetupInfo"));
    obs_properties_add_group(props, "grp_conn", T_("GroupConnection"), OBS_GROUP_NORMAL, cn);

    // --- 2 · Canales ---
    obs_properties_t *chp = obs_properties_create();
    obs_property_t *mode = obs_properties_add_list(chp, kMode, T_("Mode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mode, T_("ModeStereo"), ModeStereo);
    obs_property_list_add_int(mode, T_("ModeMono"), ModeMono);
    obs_property_list_add_int(mode, T_("ModeMulti"), ModeMulti);
    obs_property_set_modified_callback(mode, modeChanged);
    obs_property_t *count = obs_properties_add_int(chp, kCount, T_("ChannelCount"), 1, 8, 1);
    obs_property_set_modified_callback(count, modeChanged);
    for (int i = 1; i <= 8; ++i)
        obs_properties_add_list(chp, chKey(i).c_str(), "", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    fillChannelLists(src, chp, currentDev);
    obs_properties_add_text(chp, "ch_info", T_("ChannelInfoTitle"), OBS_TEXT_INFO);
    if (settings)
        obs_data_set_default_string(settings, "ch_info", T_("ChannelInfo"));
    obs_properties_add_group(props, "grp_channels", T_("GroupChannels"), OBS_GROUP_NORMAL, chp);

    // --- 3 · Avisos ---
    obs_properties_t *al = obs_properties_create();
    obs_properties_add_bool(al, kBanner, T_("BannerEnabled"));
    obs_properties_add_bool(al, kArmed, T_("Armed"));
    obs_property_t *adv = obs_properties_add_bool(al, kShowAdvanced, T_("ShowAdvanced"));
    obs_property_set_modified_callback(adv, advancedChanged);
    const bool showAdv = settings && obs_data_get_bool(settings, kShowAdvanced);
    obs_property_t *prof = obs_properties_add_list(al, kProfile, T_("Profile"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(prof, T_("ProfileEnsayo"), "ensayo");
    obs_property_list_add_string(prof, T_("ProfileTransmision"), "transmision");
    obs_property_list_add_string(prof, T_("ProfileGrabacion"), "grabacion");
    obs_property_set_modified_callback(prof, profileChanged);
    obs_property_set_visible(prof, showAdv);
    struct Row {
        RuleId r;
        const char *unitKey;
        double min, max, step;
    };
    const Row rows[] = {{RuleId::A01_Silence, "UnitDbfs", -96, 0, 1},  {RuleId::A02_StreamInterrupted, "UnitMs", 200, 10000, 100},
                        {RuleId::A03_DriverLost, nullptr, 0, 0, 0},   {RuleId::A04_NoLink, nullptr, 0, 0, 0},
                        {RuleId::A05_SlowLink, "UnitMbps", 10, 10000, 10}, {RuleId::A06_NicErrors, "UnitErrors", 1, 100000, 1},
                        {RuleId::A07_Latency, "UnitMs", 5, 2000, 5},  {RuleId::A08_Dropouts, "UnitPerMin", 0, 1000, 1},
                        {RuleId::A09_Clipping, "UnitWindows", 1, 8, 1}, {RuleId::A10_Recovered, nullptr, 0, 0, 0}};
    for (const auto &row : rows) {
        const std::string code = ruleCode(row.r);
        obs_property_t *en =
            obs_properties_add_bool(al, ("alerts." + code + ".enabled").c_str(), (code + " · " + ruleTitle(row.r)).c_str());
        obs_property_set_visible(en, showAdv);
        if (row.unitKey) {
            obs_property_t *th = obs_properties_add_float(
                al, thresholdKey(row.r).c_str(), ("    " + code + " · " + T_("Threshold") + " (" + T_(row.unitKey) + ")").c_str(),
                row.min, row.max, row.step);
            obs_property_set_visible(th, showAdv);
        }
    }
    obs_properties_add_text(al, "alerts_info", T_("AlertsInfoTitle"), OBS_TEXT_INFO);
    if (settings)
        obs_data_set_default_string(settings, "alerts_info", T_("AlertsInfo"));
    obs_properties_add_group(props, "grp_alerts", T_("GroupAlerts"), OBS_GROUP_NORMAL, al);

    // --- Estado y diagnóstico (SoundGrid Monitor) ---
    obs_properties_t *st = obs_properties_create();
    obs_property_t *status = obs_properties_add_text(st, kStatus, T_("StatusLabel"), OBS_TEXT_INFO);
    obs_property_text_set_info_type(status, bad ? OBS_TEXT_INFO_WARNING : OBS_TEXT_INFO_NORMAL);
    obs_properties_add_button2(st, "refresh", T_("Refresh"), onRefresh, src);
    obs_properties_add_button2(st, "reconnect", T_("Reconnect"), onReconnect, src);
    obs_properties_add_button2(st, "ack", T_("AcknowledgeAll"), onAckAll, src);
    obs_properties_add_button2(st, "mute", T_("MuteNotifications"), onMute, src);
    obs_properties_add_button2(st, "export", T_("ExportDiagnostics"), onExport, src);
    obs_properties_add_button2(st, "simulate_fault", T_("SimulateFault"), onSimulateFault, src);
    obs_property_set_visible(obs_properties_get(st, "simulate_fault"), currentDev.rfind("mock:", 0) == 0);
    obs_properties_add_group(props, "grp_status", T_("GroupStatus"), OBS_GROUP_NORMAL, st);

    obs_properties_add_text(props, "credits", T_("CreditsTitle"), OBS_TEXT_INFO);
    if (settings)
        obs_data_set_default_string(settings, "credits",
                            (std::string("OBS SoundGrid Input ") + SG_PLUGIN_VERSION + " · " + T_("CreditsBy") + " " +
                             SG_PLUGIN_AUTHOR + " · GPLv3")
                                .c_str());

    if (settings)
        obs_data_release(settings);
    return props;
}

} // namespace

std::shared_ptr<CoreRuntime> pluginRuntime()
{
    std::lock_guard lk(g_rtMu);
    return g_rt;
}

void setPluginRuntime(std::shared_ptr<CoreRuntime> rt)
{
    std::lock_guard lk(g_rtMu);
    g_rt = std::move(rt);
}

void registerSoundGridSource()
{
    obs_source_info info{};
    info.id = kSourceId;
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = getName;
    info.create = create;
    info.destroy = destroy;
    info.update = update;
    info.get_defaults = getDefaults;
    info.get_properties = getProperties;
    info.icon_type = OBS_ICON_TYPE_AUDIO_INPUT;
    obs_register_source(&info);
}

void openSoundGridMonitor()
{
    obs_source_t *found = nullptr;
    obs_enum_sources(
        [](void *p, obs_source_t *s) {
            const char *id = obs_source_get_id(s);
            if (id && std::string(id) == kSourceId) {
                *static_cast<obs_source_t **>(p) = obs_source_get_ref(s);
                return false;
            }
            return true;
        },
        &found);
    if (!found) {
        blog(LOG_WARNING, "[soundgrid] SoundGrid Monitor: no hay ninguna fuente 'SoundGrid Input'. Añádela en Fuentes.");
        return;
    }
    obs_frontend_open_source_properties(found);
    obs_source_release(found);
}

std::vector<BannerSourceInput> collectBannerInputs()
{
    std::vector<BannerSourceInput> out;
    std::lock_guard lk(g_srcMu);
    out.reserve(g_sources.size());
    for (SgSource *src : g_sources) {
        if (!src->session)
            continue;
        // Solo cuenta lo que llega a la salida: una fuente eliminada o que no está en ninguna escena
        // visible (ni como audio global) no produce un aviso, aunque OBS aún no la haya destruido.
        if (!src->source || obs_source_removed(src->source) || !obs_source_showing(src->source))
            continue;
        const SessionStatus st = src->session->status();
        BannerSourceInput in;
        in.sourceName = src->session->name();
        in.bannerEnabled = src->bannerEnabled.load();
        in.notificationsMuted = st.notificationsMuted;
        in.openIncidents = st.openIncidents;
        out.push_back(std::move(in));
    }
    return out;
}

void openSourcePropertiesByName(const std::string &name)
{
    if (name.empty())
        return openSoundGridMonitor();
    obs_source_t *s = obs_get_source_by_name(name.c_str());
    if (!s)
        return openSoundGridMonitor();
    obs_frontend_open_source_properties(s);
    obs_source_release(s);
}

} // namespace sg
