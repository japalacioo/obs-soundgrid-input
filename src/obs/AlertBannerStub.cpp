// Plataformas sin barra nativa todavía (macOS, Linux): las alertas siguen en el panel y el log.
#include "AlertBanner.h"

namespace sg {

void startAlertBanner() {}
void stopAlertBanner() {}
bool alertBannerAvailable()
{
    return false;
}

} // namespace sg
