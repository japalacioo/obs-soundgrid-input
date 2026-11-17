#include "SampleConvert.h"

#include <cstring>

namespace sg {

const char *sampleFormatName(SampleFormat f)
{
    switch (f) {
    case SampleFormat::Int16LE: return "int16le";
    case SampleFormat::Int24LE: return "int24le";
    case SampleFormat::Int32LE: return "int32le";
    case SampleFormat::Int32LE24: return "int32le_24";
    case SampleFormat::Float32LE: return "float32le";
    case SampleFormat::Float64LE: return "float64le";
    case SampleFormat::Int16BE: return "int16be";
    case SampleFormat::Int24BE: return "int24be";
    case SampleFormat::Int32BE: return "int32be";
    case SampleFormat::Float32BE: return "float32be";
    default: return "unsupported";
    }
}

std::size_t bytesPerSample(SampleFormat f)
{
    switch (f) {
    case SampleFormat::Int16LE:
    case SampleFormat::Int16BE: return 2;
    case SampleFormat::Int24LE:
    case SampleFormat::Int24BE: return 3;
    case SampleFormat::Int32LE:
    case SampleFormat::Int32LE24:
    case SampleFormat::Int32BE:
    case SampleFormat::Float32LE:
    case SampleFormat::Float32BE: return 4;
    case SampleFormat::Float64LE: return 8;
    default: return 0;
    }
}

namespace {

inline float readSample(SampleFormat fmt, const uint8_t *p) noexcept
{
    switch (fmt) {
    case SampleFormat::Int16LE: {
        const int16_t v = static_cast<int16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
        return static_cast<float>(v) * (1.0f / 32768.0f);
    }
    case SampleFormat::Int16BE: {
        const int16_t v = static_cast<int16_t>(static_cast<uint16_t>(p[1]) | (static_cast<uint16_t>(p[0]) << 8));
        return static_cast<float>(v) * (1.0f / 32768.0f);
    }
    case SampleFormat::Int24LE: {
        int32_t v = static_cast<int32_t>(static_cast<uint32_t>(p[0]) << 8 | static_cast<uint32_t>(p[1]) << 16 |
                                         static_cast<uint32_t>(p[2]) << 24);
        return static_cast<float>(v >> 8) * (1.0f / 8388608.0f);
    }
    case SampleFormat::Int24BE: {
        int32_t v = static_cast<int32_t>(static_cast<uint32_t>(p[2]) << 8 | static_cast<uint32_t>(p[1]) << 16 |
                                         static_cast<uint32_t>(p[0]) << 24);
        return static_cast<float>(v >> 8) * (1.0f / 8388608.0f);
    }
    case SampleFormat::Int32LE: {
        uint32_t u;
        std::memcpy(&u, p, 4);
        return static_cast<float>(static_cast<double>(static_cast<int32_t>(u)) * (1.0 / 2147483648.0));
    }
    case SampleFormat::Int32BE: {
        const uint32_t u = static_cast<uint32_t>(p[3]) | static_cast<uint32_t>(p[2]) << 8 |
                           static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[0]) << 24;
        return static_cast<float>(static_cast<double>(static_cast<int32_t>(u)) * (1.0 / 2147483648.0));
    }
    case SampleFormat::Int32LE24: {
        uint32_t u;
        std::memcpy(&u, p, 4);
        // 24 bits significativos en la parte baja; extender signo.
        const int32_t v = static_cast<int32_t>(u << 8) >> 8;
        return static_cast<float>(v) * (1.0f / 8388608.0f);
    }
    case SampleFormat::Float32LE: {
        float f;
        std::memcpy(&f, p, 4);
        return f;
    }
    case SampleFormat::Float32BE: {
        const uint32_t u = static_cast<uint32_t>(p[3]) | static_cast<uint32_t>(p[2]) << 8 |
                           static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[0]) << 24;
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    case SampleFormat::Float64LE: {
        double d;
        std::memcpy(&d, p, 8);
        return static_cast<float>(d);
    }
    default:
        return 0.0f;
    }
}

} // namespace

bool convertToFloat(SampleFormat fmt, const void *src, float *dst, std::size_t frames) noexcept
{
    const std::size_t bps = bytesPerSample(fmt);
    if (bps == 0 || !src || !dst) {
        if (dst)
            std::memset(dst, 0, frames * sizeof(float));
        return false;
    }
    if (fmt == SampleFormat::Float32LE) {
        std::memcpy(dst, src, frames * sizeof(float));
        return true;
    }
    const auto *p = static_cast<const uint8_t *>(src);
    for (std::size_t i = 0; i < frames; ++i)
        dst[i] = readSample(fmt, p + i * bps);
    return true;
}

bool convertInterleavedChannelToFloat(SampleFormat fmt, const void *src, std::size_t channelIndex,
                                      std::size_t channelCount, float *dst, std::size_t frames) noexcept
{
    const std::size_t bps = bytesPerSample(fmt);
    if (bps == 0 || !src || !dst || channelIndex >= channelCount) {
        if (dst)
            std::memset(dst, 0, frames * sizeof(float));
        return false;
    }
    const auto *p = static_cast<const uint8_t *>(src) + channelIndex * bps;
    const std::size_t stride = channelCount * bps;
    for (std::size_t i = 0; i < frames; ++i)
        dst[i] = readSample(fmt, p + i * stride);
    return true;
}

} // namespace sg
