#include "game/GamePlayer.h"
#include "rogue/Modules.h"
#include "game/GamePlayerBullet.h"
#include "game/GameSpecialShot.h"
#include "core/GameInput.h"
#include "core/GameTimer.h"
#include "core/GameAudio.h"
#include "game/GameVfx.h"
#include "core/Renderer2D.h"
#include "core/AssetManager.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>

using GameVfx::Sprite;

namespace
{
constexpr float ASPECT = GameVfx::ASPECT;
constexpr float HORIZONTAL_SPEED_SCALE = ASPECT * 1.08f;
constexpr float VERTICAL_SPEED_SCALE = 1.00f;
// The hurt box: the core of the ship (half sizes, normalized units). The whole ship box absorbs bullets of its colour; only the core can be hurt.
// (There is no precision mode any more: its key is the TACTIC key, see rogue/ModulesEffects.cpp.)
constexpr float HURT_HALF_WIDTH = 0.034f; // (x 1.12, with the picture)
constexpr float HURT_HALF_HEIGHT = 0.045f;

constexpr float SCREEN_LEFT = -1.00f;
constexpr float SCREEN_RIGHT = 1.00f;
constexpr float SCREEN_TOP = 0.93f;
constexpr float SCREEN_BOTTOM = -0.93f;

constexpr float DRAW_HALF_WIDTH = 0.095f;
constexpr int SWITCH_DURATION = 20;
constexpr int SWITCH_LOCK_FRAMES = 18; // 0.3 s: mashing C can no longer replace reading the bullets
constexpr int DEATH_DURATION = 60;

int g_MuzzleFlash = 0; // frames left of the muzzle flash after a shot
int g_PolarityFlashTimer = 0;
int g_SwitchLock = 0;
float g_MoveInputX = 0.0f;
float g_MoveInputY = 0.0f;
float g_Tilt = 0.0f;
int g_VisualTimer = 0;
constexpr int INTRO_FRAMES = 42;
constexpr float INTRO_START_X = -1.30f; // outside the screen on the left
constexpr float INTRO_END_X = -0.75f;   // where GamePlayer::Initialize puts the ship
int g_IntroTimer = 0;
int g_IntroFrames = INTRO_FRAMES;
float g_IntroStartX = INTRO_START_X;
bool g_Hold = false;
float g_PrevX = 0.0f; // position at the start of the last step (render interpolation)
float g_PrevY = 0.0f;

int ShipTexture(bool isRed)
{
    return AssetManager::Get(isRed ? AssetManager::Tex::PlayerRed : AssetManager::Tex::PlayerBlue);
}

float Clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

float EaseOut(float t)
{
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

void ShipPoint(const Player& player, float u, float v, float& x, float& y)
{
    const float width = DRAW_HALF_WIDTH * 2.0f;
    const float height = width / ASPECT;
    x = player.x + (u - 0.5f) * width;
    y = player.y + (0.5f - v) * height;
}

void UpdatePolarityInput(Player& player)
{
    if (g_SwitchLock > 0)
    {
        --g_SwitchLock;
    }

    if (!GameInput::ActionTriggered(GameSettings::Action::Switch))
    {
        return;
    }

    if (g_SwitchLock > 0)
    {
        return; // locked: the key press is ignored
    }

    player.polarity = player.polarity == Polarity::Red ? Polarity::Blue : Polarity::Red;

    g_PolarityFlashTimer = SWITCH_DURATION;
    g_SwitchLock = SWITCH_LOCK_FRAMES;
    GameAudio::PlayPolaritySwitch(player.polarity == Polarity::Red); // (the polarity has just been switched: this is the new colour)
}

void UpdateMovement(Player& player)
{
    const float speed = player.speed * Mod::MoveSpeedScale(); // (the tactic CHARGE slows the ship while it charges)

    float moveX = 0.0f;
    float moveY = 0.0f;
    bool positionApplied = false;

    float stickX = 0.0f;
    float stickY = 0.0f;
    if (GameInput::GetStickMove(stickX, stickY))
    {
        // gamepad stick: analog, a small deflection is a slow move
        moveX = stickX;
        moveY = stickY;
    }
    else
    {
        using GameSettings::Action;
        if (GameInput::ActionDown(Action::MoveLeft) || GameInput::IsDown(VK_LEFT)) moveX -= 1.0f; // (the arrows always move the ship too)
        if (GameInput::ActionDown(Action::MoveRight) || GameInput::IsDown(VK_RIGHT)) moveX += 1.0f;
        if (GameInput::ActionDown(Action::MoveUp) || GameInput::IsDown(VK_UP)) moveY += 1.0f;
        if (GameInput::ActionDown(Action::MoveDown) || GameInput::IsDown(VK_DOWN)) moveY -= 1.0f;

        const float length = std::sqrt(moveX * moveX + moveY * moveY);
        if (length > 0.0001f)
        {
            moveX /= length;
            moveY /= length;
        }
        else if (GameInput::MouseSteering() && !GameInput::IsPointerCaptured())
        {
            // mouse: the ship flies towards the cursor with the same top speed as the keys (so the mouse has no advantage)
            const float maxPixels = speed * HORIZONTAL_SPEED_SCALE * 640.0f;
            const float dxPixels = (GameInput::GetMouseX() - player.x) * 640.0f;
            const float dyPixels = (GameInput::GetMouseY() - player.y) * 360.0f;
            const float distance = std::sqrt(dxPixels * dxPixels + dyPixels * dyPixels);
            if (distance > 0.75f && maxPixels > 0.0f)
            {
                const float step = std::min(maxPixels, distance * 0.5f); // slows down on arrival: no jitter around the cursor
                player.x += dxPixels / distance * step / 640.0f;
                player.y += dyPixels / distance * step / 360.0f;
                moveX = dxPixels / distance * step / maxPixels;
                moveY = dyPixels / distance * step / maxPixels;
                positionApplied = true;
            }
        }
    }

    g_MoveInputX = moveX;
    g_MoveInputY = moveY;

    if (!positionApplied)
    {
        player.x += moveX * speed * HORIZONTAL_SPEED_SCALE;
        player.y += moveY * speed * VERTICAL_SPEED_SCALE;
    }

    player.x = Clamp(player.x, SCREEN_LEFT + player.halfWidth, SCREEN_RIGHT - player.halfWidth);
    player.y = Clamp(player.y, SCREEN_BOTTOM + player.halfHeight, SCREEN_TOP - player.halfHeight);

    g_Tilt += (moveY * 0.08f - g_Tilt) * 0.18f;
}

void DrawEngines(const Player& player, float r, float g, float b, float alpha)
{
    const float time = static_cast<float>(g_VisualTimer);

    const float thrust = 1.0f + g_MoveInputX * 0.45f;

    for (int i = 0; i < 2; i++)
    {
        const float v = i == 0 ? 0.42f : 0.58f;
        float nx, ny;
        ShipPoint(player, 0.025f, v, nx, ny);

        const float flicker = 0.88f + 0.08f * std::sin(time * 1.9f + static_cast<float>(i) * 2.1f) + 0.04f * std::sin(time * 4.3f + static_cast<float>(i));
        const float length = 0.075f * thrust * flicker;

        GameVfx::Draw(Sprite::Flame, nx - length * 0.5f, ny, length, 0.034f, 0.0f, r, g, b, 0.80f * alpha);
        GameVfx::Draw(Sprite::Flame, nx - length * 0.28f, ny, length * 0.56f, 0.013f, 0.0f, 1.0f, 1.0f, 1.0f, 0.85f * alpha);
        GameVfx::DrawRound(Sprite::Glow, nx, ny, 0.030f, r, g, b, 0.50f * alpha);
    }
}

// Shot flash at the nose: a bloom, a hot core, a flare along the shot and two short sparks to the sides.
void DrawMuzzleFlash(const Player& player, float r, float g, float b, float alpha)
{
    if (g_MuzzleFlash <= 0)
    {
        return;
    }
    const float life = static_cast<float>(g_MuzzleFlash) / 5.0f;
    float x, y;
    ShipPoint(player, 0.97f, 0.5f, x, y);
    GameVfx::DrawRound(Sprite::Glow, x, y, 0.040f + 0.085f * life, r, g, b, 0.80f * life * alpha);
    GameVfx::DrawRound(Sprite::Glow, x + 0.008f, y, 0.014f + 0.030f * life, 1.0f, 1.0f, 1.0f, life * alpha);
    GameVfx::Draw(Sprite::Spark, x + 0.040f * life + 0.012f, y, 0.050f + 0.120f * life, 0.020f, 0.0f, 1.0f, 0.96f, 0.90f, life * alpha);
    GameVfx::Draw(Sprite::Spark, x + 0.030f, y, 0.040f + 0.090f * life, 0.030f, 0.0f, r, g, b, 0.80f * life * alpha);
    for (int side = -1; side <= 1; side += 2)
    {
        const float a = 0.55f * static_cast<float>(side);
        GameVfx::Draw(Sprite::Spark, x + std::cos(a) * 0.022f * (2.0f - life), y + std::sin(a) * 0.022f * (2.0f - life) / ASPECT,
                      0.030f * life + 0.008f, 0.014f, a, r, g, b, 0.85f * life * alpha);
    }
}

void DrawCore(const Player& player, float r, float g, float b, float alpha)
{
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(g_VisualTimer) * 0.08f);
    float x, y;
    ShipPoint(player, 0.44f, 0.5f, x, y);

    GameVfx::DrawRound(Sprite::Glow, x, y, 0.034f, r, g, b, (0.35f + 0.20f * pulse) * alpha);
    GameVfx::DrawRound(Sprite::Glow, x, y, 0.010f, 1.0f, 1.0f, 1.0f, 0.85f * alpha);
}

void DrawSwitch(const Player& player, float r, float g, float b, float alpha)
{
    if (g_PolarityFlashTimer <= 0)
    {
        return;
    }

    const float remain = static_cast<float>(g_PolarityFlashTimer) / SWITCH_DURATION;
    const float progress = 1.0f - remain;
    const float eased = EaseOut(progress);
    float x, y;
    ShipPoint(player, 0.44f, 0.5f, x, y);

    GameVfx::DrawRound(Sprite::Glow, x, y, 0.22f * remain + 0.04f, r, g, b, remain * remain * 0.70f * alpha);
    GameVfx::DrawRound(Sprite::Ring, x, y, 0.05f + eased * 0.26f, r, g, b, remain * 0.90f * alpha);

    if (progress > 0.20f)
    {
        const float p = (progress - 0.20f) / 0.80f;
        GameVfx::DrawRound(Sprite::Ring, x, y, 0.04f + EaseOut(p) * 0.15f, 1.0f, 1.0f, 1.0f, (1.0f - p) * 0.45f * alpha);
    }

    constexpr int SPARK_COUNT = 8;
    for (int i = 0; i < SPARK_COUNT; i++)
    {
        const float angle = 6.2831853f * static_cast<float>(i) / SPARK_COUNT + 0.39f;
        const float distance = 0.05f + eased * 0.13f;

        GameVfx::Draw(Sprite::Spark, x + std::cos(angle) * distance, y + std::sin(angle) * distance / ASPECT,
                      0.06f * remain + 0.01f, 0.018f, angle, r, g, b, remain * alpha);
    }
}

// thin ring around the ship that runs down while the colour switch is locked
void DrawSwitchLock(const Player& player, float alpha)
{
    if (g_SwitchLock <= 0)
    {
        return;
    }

    constexpr int SEGMENTS = 24;
    constexpr float RADIUS = 0.082f;
    const int lit = static_cast<int>(std::ceil(GamePlayer::GetSwitchLockRate() * static_cast<float>(SEGMENTS)));

    for (int i = 0; i < lit; ++i)
    {
        const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(SEGMENTS) - 1.5707963f;
        const float a1 = 6.2831853f * (static_cast<float>(i) + 0.62f) / static_cast<float>(SEGMENTS) - 1.5707963f;

        GameVfx::DrawLine(player.x + std::cos(a0) * RADIUS, player.y + std::sin(a0) * RADIUS / ASPECT,
                          player.x + std::cos(a1) * RADIUS, player.y + std::sin(a1) * RADIUS / ASPECT, 0.0034f, 1.0f, 1.0f, 1.0f,
                          0.70f * alpha);
    }
}

// the core of the ship (what can be hurt): a small mark of its colour in the middle of the ship, always on
void DrawFocusMarker(const Player& player, bool isRed, float alpha)
{
    float u0, v0, u1, v1;
    AssetManager::BulletUv(isRed ? AssetManager::MarkerRed : AssetManager::MarkerBlue, u0, v0, u1, v1);

    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    Renderer2D_DrawTextureEx(AssetManager::Get(AssetManager::Tex::Bullets), player.x, player.y, 0.047f, 0.047f / ASPECT, 0.0f,
                             1.0f, 1.0f, 1.0f, 0.85f * alpha, u0, v0, u1, v1);
}

void DrawDeath(const Player& player, int deathTimer)
{
    if (deathTimer <= 0)
    {
        return;
    }

    const float progress = 1.0f - static_cast<float>(deathTimer) / DEATH_DURATION;
    const bool isRed = player.polarity == Polarity::Red;

    float r, g, b;
    GameVfx::GetPolarityColor(isRed, r, g, b);

    const float x = player.x;
    const float y = player.y;

    if (progress < 0.12f)
    {
        const int texture = ShipTexture(isRed);
        const float flash = progress / 0.12f;

        if (texture >= 0)
        {
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
            Renderer2D_DrawTextureEx(texture, x, y, DRAW_HALF_WIDTH * 2.0f, DRAW_HALF_WIDTH * 2.0f / ASPECT, g_Tilt, 1.0f, 1.0f,
                                     1.0f, 1.0f - flash, 0.0f, 0.0f, 1.0f, 1.0f);

            Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
            Renderer2D_DrawTextureEx(texture, x, y, DRAW_HALF_WIDTH * 2.0f, DRAW_HALF_WIDTH * 2.0f / ASPECT, g_Tilt, 1.0f, 1.0f,
                                     1.0f, flash, 0.0f, 0.0f, 1.0f, 1.0f);
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
        }
    }

    if (progress < 0.35f)
    {
        const float flash = 1.0f - progress / 0.35f;
        GameVfx::DrawRound(Sprite::Glow, x, y, 0.55f * flash + 0.10f, 1.0f, 1.0f, 1.0f, flash * 0.9f);
        GameVfx::DrawRound(Sprite::Glow, x, y, 0.80f * flash, r, g, b, flash * 0.8f);
    }

    const float life = 1.0f - progress;
    GameVfx::DrawRound(Sprite::Glow, x, y, 0.20f + progress * 0.10f, 1.0f, 0.55f, 0.20f, life * life * 0.70f);

    GameVfx::DrawRound(Sprite::Ring, x, y, 0.08f + EaseOut(progress) * 0.90f, r, g, b, life * 0.90f);

    if (progress > 0.08f)
    {
        const float p = (progress - 0.08f) / 0.92f;
        GameVfx::DrawRound(Sprite::Ring, x, y, 0.05f + EaseOut(p) * 0.55f, 1.0f, 1.0f, 1.0f, (1.0f - p) * 0.60f);
    }

    constexpr int SPARK_COUNT = 20;
    for (int i = 0; i < SPARK_COUNT; i++)
    {
        const float random = GameVfx::Hash(i * 7 + 11);
        const float angle = 6.2831853f * (static_cast<float>(i) + random * 0.7f) / SPARK_COUNT;
        const float speed = 0.5f + random;
        const float distance = 0.03f + EaseOut(progress) * 0.38f * speed;
        const float length = 0.10f * life * speed + 0.01f;
        const bool white = i % 3 == 0;

        GameVfx::Draw(Sprite::Spark, x + std::cos(angle) * distance, y + std::sin(angle) * distance / ASPECT, length, 0.022f,
                      angle, white ? 1.0f : r, white ? 1.0f : g, white ? 1.0f : b, life);
    }

    constexpr int EMBER_COUNT = 10;
    for (int i = 0; i < EMBER_COUNT; i++)
    {
        const float random = GameVfx::Hash(i * 13 + 57);
        const float angle = 6.2831853f * random;
        const float distance = EaseOut(progress) * (0.10f + random * 0.20f);

        GameVfx::DrawRound(Sprite::Glow, x + std::cos(angle) * distance,
                           y + std::sin(angle) * distance / ASPECT - progress * progress * 0.08f, 0.030f * life + 0.006f, 1.0f,
                           0.60f, 0.25f, life);
    }
}
}

void GamePlayer::Initialize(Player& player)
{
    g_IntroTimer = 0;
    player.x = INTRO_END_X;
    player.y = 0.0f;
    player.halfWidth = 0.067f; // the box that absorbs bullets of its colour (x 1.12, with the picture)
    player.halfHeight = 0.090f;
    player.speed = 0.018f;
    player.polarity = Polarity::Blue;

    g_PolarityFlashTimer = 0;
    g_SwitchLock = 0;
    g_MoveInputX = 0.0f;
    g_Tilt = 0.0f;
    g_VisualTimer = 0;
    g_MuzzleFlash = 0;
    g_PrevX = player.x;
    g_PrevY = player.y;
}

float GamePlayer::DrawWidth()
{
    return DRAW_HALF_WIDTH * 2.0f;
}

void GamePlayer::BeginIntro(Player& player, bool matchCut)
{
    g_IntroFrames = matchCut ? 34 : INTRO_FRAMES;
    g_IntroStartX = matchCut ? -1.17f : INTRO_START_X; // (the edge of the picture is at -1, the ship is 0.19 wide)
    g_IntroTimer = g_IntroFrames;
    player.x = g_IntroStartX;
    player.y = 0.0f;
    g_PrevX = player.x;
    g_PrevY = player.y;
    g_MoveInputX = 2.0f;
}

bool GamePlayer::IsIntro()
{
    return g_IntroTimer > 0;
}

void GamePlayer::SetHold(bool hold)
{
    g_Hold = hold;
}

bool GamePlayer::IsHeld()
{
    return g_Hold;
}

void GamePlayer::BeginStep(const Player& player)
{
    g_PrevX = player.x;
    g_PrevY = player.y;
}

float GamePlayer::GetRenderX(const Player& player)
{
    return GameTimer::Blend(g_PrevX, player.x);
}

float GamePlayer::GetRenderY(const Player& player)
{
    return GameTimer::Blend(g_PrevY, player.y);
}

void GamePlayer::Update(Player& player, int& shotCoolTime)
{
    g_VisualTimer++;
    if (g_MuzzleFlash > 0)
    {
        --g_MuzzleFlash;
    }
    if (g_IntroTimer > 0)
    {
        // the entrance: the ship decelerates from the edge to its place, the engines burn long. No input until it has arrived.
        --g_IntroTimer;
        const float progress = 1.0f - static_cast<float>(g_IntroTimer) / static_cast<float>(g_IntroFrames);
        player.x = g_IntroStartX + (INTRO_END_X - g_IntroStartX) * EaseOut(progress);
        player.y = 0.0f;
        g_MoveInputX = 2.0f * (1.0f - progress);
        g_Tilt *= 0.9f;
            return;
    }
    if (g_Hold)
    {
        // the boss arrives: the ship glides back to its place (the same as after its entrance) and waits, the engines idle
        player.x += (INTRO_END_X - player.x) * 0.06f;
        player.y += (0.0f - player.y) * 0.06f;
        g_MoveInputX *= 0.9f;
        g_Tilt *= 0.9f;
        if (g_PolarityFlashTimer > 0) g_PolarityFlashTimer--;
        if (shotCoolTime > 0) shotCoolTime--;
        return;
    }
    UpdatePolarityInput(player);

    if (g_PolarityFlashTimer > 0)
    {
        g_PolarityFlashTimer--;
    }

    UpdateMovement(player);

    const bool shootInput = GameInput::ActionDown(GameSettings::Action::Shoot);

    if (shootInput && shotCoolTime <= 0)
    {
        if (GamePlayerBullet::Spawn(player.x + player.halfWidth + 0.03f, player.y, player.polarity))
        {
            shotCoolTime = 10;
            g_MuzzleFlash = 5;
            GameAudio::PlayPlayerShot(player.polarity == Polarity::Red); // (the shot has the voice of the colour of the ship)
        }
    }

    if (shotCoolTime > 0)
    {
        shotCoolTime--;
    }
}

void GamePlayer::Draw(const Player& actualPlayer, int playerHp, int playerDeathTimer, int invincibleTimer)
{
    Player player = actualPlayer; // drawn between the position of the last step and the new one
    player.x = GetRenderX(actualPlayer);
    player.y = GetRenderY(actualPlayer);
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();

    if (playerHp <= 0)
    {
        DrawDeath(player, playerDeathTimer);
        Renderer2D_SetBlendMode(previous);
        return;
    }

    const float drawHalfHeight = DRAW_HALF_WIDTH / ASPECT;
    const bool isRed = player.polarity == Polarity::Red;
    const int texture = ShipTexture(isRed);

    float r, g, b;
    GameVfx::GetPolarityColor(isRed, r, g, b);

    float alpha = 1.0f;
    if (invincibleTimer > 0)
    {
        alpha = ((invincibleTimer / 4) % 2 == 0) ? 0.40f : 0.92f;
    }

    DrawEngines(player, r, g, b, alpha);

    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);

    if (texture >= 0)
    {
        Renderer2D_DrawTextureEx(texture, player.x, player.y, DRAW_HALF_WIDTH * 2.0f, drawHalfHeight * 2.0f, g_Tilt, 1.0f, 1.0f,
                                 1.0f, alpha, 0.0f, 0.0f, 1.0f, 1.0f);

        if (g_PolarityFlashTimer > 0)
        {
            const float wash = static_cast<float>(g_PolarityFlashTimer) / SWITCH_DURATION;
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
            Renderer2D_DrawTextureEx(texture, player.x, player.y, DRAW_HALF_WIDTH * 2.0f, drawHalfHeight * 2.0f, g_Tilt, r, g, b,
                                     wash * wash * 0.8f * alpha, 0.0f, 0.0f, 1.0f, 1.0f);
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
        }
    }

    DrawCore(player, r, g, b, alpha);
    DrawMuzzleFlash(player, r, g, b, alpha);
    DrawSwitch(player, r, g, b, alpha);
    DrawSwitchLock(player, alpha);

    DrawFocusMarker(player, isRed, alpha); // the core that can be hurt

    Renderer2D_SetBlendMode(previous);
}

float GamePlayer::GetHurtHalfWidth(const Player& player)
{
    (void)player;
    return HURT_HALF_WIDTH;
}

float GamePlayer::GetHurtHalfHeight(const Player& player)
{
    (void)player;
    return HURT_HALF_HEIGHT;
}

void GamePlayer::GetMoveInput(float& x, float& y)
{
    x = g_MoveInputX;
    y = g_MoveInputY;
}

void GamePlayer::ClampToScreen(Player& player)
{
    player.x = Clamp(player.x, SCREEN_LEFT + player.halfWidth, SCREEN_RIGHT - player.halfWidth);
    player.y = Clamp(player.y, SCREEN_BOTTOM + player.halfHeight, SCREEN_TOP - player.halfHeight);
}

float GamePlayer::GetSwitchLockRate()
{
    return static_cast<float>(g_SwitchLock) / static_cast<float>(SWITCH_LOCK_FRAMES);
}

void GamePlayer::DrawUnder(const Player& actualPlayer, int playerHp, int invincibleTimer)
{
    Player player = actualPlayer;
    player.x = GetRenderX(actualPlayer);
    player.y = GetRenderY(actualPlayer);
    if (playerHp <= 0)
    {
        return;
    }

    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    const bool isRed = player.polarity == Polarity::Red;
    float r, g, b;
    GameVfx::GetPolarityColor(isRed, r, g, b);

    float alpha = 1.0f;

    if (invincibleTimer > 0)
    {
        alpha = ((invincibleTimer / 4) % 2 == 0) ? 0.40f : 0.92f;
    }

    // dark halo: the background behind the ship is pulled down, so the bright ship stands out of busy bullet fields
    GameVfx::DrawAlpha(Sprite::Glow, player.x, player.y, 0.30f, 0.30f / ASPECT, 0.0f, 0.0f, 0.0f, 0.0f, 0.80f * alpha);
    GameVfx::DrawAlpha(Sprite::Glow, player.x, player.y, 0.18f, 0.18f / ASPECT, 0.0f, 0.0f, 0.0f, 0.0f, 0.60f * alpha);

    // polarity rim light: the ship texture once more, a little larger, additive and tinted
    const int texture = ShipTexture(isRed);

    if (texture >= 0)
    {
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
        Renderer2D_DrawTextureEx(texture, player.x, player.y, DRAW_HALF_WIDTH * 2.0f * 1.10f,
                                 DRAW_HALF_WIDTH / ASPECT * 2.0f * 1.10f, g_Tilt, r, g, b, 0.34f * alpha, 0.0f, 0.0f, 1.0f, 1.0f);
    }

    // the polarity ring: a hairline of the colour of the ship, hard-edged (it used to be a soft glow). It is a solid line on purpose: the dashed
    // ellipse is the net of SIPHON, and the two must not be mistaken for each other.
    {
        constexpr int SEG = 40;
        constexpr float R = 0.1025f;
        for (int i = 0; i < SEG; i++)
        {
            const float a0 = 6.2831853f * static_cast<float>(i) / SEG, a1 = 6.2831853f * static_cast<float>(i + 1) / SEG;
            GameVfx::DrawBar(player.x + std::cos(a0) * R, player.y + std::sin(a0) * R / ASPECT, player.x + std::cos(a1) * R, player.y + std::sin(a1) * R / ASPECT, 0.0016f, r, g,
                             b, 0.26f * alpha);
        }
    }
    Renderer2D_SetBlendMode(previous);
}








