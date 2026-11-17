#pragma once
// Contrato de transporte de audio (SDD §2.1). Cada adaptador (Mock, ASIO, Core Audio y,
// solo si se autoriza, SoundGrid nativo) implementa esta interfaz.
//
// Contrato de hilos:
//  - enumerateDevices/open/start/stop/close/snapshot/takeEvents: hilo de control
//    (nunca el hilo de audio). stop() garantiza que, al volver, no habrá más callbacks.
//  - ICaptureSink::onCapture: hilo de audio del driver (tiempo real). Prohibido bloquear,
//    asignar memoria, escribir logs o hacer I/O.
#include "../audio/SampleConvert.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sg {

enum class ErrorCode {
    Ok = 0,
    NotFound,          // el dispositivo/driver no existe
    Busy,              // en uso por otra aplicación o por otro driver del proceso
    NotSupported,      // función no soportada por el adaptador
    InitFailed,        // el driver rechazó la inicialización
    FormatUnsupported, // frecuencia o formato no soportado
    DriverError,       // error genérico del driver
    NotOpen,
    NotImplemented,    // p. ej. NativeSoundGridTransport sin autorización
};
const char *errorCodeName(ErrorCode c);

struct Result {
    ErrorCode code = ErrorCode::Ok;
    std::string message;
    bool ok() const { return code == ErrorCode::Ok; }
    static Result success() { return {}; }
    static Result fail(ErrorCode c, std::string m) { return {c, std::move(m)}; }
};

// Capacidades declaradas por evidencia del adaptador, nunca por suposición.
struct TransportCapabilities {
    bool canCapturePcm = false;
    bool canDiscoverSoundGridNodes = false; // ningún adaptador actual lo hace
    bool canReadLv1ChannelNames = false;    // sin API pública conocida
    bool canControlPatching = false;        // prohibido por diseño (ADR-006)
    bool requiresWavesDriver = false;
    bool requiresHostConfiguration = false; // NIC/Inventory/patch fuera del plugin
    bool isHardwareValidated = false;       // solo true tras HIL con LV1 real
};

// Procedencia del nombre de un canal (IChannelCatalog).
enum class ChannelNameSource { DriverPort, UserProvided, VerifiedPatch, Lv1Reported };
const char *channelNameSourceName(ChannelNameSource s);

struct ChannelInfo {
    uint32_t index = 0;          // 0-based
    std::string name;            // p. ej. "Input 1" o el nombre del puerto del driver
    ChannelNameSource source = ChannelNameSource::DriverPort;
};

struct DeviceInfo {
    std::string id;      // "<backend>:<identificador estable>"
    std::string name;    // nombre visible del driver/dispositivo
    std::string backend; // "mock", "asio", "coreaudio"
    uint32_t inputChannels = 0;  // 0 si no se conoce sin abrir el driver
    bool channelsKnown = false;
    bool looksLikeWavesSoundGrid = false; // solo por nombre del driver; NO identifica una LV1
};

struct StreamConfig {
    std::string deviceId;
    uint32_t preferredSampleRate = 0;  // 0 = la del driver/sesión (no se fuerza)
    uint32_t preferredBufferFrames = 0; // 0 = la del driver
};

struct StreamFormat {
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t bufferFrames = 0;
    SampleFormat driverFormat = SampleFormat::Unsupported;
    double driverInputLatencyMs = -1; // informado por el driver (estimado); <0 = desconocido
};

enum class TransportState { Unavailable, Discovered, Configured, Streaming, Degraded, Reconnecting, Error };
const char *transportStateName(TransportState s);

// Eventos asíncronos que el driver señala (desde cualquier hilo) y el control consume.
enum TransportEventBits : uint32_t {
    kEvtNone = 0,
    kEvtDeviceLost = 1u << 0,
    kEvtFormatChanged = 1u << 1,
    kEvtResetRequested = 1u << 2,
    kEvtOverload = 1u << 3,
    kEvtDeviceListChanged = 1u << 4,
    kEvtResyncRequested = 1u << 5,
};

struct TransportStatus {
    TransportState state = TransportState::Unavailable;
    uint64_t callbacks = 0;
    uint64_t framesCaptured = 0;
    int64_t lastCallbackNs = 0;
    uint64_t overloads = 0;      // xruns reportados por el driver, si los reporta
    bool overloadsObservable = false;
    uint64_t conversionErrors = 0;
    uint64_t duplicateBuffers = 0;  // mismo medio buffer entregado dos veces (descartado)
    uint64_t timeInfoCallbacks = 0; // diagnóstico: llamadas por bufferSwitchTimeInfo
    ErrorCode lastError = ErrorCode::Ok;
    std::string lastErrorMessage;
};

// Vista de un bloque capturado, ya convertido a float32 planar.
struct CaptureBlockView {
    const float *const *planes = nullptr;
    uint32_t channels = 0;
    uint32_t frames = 0;
    uint32_t sampleRate = 0;
    int64_t callbackTimeNs = 0; // reloj monotónico al entrar al callback
    uint32_t flags = 0;         // BlockFlags
};

class ICaptureSink {
public:
    virtual ~ICaptureSink() = default;
    virtual void onCapture(const CaptureBlockView &block) noexcept = 0; // TIEMPO REAL
};

class IAudioTransport {
public:
    virtual ~IAudioTransport() = default;
    virtual const char *backendName() const = 0;
    virtual TransportCapabilities capabilities() const = 0;
    virtual std::vector<DeviceInfo> enumerateDevices() = 0;
    virtual std::vector<ChannelInfo> channels() const = 0; // del dispositivo abierto
    virtual Result open(const StreamConfig &cfg) = 0;
    virtual Result start(ICaptureSink *sink) = 0;
    virtual void stop() = 0;
    virtual void close() = 0;
    virtual StreamFormat format() const = 0;
    virtual TransportStatus snapshot() const = 0;
    virtual uint32_t takeEvents() = 0;
};

using TransportFactory = std::unique_ptr<IAudioTransport> (*)();

} // namespace sg
