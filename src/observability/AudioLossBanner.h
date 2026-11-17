#pragma once
// Modelo del aviso visible en OBS cuando se pierde el audio (sin dependencias de UI ni de SO).
// Lo consulta periódicamente la capa de interfaz (barra roja en la ventana principal de OBS).
#include "AlertTypes.h"

#include <string>
#include <vector>

namespace sg {

// Lo que la interfaz sabe de cada fuente SoundGrid en un instante.
struct BannerSourceInput {
    std::string sourceName;
    bool bannerEnabled = true;      // ajuste por fuente "Mostrar aviso en OBS"
    bool notificationsMuted = false; // "Silenciar avisos 10 min": la detección sigue, el aviso no
    std::vector<AlertEvent> openIncidents;
};

enum class BannerLevel { Hidden, Lost, Recovering, Recovered };

struct BannerState {
    BannerLevel level = BannerLevel::Hidden;
    std::string headline;     // "SE PERDIÓ EL AUDIO" / "AUDIO RESTABLECIDO..." / "AUDIO RECUPERADO"
    std::string detail;       // fuente, causa y hora
    std::string sourceToOpen; // fuente cuyo panel abre el clic
    RuleId rule = RuleId::A02_StreamInterrupted;
    int affectedSources = 0;

    bool operator==(const BannerState &o) const
    {
        return level == o.level && headline == o.headline && detail == o.detail && sourceToOpen == o.sourceToOpen;
    }
    bool operator!=(const BannerState &o) const { return !(*this == o); }
};

// Reglas que significan "se dejó de recibir audio" (A10 no, es la recuperación).
bool isAudioLossRule(RuleId r);

// "2026-11-17T23:39:21.651Z" (UTC) -> "18:39:21" en la hora local del equipo. Otro formato: sin cambios.
std::string localClockFromIso(const std::string &iso);

class AudioLossBannerModel {
public:
    explicit AudioLossBannerModel(int64_t recoveredShowMs = 5000) : recoveredShowNs_(recoveredShowMs * 1'000'000) {}

    // Evalúa el estado actual. nowNs: reloj monotónico (solo para el aviso verde temporal).
    BannerState update(const std::vector<BannerSourceInput> &sources, int64_t nowNs);

private:
    int64_t recoveredShowNs_;
    BannerLevel lastLevel_ = BannerLevel::Hidden;
    int64_t recoveredUntilNs_ = 0;
    std::string lastSource_;
};

} // namespace sg
