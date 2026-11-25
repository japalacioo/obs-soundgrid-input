// Estilo común de las vistas del monitor y geometría de la vista de sonoridad (src/monitor/MonitorStyle.*).
#include "monitor/MonitorStyle.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace sg::monitor;
using sg::loudness::HistoryPoint;
using sg::loudness::TargetZone;

namespace {
const std::pair<float, float> kSizes[] = {{1920, 1080}, {1280, 720}, {3840, 2160}, {640, 360}};

void expectInside(const Rect &outer, const Rect &r, const char *what, float W, float H)
{
    EXPECT_TRUE(outer.contains(r)) << what << " fuera de su panel a " << W << "x" << H;
}
} // namespace

TEST(LoudnessView, FullLayoutFitsAndLeavesMultiviewLabelFree)
{
    for (auto [W, H] : kSizes)
        for (bool dyn : {false, true}) {
            const LoudnessLayout l = computeLoudnessLayout(W, H, false, dyn);
            for (const Rect *r : {&l.header, &l.barPanel, &l.graphPanel, &l.readPanel})
                expectInside(l.canvas, *r, "panel", W, H);
            for (const Rect *r : {&l.barPanel, &l.graphPanel, &l.readPanel})
                EXPECT_LE(r->bottom(), H * 0.83f + 0.5f) << "el 17 % inferior es del nombre de la escena";
            // Orden de Youlean: barra M, tarjetas, distribución, escala e historial.
            EXPECT_LT(l.barPanel.right(), l.readPanel.x + 0.5f);
            EXPECT_LT(l.readPanel.right(), l.graphPanel.x + 0.5f);
            EXPECT_LE(l.histogram.right(), l.graphScale.x + 0.5f);
            EXPECT_LE(l.graphScale.right(), l.graph.x + 0.5f);
            EXPECT_FALSE(l.header.overlaps(l.barPanel));
            expectInside(l.barPanel, l.bar, "barra", W, H);
            expectInside(l.barPanel, l.barScale, "escala de la barra", W, H);
            EXPECT_LE(l.barScale.right(), l.bar.x + 0.5f) << "la escala, a la izquierda de la barra";
            expectInside(l.graphPanel, l.graph, "historial", W, H);
            expectInside(l.graphPanel, l.histogram, "histograma", W, H);
            // Tarjetas: integrated y 2 × 4.
            expectInside(l.readPanel, l.bigCard, "integrated", W, H);
            for (const Rect *r : {&l.bigValue, &l.bigLabel, &l.statusChip})
                expectInside(l.bigCard, *r, "parte de integrated", W, H);
            EXPECT_FALSE(l.bigLabel.overlaps(l.statusChip));
            ASSERT_EQ(l.rows.size(), static_cast<std::size_t>(kReadoutRows));
            for (std::size_t i = 0; i < l.rows.size(); ++i) {
                expectInside(l.readPanel, l.rows[i], "tarjeta", W, H);
                EXPECT_FALSE(l.rows[i].overlaps(l.bigCard));
                for (std::size_t j = i + 1; j < l.rows.size(); ++j)
                    EXPECT_FALSE(l.rows[i].overlaps(l.rows[j])) << i << " y " << j;
                EXPECT_FALSE(cardValue(l.rows[i]).overlaps(cardLabel(l.rows[i])));
            }
            // Cabecera: etiqueta, píldoras y distintivo sin solaparse.
            for (const Rect *r : {&l.label, &l.presetPill, &l.chanPill, &l.badge})
                expectInside(l.header, *r, "cabecera", W, H);
            EXPECT_FALSE(l.label.overlaps(l.presetPill));
            EXPECT_FALSE(l.presetPill.overlaps(l.chanPill));
            EXPECT_FALSE(l.chanPill.overlaps(l.badge));
            // La barra y el historial comparten la escala vertical: mismo alto y misma posición.
            EXPECT_FLOAT_EQ(l.bar.y, l.graph.y);
            EXPECT_FLOAT_EQ(l.bar.h, l.graph.h);
            EXPECT_FLOAT_EQ(l.histogram.h, l.graph.h);
            if (dyn) {
                EXPECT_GT(l.dynamics.h, 0.0f);
                EXPECT_FALSE(l.dynamics.overlaps(l.graph));
                expectInside(l.graphPanel, l.dynamics, "dinámica", W, H);
            } else {
                EXPECT_EQ(l.dynamics.h, 0.0f);
            }
        }
}

TEST(LoudnessView, HeaderMatchesClassicView)
{
    for (auto [W, H] : kSizes) {
        const LoudnessLayout l = computeLoudnessLayout(W, H, false, false);
        const MonitorLayout c = computeLayout(W, H);
        EXPECT_FLOAT_EQ(l.header.y, c.header.y);
        EXPECT_FLOAT_EQ(l.header.h, c.header.h);
        EXPECT_FLOAT_EQ(l.badge.x, c.clipBadge.x);
        EXPECT_FLOAT_EQ(l.fontPx, c.fontPx);
        EXPECT_FLOAT_EQ(l.smallFontPx, c.smallFontPx);
    }
}

TEST(LoudnessView, CompactLayout)
{
    for (auto [W, H] : kSizes) {
        const LoudnessLayout l = computeLoudnessLayout(W, H, true, true);
        EXPECT_TRUE(l.compact);
        EXPECT_EQ(l.graph.w * l.graph.h, 0.0f) << "sin historial en modo compacto";
        EXPECT_EQ(l.dynamics.h, 0.0f);
        ASSERT_EQ(l.rows.size(), 3u);
        for (const Rect &r : l.rows)
            EXPECT_FALSE(r.overlaps(l.bigCard));
        EXPECT_FALSE(l.barPanel.overlaps(l.readPanel));
        expectInside(l.canvas, l.readPanel, "lecturas", W, H);
        EXPECT_GT(l.bigFontPx, computeLoudnessLayout(W, H, false, false).bigFontPx) << "valor grande para overlay";
    }
}

TEST(LoudnessView, TextScaleChangesFontsOnly)
{
    const LoudnessLayout a = computeLoudnessLayout(1920, 1080, false, false, 1.0f);
    const LoudnessLayout b = computeLoudnessLayout(1920, 1080, false, false, 1.25f);
    EXPECT_NEAR(b.fontPx, a.fontPx * 1.25f, 1e-3);
    EXPECT_FLOAT_EQ(a.readPanel.w, b.readPanel.w);
    const MonitorLayout c1 = computeLayout(1920, 1080), c2 = computeLayout(1920, 1080, 0.8f);
    EXPECT_NEAR(c2.smallFontPx, c1.smallFontPx * 0.8f, 1e-3);
    EXPECT_FLOAT_EQ(computeLayout(1920, 1080, 9.0f).fontPx, computeLayout(1920, 1080, 2.0f).fontPx) << "límite 2×";
}

TEST(LoudnessView, ScaleAndTicks)
{
    EXPECT_DOUBLE_EQ(lufsFraction(0.0), 1.0);
    EXPECT_DOUBLE_EQ(lufsFraction(-60.0), 0.0);
    EXPECT_DOUBLE_EQ(lufsFraction(-30.0), 0.5);
    EXPECT_DOUBLE_EQ(lufsFraction(-80.0), 0.0);
    EXPECT_DOUBLE_EQ(lufsFraction(sg::loudness::kNotMeasured), 0.0);
    const auto t = loudnessTicks();
    ASSERT_EQ(t.size(), 31u);
    int labels = 0;
    for (const auto &k : t)
        labels += !k.label.empty();
    EXPECT_EQ(labels, 7);
    EXPECT_EQ(t.front().label, "0");
    EXPECT_EQ(t.back().label, "\xE2\x88\x92" "60");
}

TEST(LoudnessView, HistoryColumnsNewestOnTheRight)
{
    std::vector<HistoryPoint> pts;
    for (int i = 0; i < 50; ++i) {
        HistoryPoint p;
        p.momentary = static_cast<float>(-40 + i * 0.2);
        p.shortTerm = i >= 30 ? -30.0f : static_cast<float>(sg::loudness::kNotMeasured);
        p.integrated = -31.0f;
        pts.push_back(p);
    }
    const auto c = historyColumns(pts, 100, 100); // 10 s de ventana, 5 s de datos
    ASSERT_EQ(c.size(), 100u);
    EXPECT_FALSE(c[0].has) << "la mitad izquierda, sin datos todavía";
    EXPECT_FALSE(c[49].has);
    EXPECT_TRUE(c[50].has);
    EXPECT_TRUE(c[99].has);
    EXPECT_NEAR(c[99].momentaryMax, -40 + 49 * 0.2, 1e-4) << "el punto más reciente, a la derecha";
    EXPECT_FALSE(c[60].hasShortTerm) << "short-term aún no medido";
    EXPECT_TRUE(c[99].hasShortTerm);
    // Más puntos que columnas: cada columna resume varios (mín/máx de momentary).
    const auto d = historyColumns(pts, 50, 10);
    EXPECT_TRUE(d[9].has);
    EXPECT_LT(d[9].momentaryMin, d[9].momentaryMax);
    EXPECT_TRUE(historyColumns({}, 100, 10)[9].has == false);
    // Menos puntos que columnas (30 s en 500 px): cada punto ocupa varias columnas, sin huecos.
    const auto e = historyColumns(pts, 300, 500);
    int filled = 0;
    for (const auto &col : e)
        filled += col.has;
    EXPECT_GE(filled, 50 * 500 / 300) << "50 puntos de 300 cubren 1/6 del ancho";
    for (uint32_t x = 500 - 80; x < 500; ++x)
        EXPECT_TRUE(e[x].has) << "columna " << x;
}

TEST(LoudnessView, ThemesAndZoneColors)
{
    const Theme &d = theme(ThemeId::Dark), &l = theme(ThemeId::Light);
    EXPECT_NE(d.canvas, l.canvas);
    // Contraste del texto: claro sobre oscuro y al revés.
    auto luma = [](uint32_t c) { return 0.3 * ((c >> 16) & 0xFF) + 0.59 * ((c >> 8) & 0xFF) + 0.11 * (c & 0xFF); };
    EXPECT_GT(luma(d.text) - luma(d.panel), 150);
    EXPECT_GT(luma(l.panel) - luma(l.text), 150);
    // Texto neutro (R = G = B): el orden de bytes de las fuentes de texto no importa.
    for (uint32_t c : {d.text, d.textDim, l.text, l.textDim, d.textOnAccent}) {
        EXPECT_EQ((c >> 16) & 0xFF, (c >> 8) & 0xFF);
        EXPECT_EQ((c >> 8) & 0xFF, c & 0xFF);
    }
    EXPECT_EQ(zoneColor(d, TargetZone::Below), d.below);
    EXPECT_EQ(zoneColor(d, TargetZone::Within), d.within);
    EXPECT_EQ(zoneColor(d, TargetZone::Above, true), d.aboveDim);
    EXPECT_NE(d.below, d.within);
    EXPECT_NE(d.within, d.above);
}
