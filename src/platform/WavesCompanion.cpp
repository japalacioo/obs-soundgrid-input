#include "WavesCompanion.h"

namespace sg {

namespace {
constexpr int64_t kNsPerMsLocal = 1'000'000;
}

void KeepAlive::resetGiveUp()
{
    st_.gaveUp = false;
    quickFailures_ = 0;
    st_.lastError.clear();
}

std::optional<std::string> KeepAlive::poll(IProcessOps &ops, bool enabled, int64_t nowNs)
{
    const bool running = ops.isRunning(t_.processName);
    st_.checked = true;
    st_.running = running;

    if (running) {
        if (!tracking_) {
            tracking_ = true;
            runningSinceNs_ = nowNs;
        }
        seenSinceLaunch_ = true;
        if (quickFailures_ && nowNs - runningSinceNs_ >= p_.stableMs * kNsPerMsLocal)
            quickFailures_ = 0; // estable: los fallos anteriores ya no cuentan
        return std::nullopt;
    }

    std::optional<std::string> msg;
    if (tracking_) {
        // Estaba abierto y se cerró. Si lo habíamos abierto nosotros y duró poco, cuenta como fallo.
        tracking_ = false;
        const int64_t upMs = (nowNs - runningSinceNs_) / kNsPerMsLocal;
        if (launchedByUs_ && upMs < p_.stableMs)
            ++quickFailures_;
        msg = t_.label + " se cerró";
    }
    if (!enabled || st_.gaveUp)
        return msg;

    if (launched_ && !seenSinceLaunch_) {
        if (nowNs - lastLaunchNs_ < p_.settleMs * kNsPerMsLocal)
            return msg; // aún arrancando
        ++quickFailures_; // se abrió pero nunca apareció
        seenSinceLaunch_ = true;
    }
    if (quickFailures_ >= p_.maxQuickFailures) {
        st_.gaveUp = true;
        return t_.label + ": se cerró " + std::to_string(quickFailures_) +
               " veces seguidas tras abrirlo; se deja de reabrir (pulsa el botón para abrirlo a mano)";
    }

    std::string err;
    launched_ = true;
    lastLaunchNs_ = nowNs;
    seenSinceLaunch_ = false;
    if (ops.launch(t_, err)) {
        launchedByUs_ = true;
        ++st_.relaunches;
        st_.lastError.clear();
        return (msg ? *msg + "; " : t_.label + " no estaba abierto; ") + "se abrió (minimizado)";
    }
    st_.lastError = err;
    ++quickFailures_;
    seenSinceLaunch_ = true;
    return (msg ? *msg + "; " : std::string()) + "no se pudo abrir " + t_.label + ": " + err;
}

} // namespace sg
