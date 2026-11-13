#include "ui/DemoKit.h"
#include "core/Renderer2D.h"
#include "ui/GameMenuUi.h"

#include <algorithm>
#include <cmath>

namespace DemoKit
{
namespace
{
constexpr float PI_ = 3.1415926f;

// everything of a demo stays inside its strip (a Liang-Barsky clip of every line)
float g_ClipX0 = 0.0f, g_ClipY0 = 0.0f, g_ClipX1 = 1280.0f, g_ClipY1 = 720.0f;

bool InClip(float x, float y)
{
    return x >= g_ClipX0 && x <= g_ClipX1 && y >= g_ClipY0 && y <= g_ClipY1;
}
}

float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float Ease(float t)
{
    t = Clamp01(t);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float Seg01(float t, float a, float b)
{
    if (b <= a) return t >= b ? 1.0f : 0.0f;
    return Clamp01((t - a) / (b - a));
}

float Pulse(float t, float a, float b)
{
    const float len = b - a;
    return Seg01(t, a, a + len * 0.2f) * (1.0f - Seg01(t, b - len * 0.3f, b));
}

Color Fade(Color c, float a)
{
    c.a *= a;
    return c;
}

Color Mix(Color a, Color b, float t)
{
    return Rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

Scene Begin(float x, float y, float w, float h, int frame, int loop, bool fadeSeam, bool grid)
{
    // the strip: a dark ground with a hairline frame and a faint grid
    GameMenuUi::Panel(x, y, w, h, 0.0f, Rgba(0.0f, 0.0f, 0.0f, 0.46f), Rgba(1.0f, 1.0f, 1.0f, 0.16f));
    if (grid)
    {
        for (int i = 1; i < 10; ++i) UiKit::Fill(x + w * static_cast<float>(i) / 10.0f, y + 1.0f, 1.0f, h - 2.0f, Rgba(1.0f, 1.0f, 1.0f, 0.025f));
    }
    g_ClipX0 = x + 1.0f;
    g_ClipY0 = y + 1.0f;
    g_ClipX1 = x + w - 1.0f;
    g_ClipY1 = y + h - 1.0f;
    Scene s{x, y, w, h, static_cast<float>(loop > 0 ? frame % loop : frame), 1.0f};
    // the loop fades in at its start and out at its end, so that its seam is not seen
    if (fadeSeam) s.a = Clamp01(s.t / 8.0f) * (1.0f - Seg01(s.t, static_cast<float>(loop) - 8.0f, static_cast<float>(loop)));
    return s;
}

// ---- shapes ---------------------------------------------------------------------------------------------------------------------------------------------
void Line(float x0, float y0, float x1, float y1, float th, Color c)
{
    if (c.a <= 0.003f) return;
    float t0 = 0.0f, t1 = 1.0f;
    const float dx = x1 - x0, dy = y1 - y0;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0 - g_ClipX0, g_ClipX1 - x0, y0 - g_ClipY0, g_ClipY1 - y0};
    for (int i = 0; i < 4; ++i)
    {
        if (std::fabs(p[i]) < 1.0e-6f)
        {
            if (q[i] < 0.0f) return;
            continue;
        }
        const float r = q[i] / p[i];
        if (p[i] < 0.0f)
            t0 = std::max(t0, r);
        else
            t1 = std::min(t1, r);
        if (t0 > t1) return;
    }
    GameMenuUi::Segment(x0 + dx * t0, y0 + dy * t0, x0 + dx * t1, y0 + dy * t1, th, c);
}

void Disc(float cx, float cy, float r, Color c)
{
    if (r <= 0.3f || c.a <= 0.003f || !InClip(cx, cy)) return;
    Renderer2DVertex v[12];
    for (int i = 0; i < 12; ++i)
    {
        const float a = 2.0f * PI_ * static_cast<float>(i) / 12.0f;
        v[i] = Renderer2DVertex{UiKit::NX(cx + std::cos(a) * r), UiKit::NY(cy + std::sin(a) * r), c.r, c.g, c.b, c.a};
    }
    Renderer2D_DrawPolygon(v, 12);
}

void Ring(float cx, float cy, float r, float th, Color c)
{
    if (r <= 0.5f || c.a <= 0.003f) return;
    constexpr int N = 36;
    for (int i = 0; i < N; ++i)
    {
        const float a0 = 2.0f * PI_ * static_cast<float>(i) / N, a1 = 2.0f * PI_ * static_cast<float>(i + 1) / N;
        Line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, th, c);
    }
}

void DashedRing(float cx, float cy, float r, float th, Color c, int dashes)
{
    if (r <= 0.5f || c.a <= 0.003f) return;
    const int n = dashes * 2;
    for (int i = 0; i < n; i += 2)
    {
        const float a0 = 2.0f * PI_ * static_cast<float>(i) / static_cast<float>(n), a1 = 2.0f * PI_ * static_cast<float>(i + 1) / static_cast<float>(n);
        Line(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r, th, c);
    }
}

void Dia(float cx, float cy, float r, Color c)
{
    if (c.a <= 0.003f || !InClip(cx, cy)) return;
    GameMenuUi::DiamondFill(cx, cy, r, c);
}

void DiaOutline(float cx, float cy, float r, float th, Color c)
{
    Line(cx, cy - r, cx + r, cy, th, c);
    Line(cx + r, cy, cx, cy + r, th, c);
    Line(cx, cy + r, cx - r, cy, th, c);
    Line(cx - r, cy, cx, cy - r, th, c);
}

void Box(float x, float y, float w, float h, Color c)
{
    if (c.a <= 0.003f || w <= 0.0f || h <= 0.0f) return;
    const float x0 = std::max(x, g_ClipX0), y0 = std::max(y, g_ClipY0);
    const float x1 = std::min(x + w, g_ClipX1), y1 = std::min(y + h, g_ClipY1);
    if (x1 <= x0 || y1 <= y0) return;
    UiKit::Fill(x0, y0, x1 - x0, y1 - y0, c);
}

void Frame(float x, float y, float w, float h, float th, Color c)
{
    Line(x, y, x + w, y, th, c);
    Line(x + w, y, x + w, y + h, th, c);
    Line(x + w, y + h, x, y + h, th, c);
    Line(x, y + h, x, y, th, c);
}

void Bolt(float x0, float y0, float x1, float y1, int seed, float th, Color c)
{
    constexpr int N = 6;
    float px = x0, py = y0;
    const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy) + 0.001f;
    const float nx = -dy / len, ny = dx / len;
    for (int i = 1; i <= N; ++i)
    {
        const float k = static_cast<float>(i) / N;
        const float jitter = i == N ? 0.0f : static_cast<float>(((seed * 7 + i * 13) % 9) - 4) * 1.3f;
        const float qx = x0 + dx * k + nx * jitter, qy = y0 + dy * k + ny * jitter;
        Line(px, py, qx, qy, th, c);
        px = qx;
        py = qy;
    }
}

void Brackets(float cx, float cy, float r, float th, Color c)
{
    const float l = r * 0.55f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
        {
            const float x = cx + static_cast<float>(sx) * r, y = cy + static_cast<float>(sy) * r;
            Line(x, y, x - static_cast<float>(sx) * l, y, th, c);
            Line(x, y, x, y - static_cast<float>(sy) * l, th, c);
        }
}

void Hex(float cx, float cy, float r, float th, Color c)
{
    for (int i = 0; i < 6; ++i)
    {
        const float a0 = PI_ / 3.0f * static_cast<float>(i), a1 = PI_ / 3.0f * static_cast<float>(i + 1);
        Line(cx + std::cos(a0) * r, cy + std::sin(a0) * r * 0.9f, cx + std::cos(a1) * r, cy + std::sin(a1) * r * 0.9f, th, c);
    }
}

void Arc(float cx, float cy, float r, float th, float from, float to, Color c)
{
    if (to <= from || r <= 0.5f) return;
    const int n = std::max(2, static_cast<int>((to - from) * 36.0f) + 1);
    float px = cx + std::cos(from * 2.0f * PI_ - PI_ * 0.5f) * r, py = cy + std::sin(from * 2.0f * PI_ - PI_ * 0.5f) * r;
    for (int i = 1; i <= n; ++i)
    {
        const float k = from + (to - from) * static_cast<float>(i) / static_cast<float>(n);
        const float qx = cx + std::cos(k * 2.0f * PI_ - PI_ * 0.5f) * r, qy = cy + std::sin(k * 2.0f * PI_ - PI_ * 0.5f) * r;
        Line(px, py, qx, qy, th, c);
        px = qx;
        py = qy;
    }
}

void Cross(float cx, float cy, float r, float th, Color c)
{
    Line(cx - r, cy - r, cx + r, cy + r, th, c);
    Line(cx - r, cy + r, cx + r, cy - r, th, c);
}

void Ship(float cx, float cy, Color tint, float a, float scale)
{
    if (a <= 0.003f || !InClip(cx, cy)) return;
    const float k = scale;
    const Renderer2DVertex v[4] = {{UiKit::NX(cx + 11.0f * k), UiKit::NY(cy), 0.95f, 0.96f, 1.0f, a},
                                   {UiKit::NX(cx - 8.0f * k), UiKit::NY(cy - 8.0f * k), 0.95f, 0.96f, 1.0f, a},
                                   {UiKit::NX(cx - 4.0f * k), UiKit::NY(cy), 0.55f, 0.58f, 0.68f, a},
                                   {UiKit::NX(cx - 8.0f * k), UiKit::NY(cy + 8.0f * k), 0.95f, 0.96f, 1.0f, a}};
    Renderer2D_DrawPolygon(v, 4);
    Dia(cx - 2.0f * k, cy, 2.4f * k, Fade(tint, a));
}

void Enemy(float cx, float cy, Color rim, float flash, float a, float scale)
{
    if (a <= 0.003f || !InClip(cx, cy)) return;
    const float R = 11.0f * scale;
    Renderer2DVertex v[6];
    for (int i = 0; i < 6; ++i)
    {
        const float ang = PI_ / 3.0f * static_cast<float>(i);
        const float k = 0.10f + 0.80f * flash;
        v[i] = Renderer2DVertex{UiKit::NX(cx + std::cos(ang) * R), UiKit::NY(cy + std::sin(ang) * R * 0.85f), k, k, k + 0.04f, a};
    }
    Renderer2D_DrawPolygon(v, 6);
    for (int i = 0; i < 6; ++i)
    {
        const float a0 = PI_ / 3.0f * static_cast<float>(i), a1 = PI_ / 3.0f * static_cast<float>(i + 1);
        Line(cx + std::cos(a0) * R, cy + std::sin(a0) * R * 0.85f, cx + std::cos(a1) * R, cy + std::sin(a1) * R * 0.85f, 1.6f, Fade(rim, a));
    }
}

void Bullet(float cx, float cy, Color c, bool white, float scale, float a)
{
    if (scale <= 0.02f || a <= 0.003f) return;
    Dia(cx, cy, 4.2f * scale, Fade(c, a));
    if (white) Ring(cx, cy, 7.0f * scale, 1.2f, Fade(WHITE, a * 0.8f));
}

void Shot(float cx, float cy, Color c, float a)
{
    Line(cx - 6.0f, cy, cx + 6.0f, cy, 2.4f, Fade(c, a * 0.45f));
    Line(cx - 4.0f, cy, cx + 6.0f, cy, 1.1f, Fade(WHITE, a * 0.9f));
}

// ---- gauges -------------------------------------------------------------------------------------------------------------------------------------------
void Bar(float x, float y, float w, float h, float fill, Color c, float a)
{
    Box(x, y, w, h, Rgba(1.0f, 1.0f, 1.0f, 0.14f * a));
    Box(x, y, w * Clamp01(fill), h, Fade(c, 0.92f * a));
}

void Pips(float x, float cy, int count, float lit, float gap, float r, Color c, float a)
{
    for (int i = 0; i < count; ++i)
    {
        const float k = Clamp01(lit - static_cast<float>(i));
        const float px = x + static_cast<float>(i) * gap;
        Dia(px, cy, r, Rgba(1.0f, 1.0f, 1.0f, 0.12f * a));
        if (k > 0.0f) Dia(px, cy, r * (0.4f + 0.6f * k), Fade(c, k * a));
    }
}

void Beam(float x0, float cy, float len, float half, Color core, Color glow, float a)
{
    if (len <= 0.0f) return;
    Box(x0, cy - half * 1.9f, len, half * 3.8f, Fade(glow, 0.28f * a));
    Box(x0, cy - half, len, half * 2.0f, Fade(core, 0.95f * a));
}

void Num(const std::string& text, float x, float y, float px, Color c, float a, bool centre)
{
    if (a <= 0.003f) return;
    UiKit::Text(text, x, y, px, Fade(c, a), centre ? TextAlignment::Center : TextAlignment::Left, 120.0f, false);
}

// ---- motion -------------------------------------------------------------------------------------------------------------------------------------------
float FlightHitTime(const Scene& s, const Flight& f, float speed)
{
    const float startX = f.startX > 0.0f ? f.startX : s.right() - 14.0f;
    return f.t0 + (startX - f.hitX) / speed;
}

bool Fly(const Scene& s, const Flight& f, float shipX, float shipY, float speed)
{
    if (s.t < f.t0) return false;
    const float startX = f.startX > 0.0f ? f.startX : s.right() - 14.0f;
    const float x = startX - speed * (s.t - f.t0);
    if (f.absorbed)
    {
        const float tHit = FlightHitTime(s, f, speed);
        if (s.t >= tHit)
        {
            const float k = Seg01(s.t, tHit, tHit + 9.0f);
            if (k >= 1.0f) return false;
            const float qx = f.hitX + (shipX - f.hitX) * Ease(k), qy = f.y + (shipY - f.y) * Ease(k);
            Bullet(qx, qy, f.color, f.white, 1.0f - 0.6f * k, s.a * (1.0f - 0.5f * k));
            Dia(qx, qy, 2.2f, Fade(GREEN, s.a * k)); // the energy
            return s.t - tHit < 1.0f;
        }
    }
    if (x < s.x - 8.0f) return false;
    Bullet(x, f.y, f.color, f.white, 1.0f, s.a);
    return false;
}

void EnergyMote(const Scene& s, float t0, float t1, float x0, float y0, float x1, float y1, Color c)
{
    if (s.t < t0 || s.t > t1) return;
    const float k = Ease(Seg01(s.t, t0, t1));
    Dia(x0 + (x1 - x0) * k, y0 + (y1 - y0) * k, 2.6f * (1.0f - 0.4f * k), Fade(c, s.a * (1.0f - 0.5f * k)));
}
}
