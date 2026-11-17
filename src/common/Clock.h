#pragma once
// Reloj monotónico inyectable. Las duraciones usan siempre este reloj; la hora civil
// solo se usa para mostrar incidentes (ver WallClock).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace sg {

class IClock {
public:
    virtual ~IClock() = default;
    // Nanosegundos monotónicos. Debe ser seguro desde el hilo de audio (sin locks ni I/O).
    virtual int64_t nowNs() const noexcept = 0;
};

// std::chrono::steady_clock: en MSVC usa QueryPerformanceCounter y en macOS
// mach_absolute_time, la misma base que os_gettime_ns() de libobs.
class SteadyClock final : public IClock {
public:
    int64_t nowNs() const noexcept override
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    static SteadyClock &instance()
    {
        static SteadyClock c;
        return c;
    }
};

// Reloj virtual determinista para pruebas.
class VirtualClock final : public IClock {
public:
    explicit VirtualClock(int64_t startNs = 1'000'000'000) : t_(startNs) {}
    int64_t nowNs() const noexcept override { return t_.load(std::memory_order_acquire); }
    void advanceNs(int64_t d) noexcept { t_.fetch_add(d, std::memory_order_acq_rel); }
    void advanceMs(int64_t ms) noexcept { advanceNs(ms * 1'000'000); }
    void setNs(int64_t v) noexcept { t_.store(v, std::memory_order_release); }

private:
    std::atomic<int64_t> t_;
};

constexpr int64_t kNsPerMs = 1'000'000;
constexpr int64_t kNsPerSec = 1'000'000'000;

// Hora civil ISO-8601 UTC (solo para presentación y logs).
std::string wallClockIso8601Utc();

} // namespace sg
