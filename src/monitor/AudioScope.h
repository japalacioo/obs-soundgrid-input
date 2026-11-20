#pragma once
// Monitor de audio para el Multiview (núcleo sin libobs, probado en tests/unit/test_audio_monitor.cpp).
//
//  - AudioScopeTap: recibe los bloques PCM de una fuente SoundGrid (hilo de salida de la fuente) y
//    guarda un historial por "cubos" de 5 ms con mín/máx/pico/suma de cuadrados por canal. El hilo
//    de render de OBS lee una copia (snapshot). Candado corto: el productor no es el hilo del driver.
//  - computeLevels: Peak y RMS en dBFS de una ventana de tiempo.
//  - MeterBallistics: caída del pico, peak hold y aviso de clip con los valores del medidor de OBS.
//  - waveformColumns: columnas mín/máx del osciloscopio; lo más reciente a la derecha y, al avanzar
//    el reloj, todo se desplaza a la izquierda. Los huecos de tiempo sin audio quedan vacíos.
//  - computeLayout / scaleTicks / meterZones: geometría y escala, independientes de la resolución.
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace sg::monitor {

// Valores por defecto del medidor de volumen de OBS (frontend/components/VolumeMeter.cpp).
constexpr double kMinDb = -60.0;     // fondo de escala
constexpr double kWarningDb = -20.0; // verde -> amarillo
constexpr double kErrorDb = -9.0;    // amarillo -> rojo
constexpr double kClipDb = -0.1;     // mismo umbral que A09 (MeterBank::kClipLinear)
constexpr double kNegInf = -std::numeric_limits<double>::infinity();
constexpr int64_t kNsPerMsM = 1'000'000;
constexpr int64_t kNsPerSecM = 1'000'000'000;

double ampToDb(double amplitude);                 // 0 -> -inf
double dbToFraction(double db, double minDb = kMinDb); // lineal en dB, 0..1 (como OBS)
// "−12.3", "−∞" o "0.0" (signo menos tipográfico para que se lea a distancia).
std::string formatDb(double db);

// ------------------------------------------------------------------ historial

struct ScopeBucket {
    int64_t startNs = 0; // instante del primer frame (reloj de OBS)
    int64_t endNs = 0;   // instante tras el último frame
    std::array<float, 2> min{0, 0};
    std::array<float, 2> max{0, 0};
    std::array<float, 2> peak{0, 0}; // |x| máximo
    std::array<double, 2> sumSq{0, 0};
    uint32_t frames = 0;
};

struct TapConfig {
    int64_t bucketNs = 5 * kNsPerMsM;
    int64_t historyNs = 30 * kNsPerSecM;
    int64_t gapToleranceNs = 20 * kNsPerMsM; // salto de timestamp que corta el cubo en curso
};

class AudioScopeTap {
public:
    explicit AudioScopeTap(TapConfig cfg = {});

    // Productor. planes: float planar; con 1 canal, R = L (mono). Solo se usan los 2 primeros.
    void push(const float *const *planes, uint32_t channels, uint32_t frames, uint32_t sampleRate,
              uint64_t firstFrameNs);

    struct Snapshot {
        std::vector<ScopeBucket> buckets; // ordenados por tiempo
        int64_t lastDataNs = 0;           // fin del último frame recibido (0 = nunca)
        uint32_t sampleRate = 0;
        uint32_t inputChannels = 0;       // canales de la fuente (1 = mono duplicado)
        uint64_t totalFrames = 0;
    };
    // Cubos que terminan después de fromNs (incluye el cubo en curso, aunque esté incompleto).
    Snapshot snapshot(int64_t fromNs) const;
    void reset();
    const TapConfig &config() const { return cfg_; }

private:
    void flushCurrent();
    int64_t bucketEnd(const ScopeBucket &b) const;

    TapConfig cfg_;
    mutable std::mutex mu_;
    std::vector<ScopeBucket> ring_;
    std::size_t head_ = 0;  // siguiente posición de escritura
    std::size_t count_ = 0;
    ScopeBucket cur_;
    bool curOpen_ = false;
    uint64_t curStartFrame_ = 0; // frame (desde el ancla) del primer frame de cur_
    uint32_t bucketFrames_ = 0;
    bool anchored_ = false;
    int64_t anchorNs_ = 0;
    uint64_t anchorFrames_ = 0; // frames recibidos desde el ancla
    int64_t lastDataNs_ = 0;
    uint32_t rate_ = 0;
    uint32_t inChannels_ = 0;
    uint64_t totalFrames_ = 0;
};

struct Levels {
    std::array<double, 2> peakDb{kNegInf, kNegInf};
    std::array<double, 2> rmsDb{kNegInf, kNegInf};
    uint64_t frames = 0; // 0 = sin audio en la ventana
};
// Peak (|x| máximo) y RMS de los cubos cuyo centro cae en (fromNs, toNs].
Levels computeLevels(const std::vector<ScopeBucket> &buckets, int64_t fromNs, int64_t toNs);

// ------------------------------------------------------------------ medidor

struct BallisticsConfig {
    double minDb = kMinDb;
    double decayDbPerSec = 11.76; // OBS: 20 dB en 1,7 s
    double peakHoldSec = 20.0;    // OBS: 20 s
    double clipDb = kClipDb;
    double clipHoldSec = 5.0;     // el aviso de clip se mantiene encendido este tiempo
};

class MeterBallistics {
public:
    explicit MeterBallistics(BallisticsConfig cfg = {}) : cfg_(cfg) {}
    void update(int64_t nowNs, double peakDb, double rmsDb);
    void reset();

    double displayPeakDb() const { return peak_; }
    double displayRmsDb() const { return rms_; }
    double holdDb() const { return hold_; }
    bool clipActive(int64_t nowNs) const;
    uint64_t clipEvents() const { return clipEvents_; }

private:
    BallisticsConfig cfg_;
    double peak_ = kNegInf;
    double rms_ = kNegInf;
    double hold_ = kNegInf;
    int64_t holdSinceNs_ = 0;
    int64_t lastNs_ = 0;
    int64_t lastClipNs_ = std::numeric_limits<int64_t>::min();
    bool wasClipping_ = false;
    uint64_t clipEvents_ = 0;
};

enum class Zone { Nominal, Warning, Error };
Zone zoneFor(double db);
struct ZoneSpan {
    Zone zone;
    double from; // fracción del medidor (0 = abajo)
    double to;
};
std::vector<ZoneSpan> meterZones(double minDb = kMinDb);
// Partes del medidor encendidas para un pico mostrado (colores brillantes por zona).
std::vector<ZoneSpan> litZones(double displayPeakDb, double minDb = kMinDb);

struct Tick {
    double db;
    double fraction;
    bool major;
    std::string label; // vacío en las marcas menores
};
std::vector<Tick> scaleTicks(double minDb = kMinDb, double majorStepDb = 5.0, double minorStepDb = 1.0);

// ------------------------------------------------------------------ osciloscopio

struct Column {
    bool has = false;
    float min = 0;
    float max = 0;
};
// width columnas que cubren (nowNs − spanNs, nowNs]; columna width−1 = lo más reciente.
std::vector<std::array<Column, 2>> waveformColumns(const std::vector<ScopeBucket> &buckets, int64_t nowNs,
                                                   int64_t spanNs, uint32_t width);

// ------------------------------------------------------------------ distribución

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(const Rect &o) const
    {
        return o.x >= x - 0.01f && o.y >= y - 0.01f && o.right() <= right() + 0.01f && o.bottom() <= bottom() + 0.01f;
    }
    bool overlaps(const Rect &o) const { return x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom(); }
};

struct MonitorLayout {
    Rect canvas;
    Rect header;     // franja superior: etiqueta de la fuente + CLIP
    Rect label;      // texto de la etiqueta
    Rect clipBadge;  // indicador de clip
    Rect meterPanel; // medidores + escala
    std::array<Rect, 2> meters; // L, R (barras verticales)
    Rect scale;      // números de la escala (entre la barra L y la R)
    std::array<Rect, 2> holdText;    // valor del peak hold encima de cada barra
    std::array<Rect, 2> channelText; // "L" / "R" debajo de cada barra
    Rect scopePanel;
    std::array<Rect, 2> lanes;   // osciloscopio L arriba, R abajo
    std::array<Rect, 2> readout; // "L  PEAK … dBFS   RMS … dBFS"
    Rect timeAxis;   // "−5 s … 0 s"
    float fontPx = 0;      // texto principal
    float smallFontPx = 0; // escala y lecturas
    float border = 0;
};
// Reserva abajo ~17 % libre: el Multiview dibuja ahí el nombre de la escena.
MonitorLayout computeLayout(float width, float height);

} // namespace sg::monitor
