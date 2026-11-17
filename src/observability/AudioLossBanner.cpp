#include "AudioLossBanner.h"

#include <cstdio>
#include <ctime>

namespace sg {

bool isAudioLossRule(RuleId r)
{
    return r == RuleId::A01_Silence || r == RuleId::A02_StreamInterrupted || r == RuleId::A03_DriverLost ||
           r == RuleId::A04_NoLink;
}

namespace {

// Causa más profunda primero: un cable suelto explica un driver perdido y la falta de frames.
int priority(RuleId r)
{
    switch (r) {
    case RuleId::A04_NoLink: return 4;
    case RuleId::A03_DriverLost: return 3;
    case RuleId::A02_StreamInterrupted: return 2;
    case RuleId::A01_Silence: return 1;
    default: return 0;
    }
}

const char *shortCause(RuleId r)
{
    switch (r) {
    case RuleId::A04_NoLink: return "sin enlace de red (cable o switch)";
    case RuleId::A03_DriverLost: return "el driver dejó de responder";
    case RuleId::A02_StreamInterrupted: return "no llegan muestras del driver";
    case RuleId::A01_Silence: return "silencio en una fuente supervisada";
    default: return "";
    }
}

} // namespace

std::string localClockFromIso(const std::string &iso)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (iso.size() < 19 || iso[10] != 'T' ||
        std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6)
        return iso; // ya viene en formato de presentación
    std::tm utc{};
    utc.tm_year = y - 1900;
    utc.tm_mon = mo - 1;
    utc.tm_mday = d;
    utc.tm_hour = h;
    utc.tm_min = mi;
    utc.tm_sec = s;
#if defined(_WIN32)
    const std::time_t t = _mkgmtime(&utc);
    std::tm local{};
    if (t == -1 || localtime_s(&local, &t) != 0)
        return iso;
#else
    const std::time_t t = timegm(&utc);
    std::tm local{};
    if (t == -1 || !localtime_r(&t, &local))
        return iso;
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &local);
    return buf;
}

BannerState AudioLossBannerModel::update(const std::vector<BannerSourceInput> &sources, int64_t nowNs)
{
    const AlertEvent *worst = nullptr;
    const BannerSourceInput *worstSrc = nullptr;
    bool worstRecovering = false;
    int affected = 0;

    for (const auto &s : sources) {
        if (!s.bannerEnabled || s.notificationsMuted)
            continue;
        // Por fuente: ¿sigue faltando el audio? Cuenta también las consecuencias: durante una
        // reconexión el driver puede volver (A03 en recuperación) sin que lleguen muestras (A02).
        const AlertEvent *shown = nullptr;
        bool anyActive = false;
        for (const auto &e : s.openIncidents) {
            if (!isAudioLossRule(e.rule) || e.state == AlertState::Resolved)
                continue;
            anyActive |= e.state != AlertState::Recovering;
            // Se muestra la causa raíz (no secundaria) de mayor prioridad.
            const bool better = !shown || (shown->secondary && !e.secondary) ||
                                (shown->secondary == e.secondary && priority(e.rule) > priority(shown->rule));
            if (better)
                shown = &e;
        }
        if (!shown)
            continue;
        ++affected;
        const bool recovering = !anyActive;
        // Una pérdida en curso pesa más que una que ya se está recuperando.
        const bool better = !worst || (worstRecovering && !recovering) ||
                            (worstRecovering == recovering && priority(shown->rule) > priority(worst->rule));
        if (better) {
            worst = shown;
            worstSrc = &s;
            worstRecovering = recovering;
        }
    }

    BannerState st;
    if (worst) {
        st.level = worstRecovering ? BannerLevel::Recovering : BannerLevel::Lost;
        st.rule = worst->rule;
        st.sourceToOpen = worstSrc->sourceName;
        st.affectedSources = affected;
        st.headline = worstRecovering ? "AUDIO RESTABLECIDO, VERIFICANDO" : "SE PERDIÓ EL AUDIO";
        st.detail = "'" + worstSrc->sourceName + "': " + shortCause(worst->rule);
        if (!worst->wallTime.empty())
            st.detail += " · desde las " + localClockFromIso(worst->wallTime);
        if (affected > 1)
            st.detail += " · " + std::to_string(affected) + " fuentes afectadas";
        st.detail += " · clic para ver el panel";
        lastLevel_ = st.level;
        lastSource_ = st.sourceToOpen;
        recoveredUntilNs_ = 0;
        return st;
    }

    // Nada abierto: si antes había pérdida, se confirma la recuperación unos segundos.
    if (lastLevel_ == BannerLevel::Lost || lastLevel_ == BannerLevel::Recovering) {
        recoveredUntilNs_ = nowNs + recoveredShowNs_;
        lastLevel_ = BannerLevel::Recovered;
    }
    if (lastLevel_ == BannerLevel::Recovered && nowNs < recoveredUntilNs_) {
        st.level = BannerLevel::Recovered;
        st.headline = "AUDIO RECUPERADO";
        st.detail = "'" + lastSource_ + "' vuelve a recibir audio";
        st.sourceToOpen = lastSource_;
        return st;
    }
    lastLevel_ = BannerLevel::Hidden;
    return st;
}

} // namespace sg
