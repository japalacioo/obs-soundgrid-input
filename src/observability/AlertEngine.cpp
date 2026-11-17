#include "AlertEngine.h"

#include "../common/Clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sg {

// ------------------------------------------------------------------ nombres

const char *ruleCode(RuleId r)
{
    static const char *codes[] = {"A01", "A02", "A03", "A04", "A05", "A06", "A07", "A08", "A09", "A10"};
    const auto i = static_cast<std::size_t>(r);
    return i < kRuleCount ? codes[i] : "A??";
}

const char *ruleTitle(RuleId r)
{
    switch (r) {
    case RuleId::A01_Silence: return "Sin sonido / silencio prolongado";
    case RuleId::A02_StreamInterrupted: return "Flujo de audio interrumpido";
    case RuleId::A03_DriverLost: return "Driver/dispositivo desconectado";
    case RuleId::A04_NoLink: return "Sin conexión de red";
    case RuleId::A05_SlowLink: return "Red lenta / enlace degradado";
    case RuleId::A06_NicErrors: return "Errores y pérdidas de red";
    case RuleId::A07_Latency: return "Latencia elevada";
    case RuleId::A08_Dropouts: return "Buffer en riesgo / dropouts";
    case RuleId::A09_Clipping: return "Señal saturada (clipping)";
    case RuleId::A10_Recovered: return "Conexión recuperada";
    default: return "?";
    }
}

const char *alertStateName(AlertState s)
{
    switch (s) {
    case AlertState::Active: return "ACTIVE";
    case AlertState::Acknowledged: return "ACKNOWLEDGED";
    case AlertState::Recovering: return "RECOVERING";
    case AlertState::Resolved: return "RESOLVED";
    }
    return "?";
}

const char *ruleEvalStateName(RuleEvalState s)
{
    switch (s) {
    case RuleEvalState::Disabled: return "DISABLED";
    case RuleEvalState::Initializing: return "INITIALIZING";
    case RuleEvalState::Healthy: return "HEALTHY";
    case RuleEvalState::Pending: return "PENDING";
    case RuleEvalState::Active: return "ACTIVE";
    case RuleEvalState::Acknowledged: return "ACKNOWLEDGED";
    case RuleEvalState::Recovering: return "RECOVERING";
    case RuleEvalState::Unknown: return "UNKNOWN";
    }
    return "?";
}

const char *alertProfileName(AlertProfile p)
{
    switch (p) {
    case AlertProfile::Ensayo: return "Ensayo";
    case AlertProfile::Transmision: return "Transmisión";
    case AlertProfile::Grabacion: return "Grabación";
    }
    return "?";
}

const char *globalStatusText(GlobalStatus s)
{
    switch (s) {
    case GlobalStatus::AudioOk: return "Audio OK";
    case GlobalStatus::SilenceDetected: return "Silencio detectado";
    case GlobalStatus::AudioInterrupted: return "Audio interrumpido";
    case GlobalStatus::DriverUnavailable: return "Driver no disponible";
    case GlobalStatus::NoLink: return "Sin enlace";
    case GlobalStatus::DegradedLink: return "Enlace degradado";
    case GlobalStatus::NetworkErrors: return "Errores de red";
    case GlobalStatus::HighLatency: return "Latencia elevada";
    case GlobalStatus::Dropouts: return "Cortes de audio";
    case GlobalStatus::Clipping: return "Saturación";
    case GlobalStatus::DataUnavailable: return "Datos no disponibles";
    case GlobalStatus::Initializing: return "Iniciando";
    }
    return "?";
}

// ------------------------------------------------------------------ configuración

AlertConfig AlertConfig::defaults(AlertProfile p)
{
    AlertConfig c;
    c.profile = p;
    auto set = [&](RuleId r, bool en, double th, int64_t win, int64_t trailing, Severity sev) {
        RuleConfig &rc = c.rule(r);
        rc.enabled = en;
        rc.threshold = th;
        rc.windowMs = win;
        rc.trailingMs = trailing;
        rc.severity = sev;
        rc.clearMs = 5000;
        rc.cooldownMs = 30000;
    };
    // Valores iniciales del plan §4A.1 / SDD-09 (configurables, por validar en laboratorio).
    set(RuleId::A01_Silence, true, -60.0, 10000, 0, Severity::Warn);       // solo si la fuente está ARMED
    set(RuleId::A02_StreamInterrupted, true, 1000.0, 0, 0, Severity::Critical); // ms sin frames
    set(RuleId::A03_DriverLost, true, 0.0, 500, 0, Severity::Critical);
    set(RuleId::A04_NoLink, true, 0.0, 1000, 0, Severity::Critical);
    set(RuleId::A05_SlowLink, true, 1e9, 5000, 0, Severity::Warn);         // bps esperados
    set(RuleId::A06_NicErrors, true, 10.0, 0, 5000, Severity::Warn);       // errores en ventana
    set(RuleId::A07_Latency, true, 80.0, 5000, 0, Severity::Warn);         // ms (tramo medido)
    set(RuleId::A08_Dropouts, true, 3.0, 0, 60000, Severity::Critical);    // > N por minuto
    set(RuleId::A09_Clipping, true, 3.0, 0, 2000, Severity::Warn);         // ventanas en 2 s
    set(RuleId::A10_Recovered, true, 0.0, 0, 0, Severity::Info);
    c.notifyMinSeverity = Severity::Warn;

    switch (p) {
    case AlertProfile::Ensayo:
        // Avisos discretos: solo se notifican los fallos críticos.
        c.notifyMinSeverity = Severity::Critical;
        break;
    case AlertProfile::Transmision:
        break;
    case AlertProfile::Grabacion:
        // Priorizar cortes: cualquier discontinuidad en 60 s es crítica.
        c.rule(RuleId::A08_Dropouts).threshold = 0.0;
        break;
    }
    return c;
}

// ------------------------------------------------------------------ utilidades

namespace {

std::string fmt(const char *f, double v)
{
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

double maxRmsDb(const MeterWindow &m)
{
    double best = 0;
    for (uint32_t c = 0; c < m.channels && c < 8; ++c)
        best = std::max(best, m.rms[c]);
    return linearToDbfs(best);
}

double maxPeakDb(const MeterWindow &m)
{
    double best = 0;
    for (uint32_t c = 0; c < m.channels && c < 8; ++c)
        best = std::max(best, static_cast<double>(m.peak[c]));
    return linearToDbfs(best);
}

const char *nicSourceName(const HealthSnapshot &s)
{
    switch (s.platform) {
    case Platform::Windows: return "NIC del SO (Windows)";
    case Platform::MacOS: return "NIC del SO (macOS)";
    default: return "NIC del SO";
    }
}

} // namespace

AlertEngine::AlertEngine(AlertConfig cfg) : cfg_(std::move(cfg)) {}

void AlertEngine::setConfig(const AlertConfig &cfg)
{
    cfg_ = cfg;
}

void AlertEngine::restart(int64_t nowNs)
{
    graceUntilNs_ = nowNs + cfg_.graceMs * kNsPerMs;
    lastProgressNs_ = nowNs;
    started_ = true;
    for (auto &r : rt_) {
        if (r.incident.id == 0)
            r.state = RuleEvalState::Initializing;
        r.badSinceNs = -1;
        // Un incidente en recuperación reinicia su ventana de histéresis (no se resuelve de golpe).
        r.goodSinceNs = r.state == RuleEvalState::Recovering ? nowNs : -1;
    }
    nicErrHist_.clear();
    dropoutHist_.clear();
    clipTicks_.clear();
    global_ = GlobalStatus::Initializing;
}

bool AlertEngine::isOpen(RuleId r) const
{
    return rt(r).incident.id != 0;
}

// ------------------------------------------------------------------ evaluación de reglas

AlertEngine::Eval AlertEngine::evalRule(RuleId r, const HealthSnapshot &s)
{
    const RuleConfig &rc = cfg_.rule(r);
    Eval e;
    if (!rc.enabled) {
        e.cond = Cond::NotApplicable;
        return e;
    }
    const int64_t now = s.nowNs;
    switch (r) {
    case RuleId::A01_Silence: {
        if (!s.armed || !s.sourceRunning) {
            e.cond = Cond::NotApplicable; // A01 solo para fuentes marcadas como ARMED
            break;
        }
        // Sin frames no se puede juzgar el silencio (eso es A02/A03/A04).
        if (!framesAdvancedThisTick_ || s.meter.samples == 0 || isOpen(RuleId::A02_StreamInterrupted) ||
            isOpen(RuleId::A03_DriverLost) || isOpen(RuleId::A04_NoLink)) {
            e.cond = Cond::Unknown;
            e.ev.observed = "NO DISPONIBLE (sin PCM)";
            break;
        }
        const double db = maxRmsDb(s.meter);
        e.ev.observed = db <= -199 ? "-inf dBFS RMS" : fmt("%.1f dBFS RMS", db);
        e.ev.threshold = fmt("< %.0f dBFS", rc.threshold) + " durante " + std::to_string(rc.windowMs / 1000) + " s";
        e.ev.measurementSource = "PCM (medidor RMS)";
        e.ev.confidence = Confidence::Measured;
        e.ev.cause = "Frames llegan con regularidad pero el nivel está por debajo del umbral";
        e.ev.action = "Revisa mute, patch en la LV1 y el origen de la señal";
        e.cond = db < rc.threshold ? Cond::Bad : Cond::Good;
        break;
    }
    case RuleId::A02_StreamInterrupted: {
        if (!s.sourceRunning) {
            e.cond = Cond::NotApplicable;
            break;
        }
        const double gapMs = static_cast<double>(now - lastProgressNs_) / kNsPerMs;
        e.ev.observed = framesAdvancedThisTick_ ? "frames recibiéndose" : fmt("%.0f ms sin frames", gapMs);
        e.ev.threshold = fmt("> %.0f ms sin frames", rc.threshold);
        e.ev.measurementSource = "Contador de frames PCM";
        e.ev.cause = "No llegan bloques de audio del transporte estando la fuente en marcha";
        e.ev.action = "Revisa el driver, la asignación en Setup > Inventory y el patch de la LV1";
        e.cond = (!framesAdvancedThisTick_ && gapMs > rc.threshold) ? Cond::Bad : Cond::Good;
        if (!framesAdvancedThisTick_ && gapMs <= rc.threshold)
            e.cond = Cond::Unknown; // dentro del margen: no concluir nada todavía
        break;
    }
    case RuleId::A03_DriverLost: {
        if (!s.sourceRunning) {
            e.cond = Cond::NotApplicable;
            break;
        }
        e.ev.measurementSource = "Estado del driver";
        e.ev.observed = std::string(transportStateName(s.transportState));
        if (s.lastError != ErrorCode::Ok)
            e.ev.observed += std::string(" (") + errorCodeName(s.lastError) + ": " + s.lastErrorMessage + ")";
        e.ev.threshold = "error o invalidación confirmada (debounce " + std::to_string(rc.windowMs) + " ms)";
        e.ev.cause = "El dispositivo SoundGrid/driver no está disponible";
        e.ev.action = "Intentando reconectar. Comprueba que el driver Waves está en marcha y no lo usa otra aplicación";
        switch (s.transportState) {
        case TransportState::Error:
        case TransportState::Reconnecting:
        case TransportState::Unavailable:
            e.cond = Cond::Bad;
            break;
        case TransportState::Streaming:
        case TransportState::Degraded:
            e.cond = Cond::Good;
            break;
        default:
            e.cond = Cond::Unknown;
        }
        break;
    }
    case RuleId::A04_NoLink: {
        if (!s.nicSelected) {
            e.cond = Cond::NotApplicable;
            break;
        }
        e.ev.measurementSource = nicSourceName(s);
        e.ev.threshold = "carrier DOWN > " + std::to_string(rc.windowMs) + " ms";
        e.ev.action = "Comprueba el cable Ethernet, el switch SoundGrid y la NIC";
        if (!s.nic.present) {
            e.cond = Cond::Bad;
            e.ev.observed = "NIC no presente";
            e.ev.cause = "La NIC seleccionada no está presente (¿adaptador USB desconectado?)";
        } else if (!s.nic.linkUp.has_value()) {
            e.cond = Cond::Unknown;
            e.ev.observed = "estado de enlace NO DISPONIBLE";
            e.ev.confidence = Confidence::NotObservable;
        } else {
            e.cond = *s.nic.linkUp ? Cond::Good : Cond::Bad;
            e.ev.observed = *s.nic.linkUp ? "enlace UP" : "enlace DOWN";
            e.ev.cause = "Cable Ethernet desconectado o switch sin enlace";
        }
        break;
    }
    case RuleId::A05_SlowLink: {
        if (!s.nicSelected) {
            e.cond = Cond::NotApplicable;
            break;
        }
        e.ev.measurementSource = nicSourceName(s);
        const double expected = rc.threshold > 0 ? rc.threshold : static_cast<double>(s.expectedSpeedBps);
        e.ev.threshold = fmt("< %.0f Mbps durante ", expected / 1e6) + std::to_string(rc.windowMs / 1000) + " s";
        e.ev.cause = "Velocidad negociada inferior a la esperada (no prueba pérdida de paquetes)";
        e.ev.action = "Comprueba cable (Cat5e/Cat6), puerto del switch y NIC";
        if (!s.nic.present || !s.nic.linkUp.value_or(false) || !s.nic.speedBps) {
            e.cond = Cond::Unknown;
            e.ev.observed = "velocidad NO DISPONIBLE";
            e.ev.confidence = Confidence::NotObservable;
        } else {
            e.ev.observed = fmt("%.0f Mbps", static_cast<double>(*s.nic.speedBps) / 1e6);
            e.cond = static_cast<double>(*s.nic.speedBps) < expected ? Cond::Bad : Cond::Good;
        }
        break;
    }
    case RuleId::A06_NicErrors: {
        if (!s.nicSelected) {
            e.cond = Cond::NotApplicable;
            break;
        }
        e.ev.measurementSource = nicSourceName(s);
        e.ev.threshold = fmt(">= %.0f errores en ", rc.threshold) + std::to_string(rc.trailingMs / 1000) + " s";
        e.ev.cause = "Errores/descartes contabilizados por la NIC (no son pérdidas SoundGrid medidas)";
        e.ev.action = "Revisa el enlace, el cable y la carga del switch";
        const bool any = s.nic.inErrors || s.nic.inDiscards || s.nic.outErrors;
        if (!s.nic.present || !any) {
            e.cond = Cond::Unknown;
            e.ev.observed = "contadores NO DISPONIBLES";
            e.ev.confidence = Confidence::NotObservable;
            nicErrHist_.clear();
            break;
        }
        const uint64_t total = s.nic.inErrors.value_or(0) + s.nic.inDiscards.value_or(0) + s.nic.outErrors.value_or(0);
        if (!nicErrHist_.empty() && total < nicErrHist_.back().second)
            nicErrHist_.clear(); // contadores reiniciados (NIC reconectada): nueva línea base
        nicErrHist_.emplace_back(now, total);
        while (nicErrHist_.size() > 1 && now - nicErrHist_.front().first > rc.trailingMs * kNsPerMs)
            nicErrHist_.pop_front();
        const uint64_t delta = total - nicErrHist_.front().second;
        e.ev.observed = std::to_string(delta) + " errores en la ventana";
        e.cond = static_cast<double>(delta) >= rc.threshold && delta > 0 ? Cond::Bad : Cond::Good;
        break;
    }
    case RuleId::A07_Latency: {
        if (!s.sourceRunning) {
            e.cond = Cond::NotApplicable;
            break;
        }
        e.ev.measurementSource = "Tramo medido: callback del driver -> entrega a OBS";
        e.ev.threshold = fmt("> %.0f ms durante ", rc.threshold) + std::to_string(rc.windowMs / 1000) + " s";
        std::string extra = " · Latencia extremo a extremo: NO DISPONIBLE";
        if (s.queueDelayMs.known())
            extra += fmt(" · Retardo estimado por cola: %.1f ms", *s.queueDelayMs.value);
        if (!s.pluginLatencyMs.known()) {
            e.cond = Cond::Unknown;
            e.ev.observed = "NO DISPONIBLE" + extra;
            e.ev.confidence = Confidence::NotObservable;
            break;
        }
        e.ev.observed = fmt("%.1f ms", *s.pluginLatencyMs.value) + extra;
        e.ev.confidence = s.pluginLatencyMs.confidence;
        e.ev.cause = "Retardo interno del plugin elevado (CPU, cola o hilo de entrega)";
        e.ev.action = "Revisa la carga de CPU y el tamaño de buffer";
        e.cond = *s.pluginLatencyMs.value > rc.threshold ? Cond::Bad : Cond::Good;
        break;
    }
    case RuleId::A08_Dropouts: {
        if (!s.sourceRunning) {
            e.cond = Cond::NotApplicable;
            break;
        }
        const uint64_t total = s.dropoutsTotal();
        if (!dropoutHist_.empty() && total < dropoutHist_.back().second)
            dropoutHist_.clear();
        dropoutHist_.emplace_back(now, total);
        while (dropoutHist_.size() > 1 && now - dropoutHist_.front().first > rc.trailingMs * kNsPerMs)
            dropoutHist_.pop_front();
        const uint64_t delta = total - dropoutHist_.front().second;
        e.ev.measurementSource = s.driverOverloadsObservable ? "Cola interna + discontinuidades + xruns del driver"
                                                              : "Cola interna + discontinuidades (xruns del driver no observables)";
        e.ev.observed = std::to_string(delta) + " en la ventana (desbordes " + std::to_string(s.overflows) +
                        ", discontinuidades " + std::to_string(s.discontinuities) + ")";
        e.ev.threshold = fmt("> %.0f en ", rc.threshold) + std::to_string(rc.trailingMs / 1000) + " s";
        e.ev.cause = "Se detectaron cortes de audio (desbordes, huecos de timestamp o xruns)";
        e.ev.action = "Revisa buffers y carga de CPU";
        e.cond = static_cast<double>(delta) > rc.threshold ? Cond::Bad : Cond::Good;
        break;
    }
    case RuleId::A09_Clipping: {
        if (!s.sourceRunning || !framesAdvancedThisTick_ || s.meter.samples == 0) {
            e.cond = s.sourceRunning ? Cond::Unknown : Cond::NotApplicable;
            break;
        }
        bool clipped = false;
        for (uint32_t c = 0; c < s.meter.channels && c < 8; ++c)
            clipped |= s.meter.clippedSamples[c] > 0;
        if (clipped)
            clipTicks_.push_back(now);
        while (!clipTicks_.empty() && now - clipTicks_.front() > rc.trailingMs * kNsPerMs)
            clipTicks_.pop_front();
        e.ev.measurementSource = "PCM (pico digital)";
        e.ev.observed = fmt("pico %.2f dBFS", maxPeakDb(s.meter)) + ", " + std::to_string(clipTicks_.size()) +
                        " ventanas saturadas";
        e.ev.threshold = fmt(">= %.0f ventanas con pico >= -0.1 dBFS en ", rc.threshold) +
                         std::to_string(rc.trailingMs / 1000) + " s";
        e.ev.cause = "Posible saturación digital de entrada";
        e.ev.action = "Reduce la ganancia de envío en la LV1 (el plugin no altera niveles)";
        e.cond = static_cast<double>(clipTicks_.size()) >= rc.threshold ? Cond::Bad : Cond::Good;
        break;
    }
    default:
        e.cond = Cond::NotApplicable;
    }
    return e;
}

// ------------------------------------------------------------------ máquina de estados

AlertEvent AlertEngine::makeEvent(RuleId r, const Eval &e, const HealthSnapshot &s, const char *transition)
{
    AlertEvent ev;
    ev.rule = r;
    ev.severity = cfg_.rule(r).severity;
    ev.transition = transition;
    ev.wallTime = wallClockIso8601Utc();
    ev.message = std::string(ruleCode(r)) + " · " + ruleTitle(r);
    ev.evidence = e.ev;
    ev.platform = s.platform;
    ev.affectedSource = s.sourceName;
    return ev;
}

bool AlertEngine::shouldNotify(const AlertEvent &e, int64_t nowNs) const
{
    if (e.secondary || notificationsMuted(nowNs))
        return false;
    return static_cast<int>(e.severity) >= static_cast<int>(cfg_.notifyMinSeverity);
}

void AlertEngine::step(RuleId r, const Eval &e, const HealthSnapshot &s, std::vector<AlertEvent> &out)
{
    Runtime &x = rt(r);
    const RuleConfig &rc = cfg_.rule(r);
    const int64_t now = s.nowNs;
    x.observed = e.ev.observed.empty() ? (e.cond == Cond::Unknown ? "NO DISPONIBLE" : "") : e.ev.observed;

    auto closeIncident = [&](bool quiet) {
        AlertEvent done = x.incident;
        done.state = AlertState::Resolved;
        done.transition = "RESOLVED";
        done.endedAtNs = now;
        done.wallTime = wallClockIso8601Utc();
        done.notify = false;
        if (!quiet)
            done.evidence = e.ev.observed.empty() ? done.evidence : e.ev;
        out.push_back(done);
        // A10: confirmación de recuperación del incidente principal.
        if (!quiet && cfg_.rule(RuleId::A10_Recovered).enabled && !done.secondary) {
            AlertEvent rec = done;
            rec.rule = RuleId::A10_Recovered;
            rec.severity = Severity::Info;
            rec.transition = "RECOVERED";
            rec.message = std::string("A10 · Recuperado: ") + ruleTitle(r);
            rec.evidence.cause = "La condición se mantuvo sana " + std::to_string(rc.clearMs / 1000) + " s";
            rec.notify = x.lastNotified && !notificationsMuted(now);
            out.push_back(rec);
        }
        // Consecuencias que siguen abiertas (y no se están recuperando) cuando su causa raíz se
        // cierra: pasan a ser incidentes principales y se notifican si procede.
        if (r == RuleId::A04_NoLink || r == RuleId::A03_DriverLost) {
            for (std::size_t k = 0; k < kRuleCount; ++k) {
                Runtime &d = rt_[k];
                if (!d.incident.id || !d.incident.secondary || d.incident.correlationId != done.id ||
                    d.state == RuleEvalState::Recovering)
                    continue;
                const RuleId dep = static_cast<RuleId>(k);
                if (r == RuleId::A04_NoLink && dep == RuleId::A02_StreamInterrupted && isOpen(RuleId::A03_DriverLost) &&
                    !rt(RuleId::A03_DriverLost).incident.secondary) {
                    d.incident.correlationId = rt(RuleId::A03_DriverLost).incident.id; // sigue siendo consecuencia
                    continue;
                }
                d.incident.secondary = false;
                d.incident.correlationId = 0;
                AlertEvent upd = d.incident;
                upd.transition = "PROMOTED";
                upd.wallTime = wallClockIso8601Utc();
                upd.notify = shouldNotify(upd, now);
                d.lastNotified = d.lastNotified || upd.notify;
                out.push_back(upd);
            }
        }
        x.lastResolvedNs = now;
        x.lastResolvedId = x.incident.id;
        x.incident = AlertEvent{};
        x.wasAcknowledged = false;
        x.goodSinceNs = -1;
        x.badSinceNs = -1;
    };

    if (e.cond == Cond::NotApplicable) {
        if (x.incident.id)
            closeIncident(true);
        x.state = rc.enabled ? RuleEvalState::Healthy : RuleEvalState::Disabled;
        x.badSinceNs = -1;
        return;
    }

    if (e.cond == Cond::Unknown) {
        // Nunca convertir UNKNOWN en OK ni en alarma: se conserva el incidente abierto.
        if (!x.incident.id) {
            x.state = RuleEvalState::Unknown;
            x.badSinceNs = -1;
        }
        return;
    }

    if (e.cond == Cond::Bad) {
        x.goodSinceNs = -1;
        if (x.incident.id) {
            if (x.state == RuleEvalState::Recovering)
                x.state = x.wasAcknowledged ? RuleEvalState::Acknowledged : RuleEvalState::Active;
            x.incident.evidence = e.ev; // evidencia más reciente para la UI
            return;
        }
        if (x.badSinceNs < 0)
            x.badSinceNs = now;
        x.state = RuleEvalState::Pending;
        if (now - x.badSinceNs < rc.windowMs * kNsPerMs)
            return;
        // Si la causa raíz está pendiente de confirmar (debounce más largo), esperar: así la
        // consecuencia se abre ya correlacionada y no se notifica como incidente principal.
        auto rootPending = [&](RuleId root) {
            return cfg_.rule(root).enabled && rt(root).state == RuleEvalState::Pending;
        };
        const bool dependsOnLink = r == RuleId::A02_StreamInterrupted || r == RuleId::A03_DriverLost ||
                                   r == RuleId::A05_SlowLink || r == RuleId::A06_NicErrors;
        if ((dependsOnLink && rootPending(RuleId::A04_NoLink)) ||
            (r == RuleId::A02_StreamInterrupted && rootPending(RuleId::A03_DriverLost)))
            return;

        // Activar (o reabrir dentro del cooldown, sin nueva notificación).
        const bool reopen = x.lastResolvedNs >= 0 && now - x.lastResolvedNs < rc.cooldownMs * kNsPerMs;
        AlertEvent ev = makeEvent(r, e, s, reopen ? "REOPENED" : "ACTIVATED");
        ev.id = reopen ? x.lastResolvedId : nextIncidentId_++;
        ev.state = AlertState::Active;
        ev.startedAtNs = x.badSinceNs;
        // Correlación de causa raíz: NIC DOWN domina; driver perdido domina el flujo.
        if (r == RuleId::A02_StreamInterrupted || r == RuleId::A03_DriverLost || r == RuleId::A01_Silence) {
            if (isOpen(RuleId::A04_NoLink)) {
                ev.correlationId = rt(RuleId::A04_NoLink).incident.id;
                ev.secondary = true;
            } else if (r == RuleId::A02_StreamInterrupted && isOpen(RuleId::A03_DriverLost)) {
                ev.correlationId = rt(RuleId::A03_DriverLost).incident.id;
                ev.secondary = true;
            }
        }
        if ((r == RuleId::A05_SlowLink || r == RuleId::A06_NicErrors) && isOpen(RuleId::A04_NoLink)) {
            ev.correlationId = rt(RuleId::A04_NoLink).incident.id;
            ev.secondary = true;
        }
        // Una reapertura solo se calla si el incidente ya se había notificado; si antes fue
        // secundario o estaba silenciado y ahora es principal, se notifica.
        const bool alreadyNotified = reopen && x.lastNotified;
        ev.notify = shouldNotify(ev, now) && !alreadyNotified;
        x.lastNotified = ev.notify || alreadyNotified;
        x.incident = ev;
        x.state = RuleEvalState::Active;
        out.push_back(ev);

        // Si se abre la causa raíz después de sus consecuencias, reclasificarlas.
        if (r == RuleId::A04_NoLink || r == RuleId::A03_DriverLost) {
            for (RuleId dep : {RuleId::A02_StreamInterrupted, RuleId::A03_DriverLost, RuleId::A05_SlowLink,
                               RuleId::A06_NicErrors}) {
                if (dep == r || (r == RuleId::A03_DriverLost && dep != RuleId::A02_StreamInterrupted))
                    continue;
                Runtime &d = rt(dep);
                if (d.incident.id && !d.incident.secondary) {
                    d.incident.secondary = true;
                    d.incident.correlationId = ev.id;
                    AlertEvent upd = d.incident;
                    upd.transition = "CORRELATED";
                    upd.notify = false;
                    upd.wallTime = wallClockIso8601Utc();
                    out.push_back(upd);
                }
            }
        }
        return;
    }

    // Cond::Good
    x.badSinceNs = -1;
    if (!x.incident.id) {
        x.state = RuleEvalState::Healthy;
        return;
    }
    if (x.state != RuleEvalState::Recovering) {
        x.state = RuleEvalState::Recovering;
        x.goodSinceNs = now;
        AlertEvent ev = x.incident;
        ev.state = AlertState::Recovering;
        ev.transition = "RECOVERING";
        ev.notify = false;
        ev.wallTime = wallClockIso8601Utc();
        ev.evidence = e.ev;
        out.push_back(ev);
        return;
    }
    if (now - x.goodSinceNs >= rc.clearMs * kNsPerMs)
        closeIncident(false);
}

std::vector<AlertEvent> AlertEngine::evaluate(const HealthSnapshot &s)
{
    std::vector<AlertEvent> out;
    if (!started_)
        restart(s.nowNs);

    framesAdvancedThisTick_ = s.framesWritten != lastFrames_;
    if (framesAdvancedThisTick_) {
        lastFrames_ = s.framesWritten;
        lastProgressNs_ = s.nowNs;
    }
    if (!s.sourceRunning)
        lastProgressNs_ = s.nowNs; // detenida a propósito: no cuenta como hueco

    if (s.nowNs < graceUntilNs_) {
        for (auto &r : rt_)
            if (!r.incident.id)
                r.state = RuleEvalState::Initializing;
        global_ = GlobalStatus::Initializing;
        return out;
    }

    // Orden: causas raíz primero, para que la correlación vea los incidentes abiertos.
    static constexpr RuleId order[] = {RuleId::A04_NoLink,   RuleId::A03_DriverLost, RuleId::A02_StreamInterrupted,
                                       RuleId::A05_SlowLink, RuleId::A06_NicErrors,  RuleId::A08_Dropouts,
                                       RuleId::A07_Latency,  RuleId::A09_Clipping,   RuleId::A01_Silence};
    for (RuleId r : order)
        step(r, evalRule(r, s), s, out);
    updateGlobal(s);
    return out;
}

std::optional<AlertEvent> AlertEngine::acknowledge(uint64_t incidentId, int64_t nowNs)
{
    for (std::size_t i = 0; i < kRuleCount; ++i) {
        Runtime &x = rt_[i];
        if (x.incident.id == incidentId && incidentId != 0) {
            x.wasAcknowledged = true;
            if (x.state == RuleEvalState::Active)
                x.state = RuleEvalState::Acknowledged;
            x.incident.state = AlertState::Acknowledged;
            AlertEvent ev = x.incident;
            ev.transition = "ACKNOWLEDGED";
            ev.notify = false;
            ev.wallTime = wallClockIso8601Utc();
            (void)nowNs;
            return ev;
        }
    }
    return std::nullopt;
}

RuleStatus AlertEngine::ruleStatus(RuleId r) const
{
    const Runtime &x = rt(r);
    RuleStatus st;
    st.state = cfg_.rule(r).enabled ? x.state : RuleEvalState::Disabled;
    st.observed = x.observed;
    st.incidentId = x.incident.id;
    st.secondary = x.incident.secondary;
    return st;
}

std::vector<AlertEvent> AlertEngine::openIncidents() const
{
    std::vector<AlertEvent> v;
    for (const auto &x : rt_)
        if (x.incident.id) {
            AlertEvent e = x.incident;
            if (x.state == RuleEvalState::Recovering)
                e.state = AlertState::Recovering;
            v.push_back(e);
        }
    std::sort(v.begin(), v.end(), [](const AlertEvent &a, const AlertEvent &b) {
        if (a.secondary != b.secondary)
            return !a.secondary;
        return static_cast<int>(a.severity) > static_cast<int>(b.severity);
    });
    return v;
}

void AlertEngine::updateGlobal(const HealthSnapshot &s)
{
    auto primaryOpen = [&](RuleId r) {
        const Runtime &x = rt(r);
        return x.incident.id && !x.incident.secondary && x.state != RuleEvalState::Recovering;
    };
    if (primaryOpen(RuleId::A04_NoLink))
        global_ = GlobalStatus::NoLink;
    else if (primaryOpen(RuleId::A03_DriverLost))
        global_ = GlobalStatus::DriverUnavailable;
    else if (primaryOpen(RuleId::A02_StreamInterrupted))
        global_ = GlobalStatus::AudioInterrupted;
    else if (primaryOpen(RuleId::A08_Dropouts))
        global_ = GlobalStatus::Dropouts;
    else if (primaryOpen(RuleId::A05_SlowLink))
        global_ = GlobalStatus::DegradedLink;
    else if (primaryOpen(RuleId::A06_NicErrors))
        global_ = GlobalStatus::NetworkErrors;
    else if (primaryOpen(RuleId::A07_Latency))
        global_ = GlobalStatus::HighLatency;
    else if (primaryOpen(RuleId::A09_Clipping))
        global_ = GlobalStatus::Clipping;
    else if (primaryOpen(RuleId::A01_Silence))
        global_ = GlobalStatus::SilenceDetected;
    else if (s.sourceRunning && framesAdvancedThisTick_)
        global_ = GlobalStatus::AudioOk;
    else
        global_ = GlobalStatus::DataUnavailable;
}

} // namespace sg
