#include "Diagnostics.h"

#include "observability/AlertSink.h"

#include <cstdio>
#include <sstream>

namespace sg {

namespace {
std::string obsStr(const Observed<double> &o)
{
    if (!o.known())
        return "NO DISPONIBLE";
    char b[64];
    std::snprintf(b, sizeof(b), "%.2f ms (%s)", *o.value, confidenceName(o.confidence));
    return b;
}
} // namespace

std::string formatStatusText(const SessionStatus &st)
{
    std::ostringstream os;
    const auto &i = st.indicators;
    os << "Estado: " << globalStatusText(st.global) << "\n";
    if (!st.configError.empty())
        os << "Configuración: " << st.configError << "\n";
    os << "Tipo de dispositivo: " << i.deviceKind << "\n";
    os << "Red (NIC): " << i.nic << "\n";
    os << "Driver: " << i.driver << "\n";
    os << "Audio: " << i.audio << "\n";
    os << "Patch LV1: " << i.patch << "\n";
    os << "Consola LV1: " << i.lv1 << "\n";
    os << "Latencia: " << i.latency << "\n";
    const auto &s = st.last;
    if (s.sampleRate)
        os << "Formato: " << s.sampleRate << " Hz · " << s.outChannels << " canal(es) hacia OBS\n";
    os << "Contadores: desbordes " << s.overflows << " · discontinuidades " << s.discontinuities << " · xruns driver "
       << (s.driverOverloadsObservable ? std::to_string(s.driverOverloads) : std::string("NO OBSERVABLE"))
       << " · reconexiones " << s.reconnectAttempts << "\n";
    if (st.openIncidents.empty()) {
        os << "Alertas activas: ninguna";
    } else {
        os << "Alertas activas:";
        for (const auto &e : st.openIncidents) {
            os << "\n • [" << severityName(e.severity) << "] " << e.message << " (" << alertStateName(e.state)
               << (e.secondary ? ", consecuencia" : "") << ")\n   Observado: " << e.evidence.observed
               << "\n   Umbral: " << e.evidence.threshold << "\n   Fuente de la medida: " << e.evidence.measurementSource;
            if (!e.evidence.action.empty())
                os << "\n   Acción: " << e.evidence.action;
        }
    }
    return os.str();
}

std::string diagnosticsJson(const std::string &sourceName, const SourceConfig &cfg, const SessionStatus &st,
                            const std::string &obsVersion, const std::string &pluginVersion)
{
    const auto &s = st.last;
    std::ostringstream os;
    os << "{\n  \"generated_at\": \"" << wallClockIso8601Utc() << "\",\n  \"plugin_version\": \""
       << jsonEscape(pluginVersion) << "\",\n  \"obs_version\": \"" << jsonEscape(obsVersion)
       << "\",\n  \"platform\": \"" << platformName(currentPlatform()) << "\",\n  \"hardware_validated\": false,\n"
       << "  \"source\": \"" << jsonEscape(sourceName) << "\",\n  \"device\": {\"backend\": \"" << jsonEscape(s.backend)
       << "\", \"name\": \"" << jsonEscape(s.deviceName) << "\", \"looks_like_waves_soundgrid\": "
       << (s.deviceLooksSoundGrid ? "true" : "false") << ", \"state\": \"" << transportStateName(s.transportState)
       << "\", \"last_error\": \"" << errorCodeName(s.lastError) << "\", \"sample_rate\": " << s.sampleRate << "},\n"
       << "  \"channel_map\": \"" << jsonEscape(cfg.map.describe()) << "\",\n"
       << "  \"nic\": {\"monitored\": " << (s.nicSelected ? "true" : "false") << ", \"alias\": \""
       << jsonEscape(s.nicName) << "\", \"link\": \""
       << (s.nic.linkUp ? (*s.nic.linkUp ? "UP" : "DOWN") : "UNKNOWN") << "\", \"speed_mbps\": "
       << (s.nic.speedBps ? std::to_string(*s.nic.speedBps / 1000000) : std::string("null")) << "},\n"
       << "  \"latency\": {\"plugin\": \"" << obsStr(s.pluginLatencyMs) << "\", \"queue\": \""
       << obsStr(s.queueDelayMs) << "\", \"driver\": \"" << obsStr(s.driverLatencyMs)
       << "\", \"end_to_end\": \"NO DISPONIBLE\"},\n"
       << "  \"counters\": {\"frames\": " << s.framesWritten << ", \"overflows\": " << s.overflows
       << ", \"discontinuities\": " << s.discontinuities << ", \"driver_overloads\": "
       << (s.driverOverloadsObservable ? std::to_string(s.driverOverloads) : std::string("null"))
       << ", \"reconnect_attempts\": " << s.reconnectAttempts << ", \"blocks_delivered\": " << st.blocksDelivered
       << "},\n  \"global_status\": \"" << jsonEscape(globalStatusText(st.global)) << "\",\n  \"open_incidents\": [";
    bool first = true;
    for (const auto &e : st.openIncidents) {
        os << (first ? "\n    " : ",\n    ") << alertEventToJson(e);
        first = false;
    }
    os << (first ? "]" : "\n  ]") << ",\n  \"limitations\": [\"La detección de NIC no identifica una consola LV1\","
       << " \"Nombres de canal = puertos del driver, no nombres LV1\", \"Latencia extremo a extremo no medida\"]\n}\n";
    return os.str();
}

} // namespace sg
