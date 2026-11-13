#include "game/GameEffect.h"
#include "game/GameVfx.h"
#include "game/GameState.h"

#include <algorithm>
#include <cmath>

using GameVfx::Sprite;

namespace
{
constexpr float ASPECT = GameVfx::ASPECT;
constexpr float TWO_PI = 6.2831853f;

struct ExplosionEffect
{
    float x;
    float y;
    float startSize;
    int timer;
    int duration;
    int seed;
    bool active;
};
struct AbsorbEffect
{
    float x;
    float y;
    float startSize;
    Polarity polarity;
    int timer;
    int duration;
    int seed;
    bool active;
};
struct HitEffect
{
    float x;
    float y;
    bool heavy;
    int timer;
    int duration;
    int seed;
    bool active;
};

// SIPHON: a bullet caught by the wide net is pulled to the ship along a thread of light (the net shows it took the bullet)
struct SiphonEffect
{
    float x;
    float y;
    Polarity polarity;
    int timer;
    int duration;
    int seed;
    bool active;
};
// DEFLECT: a white bullet turned into energy: a white diamond flash with four blades
struct DeflectEffect
{
    float x;
    float y;
    int timer;
    int duration;
    bool active;
};

// the shield of an elite broken by the beam: its six sides fly apart
struct ShieldBreakEffect
{
    float x;
    float y;
    float radius;
    int timer;
    int duration;
    int seed;
    bool active;
};

constexpr int SHIELD_BREAK_MAX = 8;
ShieldBreakEffect g_ShieldBreaks[SHIELD_BREAK_MAX]{};
struct Crystal
{
    float x, y, vx, vy;
    float rot, spin;
    float size;
    float r, g, b;
    int life, maxLife;
    bool active;
};
Crystal g_Crystals[GameEffect::CRYSTAL_MAX]{};

constexpr int SIPHON_MAX = 40;
constexpr int DEFLECT_MAX = 8;
SiphonEffect g_Siphons[SIPHON_MAX]{};
DeflectEffect g_Deflects[DEFLECT_MAX]{};

constexpr int EXPLOSION_MAX = 32;
constexpr int ABSORB_MAX = 32;
constexpr int HIT_MAX = 48;

ExplosionEffect g_Explosions[EXPLOSION_MAX]{};
AbsorbEffect g_Absorbs[ABSORB_MAX]{};
HitEffect g_Hits[HIT_MAX]{};

int g_SeedCounter = 0;

float Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float EaseOut(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

template <typename T> T* FindFree(T* effects, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (!effects[i].active)
        {
            return &effects[i];
        }
    }

    return nullptr;
}

// The style guide for effects: hard-edged shapes (bars, triangles, diamonds) with a white-hot middle; soft glow only as a short flash.
// Colour only where it means something (the polarity: absorb, siphon, echo). An explosion must never outshine the bullets for long.

void DrawExplosion(const ExplosionEffect& effect)
{
    const float life = static_cast<float>(effect.timer) / static_cast<float>(effect.duration);
    const float progress = 1.0f - life;
    const float eased = EaseOut(progress);
    const float size = effect.startSize;

    // 1. the flash: short (the first 5 frames), white
    if (progress < 0.18f)
    {
        const float f = 1.0f - progress / 0.18f;
        GameVfx::DrawRound(Sprite::Glow, effect.x, effect.y, size * 1.8f, 1.0f, 1.0f, 1.0f, f * 0.65f);
        GameVfx::DrawDiamond(effect.x, effect.y, size * (0.30f + 0.25f * f), 0.7854f, 1.0f, 1.0f, 1.0f, f);
    }
    // 2. the shock ring: 16 hard segments that fly out and thin
    {
        const float rad = size * (0.35f + 1.55f * eased);
        const float th = 0.010f * life + 0.0015f;
        constexpr int SEG = 16;
        for (int i = 0; i < SEG; ++i)
        {
            if ((i + effect.seed) % 5 == 0) continue; // (gaps: the ring is cut)
            const float a0 = TWO_PI * static_cast<float>(i) / SEG + static_cast<float>(effect.seed) * 0.37f;
            const float a1 = a0 + TWO_PI / SEG * 0.72f;
            GameVfx::DrawBar(effect.x + std::cos(a0) * rad, effect.y + std::sin(a0) * rad / ASPECT, effect.x + std::cos(a1) * rad,
                             effect.y + std::sin(a1) * rad / ASPECT, th, 1.0f, 1.0f, 1.0f, life * life * 0.85f);
        }
    }
    // 3. (the hull no longer breaks into dark plates: the debris of the game is CRYSTALS, see SpawnCrystals - EnemyArt::OnDeath throws them)
    // 4. sparks: thin white needles out of the middle (the first half)
    if (progress < 0.5f)
    {
        const float k = 1.0f - progress / 0.5f;
        constexpr int RAYS = 9;
        for (int i = 0; i < RAYS; i++)
        {
            const float rnd = GameVfx::Hash(effect.seed * 41 + i);
            const float angle = TWO_PI * (static_cast<float>(i) + 0.5f * rnd) / RAYS;
            const float inner = size * (0.30f + 1.1f * EaseOut(progress / 0.5f));
            const float len = size * (0.6f + 1.0f * GameVfx::Hash(effect.seed * 43 + i)) * k;
            GameVfx::DrawBar(effect.x + std::cos(angle) * inner, effect.y + std::sin(angle) * inner / ASPECT, effect.x + std::cos(angle) * (inner + len),
                             effect.y + std::sin(angle) * (inner + len) / ASPECT, 0.0030f * k + 0.001f, 1.0f, 0.96f, 0.90f, k);
        }
    }
}

// ABSORB: the bullet is drawn into the ship: a ring of its colour cut in four closes in, four streaks run in, a diamond core flashes white
void DrawAbsorb(const AbsorbEffect& effect)
{
    const float life = static_cast<float>(effect.timer) / static_cast<float>(effect.duration);
    const float progress = 1.0f - life;
    float r, g, b;
    GameVfx::GetPolarityColor(effect.polarity == Polarity::Red, r, g, b);
    const float contract = 1.0f - EaseOut(progress);
    const float scale = effect.startSize / 0.075f;
    const float rad = (0.030f + contract * 0.085f) * (0.7f + 0.3f * scale);
    const float turn = progress * 1.6f + static_cast<float>(effect.seed) * 0.5f;
    for (int i = 0; i < 4; ++i)
    {
        const float a0 = turn + 1.5707963f * static_cast<float>(i);
        const float a1 = a0 + 1.0f;
        GameVfx::DrawBar(effect.x + std::cos(a0) * rad, effect.y + std::sin(a0) * rad / ASPECT, effect.x + std::cos(a1) * rad, effect.y + std::sin(a1) * rad / ASPECT,
                         0.0040f, r, g, b, Clamp01(progress * 4.0f) * life);
    }
    for (int i = 0; i < 4; ++i)
    {
        const float a = turn * 0.5f + 1.5707963f * static_cast<float>(i) + 0.7854f;
        const float d0 = rad * 1.25f, d1 = rad * 0.55f;
        GameVfx::DrawBar(effect.x + std::cos(a) * d0, effect.y + std::sin(a) * d0 / ASPECT, effect.x + std::cos(a) * d1, effect.y + std::sin(a) * d1 / ASPECT,
                         0.0022f, 1.0f, 1.0f, 1.0f, life * 0.8f);
    }
    if (progress > 0.45f)
    {
        const float f = 1.0f - (progress - 0.45f) / 0.55f;
        GameVfx::DrawRound(Sprite::Glow, effect.x, effect.y, 0.05f * (0.7f + 0.3f * scale), r, g, b, f * 0.55f);
        GameVfx::DrawDiamond(effect.x, effect.y, 0.012f * f + 0.004f, 0.0f, 1.0f, 1.0f, 1.0f, f);
    }
}

// a hit on an enemy: a short white diamond and three hard sparks thrown back
void DrawHit(const HitEffect& effect)
{
    const float life = static_cast<float>(effect.timer) / static_cast<float>(effect.duration);
    const float progress = 1.0f - life;
    const float eased = EaseOut(progress);
    const float scale = effect.heavy ? 1.35f : 1.0f;
    GameVfx::DrawRound(Sprite::Glow, effect.x, effect.y, 0.040f * scale * life, 1.0f, 1.0f, 1.0f, life * 0.55f);
    GameVfx::DrawDiamond(effect.x, effect.y, 0.010f * scale * (0.5f + life), 0.7854f + progress, 1.0f, 1.0f, 1.0f, life);
    const int sparkCount = effect.heavy ? 5 : 3;
    for (int i = 0; i < sparkCount; i++)
    {
        const float random = GameVfx::Hash(effect.seed * 19 + i);
        const float angle = 3.1415926f + (random - 0.5f) * 2.0f;
        const float d0 = (0.008f + eased * 0.050f * (0.6f + random * 0.8f)) * scale;
        const float d1 = d0 + (0.026f * life + 0.004f) * scale;
        GameVfx::DrawBar(effect.x + std::cos(angle) * d0, effect.y + std::sin(angle) * d0 / ASPECT, effect.x + std::cos(angle) * d1,
                         effect.y + std::sin(angle) * d1 / ASPECT, 0.0028f, 1.0f, 1.0f, 1.0f, life);
    }
}

// CRYSTAL: a flat sliver - a fill and one brighter edge. Never more than 0.5 opaque; the first part of its life is full, the last 45 % fades out.
void DrawCrystal(const Crystal& c)
{
    const float life = static_cast<float>(c.life) / static_cast<float>(c.maxLife);
    const float fade = Clamp01(life * 2.2f);
    const float cs = std::cos(c.rot), sn = std::sin(c.rot);
    const float lx[3] = {c.size, -c.size * 0.55f, -c.size * 0.25f}, ly[3] = {0.0f, c.size * 0.42f, -c.size * 0.50f};
    float x[3], y[3];
    for (int k = 0; k < 3; ++k)
    {
        x[k] = c.x + lx[k] * cs - ly[k] * sn;
        y[k] = c.y + (lx[k] * sn + ly[k] * cs) / ASPECT;
    }
    GameVfx::DrawTri(x[0], y[0], x[1], y[1], x[2], y[2], c.r, c.g, c.b, 0.34f * fade, false);
    const float er = c.r + (1.0f - c.r) * 0.55f, eg = c.g + (1.0f - c.g) * 0.55f, eb = c.b + (1.0f - c.b) * 0.55f;
    GameVfx::DrawBar(x[0], y[0], x[1], y[1], 0.0022f, er, eg, eb, 0.50f * fade, false);
}

// SIPHON: the caught bullet flies along a thread of light into the ship (it follows the ship), a diamond of its colour at its head
void DrawSiphon(const SiphonEffect& e)
{
    const float life = static_cast<float>(e.timer) / static_cast<float>(e.duration);
    const float k = EaseOut(1.0f - life); // 0 at the bullet, 1 at the ship
    float r, g, b;
    GameVfx::GetPolarityColor(e.polarity == Polarity::Red, r, g, b);
    const float sx = GameState::player.x, sy = GameState::player.y;
    const float hx = e.x + (sx - e.x) * k, hy = e.y + (sy - e.y) * k;
    const float tail = std::max(0.0f, k - 0.35f);
    const float tx = e.x + (sx - e.x) * tail, ty = e.y + (sy - e.y) * tail;
    // the thread: from the point of capture to the ship, faint; the bright part is the moving bullet with its tail
    GameVfx::DrawBar(e.x, e.y, sx, sy, 0.0012f, r, g, b, 0.35f * life);
    GameVfx::DrawBar(tx, ty, hx, hy, 0.0040f, r, g, b, 0.85f);
    GameVfx::DrawBar(tx, ty, hx, hy, 0.0015f, 1.0f, 1.0f, 1.0f, 0.90f);
    GameVfx::DrawDiamond(hx, hy, 0.010f, k * 4.0f, r, g, b, 1.0f);
    GameVfx::DrawDiamond(hx, hy, 0.0045f, k * 4.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    // the hook: a small bracket where the net took it (it fades first)
    const float hk = Clamp01(life * 2.0f - 1.0f);
    if (hk > 0.0f)
    {
        const float rr = 0.020f;
        for (int i = 0; i < 4; ++i)
        {
            const float a0 = 0.7854f + 1.5707963f * static_cast<float>(i) - 0.35f, a1 = a0 + 0.7f;
            GameVfx::DrawBar(e.x + std::cos(a0) * rr, e.y + std::sin(a0) * rr / ASPECT, e.x + std::cos(a1) * rr, e.y + std::sin(a1) * rr / ASPECT, 0.0025f, r, g, b, hk);
        }
    }
}

// SHIELD BREAK: the hexagon cracks - every side flies out along its normal, turning, and thins away; the six corners go out as diamonds;
// a hard white flash at the start. It is the answer to the beam, so it is loud for a moment (14 frames) and then it is gone.
void DrawShieldBreak(const ShieldBreakEffect& e)
{
    const float life = static_cast<float>(e.timer) / static_cast<float>(e.duration);
    const float k = EaseOut(1.0f - life);
    if (life > 0.80f)
    {
        const float f = (life - 0.80f) / 0.20f;
        GameVfx::DrawRound(Sprite::Glow, e.x, e.y, e.radius * 2.2f * f, 1.0f, 1.0f, 1.0f, 0.45f * f);
        GameVfx::DrawDiamond(e.x, e.y, e.radius * 0.35f * f, 0.7854f, 1.0f, 1.0f, 1.0f, f);
    }
    for (int i = 0; i < 6; i++)
    {
        const float a0 = TWO_PI * static_cast<float>(i) / 6.0f, a1 = TWO_PI * static_cast<float>(i + 1) / 6.0f;
        const float mid = (a0 + a1) * 0.5f;
        const float turn = (GameVfx::Hash(e.seed * 13 + i) - 0.5f) * 1.6f * k;
        const float push = e.radius * (0.10f + 0.85f * k) * (0.8f + 0.4f * GameVfx::Hash(e.seed * 7 + i));
        const float cx = e.x + std::cos(mid) * (e.radius * 0.866f + push), cy = e.y + std::sin(mid) * (e.radius * 0.866f + push) / ASPECT;
        // the side as a bar of the length of the radius, turned by `turn` about its middle
        const float tx = -std::sin(mid + turn) * e.radius * 0.5f, ty = std::cos(mid + turn) * e.radius * 0.5f / ASPECT;
        GameVfx::DrawBar(cx - tx, cy - ty, cx + tx, cy + ty, 0.0050f * life + 0.0012f, 0.85f, 0.94f, 1.0f, 0.95f * life);
        GameVfx::DrawBar(cx - tx * 0.6f, cy - ty * 0.6f, cx + tx * 0.6f, cy + ty * 0.6f, 0.0018f, 1.0f, 1.0f, 1.0f, life);
        // the corner that was at a0: a diamond that is thrown outwards
        const float cr = e.radius * (1.0f + 0.9f * k);
        GameVfx::DrawDiamond(e.x + std::cos(a0) * cr, e.y + std::sin(a0) * cr / ASPECT, 0.0075f * life + 0.002f, a0 + k * 3.0f, 0.9f, 0.96f, 1.0f, life);
    }
    // the shock: the hexagon once more, expanding and thin
    for (int i = 0; i < 6; i++)
    {
        const float a0 = TWO_PI * static_cast<float>(i) / 6.0f, a1 = TWO_PI * static_cast<float>(i + 1) / 6.0f;
        const float r = e.radius * (1.0f + 0.55f * k);
        GameVfx::DrawBar(e.x + std::cos(a0) * r, e.y + std::sin(a0) * r / ASPECT, e.x + std::cos(a1) * r, e.y + std::sin(a1) * r / ASPECT, 0.0022f, 1.0f, 1.0f, 1.0f,
                         0.50f * life * life);
    }
}

// DEFLECT: a white diamond flash with four blades
void DrawDeflect(const DeflectEffect& e)
{
    const float life = static_cast<float>(e.timer) / static_cast<float>(e.duration);
    const float k = EaseOut(1.0f - life);
    GameVfx::DrawDiamond(e.x, e.y, 0.012f + 0.018f * k, 0.7854f, 1.0f, 1.0f, 1.0f, life);
    for (int i = 0; i < 4; ++i)
    {
        const float a = 1.5707963f * static_cast<float>(i);
        const float d0 = 0.02f + 0.04f * k, d1 = d0 + 0.05f * life;
        GameVfx::DrawBar(e.x + std::cos(a) * d0, e.y + std::sin(a) * d0 / ASPECT, e.x + std::cos(a) * d1, e.y + std::sin(a) * d1 / ASPECT, 0.0030f, 1.0f, 1.0f, 1.0f, life);
    }
    GameVfx::DrawRound(Sprite::Glow, e.x, e.y, 0.10f * life, 0.9f, 0.95f, 1.0f, 0.5f * life);
}
}

void GameEffect::Initialize()
{
    for (auto& effect : g_Explosions)
        effect = {};
    for (auto& effect : g_Absorbs)
        effect = {};
    for (auto& effect : g_Hits)
        effect = {};
    for (auto& effect : g_Siphons)
        effect = {};
    for (auto& effect : g_Deflects)
        effect = {};
    for (auto& effect : g_ShieldBreaks)
        effect = {};
    for (auto& c : g_Crystals)
        c = {};
}

void GameEffect::SpawnSiphon(float x, float y, Polarity polarity)
{
    SiphonEffect* effect = FindFree(g_Siphons, SIPHON_MAX);
    if (effect == nullptr) return;
    effect->active = true;
    effect->x = x;
    effect->y = y;
    effect->polarity = polarity;
    effect->duration = 12;
    effect->timer = effect->duration;
    effect->seed = ++g_SeedCounter;
}

void GameEffect::SpawnShieldBreak(float x, float y, float radius)
{
    for (int i = 0; i < 6; ++i) // a cold crystal off every corner
    {
        const float a = TWO_PI * static_cast<float>(i) / 6.0f;
        SpawnCrystals(x + std::cos(a) * radius, y + std::sin(a) * radius / ASPECT, 1, radius * 0.40f, CrystalTint::White, a, 0.25f, 0.012f, 0.022f, 14, 20);
    }
    ShieldBreakEffect* effect = FindFree(g_ShieldBreaks, SHIELD_BREAK_MAX);
    if (effect == nullptr) return;
    effect->active = true;
    effect->x = x;
    effect->y = y;
    effect->radius = radius;
    effect->duration = 22;
    effect->timer = effect->duration;
    effect->seed = ++g_SeedCounter;
}

void GameEffect::SpawnCrystals(float x, float y, int count, float size, CrystalTint tint, float baseAngle, float spread, float speedLow, float speedHigh,
                               int lifeLow, int lifeHigh)
{
    for (int i = 0; i < count; ++i)
    {
        // a free slot, or the crystal that is nearest to its end
        Crystal* slot = nullptr;
        for (auto& c : g_Crystals)
        {
            if (!c.active)
            {
                slot = &c;
                break;
            }
            if (slot == nullptr || c.life < slot->life) slot = &c;
        }
        const int seed = ++g_SeedCounter;
        const float h0 = GameVfx::Hash(seed * 3 + 1), h1 = GameVfx::Hash(seed * 3 + 2), h2 = GameVfx::Hash(seed * 3 + 3), h3 = GameVfx::Hash(seed * 7 + 5);
        const float angle = baseAngle + (h0 * 2.0f - 1.0f) * spread;
        const float speed = speedLow + (speedHigh - speedLow) * h1;
        Crystal& c = *slot;
        c = Crystal{};
        c.active = true;
        c.x = x;
        c.y = y;
        c.vx = std::cos(angle) * speed;
        c.vy = std::sin(angle) * speed / ASPECT;
        c.size = size * (0.6f + 0.8f * h2);
        c.rot = angle + (h3 - 0.5f) * 1.2f;
        c.spin = (h3 - 0.5f) * 0.36f;
        c.maxLife = c.life = lifeLow + static_cast<int>(static_cast<float>(lifeHigh - lifeLow + 1) * h2);
        bool red = tint == CrystalTint::Red || (tint == CrystalTint::Mixed && (i & 1) == 0);
        if (tint == CrystalTint::White)
        {
            c.r = 0.93f, c.g = 0.96f, c.b = 1.00f;
        }
        else if (red)
        {
            c.r = 1.00f, c.g = 0.62f, c.b = 0.48f; // warm
        }
        else
        {
            c.r = 0.60f, c.g = 0.88f, c.b = 1.00f; // cold
        }
    }
}

void GameEffect::SpawnDeflect(float x, float y)
{
    DeflectEffect* effect = FindFree(g_Deflects, DEFLECT_MAX);
    if (effect == nullptr) return;
    effect->active = true;
    effect->x = x;
    effect->y = y;
    effect->duration = 16;
    effect->timer = effect->duration;
}

void GameEffect::SpawnExplosion(float x, float y, float size)
{
    ExplosionEffect* effect = FindFree(g_Explosions, EXPLOSION_MAX);
    if (effect == nullptr) return;
    effect->active = true;
    effect->x = x;
    effect->y = y;
    effect->startSize = size;
    effect->duration = 30;
    effect->timer = effect->duration;
    effect->seed = ++g_SeedCounter;
}

void GameEffect::SpawnAbsorb(float x, float y, float size, Polarity polarity)
{
    AbsorbEffect* effect = FindFree(g_Absorbs, ABSORB_MAX);
    if (effect == nullptr) return;
    effect->active = true;
    effect->x = x;
    effect->y = y;
    effect->startSize = size;
    effect->polarity = polarity;
    effect->duration = 18;
    effect->timer = effect->duration;
    effect->seed = ++g_SeedCounter;
}

void GameEffect::SpawnHit(float x, float y, bool heavy)
{
    HitEffect* effect = FindFree(g_Hits, HIT_MAX);
    if (effect == nullptr) return;
    effect->active = true;
    effect->x = x;
    effect->y = y;
    effect->heavy = heavy;
    effect->duration = heavy ? 12 : 9;
    effect->timer = effect->duration;
    effect->seed = ++g_SeedCounter;
}

void GameEffect::Update()
{
    for (auto& effect : g_Explosions)
        if (effect.active && --effect.timer <= 0) effect.active = false;

    for (auto& effect : g_Absorbs)
        if (effect.active && --effect.timer <= 0) effect.active = false;

    for (auto& effect : g_Hits)
        if (effect.active && --effect.timer <= 0) effect.active = false;

    for (auto& effect : g_Siphons)
        if (effect.active && --effect.timer <= 0) effect.active = false;

    for (auto& effect : g_Deflects)
        if (effect.active && --effect.timer <= 0) effect.active = false;

    for (auto& effect : g_ShieldBreaks)
        if (effect.active && --effect.timer <= 0) effect.active = false;

    for (auto& c : g_Crystals)
    {
        if (!c.active) continue;
        c.x += c.vx;
        c.y += c.vy;
        c.vx *= 0.90f;
        c.vy *= 0.90f;
        c.spin *= 0.94f;
        c.rot += c.spin;
        if (--c.life <= 0) c.active = false;
    }
}

void GameEffect::Draw()
{
    for (const auto& effect : g_Explosions)
        if (effect.active) DrawExplosion(effect);

    for (const auto& effect : g_Absorbs)
        if (effect.active) DrawAbsorb(effect);

    for (const auto& effect : g_Hits)
        if (effect.active) DrawHit(effect);

    for (const auto& effect : g_Siphons)
        if (effect.active) DrawSiphon(effect);

    for (const auto& effect : g_Deflects)
        if (effect.active) DrawDeflect(effect);

    for (const auto& effect : g_ShieldBreaks)
        if (effect.active) DrawShieldBreak(effect);

    for (const auto& c : g_Crystals)
        if (c.active) DrawCrystal(c);
}





