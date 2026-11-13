// How the enemies and their bullets are DRAWN: the bodies, the warnings, the marks of an elite and its shield, the rails, the muzzle flashes. The logic (movement, attacks, spawning)
// is GameEnemy.cpp; what the two share is EnemyInternal.h.

#include "game/GameEnemy.h"
#include "game/GameVfx.h"
#include "game/EnemyArt.h"
#include "game/GameEffect.h"
#include "core/Renderer2D.h"
#include "core/AssetManager.h"
#include "game/GameSpecialShot.h"
#include "core/GameAudio.h"
#include "game/ScreenShake.h"
#include "game/GameState.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "core/GameTimer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "game/BeamFx.h"
#include "game/EnemyInternal.h"

using GameVfx::Sprite;
using namespace EnemyInternal;

namespace EnemyInternal
{
int g_VisualFrame = 0;  // the clock of the drawing (a step of the screen, not of the world)
}

namespace
{

void DrawTargetCorners(float x, float y, float radius, float r, float g, float b, float a)
{
    const float ry = radius / ASPECT;
    const float leg = radius * 0.38f;

    for (int sx = -1; sx <= 1; sx += 2)
    {
        for (int sy = -1; sy <= 1; sy += 2)
        {
            const float cx = x + static_cast<float>(sx) * radius;
            const float cy = y + static_cast<float>(sy) * ry;

            GameVfx::DrawLine(cx, cy, cx - static_cast<float>(sx) * leg, cy, 0.0025f, r, g, b, a);
            GameVfx::DrawLine(cx, cy, cx, cy - static_cast<float>(sy) * leg / ASPECT, 0.0025f, r, g, b, a);
        }
    }
}

void DrawMuzzleVisuals()
{
    for (const auto& v : g_MuzzleVisuals)
    {
        if (v.timer <= 0)
        {
            continue;
        }
        float r, g, b;
        VisualColor(v.polarity, r, g, b);
        const float life = static_cast<float>(v.timer) / 6.0f;
        const float grow = 1.0f - life;
        const float dirX = std::cos(v.angle);
        const float dirY = std::sin(v.angle) / ASPECT;
        // bloom + hot core
        GameVfx::DrawRound(Sprite::Glow, v.x, v.y, 0.030f + 0.075f * life, r, g, b, life * 0.90f);
        GameVfx::DrawRound(Sprite::Glow, v.x, v.y, 0.012f + 0.024f * life, 1.0f, 1.0f, 1.0f, life);
        // a flare along the shot and a short cone
        const float length = 0.030f + 0.090f * life;
        GameVfx::Draw(Sprite::Spark, v.x + dirX * length * 0.5f, v.y + dirY * length * 0.5f, length, 0.016f, v.angle, 1.0f, 0.95f, 0.88f, life);
        for (int side = -1; side <= 1; side += 2)
        {
            const float a = v.angle + 0.42f * static_cast<float>(side);
            const float len = 0.026f + 0.045f * life;
            GameVfx::Draw(Sprite::Spark, v.x + std::cos(a) * len * 0.5f, v.y + std::sin(a) * len * 0.5f / ASPECT, len, 0.012f, a, r, g, b,
                          life * 0.85f);
        }
        // a small shock ring that opens up
        GameVfx::DrawRound(Sprite::Ring, v.x, v.y, 0.018f + 0.070f * grow, r, g, b, life * 0.45f);
    }
}

void DrawRails()
{
    int index = 0;
    for (const Rail& r : g_Rails)
    {
        ++index;
        if (!r.active) continue;
        const float len = 2.6f;
        const float x1 = r.x0 + std::cos(r.angle) * len;
        const float y1 = r.y0 + std::sin(r.angle) * len / ASPECT;
        const BeamFx::Palette palette = BeamFx::WhitePalette();
        if (r.state == 1)
        {
            // the aim: a line that the player can read (it does not move), and the light that is drawn into the muzzle
            const float progress = static_cast<float>(r.t) / static_cast<float>(RAIL_WARN);
            const bool late = r.t >= RAIL_WARN - 14;
            const float a = late ? ((r.t / 3) % 2 == 0 ? 0.90f : 0.35f) : 0.20f + 0.40f * progress;
            GameVfx::DrawLine(r.x0, r.y0, x1, y1, late ? 0.0045f : 0.0028f, 1.0f, 1.0f, 1.0f, a);
            BeamFx::DrawChargeUp(r.x0, r.y0, 0.13f, progress, static_cast<float>(r.t), palette, index * 11);
            continue;
        }
        // the shot: the plasma of every beam of the game (BeamFx), white and cold like the laser of the drone
        const float fade = (std::min)(1.0f, static_cast<float>(RAIL_FIRE - r.t) / 6.0f);
        BeamFx::BeamParams beam;
        beam.x0 = r.x0;
        beam.y0 = r.y0;
        beam.x1 = x1;
        beam.y1 = y1;
        beam.halfWidth = RAIL_HALF * 1.1f;
        beam.time = static_cast<float>(r.t);
        beam.envelope = fade;
        beam.turbulence = 0.5f;
        beam.arcs = 2;
        beam.pulseSpeed = 1.6f;
        beam.seed = 20 + index * 7;
        BeamFx::DrawBeam(beam, palette);
        BeamFx::DrawMuzzle(r.x0, r.y0, 0.07f, static_cast<float>(r.t), fade, palette);
    }
}

// The marks of an elite and its shield, in the hard-edged style of the effects (no soft glow: bars, diamonds, one short flash).
//   elite   four gold corner brackets (hard bars) that breathe a little
//   shield  a hexagon barrier in cold white, fixed in its orientation: a hairline on every side, one running light that circles it, a small
//           diamond on every corner. A plain shot that the shield holds off makes the sides facing the ship flare and a second hexagon
//           spread out of it and fade (the shield took the shot). The beam breaks it for good: GameEffect::SpawnShieldBreak (Combat.cpp).
void DrawEliteMarks(const Enemy& enemy)
{
    if (enemy.state == EnemyState::Enter && enemy.age < 20) return;
    if (enemy.elite)
    {
        const float k = 0.75f + 0.25f * std::sin(static_cast<float>(enemy.age) * 0.12f);
        const float w = enemy.halfWidth * 1.25f, h = enemy.halfHeight * 1.15f, l = 0.028f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2)
            {
                const float cx = enemy.x + static_cast<float>(sx) * w, cy = enemy.y + static_cast<float>(sy) * h;
                GameVfx::DrawBar(cx, cy, cx - static_cast<float>(sx) * l, cy, 0.0042f, 1.0f, 0.82f, 0.30f, 0.85f * k);
                GameVfx::DrawBar(cx, cy, cx, cy - static_cast<float>(sy) * l / ASPECT, 0.0042f, 1.0f, 0.82f, 0.30f, 0.85f * k);
            }
    }
    if (!enemy.shield) return;
    const float t = static_cast<float>(enemy.age);
    const float flash = enemy.hitFlashTimer > 0 ? static_cast<float>(enemy.hitFlashTimer) / 4.0f : 0.0f; // 1 on the frame of a hit, 0 at rest
    const float radius = enemy.halfWidth * 1.35f * (1.0f + 0.012f * std::sin(t * 0.06f)); // (it breathes a little)
    constexpr float CR = 0.70f, CG = 0.86f, CB = 1.0f; // the cold white of the shield
    float vx[6], vy[6];
    for (int i = 0; i < 6; i++)
    {
        const float a = PI * static_cast<float>(i) / 3.0f;
        vx[i] = enemy.x + std::cos(a) * radius;
        vy[i] = enemy.y + std::sin(a) * radius / ASPECT;
    }
    // the barrier: a faint glass fill (additive, a few percent), so the hull behind it stays what it is
    for (int i = 0; i < 6; i++)
        GameVfx::DrawTri(enemy.x, enemy.y, vx[i], vy[i], vx[(i + 1) % 6], vy[(i + 1) % 6], CR, CG, CB, 0.025f + 0.10f * flash * (i == 2 || i == 3 ? 1.0f : 0.25f));
    // the sides: a hairline, brighter and thicker on the two sides that face the ship while a shot is held off
    for (int i = 0; i < 6; i++)
    {
        const bool facing = (i == 2 || i == 3);
        const float boost = facing ? flash : 0.0f;
        GameVfx::DrawBar(vx[i], vy[i], vx[(i + 1) % 6], vy[(i + 1) % 6], 0.0022f + 0.0035f * boost, CR + (1.0f - CR) * boost, CG + (1.0f - CG) * boost, CB,
                         0.30f + 0.60f * boost);
    }
    // the running light: one bright stretch of 1.6 sides that circles the hexagon (every shield starts at its own place)
    {
        const float phase = std::fmod(t * 0.0042f + enemy.baseY * 2.9f, 1.0f) * 6.0f;
        constexpr float LENGTH = 1.6f;
        for (int wrap = -1; wrap <= 0; wrap++)
        {
            const float u0 = phase + static_cast<float>(wrap) * 6.0f, u1 = u0 + LENGTH;
            for (int i = 0; i < 6; i++)
            {
                const float s0 = std::max(u0, static_cast<float>(i)), s1 = std::min(u1, static_cast<float>(i + 1));
                if (s1 <= s0) continue;
                const float f0 = s0 - static_cast<float>(i), f1 = s1 - static_cast<float>(i);
                const int j = (i + 1) % 6;
                GameVfx::DrawBar(vx[i] + (vx[j] - vx[i]) * f0, vy[i] + (vy[j] - vy[i]) * f0, vx[i] + (vx[j] - vx[i]) * f1, vy[i] + (vy[j] - vy[i]) * f1, 0.0040f, 0.90f, 0.96f, 1.0f,
                                 0.85f);
            }
        }
    }
    // the corners: hard diamonds
    for (int i = 0; i < 6; i++) GameVfx::DrawDiamond(vx[i], vy[i], 0.0070f, 0.0f, 0.85f, 0.94f, 1.0f, 0.75f);
    // a shot held off: a second hexagon spreads out of the first and fades
    if (flash > 0.0f)
    {
        const float grow = 1.0f + 0.14f * (1.0f - flash);
        for (int i = 0; i < 6; i++)
        {
            const float a0 = PI * static_cast<float>(i) / 3.0f, a1 = PI * static_cast<float>(i + 1) / 3.0f;
            GameVfx::DrawBar(enemy.x + std::cos(a0) * radius * grow, enemy.y + std::sin(a0) * radius * grow / ASPECT, enemy.x + std::cos(a1) * radius * grow,
                             enemy.y + std::sin(a1) * radius * grow / ASPECT, 0.0026f, 1.0f, 1.0f, 1.0f, 0.55f * flash);
        }
    }
}

int GetEnemyTextureId(EnemyType type)
{
    using AssetManager::Tex;

    switch (type)
    {
    case EnemyType::Sine:
        return AssetManager::Get(Tex::EnemySine);

    case EnemyType::Heavy:
        return AssetManager::Get(Tex::EnemyHeavy);

    case EnemyType::Charger:
        return AssetManager::Get(Tex::EnemyCharger);

    case EnemyType::Turret:
        return AssetManager::Get(Tex::EnemyTurret);

    case EnemyType::Sniper:
        return AssetManager::Get(Tex::EnemySniper);

    case EnemyType::Rival:
        return AssetManager::Get(Tex::EnemyRival);

    case EnemyType::Scout:
    default:
        return AssetManager::Get(Tex::EnemyScout);
    }
}

void GetEnemyVisualScale(EnemyType type, float& widthScale, float& heightScale)
{
    EnemyArt::VisualScale(type, widthScale, heightScale);
}

Polarity GetDisplayedShotPolarity(const Enemy& enemy)
{
    if (enemy.type == EnemyType::Turret && enemy.state == EnemyState::Attack && enemy.attackPatternIndex == 2)
        return enemy.polarity == Polarity::Red ? Polarity::Blue : Polarity::Red;
    return enemy.polarity;
}

// The hulls are dark: their light strips show a dim neutral light that turns into the colour of the next shot while the enemy winds up
// (and for the last frames before a scout / sine fires), and flashes it when it fires. The rival - the black mirror of the ship - always shows its colour.
void DisplayColor(const Enemy& enemy, float charge, float& r, float& g, float& b)
{
    VisualColor(GetDisplayedShotPolarity(enemy), r, g, b);
    if (enemy.type == EnemyType::Rival) return;
    float heat = charge;
    if (enemy.kick > 0) heat = std::max(heat, static_cast<float>(enemy.kick) / 8.0f);
    if ((enemy.type == EnemyType::Scout || enemy.type == EnemyType::Sine) && enemy.shotTimer > 0 && enemy.shotTimer <= 12)
        heat = std::max(heat, 1.0f - static_cast<float>(enemy.shotTimer) / 12.0f);
    heat = std::min(1.0f, std::max(0.0f, heat));
    heat = heat * heat * (3.0f - 2.0f * heat);
    constexpr float NR = 0.30f, NG = 0.36f, NB = 0.46f; // the idle light (dim: an enemy must never outshine the bullets)
    r = NR + (r - NR) * heat;
    g = NG + (g - NG) * heat;
    b = NB + (b - NB) * heat;
}

struct EnemyVisualSpec
{
    float coreU;
    float coreV;
    int nozzleCount;
    float nozzleU;
    float nozzleV[2];
    float flameLength;
    float flameHeight;
};

const EnemyVisualSpec& GetVisualSpec(EnemyType type)
{
    static const EnemyVisualSpec specs[7] = {
        // (the nozzles of the enemy art v2: tools/toon/enemies2.py)
        {0.40f, 0.50f, 1, 0.975f, {0.50f, 0.50f}, 0.060f, 0.028f}, {0.30f, 0.50f, 1, 0.97f, {0.50f, 0.50f}, 0.058f, 0.028f},
        {0.50f, 0.50f, 2, 0.98f, {0.30f, 0.70f}, 0.074f, 0.032f}, {0.45f, 0.50f, 2, 0.98f, {0.36f, 0.64f}, 0.064f, 0.028f},
        {0.50f, 0.50f, 2, 0.915f, {0.36f, 0.64f}, 0.066f, 0.030f}, {0.35f, 0.50f, 2, 0.99f, {0.40f, 0.60f}, 0.056f, 0.024f},
        {0.50f, 0.50f, 2, 0.95f, {0.41f, 0.59f}, 0.064f, 0.026f}};

    int index = static_cast<int>(type);
    if (index < 0 || index > 6)
    {
        index = 0;
    }

    return specs[index];
}

bool IsWarningState(const Enemy& enemy)
{
    return enemy.state == EnemyState::AttackWarning || enemy.state == EnemyState::ChargeWarning;
}

float GetWarningProgress(const Enemy& enemy)
{
    if (!IsWarningState(enemy))
    {
        return 0.0f;
    }

    float duration = 30.0f;
    int timer = enemy.warningTimer;

    switch (enemy.type)
    {
    case EnemyType::Sine:
        duration = enemy.pendingAttackType == EnemyAttackType::Homing ? 36.0f : 24.0f;
        break;

    case EnemyType::Heavy:
        duration = enemy.pendingAttackType == EnemyAttackType::Ring ? 34.0f : 22.0f;
        break;

    case EnemyType::Charger:
        duration = 45.0f;
        timer = enemy.stateTimer;
        break;

    case EnemyType::Turret:
        duration = 30.0f;
        break;

    case EnemyType::Sniper:
        duration = 40.0f;
        break;

    case EnemyType::Rival:
        duration = static_cast<float>(RivalWarningFrames(enemy.pendingAttackType, RivalTier(enemy)));
        break;

    default:
        break;
    }

    return ClampFloat(1.0f - static_cast<float>(timer) / duration, 0.0f, 1.0f);
}

float GetEnemyThrust(const Enemy& enemy, float warningProgress)
{
    switch (enemy.type)
    {
    case EnemyType::Charger:
        if (enemy.state == EnemyState::Charging)
        {
            return 2.4f;
        }
        if (enemy.state == EnemyState::ChargeWarning)
        {
            return 0.35f + 0.90f * warningProgress;
        }
        return 1.0f;

    case EnemyType::Heavy:
    case EnemyType::Turret:
        return enemy.state == EnemyState::Enter ? 1.0f : 0.30f;

    case EnemyType::Sniper:
        if (enemy.state == EnemyState::Reposition)
        {
            return 1.5f;
        }
        return enemy.state == EnemyState::Enter ? 1.0f : 0.30f;

    case EnemyType::Rival:
        if (enemy.state == EnemyState::Attack && enemy.pendingAttackType == EnemyAttackType::Dash)
        {
            return 2.6f;
        }
        if (enemy.state == EnemyState::AttackWarning && enemy.pendingAttackType == EnemyAttackType::Dash)
        {
            return 0.25f + 1.20f * warningProgress; // coiling: the engines spool up
        }
        if (enemy.state == EnemyState::Evade)
        {
            return 2.2f;
        }
        if (enemy.state == EnemyState::Reposition)
        {
            return 1.4f;
        }
        if (enemy.state == EnemyState::Enter)
        {
            return 1.2f;
        }
        return 0.55f;

    default:
        return 1.0f;
    }
}

void DrawEnemyEngine(const Enemy& enemy, const EnemyVisualSpec& spec, const EnemyArt::Pose& pose, float alpha, float thrust, float charge)
{
    float r, g, b;
    DisplayColor(enemy, charge, r, g, b);

    const float lengthScale = ClampFloat(0.35f + 0.65f * thrust, 0.30f, 2.10f);
    const float widthScale = ClampFloat(0.70f + 0.30f * thrust, 0.65f, 1.35f);
    const float intensity = ClampFloat(0.45f + 0.40f * thrust, 0.45f, 1.00f);
    const float time = static_cast<float>(g_VisualFrame);
    const float axisX = std::cos(pose.rot); // the exhaust runs along the hull's own +x, turned by the pose
    const float axisY = std::sin(pose.rot) / ASPECT;

    for (int i = 0; i < spec.nozzleCount; i++)
    {
        float x, y;
        pose.Point(spec.nozzleU, spec.nozzleV[i], x, y);

        const float phase = static_cast<float>(i) * 2.1f + enemy.baseY * 9.0f + static_cast<float>(enemy.groupId) * 3.0f;
        const float flicker = 0.90f + 0.07f * std::sin(time * 0.55f + phase) + 0.03f * std::sin(time * 1.30f + phase * 1.7f);

        const float length = spec.flameLength * lengthScale * flicker;
        const float height = spec.flameHeight * widthScale;

        GameVfx::Draw(Sprite::Flame, x + axisX * length * 0.5f, y + axisY * length * 0.5f, length, height, PI + pose.rot, r, g, b, 0.80f * intensity * alpha);

        GameVfx::Draw(Sprite::Flame, x + axisX * length * 0.30f, y + axisY * length * 0.30f, length * 0.55f, height * 0.40f, PI + pose.rot, 1.0f, 1.0f, 1.0f,
                      0.85f * intensity * alpha);

        GameVfx::DrawRound(Sprite::Glow, x, y, height * 1.6f, r, g, b, 0.35f * intensity * alpha);
    }
}

void DrawEnemyGhosts(const Enemy& enemy, int texture, const EnemyArt::Pose& pose, float halfWidth, float halfHeight)
{
    if (texture < 0)
    {
        return;
    }

    float stepX = 0.0f;
    float stepY = 0.0f;
    float strength = 0.0f;

    const bool trailing = (enemy.type == EnemyType::Charger && enemy.state == EnemyState::Charging)
                          || (enemy.type == EnemyType::Rival && enemy.state == EnemyState::Attack && enemy.pendingAttackType == EnemyAttackType::Dash); // the trail of the charger, and of the rival's dash
    if (trailing)
    {
        stepX = -enemy.vx * 1.4f;
        stepY = -enemy.vy * 1.4f;
        strength = 1.0f;
    }
    else if (enemy.type == EnemyType::Rival && enemy.state == EnemyState::Evade && enemy.warningTimer <= 0)
    {
        const float directionX = enemy.targetX - enemy.x;
        const float directionY = (enemy.targetY - enemy.y) * ASPECT;
        const float length = std::sqrt(directionX * directionX + directionY * directionY);

        if (length > 0.001f)
        {
            stepX = -directionX / length * 0.022f;
            stepY = -directionY / length * 0.022f / ASPECT;
            strength = 0.85f;
        }
    }
    else if (enemy.type == EnemyType::Rival && enemy.state == EnemyState::Reposition) // it glides to its next place: a faint trail
    {
        const float directionX = enemy.targetX - enemy.x;
        const float directionY = (enemy.targetY - enemy.y) * ASPECT;
        const float length = std::sqrt(directionX * directionX + directionY * directionY);
        if (length > 0.05f)
        {
            stepX = -directionX / length * 0.016f;
            stepY = -directionY / length * 0.016f / ASPECT;
            strength = 0.45f;
        }
    }
    else if (enemy.type == EnemyType::Rival && enemy.state == EnemyState::Attack && enemy.pendingAttackType == EnemyAttackType::HalfRing) // the orbit
    {
        stepX = -std::sin(enemy.aux) * -RIVAL_ORBIT_X * 0.060f * enemy.vy * 0.8f;
        stepY = std::cos(enemy.aux) * RIVAL_ORBIT_Y * 0.060f * enemy.vy * -0.8f;
        strength = 0.45f;
    }
    if (strength <= 0.0f)
    {
        return;
    }

    float r, g, b;
    VisualColor(enemy.polarity, r, g, b);

    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);

    for (int k = 1; k <= 3; k++)
    {
        const float fade = 0.30f - 0.08f * static_cast<float>(k - 1);

        Renderer2D_DrawTextureEx(texture, pose.x + stepX * static_cast<float>(k), pose.y + stepY * static_cast<float>(k),
                                 halfWidth * 2.0f, halfHeight * 2.0f, pose.rot, r * 1.5f, g * 1.5f, b * 1.5f, fade * strength, 0.0f,
                                 0.0f, 1.0f, 1.0f);
    }

    Renderer2D_SetBlendMode(previous);
}

void DrawEnemyCore(const Enemy& enemy, float x, float y, float halfWidth, float alpha, float charge)
{
    float r, g, b;
    DisplayColor(enemy, charge, r, g, b); // (neutral at rest, its colour when it winds up / fires)

    const float pulse =
        0.5f
        + 0.5f * std::sin(static_cast<float>(g_VisualFrame) * 0.09f + enemy.baseY * 7.0f + static_cast<float>(enemy.groupId));

    const float unit = ClampFloat(halfWidth, 0.04f, 0.14f);

    GameVfx::DrawRound(Sprite::Glow, x, y, unit * (0.60f + 0.35f * charge), r, g, b,
                       (0.20f + 0.10f * pulse + 0.40f * charge) * alpha);

    GameVfx::DrawRound(Sprite::Glow, x, y, unit * (0.30f + 0.14f * charge), r, g, b, (0.80f + 0.20f * charge) * alpha);

    GameVfx::DrawRound(Sprite::Glow, x, y, unit * (0.12f + 0.07f * charge), 1.0f, 1.0f, 1.0f, (0.60f + 0.40f * charge) * alpha);
}

// velocity: how far the enemy moved in the last world step (the art tilts the hull into a turn and aims the lance of a ram along it)
void DrawEnemyBody(const Enemy& enemy, float velocityX, float velocityY)
{
    const Renderer2DBlendMode previousBlend = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);

    float widthScale = 1.0f;
    float heightScale = 1.0f;
    GetEnemyVisualScale(enemy.type, widthScale, heightScale);

    float hw = enemy.halfWidth * widthScale;
    float hh = enemy.halfHeight * heightScale;

    if (enemy.type == EnemyType::Charger && enemy.state == EnemyState::Charging)
    {
        hw *= 1.18f;
        hh *= 0.92f;
    }

    if (enemy.type == EnemyType::Rival && (enemy.state == EnemyState::Evade || (enemy.state == EnemyState::Attack && enemy.pendingAttackType == EnemyAttackType::Dash)))
    {
        hw *= 1.12f;
        hh *= 0.88f;
    }

    const float alpha = enemy.state == EnemyState::Evade ? 0.82f : 1.0f;
    const float flashRate =
        enemy.hitFlashTimer > 0 ? ClampFloat(static_cast<float>(enemy.hitFlashTimer) / 4.0f, 0.0f, 1.0f) : 0.0f;

    const float visualX = enemy.x + flashRate * 0.006f;
    const int texture = GetEnemyTextureId(enemy.type);
    const EnemyVisualSpec& spec = GetVisualSpec(enemy.type);

    const float warningProgress = GetWarningProgress(enemy);
    const float charge = IsWarningState(enemy) ? warningProgress : 0.0f;
    const float thrust = GetEnemyThrust(enemy, warningProgress);

    EnemyArt::View view;
    view.enemy = &enemy;
    view.x = visualX;
    view.y = enemy.y;
    view.vx = velocityX;
    view.vy = velocityY;
    view.hw = hw;
    view.hh = hh;
    view.alpha = alpha;
    DisplayColor(enemy, charge, view.r, view.g, view.b);
    view.charge = charge;
    view.frame = g_VisualFrame;
    const EnemyArt::Pose pose = EnemyArt::MakePose(view);

    EnemyArt::DrawUnder(view, pose);
    DrawEnemyEngine(enemy, spec, pose, alpha, thrust, charge);
    DrawEnemyGhosts(enemy, texture, pose, hw, hh);

    Renderer2D_DrawTextureEx(texture, pose.x, pose.y, hw * 2.0f, hh * 2.0f, pose.rot, 1.0f, 1.0f, 1.0f, alpha, 0.0f, 0.0f, 1.0f,
                             1.0f);

    if (flashRate > 0.0f)
    {
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);

        Renderer2D_DrawTextureEx(texture, pose.x, pose.y, hw * 2.0f, hh * 2.0f, pose.rot, 1.00f, 0.92f, 0.80f,

                                 flashRate * 0.60f * alpha, 0.0f, 0.0f, 1.0f, 1.0f);

        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    }

    EnemyArt::DrawGlow(view, pose); // the plating, lit in the colour of the enemy

    float coreX, coreY;
    pose.Point(spec.coreU, spec.coreV, coreX, coreY);

    DrawEnemyCore(enemy, coreX, coreY, hw, alpha, charge);

    EnemyArt::DrawOver(view, pose); // what belongs to this kind of enemy only

    Renderer2D_SetBlendMode(previousBlend);
}

void DrawChargeVisual(const Enemy& enemy, float p, bool ringAttack)
{
    float r, g, b;
    VisualColor(GetDisplayedShotPolarity(enemy), r, g, b);

    const float x = enemy.x - enemy.halfWidth;
    const float y = enemy.y;
    const float scale = ringAttack ? 1.45f : 1.0f;

    GameVfx::DrawRound(Sprite::Glow, x, y, (0.026f + 0.056f * p) * scale, r, g, b, 0.20f + 0.60f * p);
    GameVfx::DrawRound(Sprite::Glow, x, y, (0.010f + 0.020f * p) * scale, 1.0f, 1.0f, 1.0f, 0.35f + 0.65f * p);
    GameVfx::DrawRound(Sprite::Ring, x, y, (0.095f - 0.060f * p) * scale, r, g, b, 0.12f + 0.55f * p);

    if (ringAttack)
    {
        GameVfx::DrawRound(Sprite::Ring, x, y, (0.150f - 0.090f * p) * scale, r, g, b, 0.08f + 0.30f * p);
    }

    for (int i = 0; i < 4; i++)
    {
        const float angle = 1.5707963f * static_cast<float>(i) + 0.55f;
        const float distance = (0.075f * (1.0f - p) + 0.012f) * scale;

        GameVfx::Draw(Sprite::Spark, x + std::cos(angle) * distance, y + std::sin(angle) * distance / ASPECT, 0.030f * scale,
                      0.012f, angle + PI, r, g, b, 0.15f + 0.75f * p);
    }
}

void DrawAimLine(float startX, float startY, float targetX, float targetY, float extend, float p, float r, float g, float b)
{
    const float directionX = targetX - startX;
    const float directionY = (targetY - startY) * ASPECT;
    const float length = std::sqrt(directionX * directionX + directionY * directionY);

    if (length < 0.001f)
    {
        return;
    }

    const float endX = targetX + directionX / length * extend;
    const float endY = targetY + directionY / length * extend / ASPECT;

    const float thickness = 0.0040f + 0.0024f * p;
    const float alpha = 0.16f + 0.52f * p;

    GameVfx::DrawLine(startX, startY, endX, endY, thickness * 3.2f, r, g, b, 0.05f + 0.10f * p);
    GameVfx::DrawLine(startX, startY, endX, endY, thickness, r, g, b, alpha);

    GameVfx::DrawLine(startX, startY, startX + (endX - startX) * 0.28f, startY + (endY - startY) * 0.28f, thickness * 1.4f, r, g,
                      b, alpha * 1.1f);
}

void DrawTargetReticle(float x, float y, float p, float r, float g, float b)
{
    GameVfx::DrawRound(Sprite::Ring, x, y, 0.085f - 0.050f * p, r, g, b, 0.18f + 0.50f * p);
    DrawTargetCorners(x, y, 0.022f, r, g, b, 0.20f + 0.40f * p);
}

void DrawSineAttackWarning(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Sine || enemy.state != EnemyState::AttackWarning)
    {
        return;
    }

    const float p = GetWarningProgress(enemy);
    DrawChargeVisual(enemy, p, false);

    float r, g, b;
    VisualColor(GetDisplayedShotPolarity(enemy), r, g, b);

    if (enemy.pendingAttackType == EnemyAttackType::Homing)
    {
        DrawTargetReticle(enemy.targetX, enemy.targetY, p, r, g, b);
    }
    else
    {
        DrawAimLine(enemy.x - enemy.halfWidth, enemy.y, enemy.targetX, enemy.targetY, 0.0f, p, r, g, b);
    }
}

void DrawSniperAttackWarning(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Sniper || enemy.state != EnemyState::AttackWarning)
    {
        return;
    }

    const float p = GetWarningProgress(enemy);
    DrawChargeVisual(enemy, p, false);

    float r, g, b;
    VisualColor(GetDisplayedShotPolarity(enemy), r, g, b);

    r = g = b = 1.0f; // the sniper's bullet is white: it cannot be absorbed
    DrawAimLine(enemy.x - enemy.halfWidth, enemy.y, enemy.targetX, enemy.targetY, 0.45f, p, r, g, b);
    DrawTargetReticle(enemy.targetX, enemy.targetY, p, r, g, b);
}

// The lane of a dash: a band from the rival to the place it locked, 0.10 beyond it (but never past the left wall), as wide as the rival. It follows the ship until 14 frames
// before the dash and is locked after that: the band stops following and blinks.
void DrawDashLane(const Enemy& enemy, float p, float r, float g, float b)
{
    const float sx = enemy.x - enemy.halfWidth;
    const float sy = enemy.y;
    float dx = enemy.targetX - sx;
    float dy = (enemy.targetY - sy) * ASPECT;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.02f)
    {
        return;
    }
    dx /= length;
    dy /= length;
    float reach = length + 0.10f;
    if (dx < -0.001f)
    {
        reach = (std::min)(reach, (sx + 0.84f) / -dx); // the rival stops at the left wall
    }
    const bool locked = enemy.warningTimer <= 14;
    const float width = enemy.halfHeight * 1.10f * ASPECT; // half width of the lane (equal-scale units)
    const float nx = -dy;
    const float ny = dx;
    auto corner = [&](float t, float w, float alpha) { return Renderer2DVertex{sx + dx * t + nx * w, sy + (dy * t + ny * w) / ASPECT, r, g, b, alpha}; };
    const float a0 = (locked ? 0.20f : 0.10f) * (0.4f + 0.6f * p);
    const float a1 = a0 * 0.35f;
    const Renderer2DVertex lane[4] = {corner(0.0f, width, a0), corner(reach, width, a1), corner(reach, -width, a1), corner(0.0f, -width, a0)};
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    Renderer2D_DrawPolygon(lane, 4);
    Renderer2D_SetBlendMode(previous);
    const float edge = (locked ? 0.70f : 0.35f) * (0.4f + 0.6f * p) * ((locked && (enemy.warningTimer / 2) % 2 == 0) ? 0.55f : 1.0f);
    for (int side = -1; side <= 1; side += 2)
    {
        const float w = width * static_cast<float>(side);
        GameVfx::DrawLine(sx + nx * w, sy + ny * w / ASPECT, sx + dx * reach + nx * w, sy + (dy * reach + ny * w) / ASPECT, 0.0034f, r, g, b, edge);
    }
    // chevrons run along the lane towards the place it is going to
    for (int i = 0; i < 5; ++i)
    {
        const float t = std::fmod(static_cast<float>(i) / 5.0f + static_cast<float>(g_VisualFrame) * 0.016f, 1.0f) * reach;
        const float fade = (1.0f - t / reach) * (0.30f + 0.55f * p);
        const float cx = sx + dx * t;
        const float cy = sy + dy * t / ASPECT;
        GameVfx::DrawLine(cx + nx * width * 0.5f - dx * 0.018f, cy + (ny * width * 0.5f - dy * 0.018f) / ASPECT, cx, cy, 0.0034f, r, g, b, fade);
        GameVfx::DrawLine(cx - nx * width * 0.5f - dx * 0.018f, cy + (-ny * width * 0.5f - dy * 0.018f) / ASPECT, cx, cy, 0.0034f, r, g, b, fade);
    }
    DrawTargetReticle(enemy.targetX, enemy.targetY, locked ? 1.0f : p * 0.6f, r, g, b);
}

// the orbit of the half ring move, drawn before it starts: a dotted ellipse round the point that is one radius to the left of the rival
void DrawOrbitPreview(const Enemy& enemy, float p, float r, float g, float b)
{
    const float cx = enemy.x - RIVAL_ORBIT_X;
    const float cy = enemy.y;
    constexpr int DOTS = 28;
    for (int i = 0; i < DOTS; ++i)
    {
        const float a = PI * 2.0f * static_cast<float>(i) / static_cast<float>(DOTS) + static_cast<float>(g_VisualFrame) * 0.03f * (enemy.polarity == Polarity::Red ? 1.0f : -1.0f);
        GameVfx::DrawRound(Sprite::Glow, cx + std::cos(a) * RIVAL_ORBIT_X, cy + std::sin(a) * RIVAL_ORBIT_Y, 0.012f, r, g, b, (0.10f + 0.30f * p) * (0.5f + 0.5f * std::sin(a * 2.0f + static_cast<float>(g_VisualFrame) * 0.1f)));
    }
}

void DrawRivalAttackWarning(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Rival || enemy.state != EnemyState::AttackWarning)
    {
        return;
    }
    const float p = GetWarningProgress(enemy);
    const EnemyAttackType type = enemy.pendingAttackType;
    float r, g, b;
    VisualColor(GetDisplayedShotPolarity(enemy), r, g, b);
    if (type == EnemyAttackType::Dash)
    {
        DrawChargeVisual(enemy, p, false);
        DrawDashLane(enemy, p, r, g, b);
        return;
    }
    DrawChargeVisual(enemy, p, type == EnemyAttackType::Ring || type == EnemyAttackType::HalfRing);
    if (type == EnemyAttackType::Needles)
    {
        // a white line to the place that the needles go to; it follows the ship (and leads it) until the last 12 frames, then it is locked and blinks
        const bool locked = enemy.warningTimer <= NEEDLE_LOCK_FRAMES;
        const float q = locked ? 1.0f : 0.35f + 0.35f * p;
        DrawAimLine(enemy.x - enemy.halfWidth, enemy.y, enemy.targetX, enemy.targetY, 0.45f, q, 1.0f, 1.0f, 1.0f);
        DrawTargetReticle(enemy.targetX, enemy.targetY, locked ? 1.0f : p * 0.6f, 1.0f, 1.0f, 1.0f);
        if (locked && (enemy.warningTimer / 2) % 2 == 0)
        {
            GameVfx::DrawRound(Sprite::Ring, enemy.targetX, enemy.targetY, 0.060f, 1.0f, 1.0f, 1.0f, 0.55f);
        }
    }
    else if (type == EnemyAttackType::HalfRing)
    {
        DrawOrbitPreview(enemy, p, r, g, b);
    }
    else if (type != EnemyAttackType::Ring)
    {
        DrawAimLine(enemy.x - enemy.halfWidth, enemy.y, enemy.targetX, enemy.targetY, 0.0f, p, r, g, b); // fan, counter
    }
}

void DrawRivalPolaritySwitch(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Rival || enemy.polaritySwitchTimer <= 0)
    {
        return;
    }

    float r, g, b;
    VisualColor(enemy.polarity, r, g, b);

    const float life = static_cast<float>(enemy.polaritySwitchTimer) / 18.0f;
    const float size = (0.070f + (1.0f - life) * 0.080f);

    GameVfx::DrawRound(Sprite::Ring, enemy.x, enemy.y, size, r, g, b, life * 0.75f);
    GameVfx::DrawRound(Sprite::Glow, enemy.x, enemy.y, size * 0.9f, r, g, b, life * 0.35f);
}

void DrawRivalEvadeWarning(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Rival || enemy.state != EnemyState::Evade || enemy.warningTimer <= 0)
    {
        return;
    }
    const float p = 1.0f - static_cast<float>(enemy.warningTimer) / 8.0f;
    const float size = enemy.halfWidth * 2.0f * (1.6f + 1.4f * p);
    GameVfx::DrawRound(Sprite::Ring, enemy.x, enemy.y, size, 0.70f, 0.85f, 1.00f, (1.0f - p) * 0.60f);
    // a "!" above the ship: it has seen the beam
    const float topY = enemy.y + enemy.halfHeight * 1.5f;
    const float a = 1.0f - 0.5f * p;
    GameVfx::DrawLine(enemy.x, topY + 0.115f, enemy.x, topY + 0.040f, 0.010f, 1.0f, 1.0f, 1.0f, a);
    GameVfx::DrawRound(Sprite::Glow, enemy.x, topY, 0.024f, 1.0f, 1.0f, 1.0f, a);
}

// The effects of the mini boss that are not a bullet: the dodge, the landing and the flash of the second half.
void DrawRivalFx(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Rival || enemy.fxKind == 0 || enemy.fxTimer <= 0)
    {
        return;
    }
    float pr, pg, pb;
    VisualColor(enemy.polarity, pr, pg, pb);
    if (enemy.fxKind == 1)
    {
        // the dodge: a streak from where it was, a ring and a flash at the start, and the ship torn into its two colours
        const float life = static_cast<float>(enemy.fxTimer) / 36.0f;
        GameVfx::DrawLine(enemy.fxX, enemy.fxY, enemy.x, enemy.y, 0.020f * life + 0.004f, pr, pg, pb, 0.50f * life);
        GameVfx::DrawLine(enemy.fxX, enemy.fxY, enemy.x, enemy.y, 0.005f, 1.0f, 1.0f, 1.0f, 0.85f * life);
        GameVfx::DrawRound(Sprite::Ring, enemy.fxX, enemy.fxY, 0.10f + 0.26f * (1.0f - life), pr, pg, pb, 0.70f * life);
        GameVfx::DrawRound(Sprite::Glow, enemy.fxX, enemy.fxY, 0.04f + 0.14f * life, 1.0f, 1.0f, 1.0f, 0.60f * life);
        const int texture = GetEnemyTextureId(enemy.type);
        if (texture >= 0 && life > 0.05f)
        {
            float widthScale = 1.0f;
            float heightScale = 1.0f;
            GetEnemyVisualScale(enemy.type, widthScale, heightScale);
            const float width = enemy.halfWidth * widthScale * 2.0f;
            const float height = enemy.halfHeight * heightScale * 2.0f;
            const float dx = enemy.x - enemy.fxX;
            const float dy = (enemy.y - enemy.fxY) * ASPECT;
            const float length = std::sqrt(dx * dx + dy * dy);
            float nx = 0.0f;
            float ny = 1.0f;
            if (length > 0.001f)
            {
                nx = -dy / length;
                ny = dx / length;
            }
            const float offset = 0.045f * life;
            const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
            Renderer2D_DrawTextureEx(texture, enemy.x + nx * offset, enemy.y + ny * offset / ASPECT, width, height, 0.0f, 1.00f, 0.30f, 0.22f, 0.34f * life, 0.0f, 0.0f, 1.0f, 1.0f);
            Renderer2D_DrawTextureEx(texture, enemy.x - nx * offset, enemy.y - ny * offset / ASPECT, width, height, 0.0f, 0.25f, 0.62f, 1.00f, 0.34f * life, 0.0f, 0.0f, 1.0f, 1.0f);
            Renderer2D_SetBlendMode(previous);
        }
    }
    else if (enemy.fxKind == 2)
    {
        // the landing: a ring in its colour, a white ring inside it, a flash
        const float life = static_cast<float>(enemy.fxTimer) / 16.0f;
        const float k = 1.0f - life;
        GameVfx::DrawRound(Sprite::Ring, enemy.x, enemy.y, 0.10f + 0.30f * k, pr, pg, pb, 0.85f * life);
        GameVfx::DrawRound(Sprite::Ring, enemy.x, enemy.y, 0.06f + 0.18f * k, 1.0f, 1.0f, 1.0f, 0.55f * life);
        GameVfx::DrawRound(Sprite::Glow, enemy.x, enemy.y, 0.05f + 0.22f * life, pr, pg, pb, 0.55f * life);
    }
    else if (enemy.fxKind == 3)
    {
        // the second half of the duel: two shock rings, a white flash and spokes of light
        const float life = static_cast<float>(enemy.fxTimer) / 44.0f;
        const float k = 1.0f - life;
        GameVfx::DrawRound(Sprite::Ring, enemy.x, enemy.y, 0.14f + 0.80f * k, pr, pg, pb, 0.90f * life);
        GameVfx::DrawRound(Sprite::Ring, enemy.x, enemy.y, 0.08f + 0.50f * k, 1.0f, 1.0f, 1.0f, 0.65f * life);
        GameVfx::DrawRound(Sprite::Glow, enemy.x, enemy.y, 0.10f + 0.34f * life, 1.0f, 1.0f, 1.0f, 0.45f * life * life);
        for (int i = 0; i < 10; i++)
        {
            const float angle = PI * 2.0f * static_cast<float>(i) / 10.0f + 0.2f;
            const float d0 = 0.05f + 0.26f * k;
            const float d1 = d0 + 0.05f + 0.10f * life;
            GameVfx::DrawLine(enemy.x + std::cos(angle) * d0, enemy.y + std::sin(angle) * d0 / ASPECT, enemy.x + std::cos(angle) * d1,
                              enemy.y + std::sin(angle) * d1 / ASPECT, 0.006f, 1.0f, 1.0f, 1.0f, 0.80f * life);
        }
    }
}

void DrawRivalPhaseTwoEffect(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Rival || !IsRivalPhaseTwo(enemy))
    {
        return;
    }

    float r, g, b;
    VisualColor(enemy.polarity, r, g, b);

    const float pulse = 0.10f + 0.04f * std::sin(enemy.moveTime * 5.0f);

    GameVfx::DrawRound(Sprite::Glow, enemy.x, enemy.y, enemy.halfWidth * 3.2f, r, g, b, pulse);
}

void DrawHeavyAttackWarning(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Heavy || enemy.state != EnemyState::AttackWarning)
    {
        return;
    }

    DrawChargeVisual(enemy, GetWarningProgress(enemy), true);
}

void DrawTurretAttackWarning(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Turret || enemy.state != EnemyState::AttackWarning)
    {
        return;
    }

    DrawChargeVisual(enemy, GetWarningProgress(enemy), false);
}

void DrawChargerWarning(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Charger || enemy.state != EnemyState::ChargeWarning)
    {
        return;
    }

    const float p = GetWarningProgress(enemy);
    DrawChargeVisual(enemy, p, false);

    float r, g, b;
    VisualColor(enemy.polarity, r, g, b);

    DrawAimLine(enemy.x, enemy.y, enemy.targetX, enemy.targetY, 0.80f, p, r, g, b);
}
}

void GameEnemy::Draw(const Enemy enemies[], int enemyCount)
{
    ++g_VisualFrame;

    for (int i = 0; i < enemyCount; i++)
    {
        if (!enemies[i].active)
        {
            continue;
        }

        // drawn between the position of the last step and the position of this one (see GameTimer::Blend)
        Enemy view = enemies[i];
        view.x = GameTimer::Blend(view.prevX, view.x);
        view.y = GameTimer::Blend(view.prevY, view.y);
        const Enemy& enemy = view;

        DrawChargerWarning(enemy);
        DrawSineAttackWarning(enemy);
        DrawHeavyAttackWarning(enemy);
        DrawTurretAttackWarning(enemy);
        DrawSniperAttackWarning(enemy);
        DrawRivalAttackWarning(enemy);
        DrawRivalPhaseTwoEffect(enemy);
        DrawRivalEvadeWarning(enemy);
        DrawRivalPolaritySwitch(enemy);

        DrawEnemyBody(enemy, enemies[i].x - enemies[i].prevX, enemies[i].y - enemies[i].prevY);
        DrawEliteMarks(enemy);
        DrawRivalFx(enemy);
    }
    DrawRails();
    EnemyArt::DrawParticles();
    DrawMuzzleVisuals();
}

void GameEnemy::DrawBullets(const EnemyBullet enemyBullets[], int enemyBulletCount)
{
    static int frame = 0;
    frame++;

    for (int i = 0; i < enemyBulletCount; i++)
    {
        const EnemyBullet& bullet = enemyBullets[i];

        if (!bullet.active)
        {
            continue;
        }

        if (bullet.neutral)
        {
            GameVfx::DrawNeutralBullet(GameTimer::Blend(bullet.prevX, bullet.x), GameTimer::Blend(bullet.prevY, bullet.y), bullet.angle, frame, bullet.speed);
            continue;
        }

        const float bx = GameTimer::Blend(bullet.prevX, bullet.x);
        const float by = GameTimer::Blend(bullet.prevY, bullet.y);
        GameVfx::DrawEnemyBullet(bx, by, bullet.angle, bullet.polarity == Polarity::Red, bullet.isHoming, frame, bullet.speed, bullet.size, GameVfx::BulletNormal);
        if (bullet.fuse > 0)
        {
            // a mine: a ring closes in on it as the fuse burns, and blinks over the last 40 steps (the burst is the danger, not the mine)
            float r, g, b;
            VisualColor(bullet.polarity, r, g, b);
            const float burn = 1.0f - static_cast<float>(bullet.fuse) / static_cast<float>(MINE_FUSE);
            const bool blink = bullet.fuse > 40 || (bullet.fuse / 4) % 2 == 0;
            GameVfx::DrawRound(Sprite::Ring, bx, by, 0.150f - 0.090f * burn, r, g, b, (0.35f + 0.45f * burn) * (blink ? 1.0f : 0.35f));
        }
    }
}
