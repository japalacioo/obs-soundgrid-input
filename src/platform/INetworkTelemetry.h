#pragma once
// Telemetría pasiva de la NIC (SDD-06): estado de enlace, velocidad negociada y contadores
// de error del sistema operativo. Sin ping, sin escaneos, sin tráfico sobre SoundGrid.
// Los valores no disponibles son std::nullopt (UNKNOWN), nunca cero.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sg {

struct NicInfo {
    std::string id;          // identificador estable (GUID en Windows, nombre BSD en macOS)
    std::string displayName; // alias visible (p. ej. "Ethernet 2")
    std::string description; // modelo del adaptador
    bool isPhysical = false;
    bool isEthernet = false;
    std::string mac; // "aa:bb:cc:dd:ee:ff" si se conoce; solo para emparejar, nunca se registra
};

struct NicSample {
    bool present = false;              // la NIC existe
    std::optional<bool> linkUp;        // carrier
    std::optional<uint64_t> speedBps;  // velocidad negociada
    std::optional<uint64_t> inErrors;
    std::optional<uint64_t> inDiscards;
    std::optional<uint64_t> outErrors;
    std::optional<uint64_t> outDiscards;
    const char *source = "";           // API usada (para la evidencia de la alerta)
};

class INetworkTelemetry {
public:
    virtual ~INetworkTelemetry() = default;
    virtual std::vector<NicInfo> listInterfaces() = 0;
    virtual NicSample sample(const std::string &nicId) = 0;
    virtual const char *apiName() const = 0;
};

// Implementación de la plataforma actual (Windows: IP Helper GetIfTable2; macOS:
// getifaddrs + SIOCGIFMEDIA). En otras plataformas devuelve un stub "no observable".
std::unique_ptr<INetworkTelemetry> createPlatformNetworkTelemetry();

// Telemetría simulada para pruebas (T29–T32).
class MockNetworkTelemetry final : public INetworkTelemetry {
public:
    NicSample current;
    std::vector<NicInfo> interfaces{{"mock-nic", "Mock Ethernet", "Simulated", true, true}};
    std::vector<NicInfo> listInterfaces() override { return interfaces; }
    NicSample sample(const std::string &) override { return current; }
    const char *apiName() const override { return "mock"; }
};

} // namespace sg
