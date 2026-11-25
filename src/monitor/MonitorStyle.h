#pragma once
// Estilo común de las dos vistas del monitor del Multiview (clásica y sonoridad) y geometría de la
// vista de sonoridad, al estilo de Youlean Loudness Meter 2: fondos grafito neutros, lecturas en
// tarjetas redondeadas (valor grande, unidad pequeña y nombre debajo), barra M gris con la parte por
// encima del objetivo en rojo, objetivo en naranja, historial azul que pasa a rojo por encima del
// objetivo y distribución junto al historial. Núcleo sin libobs: tests/unit/test_loudness_view.cpp.
#include "monitor/AudioScope.h"
#include "monitor/Loudness.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sg::monitor {

// Colores 0xAARRGGBB (textos: 0xRRGGBB, sin alfa).
struct Theme {
    uint32_t canvas, panel, panelAlt, header, line, grid, tickMajor, tickMinor;
    uint32_t text, textDim, textOnAccent; // RGB sin alfa
    uint32_t alert, muted, badgeOff;
    uint32_t card, cardHi; // tarjetas de lecturas (cardHi: integrated)
    // Zonas respecto al objetivo de sonoridad: debajo, dentro y encima (topes de la barra, estado y textos).
    uint32_t below, within, above;
    // Rellenos: área de short-term del historial y del histograma.
    uint32_t belowDim, withinDim, aboveDim;
    uint32_t targetBand, target, barFill, momentaryLine, integratedLine, dynamics, wave;
};
enum class ThemeId { Dark = 0, Light = 1 };
const Theme &theme(ThemeId id);

uint32_t zoneColor(const Theme &t, loudness::TargetZone z, bool dim = false);

// ------------------------------------------------------------------ escala de sonoridad

constexpr double kLufsMin = -60.0; // misma escala vertical que el medidor clásico
constexpr double kLufsMax = 0.0;
double lufsFraction(double lufs); // 0 (abajo) .. 1 (arriba); no medido -> 0
// Marcas cada 2 LU, con número cada 10 LU ("0", "−10" … "−60").
std::vector<Tick> loudnessTicks();

// ------------------------------------------------------------------ historial

struct HistoryColumn {
    bool has = false;
    float momentaryMin = 0, momentaryMax = 0; // LUFS
    float shortTerm = 0;   // último short-term medido de la columna (NaN/−inf si ninguno)
    float integrated = 0;
    float psr = 0;
    bool hasShortTerm = false, hasIntegrated = false, hasPsr = false;
};
// width columnas para los últimos spanPoints puntos (10 por segundo); la última columna es lo más
// reciente. Con menos puntos que spanPoints, la izquierda queda vacía.
std::vector<HistoryColumn> historyColumns(const std::vector<loudness::HistoryPoint> &points, std::size_t spanPoints,
                                          uint32_t width);

// ------------------------------------------------------------------ distribución de la vista

struct LoudnessLayout {
    Rect canvas;
    Rect header, label, badge;   // misma cabecera que la vista clásica; badge = "TP"
    Rect presetPill, chanPill;   // en la cabecera: objetivo y canales
    Rect barPanel;
    Rect barScale;     // números de la escala (a la izquierda de la barra, como Youlean)
    Rect bar;          // momentary
    Rect stMarker;     // columna del triángulo de short-term (a la derecha de la barra)
    Rect barTitle;     // "M"
    Rect barUnit;      // "LUFS"
    Rect readPanel;    // tarjetas
    Rect bigCard;      // integrated
    Rect bigValue, bigLabel, statusChip; // dentro de bigCard
    std::vector<Rect> rows;  // tarjetas: short-term, momentary, true peak máx., LRA, PLR, PSR, DR, momentary máx.
    Rect graphPanel;
    Rect histogram;    // distribución (a la izquierda del historial, crece hacia la izquierda)
    Rect graphScale;   // números de la escala del historial
    Rect graph;        // historial
    Rect dynamics;     // PSR (opcional; alto 0 si no se muestra)
    Rect timeAxis;
    float fontPx = 0, smallFontPx = 0, bigFontPx = 0, border = 0, radius = 0;
    bool compact = false;
};
constexpr int kReadoutRows = 8;
// Partes de una tarjeta: valor (arriba) y nombre (abajo).
Rect cardValue(const Rect &card);
Rect cardLabel(const Rect &card);
// compact: barra, integrated grande y tres tarjetas, sin historial (modo "mini" para una escena).
LoudnessLayout computeLoudnessLayout(float width, float height, bool compact, bool showDynamics,
                                     float textScale = 1.0f);

} // namespace sg::monitor
