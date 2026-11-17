#pragma once
// Anillo SPSC de bloques PCM float32 planares, preasignado. El productor es el hilo de
// captura (callback del driver); el consumidor es el trabajador que entrega a OBS.
#include "SpscQueue.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace sg {

enum BlockFlags : uint32_t {
    kBlockNone = 0,
    kBlockDiscontinuity = 1u << 0, // salto de timestamp o pérdida previa a este bloque
    kBlockFormatChanged = 1u << 1,
};

struct PcmBlock {
    uint32_t frames = 0;
    uint32_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t flags = 0;
    uint64_t timestampNs = 0;    // instante estimado del primer frame (reloj monotónico)
    int64_t captureTimeNs = 0;   // instante en que el callback recibió el bloque (para latencia medida)
    uint64_t sequence = 0;
    float *planes[8] = {};       // apuntan al almacenamiento interno del slot
};

class PcmBlockRing {
public:
    static constexpr uint32_t kMaxChannels = 8; // límite de OBS por fuente (MAX_AUDIO_CHANNELS)

    PcmBlockRing(uint32_t slots, uint32_t maxFramesPerBlock, uint32_t channels)
        : maxFrames_(maxFramesPerBlock),
          channels_(std::min(channels, kMaxChannels)),
          slotCount_(roundPow2(slots)),
          storage_(static_cast<std::size_t>(slotCount_) * maxFrames_ * channels_),
          blocks_(slotCount_),
          free_(slotCount_),
          ready_(slotCount_)
    {
        for (uint32_t i = 0; i < slotCount_; ++i) {
            for (uint32_t c = 0; c < channels_; ++c)
                blocks_[i].planes[c] = storage_.data() + (static_cast<std::size_t>(i) * channels_ + c) * maxFrames_;
            free_.tryPush(i);
        }
    }

    uint32_t maxFrames() const noexcept { return maxFrames_; }
    uint32_t channels() const noexcept { return channels_; }
    uint32_t slotCount() const noexcept { return slotCount_; }

    // --- Productor (tiempo real) ---
    // Obtiene un slot libre o nullptr si el anillo está lleno (overflow contabilizado).
    PcmBlock *acquireWrite() noexcept
    {
        uint32_t idx;
        if (!free_.tryPop(idx)) {
            overflows_.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        writing_ = idx;
        return &blocks_[idx];
    }
    void commitWrite() noexcept
    {
        ready_.tryPush(writing_); // nunca falla: misma capacidad que free_
    }

    // --- Consumidor ---
    const PcmBlock *peekRead() noexcept
    {
        if (reading_ != kNone)
            return &blocks_[reading_];
        uint32_t idx;
        if (!ready_.tryPop(idx))
            return nullptr;
        reading_ = idx;
        return &blocks_[idx];
    }
    void releaseRead() noexcept
    {
        if (reading_ == kNone)
            return;
        free_.tryPush(reading_);
        reading_ = kNone;
    }

    // Número de bloques listos para consumir (aproximado, para métricas de cola).
    std::size_t queuedBlocks() const noexcept { return ready_.sizeApprox(); }
    uint64_t overflows() const noexcept { return overflows_.load(std::memory_order_relaxed); }

private:
    static uint32_t roundPow2(uint32_t v)
    {
        uint32_t p = 2;
        while (p < v)
            p <<= 1;
        return p;
    }
    static constexpr uint32_t kNone = 0xffffffffu;

    uint32_t maxFrames_;
    uint32_t channels_;
    uint32_t slotCount_;
    std::vector<float> storage_;
    std::vector<PcmBlock> blocks_;
    SpscQueue<uint32_t> free_;  // consumidor -> productor
    SpscQueue<uint32_t> ready_; // productor -> consumidor
    uint32_t writing_ = kNone;
    uint32_t reading_ = kNone;
    std::atomic<uint64_t> overflows_{0};
};

} // namespace sg
