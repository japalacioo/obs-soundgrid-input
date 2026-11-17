#pragma once
// Selección y mezcla de canales del dispositivo hacia la disposición de una fuente OBS.
// OBS admite como máximo 8 canales por fuente (MAX_AUDIO_CHANNELS); para más canales
// (stems) se crean varias fuentes sobre el mismo dispositivo compartido.
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace sg {

enum class ChannelMode : uint8_t {
    Mono,   // promedio de 1..8 entradas (sin clipping por suma)
    Stereo, // entradas L y R
    Multi   // 1..8 entradas en orden, una por canal de salida
};

const char *channelModeName(ChannelMode m);

struct ChannelMap {
    static constexpr uint32_t kMaxOut = 8;
    static constexpr int16_t kUnused = -1;

    ChannelMode mode = ChannelMode::Stereo;
    uint8_t count = 2; // número de entradas usadas
    std::array<int16_t, kMaxOut> inputs{0, 1, kUnused, kUnused, kUnused, kUnused, kUnused, kUnused};

    static ChannelMap mono(int16_t in);
    static ChannelMap monoSum(std::initializer_list<int16_t> ins);
    static ChannelMap stereo(int16_t l, int16_t r);
    static ChannelMap multi(std::initializer_list<int16_t> ins);

    uint32_t outputChannels() const noexcept;
    // Valida contra el número de canales que expone el dispositivo.
    bool validate(uint32_t deviceChannels, std::string *why = nullptr) const;
    std::string describe() const; // p. ej. "stereo(3,4)" con índices 1-based para el usuario
    bool operator==(const ChannelMap &o) const = default;
};

// Aplica el mapa (tiempo real, sin asignaciones). Las entradas fuera de rango producen
// silencio seguro. outPlanes debe tener outputChannels() buffers de 'frames' floats.
void applyChannelMap(const ChannelMap &map, const float *const *devicePlanes, uint32_t deviceChannels,
                     uint32_t frames, uint32_t frameOffset, float *const *outPlanes) noexcept;

} // namespace sg
