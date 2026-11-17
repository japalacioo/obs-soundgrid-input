// Adaptador Core Audio (HAL). Ver CoreAudioTransport.h.
// ESTADO: escrito para macOS; NO compilado ni probado en este entorno (ver docs/TEST_REPORT.md).
#include "CoreAudioTransport.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace sg {

namespace {

constexpr uint32_t kMaxChannels = 128;

std::string cfToUtf8(CFStringRef s)
{
    if (!s)
        return {};
    const CFIndex len = CFStringGetLength(s);
    const CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<size_t>(max), '\0');
    if (!CFStringGetCString(s, out.data(), max, kCFStringEncodingUTF8))
        return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

AudioObjectPropertyAddress addr(AudioObjectPropertySelector sel,
                                AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal,
                                AudioObjectPropertyElement el = kAudioObjectPropertyElementMain)
{
    return {sel, scope, el};
}

std::string deviceStringProp(AudioObjectID dev, AudioObjectPropertySelector sel)
{
    CFStringRef s = nullptr;
    UInt32 size = sizeof(s);
    auto a = addr(sel);
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, &s) != noErr || !s)
        return {};
    std::string r = cfToUtf8(s);
    CFRelease(s);
    return r;
}

uint32_t inputChannelCount(AudioObjectID dev)
{
    auto a = addr(kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, nullptr, &size) != noErr || size == 0)
        return 0;
    std::vector<uint8_t> buf(size);
    auto *abl = reinterpret_cast<AudioBufferList *>(buf.data());
    if (AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, abl) != noErr)
        return 0;
    uint32_t n = 0;
    for (UInt32 i = 0; i < abl->mNumberBuffers; ++i)
        n += abl->mBuffers[i].mNumberChannels;
    return n;
}

std::vector<AudioObjectID> allDevices()
{
    auto a = addr(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr)
        return {};
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr)
        return {};
    return ids;
}

bool looksLikeSoundGrid(const std::string &name)
{
    std::string l = name;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return l.find("soundgrid") != std::string::npos || l.find("waves") != std::string::npos;
}

} // namespace

std::vector<DeviceInfo> enumerateCoreAudioInputDevices()
{
    std::vector<DeviceInfo> out;
    for (AudioObjectID dev : allDevices()) {
        const uint32_t ch = inputChannelCount(dev);
        if (ch == 0)
            continue;
        DeviceInfo d;
        d.name = deviceStringProp(dev, kAudioObjectPropertyName);
        d.id = "coreaudio:" + deviceStringProp(dev, kAudioDevicePropertyDeviceUID);
        d.backend = "coreaudio";
        d.inputChannels = ch;
        d.channelsKnown = true;
        d.looksLikeWavesSoundGrid = looksLikeSoundGrid(d.name);
        out.push_back(std::move(d));
    }
    return out;
}

namespace {

class CoreAudioTransport final : public IAudioTransport {
public:
    explicit CoreAudioTransport(IClock *clock) : clock_(clock ? clock : &SteadyClock::instance()) {}
    ~CoreAudioTransport() override { close(); }

    const char *backendName() const override { return "coreaudio"; }

    TransportCapabilities capabilities() const override
    {
        TransportCapabilities c;
        c.canCapturePcm = true;
        c.requiresWavesDriver = isSoundGrid_;
        c.requiresHostConfiguration = isSoundGrid_;
        return c;
    }

    std::vector<DeviceInfo> enumerateDevices() override { return enumerateCoreAudioInputDevices(); }

    std::vector<ChannelInfo> channels() const override
    {
        std::lock_guard lk(mu_);
        return channelInfo_;
    }

    Result open(const StreamConfig &cfg) override
    {
        close();
        const std::string prefix = "coreaudio:";
        if (cfg.deviceId.rfind(prefix, 0) != 0)
            return Result::fail(ErrorCode::NotFound, "identificador Core Audio inválido");
        const std::string uid = cfg.deviceId.substr(prefix.size());
        AudioObjectID found = kAudioObjectUnknown;
        for (AudioObjectID d : allDevices())
            if (deviceStringProp(d, kAudioDevicePropertyDeviceUID) == uid)
                found = d;
        if (found == kAudioObjectUnknown)
            return fail(Result::fail(ErrorCode::NotFound, "dispositivo Core Audio no presente: " + uid));
        dev_ = found;
        const std::string name = deviceStringProp(dev_, kAudioObjectPropertyName);
        isSoundGrid_ = looksLikeSoundGrid(name);

        const uint32_t ch = std::min(inputChannelCount(dev_), kMaxChannels);
        if (ch == 0)
            return fail(Result::fail(ErrorCode::NotSupported, "el dispositivo no expone entradas"));

        Float64 rate = 0;
        UInt32 size = sizeof(rate);
        auto ra = addr(kAudioDevicePropertyNominalSampleRate);
        if (AudioObjectGetPropertyData(dev_, &ra, 0, nullptr, &size, &rate) != noErr || rate <= 0)
            return fail(Result::fail(ErrorCode::DriverError, "no se pudo leer la frecuencia del dispositivo"));
        if (cfg.preferredSampleRate && static_cast<uint32_t>(rate) != cfg.preferredSampleRate)
            return fail(Result::fail(ErrorCode::FormatUnsupported, "el plugin no cambia la frecuencia del dispositivo"));

        UInt32 frames = 0;
        size = sizeof(frames);
        auto fa = addr(kAudioDevicePropertyBufferFrameSize);
        AudioObjectGetPropertyData(dev_, &fa, 0, nullptr, &size, &frames);
        AudioValueRange range{};
        size = sizeof(range);
        auto rr = addr(kAudioDevicePropertyBufferFrameSizeRange);
        AudioObjectGetPropertyData(dev_, &rr, 0, nullptr, &size, &range);
        const uint32_t maxFrames = std::max<uint32_t>({frames * 2, static_cast<uint32_t>(range.mMaximum), 4096});

        UInt32 latency = 0, safety = 0;
        size = sizeof(UInt32);
        auto la = addr(kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput);
        AudioObjectGetPropertyData(dev_, &la, 0, nullptr, &size, &latency);
        auto sa = addr(kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput);
        AudioObjectGetPropertyData(dev_, &sa, 0, nullptr, &size, &safety);

        std::vector<ChannelInfo> infos;
        for (uint32_t c = 0; c < ch; ++c) {
            std::string n = "Input " + std::to_string(c + 1);
            CFStringRef s = nullptr;
            UInt32 sz = sizeof(s);
            auto na = addr(kAudioObjectPropertyElementName, kAudioObjectPropertyScopeInput, c + 1);
            if (AudioObjectHasProperty(dev_, &na) &&
                AudioObjectGetPropertyData(dev_, &na, 0, nullptr, &sz, &s) == noErr && s) {
                const std::string v = cfToUtf8(s);
                CFRelease(s);
                if (!v.empty())
                    n = v;
            }
            infos.push_back({c, n, ChannelNameSource::DriverPort});
        }

        scratch_.assign(ch, std::vector<float>(maxFrames));
        planes_.resize(ch);
        for (uint32_t c = 0; c < ch; ++c)
            planes_[c] = scratch_[c].data();
        channelCount_ = ch;
        maxFrames_ = maxFrames;
        rate_ = static_cast<uint32_t>(rate);

        addListeners();
        {
            std::lock_guard lk(mu_);
            channelInfo_ = std::move(infos);
            format_.sampleRate = static_cast<uint32_t>(rate);
            format_.channels = ch;
            format_.bufferFrames = frames;
            format_.driverFormat = SampleFormat::Float32LE;
            format_.driverInputLatencyMs = 1000.0 * (latency + safety + frames) / rate;
            lastError_ = ErrorCode::Ok;
            lastErrorMessage_.clear();
        }
        open_ = true;
        state_ = TransportState::Configured;
        return Result::success();
    }

    Result start(ICaptureSink *sink) override
    {
        if (!open_)
            return Result::fail(ErrorCode::NotOpen, "start() sin open()");
        sink_.store(sink, std::memory_order_release);
        if (AudioDeviceCreateIOProcID(dev_, &CoreAudioTransport::ioProc, this, &procId_) != noErr || !procId_)
            return fail(Result::fail(ErrorCode::DriverError, "AudioDeviceCreateIOProcID falló"));
        if (AudioDeviceStart(dev_, procId_) != noErr) {
            AudioDeviceDestroyIOProcID(dev_, procId_);
            procId_ = nullptr;
            return fail(Result::fail(ErrorCode::Busy, "AudioDeviceStart falló (¿permiso de micrófono para OBS?)"));
        }
        running_ = true;
        state_ = TransportState::Streaming;
        return Result::success();
    }

    void stop() override
    {
        if (!running_)
            return;
        AudioDeviceStop(dev_, procId_); // síncrono fuera del hilo de E/S
        AudioDeviceDestroyIOProcID(dev_, procId_);
        procId_ = nullptr;
        running_ = false;
        sink_.store(nullptr, std::memory_order_release);
        if (open_)
            state_ = TransportState::Configured;
    }

    void close() override
    {
        stop();
        if (open_)
            removeListeners();
        open_ = false;
        dev_ = kAudioObjectUnknown;
        if (state_ != TransportState::Error)
            state_ = TransportState::Discovered;
    }

    StreamFormat format() const override
    {
        std::lock_guard lk(mu_);
        return format_;
    }

    TransportStatus snapshot() const override
    {
        TransportStatus s;
        s.state = state_.load();
        s.callbacks = callbacks_.load();
        s.framesCaptured = frames_.load();
        s.lastCallbackNs = lastCallbackNs_.load();
        s.overloads = overloads_.load();
        s.overloadsObservable = true; // kAudioDeviceProcessorOverload
        std::lock_guard lk(mu_);
        s.lastError = lastError_;
        s.lastErrorMessage = lastErrorMessage_;
        return s;
    }

    uint32_t takeEvents() override { return events_.exchange(0); }

private:
    Result fail(Result r)
    {
        state_ = TransportState::Error;
        std::lock_guard lk(mu_);
        lastError_ = r.code;
        lastErrorMessage_ = r.message;
        return r;
    }

    static constexpr AudioObjectPropertySelector kWatched[] = {
        kAudioDevicePropertyDeviceIsAlive, kAudioDevicePropertyNominalSampleRate,
        kAudioDeviceProcessorOverload, kAudioDevicePropertyStreamConfiguration};

    void addListeners()
    {
        for (auto sel : kWatched) {
            auto a = addr(sel, sel == kAudioDevicePropertyStreamConfiguration ? kAudioObjectPropertyScopeInput
                                                                               : kAudioObjectPropertyScopeGlobal);
            AudioObjectAddPropertyListener(dev_, &a, &CoreAudioTransport::listener, this);
        }
    }
    void removeListeners()
    {
        for (auto sel : kWatched) {
            auto a = addr(sel, sel == kAudioDevicePropertyStreamConfiguration ? kAudioObjectPropertyScopeInput
                                                                               : kAudioObjectPropertyScopeGlobal);
            AudioObjectRemovePropertyListener(dev_, &a, &CoreAudioTransport::listener, this);
        }
    }

    // Se ejecuta en un hilo de notificaciones de Core Audio (no en el de E/S).
    static OSStatus listener(AudioObjectID dev, UInt32 n, const AudioObjectPropertyAddress *addrs, void *ctx)
    {
        auto *self = static_cast<CoreAudioTransport *>(ctx);
        for (UInt32 i = 0; i < n; ++i) {
            switch (addrs[i].mSelector) {
            case kAudioDevicePropertyDeviceIsAlive: {
                UInt32 alive = 1, size = sizeof(alive);
                auto a = addr(kAudioDevicePropertyDeviceIsAlive);
                AudioObjectGetPropertyData(dev, &a, 0, nullptr, &size, &alive);
                if (!alive) {
                    self->state_ = TransportState::Error;
                    self->events_.fetch_or(kEvtDeviceLost);
                }
                break;
            }
            case kAudioDevicePropertyNominalSampleRate:
                self->events_.fetch_or(kEvtFormatChanged | kEvtResetRequested);
                break;
            case kAudioDeviceProcessorOverload:
                self->overloads_.fetch_add(1);
                self->events_.fetch_or(kEvtOverload);
                break;
            case kAudioDevicePropertyStreamConfiguration:
                self->events_.fetch_or(kEvtResetRequested);
                break;
            default:
                break;
            }
        }
        return noErr;
    }

    // Hilo de E/S de Core Audio (tiempo real).
    static OSStatus ioProc(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *in,
                           const AudioTimeStamp *inTime, AudioBufferList *, const AudioTimeStamp *, void *ctx)
    {
        auto *self = static_cast<CoreAudioTransport *>(ctx);
        if (!in || in->mNumberBuffers == 0)
            return noErr;
        const AudioBuffer &b0 = in->mBuffers[0];
        const uint32_t ch0 = b0.mNumberChannels ? b0.mNumberChannels : 1;
        uint32_t frames = b0.mDataByteSize / (sizeof(float) * ch0);
        frames = std::min(frames, self->maxFrames_);
        uint32_t c = 0;
        for (UInt32 bi = 0; bi < in->mNumberBuffers && c < self->channelCount_; ++bi) {
            const AudioBuffer &b = in->mBuffers[bi];
            const uint32_t nch = b.mNumberChannels ? b.mNumberChannels : 1;
            for (uint32_t k = 0; k < nch && c < self->channelCount_; ++k, ++c) {
                if (b.mData)
                    convertInterleavedChannelToFloat(SampleFormat::Float32LE, b.mData, k, nch, self->planes_[c], frames);
                else
                    std::fill_n(self->planes_[c], frames, 0.0f);
            }
        }
        for (; c < self->channelCount_; ++c)
            std::fill_n(self->planes_[c], frames, 0.0f);

        const uint32_t rate = self->rate_;
        int64_t now = self->clock_->nowNs();
        if (inTime && (inTime->mFlags & kAudioTimeStampHostTimeValid) && rate) {
            // mHostTime marca el primer frame; el hub espera "fin de bloque".
            now = static_cast<int64_t>(AudioConvertHostTimeToNanos(inTime->mHostTime)) +
                  static_cast<int64_t>(1e9 * frames / rate);
        }
        self->callbacks_.fetch_add(1, std::memory_order_relaxed);
        self->frames_.fetch_add(frames, std::memory_order_relaxed);
        self->lastCallbackNs_.store(now, std::memory_order_relaxed);
        if (ICaptureSink *s = self->sink_.load(std::memory_order_acquire)) {
            CaptureBlockView v;
            v.planes = self->planes_.data();
            v.channels = self->channelCount_;
            v.frames = frames;
            v.sampleRate = rate;
            v.callbackTimeNs = now;
            s->onCapture(v);
        }
        return noErr;
    }

    IClock *clock_;
    AudioObjectID dev_ = kAudioObjectUnknown;
    AudioDeviceIOProcID procId_ = nullptr;
    bool open_ = false;
    bool running_ = false;
    bool isSoundGrid_ = false;

    std::vector<std::vector<float>> scratch_;
    std::vector<float *> planes_;
    uint32_t channelCount_ = 0;
    uint32_t maxFrames_ = 0;
    uint32_t rate_ = 0;
    std::atomic<ICaptureSink *> sink_{nullptr};

    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> frames_{0};
    std::atomic<int64_t> lastCallbackNs_{0};
    std::atomic<uint64_t> overloads_{0};
    std::atomic<uint32_t> events_{0};
    std::atomic<TransportState> state_{TransportState::Discovered};

    mutable std::mutex mu_;
    std::vector<ChannelInfo> channelInfo_;
    StreamFormat format_;
    ErrorCode lastError_ = ErrorCode::Ok;
    std::string lastErrorMessage_;
};

} // namespace

std::unique_ptr<IAudioTransport> createCoreAudioTransport(IClock *clock)
{
    return std::make_unique<CoreAudioTransport>(clock);
}

} // namespace sg
