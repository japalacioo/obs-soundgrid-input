// Telemetría NIC para Windows mediante IP Helper (GetIfTable2 / MIB_IF_ROW2).
// No requiere privilegios de administrador y no genera tráfico de red.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <windows.h>
#include <objbase.h>

#include "../INetworkTelemetry.h"

#include <cstdio>
#include <string>

namespace sg {

namespace {

std::string wide(const wchar_t *w)
{
    if (!w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 1 ? static_cast<size_t>(n - 1) : 0, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string guidString(const GUID &g)
{
    wchar_t buf[64];
    StringFromGUID2(g, buf, 64);
    return wide(buf);
}

class WindowsNicTelemetry final : public INetworkTelemetry {
public:
    const char *apiName() const override { return "Windows IP Helper (GetIfTable2)"; }

    std::vector<NicInfo> listInterfaces() override
    {
        std::vector<NicInfo> out;
        PMIB_IF_TABLE2 table = nullptr;
        if (GetIfTable2(&table) != NO_ERROR || !table)
            return out;
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IF_ROW2 &r = table->Table[i];
            const bool ethernet = r.Type == IF_TYPE_ETHERNET_CSMACD;
            const bool physical = r.InterfaceAndOperStatusFlags.HardwareInterface != 0;
            // Filtrar filtros/pseudo-interfaces (p. ej. "-WFP Native MAC Layer LightWeight Filter").
            if (!ethernet || r.InterfaceAndOperStatusFlags.FilterInterface)
                continue;
            NicInfo n;
            n.id = guidString(r.InterfaceGuid);
            n.displayName = wide(r.Alias);
            n.description = wide(r.Description);
            n.isEthernet = ethernet;
            n.isPhysical = physical;
            if (r.PhysicalAddressLength == 6) {
                char mac[18];
                std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", r.PhysicalAddress[0],
                              r.PhysicalAddress[1], r.PhysicalAddress[2], r.PhysicalAddress[3], r.PhysicalAddress[4],
                              r.PhysicalAddress[5]);
                n.mac = mac;
            }
            bool dup = false;
            for (auto &e : out)
                dup |= e.id == n.id;
            if (!dup)
                out.push_back(std::move(n));
        }
        FreeMibTable(table);
        return out;
    }

    NicSample sample(const std::string &nicId) override
    {
        NicSample s;
        s.source = apiName();
        PMIB_IF_TABLE2 table = nullptr;
        if (GetIfTable2(&table) != NO_ERROR || !table)
            return s; // no observable
        for (ULONG i = 0; i < table->NumEntries; ++i) {
            const MIB_IF_ROW2 &r = table->Table[i];
            if (r.InterfaceAndOperStatusFlags.FilterInterface || guidString(r.InterfaceGuid) != nicId)
                continue;
            s.present = true;
            if (r.MediaConnectState == MediaConnectStateConnected)
                s.linkUp = true;
            else if (r.MediaConnectState == MediaConnectStateDisconnected)
                s.linkUp = false;
            // MediaConnectStateUnknown => sin dato (nullopt)
            if (s.linkUp.value_or(false) && r.ReceiveLinkSpeed && r.ReceiveLinkSpeed != ~0ULL)
                s.speedBps = r.ReceiveLinkSpeed;
            s.inErrors = r.InErrors;
            s.inDiscards = r.InDiscards;
            s.outErrors = r.OutErrors;
            s.outDiscards = r.OutDiscards;
            break;
        }
        FreeMibTable(table);
        return s;
    }
};

} // namespace

std::unique_ptr<INetworkTelemetry> createPlatformNetworkTelemetry()
{
    return std::make_unique<WindowsNicTelemetry>();
}

} // namespace sg
