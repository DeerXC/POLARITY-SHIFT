#include "game/scene/HowToTraining.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/GameSettings.h"
#include "core/Loc.h"
#include "ui/DemoKit.h"
#include "ui/GameMenuUi.h"
#include "ui/UiKit.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace HowToTraining
{
namespace
{
namespace D = DemoKit;

constexpr int STEP_MOVE = 0, STEP_SWITCH = 1, STEP_ABSORB = 2, STEP_BEAM = 3, STEP_DODGE = 4, STEP_DONE = 5;
constexpr int STEP_COUNT = 5;
constexpr float SHIP_SPEED = 3.4f;
constexpr float MOVE_GOAL = 240.0f;   // px of flying
constexpr int SWITCH_GOAL = 2;
constexpr int ABSORB_GOAL = 6;
constexpr int DODGE_GOAL = 4;
constexpr float ABSORB_R = 26.0f;     // the whole ship absorbs bullets of its colour
constexpr float HURT_R = 9.0f;        // only its core can be hurt
constexpr int BEAM_FRAMES = 46;
constexpr int BULLET_MAX = 24;

struct Bullet
{
    bool active = false;
    float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
    int kind = 0; // 0 blue, 1 red, 2 white
};

int g_Step = STEP_MOVE;
int g_StepT = 0;
int g_Frame = 0;
float g_ShipX = 0.0f, g_ShipY = 0.0f;
bool g_Red = false;
float g_Moved = 0.0f;
int g_Switches = 0;
int g_Absorbed = 0;
int g_Dodged = 0;
float g_Energy = 0.0f;
float g_EnergyShown = 0.0f;
float g_Hp = 100.0f;
int g_HitFlash = 0;
int g_AbsorbFlash = 0;
int g_RefuseFlash = 0; // the beam key pressed with a bar that is not full
int g_BeamT = 0;       // > 0 while the beam is on
bool g_ShieldUp = true;
int g_DummyFlash = 0;
float g_DummyY = 0.0f; // the dummy of the beam step stays in the lane of the ship
int g_SpawnT = 0;
int g_StepDoneFlash = 0;
unsigned g_Seed = 12345u;
Bullet g_Bullets[BULLET_MAX];

float Rand01()
{
    g_Seed = g_Seed * 1664525u + 1013904223u;
    return static_cast<float>((g_Seed >> 8) & 0xFFFF) / 65535.0f;
}

float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void ClearBullets()
{
    for (Bullet& b : g_Bullets) b.active = false;
}

void Spawn(int kind, float aimY, float speed)
{
    for (Bullet& b : g_Bullets)
    {
        if (b.active) continue;
        b.active = true;
        b.kind = kind;
        b.x = X + W - 14.0f;
        b.y = Clamp(aimY, Y + 24.0f, Y + H - 24.0f);
        b.vx = -speed;
        b.vy = 0.0f;
        return;
    }
}

void GoTo(int step)
{
    g_Step = step;
    g_StepT = 0;
    g_SpawnT = 0;
    g_StepDoneFlash = 24;
    ClearBullets();
    if (step == STEP_ABSORB)
    {
        g_Absorbed = 0;
        g_Energy = 0.0f;
    }
    if (step == STEP_BEAM)
    {
        g_Energy = 100.0f;
        g_ShieldUp = true;
        g_BeamT = 0;
    }
    if (step == STEP_DODGE)
    {
        g_Dodged = 0;
        g_Energy = 0.0f;
    }
    if (step != STEP_DONE) GameAudio::Play(GameSound::MenuApply);
    else GameAudio::Play(GameSound::EnergyFull);
}

// the move: the keys, the pad, the cursor - the same as in the game (the stage is small: the cursor works inside it)
void MoveShip()
{
    using GameSettings::Action;
    float mx = 0.0f, my = 0.0f;
    float sx = 0.0f, sy = 0.0f;
    if (GameInput::GetStickMove(sx, sy))
    {
        mx = sx;
        my = -sy;
    }
    else
    {
        if (GameInput::ActionDown(Action::MoveLeft)) mx -= 1.0f;
        if (GameInput::ActionDown(Action::MoveRight)) mx += 1.0f;
        if (GameInput::ActionDown(Action::MoveUp)) my -= 1.0f;
        if (GameInput::ActionDown(Action::MoveDown)) my += 1.0f;
        const float len = std::sqrt(mx * mx + my * my);
        if (len > 1.0f)
        {
            mx /= len;
            my /= len;
        }
        else if (len == 0.0f && GameInput::LastDevice() == GameInput::Device::Mouse && GameInput::IsMouseInsideWindow() && GameSettings::Get().mouseSteering)
        {
            const float tx = (GameInput::GetMouseX() + 1.0f) * 640.0f, ty = (1.0f - GameInput::GetMouseY()) * 360.0f;
            if (tx >= X && tx <= X + W && ty >= Y && ty <= Y + H)
            {
                const float dx = tx - g_ShipX, dy = ty - g_ShipY, d = std::sqrt(dx * dx + dy * dy);
                if (d > 1.5f)
                {
                    const float step = std::min(SHIP_SPEED, d * 0.5f);
                    mx = dx / d * step / SHIP_SPEED;
                    my = dy / d * step / SHIP_SPEED;
                }
            }
        }
    }
    const float ox = g_ShipX, oy = g_ShipY;
    g_ShipX = Clamp(g_ShipX + mx * SHIP_SPEED, X + 24.0f, X + W * 0.56f);
    g_ShipY = Clamp(g_ShipY + my * SHIP_SPEED, Y + 28.0f, Y + H - 28.0f);
    g_Moved += std::fabs(g_ShipX - ox) + std::fabs(g_ShipY - oy);
}

void HitShip(int damage)
{
    g_Hp = std::max(20.0f, g_Hp - static_cast<float>(damage)); // (a training: the ship is never lost)
    g_HitFlash = 24;
    GameAudio::Play(GameSound::MenuError);
    GameInput::Rumble(0.5f, 14);
}

void UpdateBullets()
{
    for (Bullet& b : g_Bullets)
    {
        if (!b.active) continue;
        b.x += b.vx;
        b.y += b.vy;
        const float dx = b.x - g_ShipX, dy = b.y - g_ShipY;
        const float d = std::sqrt(dx * dx + dy * dy);
        const bool white = b.kind == 2;
        const bool same = !white && ((b.kind == 1) == g_Red);
        if (same && d < ABSORB_R)
        {
            b.active = false;
            g_AbsorbFlash = 12;
            GameAudio::PlayAbsorb(g_Red);
            if (g_Step == STEP_ABSORB)
            {
                ++g_Absorbed;
                g_Energy = std::min(100.0f, static_cast<float>(g_Absorbed) * 100.0f / static_cast<float>(ABSORB_GOAL));
            }
            continue;
        }
        if (d < HURT_R + 3.0f)
        {
            b.active = false;
            HitShip(white ? 18 : 12);
            if (g_Step == STEP_DODGE) g_Dodged = std::max(0, g_Dodged - 1); // a hit takes back one dodge
            continue;
        }
        if (b.x < X + 6.0f)
        {
            b.active = false;
            if (g_Step == STEP_DODGE && white) ++g_Dodged;
        }
    }
}

bool BeamPressed()
{
    return GameInput::ActionTriggered(GameSettings::Action::Special);
}
}

void Reset()
{
    g_Step = STEP_MOVE;
    g_StepT = 0;
    g_Frame = 0;
    g_ShipX = X + 90.0f;
    g_ShipY = Y + H * 0.5f;
    g_Red = false;
    g_Moved = 0.0f;
    g_Switches = 0;
    g_Absorbed = 0;
    g_Dodged = 0;
    g_Energy = 0.0f;
    g_EnergyShown = 0.0f;
    g_Hp = 100.0f;
    g_HitFlash = g_AbsorbFlash = g_RefuseFlash = g_BeamT = g_DummyFlash = g_SpawnT = 0;
    g_StepDoneFlash = 0;
    g_ShieldUp = true;
    g_DummyY = Y + H * 0.5f;
    g_Seed = 12345u;
    ClearBullets();
}

bool Done()
{
    return g_Step == STEP_DONE;
}

int Step()
{
    return g_Step;
}

void Update()
{
    ++g_Frame;
    ++g_StepT;
    if (g_HitFlash > 0) --g_HitFlash;
    if (g_AbsorbFlash > 0) --g_AbsorbFlash;
    if (g_RefuseFlash > 0) --g_RefuseFlash;
    if (g_DummyFlash > 0) --g_DummyFlash;
    if (g_StepDoneFlash > 0) --g_StepDoneFlash;
    g_Hp = std::min(100.0f, g_Hp + 0.12f);
    g_EnergyShown += (g_Energy - g_EnergyShown) * 0.2f;

    if (g_Step == STEP_DONE)
    {
        MoveShip();
        if (GameInput::ActionTriggered(GameSettings::Action::Switch)) g_Red = !g_Red;
        if (GameInput::IsTriggered(VK_RETURN)) Reset();
        return;
    }

    MoveShip();
    if (GameInput::ActionTriggered(GameSettings::Action::Switch))
    {
        g_Red = !g_Red;
        GameAudio::PlayPolaritySwitch(g_Red); // (g_Red has just been switched: the new colour)
        if (g_Step == STEP_SWITCH) ++g_Switches;
    }

    switch (g_Step)
    {
    case STEP_MOVE:
        if (g_Moved >= MOVE_GOAL) GoTo(STEP_SWITCH);
        break;
    case STEP_SWITCH:
        if (g_Switches >= SWITCH_GOAL) GoTo(STEP_ABSORB);
        break;
    case STEP_ABSORB:
    {
        // a bullet every 50 steps, aimed at the lane of the ship; two colours, so the colour of the ship has to follow
        if (++g_SpawnT >= 50)
        {
            g_SpawnT = 0;
            const int kind = Rand01() < 0.5f ? 0 : 1;
            Spawn(kind, g_ShipY + (Rand01() - 0.5f) * 24.0f, 2.1f);
        }
        UpdateBullets();
        if (g_Absorbed >= ABSORB_GOAL) GoTo(STEP_BEAM);
        break;
    }
    case STEP_BEAM:
    {
        if (g_BeamT == 0) g_DummyY += (g_ShipY - g_DummyY) * 0.06f;
        if (g_BeamT > 0)
        {
            --g_BeamT;
            if (g_BeamT == BEAM_FRAMES - 16 && g_ShieldUp)
            {
                g_ShieldUp = false; // the beam breaks the shield for good
                g_DummyFlash = 16;
                GameAudio::Play(GameSound::EnemyExplosion);
            }
            if (g_BeamT == 0) GoTo(STEP_DODGE);
        }
        else if (BeamPressed())
        {
            if (g_Energy >= 100.0f)
            {
                g_BeamT = BEAM_FRAMES;
                g_Energy = 0.0f;
                GameAudio::Play(GameSound::SpecialShot);
                GameInput::Rumble(0.7f, 24);
            }
            else
            {
                g_RefuseFlash = 20;
            }
        }
        break;
    }
    case STEP_DODGE:
    {
        if (++g_SpawnT >= 48)
        {
            g_SpawnT = 0;
            Spawn(2, g_ShipY + (Rand01() - 0.5f) * 14.0f, 2.8f);
        }
        UpdateBullets();
        if (g_Dodged >= DODGE_GOAL) GoTo(STEP_DONE);
        break;
    }
    default: break;
    }
}

namespace
{
std::string MoveLabel()
{
    if (GameInput::UsingGamepad()) return "L STICK";
    if (GameInput::LastDevice() == GameInput::Device::Mouse && GameSettings::Get().mouseSteering) return Loc::T("CURSOR");
    const std::string label = GameInput::PromptLabel("WASD");
    return label == "MOVE KEYS" ? Loc::T(label.c_str()) : label;
}

void DrawProgressPips()
{
    // the five steps, top right of the stage
    for (int i = 0; i < STEP_COUNT; ++i)
    {
        const float px = X + W - 22.0f - static_cast<float>(STEP_COUNT - 1 - i) * 16.0f, py = Y + 16.0f;
        const bool done = g_Step > i, now = g_Step == i;
        D::Dia(px, py, now ? 5.0f : 4.0f, done ? D::Fade(D::GOLD, 0.95f) : (now ? D::Fade(D::WHITE, 0.9f) : D::Rgba(1.0f, 1.0f, 1.0f, 0.18f)));
    }
}
}

void Draw(float alpha, float slide)
{
    if (alpha <= 0.01f) return;
    UiKit::SetOrigin(slide, 0.0f);
    D::Begin(X, Y, W, H, g_Frame, 0, false, true);
    const D::Color shipTint = g_Red ? D::RED : D::BLUE;

    // ---- the dummy of the beam step (a shielded elite) and the beam
    if (g_Step == STEP_BEAM || g_Step == STEP_DODGE)
    {
        const float ex = X + W - 60.0f, ey = g_Step == STEP_BEAM ? g_DummyY : Y + H * 0.5f;
        D::Enemy(ex, ey, D::GOLD, g_DummyFlash > 0 ? 1.0f : 0.0f, alpha, 1.5f);
        if (g_Step == STEP_BEAM && g_ShieldUp) D::Hex(ex, ey, 27.0f, 2.2f, D::Fade(D::GOLD, 0.9f * alpha));
        if (g_Step == STEP_BEAM && !g_ShieldUp) D::Hex(ex, ey, 27.0f, 1.0f, D::Fade(D::GOLD, 0.2f * alpha));
    }
    if (g_BeamT > 0)
    {
        const float k = static_cast<float>(BEAM_FRAMES - g_BeamT) / static_cast<float>(BEAM_FRAMES);
        const float len = (X + W - g_ShipX - 14.0f) * D::Ease(D::Seg01(k, 0.0f, 0.25f));
        D::Beam(g_ShipX + 12.0f, g_ShipY, len, 5.0f, D::WHITE, D::BLUE, alpha * (1.0f - D::Seg01(k, 0.7f, 1.0f)));
    }

    // ---- bullets
    for (const Bullet& b : g_Bullets)
    {
        if (!b.active) continue;
        const D::Color c = b.kind == 1 ? D::RED : D::BLUE;
        D::Bullet(b.x, b.y, b.kind == 2 ? D::WHITE : c, b.kind == 2, 1.25f, alpha);
        if (b.kind == 2) D::Line(b.x + 6.0f, b.y, b.x + 30.0f, b.y, 2.0f, D::Fade(D::WHITE, 0.35f * alpha));
    }

    // ---- the ship: the ring of its absorb box, the core that can be hurt
    D::Ring(g_ShipX, g_ShipY, ABSORB_R, 1.2f, D::Fade(shipTint, (0.30f + (g_AbsorbFlash > 0 ? 0.6f : 0.0f)) * alpha));
    if (g_AbsorbFlash > 0) D::Ring(g_ShipX, g_ShipY, ABSORB_R + 14.0f - static_cast<float>(g_AbsorbFlash), 1.6f, D::Fade(shipTint, 0.8f * alpha * static_cast<float>(g_AbsorbFlash) / 12.0f));
    D::Ship(g_ShipX, g_ShipY, shipTint, alpha * ((g_HitFlash / 4) % 2 == 1 ? 0.35f : 1.0f), 1.5f);
    D::Disc(g_ShipX - 2.0f, g_ShipY, 2.6f, D::Fade(D::WHITE, 0.9f * alpha)); // the core

    // ---- gauges: the HP and the energy of the stage
    D::Bar(X + 16.0f, Y + 12.0f, 120.0f, 6.0f, g_Hp / 100.0f, g_HitFlash > 0 ? D::RED : D::GREEN, alpha);
    D::Bar(X + 16.0f, Y + 26.0f, 120.0f, 6.0f, g_EnergyShown / 100.0f, g_Energy >= 100.0f ? D::GOLD : D::BLUE, alpha);
    D::Num("HP", X + 150.0f, Y + 15.0f, 10.0f, D::DIM, alpha, false);
    D::Num("EN", X + 150.0f, Y + 29.0f, 10.0f, D::DIM, alpha, false);
    DrawProgressPips();
    UiKit::SetOrigin(0.0f, 0.0f);

    // ---- the prompt under the stage
    std::string title, sub;
    switch (g_Step)
    {
    case STEP_MOVE:
        title = Loc::FmtText("MOVE WITH {0}", MoveLabel());
        sub = Loc::Fmt("FLY {0} / {1}", std::min(static_cast<int>(g_Moved), static_cast<int>(MOVE_GOAL)), static_cast<int>(MOVE_GOAL));
        break;
    case STEP_SWITCH:
        title = Loc::FmtText("PRESS {0} TO SWITCH RED / BLUE", Loc::T(GameInput::ActionLabel(GameSettings::Action::Switch).c_str()));
        sub = Loc::Fmt("SWITCHES {0} / {1}", std::min(g_Switches, SWITCH_GOAL), SWITCH_GOAL);
        break;
    case STEP_ABSORB:
        title = Loc::Fmt("ABSORB {0} BULLETS OF YOUR COLOR", ABSORB_GOAL);
        sub = g_HitFlash > 0 ? Loc::T("OTHER COLOR HURTS: SWITCH TO MATCH") : Loc::Fmt("ABSORBED {0} / {1}", std::min(g_Absorbed, ABSORB_GOAL), ABSORB_GOAL);
        break;
    case STEP_BEAM:
        title = g_BeamT > 0 ? Loc::T("THE BEAM BREAKS THE SHIELD") : Loc::FmtText("ENERGY FULL: PRESS {0}", Loc::T(GameInput::ActionLabel(GameSettings::Action::Special).c_str()));
        sub = Loc::T("A SHIELDED ELITE TAKES 25% LESS FROM SHOTS");
        break;
    case STEP_DODGE:
        title = Loc::T("DODGE THE WHITE BULLETS");
        sub = g_HitFlash > 0 ? Loc::T("WHITE CANNOT BE ABSORBED") : Loc::Fmt("DODGED {0} / {1}", std::min(g_Dodged, DODGE_GOAL), DODGE_GOAL);
        break;
    default:
        title = Loc::T("TRAINING COMPLETE");
        sub = Loc::T("PRESS ENTER TO PRACTISE AGAIN");
        break;
    }
    const float ty = Y + H + 26.0f;
    const float flash = g_StepDoneFlash > 0 ? static_cast<float>(g_StepDoneFlash) / 24.0f : 0.0f;
    const UiKit::Color tc = g_RefuseFlash > 0 ? UiKit::Rgba(1.0f, 0.55f, 0.48f, alpha) : UiKit::Rgba(0.97f + 0.03f * flash, 0.98f, 1.0f, alpha);
    GameMenuUi::DrawHeavyText(title, X + W * 0.5f + slide, ty, UiKit::FitFontPx(title, 17.0f, W - 20.0f, 11.0f), tc, TextAlignment::Center, W);
    UiKit::Text(sub, X + W * 0.5f + slide, ty + 22.0f, UiKit::FitFontPx(sub, 12.5f, W - 20.0f, 9.0f), UiKit::Rgba(0.66f, 0.74f, 0.88f, 0.95f * alpha), TextAlignment::Center, W, false);
}
}

