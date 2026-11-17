// Telemetría NIC para macOS: getifaddrs (AF_LINK -> if_data: baudrate y errores) y
// SIOCGIFMEDIA (IFM_AVALID/IFM_ACTIVE) para el carrier. Sin privilegios ni tráfico.
// ESTADO: escrito para macOS; NO compilado ni probado en este entorno (ver TEST_REPORT).
#include "../INetworkTelemetry.h"

#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace sg {

namespace {

class MacNicTelemetry final : public INetworkTelemetry {
public:
    const char *apiName() const override { return "macOS getifaddrs/SIOCGIFMEDIA"; }

    std::vector<NicInfo> listInterfaces() override
    {
        std::vector<NicInfo> out;
        ifaddrs *list = nullptr;
        if (getifaddrs(&list) != 0)
            return out;
        for (ifaddrs *a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_LINK || !a->ifa_data)
                continue;
            const auto *d = static_cast<const if_data *>(a->ifa_data);
            if (d->ifi_type != IFT_ETHER)
                continue;
            NicInfo n;
            n.id = a->ifa_name;
            n.displayName = a->ifa_name;
            n.description = "Ethernet";
            n.isEthernet = true;
            n.isPhysical = std::strncmp(a->ifa_name, "en", 2) == 0;
            out.push_back(std::move(n));
        }
        freeifaddrs(list);
        return out;
    }

    NicSample sample(const std::string &nicId) override
    {
        NicSample s;
        s.source = apiName();
        ifaddrs *list = nullptr;
        if (getifaddrs(&list) != 0)
            return s;
        for (ifaddrs *a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_LINK || !a->ifa_data || nicId != a->ifa_name)
                continue;
            const auto *d = static_cast<const if_data *>(a->ifa_data);
            s.present = true;
            if (d->ifi_baudrate)
                s.speedBps = d->ifi_baudrate;
            s.inErrors = d->ifi_ierrors;
            s.outErrors = d->ifi_oerrors;
            s.inDiscards = d->ifi_iqdrops;
            break;
        }
        freeifaddrs(list);
        if (!s.present)
            return s;

        const int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            ifmediareq req{};
            std::strncpy(req.ifm_name, nicId.c_str(), sizeof(req.ifm_name) - 1);
            if (ioctl(fd, SIOCGIFMEDIA, &req) == 0 && (req.ifm_status & IFM_AVALID))
                s.linkUp = (req.ifm_status & IFM_ACTIVE) != 0;
            close(fd);
        }
        if (s.linkUp.has_value() && !*s.linkUp)
            s.speedBps.reset(); // sin enlace no hay velocidad negociada
        return s;
    }
};

} // namespace

std::unique_ptr<INetworkTelemetry> createPlatformNetworkTelemetry()
{
    return std::make_unique<MacNicTelemetry>();
}

} // namespace sg
