// Adaptador ASIO para Windows. Ver AsioTransport.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

extern "C" {
#include "third_party/obs-win-asio/iasiodrv.h"
}

#include "AsioTransport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sg {

namespace {

constexpr uint32_t kMaxAsioChannels = 128;
constexpr int kMaxSlots = 8; // drivers ASIO abiertos simultáneamente en el proceso

std::string wideToUtf8(const wchar_t *w)
{
    if (!w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(n, 1)) - 1, '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring utf8ToWide(const std::string &s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 1)) - 1, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string ansiToUtf8(const char *a)
{
    // Los nombres de canal ASIO son char[] en la página de códigos del sistema.
    const int n = MultiByteToWideChar(CP_ACP, 0, a, -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 1)), L'\0');
    MultiByteToWideChar(CP_ACP, 0, a, -1, w.data(), n);
    return wideToUtf8(w.c_str());
}

SampleFormat mapAsioType(ASIOSampleType t)
{
    switch (t) {
    case ASIOSTInt16LSB: return SampleFormat::Int16LE;
    case ASIOSTInt24LSB: return SampleFormat::Int24LE;
    case ASIOSTInt32LSB: return SampleFormat::Int32LE;
    case ASIOSTInt32LSB24: return SampleFormat::Int32LE24;
    case ASIOSTFloat32LSB: return SampleFormat::Float32LE;
    case ASIOSTFloat64LSB: return SampleFormat::Float64LE;
    case ASIOSTInt16MSB: return SampleFormat::Int16BE;
    case ASIOSTInt24MSB: return SampleFormat::Int24BE;
    case ASIOSTInt32MSB: return SampleFormat::Int32BE;
    case ASIOSTFloat32MSB: return SampleFormat::Float32BE;
    default: return SampleFormat::Unsupported;
    }
}

bool looksLikeSoundGrid(const std::string &name)
{
    std::string l = name;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return l.find("soundgrid") != std::string::npos || l.find("waves") != std::string::npos;
}

struct RegistryDriver {
    std::string key;  // nombre de la subclave (estable)
    std::string name; // Description o clave
    std::wstring clsid;
};

std::vector<RegistryDriver> readAsioRegistry()
{
    std::vector<RegistryDriver> out;
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &root) != ERROR_SUCCESS)
        return out;
    for (DWORD i = 0;; ++i) {
        wchar_t sub[256];
        DWORD subLen = 256;
        if (RegEnumKeyExW(root, i, sub, &subLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY k = nullptr;
        if (RegOpenKeyExW(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS)
            continue;
        wchar_t clsid[128] = {};
        DWORD sz = sizeof(clsid);
        DWORD type = 0;
        const bool hasClsid = RegQueryValueExW(k, L"CLSID", nullptr, &type, reinterpret_cast<BYTE *>(clsid), &sz) ==
                                  ERROR_SUCCESS &&
                              type == REG_SZ;
        wchar_t desc[256] = {};
        sz = sizeof(desc);
        const bool hasDesc = RegQueryValueExW(k, L"Description", nullptr, &type, reinterpret_cast<BYTE *>(desc), &sz) ==
                                 ERROR_SUCCESS &&
                             type == REG_SZ;
        RegCloseKey(k);
        if (!hasClsid)
            continue;
        RegistryDriver d;
        d.key = wideToUtf8(sub);
        d.name = hasDesc && desc[0] ? wideToUtf8(desc) : d.key;
        d.clsid = clsid;
        out.push_back(std::move(d));
    }
    RegCloseKey(root);
    return out;
}

// ---------------------------------------------------------------- hilo COM

class ComThread {
public:
    ComThread()
    {
        thread_ = std::thread([this] { run(); });
        std::unique_lock lk(mu_);
        cv_.wait(lk, [this] { return threadId_ != 0; });
    }
    ~ComThread()
    {
        post(nullptr);
        if (thread_.joinable())
            thread_.join();
    }
    template <class F> auto call(F &&f) -> decltype(f())
    {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto fut = task->get_future();
        post([task] { (*task)(); });
        return fut.get();
    }

private:
    void post(std::function<void()> fn)
    {
        {
            std::lock_guard lk(mu_);
            queue_.push_back(std::move(fn));
        }
        PostThreadMessageW(threadId_, WM_APP, 0, 0);
    }
    void run()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // crea la cola de mensajes
        {
            std::lock_guard lk(mu_);
            threadId_ = GetCurrentThreadId();
        }
        cv_.notify_all();
        bool quit = false;
        while (!quit && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_APP && msg.hwnd == nullptr) {
                std::deque<std::function<void()>> work;
                {
                    std::lock_guard lk(mu_);
                    work.swap(queue_);
                }
                for (auto &fn : work) {
                    if (!fn) {
                        quit = true;
                        continue;
                    }
                    fn();
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg); // algunos drivers ASIO necesitan la bomba de mensajes
            }
        }
        if (SUCCEEDED(hr))
            CoUninitialize();
    }

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    DWORD threadId_ = 0;
};

class AsioTransport;
std::array<std::atomic<AsioTransport *>, kMaxSlots> g_slots{};

} // namespace

std::vector<DeviceInfo> enumerateAsioDrivers()
{
    std::vector<DeviceInfo> out;
    for (const auto &d : readAsioRegistry()) {
        DeviceInfo i;
        i.id = "asio:" + d.key;
        i.name = d.name;
        i.backend = "asio";
        i.looksLikeWavesSoundGrid = looksLikeSoundGrid(d.name) || looksLikeSoundGrid(d.key);
        out.push_back(std::move(i));
    }
    return out;
}

namespace {

class AsioTransport final : public IAudioTransport {
public:
    explicit AsioTransport(IClock *clock) : clock_(clock ? clock : &SteadyClock::instance()) {}
    ~AsioTransport() override { close(); }

    const char *backendName() const override { return "asio"; }

    TransportCapabilities capabilities() const override
    {
        TransportCapabilities c;
        c.canCapturePcm = true;
        c.requiresWavesDriver = isSoundGrid_;
        c.requiresHostConfiguration = isSoundGrid_; // NIC, Inventory y patch se hacen fuera
        c.isHardwareValidated = false;              // sin prueba HIL con LV1 V17
        return c;
    }

    std::vector<DeviceInfo> enumerateDevices() override { return enumerateAsioDrivers(); }

    std::vector<ChannelInfo> channels() const override
    {
        std::lock_guard lk(infoMu_);
        return channelInfo_;
    }

    Result open(const StreamConfig &cfg) override
    {
        if (open_)
            close();
        const std::string prefix = "asio:";
        if (cfg.deviceId.rfind(prefix, 0) != 0)
            return Result::fail(ErrorCode::NotFound, "identificador ASIO inválido: " + cfg.deviceId);
        const std::string key = cfg.deviceId.substr(prefix.size());
        RegistryDriver drv;
        bool found = false;
        for (auto &d : readAsioRegistry())
            if (d.key == key) {
                drv = d;
                found = true;
            }
        if (!found)
            return Result::fail(ErrorCode::NotFound, "driver ASIO no instalado: " + key);
        isSoundGrid_ = looksLikeSoundGrid(drv.name) || looksLikeSoundGrid(drv.key);
        driverName_ = drv.name;

        slot_ = -1;
        for (int i = 0; i < kMaxSlots; ++i) {
            AsioTransport *expected = nullptr;
            if (g_slots[i].compare_exchange_strong(expected, this)) {
                slot_ = i;
                break;
            }
        }
        if (slot_ < 0)
            return Result::fail(ErrorCode::Busy, "demasiados drivers ASIO abiertos en este proceso");

        if (!com_)
            com_ = std::make_unique<ComThread>();
        Result r = com_->call([&] { return openOnComThread(drv, cfg); });
        if (!r.ok()) {
            com_->call([&] { releaseOnComThread(); return 0; });
            g_slots[slot_].store(nullptr);
            slot_ = -1;
            state_ = TransportState::Error;
            setError(r);
            return r;
        }
        open_ = true;
        state_ = TransportState::Configured;
        return r;
    }

    Result start(ICaptureSink *sink) override
    {
        if (!open_)
            return Result::fail(ErrorCode::NotOpen, "start() sin open()");
        sink_.store(sink, std::memory_order_release);
        lastHalf_ = -1;
        const ASIOError e = com_->call([&] { return asio_->lpVtbl->start(asio_); });
        if (e != ASE_OK) {
            sink_.store(nullptr);
            Result r = Result::fail(ErrorCode::DriverError, "ASIO start() falló: " + errorText(e));
            setError(r);
            return r;
        }
        running_ = true;
        state_ = TransportState::Streaming;
        return Result::success();
    }

    void stop() override
    {
        if (!running_)
            return;
        com_->call([&] { return asio_->lpVtbl->stop(asio_); });
        running_ = false;
        sink_.store(nullptr, std::memory_order_seq_cst);
        // Por contrato ASIO, tras stop() no hay más bufferSwitch; esperamos por si un callback ya
        // estaba en curso (seq_cst: emparejado con la marca inCallback_ del hilo de audio).
        for (int i = 0; i < 200 && inCallback_.load(std::memory_order_seq_cst); ++i)
            Sleep(1);
        if (open_)
            state_ = TransportState::Configured;
    }

    void close() override
    {
        stop();
        if (com_ && asio_)
            com_->call([&] { releaseOnComThread(); return 0; });
        if (slot_ >= 0) {
            g_slots[slot_].store(nullptr);
            slot_ = -1;
        }
        open_ = false;
        if (state_ != TransportState::Error)
            state_ = TransportState::Discovered;
    }

    StreamFormat format() const override
    {
        std::lock_guard lk(infoMu_);
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
        s.overloadsObservable = canReportOverload_;
        s.conversionErrors = conversionErrors_.load();
        s.duplicateBuffers = duplicateBuffers_.load();
        s.timeInfoCallbacks = timeInfoCalls_.load();
        std::lock_guard lk(infoMu_);
        s.lastError = lastError_;
        s.lastErrorMessage = lastErrorMessage_;
        return s;
    }

    uint32_t takeEvents() override { return events_.exchange(0); }

    // --- Callbacks ASIO (estáticos por slot) ---
    template <int N> static void bufferSwitchCb(long index, long)
    {
        if (AsioTransport *t = g_slots[N].load(std::memory_order_acquire))
            t->onBufferSwitch(index);
    }
    template <int N> static ASIOTime *bufferSwitchTimeInfoCb(ASIOTime *, long index, long)
    {
        if (AsioTransport *t = g_slots[N].load(std::memory_order_acquire)) {
            t->timeInfoCalls_.fetch_add(1, std::memory_order_relaxed);
            t->onBufferSwitch(index);
        }
        return nullptr;
    }
    template <int N> static void sampleRateCb(ASIOSampleRate rate)
    {
        if (AsioTransport *t = g_slots[N].load(std::memory_order_acquire))
            t->onSampleRateChanged(rate);
    }
    template <int N> static long messageCb(long selector, long value, void *, double *)
    {
        if (AsioTransport *t = g_slots[N].load(std::memory_order_acquire))
            return t->onMessage(selector, value);
        return 0;
    }

private:
    static std::string errorText(ASIOError e)
    {
        switch (e) {
        case ASE_NotPresent: return "ASE_NotPresent (dispositivo no presente)";
        case ASE_HWMalfunction: return "ASE_HWMalfunction";
        case ASE_InvalidParameter: return "ASE_InvalidParameter";
        case ASE_InvalidMode: return "ASE_InvalidMode";
        case ASE_SPNotAdvancing: return "ASE_SPNotAdvancing (el reloj de muestras no avanza)";
        case ASE_NoClock: return "ASE_NoClock (sin reloj)";
        case ASE_NoMemory: return "ASE_NoMemory";
        default: return "código " + std::to_string(e);
        }
    }

    void setError(const Result &r)
    {
        std::lock_guard lk(infoMu_);
        lastError_ = r.code;
        lastErrorMessage_ = r.message;
    }

    ASIOCallbacks *callbacksForSlot(int slot)
    {
        static ASIOCallbacks table[kMaxSlots] = {
#define SG_CB(N) {&bufferSwitchCb<N>, &sampleRateCb<N>, &messageCb<N>, &bufferSwitchTimeInfoCb<N>}
            SG_CB(0), SG_CB(1), SG_CB(2), SG_CB(3), SG_CB(4), SG_CB(5), SG_CB(6), SG_CB(7)
#undef SG_CB
        };
        return &table[slot];
    }

    Result openOnComThread(const RegistryDriver &drv, const StreamConfig &cfg)
    {
        CLSID clsid{};
        if (CLSIDFromString(drv.clsid.c_str(), &clsid) != S_OK)
            return Result::fail(ErrorCode::NotFound, "CLSID inválido en el registro para " + drv.name);
        // Particularidad ASIO: el CLSID se usa también como IID.
        HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void **>(&asio_));
        if (FAILED(hr) || !asio_) {
            asio_ = nullptr;
            char buf[64];
            snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return Result::fail(ErrorCode::NotFound,
                                "no se pudo cargar el driver ASIO '" + drv.name + "' (HRESULT " + buf + ")");
        }
        if (asio_->lpVtbl->init(asio_, GetDesktopWindow()) != ASIOTrue) {
            char msg[128] = {};
            asio_->lpVtbl->getErrorMessage(asio_, msg);
            return Result::fail(ErrorCode::InitFailed, "el driver rechazó init(): " + ansiToUtf8(msg) +
                                                           " (¿driver ocupado, sin NIC configurada o sin hardware?)");
        }
        long numIn = 0, numOut = 0;
        ASIOError e = asio_->lpVtbl->getChannels(asio_, &numIn, &numOut);
        if (e != ASE_OK)
            return Result::fail(ErrorCode::DriverError, "getChannels(): " + errorText(e));
        if (numIn <= 0)
            return Result::fail(ErrorCode::NotSupported, "el driver no expone entradas (¿falta asignación/patch?)");
        const uint32_t inputs = std::min<uint32_t>(static_cast<uint32_t>(numIn), kMaxAsioChannels);

        double rate = 0;
        e = asio_->lpVtbl->getSampleRate(asio_, &rate);
        if (e != ASE_OK || rate <= 0)
            return Result::fail(ErrorCode::NotSupported, "getSampleRate(): " + errorText(e) +
                                                             " (el reloj de la red SoundGrid no está disponible)");
        if (cfg.preferredSampleRate && static_cast<uint32_t>(rate) != cfg.preferredSampleRate)
            return Result::fail(ErrorCode::FormatUnsupported,
                                "el driver está a " + std::to_string(static_cast<int>(rate)) +
                                    " Hz; el plugin no cambia el reloj de la sesión");

        long minSize = 0, maxSize = 0, prefSize = 0, gran = 0;
        e = asio_->lpVtbl->getBufferSize(asio_, &minSize, &maxSize, &prefSize, &gran);
        if (e != ASE_OK || prefSize <= 0)
            return Result::fail(ErrorCode::DriverError, "getBufferSize(): " + errorText(e));
        long bufSize = prefSize;
        if (cfg.preferredBufferFrames && static_cast<long>(cfg.preferredBufferFrames) >= minSize &&
            static_cast<long>(cfg.preferredBufferFrames) <= maxSize)
            bufSize = static_cast<long>(cfg.preferredBufferFrames);

        std::vector<ChannelInfo> infos;
        types_.assign(inputs, SampleFormat::Unsupported);
        for (uint32_t c = 0; c < inputs; ++c) {
            ASIOChannelInfo ci{};
            ci.channel = static_cast<long>(c);
            ci.isInput = ASIOTrue;
            std::string name = "Input " + std::to_string(c + 1);
            if (asio_->lpVtbl->getChannelInfo(asio_, &ci) == ASE_OK) {
                types_[c] = mapAsioType(ci.type);
                if (ci.name[0])
                    name = ansiToUtf8(ci.name);
            }
            infos.push_back({c, name, ChannelNameSource::DriverPort});
        }

        bufferInfos_.assign(inputs, ASIOBufferInfo{});
        for (uint32_t c = 0; c < inputs; ++c) {
            bufferInfos_[c].isInput = ASIOTrue;
            bufferInfos_[c].channelNum = static_cast<long>(c);
        }
        e = asio_->lpVtbl->createBuffers(asio_, bufferInfos_.data(), static_cast<long>(inputs), bufSize,
                                         callbacksForSlot(slot_));
        if (e != ASE_OK)
            return Result::fail(ErrorCode::DriverError, "createBuffers(): " + errorText(e));
        buffersCreated_ = true;

        long inLat = 0, outLat = 0;
        double latMs = -1;
        if (asio_->lpVtbl->getLatencies(asio_, &inLat, &outLat) == ASE_OK && inLat > 0)
            latMs = 1000.0 * inLat / rate;
        canReportOverload_ = asio_->lpVtbl->future(asio_, kAsioCanReportOverload, nullptr) == ASE_SUCCESS;
        postOutput_ = asio_->lpVtbl->outputReady(asio_) == ASE_OK;

        scratch_.assign(inputs, std::vector<float>(static_cast<size_t>(bufSize)));
        planes_.resize(inputs);
        for (uint32_t c = 0; c < inputs; ++c)
            planes_[c] = scratch_[c].data();
        rate_.store(static_cast<uint32_t>(rate));
        bufFrames_ = static_cast<uint32_t>(bufSize);
        channelCount_ = inputs;

        std::lock_guard lk(infoMu_);
        channelInfo_ = std::move(infos);
        format_.sampleRate = static_cast<uint32_t>(rate);
        format_.channels = inputs;
        format_.bufferFrames = static_cast<uint32_t>(bufSize);
        format_.driverFormat = inputs ? types_[0] : SampleFormat::Unsupported;
        format_.driverInputLatencyMs = latMs;
        lastError_ = ErrorCode::Ok;
        lastErrorMessage_.clear();
        return Result::success();
    }

    void releaseOnComThread()
    {
        if (!asio_)
            return;
        if (buffersCreated_)
            asio_->lpVtbl->disposeBuffers(asio_);
        buffersCreated_ = false;
        asio_->lpVtbl->Release(asio_);
        asio_ = nullptr;
    }

    // --- Tiempo real ---
    void onBufferSwitch(long index) noexcept
    {
        inCallback_.store(true, std::memory_order_seq_cst);
        IASIO *const drv = asio_; // copia local: close() lo pone a nullptr tras stop()
        const int64_t now = clock_->nowNs();
        const uint32_t frames = bufFrames_;
        const long half = index & 1;
        // El doble buffer ASIO debería alternar 0/1. Algunos drivers no lo hacen (p. ej.
        // ReaRoute sin REAPER abierto repite siempre la misma mitad). No se descarta nada
        // (perderíamos audio en drivers atípicos): se contabiliza como anomalía de diagnóstico;
        // si además el ritmo no es tiempo real, el suavizador de timestamps genera
        // discontinuidades y A08 lo hace visible.
        if (half == lastHalf_)
            duplicateBuffers_.fetch_add(1, std::memory_order_relaxed);
        lastHalf_ = half;
        for (uint32_t c = 0; c < channelCount_; ++c) {
            if (!convertToFloat(types_[c], bufferInfos_[c].buffers[half], planes_[c], frames))
                conversionErrors_.fetch_add(1, std::memory_order_relaxed);
        }
        callbacks_.fetch_add(1, std::memory_order_relaxed);
        frames_.fetch_add(frames, std::memory_order_relaxed);
        lastCallbackNs_.store(now, std::memory_order_relaxed);
        if (ICaptureSink *s = sink_.load(std::memory_order_seq_cst)) {
            CaptureBlockView v;
            v.planes = planes_.data();
            v.channels = channelCount_;
            v.frames = frames;
            v.sampleRate = rate_.load(std::memory_order_relaxed);
            v.callbackTimeNs = now;
            s->onCapture(v);
        }
        if (postOutput_ && drv)
            drv->lpVtbl->outputReady(drv);
        inCallback_.store(false, std::memory_order_seq_cst);
    }

    void onSampleRateChanged(double rate) noexcept
    {
        if (rate > 0 && static_cast<uint32_t>(rate) != rate_.load()) {
            events_.fetch_or(kEvtFormatChanged | kEvtResetRequested);
        }
    }

    long onMessage(long selector, long value) noexcept
    {
        switch (selector) {
        case kAsioSelectorSupported:
            return (value == kAsioResetRequest || value == kAsioEngineVersion || value == kAsioResyncRequest ||
                    value == kAsioLatenciesChanged || value == kAsioOverload || value == kAsioBufferSizeChange)
                       ? 1
                       : 0;
        case kAsioEngineVersion:
            return 2;
        case kAsioResetRequest:
        case kAsioBufferSizeChange:
            events_.fetch_or(kEvtResetRequested);
            return 1;
        case kAsioResyncRequest:
            events_.fetch_or(kEvtResyncRequested);
            return 1;
        case kAsioLatenciesChanged:
            return 1;
        case kAsioOverload:
            overloads_.fetch_add(1, std::memory_order_relaxed);
            events_.fetch_or(kEvtOverload);
            return 1;
        case kAsioSupportsTimeInfo:
        case kAsioSupportsTimeCode:
            return 0; // usamos bufferSwitch clásico
        default:
            return 0;
        }
    }

    IClock *clock_;
    std::unique_ptr<ComThread> com_;
    IASIO *asio_ = nullptr;
    int slot_ = -1;
    bool open_ = false;
    bool running_ = false;
    bool buffersCreated_ = false;
    bool isSoundGrid_ = false;
    bool canReportOverload_ = false;
    bool postOutput_ = false;
    std::string driverName_;

    // Datos usados en el callback (fijados antes de start)
    std::vector<ASIOBufferInfo> bufferInfos_;
    std::vector<SampleFormat> types_;
    std::vector<std::vector<float>> scratch_;
    std::vector<float *> planes_;
    uint32_t bufFrames_ = 0;
    uint32_t channelCount_ = 0;
    std::atomic<uint32_t> rate_{0};
    std::atomic<ICaptureSink *> sink_{nullptr};
    std::atomic<bool> inCallback_{false};

    std::atomic<uint64_t> callbacks_{0};
    std::atomic<uint64_t> frames_{0};
    std::atomic<int64_t> lastCallbackNs_{0};
    std::atomic<uint64_t> overloads_{0};
    std::atomic<uint64_t> conversionErrors_{0};
    std::atomic<uint64_t> duplicateBuffers_{0};
    std::atomic<uint64_t> timeInfoCalls_{0};
    long lastHalf_ = -1; // solo hilo de audio (reiniciado en start)
    std::atomic<uint32_t> events_{0};
    std::atomic<TransportState> state_{TransportState::Discovered};

    mutable std::mutex infoMu_;
    std::vector<ChannelInfo> channelInfo_;
    StreamFormat format_;
    ErrorCode lastError_ = ErrorCode::Ok;
    std::string lastErrorMessage_;
};

} // namespace

std::unique_ptr<IAudioTransport> createAsioTransport(IClock *clock)
{
    return std::make_unique<AsioTransport>(clock);
}

} // namespace sg
