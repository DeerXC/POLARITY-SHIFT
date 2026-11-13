#include "game/GameSpecialShot.h"
#include "core/GameInput.h"
#include "core/GameAudio.h"
#include "game/BeamFx.h"
#include "game/GamePlayer.h"
#include "game/GameVfx.h"
#include "rogue/Modules.h"

#include <Windows.h>
#include <cmath>

using GameVfx::Sprite;

namespace
{
constexpr float ASPECT = GameVfx::ASPECT;

constexpr int SHOT_DURATION = 72;
constexpr int DAMAGE_INTERVAL = 6;
constexpr int EXTEND_FRAMES = 6;
constexpr int FADE_FRAMES = 16;

constexpr float SHOT_START_OFFSET = 0.08f;
constexpr float SHOT_END_X = 1.10f;
constexpr float SHOT_HALF_HEIGHT = 0.10f;
// the zone that the beam is drawn for, in equal-scale units (the two rails stand 0.095 high: the width of the zone as the player has always read it)
constexpr float SHOT_ZONE_HALF = 0.095f * ASPECT;

bool g_IsActive = false;
int g_ShotDuration = SHOT_DURATION; // SHOT_DURATION, longer with the talent LONG PIERCE (set when the beam is fired)
int g_ActiveTimer = 0;
int g_DamageTimer = 0;
float g_StartX = 0.0f;
float g_CenterY = 0.0f;
float g_DrawCenterY = 0.0f; // where the beam is drawn (the interpolated position of the ship)
Polarity g_ShotPolarity = Polarity::Blue;

float EaseOut(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}
}

void GameSpecialShot::Initialize()
{
    g_IsActive = false;
    g_ActiveTimer = 0;
    g_DamageTimer = 0;
    g_StartX = 0.0f;
    g_CenterY = 0.0f;
    g_ShotPolarity = Polarity::Blue;
}

bool GameSpecialShot::TryActivate(const Player& player, int& energy, int maxEnergy)
{
    (void)maxEnergy; // (what the beam needs is Mod::EnergyMax(); Mod::BeginBeam decides between a full beam and a half one of TWIN)
    if (g_IsActive)
    {
        return false;
    }

    if (!GameInput::ActionTriggered(GameSettings::Action::Special))
    {
        return false;
    }

    if (!Mod::BeginBeam(energy))
    {
        return false;
    }

    g_IsActive = true;
    g_ShotDuration = static_cast<int>(std::lround(static_cast<float>(SHOT_DURATION) * Mod::BeamDurationScale()));
    g_ActiveTimer = g_ShotDuration;
    g_DamageTimer = 0;
    g_ShotPolarity = player.polarity;
    g_StartX = player.x + player.halfWidth + SHOT_START_OFFSET;
    g_CenterY = player.y;

    GameAudio::Play(GameSound::SpecialShot);
    return true;
}

bool GameSpecialShot::Update(const Player& player)
{
    if (!g_IsActive)
    {
        return false;
    }

    g_StartX = player.x + player.halfWidth + SHOT_START_OFFSET;
    g_CenterY = player.y;

    bool shouldApplyDamage = false;
    if (g_DamageTimer <= 0)
    {
        shouldApplyDamage = true;
        g_DamageTimer = DAMAGE_INTERVAL;
    }

    g_DamageTimer--;
    g_ActiveTimer--;

    if (g_ActiveTimer <= 0)
    {
        g_ActiveTimer = 0;
        g_DamageTimer = 0;
        g_IsActive = false;
    }

    return shouldApplyDamage;
}

void GameSpecialShot::Draw(const Player& player)
{
    const float drawStartX = GamePlayer::GetRenderX(player) + player.halfWidth + SHOT_START_OFFSET;
    g_DrawCenterY = GamePlayer::GetRenderY(player);

    if (!g_IsActive)
    {
        return;
    }

    const int elapsed = g_ShotDuration - g_ActiveTimer;
    const float time = static_cast<float>(elapsed);

    // The piercing beam is WHITE: the colour of what is outside the polarity. A white bullet cannot be absorbed, the white beam cannot be stopped: it goes through armour (and
    // it does not protect the ship: it can be hurt while it fires). The body is white; the two strands that braid round its core are the colour of the ship and its mirror.
    const BeamFx::Palette palette = BeamFx::PiercingPalette(g_ShotPolarity == Polarity::Red);

    const float grown = elapsed < EXTEND_FRAMES ? EaseOut(static_cast<float>(elapsed) / EXTEND_FRAMES) : 1.0f;
    float width = 1.0f + 0.25f * std::exp(-time * 0.18f); // a swell at the start, and the zone settles
    float envelope = 1.0f;
    if (g_ActiveTimer < FADE_FRAMES)
    {
        const float fade = static_cast<float>(g_ActiveTimer) / FADE_FRAMES;
        width *= fade * fade;
        envelope = fade;
    }
    width *= 1.0f + 0.04f * std::sin(time * 1.1f) + 0.03f * std::sin(time * 2.9f);

    const float y = g_DrawCenterY;
    BeamFx::BeamParams beam;
    beam.x0 = drawStartX;
    beam.y0 = y;
    beam.x1 = SHOT_END_X;
    beam.y1 = y;
    beam.halfWidth = SHOT_ZONE_HALF * width;
    beam.time = time;
    beam.envelope = envelope;
    beam.grown = grown;
    beam.turbulence = 0.85f;
    beam.pulseSpeed = 1.4f;
    beam.seed = 3;
    BeamFx::DrawBeam(beam, palette);

    const float muzzleX = drawStartX - 0.01f;
    BeamFx::DrawMuzzle(muzzleX, y, 0.11f * width, time, envelope, palette);

    if (elapsed < EXTEND_FRAMES + 3) // the tip, while the beam is still growing
    {
        const float tipX = drawStartX + (SHOT_END_X - drawStartX) * grown;
        GameVfx::DrawRound(Sprite::Glow, tipX, y, 0.22f, palette.aura.r, palette.aura.g, palette.aura.b, 0.9f);
        GameVfx::DrawRound(Sprite::Glow, tipX, y, 0.08f, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (elapsed < 10) // the shot itself: a flash and a ring that leaves the muzzle
    {
        const float flash = 1.0f - time / 10.0f;
        GameVfx::DrawRound(Sprite::Glow, muzzleX, y, 0.9f * flash, 1.0f, 1.0f, 1.0f, flash * 0.55f);
        GameVfx::DrawRound(Sprite::Ring, muzzleX, y, 0.12f + EaseOut(1.0f - flash) * 0.70f, palette.aura.r, palette.aura.g, palette.aura.b, flash);
    }
}

bool GameSpecialShot::IsActive()
{
    return g_IsActive;
}

float GameSpecialShot::GetStartX()
{
    return g_StartX;
}

float GameSpecialShot::GetEndX()
{
    return SHOT_END_X;
}

float GameSpecialShot::GetCenterY()
{
    return g_CenterY;
}

float GameSpecialShot::GetHalfHeight()
{
    return SHOT_HALF_HEIGHT;
}




