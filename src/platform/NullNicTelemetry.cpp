// Plataformas fuera de alcance: telemetría "no observable" (solo para compilar el núcleo).
#include "INetworkTelemetry.h"

namespace sg {

namespace {
class NullNicTelemetry final : public INetworkTelemetry {
public:
    std::vector<NicInfo> listInterfaces() override { return {}; }
    NicSample sample(const std::string &) override { return {}; }
    const char *apiName() const override { return "no disponible"; }
};
} // namespace

std::unique_ptr<INetworkTelemetry> createPlatformNetworkTelemetry()
{
    return std::make_unique<NullNicTelemetry>();
}

} // namespace sg
