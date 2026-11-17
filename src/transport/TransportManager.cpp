#include "TransportManager.h"

#if defined(_WIN32)
#include "../platform/windows/AsioTransport.h"
#elif defined(__APPLE__)
#include "../platform/macos/CoreAudioTransport.h"
#endif

#include <chrono>

namespace sg {

const char *errorCodeName(ErrorCode c)
{
    switch (c) {
    case ErrorCode::Ok: return "OK";
    case ErrorCode::NotFound: return "NOT_FOUND";
    case ErrorCode::Busy: return "BUSY";
    case ErrorCode::NotSupported: return "NOT_SUPPORTED";
    case ErrorCode::InitFailed: return "INIT_FAILED";
    case ErrorCode::FormatUnsupported: return "FORMAT_UNSUPPORTED";
    case ErrorCode::DriverError: return "DRIVER_ERROR";
    case ErrorCode::NotOpen: return "NOT_OPEN";
    case ErrorCode::NotImplemented: return "NOT_IMPLEMENTED";
    }
    return "?";
}

const char *transportStateName(TransportState s)
{
    switch (s) {
    case TransportState::Unavailable: return "UNAVAILABLE";
    case TransportState::Discovered: return "DISCOVERED";
    case TransportState::Configured: return "CONFIGURED";
    case TransportState::Streaming: return "STREAMING";
    case TransportState::Degraded: return "DEGRADED";
    case TransportState::Reconnecting: return "RECONNECTING";
    case TransportState::Error: return "ERROR";
    }
    return "?";
}

const char *channelNameSourceName(ChannelNameSource s)
{
    switch (s) {
    case ChannelNameSource::DriverPort: return "driver_port";
    case ChannelNameSource::UserProvided: return "user_provided";
    case ChannelNameSource::VerifiedPatch: return "verified_patch";
    case ChannelNameSource::Lv1Reported: return "lv1_named_channel";
    }
    return "?";
}

TransportManager::TransportManager(IClock &clock, RetryPolicy policy) : clock_(clock), policy_(policy)
{
    factories_["mock"] = [this] {
        MockConfig c = mockCfg_;
        return std::unique_ptr<IAudioTransport>(new MockTransport(c, &clock_));
    };
#if defined(_WIN32)
    factories_["asio"] = [this] { return createAsioTransport(&clock_); };
#elif defined(__APPLE__)
    factories_["coreaudio"] = [this] { return createCoreAudioTransport(&clock_); };
#endif
}

TransportManager::~TransportManager()
{
    stopControlThread();
    shutdownAll();
}

std::vector<std::string> TransportManager::backends() const
{
    std::lock_guard lk(mu_);
    std::vector<std::string> v;
    for (auto &[k, _] : factories_)
        v.push_back(k);
    return v;
}

void TransportManager::setFactory(const std::string &backend, Factory f)
{
    std::lock_guard lk(mu_);
    factories_[backend] = std::move(f);
}

std::string TransportManager::backendOf(const std::string &deviceId)
{
    const auto p = deviceId.find(':');
    return p == std::string::npos ? std::string() : deviceId.substr(0, p);
}

std::vector<DeviceInfo> TransportManager::enumerate(const std::string &backend)
{
#if defined(_WIN32)
    if (backend == "asio")
        return enumerateAsioDrivers(); // solo registro: no carga drivers
#elif defined(__APPLE__)
    if (backend == "coreaudio")
        return enumerateCoreAudioInputDevices();
#endif
    Factory f;
    {
        std::lock_guard lk(mu_);
        auto it = factories_.find(backend);
        if (it == factories_.end())
            return {};
        f = it->second;
    }
    auto t = f();
    return t ? t->enumerateDevices() : std::vector<DeviceInfo>{};
}

std::vector<DeviceInfo> TransportManager::enumerateAll()
{
    std::vector<DeviceInfo> all;
    for (const auto &b : backends()) {
        auto v = enumerate(b);
        all.insert(all.end(), v.begin(), v.end());
    }
    return all;
}

std::shared_ptr<DeviceHub> TransportManager::acquire(const std::string &deviceId)
{
    std::lock_guard lk(mu_);
    if (auto it = hubs_.find(deviceId); it != hubs_.end())
        return it->second;
    auto fit = factories_.find(backendOf(deviceId));
    if (fit == factories_.end())
        return nullptr;
    auto hub = std::make_shared<DeviceHub>(fit->second(), deviceId, clock_, policy_);
    hubs_[deviceId] = hub;
    return hub;
}

void TransportManager::releaseIfUnused(const std::shared_ptr<DeviceHub> &hub)
{
    if (!hub)
        return;
    std::shared_ptr<DeviceHub> victim;
    {
        std::lock_guard lk(mu_);
        auto it = hubs_.find(hub->deviceId());
        if (it == hubs_.end() || it->second != hub || hub->subscriberCount() != 0)
            return;
        victim = it->second;
        hubs_.erase(it);
    }
    victim->shutdown(); // fuera del lock del registro
}

std::vector<std::shared_ptr<DeviceHub>> TransportManager::hubs() const
{
    std::lock_guard lk(mu_);
    std::vector<std::shared_ptr<DeviceHub>> v;
    for (auto &[_, h] : hubs_)
        v.push_back(h);
    return v;
}

void TransportManager::pollOnce()
{
    for (auto &h : hubs())
        h->poll();
}

void TransportManager::startControlThread(int64_t periodMs)
{
    if (thread_.joinable())
        return;
    stop_ = false;
    thread_ = std::thread([this, periodMs] {
        while (!stop_) {
            pollOnce();
            std::this_thread::sleep_for(std::chrono::milliseconds(periodMs));
        }
    });
}

void TransportManager::stopControlThread()
{
    stop_ = true;
    if (thread_.joinable())
        thread_.join();
}

void TransportManager::shutdownAll()
{
    for (auto &h : hubs())
        h->shutdown();
    std::lock_guard lk(mu_);
    hubs_.clear();
}

} // namespace sg
