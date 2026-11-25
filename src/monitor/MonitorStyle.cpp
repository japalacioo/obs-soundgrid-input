#include "MonitorStyle.h"

#include <algorithm>
#include <cmath>

namespace sg::monitor {

namespace {
// Grafito neutro, como Youlean Loudness Meter 2 (sin tintes azulados en las superficies).
const Theme kDark = {
    /*canvas*/ 0xFF1B1C1F, /*panel*/ 0xFF26272B, /*panelAlt*/ 0xFF1F2023, /*header*/ 0xFF2C2D31,
    /*line*/ 0xFF3A3C42, /*grid*/ 0xFF2D2F33, /*tickMajor*/ 0xFFB9BBC0, /*tickMinor*/ 0xFF55585F,
    /*text*/ 0xEBEBEB, /*textDim*/ 0x9C9C9C, /*textOnAccent*/ 0xFFFFFF,
    /*alert*/ 0xFFE0524B, /*muted*/ 0xFFB5832A, /*badgeOff*/ 0xFF34363B,
    /*card*/ 0xFF34363B, /*cardHi*/ 0xFF45484F,
    /*below*/ 0xFF6E95D6, /*within*/ 0xFF5FBF7F, /*above*/ 0xFFE0524B,
    /*belowDim*/ 0xFF557BB4, /*withinDim*/ 0xFF3F8A5E, /*aboveDim*/ 0xFFB04A44,
    /*targetBand*/ 0xFF2A2924, /*target*/ 0xFFF0A43A, /*barFill*/ 0xFFD6D7DA, /*momentaryLine*/ 0xFFF2F2F2,
    /*integratedLine*/ 0xFFF6D365, /*dynamics*/ 0xFF8EA6CC, /*wave*/ 0xFF7EA6E0,
};
const Theme kLight = {
    /*canvas*/ 0xFFE4E5E8, /*panel*/ 0xFFF2F3F5, /*panelAlt*/ 0xFFFFFFFF, /*header*/ 0xFFD6D8DC,
    /*line*/ 0xFFC6C9CF, /*grid*/ 0xFFE6E8EB, /*tickMajor*/ 0xFF4A4D55, /*tickMinor*/ 0xFFB3B7BE,
    /*text*/ 0x1E1E1E, /*textDim*/ 0x6A6A6A, /*textOnAccent*/ 0xFFFFFF,
    /*alert*/ 0xFFD64541, /*muted*/ 0xFFB7791F, /*badgeOff*/ 0xFFD9DCE1,
    /*card*/ 0xFFFFFFFF, /*cardHi*/ 0xFFE1E6EE,
    /*below*/ 0xFF3E73C9, /*within*/ 0xFF2E9E5B, /*above*/ 0xFFD64541,
    /*belowDim*/ 0xFFA3BCE6, /*withinDim*/ 0xFFA4D9B8, /*aboveDim*/ 0xFFEBA9A4,
    /*targetBand*/ 0xFFF7EFDF, /*target*/ 0xFFD9822B, /*barFill*/ 0xFF8E939C, /*momentaryLine*/ 0xFF2A2D33,
    /*integratedLine*/ 0xFFB88A00, /*dynamics*/ 0xFF6D84AA, /*wave*/ 0xFF3E73C9,
};
} // namespace

const Theme &theme(ThemeId id)
{
    return id == ThemeId::Light ? kLight : kDark;
}

uint32_t zoneColor(const Theme &t, loudness::TargetZone z, bool dim)
{
    switch (z) {
    case loudness::TargetZone::Below:
        return dim ? t.belowDim : t.below;
    case loudness::TargetZone::Within:
        return dim ? t.withinDim : t.within;
    case loudness::TargetZone::Above:
        return dim ? t.aboveDim : t.above;
    }
    return t.within;
}

double lufsFraction(double lufs)
{
    if (!std::isfinite(lufs))
        return 0.0;
    return std::clamp((lufs - kLufsMin) / (kLufsMax - kLufsMin), 0.0, 1.0);
}

std::vector<Tick> loudnessTicks()
{
    std::vector<Tick> out;
    for (int v = static_cast<int>(kLufsMax); v >= static_cast<int>(kLufsMin); v -= 2) {
        Tick t;
        t.db = v;
        t.fraction = lufsFraction(v);
        t.major = v % 10 == 0;
        if (t.major)
            t.label = v == 0 ? "0" : "\xE2\x88\x92" + std::to_string(-v);
        out.push_back(t);
    }
    return out;
}

std::vector<HistoryColumn> historyColumns(const std::vector<loudness::HistoryPoint> &points, std::size_t spanPoints,
                                          uint32_t width)
{
    std::vector<HistoryColumn> cols(width);
    if (!width || !spanPoints || points.empty())
        return cols;
    const std::size_t n = points.size();
    for (std::size_t j = 0; j < n; ++j) {
        const std::size_t age = n - 1 - j; // 0 = el punto más reciente
        if (age >= spanPoints)
            continue;
        // Columnas que cubre el punto: con más columnas que puntos, cada punto ocupa varias (sin huecos).
        const auto off0 = static_cast<uint32_t>(static_cast<double>(age) * width / static_cast<double>(spanPoints));
        auto off1 = static_cast<uint32_t>(std::ceil(static_cast<double>(age + 1) * width / static_cast<double>(spanPoints)));
        off1 = std::min(std::max(off1, off0 + 1), width);
        if (off0 >= width)
            continue;
        const auto &p = points[j];
        for (uint32_t off = off0; off < off1; ++off) {
        HistoryColumn &c = cols[width - 1 - off];
        if (std::isfinite(p.momentary)) {
            if (!c.has) {
                c.momentaryMin = c.momentaryMax = p.momentary;
            } else {
                c.momentaryMin = std::min(c.momentaryMin, p.momentary);
                c.momentaryMax = std::max(c.momentaryMax, p.momentary);
            }
            c.has = true;
        }
        // Los puntos llegan en orden: el último de la columna gana.
        if (std::isfinite(p.shortTerm)) {
            c.shortTerm = p.shortTerm;
            c.hasShortTerm = true;
        }
        if (std::isfinite(p.integrated)) {
            c.integrated = p.integrated;
            c.hasIntegrated = true;
        }
        if (std::isfinite(p.psr)) {
            c.psr = p.psr;
            c.hasPsr = true;
        }
        }
    }
    return cols;
}

Rect cardValue(const Rect &c)
{
    return {c.x, c.y + c.h * 0.06f, c.w, c.h * 0.54f};
}

Rect cardLabel(const Rect &c)
{
    return {c.x, c.y + c.h * 0.62f, c.w, c.h * 0.32f};
}

LoudnessLayout computeLoudnessLayout(float W, float H, bool compact, bool showDynamics, float textScale)
{
    LoudnessLayout l;
    l.compact = compact;
    l.canvas = {0, 0, W, H};
    const float s = std::clamp(textScale, 0.5f, 2.0f);
    // Mismas medidas base que la vista clásica (computeLayout): cabecera y márgenes idénticos.
    const float m = std::max(2.0f, std::min(W, H) * 0.035f);
    l.border = std::max(1.0f, H * 0.004f);
    l.radius = std::max(2.0f, H * 0.012f);
    l.fontPx = std::min(H * 0.07f, W * 0.04f) * s;
    l.smallFontPx = std::min(H * 0.048f, W * 0.027f) * s;
    l.bigFontPx = l.fontPx * (compact ? 2.2f : 1.5f);
    const float textRow = l.smallFontPx * 1.5f;
    const float gap = m * 0.5f;

    l.header = {m, m, W - 2 * m, std::max(l.fontPx * 1.6f, H * 0.1f)};
    const float badgeW = std::min(W * 0.14f, l.header.h * 2.6f);
    l.badge = {l.header.right() - badgeW, l.header.y, badgeW, l.header.h};
    // Píldoras de la cabecera (como la barra inferior de Youlean): objetivo y canales.
    const float pillH = l.header.h * 0.62f, pillY = l.header.y + (l.header.h - pillH) / 2;
    l.chanPill = {l.badge.x - gap - W * 0.085f, pillY, W * 0.085f, pillH};
    l.presetPill = {l.chanPill.x - gap - W * 0.2f, pillY, W * 0.2f, pillH};
    l.label = {l.header.x, l.header.y, std::max(1.0f, l.presetPill.x - l.header.x - gap), l.header.h};

    const float bodyTop = l.header.bottom() + m;
    const float bodyBottom = H * 0.83f; // debajo, el nombre de la escena del Multiview
    const float bodyH = std::max(1.0f, bodyBottom - bodyTop);
    const float plotTop = bodyTop + textRow;
    float plotBottom = bodyBottom - textRow;
    float dynH = 0;
    if (!compact && showDynamics) {
        dynH = std::max(1.0f, (plotBottom - plotTop) * 0.2f);
        plotBottom -= dynH + gap;
    }
    const float plotH = std::max(1.0f, plotBottom - plotTop);

    // Barra M con la escala a su izquierda.
    l.barPanel = {m, bodyTop, W * (compact ? 0.14f : 0.105f), bodyH};
    l.barTitle = {l.barPanel.x, bodyTop, l.barPanel.w, textRow};
    l.barUnit = {l.barPanel.x, plotBottom, l.barPanel.w, textRow};
    l.barScale = {l.barPanel.x, plotTop, l.barPanel.w * 0.46f, plotH};
    l.bar = {l.barScale.right(), plotTop, l.barPanel.w * 0.30f, plotH};
    l.stMarker = {l.bar.right(), plotTop, l.barPanel.w * 0.18f, plotH};

    // Tarjetas: integrated grande y una rejilla de 2 × 4 (o 3 en fila en modo compacto).
    const float rx = l.barPanel.right() + gap;
    const float readW = compact ? W - m - rx : W * 0.30f;
    l.readPanel = {rx, bodyTop, readW, bodyH};
    const float pad = gap;
    const float ix = l.readPanel.x + pad, iw = std::max(1.0f, l.readPanel.w - 2 * pad);
    const float avail = std::max(1.0f, l.readPanel.h - 2 * pad);
    const int gridRows = compact ? 1 : kReadoutRows / 2;
    const float bigUnits = compact ? 2.2f : 1.9f;
    const float unit = (avail - gap * gridRows) / (bigUnits + gridRows * (compact ? 1.4f : 1.0f));
    l.bigCard = {ix, l.readPanel.y + pad, iw, unit * bigUnits};
    l.bigValue = {l.bigCard.x, l.bigCard.y + l.bigCard.h * 0.04f, l.bigCard.w, l.bigCard.h * 0.62f};
    l.bigLabel = {l.bigCard.x, l.bigCard.y + l.bigCard.h * 0.68f, l.bigCard.w * 0.5f, l.bigCard.h * 0.26f};
    l.statusChip = {l.bigCard.x + l.bigCard.w * 0.52f, l.bigCard.y + l.bigCard.h * 0.68f, l.bigCard.w * 0.44f,
                    l.bigCard.h * 0.24f};
    float y = l.bigCard.bottom() + gap;
    if (compact) {
        const float cw = (iw - 2 * gap) / 3.0f, ch = std::max(1.0f, l.readPanel.bottom() - pad - y);
        for (int i = 0; i < 3; ++i)
            l.rows.push_back({ix + (cw + gap) * i, y, cw, ch});
        return l;
    }
    const float cw = (iw - gap) / 2.0f;
    for (int r = 0; r < gridRows; ++r) {
        for (int c = 0; c < 2; ++c)
            l.rows.push_back({ix + (cw + gap) * c, y, cw, unit});
        y += unit + gap;
    }

    // Historial: distribución a la izquierda, escala y gráfico (como Youlean).
    const float gx = l.readPanel.right() + gap;
    l.graphPanel = {gx, bodyTop, std::max(1.0f, W - m - gx), bodyH};
    const float histW = l.graphPanel.w * 0.12f;
    const float scaleW = l.graphPanel.w * 0.075f;
    l.histogram = {gx + pad, plotTop, histW, plotH};
    l.graphScale = {l.histogram.right(), plotTop, scaleW, plotH};
    l.graph = {l.graphScale.right(), plotTop, std::max(1.0f, l.graphPanel.right() - pad - l.graphScale.right()), plotH};
    if (dynH > 0)
        l.dynamics = {l.graph.x, plotBottom + gap, l.graph.w, dynH};
    l.timeAxis = {l.graph.x, bodyBottom - textRow, l.graph.w, textRow};
    return l;
}

} // namespace sg::monitor
