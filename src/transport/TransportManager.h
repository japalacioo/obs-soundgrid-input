#pragma once
// Registro de DeviceHub compartidos por id de dispositivo y fábrica de transportes por
// backend. Un hilo de control llama a DeviceHub::poll() periódicamente (apertura,
// reconexión con backoff, cierre cuando no quedan fuentes).
#include "DeviceHub.h"
#include "MockTransport.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace sg {

class TransportManager {
public:
    using Factory = std::function<std::unique_ptr<IAudioTransport>()>;

    explicit TransportManager(IClock &clock, RetryPolicy policy = {});
    ~TransportManager();

    // Backends disponibles en esta plataforma: "mock" siempre; "asio" (Windows);
    // "coreaudio" (macOS).
    std::vector<std::string> backends() const;
    void setFactory(const std::string &backend, Factory f);
    void setMockConfig(const MockConfig &cfg) { mockCfg_ = cfg; }

    std::vector<DeviceInfo> enumerate(const std::string &backend);
    std::vector<DeviceInfo> enumerateAll();

    std::shared_ptr<DeviceHub> acquire(const std::string &deviceId);
    void releaseIfUnused(const std::shared_ptr<DeviceHub> &hub);
    std::vector<std::shared_ptr<DeviceHub>> hubs() const;

    void startControlThread(int64_t periodMs = 50);
    void stopControlThread();
    void pollOnce();
    void shutdownAll();

    static std::string backendOf(const std::string &deviceId);

private:
    IClock &clock_;
    RetryPolicy policy_;
    MockConfig mockCfg_;
    mutable std::mutex mu_;
    std::map<std::string, Factory> factories_;
    std::map<std::string, std::shared_ptr<DeviceHub>> hubs_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

} // namespace sg
