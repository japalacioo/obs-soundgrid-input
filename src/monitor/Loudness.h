#pragma once
// Medidor de sonoridad según ITU-R BS.1770-4 y EBU Tech 3341/3342 (núcleo sin libobs, probado en
// tests/unit/test_loudness.cpp). Solo mide: nunca modifica el audio.
//
//  - Ponderación K (filtro de estantería + paso alto RLB) con coeficientes calculados para la
//    frecuencia de muestreo real (iguales a los de la norma a 48 kHz).
//  - Sub-bloques de 100 ms con la energía ponderada por canal (G = 1,0 para L/R/C, 1,41 para los
//    envolventes; LFE excluido). Momentary = 4 sub-bloques (400 ms); short-term = 30 (3 s).
//  - Integrated: bloques de 400 ms con 75 % de solapamiento, puerta absoluta −70 LUFS y relativa
//    −10 LU. LRA (EBU Tech 3342): short-term cada 100 ms, puertas −70 LUFS y −20 LU, P95 − P10.
//    Ambos se guardan en histogramas de 0,01 LU con la energía exacta de cada bloque: memoria
//    fija aunque la emisión dure días.
//  - True peak: sobremuestreo ×4 u ×8 con un filtro polifásico (sinc con ventana de Kaiser).
//  - PLR = true peak máximo − integrated. PSR = true peak de los últimos 3 s − short-term.
//    DR = media de PSR (cada 100 ms, con short-term por encima de −70 LUFS) desde el último reset.
//
// No es seguro entre hilos: quien lo use (AudioMonitor) lo protege con su propio candado.
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sg::loudness {

constexpr double kNotMeasured = -std::numeric_limits<double>::infinity();
constexpr double kAbsoluteGate = -70.0; // LUFS
constexpr double kRelativeGateIntegrated = -10.0;
constexpr double kRelativeGateLra = -20.0;
constexpr int kSubBlocksPerSecond = 10;
constexpr int kMomentaryBlocks = 4;  // 400 ms
constexpr int kShortTermBlocks = 30; // 3 s

// Papel de cada canal en la suma ponderada de BS.1770-4.
enum class Role { Front, Surround, Lfe };
double channelWeight(Role r); // 1,0 · 1,41 · 0

// Biquad en forma directa I (doble precisión: estable a 38 Hz con 192 kHz).
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};
struct KWeighting {
    Biquad shelf; // etapa 1
    Biquad highpass; // etapa 2 (RLB)
};
KWeighting kWeightingFor(double sampleRate);

// Convierte la energía media ponderada en LUFS (−0,691 + 10·log10). 0 -> kNotMeasured.
double energyToLufs(double energy);
double lufsToEnergy(double lufs);

// ------------------------------------------------------------------ true peak

class TruePeakDetector {
public:
    // factor: 1 (sin sobremuestreo), 2, 4 u 8. tapsPerPhase: longitud de cada fase del filtro.
    void configure(int factor, int tapsPerPhase = 16);
    void reset();
    // Devuelve el |x| máximo (lineal) del bloque, incluidas las muestras interpoladas.
    float process(const float *x, uint32_t frames);
    int factor() const { return factor_; }
    const std::vector<std::vector<float>> &phases() const { return phases_; }

private:
    int factor_ = 4;
    int taps_ = 16;
    std::vector<std::vector<float>> phases_; // [fase][tap]
    std::vector<float> hist_;                // últimas taps_ muestras (circular)
    std::size_t pos_ = 0;
};

// ------------------------------------------------------------------ histogramas

// Histograma de 0,01 LU entre −70 y +10 LUFS con el número de bloques y su energía exacta.
class GatedHistogram {
public:
    static constexpr double kMin = kAbsoluteGate;
    static constexpr double kMax = 10.0;
    static constexpr double kStep = 0.01;
    static constexpr int kBins = static_cast<int>((kMax - kMin) / kStep);

    GatedHistogram();
    void add(double energy); // ignora los bloques por debajo de la puerta absoluta
    void clear();
    uint64_t count() const { return total_; }
    // Media de energía de los bloques por encima de una puerta relativa (dB respecto a su media).
    double gatedLoudness(double relativeGate) const;
    // Rango de sonoridad (P95 − P10) tras la puerta relativa; kNotMeasured si no hay bloques.
    double range(double relativeGate, double lowPct = 0.10, double highPct = 0.95) const;
    // Recuento por bandas de 1 LU entre fromLufs y toLufs (para el gráfico de distribución).
    std::vector<uint64_t> coarse(double fromLufs, double toLufs) const;

private:
    static int binOf(double lufs);
    double relativeThreshold(double relativeGate) const;
    std::vector<uint64_t> counts_;
    std::vector<double> energy_;
    uint64_t total_ = 0;
    double energySum_ = 0;
};

// ------------------------------------------------------------------ medidor

struct MeterConfig {
    double sampleRate = 48000;
    std::vector<Role> roles{Role::Front, Role::Front}; // un papel por canal de entrada
    int truePeakFactor = 4;
    int historySeconds = 600; // historial gráfico (puntos de 100 ms)
};

// Un punto del historial (cada 100 ms).
struct HistoryPoint {
    float momentary = static_cast<float>(kNotMeasured);
    float shortTerm = static_cast<float>(kNotMeasured);
    float integrated = static_cast<float>(kNotMeasured);
    float psr = static_cast<float>(kNotMeasured);
};

struct Readings {
    double momentary = kNotMeasured;
    double shortTerm = kNotMeasured;
    double integrated = kNotMeasured; // kNotMeasured hasta que haya un bloque por encima de −70 LUFS
    double lra = kNotMeasured;        // kNotMeasured hasta tener short-term válidos
    double truePeakMax = kNotMeasured;    // dBTP desde el último reset
    double truePeakRecent = kNotMeasured; // dBTP de los últimos 3 s
    double samplePeakMax = kNotMeasured;  // dBFS (sin sobremuestreo)
    double momentaryMax = kNotMeasured;
    double shortTermMax = kNotMeasured;
    double plr = kNotMeasured; // dB
    double psr = kNotMeasured; // dB
    double dr = kNotMeasured;  // dB (media de PSR)
    uint64_t gatedBlocks = 0;  // bloques de 400 ms por encima de la puerta absoluta
    double measuredSeconds = 0; // audio procesado desde el último reset
};

class LoudnessMeter {
public:
    explicit LoudnessMeter(MeterConfig cfg = {});
    void configure(const MeterConfig &cfg); // también reinicia
    const MeterConfig &config() const { return cfg_; }

    // planes: un puntero por canal de cfg.roles (float, sin entrelazar). nullptr = silencio.
    void process(const float *const *planes, uint32_t frames);
    void reset(); // integrated, LRA, máximos, historial y estadísticas

    const Readings &readings() const { return r_; }
    uint64_t subBlocks() const { return subBlocks_; } // cambia cada 100 ms de audio
    // Último punto del historial y los n más recientes (el último, el más nuevo).
    std::vector<HistoryPoint> history(std::size_t n) const;
    std::size_t historySize() const { return histCount_; }
    // Distribución de short-term (bandas de 1 LU) entre fromLufs y toLufs.
    std::vector<uint64_t> distribution(double fromLufs, double toLufs) const { return lraHist_.coarse(fromLufs, toLufs); }

private:
    void closeSubBlock();
    double windowEnergy(int blocks) const; // media ponderada de los últimos n sub-bloques

    MeterConfig cfg_;
    struct BiquadState {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };
    struct ChannelState {
        BiquadState shelf, highpass;
    };
    KWeighting k_;
    std::vector<ChannelState> state_;
    std::vector<double> weight_;
    std::vector<TruePeakDetector> tp_;
    uint32_t subBlockFrames_ = 4800;
    uint32_t inSubBlock_ = 0;
    double acc_ = 0;        // suma ponderada de cuadrados del sub-bloque en curso
    float subBlockTp_ = 0;  // true peak lineal del sub-bloque en curso
    float subBlockSp_ = 0;  // sample peak lineal
    std::array<double, kShortTermBlocks> ring_{}; // energía media de los últimos sub-bloques
    std::array<float, kShortTermBlocks> tpRing_{};
    uint64_t subBlocks_ = 0;
    GatedHistogram intHist_;
    GatedHistogram lraHist_;
    double psrSum_ = 0;
    uint64_t psrCount_ = 0;
    double tpMaxLin_ = 0, spMaxLin_ = 0;
    std::vector<HistoryPoint> hist_;
    std::size_t histHead_ = 0, histCount_ = 0;
    Readings r_;
};

// ------------------------------------------------------------------ objetivos

struct Preset {
    const char *id;
    const char *name;
    double target;     // LUFS integrados
    double tolerance;  // ± LU para "dentro del objetivo"
    double truePeak;   // dBTP máximo
    double lraMax;     // LU (0 = sin límite)
    bool lkfs;         // etiqueta LKFS (ATSC)
};
const std::vector<Preset> &presets(); // el último es "custom"
const Preset *findPreset(const std::string &id);

enum class TargetZone { Below, Within, Above };
TargetZone targetZone(double lufs, double target, double tolerance);

// "−23.0", "+1.2", "—" (no medido). Signo menos tipográfico, como formatDb.
std::string formatLoudness(double v, bool explicitPlus = false);

} // namespace sg::loudness
