// Pruebas de DSP y buffers: T03, T04 (mapeo), T05 (conversión), T07 (buffers), T08 (timestamps).
#include "TestHelpers.h"
#include "audio/ChannelMap.h"
#include "audio/Metering.h"
#include "audio/PcmBlockRing.h"
#include "audio/SampleConvert.h"
#include "audio/SpscQueue.h"
#include "audio/TimestampSmoother.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

using namespace sg;

// ------------------------------------------------------------------ T05 conversión

TEST(T05_Conversion, Int16LittleEndianExact)
{
    const int16_t in[] = {0, 16384, -16384, 32767, -32768};
    float out[5];
    ASSERT_TRUE(convertToFloat(SampleFormat::Int16LE, in, out, 5));
    EXPECT_FLOAT_EQ(out[0], 0.0f);
    EXPECT_FLOAT_EQ(out[1], 0.5f);
    EXPECT_FLOAT_EQ(out[2], -0.5f);
    EXPECT_FLOAT_EQ(out[3], 32767.0f / 32768.0f);
    EXPECT_FLOAT_EQ(out[4], -1.0f);
}

TEST(T05_Conversion, Int16BigEndian)
{
    const uint8_t in[] = {0x40, 0x00, 0xC0, 0x00}; // 0x4000, 0xC000
    float out[2];
    ASSERT_TRUE(convertToFloat(SampleFormat::Int16BE, in, out, 2));
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], -0.5f);
}

TEST(T05_Conversion, Int24PackedBothEndians)
{
    const uint8_t le[] = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0, 0xFF, 0xFF, 0x7F};
    float out[3];
    ASSERT_TRUE(convertToFloat(SampleFormat::Int24LE, le, out, 3));
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], -0.5f);
    EXPECT_NEAR(out[2], 1.0f, 1e-6);
    const uint8_t be[] = {0x40, 0x00, 0x00, 0xC0, 0x00, 0x00};
    ASSERT_TRUE(convertToFloat(SampleFormat::Int24BE, be, out, 2));
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], -0.5f);
}

TEST(T05_Conversion, Int32AndInt32With24BitsLsb)
{
    const int32_t in32[] = {0x40000000, static_cast<int32_t>(0xC0000000)};
    float out[2];
    ASSERT_TRUE(convertToFloat(SampleFormat::Int32LE, in32, out, 2));
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], -0.5f);
    const int32_t in24[] = {0x00400000, 0x00C00000}; // 24 bits en la parte baja, signo en bit 23
    ASSERT_TRUE(convertToFloat(SampleFormat::Int32LE24, in24, out, 2));
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], -0.5f);
}

TEST(T05_Conversion, FloatPassThroughKeepsLevels)
{
    const float in[] = {0.25f, -0.75f, 1.0f};
    float out[3];
    ASSERT_TRUE(convertToFloat(SampleFormat::Float32LE, in, out, 3));
    EXPECT_EQ(std::memcmp(in, out, sizeof(in)), 0); // bit-exact
    const double d[] = {0.125, -1.0};
    ASSERT_TRUE(convertToFloat(SampleFormat::Float64LE, d, out, 2));
    EXPECT_FLOAT_EQ(out[0], 0.125f);
    EXPECT_FLOAT_EQ(out[1], -1.0f);
}

TEST(T05_Conversion, UnsupportedFormatGivesSafeSilence)
{
    const int16_t in[] = {1000, 2000};
    float out[2] = {9, 9};
    EXPECT_FALSE(convertToFloat(SampleFormat::Unsupported, in, out, 2));
    EXPECT_EQ(out[0], 0.0f);
    EXPECT_EQ(out[1], 0.0f);
}

TEST(T05_Conversion, InterleavedToPlanar)
{
    const float il[] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f}; // 2 frames x 3 canales
    float ch1[2];
    ASSERT_TRUE(convertInterleavedChannelToFloat(SampleFormat::Float32LE, il, 1, 3, ch1, 2));
    EXPECT_FLOAT_EQ(ch1[0], 0.2f);
    EXPECT_FLOAT_EQ(ch1[1], 0.5f);
}

// ------------------------------------------------------------------ T03/T04 mapeo

namespace {
struct Planes {
    std::vector<std::vector<float>> data;
    std::vector<const float *> ptrs;
    Planes(uint32_t ch, uint32_t frames)
    {
        data.assign(ch, std::vector<float>(frames));
        for (uint32_t c = 0; c < ch; ++c) {
            for (uint32_t f = 0; f < frames; ++f)
                data[c][f] = static_cast<float>(c + 1) * 0.01f + static_cast<float>(f) * 1e-5f;
            ptrs.push_back(data[c].data());
        }
    }
};
} // namespace

TEST(T03_ChannelMap, StereoSwapAndMono)
{
    Planes in(8, 64);
    std::vector<float> l(64), r(64);
    float *out[2] = {l.data(), r.data()};
    applyChannelMap(ChannelMap::stereo(1, 0), in.ptrs.data(), 8, 64, 0, out);
    EXPECT_FLOAT_EQ(l[0], in.data[1][0]); // L <- entrada 2 (swap)
    EXPECT_FLOAT_EQ(r[0], in.data[0][0]);

    std::vector<float> m(64);
    float *mo[1] = {m.data()};
    applyChannelMap(ChannelMap::monoSum({0, 1}), in.ptrs.data(), 8, 64, 0, mo);
    EXPECT_FLOAT_EQ(m[10], 0.5f * (in.data[0][10] + in.data[1][10])); // promedio, sin clipping
}

TEST(T04_ChannelMap, MultiChannelNoCrossTalk)
{
    Planes in(32, 128);
    std::vector<std::vector<float>> o(8, std::vector<float>(128));
    float *out[8];
    for (int i = 0; i < 8; ++i)
        out[i] = o[i].data();
    applyChannelMap(ChannelMap::multi({31, 0, 7, 15, 16, 23, 24, 2}), in.ptrs.data(), 32, 128, 0, out);
    const int expect[] = {31, 0, 7, 15, 16, 23, 24, 2};
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(std::memcmp(o[i].data(), in.data[expect[i]].data(), 128 * sizeof(float)), 0) << "salida " << i;
}

TEST(T04_ChannelMap, OutOfRangeIsSilenceAndValidationExplains)
{
    Planes in(2, 16);
    std::vector<float> l(16, 1.0f), r(16, 1.0f);
    float *out[2] = {l.data(), r.data()};
    applyChannelMap(ChannelMap::stereo(0, 5), in.ptrs.data(), 2, 16, 0, out);
    EXPECT_FLOAT_EQ(r[3], 0.0f);
    std::string why;
    EXPECT_FALSE(ChannelMap::stereo(0, 5).validate(2, &why));
    EXPECT_NE(why.find("fuera de rango"), std::string::npos);
    EXPECT_FALSE(ChannelMap::stereo(0, 1).validate(0, &why)); // 0 canales
    EXPECT_TRUE(ChannelMap::multi({0, 1, 2, 3, 4, 5, 6, 7}).validate(128, &why));
    EXPECT_EQ(ChannelMap::stereo(2, 3).describe(), "stereo(3,4)");
}

TEST(T04_ChannelMap, FrameOffsetForSplitBlocks)
{
    Planes in(2, 100);
    std::vector<float> l(10), r(10);
    float *out[2] = {l.data(), r.data()};
    applyChannelMap(ChannelMap::stereo(0, 1), in.ptrs.data(), 2, 10, 50, out);
    EXPECT_FLOAT_EQ(l[0], in.data[0][50]);
}

// ------------------------------------------------------------------ medidores

TEST(Metering, SineRmsAndPeakInDbfs)
{
    const int n = 48000;
    std::vector<float> x(n);
    const double a = dbfsToLinear(-20.0);
    for (int i = 0; i < n; ++i)
        x[i] = static_cast<float>(a * std::sin(2 * 3.141592653589793 * 1000.0 * i / 48000.0));
    const float *p[1] = {x.data()};
    MeterBank m;
    m.accumulate(p, 1, n);
    const MeterWindow w = m.takeWindow();
    EXPECT_NEAR(linearToDbfs(w.rms[0]), -23.01, 0.05); // RMS de un seno = A/√2
    EXPECT_NEAR(linearToDbfs(w.peak[0]), -20.0, 0.05);
    EXPECT_EQ(w.clippedSamples[0], 0u);
    const MeterWindow empty = m.takeWindow();
    EXPECT_EQ(empty.samples, 0u); // la ventana se reinicia
}

TEST(Metering, ClipCountAtMinusPointOneDbfs)
{
    std::vector<float> x = {0.5f, 0.99f, -1.0f, 0.2f};
    const float *p[1] = {x.data()};
    MeterBank m;
    m.accumulate(p, 1, 4);
    EXPECT_EQ(m.takeWindow().clippedSamples[0], 2u);
}

// ------------------------------------------------------------------ T07 buffers

TEST(T07_Buffers, SpscQueueFullAndEmpty)
{
    SpscQueue<int> q(4);
    int v;
    EXPECT_FALSE(q.tryPop(v));
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(q.tryPush(i));
    EXPECT_FALSE(q.tryPush(99)); // llena: el llamador cuenta el descarte
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(q.tryPop(v));
        EXPECT_EQ(v, i);
    }
}

TEST(T07_Buffers, RingOverflowIsCountedNotBlocking)
{
    PcmBlockRing ring(8, 256, 2);
    int written = 0;
    for (int i = 0; i < 20; ++i) {
        if (PcmBlock *b = ring.acquireWrite()) {
            b->frames = 256;
            ring.commitWrite();
            ++written;
        }
    }
    EXPECT_EQ(written, 8);
    EXPECT_EQ(ring.overflows(), 12u);
    EXPECT_EQ(ring.queuedBlocks(), 8u);
    while (ring.peekRead())
        ring.releaseRead();
    EXPECT_NE(ring.acquireWrite(), nullptr); // recupera capacidad tras consumir
}

TEST(T07_Buffers, ConcurrentProducerConsumerKeepsOrder)
{
    PcmBlockRing ring(16, 64, 1);
    constexpr uint64_t N = 20000;
    std::atomic<bool> done{false};
    uint64_t produced = 0, dropped = 0;
    std::thread prod([&] {
        for (uint64_t i = 0; i < N; ++i) {
            if (PcmBlock *b = ring.acquireWrite()) {
                b->sequence = i;
                b->planes[0][0] = static_cast<float>(i % 1000);
                ring.commitWrite();
                ++produced;
            } else {
                ++dropped;
                std::this_thread::yield();
            }
        }
        done = true;
    });
    uint64_t consumed = 0, last = 0;
    bool ordered = true;
    while (!done || ring.queuedBlocks()) {
        if (const PcmBlock *b = ring.peekRead()) {
            if (consumed && b->sequence <= last)
                ordered = false;
            if (b->planes[0][0] != static_cast<float>(b->sequence % 1000))
                ordered = false; // datos corruptos
            last = b->sequence;
            ++consumed;
            ring.releaseRead();
        }
    }
    prod.join();
    EXPECT_TRUE(ordered);
    EXPECT_EQ(consumed, produced);
    EXPECT_EQ(produced + dropped, N);
}

// ------------------------------------------------------------------ T08 timestamps

TEST(T08_Timestamps, MonotonicAndContinuousDespiteCallbackJitter)
{
    TimestampSmoother ts;
    const uint32_t frames = 256, rate = 48000;
    const double blockNs = 1e9 * frames / rate;
    int64_t t = 5'000'000'000;
    uint64_t prev = 0;
    for (int i = 0; i < 2000; ++i) {
        const int64_t jitter = ((i * 7919) % 5 - 2) * 1'000'000; // ±2 ms
        auto r = ts.next(t + jitter, frames, rate);
        if (i)
            EXPECT_GT(r.timestampNs, prev);
        if (i > 0)
            EXPECT_FALSE(r.discontinuity) << "bloque " << i;
        if (i > 1)
            EXPECT_NEAR(static_cast<double>(r.timestampNs - prev), blockNs, blockNs * 0.02);
        prev = r.timestampNs;
        t += static_cast<int64_t>(blockNs);
    }
    EXPECT_EQ(ts.resyncs(), 0u);
}

TEST(T08_Timestamps, LargeJumpReanchorsAndFlagsDiscontinuity)
{
    TimestampSmoother ts;
    auto a = ts.next(1'000'000'000, 480, 48000);
    auto b = ts.next(1'010'000'000, 480, 48000);
    EXPECT_FALSE(b.discontinuity);
    auto c = ts.next(1'500'000'000, 480, 48000); // hueco de 490 ms
    EXPECT_TRUE(c.discontinuity);
    EXPECT_EQ(c.timestampNs, 1'500'000'000u);
    EXPECT_EQ(ts.resyncs(), 1u);
    (void)a;
}

TEST(T06_SampleRate, RateChangeIsADiscontinuity)
{
    TimestampSmoother ts;
    ts.next(1'000'000'000, 480, 48000);
    auto r = ts.next(1'010'000'000, 960, 96000);
    EXPECT_TRUE(r.discontinuity);
}

TEST(T54_Timestamps, ZeroRateDoesNotCrashOrInvent)
{
    TimestampSmoother ts;
    auto r = ts.next(-5, 256, 0);
    EXPECT_EQ(r.timestampNs, 0u);
}
