#pragma once
// Cola SPSC (un productor, un consumidor) acotada y preasignada, sin locks ni asignaciones
// tras la construcción. Apta para el hilo de audio en tiempo real.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

namespace sg {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // relleno intencional para evitar false sharing
#endif

#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

template <class T> class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacityPow2Min)
    {
        std::size_t cap = 2;
        while (cap < capacityPow2Min)
            cap <<= 1;
        mask_ = cap - 1;
        slots_ = std::make_unique<T[]>(cap);
    }

    std::size_t capacity() const noexcept { return mask_ + 1; }

    // Productor. Devuelve false si está llena (el llamador cuenta el descarte).
    bool tryPush(const T &v) noexcept
    {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto r = read_.load(std::memory_order_acquire);
        if (w - r > mask_)
            return false;
        slots_[w & mask_] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }

    // Consumidor.
    bool tryPop(T &out) noexcept
    {
        const auto r = read_.load(std::memory_order_relaxed);
        const auto w = write_.load(std::memory_order_acquire);
        if (r == w)
            return false;
        out = std::move(slots_[r & mask_]);
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

    std::size_t sizeApprox() const noexcept
    {
        return static_cast<std::size_t>(write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire));
    }

private:
    std::unique_ptr<T[]> slots_;
    std::size_t mask_ = 0;
    alignas(kCacheLine) std::atomic<uint64_t> write_{0};
    alignas(kCacheLine) std::atomic<uint64_t> read_{0};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace sg
