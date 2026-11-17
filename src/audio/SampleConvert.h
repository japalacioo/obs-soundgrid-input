#pragma once
// Conversión de formatos de muestra de los drivers a float32 sin alterar niveles.
// Escalado: enteros con signo de N bits -> [-1, 1) dividiendo por 2^(N-1).
#include <cstddef>
#include <cstdint>

namespace sg {

enum class SampleFormat {
    Int16LE,
    Int24LE,   // 3 bytes empaquetados
    Int32LE,   // 32 bits completos
    Int32LE24, // 24 bits alineados en 32 (LSB)
    Float32LE,
    Float64LE,
    Int16BE,
    Int24BE,
    Int32BE,
    Float32BE,
    Unsupported
};

const char *sampleFormatName(SampleFormat f);
std::size_t bytesPerSample(SampleFormat f);

// Convierte 'frames' muestras de un buffer no entrelazado (un canal) a float32.
// Devuelve false si el formato no está soportado (el llamador entrega silencio seguro).
bool convertToFloat(SampleFormat fmt, const void *src, float *dst, std::size_t frames) noexcept;

// Convierte un canal de un buffer entrelazado (stride en muestras) a float32.
bool convertInterleavedChannelToFloat(SampleFormat fmt, const void *src, std::size_t channelIndex,
                                      std::size_t channelCount, float *dst, std::size_t frames) noexcept;

} // namespace sg
