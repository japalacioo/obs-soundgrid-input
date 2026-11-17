// Transporte simulado y DeviceHub: T02/T03/T04 (audio y mapeo), T06, T07, T10, T11, T13,
// T41, T52, T55 y reconexión con backoff (SDD-10).
#include "TestHelpers.h"
#include "transport/DeviceHub.h"
#include "transport/MockTransport.h"
#include "transport/TransportManager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace sg;
using namespace sgtest;

namespace {

struct Fixture {
    VirtualClock clock;
    MockTransport *mock = nullptr;
    std::unique_ptr<DeviceHub> hub;

    explicit Fixture(MockConfig cfg = {}, RetryPolicy pol = {})
    {
        cfg.realtime = false;
        auto t = std::make_unique<MockTransport>(cfg, &clock);
        mock = t.get();
        hub = std::make_unique<DeviceHub>(std::move(t), "mock:default", clock, pol);
    }

    // Recoge todo lo que hay en la cola de una suscripción, por canal de salida.
    static std::vector<std::vector<float>> drain(Subscription &s, uint32_t *rate = nullptr, uint32_t *flags = nullptr)
    {
        std::vector<std::vector<float>> out;
        while (const PcmBlock *b = s.peek()) {
            if (out.size() < b->channels)
                out.resize(b->channels);
            for (uint32_t c = 0; c < b->channels; ++c)
                out[c].insert(out[c].end(), b->planes[c], b->planes[c] + b->frames);
            if (rate)
                *rate = b->sampleRate;
            if (flags)
                *flags |= b->flags;
            s.release(0);
        }
        return out;
    }
};

const std::vector<double> kCands = {1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000};

} // namespace

TEST(T02_MockAudio, OneKhzSineReachesSubscriptionAtExpectedLevel)
{
    Fixture f;
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    f.hub->poll();
    std::vector<std::vector<float>> out(2);
    for (int i = 0; i < 4; ++i) { // ~1.07 s en tandas que caben en el anillo (64 bloques)
        f.mock->pump(50);
        auto part = Fixture::drain(*sub);
        ASSERT_EQ(part.size(), 2u);
        for (int c = 0; c < 2; ++c)
            out[c].insert(out[c].end(), part[c].begin(), part[c].end());
    }
    EXPECT_EQ(sub->stats().overflows.load(), 0u);
    ASSERT_GE(out[0].size(), 48000u);
    EXPECT_EQ(dominant(out[0], kCands, 48000), 1000.0); // entrada 1 = 1 kHz
    EXPECT_EQ(dominant(out[1], kCands, 48000), 2000.0); // entrada 2 = 2 kHz
    EXPECT_NEAR(rmsDb(out[0]), -23.0, 0.2);              // seno a -20 dBFS de pico
}

TEST(T03_MockAudio, SwapAndMonoDownmix)
{
    Fixture f;
    auto swapped = f.hub->subscribe(ChannelMap::stereo(1, 0));
    auto mono = f.hub->subscribe(ChannelMap::mono(2));
    f.hub->poll();
    f.mock->pump(100);
    auto s = Fixture::drain(*swapped);
    auto m = Fixture::drain(*mono);
    EXPECT_EQ(dominant(s[0], kCands, 48000), 2000.0);
    EXPECT_EQ(dominant(s[1], kCands, 48000), 1000.0);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(dominant(m[0], kCands, 48000), 3000.0);
}

TEST(T04_MockAudio, ThirtyTwoChannelsFanOutWithoutCrossTalk)
{
    MockConfig cfg;
    cfg.channels = 32;
    cfg.baseFrequency = 250.0; // canal c -> 250*(c+1) Hz, todos por debajo de Nyquist
    Fixture f(cfg);
    std::vector<std::shared_ptr<Subscription>> subs;
    for (int g = 0; g < 4; ++g) {
        const int16_t b = static_cast<int16_t>(g * 8);
        subs.push_back(f.hub->subscribe(ChannelMap::multi(
            {b, int16_t(b + 1), int16_t(b + 2), int16_t(b + 3), int16_t(b + 4), int16_t(b + 5), int16_t(b + 6),
             int16_t(b + 7)})));
    }
    f.hub->poll();
    f.mock->pump(100);
    std::vector<double> cands;
    for (int c = 0; c < 32; ++c)
        cands.push_back(250.0 * (c + 1));
    for (int g = 0; g < 4; ++g) {
        auto out = Fixture::drain(*subs[g]);
        ASSERT_EQ(out.size(), 8u);
        for (int k = 0; k < 8; ++k)
            EXPECT_EQ(dominant(out[k], cands, 48000), 250.0 * (g * 8 + k + 1)) << "fuente " << g << " canal " << k;
    }
    EXPECT_EQ(f.mock->opens(), 1u); // un único stream para las cuatro fuentes
}

TEST(T06_SampleRate, BlocksCarryDriverRateForObsResampling)
{
    MockConfig cfg;
    cfg.sampleRate = 96000;
    Fixture f(cfg);
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    f.hub->poll();
    f.mock->pump(10);
    uint32_t rate = 0, flags = 0;
    Fixture::drain(*sub, &rate, &flags);
    EXPECT_EQ(rate, 96000u);
    f.mock->changeSampleRate(48000);
    f.mock->pump(10);
    flags = 0;
    Fixture::drain(*sub, &rate, &flags);
    EXPECT_EQ(rate, 48000u);
    EXPECT_TRUE(flags & kBlockDiscontinuity); // el cambio se marca, no se oculta
}

TEST(T07_Hub, OverflowWithoutConsumerIsCountedAndMarked)
{
    Fixture f;
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    f.hub->poll();
    f.mock->pump(Subscription::kSlots + 30);
    EXPECT_EQ(sub->stats().overflows.load(), 30u);
    Fixture::drain(*sub);
    f.mock->pump(1);
    uint32_t flags = 0;
    Fixture::drain(*sub, nullptr, &flags);
    EXPECT_TRUE(flags & kBlockDiscontinuity);
    EXPECT_EQ(sub->stats().discontinuities.load(), 0u); // el desborde ya se contó en overflows (R12)
}

TEST(T11_Driver, BusyAndMissingDriverAreReportedAndRetried)
{
    RetryPolicy pol;
    pol.initialDelayMs = 100;
    pol.jitter = 0;
    Fixture f({}, pol);
    f.mock->setFault(MockFault::BusyOnOpen);
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    f.hub->poll();
    HubStatus st = f.hub->status();
    EXPECT_EQ(st.lastOpenError, ErrorCode::Busy);
    EXPECT_EQ(st.state, TransportState::Reconnecting);
    f.mock->setFault(MockFault::None);
    f.clock.advanceMs(150);
    f.hub->poll();
    st = f.hub->status();
    EXPECT_EQ(st.state, TransportState::Streaming);
    EXPECT_EQ(st.lastOpenError, ErrorCode::Ok);
    EXPECT_EQ(st.successfulReconnects, 1u);
}

TEST(SDD10_Reconnect, DisconnectThenRecoverWithExponentialBackoff)
{
    RetryPolicy pol;
    pol.initialDelayMs = 1000;
    pol.jitter = 0;
    pol.maxDelayMs = 8000;
    Fixture f({}, pol);
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    f.hub->poll();
    f.mock->pump(5);
    f.mock->setFault(MockFault::Disconnect);
    f.hub->poll(); // detecta la pérdida: cierra y programa reintento
    EXPECT_EQ(f.hub->status().state, TransportState::Reconnecting);

    std::vector<int64_t> retries;
    int64_t lastNext = f.hub->status().nextRetryNs;
    for (int i = 0; i < 400; ++i) { // 40 s virtuales
        f.clock.advanceMs(100);
        f.hub->poll();
        const int64_t n = f.hub->status().nextRetryNs;
        if (n != lastNext) {
            retries.push_back(n - lastNext);
            lastNext = n;
        }
    }
    ASSERT_GE(retries.size(), 3u);
    EXPECT_NEAR(retries[0] / 1e6, 2000, 150); // 1 s, 2 s, 4 s, 8 s (cap)
    EXPECT_NEAR(retries[1] / 1e6, 4000, 150);
    EXPECT_NEAR(retries[2] / 1e6, 8000, 150);
    EXPECT_GE(f.hub->status().reconnectAttempts, 3u);

    f.mock->setFault(MockFault::None);
    for (int i = 0; i < 100 && f.hub->status().state != TransportState::Streaming; ++i) {
        f.clock.advanceMs(100);
        f.hub->poll();
    }
    EXPECT_EQ(f.hub->status().state, TransportState::Streaming);
    f.mock->pump(5);
    EXPECT_FALSE(Fixture::drain(*sub).empty()); // el audio vuelve a fluir
}

TEST(SDD10_Reconnect, StalledDriverIsReopenedAfterTimeout)
{
    RetryPolicy pol;
    pol.stallTimeoutMs = 3000;
    pol.initialDelayMs = 100;
    pol.jitter = 0;
    Fixture f({}, pol);
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    f.hub->poll();
    f.mock->pump(2);
    f.hub->poll();
    f.mock->setFault(MockFault::Stall);
    for (int i = 0; i < 25; ++i) {
        f.clock.advanceMs(100);
        f.hub->poll();
    }
    EXPECT_EQ(f.mock->opens(), 1u); // 2.5 s: todavía no
    for (int i = 0; i < 10; ++i) {
        f.clock.advanceMs(100);
        f.hub->poll();
    }
    EXPECT_GE(f.mock->opens(), 2u); // reabierto el receptor (nunca la consola)
}

TEST(SDD10_Reconnect, ManualReconnectAfterMaxAttempts)
{
    RetryPolicy pol;
    pol.initialDelayMs = 10;
    pol.maxDelayMs = 10;
    pol.jitter = 0;
    pol.maxAttempts = 3;
    Fixture f({}, pol);
    f.mock->setFault(MockFault::NotFoundOnOpen);
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    for (int i = 0; i < 50; ++i) {
        f.clock.advanceMs(20);
        f.hub->poll();
    }
    const auto attempts = f.hub->status().reconnectAttempts;
    EXPECT_LE(attempts, 4u); // acotado: sin bucles infinitos
    f.mock->setFault(MockFault::None);
    f.clock.advanceMs(1000);
    f.hub->poll();
    EXPECT_NE(f.hub->status().state, TransportState::Streaming); // espera acción manual
    f.hub->requestReconnect();
    f.hub->poll();
    EXPECT_EQ(f.hub->status().state, TransportState::Streaming);
}

TEST(T52_MapChange, ChangingChannelsDoesNotReopenDriver)
{
    Fixture f;
    auto sub = f.hub->subscribe(ChannelMap::stereo(0, 1));
    f.hub->poll();
    f.mock->pump(20);
    Fixture::drain(*sub);
    f.hub->updateMap(sub, ChannelMap::stereo(4, 5));
    f.hub->poll();
    f.mock->pump(100);
    auto out = Fixture::drain(*sub);
    EXPECT_EQ(dominant(out[0], kCands, 48000), 5000.0);
    EXPECT_EQ(f.mock->opens(), 1u);
}

TEST(T55_SharedDevice, TwoSourcesSurviveDriverReopen)
{
    RetryPolicy pol;
    pol.initialDelayMs = 50;
    pol.jitter = 0;
    Fixture f({}, pol);
    auto a = f.hub->subscribe(ChannelMap::stereo(0, 1));
    auto b = f.hub->subscribe(ChannelMap::stereo(0, 1)); // fuente duplicada
    f.hub->poll();
    f.mock->pump(10);
    f.hub->requestReconnect();
    f.hub->poll();
    EXPECT_EQ(f.mock->opens(), 2u);
    Fixture::drain(*a);
    Fixture::drain(*b);
    f.mock->pump(10);
    EXPECT_FALSE(Fixture::drain(*a).empty());
    EXPECT_FALSE(Fixture::drain(*b).empty());
}

TEST(T10_T41_Stress, HundredSubscribeCyclesWhileStreamingRealtime)
{
    MockConfig cfg;
    cfg.realtime = true;
    cfg.bufferFrames = 64; // callbacks frecuentes para estresar la carrera RT/control
    auto t = std::make_unique<MockTransport>(cfg);
    MockTransport *mock = t.get();
    DeviceHub hub(std::move(t), "mock:default", SteadyClock::instance());
    auto keep = hub.subscribe(ChannelMap::stereo(0, 1));
    hub.poll();
    for (int i = 0; i < 100; ++i) {
        auto s = hub.subscribe(ChannelMap::multi({0, 1, 2, 3}));
        ASSERT_NE(s, nullptr);
        hub.updateMap(s, ChannelMap::mono(i % 8));
        std::this_thread::sleep_for(std::chrono::microseconds(300));
        hub.unsubscribe(s);
        while (keep->peek())
            keep->release(0);
    }
    EXPECT_EQ(hub.subscriberCount(), 1u);
    EXPECT_GT(mock->generatedBlocks(), 0u);
    hub.shutdown();
}

TEST(TransportManager, SharesHubPerDeviceAndReleasesWhenUnused)
{
    VirtualClock clock;
    TransportManager tm(clock);
    auto h1 = tm.acquire("mock:default");
    auto h2 = tm.acquire("mock:default");
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(tm.acquire("nope:x"), nullptr);
    auto s = h1->subscribe(ChannelMap::stereo(0, 1));
    tm.releaseIfUnused(h1);
    EXPECT_EQ(tm.hubs().size(), 1u); // aún tiene suscriptor
    h1->unsubscribe(s);
    tm.releaseIfUnused(h1);
    EXPECT_TRUE(tm.hubs().empty());
    EXPECT_FALSE(tm.enumerate("mock").empty());
}
