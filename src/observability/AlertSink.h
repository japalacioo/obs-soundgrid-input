#pragma once
// Salida de alertas: despachador con cola acotada (nunca bloquea ni crece sin límite;
// cuenta los eventos descartados), historial en memoria para la UI y log JSONL rotativo.
// Privacidad: nunca se escriben direcciones IP/MAC ni nombres de sesión.
#include "AlertTypes.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sg {

std::string jsonEscape(const std::string &s);
std::string alertEventToJson(const AlertEvent &e);

class IAlertSink {
public:
    virtual ~IAlertSink() = default;
    virtual void onEvent(const AlertEvent &e) = 0; // hilo del despachador
};

// Historial acotado y consultable desde la UI.
class AlertHistory final : public IAlertSink {
public:
    explicit AlertHistory(std::size_t capacity = 500) : capacity_(capacity) {}
    void onEvent(const AlertEvent &e) override;
    std::vector<AlertEvent> recent(std::size_t max = 50) const;
    void clear();

private:
    mutable std::mutex mu_;
    std::deque<AlertEvent> events_;
    std::size_t capacity_;
};

// Log JSONL rotativo por tamaño con retención configurable.
class JsonlAlertLog final : public IAlertSink {
public:
    JsonlAlertLog(std::string path, uint64_t maxBytes = 5ull << 20, int keepFiles = 5);
    void onEvent(const AlertEvent &e) override;
    void writeLine(const std::string &jsonLine); // p. ej. exportación de diagnóstico
    const std::string &path() const { return path_; }
    uint64_t rotations() const { return rotations_; }

private:
    void rotateIfNeeded(std::size_t incoming);
    std::mutex mu_;
    std::string path_;
    uint64_t maxBytes_;
    int keep_;
    uint64_t rotations_ = 0;
};

// Sumidero basado en función (log de OBS, notificaciones de la UI, pruebas).
class CallbackSink final : public IAlertSink {
public:
    explicit CallbackSink(std::function<void(const AlertEvent &)> fn) : fn_(std::move(fn)) {}
    void onEvent(const AlertEvent &e) override { fn_(e); }

private:
    std::function<void(const AlertEvent &)> fn_;
};

class AlertDispatcher {
public:
    explicit AlertDispatcher(std::size_t capacity = 1024, bool startThread = true);
    ~AlertDispatcher();

    void addSink(std::shared_ptr<IAlertSink> s);
    // No bloquea: si la cola está llena, descarta y cuenta (T53).
    bool publish(const AlertEvent &e);
    // Procesa la cola en el hilo llamador (modo sin hilo, pruebas).
    std::size_t drain();
    void flush(); // espera a que la cola se vacíe (con límite de tiempo)

    uint64_t dropped() const { return dropped_.load(); }
    uint64_t delivered() const { return delivered_.load(); }
    std::size_t queued() const;

private:
    void run();
    std::size_t capacity_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<AlertEvent> queue_;
    std::vector<std::shared_ptr<IAlertSink>> sinks_;
    std::mutex sinksMu_;
    std::thread thread_;
    bool stop_ = false;
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> delivered_{0};
};

} // namespace sg
