#include "MockTransport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace sg {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

const char *mockSignalName(MockSignal s)
{
    switch (s) {
    case MockSignal::Silence: return "silence";
    case MockSignal::Sine: return "sine";
    case MockSignal::PinkNoise: return "pink";
    case MockSignal::Impulse: return "impulse";
    case MockSignal::ClippedSine: return "clipped_sine";
    }
    return "?";
}

MockTransport::MockTransport(MockConfig cfg, IClock *clock)
    : cfg_(cfg), clock_(clock ? clock : &SteadyClock::instance()), signal_(cfg.signal),
      levelLinear_(std::pow(10.0, cfg.levelDbfs / 20.0))
{
    cfg_.channels = std::clamp<uint32_t>(cfg_.channels, 1, 128);
    rate_ = cfg_.sampleRate;
}

MockTransport::~MockTransport()
{
    stop();
    close();
}

TransportCapabilities MockTransport::capabilities() const
{
    TransportCapabilities c;
    c.canCapturePcm = true; // PCM sintético
    return c;
}

std::vector<DeviceInfo> MockTransport::enumerateDevices()
{
    DeviceInfo d;
    d.id = "mock:default";
    d.name = "Simulador SoundGrid (" + std::to_string(cfg_.channels) + " canales)";
    d.backend = "mock";
    d.inputChannels = cfg_.channels;
    d.channelsKnown = true;
    return {d};
}

std::vector<ChannelInfo> MockTransport::channels() const
{
    std::vector<ChannelInfo> out;
    for (uint32_t i = 0; i < cfg_.channels; ++i)
        out.push_back({i, "Mock " + std::to_string(i + 1), ChannelNameSource::DriverPort});
    return out;
}

Result MockTransport::open(const StreamConfig &cfg)
{
    if (!cfg.deviceId.empty() && cfg.deviceId.rfind("mock:", 0) != 0)
        return Result::fail(ErrorCode::NotFound, "dispositivo mock desconocido: " + cfg.deviceId);
    switch (fault_.load()) {
    case MockFault::BusyOnOpen:
        state_ = TransportState::Error;
        return Result::fail(ErrorCode::Busy, "simulado: driver ocupado por otra aplicación");
    case MockFault::NotFoundOnOpen:
    case MockFault::Disconnect:
        state_ = TransportState::Error;
        return Result::fail(ErrorCode::NotFound, "simulado: dispositivo no disponible");
    default:
        break;
    }
    if (cfg.preferredSampleRate)
        rate_ = cfg.preferredSampleRate;
    if (cfg.preferredBufferFrames)
        cfg_.bufferFrames = cfg.preferredBufferFrames;
    buffers_.assign(cfg_.channels, std::vector<float>(cfg_.bufferFrames));
    planes_.resize(cfg_.channels);
    for (uint32_t c = 0; c < cfg_.channels; ++c)
        planes_[c] = buffers_[c].data();
    phase_.assign(cfg_.channels, 0.0);
    pink_.assign(cfg_.channels, std::array<double, 7>{});
    open_ = true;
    opens_.fetch_add(1);
    state_ = TransportState::Configured;
    return Result::success();
}

Result MockTransport::start(ICaptureSink *sink)
{
    if (!open_)
        return Result::fail(ErrorCode::NotOpen, "start() sin open()");
    if (running_)
        return Result::success();
    sink_ = sink;
    running_ = true;
    state_ = TransportState::Streaming;
    if (cfg_.realtime)
        thread_ = std::thread([this] { threadMain(); });
    return Result::success();
}

void MockTransport::stop()
{
    if (!running_.exchange(false))
        return;
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    sink_ = nullptr;
    if (open_)
        state_ = TransportState::Configured;
}

void MockTransport::close()
{
    stop();
    open_ = false;
    state_ = TransportState::Discovered;
}

StreamFormat MockTransport::format() const
{
    StreamFormat f;
    f.sampleRate = rate_;
    f.channels = cfg_.channels;
    f.bufferFrames = cfg_.bufferFrames;
    f.driverFormat = SampleFormat::Float32LE;
    f.driverInputLatencyMs = 1000.0 * cfg_.bufferFrames / std::max<uint32_t>(rate_, 1);
    return f;
}

TransportStatus MockTransport::snapshot() const
{
    TransportStatus s;
    s.state = state_.load();
    s.callbacks = callbacks_.load();
    s.framesCaptured = frames_.load();
    s.lastCallbackNs = lastCallbackNs_.load();
    s.overloads = overloads_.load();
    s.overloadsObservable = true;
    if (s.state == TransportState::Error) {
        s.lastError = ErrorCode::NotFound;
        s.lastErrorMessage = "simulado: dispositivo no disponible";
    }
    return s;
}

uint32_t MockTransport::takeEvents()
{
    return events_.exchange(0);
}

void MockTransport::setSignal(MockSignal s, double levelDbfs)
{
    signal_ = s;
    levelLinear_ = std::pow(10.0, levelDbfs / 20.0);
}

void MockTransport::setFault(MockFault f)
{
    fault_ = f;
    if (f == MockFault::Disconnect) {
        state_ = TransportState::Error;
        events_.fetch_or(kEvtDeviceLost);
    } else if (f == MockFault::None && open_) {
        if (state_ == TransportState::Error)
            state_ = running_ ? TransportState::Streaming : TransportState::Configured;
    }
}

void MockTransport::setDriftPpm(double ppm)
{
    driftPpm_ = ppm;
}

void MockTransport::changeSampleRate(uint32_t newRate)
{
    pendingRate_ = newRate;
}

void MockTransport::signalOverload()
{
    overloads_.fetch_add(1);
    events_.fetch_or(kEvtOverload);
}

void MockTransport::pump(uint32_t blocks)
{
    for (uint32_t i = 0; i < blocks && running_; ++i) {
        const uint32_t rate = rate_.load();
        const int64_t blockNs = static_cast<int64_t>(1e9 * cfg_.bufferFrames / rate);
        if (auto *vc = dynamic_cast<VirtualClock *>(clock_))
            vc->advanceNs(blockNs);
        const MockFault f = fault_.load();
        if (f == MockFault::Stall || f == MockFault::Disconnect)
            continue;
        produceBlock(clock_->nowNs());
    }
}

void MockTransport::threadMain()
{
    using namespace std::chrono;
    auto next = steady_clock::now();
    while (running_) {
        const uint32_t rate = rate_.load();
        const double ppm = driftPpm_.load();
        const double periodNs = 1e9 * cfg_.bufferFrames / rate / (1.0 + ppm * 1e-6);
        next += nanoseconds(static_cast<int64_t>(periodNs));
        {
            std::unique_lock lk(mu_);
            cv_.wait_until(lk, next, [this] { return !running_.load(); });
        }
        if (!running_)
            break;
        const MockFault f = fault_.load();
        if (f == MockFault::Stall || f == MockFault::Disconnect)
            continue;
        if (steady_clock::now() - next > milliseconds(200))
            next = steady_clock::now(); // tras una pausa larga, no ráfagas de recuperación
        produceBlock(clock_->nowNs());
    }
}

float MockTransport::nextSample(uint32_t ch)
{
    const double level = levelLinear_.load(std::memory_order_relaxed);
    const uint32_t rate = rate_.load(std::memory_order_relaxed);
    switch (signal_.load(std::memory_order_relaxed)) {
    case MockSignal::Silence:
        return 0.0f;
    case MockSignal::Sine:
    case MockSignal::ClippedSine: {
        const double f = cfg_.baseFrequency * (ch + 1);
        phase_[ch] += kTwoPi * f / rate;
        if (phase_[ch] > kTwoPi)
            phase_[ch] -= kTwoPi;
        double v = std::sin(phase_[ch]) * level;
        if (signal_.load(std::memory_order_relaxed) == MockSignal::ClippedSine)
            v = std::clamp(std::sin(phase_[ch]) * 2.0, -1.0, 1.0);
        return static_cast<float>(v);
    }
    case MockSignal::PinkNoise: {
        // Filtro de Paul Kellet sobre ruido blanco (xorshift).
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        const double white = (static_cast<double>(rng_) / 4294967295.0) * 2.0 - 1.0;
        auto &b = pink_[ch];
        b[0] = 0.99886 * b[0] + white * 0.0555179;
        b[1] = 0.99332 * b[1] + white * 0.0750759;
        b[2] = 0.96900 * b[2] + white * 0.1538520;
        b[3] = 0.86650 * b[3] + white * 0.3104856;
        b[4] = 0.55000 * b[4] + white * 0.5329522;
        b[5] = -0.7616 * b[5] - white * 0.0168980;
        const double pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362;
        b[6] = white * 0.115926;
        return static_cast<float>(pink * 0.11 * level);
    }
    case MockSignal::Impulse:
        // Un impulso por segundo en el primer frame de cada segundo.
        return (sampleCounter_ % rate) == 0 ? static_cast<float>(level) : 0.0f;
    }
    return 0.0f;
}

void MockTransport::produceBlock(int64_t callbackTimeNs)
{
    uint32_t flags = 0;
    if (const uint32_t nr = pendingRate_.exchange(0)) {
        rate_ = nr;
        events_.fetch_or(kEvtFormatChanged);
        flags |= 1u << 1; // kBlockFormatChanged
    }
    const uint32_t frames = cfg_.bufferFrames;
    for (uint32_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < cfg_.channels; ++c)
            buffers_[c][f] = nextSample(c);
        ++sampleCounter_;
    }
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    frames_.fetch_add(frames, std::memory_order_relaxed);
    lastCallbackNs_.store(callbackTimeNs, std::memory_order_relaxed);
    generated_.fetch_add(1, std::memory_order_relaxed);
    if (ICaptureSink *s = sink_.load(std::memory_order_acquire)) {
        CaptureBlockView v;
        v.planes = planes_.data();
        v.channels = cfg_.channels;
        v.frames = frames;
        v.sampleRate = rate_.load(std::memory_order_relaxed);
        v.callbackTimeNs = callbackTimeNs;
        v.flags = flags;
        s->onCapture(v);
    }
}

} // namespace sg
