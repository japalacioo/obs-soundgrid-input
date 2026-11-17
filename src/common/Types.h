#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace sg {

enum class Platform { Windows, MacOS, Other };

constexpr Platform currentPlatform()
{
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Other;
#endif
}

const char *platformName(Platform p);

enum class Severity { Info, Warn, Critical };
const char *severityName(Severity s);

// Procedencia de un valor mostrado al operador: nunca se presenta un valor
// estimado o no observable como si fuera medido.
enum class Confidence { Measured, Estimated, NotObservable };
const char *confidenceName(Confidence c);

// Valor observable con su procedencia. Si value está vacío, el dato NO está disponible
// (se muestra como UNKNOWN / NO DISPONIBLE, nunca como cero).
template <class T> struct Observed {
    std::optional<T> value;
    Confidence confidence = Confidence::NotObservable;
    const char *source = "";

    static Observed measured(T v, const char *src) { return {v, Confidence::Measured, src}; }
    static Observed estimated(T v, const char *src) { return {v, Confidence::Estimated, src}; }
    static Observed unknown(const char *src = "") { return {std::nullopt, Confidence::NotObservable, src}; }
    bool known() const { return value.has_value(); }
};

// Conversión de niveles lineales a dBFS (0 lineal -> -inf representado como -200 dB).
double linearToDbfs(double linear);
double dbfsToLinear(double db);

} // namespace sg
