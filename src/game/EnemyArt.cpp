#include "game/EnemyArt.h"
#include "core/AssetManager.h"
#include "core/Renderer2D.h"
#include "game/GameEffect.h"
#include "game/GameVfx.h"
#include "rogue/Balance.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

using GameVfx::Sprite;

namespace
{
constexpr float PI = 3.1415926535f;
constexpr float TWO_PI = 6.2831853f;
constexpr float ASPECT = GameVfx::ASPECT;

// ---- helpers ------------------------------------------------------------------------------------------------------------------------------------------
std::uint32_t g_Seed = 2463534242u; // xorshift: the art has its own random numbers, the game's std::rand() (waves, attacks) is not disturbed by it

float Rnd()
{
    g_Seed ^= g_Seed << 13;
    g_Seed ^= g_Seed >> 17;
    g_Seed ^= g_Seed << 5;
    return static_cast<float>(g_Seed & 0xFFFFFFu) / 16777216.0f;
}

float RndRange(float low, float high)
{
    return low + (high - low) * Rnd();
}

float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

float Clamp01(float value)
{
    return Clamp(value, 0.0f, 1.0f);
}

float Fract(float value)
{
    return value - std::floor(value);
}

float Mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

bool IsElite(const Enemy& enemy)
{
    return enemy.maxHp >= 100; // heavy, turret, sniper, rival
}

bool IsRush(const Enemy& enemy) // the charger while it rams, the rival while it dashes
{
    return (enemy.type == EnemyType::Charger && enemy.state == EnemyState::Charging)
           || (enemy.type == EnemyType::Rival && enemy.state == EnemyState::Attack && enemy.pendingAttackType == EnemyAttackType::Dash);
}

// the angle that turns a hull (nose to the left) to face a velocity (vx, vy in normalized units)
float Heading(float vx, float vy)
{
    if (std::fabs(vx) + std::fabs(vy) < 1.0e-5f)
    {
        return 0.0f;
    }
    return Clamp(std::atan2(-vy * ASPECT, -vx), -1.25f, 1.25f);
}

void Glow(float x, float y, float size, float r, float g, float b, float a)
{
    GameVfx::DrawRound(Sprite::Glow, x, y, size, r, g, b, a);
}

void Ring(float x, float y, float size, float r, float g, float b, float a)
{
    GameVfx::DrawRound(Sprite::Ring, x, y, size, r, g, b, a);
}

// a point at angle a on a circle of radius R (in x units) round (cx, cy)
void OnCircle(float cx, float cy, float radius, float angle, float& x, float& y)
{
    x = cx + std::cos(angle) * radius;
    y = cy + std::sin(angle) * radius / ASPECT;
}

// ---- per type constants ---------------------------------------------------------------------------------------------------------------------------------
struct TypeSpec
{
    float flowSpeed; // how fast the flow of light runs along the emissive layer (share of the picture per frame)
    float whiten;    // 0 = the colour of the enemy, 1 = white (the sniper fires white bullets)
    float pulse;     // speed of the breathing of the light
    float scaleW;    // size of the hull picture relative to the hit box (the same table as before: it moved here from GameEnemy.cpp)
    float scaleH;
};

const TypeSpec SPECS[7] = {
    {0.012f, 0.00f, 0.090f, 1.00f, 1.00f}, // scout
    {0.016f, 0.00f, 0.100f, 1.08f, 1.08f}, // sine
    {0.007f, 0.00f, 0.060f, 1.30f, 1.30f}, // heavy
    {0.020f, 0.00f, 0.120f, 1.16f, 1.12f}, // charger
    {0.008f, 0.00f, 0.070f, 1.20f, 1.20f}, // turret
    {0.014f, 0.60f, 0.080f, 1.18f, 1.12f}, // sniper
    {0.018f, 0.00f, 0.110f, 1.24f, 1.24f}  // rival
};

// ---- particles -------------------------------------------------------------------------------------------------------------------------------------------------
enum class Kind
{
    Glow,
    Spark,
    Smoke,
    Shard,
    Ring,
    Steam,
    Blast // an explosion that goes off after `delay` steps
};

struct Particle
{
    bool active = false;
    Kind kind = Kind::Glow;
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float life = 0.0f, maxLife = 1.0f;
    float size = 0.0f, grow = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float rot = 0.0f, spin = 0.0f;
    float drag = 1.0f;
    int delay = 0;
};

constexpr int PARTICLE_MAX = 360;
Particle g_Particles[PARTICLE_MAX];
int g_Cursor = 0;

Particle* NewParticle(Kind kind)
{
    for (int i = 0; i < PARTICLE_MAX; ++i)
    {
        Particle& p = g_Particles[(g_Cursor + i) % PARTICLE_MAX];
        if (!p.active)
        {
            g_Cursor = (g_Cursor + i + 1) % PARTICLE_MAX;
            p = Particle{};
            p.active = true;
            p.kind = kind;
            return &p;
        }
    }
    return nullptr; // full: an effect is skipped, never the game
}

void PolarityRgb(Polarity polarity, float& r, float& g, float& b)
{
    GameVfx::GetPolarityColor(polarity == Polarity::Red, r, g, b);
}

void Velocity(float angle, float speed, float& vx, float& vy)
{
    vx = std::cos(angle) * speed;
    vy = std::sin(angle) * speed / ASPECT;
}

void SpawnSpark(float x, float y, float angle, float speed, float length, int life, float r, float g, float b)
{
    if (Particle* p = NewParticle(Kind::Spark))
    {
        p->x = x;
        p->y = y;
        Velocity(angle, speed, p->vx, p->vy);
        p->size = length;
        p->life = p->maxLife = static_cast<float>(life);
        p->r = r;
        p->g = g;
        p->b = b;
        p->drag = 0.93f;
    }
}

void SpawnShard(float x, float y, float angle, float speed, float size, int life, float r, float g, float b)
{
    if (Particle* p = NewParticle(Kind::Shard))
    {
        p->x = x;
        p->y = y;
        Velocity(angle, speed, p->vx, p->vy);
        p->size = size;
        p->life = p->maxLife = static_cast<float>(life);
        p->r = r;
        p->g = g;
        p->b = b;
        p->rot = RndRange(0.0f, TWO_PI);
        p->spin = RndRange(-0.25f, 0.25f);
        p->drag = 0.965f;
    }
}

void SpawnGlowDot(float x, float y, float angle, float speed, float size, int life, float r, float g, float b)
{
    if (Particle* p = NewParticle(Kind::Glow))
    {
        p->x = x;
        p->y = y;
        Velocity(angle, speed, p->vx, p->vy);
        p->size = size;
        p->grow = -size / static_cast<float>(life) * 0.7f;
        p->life = p->maxLife = static_cast<float>(life);
        p->r = r;
        p->g = g;
        p->b = b;
        p->drag = 0.94f;
    }
}

void SpawnSmoke(float x, float y, float vx, float vy, float size, int life, float shade)
{
    if (Particle* p = NewParticle(Kind::Smoke))
    {
        p->x = x;
        p->y = y;
        p->vx = vx;
        p->vy = vy;
        p->size = size;
        p->grow = size * 0.035f;
        p->life = p->maxLife = static_cast<float>(life);
        p->r = p->g = p->b = shade;
        p->drag = 0.97f;
    }
}

void SpawnSteam(float x, float y, float vx, float vy, float size, int life)
{
    if (Particle* p = NewParticle(Kind::Steam))
    {
        p->x = x;
        p->y = y;
        p->vx = vx;
        p->vy = vy;
        p->size = size;
        p->grow = size * 0.06f;
        p->life = p->maxLife = static_cast<float>(life);
        p->r = 0.86f;
        p->g = 0.93f;
        p->b = 1.0f;
        p->drag = 0.95f;
    }
}

void SpawnRing(float x, float y, float size, float grow, int life, float r, float g, float b)
{
    if (Particle* p = NewParticle(Kind::Ring))
    {
        p->x = x;
        p->y = y;
        p->size = size;
        p->grow = grow;
        p->life = p->maxLife = static_cast<float>(life);
        p->r = r;
        p->g = g;
        p->b = b;
    }
}

void SpawnBlast(float x, float y, float size, int delay)
{
    if (delay <= 0)
    {
        GameEffect::SpawnExplosion(x, y, size);
        return;
    }
    if (Particle* p = NewParticle(Kind::Blast))
    {
        p->x = x;
        p->y = y;
        p->size = size;
        p->delay = delay;
        p->life = p->maxLife = 1000.0f;
    }
}

// a shard of hull: steel, a little lighter or darker; every few are lit in the colour of the enemy (the emissive plating that broke off)
// The debris of a dead enemy: CRYSTALS (GameEffect::SpawnCrystals), in the colour of its polarity - about half as many as the old steel shards (the call keeps
// its numbers: they say how big the enemy is), at most 14, thrown a little faster and gone in 0.2 - 0.33 s.
void Debris(float x, float y, int count, float sizeLow, float sizeHigh, float speedLow, float speedHigh, float baseAngle, float spread, Polarity polarity)
{
    const int n = std::max(4, std::min(14, count / 2));
    GameEffect::SpawnCrystals(x, y, n, (sizeLow + sizeHigh) * 0.5f * 0.62f, GameEffect::TintOf(polarity), baseAngle, spread, speedLow * 2.0f, speedHigh * 1.6f, 12, 20);
}

void Sparks(float x, float y, int count, float speedLow, float speedHigh, float baseAngle, float spread, float lengthLow, float lengthHigh, float r, float g, float b)
{
    for (int i = 0; i < count; ++i)
    {
        SpawnSpark(x, y, baseAngle + RndRange(-spread, spread), RndRange(speedLow, speedHigh), RndRange(lengthLow, lengthHigh), 8 + static_cast<int>(Rnd() * 9.0f), r, g, b);
    }
}

// ---- stateless drawing, one function per effect ---------------------------------------------------------------------------------------------------------------
void DrawEntrance(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    if (e.state != EnemyState::Enter || e.age >= 90)
    {
        return;
    }
    const float fade = 1.0f - static_cast<float>(e.age) / 90.0f;
    // streaks of speed behind the hull, and the glow of the edge of the screen that it came through
    for (int k = 0; k < 6; ++k)
    {
        const float seed = static_cast<float>((e.groupId * 7 + k * 13) & 255) / 255.0f;
        const float yy = p.y + (seed - 0.5f) * p.hh * 2.0f * 1.5f;
        const float length = (0.10f + 0.26f * Fract(seed * 7.3f)) * (0.35f + 0.65f * fade);
        const float xx = p.x + p.hw * 0.7f + length * 0.5f;
        GameVfx::Draw(Sprite::Spark, xx, yy, length, 0.007f, 0.0f, Mix(v.r, 1.0f, 0.5f), Mix(v.g, 1.0f, 0.5f), Mix(v.b, 1.0f, 0.5f), 0.45f * fade);
    }
    const float edge = Clamp01(1.0f - std::fabs(p.x - 1.0f) / 0.22f);
    if (edge > 0.01f)
    {
        Glow(1.0f, p.y, 0.30f, v.r, v.g, v.b, 0.50f * edge);
        GameVfx::Draw(Sprite::Glow, 1.0f, p.y, 0.05f, 0.55f, 0.0f, 1.0f, 1.0f, 1.0f, 0.35f * edge);
    }
}

// the sine: it rides a wave and leaves the wave behind it as a ribbon of light (the path is a formula, so the ribbon is exact)
void DrawSineRibbon(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    float px, py;
    p.Point(0.96f, 0.5f, px, py);
    float lastX = px, lastY = py;
    constexpr int SEGMENTS = 16;
    for (int i = 1; i <= SEGMENTS; ++i)
    {
        const float tau = static_cast<float>(i) * 3.0f;
        const float moveTime = e.moveTime - tau / 60.0f;
        if (moveTime < 0.0f)
        {
            break;
        }
        const float x = e.x + e.vx * tau - (e.x - v.x);
        const float y = e.baseY + std::sin(moveTime * e.frequency * PI) * e.amplitude - (e.y - v.y);
        const float life = 1.0f - static_cast<float>(i) / static_cast<float>(SEGMENTS + 1);
        GameVfx::DrawLine(lastX, lastY, x, y, 0.0065f * life + 0.0015f, v.r, v.g, v.b, 0.50f * life * v.alpha);
        if (i % 2 == 0)
        {
            Glow(x, y, 0.020f * life + 0.006f, v.r, v.g, v.b, 0.55f * life * v.alpha);
        }
        lastX = x;
        lastY = y;
    }
}

// the charger: a wake of heat behind a ram, and the nose that heats while it winds up
void DrawChargerWake(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    if (e.state != EnemyState::Charging)
    {
        return;
    }
    const float length = std::sqrt(e.vx * e.vx + e.vy * e.vy * ASPECT * ASPECT);
    if (length < 1.0e-5f)
    {
        return;
    }
    const float bx = -e.vx / length; // the way back, in equal scale units
    const float by = -e.vy * ASPECT / length;
    const float nx = -by;
    const float ny = bx;
    float tailX, tailY;
    p.Point(0.60f, 0.5f, tailX, tailY); // it starts inside the hull (drawn under it), so the hard edge of the wake is hidden
    const float half = p.hh * ASPECT * 1.05f;
    const float reach = 0.42f;
    auto corner = [&](float back, float side, float alpha)
    {
        return Renderer2DVertex{tailX + bx * back + nx * side, tailY + (by * back + ny * side) / ASPECT, v.r, v.g, v.b, alpha};
    };
    const Renderer2DVertex wake[4] = {corner(0.0f, half, 0.26f), corner(reach, half * 0.18f, 0.0f), corner(reach, -half * 0.18f, 0.0f), corner(0.0f, -half, 0.26f)};
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    Renderer2D_DrawPolygon(wake, 4);
    Renderer2D_SetBlendMode(previous);
    for (int k = 0; k < 4; ++k) // hot streaks
    {
        const float seed = static_cast<float>((e.groupId * 5 + k * 11) & 63) / 63.0f;
        const float t = Fract(static_cast<float>(v.frame) * 0.05f + seed);
        const float side = (seed - 0.5f) * 1.6f * half;
        const float sx = tailX + bx * (0.04f + t * 0.30f) + nx * side;
        const float sy = tailY + (by * (0.04f + t * 0.30f) + ny * side) / ASPECT;
        GameVfx::DrawLine(sx, sy, sx + bx * 0.05f, sy + by * 0.05f / ASPECT, 0.0045f, 1.0f, 0.95f, 0.85f, 0.55f * (1.0f - t));
    }
}

void DrawScout(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    const float t = static_cast<float>(v.frame);
    static const float TIPS[2][2] = {{0.80f, 0.10f}, {0.80f, 0.90f}}; // the pods on the wing tips
    for (int k = 0; k < 2; ++k) // navigation lights on the wing tips, blinking in turn
    {
        float x, y;
        p.Point(TIPS[k][0], TIPS[k][1], x, y);
        if (Fract(t / 46.0f + static_cast<float>(k) * 0.5f + e.baseY) < 0.16f)
        {
            Glow(x, y, 0.024f, v.r, v.g, v.b, 0.85f * v.alpha);
            Glow(x, y, 0.009f, 1.0f, 1.0f, 1.0f, 0.95f * v.alpha);
        }
    }
    if (e.shotTimer > 0 && e.shotTimer <= 9 && e.state == EnemyState::Enter) // the nose gathers light before the shot
    {
        float x, y;
        p.Point(0.08f, 0.5f, x, y); // (the light in the nose)
        const float k = 1.0f - static_cast<float>(e.shotTimer) / 9.0f;
        Glow(x, y, 0.012f + 0.032f * k, v.r, v.g, v.b, 0.35f + 0.55f * k);
        Glow(x, y, 0.006f + 0.012f * k, 1.0f, 1.0f, 1.0f, 0.5f + 0.5f * k);
    }
}

void DrawSine(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    const float wave = e.moveTime * e.frequency * PI;
    float cx, cy;
    p.Point(0.30f, 0.5f, cx, cy);
    // a halo that breathes with the wave, and two sparks that circle the core
    Ring(cx, cy, 0.060f + 0.020f * std::sin(wave), v.r, v.g, v.b, 0.28f * v.alpha);
    for (int k = 0; k < 2; ++k)
    {
        float x, y;
        OnCircle(cx, cy, 0.034f, static_cast<float>(v.frame) * 0.12f + static_cast<float>(k) * PI, x, y);
        Glow(x, y, 0.016f, v.r, v.g, v.b, 0.70f * v.alpha);
    }
}

void DrawHeavy(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    static const float VENTS[2][2] = {{0.57f, 0.33f}, {0.57f, 0.67f}}; // the two mine-bay doors on its back
    const float strength = Clamp01(0.10f + 0.90f * v.charge + (e.kick > 0 ? static_cast<float>(e.kick) / 8.0f : 0.0f));
    for (int k = 0; k < 2; ++k) // the grilles of the vents heat up while it winds up a ring, and flash when it fires
    {
        float x, y;
        p.Point(VENTS[k][0], VENTS[k][1], x, y);
        GameVfx::Draw(Sprite::Glow, x, y, p.hw * 0.55f, p.hh * 0.16f, p.rot, v.r, v.g, v.b, (0.12f + 0.55f * strength) * v.alpha);
        if (strength > 0.55f)
        {
            GameVfx::Draw(Sprite::Glow, x, y, p.hw * 0.36f, p.hh * 0.07f, p.rot, 1.0f, 1.0f, 1.0f, (strength - 0.55f) * 0.9f * v.alpha);
        }
    }
}

void DrawCharger(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    float nx, ny;
    p.Point(0.03f, 0.5f, nx, ny); // the tip of the ram lance
    const bool winding = e.state == EnemyState::ChargeWarning;
    const float heat = winding ? v.charge : (e.state == EnemyState::Charging ? 1.0f : 0.0f);
    if (heat > 0.02f) // the lance at the nose heats up
    {
        Glow(nx, ny, 0.030f + 0.070f * heat, Mix(v.r, 1.0f, 0.35f), Mix(v.g, 1.0f, 0.35f), Mix(v.b, 1.0f, 0.35f), (0.35f + 0.55f * heat) * v.alpha);
        Glow(nx, ny, 0.012f + 0.030f * heat, 1.0f, 1.0f, 1.0f, (0.4f + 0.6f * heat) * v.alpha);
        float fx, fy;
        p.Local(-p.hw * 1.0f - 0.03f - 0.06f * heat, 0.0f, fx, fy); // a flare ahead of the lance
        GameVfx::Draw(Sprite::Spark, fx, fy, 0.05f + 0.12f * heat, 0.012f, p.rot, 1.0f, 0.96f, 0.88f, 0.6f * heat * v.alpha);
    }
    static const float TIPS[2][2] = {{0.19f, 0.18f}, {0.19f, 0.82f}}; // the tips of the two jaws
    for (int k = 0; k < 2; ++k) // beacons on the wing tips: slow at rest, frantic while it winds up
    {
        float x, y;
        p.Point(TIPS[k][0], TIPS[k][1], x, y);
        const float rate = winding ? 0.9f : 0.12f;
        const float on = Clamp01(std::sin(static_cast<float>(v.frame) * rate + static_cast<float>(k) * PI));
        Glow(x, y, 0.016f + 0.014f * on, v.r, v.g, v.b, (0.25f + 0.6f * on) * v.alpha);
    }
}

void DrawTurret(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    const float t = static_cast<float>(v.frame);
    float cx, cy;
    p.Point(0.50f, 0.50f, cx, cy);
    // a ring of dashes turns round the core: slowly at rest, fast while it charges
    const float spin = t * (0.030f + 0.160f * v.charge);
    const float radius = p.hw * 0.64f; // on the armoured ring round the disc
    for (int k = 0; k < 6; ++k)
    {
        float x0, y0, x1, y1;
        OnCircle(cx, cy, radius, spin + TWO_PI * static_cast<float>(k) / 6.0f, x0, y0);
        OnCircle(cx, cy, radius, spin + TWO_PI * static_cast<float>(k) / 6.0f + 0.34f, x1, y1);
        GameVfx::DrawLine(x0, y0, x1, y1, 0.0035f, v.r, v.g, v.b, (0.30f + 0.55f * v.charge) * v.alpha);
    }
    float ax, ay, bx, by;
    p.Point(0.015f, 0.35f, ax, ay);
    p.Point(0.015f, 0.65f, bx, by);
    if (v.charge > 0.05f)
    {
        // Lightning crackles through the claws while it charges: between the two barrels, and from each barrel out to the nearest claw tip (the claws are the capacitors).
        float cx0, cy0, cx1, cy1;
        p.Point(0.49f, 0.06f, cx0, cy0); // the capacitors at the top and the bottom of the ring
        p.Point(0.49f, 0.94f, cx1, cy1);
        const float legs[3][4] = {{ax, ay, bx, by}, {ax, ay, cx0, cy0}, {bx, by, cx1, cy1}};
        for (int leg = 0; leg < 3; ++leg)
        {
            for (int arc = 0; arc < 2; ++arc)
            {
                float lastX = legs[leg][0], lastY = legs[leg][1];
                constexpr int STEPS = 7;
                for (int s = 1; s <= STEPS; ++s)
                {
                    const float f = static_cast<float>(s) / static_cast<float>(STEPS);
                    const int seed = (v.frame / 2) * 17 + arc * 31 + s * 7 + leg * 101 + e.groupId;
                    const float jx = (GameVfx::Hash(seed) - 0.5f) * 2.0f * p.hw * (leg == 0 ? 0.16f : 0.10f) * std::sin(f * PI);
                    const float jy = (GameVfx::Hash(seed + 977) - 0.5f) * 2.0f * p.hh * (leg == 0 ? 0.03f : 0.10f) * std::sin(f * PI);
                    const float x = Mix(legs[leg][0], legs[leg][2], f) + jx;
                    const float y = Mix(legs[leg][1], legs[leg][3], f) + jy;
                    GameVfx::DrawLine(lastX, lastY, x, y, 0.0055f, Mix(v.r, 1.0f, 0.65f), Mix(v.g, 1.0f, 0.65f), Mix(v.b, 1.0f, 0.65f), (0.32f + 0.60f * v.charge) * v.alpha);
                    lastX = x;
                    lastY = y;
                }
            }
        }
        Glow(cx0, cy0, 0.020f + 0.040f * v.charge, v.r, v.g, v.b, 0.6f * v.charge * v.alpha);
        Glow(cx1, cy1, 0.020f + 0.040f * v.charge, v.r, v.g, v.b, 0.6f * v.charge * v.alpha);
    }
    const float tip = Clamp01(v.charge + (e.kick > 0 ? static_cast<float>(e.kick) / 8.0f : 0.0f));
    for (int k = 0; k < 2; ++k)
    {
        const float x = k == 0 ? ax : bx;
        const float y = k == 0 ? ay : by;
        Glow(x, y, 0.018f + 0.060f * tip, v.r, v.g, v.b, (0.30f + 0.60f * tip) * v.alpha);
        Glow(x, y, 0.008f + 0.022f * tip, 1.0f, 1.0f, 1.0f, (0.35f + 0.65f * tip) * v.alpha);
    }
}

void DrawSniper(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    const float t = static_cast<float>(v.frame);
    float mx, my;
    p.Point(0.0f, 0.5f, mx, my);
    if (v.charge > 0.02f)
    {
        // rings run down the barrel towards the muzzle: the shot is being loaded
        for (int k = 0; k < 3; ++k)
        {
            const float f = Fract(v.charge * 2.2f + static_cast<float>(k) / 3.0f);
            float x, y;
            p.Point(Mix(0.38f, 0.02f, f), 0.5f, x, y);
            Ring(x, y, 0.040f * (1.0f - 0.4f * f), 1.0f, 1.0f, 1.0f, (0.15f + 0.45f * v.charge) * (1.0f - f * 0.4f) * v.alpha);
        }
        Glow(mx, my, 0.020f + 0.080f * v.charge, 1.0f, 1.0f, 1.0f, (0.25f + 0.65f * v.charge) * v.alpha);
    }
    else
    {
        Glow(mx, my, 0.018f + 0.004f * std::sin(t * 0.1f), 0.85f, 0.92f, 1.0f, 0.55f * v.alpha);
    }
    if (e.kick > 0) // the muzzle flash of the shot
    {
        const float k = static_cast<float>(e.kick) / 8.0f;
        Glow(mx, my, 0.14f * k, 1.0f, 1.0f, 1.0f, 0.85f * k);
        GameVfx::Draw(Sprite::Spark, mx - 0.05f * k, my, 0.12f * k, 0.020f, 0.0f, 1.0f, 1.0f, 1.0f, k);
    }
    // a glint on the lens of the scope: every 110 steps, for 8
    const int phase = (v.frame + e.groupId * 23) % 110;
    if (phase < 8)
    {
        float lx, ly;
        p.Point(0.35f, 0.5f, lx, ly);
        const float k = 1.0f - static_cast<float>(phase) / 8.0f;
        GameVfx::Draw(Sprite::Spark, lx, ly, 0.07f * k + 0.01f, 0.012f, 0.0f, 1.0f, 1.0f, 1.0f, 0.9f * k);
        GameVfx::Draw(Sprite::Spark, lx, ly, 0.07f * k + 0.01f, 0.012f, PI * 0.5f, 1.0f, 1.0f, 1.0f, 0.9f * k);
    }
}

void DrawRival(const EnemyArt::View& v, const EnemyArt::Pose& p)
{
    const Enemy& e = *v.enemy;
    const float t = static_cast<float>(v.frame);
    const bool half = e.hp <= e.maxHp / 2;
    const bool last = e.hp <= e.maxHp / 4;
    if (half) // the second half of the duel: a halo of two hexagons that turn against each other
    {
        const float radius = p.hw * 1.30f;
        for (int layer = 0; layer < (last ? 2 : 1); ++layer)
        {
            const float rot = t * 0.020f * (layer == 0 ? 1.0f : -1.5f);
            const float r = radius * (layer == 0 ? 1.0f : 0.86f);
            for (int k = 0; k < 6; ++k)
            {
                float x0, y0, x1, y1;
                OnCircle(p.x, p.y, r, rot + TWO_PI * static_cast<float>(k) / 6.0f, x0, y0);
                OnCircle(p.x, p.y, r, rot + TWO_PI * static_cast<float>(k + 1) / 6.0f, x1, y1);
                GameVfx::DrawLine(x0, y0, x1, y1, 0.0032f, v.r, v.g, v.b, (layer == 0 ? 0.30f : 0.20f) * v.alpha);
            }
        }
    }
    static const float BLADES[2][2] = {{0.74f, 0.21f}, {0.74f, 0.79f}}; // (its wing tips: the rival is the black mirror of the ship) // the rear blades flare while it dashes or coils
    const bool coil = e.state == EnemyState::AttackWarning && e.pendingAttackType == EnemyAttackType::Dash;
    if (IsRush(e) || coil)
    {
        for (int k = 0; k < 2; ++k)
        {
            float x, y;
            p.Point(BLADES[k][0], BLADES[k][1], x, y);
            Glow(x, y, 0.030f + (IsRush(e) ? 0.030f : 0.020f * v.charge), v.r, v.g, v.b, 0.70f * v.alpha);
            GameVfx::Draw(Sprite::Spark, x + 0.05f, y, 0.12f, 0.010f, 0.0f, v.r, v.g, v.b, 0.55f * v.alpha);
        }
    }
}

void EmitPoint(const Enemy& e, const EnemyArt::Pose& pose, float uLow, float uHigh, float vLow, float vHigh, float& x, float& y)
{
    (void)e;
    pose.Point(RndRange(uLow, uHigh), RndRange(vLow, vHigh), x, y);
}
}

// =====================================================================================================================================================
void EnemyArt::Pose::Local(float lx, float ly, float& px, float& py) const
{
    const float c = std::cos(rot);
    const float s = std::sin(rot);
    // the same rotation as Renderer2D_DrawTextureEx: equal scale in pixels, so y is scaled by the aspect ratio while it turns
    px = x + lx * c - ly * ASPECT * s;
    py = y + lx * s / ASPECT + ly * c;
}

void EnemyArt::Pose::Point(float u, float v, float& px, float& py) const
{
    Local((u - 0.5f) * 2.0f * hw, (0.5f - v) * 2.0f * hh, px, py);
}

EnemyArt::Pose EnemyArt::MakePose(const View& v)
{
    const Enemy& e = *v.enemy;
    Pose p;
    p.x = v.x;
    p.y = v.y;
    p.hw = v.hw;
    p.hh = v.hh;
    const float t = static_cast<float>(v.frame);
    const float phase = e.baseY * 7.0f + static_cast<float>(e.groupId) * 1.7f;
    const float charge = v.charge;
    if (e.kick > 0) // a shot kicks the hull back and it settles
    {
        const float k = static_cast<float>(e.kick) / 8.0f;
        float amount = 0.007f;
        if (e.type == EnemyType::Sniper) amount = 0.022f;
        else if (e.type == EnemyType::Turret) amount = 0.014f;
        else if (e.type == EnemyType::Heavy) amount = 0.012f;
        p.x += k * k * amount;
    }
    switch (e.type)
    {
    case EnemyType::Scout:
        p.rot = 0.05f * std::sin(t * 0.08f + phase);
        p.y += 0.002f * std::sin(t * 0.13f + phase);
        break;
    case EnemyType::Sine:
        p.rot = Clamp(-v.vy * 55.0f, -0.40f, 0.40f); // it banks into the wave
        break;
    case EnemyType::Heavy:
        p.y += 0.003f * std::sin(t * 0.045f + phase);
        if (charge > 0.0f)
        {
            p.x += std::sin(t * 1.9f) * 0.0045f * charge;
            p.y += std::cos(t * 2.3f) * 0.0035f * charge;
        }
        break;
    case EnemyType::Charger:
        if (e.state == EnemyState::Charging)
        {
            p.rot = Heading(e.vx, e.vy); // the lance points where it goes
        }
        else if (e.state == EnemyState::ChargeWarning)
        {
            p.x += std::sin(t * 2.1f) * 0.0045f * charge;
            p.y += std::cos(t * 2.7f) * 0.0035f * charge;
        }
        else
        {
            p.y += 0.003f * std::sin(t * 0.07f + phase);
        }
        break;
    case EnemyType::Turret:
        p.y += 0.0025f * std::sin(t * 0.04f + phase);
        if (charge > 0.0f)
        {
            p.x += std::sin(t * 2.4f) * 0.0025f * charge;
        }
        break;
    case EnemyType::Sniper:
        p.rot = Clamp(-v.vy * 22.0f, -0.35f, 0.35f);
        p.y += 0.002f * std::sin(t * 0.06f + phase);
        break;

    case EnemyType::Rival:
        if (e.state == EnemyState::Attack && e.pendingAttackType == EnemyAttackType::Dash)
        {
            p.rot = Heading(e.vx, e.vy);
        }
        else if (e.state == EnemyState::AttackWarning && e.pendingAttackType == EnemyAttackType::Dash)
        {
            p.x += std::sin(t * 2.3f) * 0.004f * charge;
            p.y += std::cos(t * 2.9f) * 0.003f * charge;
        }
        else
        {
            p.rot = Clamp(-v.vy * 14.0f, -0.25f, 0.25f);
            p.y += 0.0025f * std::sin(t * 0.06f + phase);
        }
        break;
    }
    return p;
}

void EnemyArt::VisualScale(EnemyType type, float& widthScale, float& heightScale)
{
    const int index = static_cast<int>(type);
    const TypeSpec& spec = SPECS[index < 0 || index > 7 ? 0 : index];
    widthScale = spec.scaleW;
    heightScale = spec.scaleH;
}

void EnemyArt::Reset()
{
    for (Particle& p : g_Particles)
    {
        p = Particle{};
    }
    g_Cursor = 0;
}

void EnemyArt::Update(Enemy enemies[], int count)
{
    for (Particle& p : g_Particles)
    {
        if (!p.active)
        {
            continue;
        }
        if (p.delay > 0)
        {
            if (--p.delay == 0 && p.kind == Kind::Blast)
            {
                GameEffect::SpawnExplosion(p.x, p.y, p.size);
                p.active = false;
            }
            continue;
        }
        p.x += p.vx;
        p.y += p.vy;
        p.vx *= p.drag;
        p.vy *= p.drag;
        p.rot += p.spin;
        p.size += p.grow;
        if (p.size < 0.0f)
        {
            p.size = 0.0f;
        }
        if (--p.life <= 0.0f)
        {
            p.active = false;
        }
    }
    // the emitters that belong to an enemy: damage and the steam of a heavy
    for (int i = 0; i < count; ++i)
    {
        const Enemy& e = enemies[i];
        if (!e.active)
        {
            continue;
        }
        float widthScale, heightScale;
        VisualScale(e.type, widthScale, heightScale);
        View view;
        view.enemy = &e;
        view.x = e.x;
        view.y = e.y;
        view.hw = e.halfWidth * widthScale;
        view.hh = e.halfHeight * heightScale;
        const Pose pose = MakePose(view);
        if (IsElite(e))
        {
            const float ratio = static_cast<float>(e.hp) / static_cast<float>(e.maxHp > 0 ? e.maxHp : 1);
            float x, y;
            if (ratio < 0.66f && Rnd() < (ratio < 0.33f ? 0.10f : 0.045f)) // sparks jump out of the damaged hull
            {
                EmitPoint(e, pose, 0.20f, 0.90f, 0.18f, 0.82f, x, y);
                Sparks(x, y, 3, 0.004f, 0.010f, RndRange(0.0f, TWO_PI), 0.5f, 0.02f, 0.05f, 1.0f, 0.82f, 0.50f);
            }
            if (ratio < 0.33f)
            {
                if (e.age % 3 == 0) // smoke trails behind it
                {
                    EmitPoint(e, pose, 0.45f, 0.95f, 0.25f, 0.75f, x, y);
                    SpawnSmoke(x, y, RndRange(0.001f, 0.003f), RndRange(-0.0015f, 0.003f), RndRange(0.022f, 0.036f), 44, RndRange(0.07f, 0.16f));
                }
                if (Rnd() < 0.10f) // embers
                {
                    EmitPoint(e, pose, 0.30f, 0.95f, 0.25f, 0.75f, x, y);
                    SpawnGlowDot(x, y, RndRange(0.0f, TWO_PI), RndRange(0.001f, 0.004f), 0.020f, 14, 1.0f, 0.55f, 0.18f);
                }
            }
        }
        if (e.type == EnemyType::Heavy && e.state == EnemyState::AttackWarning && e.age % 2 == 0) // steam from the vents while it winds up
        {
            for (int k = 0; k < 2; ++k)
            {
                float x, y;
                pose.Point(0.97f, k == 0 ? 0.30f : 0.70f, x, y); // (the two engines at its back)
                SpawnSteam(x, y, RndRange(0.001f, 0.004f), (k == 0 ? 1.0f : -1.0f) * RndRange(0.002f, 0.006f), 0.022f, 24);
            }
        }
    }
}

void EnemyArt::DrawUnder(const View& view, const Pose& pose)
{
    DrawEntrance(view, pose);
    switch (view.enemy->type)
    {
    case EnemyType::Sine:
        DrawSineRibbon(view, pose);
        break;
    case EnemyType::Charger:
        DrawChargerWake(view, pose);
        break;

    default:
        break;
    }
}

void EnemyArt::DrawGlow(const View& v, const Pose& p)
{
    const Enemy& e = *v.enemy;
    const int index = static_cast<int>(e.type);
    const int texture = AssetManager::EnemyGlow(index);
    if (texture < 0)
    {
        return;
    }
    const TypeSpec& spec = SPECS[index];
    const float t = static_cast<float>(v.frame);
    const float phase = e.baseY * 7.0f + static_cast<float>(e.groupId) * 1.7f;
    const float r = Mix(v.r, 1.0f, spec.whiten);
    const float g = Mix(v.g, 1.0f, spec.whiten);
    const float b = Mix(v.b, 1.0f, spec.whiten);
    const bool rush = IsRush(e);
    // the light breathes, brightens while the enemy winds up and blazes while it rams
    float intensity = 0.78f + 0.22f * std::sin(t * spec.pulse + phase);
    if (v.charge > 0.0f)
    {
        intensity += 0.60f * v.charge * (0.80f + 0.20f * std::sin(t * 0.9f));
    }
    if (rush)
    {
        intensity = 1.35f;
    }
    // a badly damaged elite: the plating gutters, goes dark for a moment, comes back
    if (IsElite(e) && e.hp * 3 < e.maxHp)
    {
        const float n = GameVfx::Hash(v.frame / 2 + e.groupId * 31);
        intensity *= n < 0.20f ? 0.12f : 0.60f + 0.40f * std::sin(t * 0.7f + phase);
    }
    if (e.hitFlashTimer > 0)
    {
        intensity += 0.9f * static_cast<float>(e.hitFlashTimer) / 4.0f;
    }
    if (e.polaritySwitchTimer > 0)
    {
        intensity += static_cast<float>(e.polaritySwitchTimer) / 18.0f; // the rival flips its colour: the plating flares
    }
    intensity = std::max(intensity, 0.0f);
    const float pad = 1.0f + 2.0f * GLOW_PAD;
    const float width = p.hw * 2.0f * pad;
    const float height = p.hh * 2.0f * pad;
    const float k = std::max(1.0f, intensity);
    const float a = std::min(1.0f, intensity) * v.alpha;
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    Renderer2D_DrawTextureEx(texture, p.x, p.y, width, height, p.rot, r * k, g * k, b * k, a, 0.0f, 0.0f, 1.0f, 1.0f);
    if (intensity > 1.0f) // beyond full: the core turns white
    {
        Renderer2D_DrawTextureEx(texture, p.x, p.y, width, height, p.rot, 1.0f, 1.0f, 1.0f, std::min(0.6f, (intensity - 1.0f) * 0.6f) * v.alpha, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    // a band of light runs along the plating from the tail to the nose (the chevrons point that way): quicker while it winds up, much quicker in a ram
    const float speed = spec.flowSpeed * (rush ? 2.6f : (v.charge > 0.0f ? 2.0f : 1.0f));
    const float centre = 1.0f - Fract(t * speed + phase * 0.13f);
    static const float WIDTH[3] = {0.22f, 0.12f, 0.055f};
    static const float ALPHA[3] = {0.18f, 0.26f, 0.45f};
    for (int slice = 0; slice < 3; ++slice)
    {
        const float u0 = Clamp01(centre - WIDTH[slice]);
        const float u1 = Clamp01(centre + WIDTH[slice]);
        if (u1 - u0 < 0.004f)
        {
            continue;
        }
        float cx, cy;
        p.Local(((u0 + u1) * 0.5f - 0.5f) * width, 0.0f, cx, cy);
        Renderer2D_DrawTextureEx(texture, cx, cy, (u1 - u0) * width, height, p.rot, Mix(r, 1.0f, 0.55f), Mix(g, 1.0f, 0.55f), Mix(b, 1.0f, 0.55f),
                                 ALPHA[slice] * std::min(1.0f, 0.5f + 0.5f * intensity) * v.alpha, u0, 0.0f, u1, 1.0f);
    }
    Renderer2D_SetBlendMode(previous);
}

void EnemyArt::DrawOver(const View& view, const Pose& pose)
{
    switch (view.enemy->type)
    {
    case EnemyType::Scout:
        DrawScout(view, pose);
        break;
    case EnemyType::Sine:
        DrawSine(view, pose);
        break;
    case EnemyType::Heavy:
        DrawHeavy(view, pose);
        break;
    case EnemyType::Charger:
        DrawCharger(view, pose);
        break;
    case EnemyType::Turret:
        DrawTurret(view, pose);
        break;
    case EnemyType::Sniper:
        DrawSniper(view, pose);
        break;

    case EnemyType::Rival:
        DrawRival(view, pose);
        break;
    }
}

void EnemyArt::DrawParticles()
{
    for (const Particle& p : g_Particles)
    {
        if (!p.active || p.delay > 0)
        {
            continue;
        }
        const float life = Clamp01(p.life / p.maxLife);
        switch (p.kind)
        {
        case Kind::Glow:
            Glow(p.x, p.y, p.size, p.r, p.g, p.b, life * 0.9f);
            Glow(p.x, p.y, p.size * 0.4f, 1.0f, 1.0f, 1.0f, life);
            break;
        case Kind::Spark:
        {
            const float angle = std::atan2(p.vy * ASPECT, p.vx);
            GameVfx::Draw(Sprite::Spark, p.x, p.y, p.size * (0.4f + 0.6f * life), 0.011f, angle, p.r, p.g, p.b, life);
            break;
        }
        case Kind::Smoke:
            GameVfx::DrawAlpha(Sprite::Glow, p.x, p.y, p.size, p.size / ASPECT, 0.0f, p.r, p.g, p.b, life * 0.50f);
            break;
        case Kind::Steam:
            GameVfx::DrawAlpha(Sprite::Glow, p.x, p.y, p.size, p.size / ASPECT, 0.0f, p.r, p.g, p.b, life * 0.30f);
            break;
        case Kind::Shard:
            GameVfx::DrawAlpha(Sprite::Shard, p.x, p.y, p.size, p.size * 0.62f / ASPECT, p.rot, p.r, p.g, p.b, Clamp01(life * 1.8f));
            break;
        case Kind::Ring:
            Ring(p.x, p.y, p.size, p.r, p.g, p.b, life * 0.8f);
            break;
        case Kind::Blast:
            break;
        }
    }
}

void EnemyArt::OnHit(const Enemy& e, float hx, float hy, bool armourHeld)
{
    float r = 1.0f, g = 0.93f, b = 0.82f;
    if (armourHeld) // the shot rang off the armour: amber metal sparks and a flash
    {
        r = 1.0f;
        g = 0.82f;
        b = 0.48f;
        SpawnRing(hx, hy, 0.040f, 0.010f, 8, 1.0f, 0.92f, 0.70f);
    }
    Sparks(hx, hy, armourHeld ? 7 : 4, 0.006f, 0.016f, PI, 1.15f, 0.03f, 0.07f, r, g, b); // the shots come from the left: the sparks fly back to it
    if (e.type == EnemyType::Sniper) // glass and thin plating chip off
    {
        for (int i = 0; i < 2; ++i)
        {
            SpawnShard(hx, hy, PI + RndRange(-1.0f, 1.0f), RndRange(0.004f, 0.010f), 0.018f, 18, 0.70f, 0.86f, 1.0f);
        }
    }
}

void EnemyArt::OnDeath(const Enemy& e)
{
    const float x = e.x;
    const float y = e.y;
    float pr, pg, pb;
    PolarityRgb(e.polarity, pr, pg, pb);
    float widthScale, heightScale;
    VisualScale(e.type, widthScale, heightScale);
    const float hw = e.halfWidth * widthScale;
    const float hh = e.halfHeight * heightScale;
    switch (e.type)
    {
    case EnemyType::Scout:
        Debris(x, y, 8, 0.028f, 0.050f, 0.006f, 0.016f, 0.0f, PI, e.polarity);
        Sparks(x, y, 8, 0.008f, 0.020f, 0.0f, PI, 0.03f, 0.07f, 1.0f, 0.88f, 0.62f);
        SpawnRing(x, y, 0.10f, 0.012f, 14, pr, pg, pb);
        SpawnSmoke(x, y, 0.001f, 0.002f, 0.030f, 30, 0.14f);
        break;
    case EnemyType::Sine:
        Debris(x, y, 9, 0.028f, 0.052f, 0.006f, 0.017f, 0.0f, PI, e.polarity);
        Sparks(x, y, 8, 0.008f, 0.020f, 0.0f, PI, 0.03f, 0.07f, 1.0f, 0.88f, 0.62f);
        for (int i = 0; i < 8; ++i) // the ribbon of the wave scatters
        {
            SpawnGlowDot(x + RndRange(0.0f, 0.18f), y + RndRange(-0.06f, 0.06f), RndRange(0.0f, TWO_PI), RndRange(0.002f, 0.007f), 0.024f, 20, pr, pg, pb);
        }
        SpawnRing(x, y, 0.12f, 0.014f, 15, pr, pg, pb);
        break;
    case EnemyType::Heavy:
        Debris(x, y, 18, 0.050f, 0.110f, 0.004f, 0.016f, 0.0f, PI, e.polarity);
        Sparks(x, y, 14, 0.008f, 0.024f, 0.0f, PI, 0.04f, 0.09f, 1.0f, 0.86f, 0.55f);
        SpawnRing(x, y, 0.16f, 0.022f, 22, 1.0f, 1.0f, 1.0f);
        SpawnRing(x, y, 0.10f, 0.016f, 18, pr, pg, pb);
        SpawnBlast(x + hw * 0.5f, y + hh * 0.4f, 0.09f, 5);
        SpawnBlast(x - hw * 0.4f, y - hh * 0.5f, 0.09f, 9);
        SpawnBlast(x + hw * 0.1f, y, 0.11f, 14);
        for (int i = 0; i < 8; ++i) SpawnSmoke(x + RndRange(-hw, hw), y + RndRange(-hh, hh), RndRange(0.0f, 0.004f), RndRange(-0.002f, 0.004f), RndRange(0.040f, 0.070f), 56, RndRange(0.06f, 0.14f));
        break;
    case EnemyType::Charger:
    {
        const float heading = std::atan2(e.vy * ASPECT, e.vx);
        Debris(x, y, 12, 0.036f, 0.070f, 0.008f, 0.022f, heading, 0.7f, e.polarity); // the lance shatters forward
        Sparks(x, y, 10, 0.012f, 0.030f, heading, 0.35f, 0.05f, 0.11f, 1.0f, 0.90f, 0.70f);
        SpawnRing(x, y, 0.12f, 0.016f, 16, pr, pg, pb);
        SpawnBlast(x - 0.04f, y, 0.07f, 6);
        break;
    }
    case EnemyType::Turret:
        Debris(x, y, 14, 0.050f, 0.100f, 0.004f, 0.015f, 0.0f, PI, e.polarity);
        GameEffect::SpawnCrystals(x - hw * 0.4f, y + hh * 0.7f, 1, 0.075f, GameEffect::TintOf(e.polarity), PI * 0.5f, 0.2f, 0.012f, 0.016f, 18, 24); // the two claws break off and fly apart
        GameEffect::SpawnCrystals(x - hw * 0.4f, y - hh * 0.7f, 1, 0.075f, GameEffect::TintOf(e.polarity), -PI * 0.5f, 0.2f, 0.012f, 0.016f, 18, 24);
        Sparks(x - hw * 0.9f, y, 12, 0.008f, 0.020f, PI, 1.2f, 0.04f, 0.09f, Mix(pr, 1.0f, 0.5f), Mix(pg, 1.0f, 0.5f), Mix(pb, 1.0f, 0.5f)); // the charge goes out of the barrels
        SpawnRing(x, y, 0.16f, 0.020f, 20, 1.0f, 1.0f, 1.0f);
        SpawnBlast(x - hw * 0.5f, y + hh * 0.4f, 0.08f, 4);
        SpawnBlast(x - hw * 0.5f, y - hh * 0.4f, 0.08f, 8);
        SpawnBlast(x + hw * 0.2f, y, 0.10f, 13);
        for (int i = 0; i < 6; ++i) SpawnSmoke(x + RndRange(-hw, hw), y + RndRange(-hh, hh), RndRange(0.0f, 0.004f), RndRange(-0.002f, 0.004f), RndRange(0.040f, 0.065f), 52, RndRange(0.06f, 0.14f));
        break;
    case EnemyType::Sniper:
        GameEffect::SpawnCrystals(x - hw * 0.8f, y, 1, 0.07f, GameEffect::CrystalTint::White, PI, 0.15f, 0.014f, 0.020f, 18, 24); // the barrel snaps off
        GameEffect::SpawnCrystals(x, y, 4, 0.030f, GameEffect::CrystalTint::White, 0.0f, PI, 0.010f, 0.026f, 12, 18); // glass
        Debris(x, y, 10, 0.040f, 0.075f, 0.005f, 0.016f, 0.0f, PI, e.polarity);
        Sparks(x, y, 10, 0.008f, 0.022f, 0.0f, PI, 0.04f, 0.08f, 1.0f, 1.0f, 1.0f);
        SpawnRing(x, y, 0.14f, 0.018f, 18, 1.0f, 1.0f, 1.0f);
        SpawnBlast(x, y + hh * 0.3f, 0.07f, 6);
        break;

    case EnemyType::Rival:
        Debris(x, y, 28, 0.050f, 0.120f, 0.005f, 0.020f, 0.0f, PI, e.polarity);
        Sparks(x, y, 22, 0.010f, 0.030f, 0.0f, PI, 0.05f, 0.11f, 1.0f, 0.90f, 0.66f);
        SpawnRing(x, y, 0.20f, 0.028f, 28, 1.0f, 1.0f, 1.0f);
        SpawnRing(x, y, 0.12f, 0.020f, 22, 1.0f, 0.30f, 0.22f);
        SpawnRing(x, y, 0.12f, 0.024f, 26, 0.25f, 0.62f, 1.0f); // the rival has both colours: it dies in both
        SpawnBlast(x + hw * 0.5f, y + hh * 0.4f, 0.10f, 4);
        SpawnBlast(x - hw * 0.5f, y - hh * 0.5f, 0.10f, 9);
        SpawnBlast(x - hw * 0.2f, y + hh * 0.5f, 0.11f, 15);
        SpawnBlast(x + hw * 0.1f, y, 0.14f, 22);
        for (int i = 0; i < 12; ++i) SpawnSmoke(x + RndRange(-hw, hw), y + RndRange(-hh, hh), RndRange(0.0f, 0.004f), RndRange(-0.002f, 0.004f), RndRange(0.045f, 0.080f), 62, RndRange(0.06f, 0.14f));
        for (int i = 0; i < 10; ++i) SpawnGlowDot(x, y, RndRange(0.0f, TWO_PI), RndRange(0.004f, 0.014f), 0.030f, 26, i % 2 == 0 ? 1.0f : 0.25f, i % 2 == 0 ? 0.30f : 0.62f, i % 2 == 0 ? 0.22f : 1.0f);
        break;
    }
}



