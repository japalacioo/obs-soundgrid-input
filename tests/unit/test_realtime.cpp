// T48: el camino de tiempo real (DeviceHub::onCapture -> mapa -> anillo -> medidores y la
// conversión de formatos) no asigna memoria. Se reemplaza operator new en este binario y
// se cuentan las asignaciones SOLO mientras un hilo marca que está en "tiempo real".
// T49: un consumidor congelado (UI/OBS bloqueado) no bloquea el callback del driver.
#include "TestHelpers.h"
#include "audio/SampleConvert.h"
#include "core/SourceSession.h"
#include "transport/DeviceHub.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <algorithm>
#include <cstdio>
#include <thread>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace {
thread_local bool t_countAllocs = false;
std::atomic<uint64_t> g_rtAllocs{0};

void *rawAlloc(std::size_t n)
{
    if (t_countAllocs)
        g_rtAllocs.fetch_add(1, std::memory_order_relaxed);
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *rawAlignedAlloc(std::size_t n, std::size_t a)
{
    if (t_countAllocs)
        g_rtAllocs.fetch_add(1, std::memory_order_relaxed);
#if defined(_MSC_VER)
    if (void *p = _aligned_malloc(n ? n : 1, a))
        return p;
#else
    if (void *p = std::aligned_alloc(a, ((n ? n : 1) + a - 1) / a * a))
        return p;
#endif
    throw std::bad_alloc();
}
void rawAlignedFree(void *p)
{
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

void *operator new(std::size_t n) { return rawAlloc(n); }
void *operator new[](std::size_t n) { return rawAlloc(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void *operator new(std::size_t n, std::align_val_t a) { return rawAlignedAlloc(n, static_cast<std::size_t>(a)); }
void *operator new[](std::size_t n, std::align_val_t a) { return rawAlignedAlloc(n, static_cast<std::size_t>(a)); }
void operator delete(void *p, std::align_val_t) noexcept { rawAlignedFree(p); }
void operator delete[](void *p, std::align_val_t) noexcept { rawAlignedFree(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { rawAlignedFree(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { rawAlignedFree(p); }

using namespace sg;

TEST(T48_Realtime, CaptureCallbackPathDoesNotAllocate)
{
    for (uint32_t frames : {32u, 64u, 128u, 1024u, 4096u}) {
        VirtualClock clock;
        MockConfig mc;
        mc.realtime = false;
        mc.channels = 128;
        DeviceHub hub(std::make_unique<MockTransport>(mc, &clock), "mock:default", clock);
        auto a = hub.subscribe(ChannelMap::stereo(0, 1));
        auto b = hub.subscribe(ChannelMap::multi({0, 1, 2, 3, 4, 5, 6, 7}));
        auto c = hub.subscribe(ChannelMap::monoSum({100, 101, 127}));
        auto d = hub.subscribe(ChannelMap::stereo(126, 127));

        std::vector<std::vector<float>> data(128, std::vector<float>(frames, 0.25f));
        std::vector<const float *> planes;
        for (auto &v : data)
            planes.push_back(v.data());
        CaptureBlockView view;
        view.planes = planes.data();
        view.channels = 128;
        view.frames = frames;
        view.sampleRate = 48000;

        g_rtAllocs = 0;
        t_countAllocs = true;
        for (int i = 0; i < 500; ++i) {
            view.callbackTimeNs = clock.nowNs();
            hub.onCapture(view); // incluye anillos llenos (desbordes) a partir del bloque 64
            clock.advanceNs(static_cast<int64_t>(1e9 * frames / 48000));
        }
        t_countAllocs = false;
        EXPECT_EQ(g_rtAllocs.load(), 0u) << "asignaciones en el callback con buffer de " << frames;
        EXPECT_GT(a->stats().overflows.load(), 0u); // el camino de desborde también se ejercitó
    }
}

TEST(T48_Realtime, SampleConversionDoesNotAllocate)
{
    std::vector<uint8_t> in(4096 * 8, 0x11);
    std::vector<float> out(4096);
    g_rtAllocs = 0;
    t_countAllocs = true;
    for (SampleFormat f : {SampleFormat::Int16LE, SampleFormat::Int24LE, SampleFormat::Int32LE,
                           SampleFormat::Int32LE24, SampleFormat::Float32LE, SampleFormat::Float64LE,
                           SampleFormat::Int24BE, SampleFormat::Float32BE})
        convertToFloat(f, in.data(), out.data(), 4096);
    t_countAllocs = false;
    EXPECT_EQ(g_rtAllocs.load(), 0u);
}

TEST(T49_FrozenConsumer, BlockedOutputDoesNotBlockDriverCallback)
{
    SteadyClock &clock = SteadyClock::instance();
    CoreRuntime rt(clock, std::make_unique<MockNetworkTelemetry>(), "", true);
    MockConfig mc;
    mc.realtime = true;
    mc.bufferFrames = 128;
    rt.transports().setMockConfig(mc);
    std::atomic<bool> freeze{true};
    std::atomic<uint64_t> delivered{0};
    auto s = std::make_shared<SourceSession>(rt, "congelada", [&](const PcmBlock &) {
        while (freeze.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // OBS/UI bloqueado
        delivered.fetch_add(1);
    });
    SourceConfig cfg;
    cfg.deviceId = "mock:default";
    s->apply(cfg);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto *mock = dynamic_cast<MockTransport *>(&s->hub()->transport());
    ASSERT_NE(mock, nullptr);
    // 3 s en CI; el plan pide 30 s: ejecutar con SG_T49_SECONDS=30.
    const char *env = std::getenv("SG_T49_SECONDS");
    const int secs = env ? std::max(1, std::atoi(env)) : 3;
    const uint64_t before = mock->generatedBlocks();
    std::this_thread::sleep_for(std::chrono::seconds(secs)); // consumidor congelado
    const uint64_t during = mock->generatedBlocks() - before;
    // 128 frames a 48 kHz = 375 callbacks/s: el driver sigue a su ritmo.
    std::printf("T49: %d s congelado, %llu callbacks del driver (esperados ~%d)\n", secs,
                static_cast<unsigned long long>(during), secs * 375);
    EXPECT_GT(during, static_cast<uint64_t>(secs * 375 * 8 / 10));
    EXPECT_GT(s->subscription()->stats().overflows.load(), 0u); // descartes contabilizados, sin bloqueo
    freeze = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_GT(delivered.load(), 10u); // se recupera
    EXPECT_GE(s->subscription()->stats().discontinuities.load(), 1u); // el hueco queda marcado
    s.reset();
    rt.shutdown();
}
