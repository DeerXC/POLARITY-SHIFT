#include "game/GameVfx.h"
#include "core/AssetManager.h"
#include "core/Renderer2D.h"
#include <cmath>
#include <vector>

namespace
{
// One texture per GameVfx::Sprite, in the order of the enum.
constexpr AssetManager::Tex SPRITE_TEXTURES[] = {
    AssetManager::Tex::FxGlow, AssetManager::Tex::FxRing, AssetManager::Tex::FxSpark, AssetManager::Tex::FxFlame,
    AssetManager::Tex::FxBeam, AssetManager::Tex::FxFlow, AssetManager::Tex::FxShard};

int GetTexture(GameVfx::Sprite sprite)
{
    return AssetManager::Get(SPRITE_TEXTURES[static_cast<int>(sprite)]);
}

float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---- enemy bullets are drawn as vector shapes (no texture): hard edges at any resolution, nothing to blur -------------------
// Red  = a dart (kite shape, pointed): short body, a tail that grows with the speed.
// Blue = an orb (octagon): round body, a shorter tail.
// Every shape has a dark rim (readable on the blue / red field of the background), a coloured body and a pale hot core.
// All lengths are in normalized x units (0.01 = 6.4 px at 1280 wide); the y of a point is corrected for the aspect ratio.
struct Heading
{
    float x, y, c, s;
    // point at u along the heading and v to the left of it
    Renderer2DVertex At(float u, float v, float r, float g, float b, float a) const
    {
        return Renderer2DVertex{x + u * c - v * s, y + (u * s + v * c) / GameVfx::ASPECT, r, g, b, a};
    }
};

void Kite(const Heading& h, float tip, float back, float half, float r, float g, float b, float a)
{
    const Renderer2DVertex v[4] = {h.At(tip, 0.0f, r, g, b, a), h.At(0.0f, -half, r, g, b, a), h.At(-back, 0.0f, r, g, b, a),
                                   h.At(0.0f, half, r, g, b, a)};
    Renderer2D_DrawPolygon(v, 4);
}

void Octagon(const Heading& h, float radius, float r, float g, float b, float a)
{
    Renderer2DVertex v[8];
    for (int i = 0; i < 8; ++i)
    {
        const float ang = 6.2831853f * (static_cast<float>(i) + 0.5f) / 8.0f;
        v[i] = h.At(std::cos(ang) * radius, std::sin(ang) * radius, r, g, b, a);
    }
    Renderer2D_DrawPolygon(v, 8);
}

void Tail(const Heading& h, float from, float length, float half, float r, float g, float b, float a)
{
    const Renderer2DVertex v[4] = {h.At(from, half, r, g, b, a), h.At(from, -half, r, g, b, a),
                                   h.At(from - length, -half * 0.25f, r, g, b, 0.0f), h.At(from - length, half * 0.25f, r, g, b, 0.0f)};
    Renderer2D_DrawPolygon(v, 4);
}
}

void GameVfx::Draw(Sprite sprite, float x, float y, float width, float height, float angle, float r, float g, float b, float a)
{
    if (a <= 0.001f || width <= 0.0f || height <= 0.0f) return;
    const int texture = GetTexture(sprite);
    if (texture < 0) return;
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    Renderer2D_DrawTextureEx(texture, x, y, width, height, angle, r, g, b, a, 0.0f, 0.0f, 1.0f, 1.0f);
    Renderer2D_SetBlendMode(previous);
}

void GameVfx::DrawRound(Sprite sprite, float x, float y, float size, float r, float g, float b, float a)
{
    Draw(sprite, x, y, size, size / ASPECT, 0.0f, r, g, b, a);
}

void GameVfx::DrawAlpha(Sprite sprite, float x, float y, float width, float height, float angle, float r, float g, float b,
                        float a)
{
    if (a <= 0.001f || width <= 0.0f || height <= 0.0f) return;
    const int texture = GetTexture(sprite);
    if (texture < 0) return;
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    Renderer2D_DrawTextureEx(texture, x, y, width, height, angle, r, g, b, a, 0.0f, 0.0f, 1.0f, 1.0f);
    Renderer2D_SetBlendMode(previous);
}

void GameVfx::DrawLine(float x1, float y1, float x2, float y2, float thickness, float r, float g, float b, float a)
{
    const float dx = x2 - x1;
    const float dy = (y2 - y1) * ASPECT;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.0001f) return;
    Draw(Sprite::Spark, (x1 + x2) * 0.5f, (y1 + y2) * 0.5f, length, thickness / ASPECT, std::atan2(dy, dx), r, g, b, a);
}

void GameVfx::DrawBar(float x1, float y1, float x2, float y2, float thickness, float r, float g, float b, float a, bool additive)
{
    if (a <= 0.001f) return;
    const float dx = x2 - x1;
    const float dy = (y2 - y1) * ASPECT;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.0001f) return;
    const float nx = -dy / length * thickness * 0.5f, ny = dx / length * thickness * 0.5f / ASPECT;
    const Renderer2DVertex v[4] = {{x1 + nx, y1 + ny, r, g, b, a}, {x2 + nx, y2 + ny, r, g, b, a}, {x2 - nx, y2 - ny, r, g, b, a}, {x1 - nx, y1 - ny, r, g, b, a}};
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(additive ? Renderer2DBlendMode::Additive : Renderer2DBlendMode::Alpha);
    Renderer2D_DrawPolygon(v, 4);
    Renderer2D_SetBlendMode(previous);
}

void GameVfx::DrawTri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a, bool additive)
{
    if (a <= 0.001f) return;
    const Renderer2DVertex v[3] = {{x0, y0, r, g, b, a}, {x1, y1, r, g, b, a}, {x2, y2, r, g, b, a}};
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(additive ? Renderer2DBlendMode::Additive : Renderer2DBlendMode::Alpha);
    Renderer2D_DrawPolygon(v, 3);
    Renderer2D_SetBlendMode(previous);
}

void GameVfx::DrawDiamond(float x, float y, float radius, float angle, float r, float g, float b, float a, bool additive)
{
    if (a <= 0.001f || radius <= 0.0f) return;
    Renderer2DVertex v[4];
    for (int i = 0; i < 4; ++i)
    {
        const float t = angle + 1.5707963f * static_cast<float>(i);
        v[i] = Renderer2DVertex{x + std::cos(t) * radius, y + std::sin(t) * radius / ASPECT, r, g, b, a};
    }
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(additive ? Renderer2DBlendMode::Additive : Renderer2DBlendMode::Alpha);
    Renderer2D_DrawPolygon(v, 4);
    Renderer2D_SetBlendMode(previous);
}

void GameVfx::DrawStrip(Sprite sprite, float left, float top, float right, float bottom, float u0, float u1, float r, float g,
                        float b, float a)
{
    if (a <= 0.001f || right <= left) return;
    const int texture = GetTexture(sprite);
    if (texture < 0) return;
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    Renderer2D_DrawTexture(texture, left, top, right, bottom, r, g, b, a, u0, 0.0f, u1, 1.0f);
    Renderer2D_SetBlendMode(previous);
}

namespace
{
void Ngon(const Heading& h, int n, float radius, float turn, float r, float g, float b, float a)
{
    Renderer2DVertex v[16];
    for (int i = 0; i < n && i < 16; ++i)
    {
        const float ang = turn + 6.2831853f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        v[i] = h.At(std::cos(ang) * radius, std::sin(ang) * radius, r, g, b, a);
    }
    Renderer2D_DrawPolygon(v, n < 16 ? n : 16);
}

constexpr float RIM_R = 0.02f, RIM_G = 0.02f, RIM_B = 0.05f;
}

void GameVfx::DrawEnemyBullet(float x, float y, float angle, bool isRed, bool isHoming, int frame, float speed, float sizeScale, int style)
{
    // One language for every bullet, the same as the ship's shots: a soft glow in the colour, a light body, a white-hot middle and a
    // streak behind it. Red is pointed, blue is round. Only size and outline change by kind (no marks, no rings of ticks).
    const float visualAngle = std::atan2(std::sin(angle) * ASPECT, std::cos(angle));
    const Heading h{x, y, std::cos(visualAngle), std::sin(visualAngle)};
    const float t = static_cast<float>(frame);
    const float pulse = 0.5f + 0.5f * std::sin(t * 0.30f + x * 11.0f + y * 7.0f);
    float r, g, b;
    GetPolarityColor(isRed, r, g, b);
    const float br = r + (1.0f - r) * 0.22f, bg = g + (1.0f - g) * 0.22f, bb = b + (1.0f - b) * 0.22f; // the body: the colour, a little lighter
    const float rimA = 0.38f;                                                                             // a faint dark edge (readable on fire)

    const bool mine = sizeScale > 1.5f;
    float k = sizeScale;
    if (!mine)
    {
        if (style == BulletBoss) k *= 1.25f;
        else if (isHoming) k *= 0.85f;
    }

    // glow and streak (under the body)
    const float streak = mine ? 0.0f : Clamp(speed * 3.0f, 0.012f, 0.055f) * (style == BulletBoss ? 1.2f : 1.0f);
    if (streak > 0.0f)
    {
        const float len = streak + 0.02f * k;
        Draw(Sprite::Spark, x - std::cos(visualAngle) * len * 0.5f, y - std::sin(visualAngle) * len * 0.5f / ASPECT, len, 0.022f * k, visualAngle, r, g, b, 0.55f);
    }
    DrawRound(Sprite::Glow, x, y, (mine ? 0.075f : 0.050f) * k, r, g, b, style == BulletBoss ? 0.60f : 0.42f);

    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    if (mine) // ---- MINE: a slowly turning hexagon (the red one with six short spikes)
    {
        const float m = sizeScale * 0.92f;
        const float turn = t * 0.04f + x * 9.0f;
        if (isRed)
        {
            for (int i = 0; i < 6; ++i)
            {
                const float a = turn + 1.0471976f * static_cast<float>(i);
                const Heading sp{x, y, std::cos(a), std::sin(a)};
                Kite(sp, 0.034f * sizeScale, 0.0f, 0.0050f * sizeScale, br, bg, bb, 1.0f);
            }
        }
        Ngon(h, 6, 0.0215f * m, turn + 0.5236f, RIM_R, RIM_G, RIM_B, rimA);
        Ngon(h, 6, 0.0190f * m, turn + 0.5236f, br, bg, bb, 1.0f);
        Ngon(h, 6, 0.0120f * m, turn + 0.5236f, r * 0.45f, g * 0.45f, b * 0.55f, 1.0f);
        Ngon(h, 6, (0.0068f + 0.0010f * pulse) * m, turn + 0.5236f, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    else if (isRed)
    {
        if (isHoming && style != BulletBoss) // HOMING red: an arrowhead (short back): it reads as "aimed"
        {
            Kite(h, 0.030f * k * 1.15f, 0.005f * k * 1.6f, 0.011f * k * 1.25f, RIM_R, RIM_G, RIM_B, rimA);
            Kite(h, 0.030f * k, 0.005f * k, 0.011f * k, br, bg, bb, 1.0f);
            Kite(h, 0.016f * k, 0.002f * k, 0.004f * k, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        else // NORMAL / SMALL / BOSS red: a dart
        {
            const float tip = 0.026f * k, back = 0.016f * k, half = 0.0085f * k;
            Kite(h, tip * 1.15f, back * 1.2f, half * 1.28f, RIM_R, RIM_G, RIM_B, rimA);
            Kite(h, tip, back, half, br, bg, bb, 1.0f);
            if (style == BulletBoss) Kite(h, tip * 0.78f, back * 0.75f, half * 0.70f, r, g, b, 1.0f); // (one more facet: heavier)
            Kite(h, tip * 0.56f, back * 0.50f, half * 0.42f, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
    else
    {
        const float rad = 0.0150f * k;
        Octagon(h, rad * 1.22f, RIM_R, RIM_G, RIM_B, rimA);
        Octagon(h, rad, br, bg, bb, 1.0f);
        if (isHoming && style != BulletBoss) // HOMING blue: a ring (hollow middle) round a small core
        {
            Octagon(h, rad * 0.66f, r * 0.30f, g * 0.30f, b * 0.40f, 1.0f);
            Octagon(h, rad * 0.30f, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        else
        {
            if (style == BulletBoss) Ngon(h, 6, rad * 0.74f, t * 0.05f, r, g, b, 1.0f);
            Octagon(h, rad * (0.48f + 0.05f * pulse), 1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
    Renderer2D_SetBlendMode(previous);
}

void GameVfx::DrawNeutralBullet(float x, float y, float angle, int frame, float speed)
{
    const float visualAngle = std::atan2(std::sin(angle) * ASPECT, std::cos(angle));
    const Heading h{x, y, std::cos(visualAngle), std::sin(visualAngle)};
    const float tail = Clamp(speed * 3.0f, 0.012f, 0.070f);
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(frame) * 0.35f);

    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    Tail(h, -0.008f, tail, 0.0050f, 0.95f, 0.97f, 1.0f, 0.75f);
    Kite(h, 0.046f, 0.016f, 0.0105f, 0.02f, 0.02f, 0.05f, 0.85f); // dark rim
    Kite(h, 0.040f, 0.012f, 0.0085f, 0.92f, 0.94f, 1.0f, 1.0f);   // body
    Kite(h, 0.022f, 0.006f, 0.0040f, 1.0f, 1.0f, 1.0f, 1.0f);     // hot core
    Renderer2D_SetBlendMode(previous);
    // the mark of the bullets that cannot be absorbed: a broken octagon of hard bars (four sides of eight) that turns slowly
    {
        constexpr float R = 0.027f;
        const float turn = static_cast<float>(frame) * 0.035f;
        for (int i = 0; i < 8; i += 2)
        {
            const float a0 = turn + 0.7853982f * static_cast<float>(i), a1 = a0 + 0.7853982f;
            GameVfx::DrawBar(x + std::cos(a0) * R, y + std::sin(a0) * R / ASPECT, x + std::cos(a1) * R, y + std::sin(a1) * R / ASPECT, 0.0020f, 1.0f, 1.0f, 1.0f,
                             0.40f + 0.30f * pulse);
        }
    }
}

void GameVfx::GetPolarityColor(bool isRed, float& r, float& g, float& b)
{
    if (isRed)
    {
        r = 1.00f;
        g = 0.30f;
        b = 0.20f;
    }
    else
    {
        r = 0.20f;
        g = 0.70f;
        b = 1.00f;
    }
}

float GameVfx::Hash(int seed)
{
    unsigned int x = static_cast<unsigned int>(seed) * 374761393u + 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u;
    x ^= x >> 16;
    return static_cast<float>(x & 0xffffu) / 65535.0f;
}





