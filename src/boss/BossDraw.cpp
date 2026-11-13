// How the boss is DRAWN: its hull in the three phases, the drones and the pods, the telegraphs of its attacks, its beams and its laser, the flashes of its end. The logic (the phases,
// the attacks, the bullets, the damage) is GameBoss.cpp; what the two share is BossInternal.h.

#include "boss/GameBoss.h"
#include "core/GameSettings.h"
#include "boss/BossMask.h"
#include "game/BeamFx.h"
#include "rogue/Balance.h"
#include "rogue/Modules.h"
#include "rogue/Meta.h"
#include "game/GameState.h"
#include "core/AssetManager.h"
#include "core/Collision.h"
#include "core/Renderer2D.h"
#include "game/GameEffect.h"
#include "core/GameAudio.h"
#include "game/GameVfx.h"
#include "game/GamePlayer.h"
#include "core/GameTimer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "boss/BossInternal.h"

using GameVfx::Sprite;
using namespace BossInternal;

namespace
{
// The art of the dreadnought (assets/textures/boss/).
int TexFull()
{
    return AssetManager::Get(AssetManager::Tex::BossHull); // turrets mounted
}

int TexBare()
{
    return AssetManager::Get(AssetManager::Tex::BossHullBare); // empty turret sockets
}

int TexBerserk()
{
    return AssetManager::Get(AssetManager::Tex::BossHullBerserk); // armour blown off, reactor exposed
}

int TexPod(int i)
{
    return AssetManager::Get(i == 0 ? AssetManager::Tex::BossPodRed : AssetManager::Tex::BossPodBlue);
}

// which version of the hull is on screen
bool TurretsMounted()
{
    if (g_Phase == 1) return true;
    return g_Phase == 2 && g_State == State::Transition && g_TransT < UNDOCK_AT;
}

bool PodsVisible()
{
    if (g_Phase == 2) return !(g_State == State::Transition && g_TransT < UNDOCK_AT);
    return g_Phase == 3 && g_State != State::Defeated;
}

bool BerserkArt()
{
    if (g_Phase != 3) return false;
    if (g_State == State::Transition) return g_TransT >= 62;
    return true;
}

// cracks of the citadel (phase 1), u / v on the hull texture; the lower half mirrors the upper half
constexpr int CRACK_COUNT = 6;
constexpr float CRACKS[CRACK_COUNT][4][2] = {{{0.48f, 0.49f}, {0.51f, 0.45f}, {0.50f, 0.42f}, {0.54f, 0.38f}},
                                             {{0.62f, 0.49f}, {0.60f, 0.44f}, {0.64f, 0.41f}, {0.62f, 0.37f}},
                                             {{0.72f, 0.49f}, {0.70f, 0.45f}, {0.73f, 0.42f}, {0.71f, 0.39f}},
                                             {{0.48f, 0.51f}, {0.51f, 0.55f}, {0.50f, 0.58f}, {0.54f, 0.62f}},
                                             {{0.62f, 0.51f}, {0.60f, 0.56f}, {0.64f, 0.59f}, {0.62f, 0.63f}},
                                             {{0.72f, 0.51f}, {0.70f, 0.55f}, {0.73f, 0.58f}, {0.71f, 0.61f}}};

float Time()
{
    return static_cast<float>(g_DrawFrame);
}
float Pulse(float speed, float phase = 0.0f)
{
    return 0.5f + 0.5f * std::sin(Time() * speed + phase);
}

void Glow(float x, float y, float size, float r, float g, float b, float a)
{
    GameVfx::DrawRound(Sprite::Glow, x, y, size, r, g, b, a);
}

void Ring(float x, float y, float size, float r, float g, float b, float a)
{
    GameVfx::DrawRound(Sprite::Ring, x, y, size, r, g, b, a);
}

void Line(float x1, float y1, float x2, float y2, float th, float r, float g, float b, float a)
{
    GameVfx::DrawLine(x1, y1, x2, y2, th, r, g, b, a);
}

void Hexagon(float x, float y, float radius, float rotation, float th, float r, float g, float b, float a)
{
    for (int i = 0; i < 6; ++i) // (hard edges: the style of the effects)
    {
        const float a0 = rotation + TWO_PI * static_cast<float>(i) / 6.0f;
        const float a1 = rotation + TWO_PI * static_cast<float>(i + 1) / 6.0f;
        GameVfx::DrawBar(x + std::cos(a0) * radius, y + std::sin(a0) * radius / ASPECT, x + std::cos(a1) * radius,
                         y + std::sin(a1) * radius / ASPECT, th, r, g, b, a);
    }
}

void Brackets(float x, float y, float halfW, float halfH, float leg, float r, float g, float b, float a)
{
    for (int sx = -1; sx <= 1; sx += 2)
    {
        for (int sy = -1; sy <= 1; sy += 2)
        {
            const float cx = x + static_cast<float>(sx) * halfW;
            const float cy = y + static_cast<float>(sy) * halfH;
            GameVfx::DrawBar(cx, cy, cx - static_cast<float>(sx) * leg, cy, 0.0040f, r, g, b, a);
            GameVfx::DrawBar(cx, cy, cx, cy - static_cast<float>(sy) * leg / ASPECT, 0.0040f, r, g, b, a);
        }
    }
}

void ChargeGlow(float x, float y, float p, float scale, bool dual, Polarity pol)
{
    float r, g, b;
    Colour(pol, r, g, b);
    if (dual)
    {
        float rr, rg, rb, br, bg, bb;
        Colour(Polarity::Red, rr, rg, rb);
        Colour(Polarity::Blue, br, bg, bb);
        const float split = 0.014f * scale * (1.0f - 0.6f * p);
        const float size = (0.045f + 0.110f * p) * scale;
        Glow(x, y + split, size, rr, rg, rb, 0.25f + 0.60f * p);
        Glow(x, y - split, size, br, bg, bb, 0.25f + 0.60f * p);
        r = 0.88f;
        g = 0.62f;
        b = 1.00f;
    }
    else
    {
        Glow(x, y, (0.045f + 0.110f * p) * scale, r, g, b, 0.25f + 0.65f * p);
    }
    Glow(x, y, (0.016f + 0.034f * p) * scale, 1.0f, 1.0f, 1.0f, 0.30f + 0.70f * p);
    // the charge closes in: a ring of 12 hard segments shrinking, six hard ticks pointing at the middle, a diamond core
    {
        const float rad = (0.11f - 0.075f * p) * scale;
        for (int i = 0; i < 12; ++i)
        {
            const float a0 = TWO_PI * static_cast<float>(i) / 12.0f - Time() * 0.03f, a1 = a0 + TWO_PI / 12.0f * 0.6f;
            GameVfx::DrawBar(x + std::cos(a0) * rad, y + std::sin(a0) * rad / ASPECT, x + std::cos(a1) * rad, y + std::sin(a1) * rad / ASPECT,
                             0.0035f, r, g, b, 0.15f + 0.70f * p);
        }
    }
    for (int i = 0; i < 6; ++i)
    {
        const float a = TWO_PI * static_cast<float>(i) / 6.0f + Time() * 0.06f;
        const float d = (0.16f * (1.0f - p) + 0.020f) * scale;
        const float l = 0.030f * scale;
        GameVfx::DrawBar(x + std::cos(a) * (d + l), y + std::sin(a) * (d + l) / ASPECT, x + std::cos(a) * d, y + std::sin(a) * d / ASPECT, 0.0030f,
                         1.0f, 1.0f, 1.0f, 0.15f + 0.75f * p);
    }
    GameVfx::DrawDiamond(x, y, (0.008f + 0.012f * p) * scale, Time() * 0.08f, 1.0f, 1.0f, 1.0f, 0.4f + 0.6f * p);
}

void AimLine(float sx, float sy, float tx, float ty, float p, Polarity pol)
{
    float r, g, b;
    Colour(pol, r, g, b);
    Line(sx, sy, tx, ty, 0.0120f, r, g, b, 0.03f + 0.07f * p);
    Line(sx, sy, tx, ty, 0.0036f, r, g, b, 0.14f + 0.46f * p);
}

float TelegraphProgress()
{
    if (g_AState != AtkState::Telegraph || g_Id == AtkId::None) return 0.0f;
    return 1.0f - static_cast<float>(g_ATimer) / static_cast<float>(TelegraphFrames(g_Id));
}

// ---------------------------------------------------------------- world effects
void DrawShocks()
{
    for (const auto& s : g_Shocks)
    {
        if (!s.active) continue;
        const float life = static_cast<float>(s.timer) / static_cast<float>(s.duration);
        const float progress = 1.0f - life;
        const float eased = EaseOut(progress);
        // a hard ring of 20 segments in the colour (cut: 4 gaps), a thin white ring inside, the flash only at the start
        const float rad = s.size * (0.12f + 0.88f * eased) * 0.5f;
        for (int i = 0; i < 20; ++i)
        {
            if (i % 5 == 4) continue;
            const float a0 = TWO_PI * static_cast<float>(i) / 20.0f + s.size * 7.0f, a1 = a0 + TWO_PI / 20.0f * 0.80f;
            GameVfx::DrawBar(s.x + std::cos(a0) * rad, s.y + std::sin(a0) * rad / ASPECT, s.x + std::cos(a1) * rad, s.y + std::sin(a1) * rad / ASPECT,
                             0.010f * life + 0.002f, s.r, s.g, s.b, life * 0.90f);
        }
        Ring(s.x, s.y, s.size * (0.08f + 0.62f * eased), 1.0f, 1.0f, 1.0f, life * life * 0.40f);
        if (progress < 0.20f)
        {
            const float flash = 1.0f - progress / 0.20f;
            Glow(s.x, s.y, s.size * 0.55f * (0.4f + 0.6f * flash), s.r, s.g, s.b, flash * 0.75f);
            Glow(s.x, s.y, s.size * 0.22f * flash, 1.0f, 1.0f, 1.0f, flash);
        }
    }
}

void DrawDebris()
{
    for (const auto& d : g_Debris)
    {
        if (!d.active) continue;
        const float life = d.life / d.maxLife;
        if (d.kind == 0)
        {
            // a plate of the hull: a dark hard triangle with one lit edge (like the enemies' wreckage)
            const float a = Clamp01(life * 1.6f), r = d.size * 0.55f, rot = d.rotation;
            const float x0 = d.x + std::cos(rot) * r, y0 = d.y + std::sin(rot) * r / ASPECT;
            const float x1 = d.x + std::cos(rot + 2.4f) * r * 0.65f, y1 = d.y + std::sin(rot + 2.4f) * r * 0.65f / ASPECT;
            const float x2 = d.x + std::cos(rot - 2.2f) * r * 0.75f, y2 = d.y + std::sin(rot - 2.2f) * r * 0.75f / ASPECT;
            GameVfx::DrawTri(x0, y0, x1, y1, x2, y2, 0.10f * d.r / 0.30f, 0.11f * d.g / 0.34f, 0.14f * d.b / 0.42f, a, false);
            GameVfx::DrawBar(x0, y0, x1, y1, 0.0030f, 0.95f, 0.95f, 1.0f, a * 0.85f);
        }
        else if (d.kind == 1)
        {
            // a spark: a hard needle along its flight
            const float angle = std::atan2(d.vy * ASPECT, d.vx);
            const float l = d.size * (0.3f + 0.7f * life) * 0.5f;
            const float ex = std::cos(angle) * l, ey = std::sin(angle) * l / ASPECT;
            GameVfx::DrawBar(d.x - ex, d.y - ey, d.x + ex, d.y + ey, 0.0030f * (0.4f + 0.6f * life), d.r, d.g, d.b, life);
        }
        else
        {
            Glow(d.x, d.y, d.size * (1.4f - life * 0.6f), d.r, d.g, d.b, life * 0.45f);
        }
    }
}

void DrawMotes()
{
    for (const auto& m : g_Motes)
    {
        if (!m.active) continue;
        float r, g, b;
        Colour(m.polarity, r, g, b);
        Glow(m.x, m.y, 0.040f, r * 0.5f + 0.5f, g * 0.5f + 0.5f, b * 0.5f + 0.35f, 0.65f);
        Glow(m.x, m.y, 0.014f, 1.0f, 1.0f, 1.0f, 0.95f);
    }
}

void DrawBolts()
{
    for (const auto& b : g_Bolts)
    {
        if (!b.active) continue;
        float vx, vy;
        Velocity(b.heading, b.speed, vx, vy);
        const float bx = GameTimer::Blend(b.prevX, b.x);
        const float by = GameTimer::Blend(b.prevY, b.y);
        if (b.white)
        {
            GameVfx::DrawNeutralBullet(bx, by, std::atan2(vy, vx), g_DrawFrame, b.speed);
            continue;
        }
        GameVfx::DrawEnemyBullet(bx, by, std::atan2(vy, vx), IsRed(b.polarity), b.homing, g_DrawFrame, b.speed, 1.0f, GameVfx::BulletBoss);
        if (b.flips && !b.flipped)
        {
            Ring(bx, by, 0.068f, 1.0f, 1.0f, 1.0f, 0.55f);
        }
    }
}

void DrawFlipLine()
{
    if (g_FlipAlpha <= 0.01f) return;
    const float a = g_FlipAlpha;
    Glow(FLIP_X, 0.0f, 0.06f, 0.80f, 0.70f, 1.0f, 0.10f * a);
    GameVfx::Draw(Sprite::Glow, FLIP_X, 0.0f, 0.07f, 2.4f, 0.0f, 0.75f, 0.70f, 1.0f, 0.30f * a);
    Line(FLIP_X, -1.0f, FLIP_X, 1.0f, 0.0045f, 1.0f, 1.0f, 1.0f, 0.80f * a);
    const float scroll = std::fmod(Time() * 0.012f, 0.16f);
    for (int k = -1; k < 14; ++k)
    {
        const float y0 = 1.0f - scroll - static_cast<float>(k) * 0.16f;
        const bool red = (k % 2 == 0);
        float r, g, b;
        Colour(red ? Polarity::Red : Polarity::Blue, r, g, b);
        Line(FLIP_X - 0.012f, y0, FLIP_X - 0.012f, y0 - 0.08f, 0.0045f, r, g, b, 0.55f * a);
        Colour(red ? Polarity::Blue : Polarity::Red, r, g, b);
        Line(FLIP_X + 0.012f, y0, FLIP_X + 0.012f, y0 - 0.08f, 0.0045f, r, g, b, 0.55f * a);
    }
}

// ---------------------------------------------------------------- telegraphs
// the lane of a beam as the telegraph shows it: a band from the weak point of the boss through the lane, with the marks of the direction running along it
void DrawSlantLane(float lane, float laneX, float half, float r, float g, float b, float fillAlpha, float edgeAlpha, float markAlpha)
{
    const float x0 = BeamOriginX();
    const float xe = -1.05f;
    const float y0 = g_Y;
    const float ye = BeamLineY(lane, laneX, xe);
    Line(xe, ye, x0, y0, 2.0f * half * ASPECT, r, g, b, fillAlpha);
    Line(xe, ye + half, x0, y0 + half, 0.005f, r, g, b, edgeAlpha);
    Line(xe, ye - half, x0, y0 - half, 0.005f, r, g, b, edgeAlpha);
    for (int i = 0; i < 8; ++i)
    {
        const float t = std::fmod(static_cast<float>(i) / 8.0f + Time() * 0.018f, 1.0f);
        const float x = x0 - t * (x0 - xe);
        const float y = BeamLineY(lane, laneX, x);
        const float a = (1.0f - t * 0.6f) * 0.85f * markAlpha;
        Line(x + 0.018f, y + 0.05f, x - 0.018f, y, 0.005f, r, g, b, a);
        Line(x - 0.018f, y, x + 0.018f, y - 0.05f, 0.005f, r, g, b, a);
    }
}

// The overload charges: the weak point is framed, and a second frame closes in on it as the charge fills. A piercing beam that reaches the weak point before the
// frames meet breaks the overload.
void DrawBreakCue(float p)
{
    const float pulse = Pulse(0.5f);
    const float k = 1.8f - 0.8f * p;
    if (p > 0.80f) // the last fifth of the charge: a ring runs out of the weak point, faster and brighter until the beam goes off
    {
        const float q = (p - 0.80f) / 0.20f;
        float cx, cy;
        GameBoss::GetTargetPosition(cx, cy);
        Ring(cx, cy, 0.10f + 0.50f * std::fmod(q * 3.0f, 1.0f), 1.0f, 0.90f, 0.40f, 0.35f + 0.45f * q);
        Glow(cx, cy, 0.30f + 0.20f * q, 1.0f, 0.85f, 0.30f, 0.10f + 0.20f * q);
    }
    auto mark = [&](float x, float y, float halfW, float halfH)
    {
        Brackets(x, y, halfW * k, halfH * k, 0.045f, 1.0f, 0.90f, 0.40f, 0.45f + 0.40f * pulse);
        Brackets(x, y, halfW, halfH, 0.03f, 1.0f, 1.0f, 1.0f, 0.55f + 0.30f * pulse);
    };
    if (g_Phase == 1)
    {
        mark(BodyX(), g_Y, BODY_HALF_W + 0.02f, BODY_HALF_H + 0.02f);
    }
    else if (g_Phase == 2)
    {
        mark(WeakX(), g_Y, BODY_HALF_W + 0.02f, BODY_HALF_H + 0.02f);
    }
    else
    {
        mark(WeakX(), g_Y, CORE_HALF_W + 0.02f, CORE_HALF_H + 0.03f);
    }
}

void DrawTelegraphs()
{
    const bool secondLane = (g_AState == AtkState::Fire && g_Id == AtkId::RailShot && g_AT >= 17 && g_AT < 48);
    const bool ramFire = (g_AState == AtkState::Fire && g_Id == AtkId::Ram && g_AT <= RAM_WIND + RAM_DASH);
    if (g_AState != AtkState::Telegraph && !secondLane && !ramFire)
    {
        return;
    }
    const float p = secondLane ? Clamp01(static_cast<float>(g_AT - 17) / 31.0f) : (ramFire ? 1.0f : TelegraphProgress());
    const float intensity = 0.35f + 0.65f * p;
    if (Interruptible()) DrawBreakCue(p);

    switch (g_Id)
    {
    case AtkId::Ram:
    {
        const float half = RamLaneHalf();
        const bool locked = ramFire || g_ATimer <= RAM_LOCK; // the lane has stopped following the ship: this is where it comes
        const float tipX = g_X - VIS_W * 0.45f;
        const float tipY = RamLaneY(tipX);
        const float endX = -1.05f;
        const float endY = RamLaneY(endX);
        const float cr = 1.0f, cg = locked ? 0.92f : 0.74f, cb = locked ? 0.78f : 0.28f;
        Line(endX, endY, tipX, tipY, 2.0f * half * ASPECT, cr, cg, cb, (locked ? 0.09f : 0.04f) + 0.08f * p);
        Line(endX, endY + half, tipX, tipY + half, locked ? 0.006f : 0.003f, cr, cg, cb, (locked ? 0.55f : 0.30f) + 0.35f * p);
        Line(endX, endY - half, tipX, tipY - half, locked ? 0.006f : 0.003f, cr, cg, cb, (locked ? 0.55f : 0.30f) + 0.35f * p);
        for (int i = 0; i < 6; ++i)
        {
            const float t = std::fmod(static_cast<float>(i) / 6.0f + Time() * 0.022f, 1.0f);
            const float x = tipX - t * (tipX - endX);
            const float y = RamLaneY(x);
            const float a = (1.0f - t * 0.6f) * 0.80f * intensity;
            Line(x + 0.020f, y + half * 0.45f, x - 0.020f, y, 0.005f, cr, cg, cb, a);
            Line(x - 0.020f, y, x + 0.020f, y - half * 0.45f, 0.005f, cr, cg, cb, a);
        }
        ChargeGlow(WeakX(), g_Y, p, 1.1f, false, g_APol);
        break;
    }
    case AtkId::Sweep:
    {
        // the nose of the hull will glide along this line, in the direction of the arrows
        float nx, ny;
        BodyTip(nx, ny);
        const float lineX = SWEEP_X + (nx - g_X);
        Line(lineX, -0.78f, lineX, 0.78f, 0.004f, 1.0f, 0.92f, 0.78f, 0.14f + 0.40f * p);
        for (int i = 0; i < 5; ++i)
        {
            const float t = std::fmod(static_cast<float>(i) / 5.0f + Time() * 0.020f, 1.0f);
            const float y = g_AFa * (0.72f - 1.44f * t);
            const float d = -g_AFa * 0.035f;
            Line(lineX - 0.022f, y - d, lineX, y, 0.005f, 1.0f, 0.92f, 0.78f, 0.7f * intensity);
            Line(lineX + 0.022f, y - d, lineX, y, 0.005f, 1.0f, 0.92f, 0.78f, 0.7f * intensity);
        }
        ChargeGlow(nx, ny, p, 1.0f, false, g_APol);
        break;
    }
    case AtkId::TwinBurst:
        for (int s = 0; s < 2; ++s)
        {
            float mx, my;
            ReactorPoint(s, mx, my);
            ChargeGlow(mx, my, p, 0.7f, false, s == 0 ? Polarity::Red : Polarity::Blue);
            AimLine(mx, my, g_PX, g_PY, p, s == 0 ? Polarity::Red : Polarity::Blue);
        }
        break;

    case AtkId::PolarityFan:
    {
        float mx, my;
        BodyTip(mx, my);
        ChargeGlow(mx, my, p, 1.0f, false, g_APol);

        for (int side = -1; side <= 1; side += 2)
        {
            const float a = PI + 0.08f + static_cast<float>(side) * 0.725f;
            Line(mx, my, mx + std::cos(a) * 0.9f, my + std::sin(a) * 0.9f / ASPECT, 0.0035f, 1.0f, 1.0f, 1.0f, 0.10f + 0.25f * p);
        }
        break;
    }
    case AtkId::ColorHelix:
    {
        for (int arm = 0; arm < 2; ++arm) // the two guns charge in the colours they open with
        {
            float cx, cy;
            MuzzlePoint(arm, cx, cy);
            ChargeGlow(cx - 0.02f, cy, p, 0.55f, false, HelixPol(arm, 0));
        }
        break;
    }
    case AtkId::SplitBloom:
    {
        float cx, cy;
        BodyTip(cx, cy);
        ChargeGlow(cx, cy, p, 0.90f, true, g_APol); // (both colours: the ring is cut into them)
        break;
    }
    case AtkId::DoubleSpiral:
    {
        for (int i = 0; i < 2; ++i) // the two sockets charge
        {
            float cx, cy;
            TurretPoint(i, cx, cy);
            ChargeGlow(cx - 0.05f, cy, p, 0.55f, false, i == 0 ? Polarity::Red : Polarity::Blue);
        }
        break;
    }
    case AtkId::PrismRing:
        ChargeGlow(WeakX(), g_Y, p, 1.1f, true, Polarity::Red);
        break;
    case AtkId::RailShot:
    {
        const float lane = secondLane ? g_AFb : g_AFa;
        const float laneX = secondLane ? g_RailXl2 : g_RailXl;
        DrawSlantLane(lane, laneX, RAIL_HALF_H, 1.0f, 1.0f, 1.0f, 0.04f + 0.09f * p, 0.35f + 0.5f * p, intensity);
        ChargeGlow(BeamOriginX(), g_Y, secondLane ? 0.4f : p, 1.0f, false, Polarity::Red); // (at the emitter of the rail gun)
        BeamFx::DrawChargeUp(BeamOriginX(), g_Y, 0.22f, secondLane ? 0.4f : p, Time(), BeamFx::BossPalette(), 7);
        break;
    }
    case AtkId::OverloadBeam:
    {
        const int count = TwinBeams() ? 2 : 1;
        for (int k = 0; k < count; ++k)
            DrawSlantLane(g_Beam[k].y, g_Beam[k].xl, BEAM_HALF_H, 0.86f, 0.90f, 1.0f, 0.02f + 0.07f * p, 0.20f + 0.55f * p, 0.7f);
        ChargeGlow(BeamOriginX(), g_Y, p, 1.3f, false, Polarity::Red);
        BeamFx::DrawChargeUp(BeamOriginX(), g_Y, 0.28f, p, Time(), BeamFx::BossPalette(), 9);
        Hexagon(WeakX(), g_Y, 0.14f, Time() * 0.03f, 0.004f, 0.92f, 0.94f, 1.0f, 0.55f * p);
        break;
    }
    case AtkId::CombSweep:
    {
        float mx, my;
        BodyTip(mx, my);
        ChargeGlow(mx, my, p, 1.0f, false, g_APol);
        for (int k = -2; k <= 2; ++k) // the five teeth of the comb, as they stand at the start
        {
            const float a = PI + static_cast<float>(k) * 0.24f;
            Line(mx, my, mx + std::cos(a) * 0.9f, my + std::sin(a) * 0.9f / ASPECT, 0.0035f, 1.0f, 1.0f, 1.0f, 0.08f + 0.22f * p);
        }
        break;
    }
    case AtkId::Collapse:
        // the core breathes in: a white ring closes on it, the bolts on the screen are about to be pulled in
        ChargeGlow(WeakX(), g_Y, p, 1.6f, true, Polarity::Red);
        Ring(WeakX(), g_Y, 0.70f - 0.55f * p, 1.0f, 1.0f, 1.0f, 0.25f + 0.55f * p);
        Ring(WeakX(), g_Y, 0.95f - 0.70f * p, 1.0f, 1.0f, 1.0f, 0.15f + 0.30f * p);
        break;
    case AtkId::NovaRings:
        ChargeGlow(WeakX(), g_Y, p, 1.5f, true, Polarity::Red);
        Hexagon(WeakX(), g_Y, 0.20f - 0.10f * p, Time() * 0.05f, 0.004f, 1.0f, 1.0f, 1.0f, 0.35f + 0.50f * p);
        break;
    case AtkId::PolarityStorm:
    {
        const float scroll = std::fmod(Time() * 0.02f, 0.2f);
        for (int i = 0; i < 9; ++i)
        {
            const float x = 0.95f - scroll - static_cast<float>(i) * 0.2f;
            for (int side = -1; side <= 1; side += 2)
            {
                const float y = static_cast<float>(side) * 0.93f;
                Line(x + 0.02f, y, x, y - static_cast<float>(side) * 0.04f, 0.004f, 1.0f, 1.0f, 1.0f, 0.40f * intensity);
                Line(x - 0.02f, y, x, y - static_cast<float>(side) * 0.04f, 0.004f, 1.0f, 1.0f, 1.0f, 0.40f * intensity);
            }
        }
        ChargeGlow(WeakX(), g_Y, p, 1.0f, true, Polarity::Red);
        break;
    }
    default:
        break;
    }
}

void DrawBeams()
{
    for (int k = 0; k < 2; ++k)
    {
        if (!g_Beam[k].active) continue;
        const Beam& bm = g_Beam[k];
        const float fadeIn = Clamp01(static_cast<float>(g_BeamT) / 4.0f);
        const float fadeOut = Clamp01(static_cast<float>(g_BeamLen - g_BeamT) / 8.0f);
        const float alpha = fadeIn * fadeOut;
        const float pulse = 1.0f + 0.07f * std::sin(Time() * 0.9f);
        const float tipX = BeamOriginX();
        const float tipY = g_Y;
        const float farX = -1.25f;
        const float farY = BeamLineY(bm.y, bm.xl, farX);

        // The beam fires both colours, so it is drawn as a plasma braided of both (BeamFx): red and blue strands wind round a white core, the rails are the edge of the danger.
        BeamFx::BeamParams beam;
        beam.x0 = tipX;
        beam.y0 = tipY;
        beam.x1 = farX;
        beam.y1 = farY;
        beam.halfWidth = g_BeamHalf * 1.25f * pulse;
        beam.time = Time();
        beam.envelope = alpha;
        beam.turbulence = 0.9f;
        beam.pulseSpeed = 1.2f;
        beam.seed = 11 + k * 7;
        const BeamFx::Palette palette = BeamFx::BossPalette();
        BeamFx::DrawBeam(beam, palette);
        BeamFx::DrawMuzzle(tipX, tipY, 0.12f * pulse, Time(), alpha, palette);
        Glow(-1.0f, BeamLineY(bm.y, bm.xl, -1.0f), 0.14f, 0.86f, 0.90f, 1.0f, 0.55f * alpha);
    }
}

// ---------------------------------------------------------------- hull (phase 1 / 2)

void DrawEngines(float alpha)
{
    for (int i = 0; i < 2; ++i)
    {
        float thrust = 1.0f;
        if (g_Phase == 2)
            thrust = 0.55f; // the empty hull only idles
        else if (g_Phase == 3)
            thrust = 1.30f;
        const float vx = g_X - g_PrevX;
        thrust += -vx * 30.0f + std::fabs(g_Y - g_PrevY) * 30.0f;
        if (g_AState == AtkState::Vent) thrust = 0.25f;
        thrust = Clamp(thrust, 0.25f, 2.4f);
        float x, y;
        HullPoint(NOZZLE_U, NOZZLE_V[i], x, y);
        const float lengthScale = Clamp(0.40f + 0.60f * thrust, 0.30f, 2.6f);
        const float widthScale = Clamp(0.75f + 0.25f * thrust, 0.70f, 1.4f);
        const float flicker = 0.90f + 0.07f * std::sin(Time() * 0.55f + static_cast<float>(i) * 2.1f);
        const float length = 0.21f * lengthScale * flicker * (VIS_H / 0.65f) * 0.8f;
        const float height = 0.092f * widthScale * 0.8f;
        float ar, ag, ab;
        PhaseAccent(g_Phase, ar, ag, ab);
        if (g_Phase <= 2) Colour(g_Ship[i].polarity, ar, ag, ab); // the nozzle is red above and blue below
        GameVfx::Draw(Sprite::Flame, x + length * 0.5f, y, length, height, PI, ar, ag, ab, 0.85f * alpha);
        GameVfx::Draw(Sprite::Flame, x + length * 0.30f, y, length * 0.55f, height * 0.40f, PI, 1.0f, 1.0f, 1.0f, 0.90f * alpha);
        Glow(x, y, height * 2.1f, ar, ag, ab, 0.45f * alpha);
    }
}

void DroneColour(int i, float& r, float& g, float& b); // (below)

// the two engines of a drone
void DrawPodEngines()
{
    if (!PodsVisible()) return;
    for (int i = 0; i < 2; ++i)
    {
        const Ship& s = g_Ship[i];
        if (!s.alive) continue;
        const float thrust = g_Frenzy > 0 ? 1.45f : 1.10f;
        float r, g, b;
        DroneColour(i, r, g, b);
        for (int k = -1; k <= 1; k += 2)
        {
            const float x = s.x + POD_NOZZLE_DX;
            const float y = s.y + static_cast<float>(k) * POD_NOZZLE_DY;
            const float flicker = 0.90f + 0.07f * std::sin(Time() * 0.55f + static_cast<float>(i * 2 + k) * 2.1f);
            const float length = 0.17f * (0.40f + 0.60f * thrust) * flicker;
            const float height = 0.050f;
            GameVfx::Draw(Sprite::Flame, x + length * 0.5f, y, length, height, PI, r, g, b, 0.85f);
            GameVfx::Draw(Sprite::Flame, x + length * 0.30f, y, length * 0.55f, height * 0.40f, PI, 1.0f, 1.0f, 1.0f, 0.90f);
            Glow(x, y, height * 2.1f, r, g, b, 0.45f);
        }
    }
}

void DrawCracks(float alpha)
{
    if (g_Phase != 1) return;
    for (int c = 0; c < CRACK_COUNT; ++c)
    {
        const float rate = g_P1Hp / P1_HP;
        const float reveal = Clamp01((1.0f - rate) * 6.5f - static_cast<float>(c) * 0.75f);
        if (reveal <= 0.0f) continue;
        const float flicker = 0.78f + 0.22f * std::sin(Time() * 0.12f + static_cast<float>(c) * 1.3f);
        const float a = reveal * flicker * alpha;
        float px[4], py[4];
        for (int k = 0; k < 4; ++k)
        {
            HullPoint(CRACKS[c][k][0], CRACKS[c][k][1], px[k], py[k]);
        }
        for (int seg = 0; seg < 3; ++seg)
        {
            Line(px[seg], py[seg], px[seg + 1], py[seg + 1], 0.0040f, 1.0f, 0.78f, 0.45f, a);
            Line(px[seg], py[seg], px[seg + 1], py[seg + 1], 0.0014f, 1.0f, 1.0f, 1.0f, a * 0.90f);
        }
        for (int k = 0; k < 4; ++k)
            Glow(px[k], py[k], 0.020f, 1.0f, 0.78f, 0.45f, a * 0.5f);
    }
}

// the colour of a drone: drone 0 shows the colour of its next fan, drone 1 is white
void DroneColour(int i, float& r, float& g, float& b)
{
    if (i == 0)
    {
        Colour(g_Ship[0].polarity, r, g, b);
        return;
    }
    r = 0.92f;
    g = 0.95f;
    b = 1.0f;
}

void DrawShipExtras(int i, float alpha)
{
    const Ship& s = g_Ship[i];
    if (!s.alive)
    {
        // the wreck smokes; where the new drone will come a ring fills up
        const float flick = 0.5f + 0.5f * std::sin(Time() * 0.4f + static_cast<float>(i) * 2.0f);
        Glow(s.x, s.y, 0.06f + 0.02f * flick, 1.0f, 0.45f, 0.12f, 0.30f * alpha);
        if (g_State == State::Battle)
        {
            const float total = static_cast<float>(DRONE_RESPAWN[g_Phase >= 3 ? 1 : 0]);
            const float k = Clamp01(1.0f - static_cast<float>(s.respawn) / total);
            float hx, hy, r, g, b;
            DroneHome(i, hx, hy);
            DroneColour(i, r, g, b);
            Ring(hx, hy, 0.10f, r, g, b, 0.10f + 0.40f * k * k);
        }
        return;
    }
    float r, g, b;
    DroneColour(i, r, g, b);
    float rx, ry;
    MuzzlePoint(i, rx, ry);
    const bool venting = (g_Vent > 0);
    float lr = r, lg = g, lb = b;
    if (venting)
    {
        lr = 1.0f;
        lg = 0.82f;
        lb = 0.30f;
    }
    // the muzzle glows up in the last half second before the next volley: the drones tell when they fire
    const float due = Clamp01(1.0f - static_cast<float>(s.fireTimer) / 30.0f);
    const float level = 0.35f + 0.50f * due + (s.fireFlash > 0 ? 0.5f : 0.0f) + (venting ? 0.4f * Pulse(0.4f) : 0.0f);
    Glow(rx, ry, 0.075f, lr, lg, lb, Clamp01(level) * 0.8f * alpha);
    Glow(rx, ry, 0.026f, 1.0f, 1.0f, 1.0f, Clamp01(level) * alpha);
    if (s.fireFlash > 0)
    {
        const float f = static_cast<float>(s.fireFlash) / 8.0f;
        Glow(rx - 0.02f, ry, 0.12f * f + 0.03f, lr, lg, lb, 0.9f * f);
        GameVfx::Draw(Sprite::Spark, rx - 0.05f * f, ry, 0.04f + 0.10f * f, 0.025f, 0.0f, 1.0f, 0.95f, 0.85f, f);
    }
    if (g_Frenzy > 0) Glow(s.x, s.y, 0.40f, 1.0f, 0.45f, 0.18f, 0.10f + 0.06f * Pulse(0.4f));
}

// The shell of the core (phase 3): a hexagon that turns around it while it is closed. It flashes when a shot stops on it, and two lines run from it to the drones
// that hold it. When it opens, the core is framed in gold.
void DrawCoreShell()
{
    if (g_Phase != 3 || g_State != State::Battle || !BerserkArt()) return;
    const float x = WeakX();
    const float y = g_Y;
    if (CoreExposed())
    {
        if (!g_Dying && g_Vent <= 0)
        {
            const float p = Pulse(0.25f);
            Brackets(x, y, CORE_HALF_W + 0.03f, CORE_HALF_H + 0.04f, 0.045f, 1.0f, 0.85f, 0.35f, 0.45f + 0.35f * p);
        }
        return;
    }
    const float rot = Time() * 0.015f;
    const float flash = g_ShellFlash > 0 ? static_cast<float>(g_ShellFlash) / 6.0f : 0.0f;
    Glow(x, y, 0.36f, 0.70f, 0.86f, 1.0f, 0.10f + 0.15f * flash);
    Hexagon(x, y, 0.15f, rot, 0.0050f, 0.80f, 0.92f, 1.0f, 0.65f + 0.30f * flash);
    Hexagon(x, y, 0.13f, -rot * 1.4f, 0.0025f, 1.0f, 1.0f, 1.0f, 0.25f + 0.40f * flash);
    for (int i = 0; i < 2; ++i)
    {
        if (!g_Ship[i].alive) continue;
        float r, g, b;
        DroneColour(i, r, g, b);
        Line(g_Ship[i].x, g_Ship[i].y, x, y, 0.0025f, r, g, b, 0.25f + 0.10f * Pulse(0.3f, static_cast<float>(i)));
    }
}

// the warning of a volley of white needles: one dotted line for each needle (the lasers are dashed bars: a different thing). It follows the ship and is locked for
// the last 12 frames (it blinks then), the volley leaves along it
void DrawWhiteWarning()
{
    if (g_WhiteWarn <= 0 || g_Phase < 2 || !g_Ship[1].alive) return;
    const int k = g_Phase >= 3 ? 1 : 0;
    const Ship& s = g_Ship[1];
    const bool locked = s.fireTimer <= 12 || g_WhiteHold > 0;
    const float p = Clamp01(static_cast<float>(g_WhiteWarn) / static_cast<float>(WHITE_WARN[k]));
    const float a = locked ? ((g_WhiteWarn / 3) % 2 == 0 ? 0.80f : 0.35f) : 0.14f + 0.30f * p;
    const float x0 = s.x - 0.07f, y0 = s.y;
    constexpr float STEP = 0.050f, LEN = 1.7f;
    const float run = std::fmod(static_cast<float>(g_WhiteWarn) * 0.003f, STEP);
    for (int n = 0; n < WHITE_COUNT[k]; ++n)
    {
        const float spread = (static_cast<float>(n) - static_cast<float>(WHITE_COUNT[k] - 1) * 0.5f) * 0.14f;
        const float heading = g_WhiteAim + spread;
        const float c = std::cos(heading), sn = std::sin(heading);
        for (float d = 0.06f + run; d < LEN; d += STEP)
        {
            const float fade = 1.0f - d / LEN * 0.6f;
            GameVfx::DrawDiamond(x0 + c * d, y0 + sn * d / ASPECT, 0.0055f, heading, 1.0f, 1.0f, 1.0f, a * fade);
        }
    }
    Glow(x0, y0, 0.05f + 0.05f * p, 1.0f, 1.0f, 1.0f, a * 0.8f); // (the muzzle: the lock-on)
}

// the laser of drone 1: a thin white line while it aims (it blinks in its last 12 frames: locked), a bright band while it fires
void DrawDroneLaser()
{
    if (g_Laser.state == 0) return;
    const float len = 2.6f;
    const float c = std::cos(g_Laser.heading), s = std::sin(g_Laser.heading);
    const float x1 = g_Laser.x0 + c * len;
    const float y1 = g_Laser.y0 + s * len / ASPECT;
    if (g_Laser.state == 1)
    {
        const int warn = LASER_WARN[g_Phase >= 3 ? 1 : 0];
        const bool locked = g_Laser.t >= warn - 12;
        const float progress = Clamp01(static_cast<float>(g_Laser.t) / static_cast<float>(warn));
        const float a = locked ? ((g_Laser.t / 3) % 2 == 0 ? 0.85f : 0.35f) : 0.20f + 0.30f * progress;
        // a dashed hard line (dashes run out from the drone); locked: solid and blinking
        if (locked)
        {
            GameVfx::DrawBar(g_Laser.x0, g_Laser.y0, x1, y1, 0.0040f, 1.0f, 1.0f, 1.0f, a);
        }
        else
        {
            constexpr float DASH = 0.06f;
            const float run = std::fmod(static_cast<float>(g_Laser.t) * 0.004f, DASH);
            for (float d = run; d < len; d += DASH)
                GameVfx::DrawBar(g_Laser.x0 + c * d, g_Laser.y0 + s * d / ASPECT, g_Laser.x0 + c * (d + DASH * 0.55f), g_Laser.y0 + s * (d + DASH * 0.55f) / ASPECT,
                                 0.0025f, 1.0f, 1.0f, 1.0f, a);
        }
        BeamFx::DrawChargeUp(g_Laser.x0, g_Laser.y0, 0.16f, progress, Time(), BeamFx::WhitePalette(), 3); // (the light is drawn into the drone)
        return;
    }
    const float fade = Clamp01(static_cast<float>(LASER_FIRE - g_Laser.t) / 6.0f);
    BeamFx::BeamParams beam;
    beam.x0 = g_Laser.x0;
    beam.y0 = g_Laser.y0;
    beam.x1 = x1;
    beam.y1 = y1;
    beam.halfWidth = LASER_HALF * 1.3f;
    beam.time = static_cast<float>(g_Laser.t);
    beam.envelope = fade;
    beam.turbulence = 0.55f;
    beam.arcs = 3;
    beam.pulseSpeed = 1.5f;
    beam.seed = 5;
    const BeamFx::Palette palette = BeamFx::WhitePalette();
    BeamFx::DrawBeam(beam, palette);
    BeamFx::DrawMuzzle(g_Laser.x0, g_Laser.y0, 0.07f, static_cast<float>(g_Laser.t), fade, palette);
}

// additive flash of the whole hull: a hit on the weak point, the vent window, a hit on the reactor
void DrawBodyOverlays(int tex)
{
    if (tex < 0) return;
    const Renderer2DBlendMode prev = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    if (g_Phase == 1)
    {
        const bool venting = (g_Vent > 0);
        if (g_BodyFlash > 0)
            Renderer2D_DrawTextureEx(tex, g_X, g_Y, VIS_W, VIS_H, 0.0f, 1.0f, 0.93f, 0.82f,
                                     Clamp01(static_cast<float>(g_BodyFlash) / 4.0f) * 0.55f, 0.0f, 0.0f, 1.0f, 1.0f);
        if (venting)
            Renderer2D_DrawTextureEx(tex, g_X, g_Y, VIS_W, VIS_H, 0.0f, 1.0f, 0.62f, 0.16f, 0.16f + 0.12f * Pulse(0.35f), 0.0f,
                                     0.0f, 1.0f, 1.0f);
    }
    else if (g_Phase == 2 && g_BodyFlash > 0)
    {
        Renderer2D_DrawTextureEx(tex, g_X, g_Y, VIS_W, VIS_H, 0.0f, 1.0f, 0.93f, 0.82f,
                                 Clamp01(static_cast<float>(g_BodyFlash) / 4.0f) * 0.45f, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    else if (g_Phase == 3 && g_CoreFlash > 0)
    {
        Renderer2D_DrawTextureEx(tex, g_X, g_Y, VIS_W, VIS_H, 0.0f, 1.0f, 0.93f, 0.82f,
                                 Clamp01(static_cast<float>(g_CoreFlash) / 4.0f) * 0.40f, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    Renderer2D_SetBlendMode(prev);
}

// the drones of phase 2 and 3 (drone 1 is pale: it is the white one; a drone that is down is a dark wreck that drifts away)
void DrawPods()
{
    if (!PodsVisible()) return;
    for (int i = 0; i < 2; ++i)
    {
        const Ship& s = g_Ship[i];
        float shade = 1.0f;
        float alpha = 1.0f;
        if (!s.alive)
        {
            shade = 0.40f;
            const float total = static_cast<float>(DRONE_RESPAWN[g_Phase >= 3 ? 1 : 0]);
            alpha = 0.95f * Clamp01((static_cast<float>(s.respawn) - total * 0.7f) / (total * 0.3f)); // (the wreck is gone well before the new drone comes)
        }
        if (alpha <= 0.01f) continue;
        const int tex = TexPod(i);
        const float bob = !s.alive ? 0.0f : std::sin(Time() * 0.05f + static_cast<float>(i) * 2.0f) * 0.004f;
        const float tr = i == 1 ? 0.70f : 1.0f, tg = i == 1 ? 0.78f : 1.0f, tb = 1.0f; // (drone 1: the blue art, washed out)
        const float cx = s.x + POD_CX;
        const float cy = s.y + bob;
        Renderer2D_DrawTextureEx(tex, cx + 0.012f, cy - 0.022f, POD_W, POD_H, 0.0f, 0.0f, 0.0f, 0.0f, 0.55f * alpha, 0.0f, 0.0f,
                                 1.0f, 1.0f); // hard shadow
        Renderer2D_DrawTextureEx(tex, cx, cy, POD_W, POD_H, 0.0f, shade * tr, shade * tg, shade * tb, alpha, 0.0f, 0.0f, 1.0f, 1.0f);
        if (i == 1 && s.alive) // a white sheen over the pale drone
        {
            const Renderer2DBlendMode prev = Renderer2D_GetBlendMode();
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
            Renderer2D_DrawTextureEx(tex, cx, cy, POD_W, POD_H, 0.0f, 1.0f, 1.0f, 1.0f, 0.22f, 0.0f, 0.0f, 1.0f, 1.0f);
            Renderer2D_SetBlendMode(prev);
        }
        if (s.alive && s.flash > 0)
        {
            const Renderer2DBlendMode prev = Renderer2D_GetBlendMode();
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
            Renderer2D_DrawTextureEx(tex, cx, cy, POD_W, POD_H, 0.0f, 1.0f, 0.93f, 0.82f, Clamp01(static_cast<float>(s.flash) / 4.0f) * 0.55f, 0.0f, 0.0f, 1.0f, 1.0f);
            Renderer2D_SetBlendMode(prev);
        }
    }
}

// glows that belong to the hull: muzzles of the mounted turrets, the vent, the reactor, the sockets
void DrawHullExtras()
{
    if (TurretsMounted())
    {
        for (int i = 0; i < 2; ++i)
            DrawShipExtras(i, 1.0f);
    }
    if (g_Phase == 1)
    {
        if (g_Vent > 0)
        {
            const float p = Pulse(0.3f);
            Glow(BodyX(), g_Y, 0.30f, 1.0f, 0.80f, 0.30f, 0.14f);
            Brackets(BodyX(), g_Y, BODY_HALF_W + 0.03f, BODY_HALF_H + 0.03f, 0.05f, 1.0f, 0.85f, 0.30f, 0.55f + 0.35f * p);
        }
        return;
    }
    if (g_Phase >= 2 && !TurretsMounted())
    {
        // the relays in the gun ports: lit in the colour of their drone while it lives, dark when it is down (phase 3: both dark = the core opens)
        for (int i = 0; i < 2; ++i)
        {
            float sx, sy, r, g, b;
            TurretPoint(i, sx, sy);
            if (i == 0) Colour(g_Ship[0].polarity, r, g, b);
            else r = 0.85f, g = 0.92f, b = 1.0f; // (drone 1 fires the white bullets)
            const float on = g_Ship[i].alive ? 1.0f : 0.0f;
            Glow(sx, sy, 0.07f, r, g, b, (0.10f + 0.35f * on) * (0.80f + 0.20f * Pulse(0.15f, static_cast<float>(i) * 2.0f)));
            if (on > 0.0f) Glow(sx, sy, 0.025f, 1.0f, 1.0f, 1.0f, 0.55f);
            // the chain of light from the relay to its drone (the pans hang on the beam of the balance): pulses run out to the drone
            if (on > 0.0f && PodsVisible())
            {
                const float dx = g_Ship[i].x - sx, dy = g_Ship[i].y - sy;
                constexpr int LINKS = 9;
                const float run = Time() * 0.05f;
                for (int k = 0; k < LINKS; ++k)
                {
                    const float f0 = (static_cast<float>(k) + 0.15f) / LINKS, f1 = (static_cast<float>(k) + 0.65f) / LINKS;
                    const float pulse = 0.5f + 0.5f * std::cos((f0 - run) * 6.2831853f * 1.5f);
                    Line(sx + dx * f0, sy + dy * f0, sx + dx * f1, sy + dy * f1, 0.0030f, r, g, b, 0.16f + 0.30f * pulse);
                }
            }
        }
    }
    if (g_Phase != 3 || !BerserkArt()) return;
    const float x = WeakX();
    const float y = g_Y;
    const float cs = Clamp01(g_CoreScale);
    const float heat = Pulse(0.12f);
    Glow(x, y, 0.34f, 1.0f, 0.78f, 0.35f, (0.26f + 0.12f * heat) * cs);
    Glow(x, y, 0.11f, 1.0f, 1.0f, 1.0f, 0.50f * cs);
    if (g_CoreFlash > 0) Glow(x, y, 0.28f, 1.0f, 0.93f, 0.82f, Clamp01(static_cast<float>(g_CoreFlash) / 4.0f) * 0.7f);
    if (g_State != State::Battle) return;
    // the clock of the core (phase 3 asks about TIME): 12 ticks round the reactor. While both drones are down the shell is open and the ticks
    // go out one by one as the open window runs down; while it is shut they stay dim.
    {
        const bool open = ShipsAlive() == 0;
        const float left = open ? Clamp01(static_cast<float>(std::max(g_Ship[0].respawn, g_Ship[1].respawn)) / static_cast<float>(CORE_WINDOW)) : 0.0f;
        constexpr int TICKS = 12;
        const float rad = CORE_HALF_H * 1.05f * ASPECT; // (x units: OnCircle-like, the circle stays round on the screen)
        for (int k = 0; k < TICKS; ++k)
        {
            const float a = 1.5707963f - 6.2831853f * static_cast<float>(k) / TICKS; // clockwise from the top
            const bool lit = open && static_cast<float>(k) < left * TICKS;
            const float r0 = rad * 1.00f, r1 = rad * 1.18f;
            Line(x + std::cos(a) * r0, y + std::sin(a) * r0 / ASPECT, x + std::cos(a) * r1, y + std::sin(a) * r1 / ASPECT, 0.0045f,
                 1.0f, lit ? 0.90f : 0.55f, lit ? 0.70f : 0.30f, lit ? 0.95f : 0.22f * cs);
        }
    }
    if (g_Vent > 0)
    {
        const float p = Pulse(0.3f);
        Ring(x, y, 0.30f, 1.0f, 0.82f, 0.30f, 0.45f + 0.35f * p);
        Brackets(x, y, CORE_HALF_W + 0.03f, CORE_HALF_H + 0.04f, 0.05f, 1.0f, 0.85f, 0.30f, 0.55f + 0.35f * p);
        Glow(x, y, 0.50f, 1.0f, 0.80f, 0.30f, 0.12f);
    }
}

// the berserk hull comes apart (phase 3 defeat): it shakes, flickers and is gone with the final blast
void DrawDefeatedHull()
{
    const int t = DEFEAT_FRAMES - g_DefeatTimer;
    if (t >= 128) return;
    const float shakeX = std::sin(Time() * 2.9f) * 0.006f;
    const float shakeY = std::cos(Time() * 3.4f) * 0.006f;
    const int tex = TexBerserk();
    const float savedX = g_X, savedY = g_Y;
    g_X += shakeX;
    g_Y += shakeY;
    if (tex >= 0)
    {
        Renderer2D_DrawTextureEx(tex, g_X, g_Y, VIS_W, VIS_H, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        const Renderer2DBlendMode prev = Renderer2D_GetBlendMode();
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
        Renderer2D_DrawTextureEx(tex, g_X, g_Y, VIS_W, VIS_H, 0.0f, 1.0f, 0.75f, 0.45f, 0.18f + 0.18f * Pulse(0.9f), 0.0f, 0.0f,
                                 1.0f, 1.0f);
        Renderer2D_SetBlendMode(prev);
    }
    Glow(WeakX(), g_Y, 0.40f + 0.10f * Pulse(0.7f), 1.0f, 0.80f, 0.40f, 0.35f);
    Glow(WeakX(), g_Y, 0.14f, 1.0f, 1.0f, 1.0f, 0.70f);
    g_X = savedX;
    g_Y = savedY;
}

// red glow at the edges of the screen while the boss is berserk
void DrawBerserkVignette()
{
    if (!g_Berserk || g_State == State::Defeated) return;
    const Renderer2DBlendMode prev = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    const float pulse = 0.70f + 0.30f * std::sin(Time() * 0.08f);
    for (int i = 0; i < 8; ++i)
    {
        const float w = 0.020f * static_cast<float>(i + 1);
        const float a = (0.060f - 0.0065f * static_cast<float>(i)) * pulse;
        Renderer2D_DrawQuad(-1.0f, 1.0f, -1.0f + w, -1.0f, 1.0f, 0.12f, 0.08f, a);
        Renderer2D_DrawQuad(1.0f - w, 1.0f, 1.0f, -1.0f, 1.0f, 0.12f, 0.08f, a);
        Renderer2D_DrawQuad(-1.0f, 1.0f, 1.0f, 1.0f - w * 1.6f, 1.0f, 0.12f, 0.08f, a * 0.8f);
        Renderer2D_DrawQuad(-1.0f, -1.0f + w * 1.6f, 1.0f, -1.0f, 1.0f, 0.12f, 0.08f, a * 0.8f);
    }
    Renderer2D_SetBlendMode(prev);
}

void DrawHullPhases()
{
    if (g_State == State::Defeated)
    {
        DrawDefeatedHull();
        return;
    }
    float shakeX = 0.0f, shakeY = 0.0f;
    if (g_State == State::Transition && g_TransTo == 2 && g_TransT < UNDOCK_AT) // the clamps let go
    {
        const float k = Clamp01(static_cast<float>(g_TransT) / 12.0f);
        shakeX = std::sin(Time() * 3.1f) * 0.004f * k;
        shakeY = std::cos(Time() * 2.7f) * 0.004f * k;
    }
    else if (g_State == State::Transition && g_TransTo == 3 && g_TransT >= 62 && g_TransT < 92) // the reactor ignites
    {
        shakeX = std::sin(Time() * 3.3f) * 0.005f;
        shakeY = std::cos(Time() * 2.9f) * 0.005f;
    }
    const float savedX = g_X, savedY = g_Y;
    g_X += shakeX;
    g_Y += shakeY;

    float ar, ag, ab;
    PhaseAccent(g_Phase, ar, ag, ab);
    // back light: the boss is a dark silhouette in front of a pale glow (neutral, not coloured)
    Glow(g_X + 0.03f, g_Y, VIS_H * 1.30f, 0.80f, 0.85f, 1.00f, 0.075f + 0.020f * Pulse(0.05f));
    Glow(g_X, g_Y, VIS_H * 1.10f, ar, ag, ab, 0.06f + 0.03f * Pulse(0.05f));

    const Renderer2DBlendMode prev = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    DrawEngines(1.0f);
    const int tex = BerserkArt() ? TexBerserk() : (TurretsMounted() ? TexFull() : TexBare());
    if (tex >= 0)
    {
        // hard drop shadow: the same hull in black, shifted down and to the right
        Renderer2D_DrawTextureEx(tex, g_X + 0.016f, g_Y - 0.030f, VIS_W, VIS_H, 0.0f, 0.0f, 0.0f, 0.0f, 0.55f, 0.0f, 0.0f, 1.0f,
                                 1.0f);
        DrawRail(); // (under the hull: it slides out of the pod)
        Renderer2D_DrawTextureEx(tex, g_X, g_Y, VIS_W, VIS_H, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    DrawBodyOverlays(tex);
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    DrawCracks(1.0f);
    DrawHullExtras();
    DrawCoreShell();
    // the drones are in front of the hull
    DrawPods();
    DrawPodEngines();
    for (int i = 0; i < 2; ++i)
    {
        if (PodsVisible()) DrawShipExtras(i, 1.0f);
    }
    Renderer2D_SetBlendMode(prev);
    g_X = savedX;
    g_Y = savedY;
}

// ---------------------------------------------------------------- phase 3 core

void DrawDefeatFlash()
{
    if (g_State != State::Defeated) return;
    const int t = DEFEAT_FRAMES - g_DefeatTimer;
    if (t < 120) return;
    const float f = Clamp01(static_cast<float>(t - 120) / 10.0f);
    const float fade = Clamp01(static_cast<float>(g_DefeatTimer) / 14.0f);
    Glow(WeakX(), g_Y, 0.4f + 1.6f * f, 1.0f, 0.82f, 0.55f, 0.85f * fade);
    Glow(WeakX(), g_Y, 0.2f + 0.8f * f, 1.0f, 1.0f, 1.0f, fade);
}

void DrawScreenFlash()
{
    if (g_FlashAlpha <= 0.0f) return;
    const Renderer2DBlendMode prev = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    Renderer2D_DrawQuad(-1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, g_FlashAlpha * 0.55f * static_cast<float>(GameSettings::Get().flashPercent) / 100.0f); // (settings: FLASHES)
    Renderer2D_SetBlendMode(prev);
}
}

void GameBoss::Draw()
{
    if (g_State == State::Inactive || g_State == State::Warning) return;
    ++g_DrawFrame;
    // everything below reads g_X / g_Y / the ship positions: for the time of this function they hold the interpolated values
    const float realX = g_X, realY = g_Y;
    float realShipX[2], realShipY[2];
    g_X = GameTimer::Blend(g_PrevX, g_X);
    g_Y = GameTimer::Blend(g_PrevY, g_Y);
    for (int i = 0; i < 2; ++i)
    {
        realShipX[i] = g_Ship[i].x;
        realShipY[i] = g_Ship[i].y;
        g_Ship[i].x = GameTimer::Blend(g_Ship[i].prevX, g_Ship[i].x);
        g_Ship[i].y = GameTimer::Blend(g_Ship[i].prevY, g_Ship[i].y);
    }
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);

    DrawBerserkVignette();
    DrawFlipLine();
    DrawTelegraphs();
    DrawBeams();
    DrawHullPhases();
    DrawDroneLaser();
    DrawWhiteWarning();
    DrawDefeatFlash();
    DrawShocks();
    DrawDebris();
    DrawBolts();
    DrawMotes();
    DrawScreenFlash();

    Renderer2D_SetBlendMode(previous);
    g_X = realX;
    g_Y = realY;
    for (int i = 0; i < 2; ++i)
    {
        g_Ship[i].x = realShipX[i];
        g_Ship[i].y = realShipY[i];
    }
}
