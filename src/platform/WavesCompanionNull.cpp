// Plataformas sin vigilancia de aplicaciones de Waves (macOS, Linux): no observa ni arranca nada.
#include "WavesCompanion.h"

namespace sg {

namespace {
class NullProcessOps final : public IProcessOps {
public:
    bool isRunning(const std::string &) override { return true; }
    bool launch(const LaunchTarget &, std::string &error) override
    {
        error = "no disponible en esta plataforma";
        return false;
    }
};
} // namespace

std::unique_ptr<IProcessOps> createPlatformProcessOps()
{
    return std::make_unique<NullProcessOps>();
}

std::optional<LaunchTarget> wavesControlPanelTarget()
{
    return std::nullopt;
}

std::optional<LaunchTarget> wavesNodeServiceTarget()
{
    return std::nullopt;
}

bool showOrLaunch(IProcessOps &, const LaunchTarget &, std::string &error)
{
    error = "no disponible en esta plataforma";
    return false;
}

} // namespace sg
