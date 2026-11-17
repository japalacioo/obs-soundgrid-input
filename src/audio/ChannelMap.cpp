#include "ChannelMap.h"

#include <cstring>
#include <sstream>

namespace sg {

const char *channelModeName(ChannelMode m)
{
    switch (m) {
    case ChannelMode::Mono: return "mono";
    case ChannelMode::Stereo: return "stereo";
    case ChannelMode::Multi: return "multi";
    }
    return "?";
}

ChannelMap ChannelMap::mono(int16_t in)
{
    ChannelMap m;
    m.mode = ChannelMode::Mono;
    m.count = 1;
    m.inputs.fill(kUnused);
    m.inputs[0] = in;
    return m;
}

ChannelMap ChannelMap::monoSum(std::initializer_list<int16_t> ins)
{
    ChannelMap m;
    m.mode = ChannelMode::Mono;
    m.inputs.fill(kUnused);
    m.count = 0;
    for (auto v : ins)
        if (m.count < kMaxOut)
            m.inputs[m.count++] = v;
    return m;
}

ChannelMap ChannelMap::stereo(int16_t l, int16_t r)
{
    ChannelMap m;
    m.mode = ChannelMode::Stereo;
    m.count = 2;
    m.inputs.fill(kUnused);
    m.inputs[0] = l;
    m.inputs[1] = r;
    return m;
}

ChannelMap ChannelMap::multi(std::initializer_list<int16_t> ins)
{
    ChannelMap m = monoSum(ins);
    m.mode = ChannelMode::Multi;
    return m;
}

uint32_t ChannelMap::outputChannels() const noexcept
{
    switch (mode) {
    case ChannelMode::Mono: return 1;
    case ChannelMode::Stereo: return 2;
    case ChannelMode::Multi: return count == 0 ? 1 : (count > kMaxOut ? kMaxOut : count);
    }
    return 2;
}

bool ChannelMap::validate(uint32_t deviceChannels, std::string *why) const
{
    auto fail = [&](const std::string &msg) {
        if (why)
            *why = msg;
        return false;
    };
    if (count == 0)
        return fail("no hay canales seleccionados");
    if (mode == ChannelMode::Stereo && count != 2)
        return fail("estéreo requiere exactamente 2 entradas");
    if (count > kMaxOut)
        return fail("más de 8 canales por fuente (límite de OBS)");
    if (deviceChannels == 0)
        return fail("el dispositivo no expone canales de entrada");
    for (uint32_t i = 0; i < count; ++i) {
        if (inputs[i] < 0 || static_cast<uint32_t>(inputs[i]) >= deviceChannels) {
            std::ostringstream os;
            os << "entrada " << (inputs[i] + 1) << " fuera de rango (dispositivo con " << deviceChannels
               << " canales)";
            return fail(os.str());
        }
    }
    return true;
}

std::string ChannelMap::describe() const
{
    std::ostringstream os;
    os << channelModeName(mode) << '(';
    for (uint32_t i = 0; i < count && i < kMaxOut; ++i) {
        if (i)
            os << ',';
        os << (inputs[i] + 1);
    }
    os << ')';
    return os.str();
}

namespace {
inline const float *inputPlane(const float *const *planes, uint32_t deviceChannels, int16_t idx) noexcept
{
    if (idx < 0 || static_cast<uint32_t>(idx) >= deviceChannels)
        return nullptr;
    return planes[idx];
}
} // namespace

void applyChannelMap(const ChannelMap &map, const float *const *devicePlanes, uint32_t deviceChannels,
                     uint32_t frames, uint32_t frameOffset, float *const *outPlanes) noexcept
{
    const uint32_t outCh = map.outputChannels();
    if (map.mode == ChannelMode::Mono) {
        float *out = outPlanes[0];
        const uint32_t n = map.count == 0 ? 1 : (map.count > ChannelMap::kMaxOut ? ChannelMap::kMaxOut : map.count);
        std::memset(out, 0, frames * sizeof(float));
        uint32_t valid = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const float *in = inputPlane(devicePlanes, deviceChannels, map.inputs[i]);
            if (!in)
                continue;
            ++valid;
            in += frameOffset;
            for (uint32_t f = 0; f < frames; ++f)
                out[f] += in[f];
        }
        if (valid > 1) {
            const float g = 1.0f / static_cast<float>(valid);
            for (uint32_t f = 0; f < frames; ++f)
                out[f] *= g;
        }
        return;
    }
    for (uint32_t c = 0; c < outCh; ++c) {
        const float *in = inputPlane(devicePlanes, deviceChannels, map.inputs[c]);
        if (in)
            std::memcpy(outPlanes[c], in + frameOffset, frames * sizeof(float));
        else
            std::memset(outPlanes[c], 0, frames * sizeof(float));
    }
}

} // namespace sg
