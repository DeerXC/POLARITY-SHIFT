#include "ui/Wordmark.h"
#include "core/Renderer2D.h"
#include "ui/GameMenuUi.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace
{
#include "ui/WordmarkGlyphs.h"

constexpr float LEAN = 0.16f;  // the top of a letter is pushed to the right by LEAN x the cap height
constexpr float GAP = 0.085f;  // between two letters (in cap heights)
constexpr float SPACE = 0.42f;

const Glyph* Find(char c)
{
    for (const Glyph& g : GLYPHS)
        if (g.letter == c) return &g;
    return nullptr;
}

// one letter at (x, y) (left end of its cap line, top), unit px tall, in colour c; `dx`, `dy`: the offset of a shadow
void DrawGlyph(const Glyph& g, float x, float y, float unit, UiKit::Color c, float dx, float dy)
{
    if (c.a <= 0.002f) return;
    for (int k = 0; k < g.pieces; ++k)
    {
        const GlyphPiece& piece = g.piece[k];
        Renderer2DVertex v[10];
        for (int i = 0; i < piece.count; ++i)
        {
            const float gx = piece.p[i][0], gy = piece.p[i][1];
            const float px = x + dx + (gx + LEAN * (1.0f - gy)) * unit;
            const float py = y + dy + gy * unit;
            v[i] = Renderer2DVertex{UiKit::NX(px), UiKit::NY(py), c.r, c.g, c.b, c.a};
        }
        Renderer2D_DrawPolygon(v, piece.count);
    }
}
}

void Wordmark::DrawEmblem(float cx, float cy, float r, float alpha, bool framed)
{
    if (alpha <= 0.002f || r < 1.0f) return;
    // keep in step with tools/make_icon.py
    constexpr float FR = 1.00f, FT = 0.085f, HR = 0.74f, SH = 0.30f, GAP = 0.060f, CUT = 0.050f, CORE = 0.15f;
    const UiKit::Color ivory{0.93f, 0.94f, 0.98f, alpha};
    const UiKit::Color ink{0.012f, 0.012f, 0.022f, alpha};
    const UiKit::Color red{1.00f, 0.18f, 0.16f, alpha};
    const UiKit::Color blue{0.10f, 0.64f, 1.00f, alpha};
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    auto poly = [&](const float (*pts)[2], int n, UiKit::Color c)
    {
        Renderer2DVertex v[4];
        for (int i = 0; i < n; ++i) v[i] = Renderer2DVertex{UiKit::NX(cx + pts[i][0] * r), UiKit::NY(cy + pts[i][1] * r), c.r, c.g, c.b, c.a};
        Renderer2D_DrawPolygon(v, n);
    };
    if (framed)
    {
        // the frame: four sides, each a quad between the outer and the inner diamond
        const float o = FR, i = FR - FT * 1.4142f;
        const float outer[4][2] = {{0, -o}, {o, 0}, {0, o}, {-o, 0}};
        const float inner[4][2] = {{0, -i}, {i, 0}, {0, i}, {-i, 0}};
        for (int k = 0; k < 4; ++k)
        {
            const int n = (k + 1) % 4;
            const float q[4][2] = {{outer[k][0], outer[k][1]}, {outer[n][0], outer[n][1]}, {inner[n][0], inner[n][1]}, {inner[k][0], inner[k][1]}};
            poly(q, 4, ivory);
        }
    }
    // the halves: a triangle each; first their ink outlines (a triangle grown from its centroid), then the colours
    for (int side = -1; side <= 1; side += 2)
    {
        const float g = GAP * 0.5f;
        const float x0 = side < 0 ? -g : g, x1 = side < 0 ? -HR : HR;
        const float dy = side < 0 ? -SH : SH;
        const float base[3][2] = {{x0, -HR}, {x1, 0.0f}, {x0, HR}};
        const float cxm = (base[0][0] + base[1][0] + base[2][0]) / 3.0f, cym = (base[0][1] + base[1][1] + base[2][1]) / 3.0f;
        float grown[3][2], plain[3][2];
        for (int k = 0; k < 3; ++k)
        {
            const float vx = base[k][0] - cxm, vy = base[k][1] - cym, l = std::sqrt(vx * vx + vy * vy);
            grown[k][0] = base[k][0] + vx / l * CUT;
            grown[k][1] = base[k][1] + vy / l * CUT + dy;
            plain[k][0] = base[k][0];
            plain[k][1] = base[k][1] + dy;
        }
        if (framed) poly(grown, 3, ink);
        poly(plain, 3, side < 0 ? red : blue);
    }
    const float c = CORE;
    const float core[4][2] = {{0, -c}, {c, 0}, {0, c}, {-c, 0}};
    poly(core, 4, ivory);
    Renderer2D_SetBlendMode(previous);
}

float Wordmark::WordWidth(const std::string& word, float unit)
{
    float w = 0.0f;
    for (size_t i = 0; i < word.size(); ++i)
    {
        const Glyph* g = Find(word[i]);
        w += g != nullptr ? g->width : SPACE;
        if (i + 1 < word.size()) w += GAP;
    }
    return w * unit;
}

void Wordmark::DrawWord(const std::string& word, float x, float y, float unit, UiKit::Color face, UiKit::Color shadow, float shadowPx, float alpha, bool seamInO)
{
    if (alpha <= 0.002f) return;
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    // pass 0: the hard offset of the whole word, pass 1: the letters (so the offset never lies over a neighbour letter)
    for (int pass = 0; pass < 2; ++pass)
    {
        float cx = x;
        for (size_t i = 0; i < word.size(); ++i)
        {
            const Glyph* g = Find(word[i]);
            if (g == nullptr)
            {
                cx += (SPACE + GAP) * unit;
                continue;
            }
            if (pass == 0)
                DrawGlyph(*g, cx, y, unit, UiKit::Color{shadow.r, shadow.g, shadow.b, shadow.a * alpha}, shadowPx, shadowPx * 0.8f);
            else
            {
                DrawGlyph(*g, cx, y, unit, UiKit::Color{face.r, face.g, face.b, face.a * alpha}, 0.0f, 0.0f);
                if (seamInO && g->letter == 'O')
                {
                    // the counter of the O: x from STROKE to width - STROKE, y from STROKE to 1 - STROKE (leaning with the letter)
                    const float mx = (g->width * 0.5f + LEAN * 0.5f) * unit, my = 0.5f * unit;
                    // the counter of the O gets a dark ground first: the red offset of the O falls into it, and a red half on red is lost
                    {
                        const float t0 = STROKE, t1 = g->width - STROKE, u0 = STROKE, u1 = 1.0f - STROKE;
                        auto at = [&](float gx, float gy) { return std::pair<float, float>(cx + (gx + LEAN * (1.0f - gy)) * unit, y + gy * unit); };
                        const auto p0 = at(t0, u0), p1 = at(t1, u0), p2 = at(t1, u1), p3 = at(t0, u1);
                        const Renderer2DVertex v[4] = {{UiKit::NX(p0.first), UiKit::NY(p0.second), 0.012f, 0.012f, 0.022f, alpha},
                                                       {UiKit::NX(p1.first), UiKit::NY(p1.second), 0.012f, 0.012f, 0.022f, alpha},
                                                       {UiKit::NX(p2.first), UiKit::NY(p2.second), 0.012f, 0.012f, 0.022f, alpha},
                                                       {UiKit::NX(p3.first), UiKit::NY(p3.second), 0.012f, 0.012f, 0.022f, alpha}};
                        Renderer2D_DrawPolygon(v, 4);
                    }
                    // the O is the frame: the two halves of the emblem inside it, as big as the counter allows (1.04 r high above and below the middle)
                    DrawEmblem(cx + mx, y + my, unit * 0.235f, alpha, false);
                }
            }
            cx += (g->width + GAP) * unit;
        }
    }
    Renderer2D_SetBlendMode(previous);
}

float Wordmark::DrawLogo(float x, float y, float unit, float a1, float a2, float slide1, float slide2, float block)
{
    const UiKit::Color white{0.90f, 0.91f, 0.95f, 1.0f}; // (not 0.97: that glowed through the bloom; this stays under it)
    const UiKit::Color red{1.00f, 0.18f, 0.16f, 1.0f};
    const UiKit::Color blue{0.10f, 0.64f, 1.00f, 1.0f};
    const float shadow1 = unit * 0.085f;
    const float unit2 = unit * 0.78f;
    const float indent = unit * 1.55f;
    const float y2 = y + unit * 1.22f;
    const float width1 = WordWidth("POLARITY", unit);
    DrawWord("POLARITY", x - slide1, y, unit, white, red, shadow1, a1, true);
    DrawWord("SHIFT", x + indent - slide2, y2, unit2, white, blue, shadow1 * 0.8f, a2, false);
    // the white block: from the end of SHIFT to the end of POLARITY, a solid slanted bar with the proportions of one stroke
    const float blockX0 = x + indent + WordWidth("SHIFT", unit2) + unit * 0.30f - slide2;
    const float blockX1 = x + width1 + unit * LEAN * 0.2f - slide2;
    if (block > 0.002f && blockX1 > blockX0 + 4.0f && a2 > 0.002f)
    {
        const float bx1 = blockX0 + (blockX1 - blockX0) * block;
        const float top = y2 + unit2 * 0.34f, bottom = y2 + unit2 * 0.66f;
        const float lean = (bottom - top) * LEAN * 2.0f;
        const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
        auto bar = [&](float dx, float dy, UiKit::Color c)
        {
            const Renderer2DVertex v[4] = {{UiKit::NX(blockX0 + lean + dx), UiKit::NY(top + dy), c.r, c.g, c.b, c.a},
                                           {UiKit::NX(bx1 + lean + dx), UiKit::NY(top + dy), c.r, c.g, c.b, c.a},
                                           {UiKit::NX(bx1 + dx), UiKit::NY(bottom + dy), c.r, c.g, c.b, c.a},
                                           {UiKit::NX(blockX0 + dx), UiKit::NY(bottom + dy), c.r, c.g, c.b, c.a}};
            Renderer2D_DrawPolygon(v, 4);
        };
        bar(shadow1 * 0.8f, shadow1 * 0.64f, UiKit::Color{blue.r, blue.g, blue.b, a2});
        bar(0.0f, 0.0f, UiKit::Color{white.r, white.g, white.b, a2});
        Renderer2D_SetBlendMode(previous);
    }
    return y2 + unit2;
}

void Wordmark::DrawBadge(float xRight, float yBottom, float unit, float alpha)
{
    if (alpha <= 0.002f) return;
    const UiKit::Color white{0.90f, 0.91f, 0.95f, 1.0f};
    const UiKit::Color red{1.00f, 0.18f, 0.16f, 1.0f};
    const UiKit::Color blue{0.10f, 0.64f, 1.00f, 1.0f};
    const float w2 = WordWidth("SHIFT", unit * 0.78f);
    const float w1 = WordWidth("POLARITY", unit);
    const float gap = unit * 0.45f;
    const float emblemR = unit * 0.66f;                      // the whole emblem (with its frame) stands in front of the words
    const float x0 = xRight - (w1 + gap + w2 + unit * 0.4f);
    const float y0 = yBottom - unit;
    DrawEmblem(x0 - emblemR - unit * 0.55f, y0 + unit * 0.5f, emblemR, alpha, true);
    DrawWord("POLARITY", x0, y0, unit, white, red, unit * 0.07f, alpha, false);
    DrawWord("SHIFT", x0 + w1 + gap, y0 + unit * 0.22f, unit * 0.78f, white, blue, unit * 0.06f, alpha, false);
}

