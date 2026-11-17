#pragma once
// Instantánea inmutable del estado observable de una fuente (SDD §2.1). La construye el
// HealthMonitor fuera del hilo de audio a partir de contadores atómicos.
#include "../audio/Metering.h"
#include "../common/Types.h"
#include "../platform/INetworkTelemetry.h"
#include "../transport/IAudioTransport.h"

#include <string>

namespace sg {

struct HealthSnapshot {
    int64_t nowNs = 0;
    std::string sourceName;
    Platform platform = currentPlatform();

    // Fuente
    bool sourceRunning = false; // el operador espera audio (fuente habilitada)
    bool armed = false;         // supervisión de silencio A01 activada para esta fuente

    // Transporte / driver
    std::string backend;
    std::string deviceName;
    bool deviceLooksSoundGrid = false;
    TransportState transportState = TransportState::Unavailable;
    ErrorCode lastError = ErrorCode::Ok;
    std::string lastErrorMessage;
    bool reconnecting = false;
    uint64_t reconnectAttempts = 0;
    uint32_t sampleRate = 0;
    uint32_t outChannels = 0;

    // PCM de esta fuente
    uint64_t framesWritten = 0;   // acumulado
    int64_t lastFrameNs = 0;      // instante del último bloque recibido
    MeterWindow meter;            // ventana desde la instantánea anterior
    uint64_t overflows = 0;       // acumulado
    uint64_t discontinuities = 0; // acumulado
    uint64_t driverOverloads = 0; // acumulado (si el driver los reporta)
    bool driverOverloadsObservable = false;

    // Latencias (cada una con su procedencia; ver A07)
    Observed<double> pluginLatencyMs;  // medido: callback del driver -> entrega a OBS
    Observed<double> queueDelayMs;     // estimado: ocupación de la cola interna
    Observed<double> driverLatencyMs;  // estimado: informado por el driver
    Observed<double> endToEndLatencyMs; // siempre NO DISPONIBLE sin instrumentación externa

    // Red
    bool nicSelected = false;
    std::string nicName; // alias visible; nunca IP/MAC
    NicSample nic;
    uint64_t expectedSpeedBps = 1'000'000'000;

    uint64_t dropoutsTotal() const
    {
        return overflows + discontinuities + (driverOverloadsObservable ? driverOverloads : 0);
    }
};

} // namespace sg
