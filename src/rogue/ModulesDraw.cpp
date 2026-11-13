#include "core/AssetManager.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/GameTimer.h"
#include "boss/GameBoss.h"
#include "core/TextRenderer.h"
#include "game/GamePlayer.h"
#include "game/GameSpecialShot.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/BeamFx.h"
#include "game/GameVfx.h"
#include "core/GameSettings.h"
#include "game/GameEffect.h"
#include "game/GameWave.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "rogue/ModulesDrawInternal.h"
#include "rogue/ModulesInternal.h"
#include "ui/GameMenuUi.h"
#include "ui/CutIn.h"
#include "ui/UiKit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "rogue/Balance.h"
#include <string>

// Drawing of the module system: world effects, HUD chips, the pick screen and the route screen. Everything is icon based:
// one icon per module, one "+" line (what it gives), one "-" line (what it costs). Texts are measured and shrunk to fit.
namespace Mod
{
// (x, y) = top-left corner in virtual 1280x720 pixels
void DrawIcon(int cell, float x, float y, float size, UiKit::Color c)
{
    const int tex = AssetManager::Get(AssetManager::Tex::ModuleIcons);
    if (tex < 0 || c.a <= 0.0f) return;
    // half a texel inside the cell on every side keeps the filtering from reaching the neighbour
    const float u0 = (static_cast<float>((cell % ICON_COLS)) * ICON_CELL + 0.5f) / ICON_ATLAS_W;
    const float v0 = (static_cast<float>((cell / ICON_COLS)) * ICON_CELL + 0.5f) / ICON_ATLAS_H;
    const float u1 = u0 + (ICON_CELL - 1.0f) / ICON_ATLAS_W;
    const float v1 = v0 + (ICON_CELL - 1.0f) / ICON_ATLAS_H;
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    Renderer2D_DrawTexture(tex, UiKit::NX(x), UiKit::NY(y), UiKit::NX(x + size), UiKit::NY(y + size), c.r, c.g, c.b, c.a, u0, v0,
                           u1, v1);
    Renderer2D_SetBlendMode(previous);
}

// ---------------------------------------------------------------- colours / small helpers
UiKit::Color VerbColor(Verb verb)
{
    switch (verb)
    {
    case Verb::Absorb:
        return UiKit::Rgba(0.55f, 1.00f, 0.72f, 1.0f);
    case Verb::Break:
        return UiKit::Rgba(1.00f, 0.74f, 0.36f, 1.0f);
    default:
        return UiKit::Rgba(1.00f, 0.62f, 0.66f, 1.0f);
    }
}

UiKit::Color Alpha(UiKit::Color c, float a)
{
    c.a *= a;
    return c;
}

// "selected" is a solid ivory plate with dark text, everything else is a thin line on black (see GameMenuUi::DrawRow)
struct CardInk
{
    UiKit::Color text, sub, number, gold;
    UiKit::Color gainIcon, gainText, costIcon, costText;
};

CardInk InkFor(bool selected)
{
    if (selected)
    {
        return {INK,
                UiKit::Rgba(0.27f, 0.29f, 0.36f, 1.0f),
                SIGNAL_RED,
                GOLD_ON_LIGHT,
                UiKit::Rgba(0.08f, 0.50f, 0.22f, 1.0f),
                INK,
                UiKit::Rgba(0.76f, 0.12f, 0.10f, 1.0f),
                UiKit::Rgba(0.58f, 0.10f, 0.08f, 1.0f)};
    }
    return {UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.94f),    DIM,
            UiKit::Rgba(0.93f, 0.92f, 0.88f, 0.55f), GOLD,
            UiKit::Rgba(0.45f, 1.00f, 0.66f, 1.0f),  UiKit::Rgba(0.90f, 0.97f, 1.0f, 0.97f),
            UiKit::Rgba(1.00f, 0.50f, 0.46f, 1.0f),  UiKit::Rgba(1.0f, 0.80f, 0.76f, 0.97f)};
}

void PolarityRgb(Polarity polarity, float& r, float& g, float& b)
{
    GameVfx::GetPolarityColor(polarity == Polarity::Red, r, g, b);
}

void Txt(const std::string& text, float x, float y, float px, UiKit::Color c, TextAlignment align, float boxWidth)
{
    UiKit::Text(text, x, y, px, c, align, boxWidth, false);
}

// small tag with centred text (a slanted plate, like the HUD plates), returns its width
float Chip(float x, float y, float height, const std::string& text, float px, UiKit::Color tint, float maxWidth)
{
    const float fitted = UiKit::FitFontPx(text, px, maxWidth - 18.0f, 8.0f);
    const float width = UiKit::TextWidth(text, fitted) + 18.0f;
    GameMenuUi::Plate(x, y, width, height, Alpha(tint, 0.18f));
    GameMenuUi::PlateLine(x, y, width, height, Alpha(tint, 0.65f));
    Txt(text, x + width * 0.5f, y + height * 0.5f, fitted, tint, TextAlignment::Center, width + 20.0f);
    return width;
}

// ---------------------------------------------------------------- modes (challenge level / boss practice)
std::string ModeLabel(int mode)
{
    if (Meta::ModeIsPractice(mode)) return Loc::T("BOSS PRACTICE");
    const int level = Meta::ModeLevel(mode);
    return level == 0 ? Loc::T("STANDARD") : Loc::Fmt("CHALLENGE {0}", level);
}

UiKit::Color LevelColor(int level)
{
    if (level <= 0) return WHITE;
    return level >= Meta::CHALLENGE_MAX ? UiKit::Rgba(1.00f, 0.56f, 0.40f, 1.0f) : GOLD;
}

UiKit::Color ModeColor(int mode)
{
    return Meta::ModeIsPractice(mode) ? PRACTICE_BLUE : LevelColor(Meta::ModeLevel(mode));
}

// ---------------------------------------------------------------- world effects
// AEGIS: while the shield is up a thin hexagon rings the ship, while it recharges a ring of segments fills up, and a hit that it took flashes out as a ring.
// SHELL: while the plating is on, a thin amber hexagon rings the ship; a hit takes it off in a flash, and it is rebuilt segment by segment with the absorbed bullets.
// The tactic around the ship: a thin violet ring fills while the tactic recharges (gone when it is ready); the charge of CHARGE grows a second ring.
// (OVERFLOW: the stored bullets are shown in the HUD, under the energy bar: nothing hangs under the ship any more)
void DrawShipMarks()
{
    if (GameState::playerHp <= 0) return;
    const float x = GamePlayer::GetRenderX(GameState::player);
    const float y = GamePlayer::GetRenderY(GameState::player);
    if (g_Tactic >= 0 && g_TacticCooldown > 0)
    {
        int total = Balance::DECOY_COOLDOWN;
        if (g_Tactic == static_cast<int>(Tactic::Dash)) total = Balance::DASH_COOLDOWN;
        if (g_Tactic == static_cast<int>(Tactic::Charge)) total = Balance::CHARGE_COOLDOWN;
        constexpr int SEGMENTS = 30;
        const int lit = static_cast<int>(static_cast<float>(SEGMENTS) * (1.0f - static_cast<float>(g_TacticCooldown) / static_cast<float>(total > 0 ? total : 1)));
        for (int i = 0; i < lit; ++i)
        {
            const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(SEGMENTS) - 1.5707963f;
            const float a1 = 6.2831853f * (static_cast<float>(i) + 0.55f) / static_cast<float>(SEGMENTS) - 1.5707963f;
            GameVfx::DrawLine(x + std::cos(a0) * 0.098f, y + std::sin(a0) * 0.098f / ASPECT_Y, x + std::cos(a1) * 0.098f, y + std::sin(a1) * 0.098f / ASPECT_Y, 0.0026f,
                              0.80f, 0.62f, 1.0f, 0.50f);
        }
    }
    if (g_ChargeFrames > 0)
    {
        const float k = static_cast<float>(g_ChargeFrames) / static_cast<float>(Balance::CHARGE_FULL);
        const bool full = g_ChargeFrames >= Balance::CHARGE_FULL;
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(g_RunFrames) * 0.5f);
        GameVfx::DrawRound(GameVfx::Sprite::Ring, x, y, 0.30f - 0.16f * k, 1.0f, full ? 0.85f : 1.0f, full ? 0.40f : 1.0f, 0.35f + 0.45f * k);
        if (full) GameVfx::DrawRound(GameVfx::Sprite::Glow, x + 0.08f, y, 0.16f, 1.0f, 0.85f, 0.40f, 0.30f + 0.25f * pulse);
    }
}

// ---------------------------------------------------------------- the shapes that the effects below are made of (hard-edged: bars and diamonds, no soft fill)
namespace
{
float Clamp01f(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float EaseOutPow(float k, float p)
{
    return 1.0f - std::pow(1.0f - Clamp01f(k), p);
}

// a ring of `segments` short bars (the gaps are hard: fill is the share of a segment that is drawn), radius in x units
void HardRing(float cx, float cy, float radius, int segments, float fill, float thick, float rot, float r, float g, float b, float a)
{
    if (a <= 0.003f || radius <= 0.0f) return;
    const float step = 6.2831853f / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i)
    {
        const float a0 = rot + step * static_cast<float>(i), a1 = a0 + step * fill;
        GameVfx::DrawBar(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius / ASPECT_Y, cx + std::cos(a1) * radius, cy + std::sin(a1) * radius / ASPECT_Y, thick, r, g, b, a);
    }
}

void Cross(float x, float y, float size, float thick, float r, float g, float b, float a)
{
    if (a <= 0.003f) return;
    GameVfx::DrawLine(x - size, y, x + size, y, thick, r, g, b, a);
    GameVfx::DrawLine(x, y - size / ASPECT_Y, x, y + size / ASPECT_Y, thick, r, g, b, a);
}
}

// ---------------------------------------------------------------- MEND on the ship
// The repair is SEEN on the ship, so that the player does not have to look at the bar. When it begins (MEND_DELAY seconds without a hit) a ring opens out of the ship with four
// crosses thrown out of it and a thread of light that rises; while it goes on, a ring of 12 dashes around the ship fills towards the next repaired point (a lit dash = a twelfth
// of a point), a few crosses rise; every repaired point is a small flash and a cross above the ship. Everything is the green of the repair and low in alpha: bullets come first.
void DrawMendFx(float x, float y, float t)
{
    if (!Has(Id::Mend)) return;
    constexpr float R = 0.45f, G = 1.0f, B = 0.62f;
    if (g_MendStartTimer > 0)
    {
        const float k = 1.0f - static_cast<float>(g_MendStartTimer) / static_cast<float>(MEND_START_FRAMES);
        const float e = EaseOutPow(k, 2.4f);
        HardRing(x, y, 0.05f + 0.22f * e, 24, 0.76f, 0.0075f * (1.0f - k) + 0.0022f, 0.0f, R, G, B, 0.85f * (1.0f - k));
        HardRing(x, y, 0.03f + 0.15f * e, 16, 0.60f, 0.0030f, 0.4f, 1.0f, 1.0f, 1.0f, 0.55f * (1.0f - k));
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.26f, R, G, B, 0.34f * (1.0f - k) * (1.0f - k));
        for (int i = 0; i < 4; ++i)
        {
            const float ang = 0.7854f + 1.5708f * static_cast<float>(i);
            const float d = 0.045f + 0.14f * e;
            Cross(x + std::cos(ang) * d, y + std::sin(ang) * d / ASPECT_Y, 0.020f * (1.0f - 0.4f * k), 0.0065f, R, G, B, 0.95f * (1.0f - k));
        }
        GameVfx::DrawBar(x, y + 0.05f / ASPECT_Y, x, y + (0.05f + 0.55f * e) / ASPECT_Y, 0.0032f * (1.0f - k) + 0.0012f, R, G, 0.8f, 0.55f * (1.0f - k));
    }
    if (MendActive())
    {
        const float prog = MendProgress();
        const float pulse = 0.5f + 0.5f * std::sin(t * 0.18f);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.22f, R, G, B, 0.06f + 0.04f * pulse);
        constexpr int DASHES = 12;
        const int lit = static_cast<int>(prog * static_cast<float>(DASHES) + 0.5f);
        for (int i = 0; i < DASHES; ++i)
        {
            const float a0 = -1.5707963f + 6.2831853f * static_cast<float>(i) / static_cast<float>(DASHES);
            const float a1 = a0 + 6.2831853f / static_cast<float>(DASHES) * 0.55f;
            const bool on = i < lit;
            GameVfx::DrawBar(x + std::cos(a0) * 0.092f, y + std::sin(a0) * 0.092f / ASPECT_Y, x + std::cos(a1) * 0.092f, y + std::sin(a1) * 0.092f / ASPECT_Y, on ? 0.0030f : 0.0018f, R, G, B,
                             on ? 0.80f : 0.20f);
        }
        for (int n = 0; n < 3; ++n)
        {
            const float k = static_cast<float>((g_RunFrames + n * 20) % 60) / 60.0f;
            Cross(x - 0.04f + 0.04f * static_cast<float>(n), y + (0.05f + 0.16f * k) / 1.0f, 0.012f, 0.0045f, R, G, 0.62f, 0.75f * (1.0f - k));
        }
    }
    if (g_MendTickFlash > 0)
    {
        const float k = 1.0f - static_cast<float>(g_MendTickFlash) / static_cast<float>(MEND_TICK_FRAMES);
        HardRing(x, y, 0.09f + 0.05f * k, 20, 0.8f, 0.0040f * (1.0f - k) + 0.0015f, t * 0.05f, R, G, B, 0.80f * (1.0f - k));
        Cross(x, y + (0.13f + 0.06f * k) / 1.0f, 0.018f * (1.0f - 0.3f * k), 0.0060f, 0.75f, 1.0f, 0.82f, 0.95f * (1.0f - k));
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.16f, R, G, B, 0.30f * (1.0f - k));
    }
}

// ---- what the modules look like on the ship (each one has a sign that can be read at a glance)
// SIPHON     the net: a dotted ellipse of the colour of the ship that turns, as big as the absorb box
// MOMENTUM   one chevron of light behind the ship per stack
// AFTERGLOW  a gold glow and a gold ring around the ship while it lasts
// FOCUS      gold brackets around the target that close in with the stacks (locked at full stacks)
// MEND       a ring opens when the repair begins; a ring of dashes fills towards every repaired point; green crosses rise (DrawMendFx)
// BACKWATER  a red flicker around the ship under half of the HP
void DrawModuleAuras()
{
    if (GameState::playerHp <= 0) return;
    const float x = GamePlayer::GetRenderX(GameState::player);
    const float y = GamePlayer::GetRenderY(GameState::player);
    const float t = static_cast<float>(g_RunFrames);
    float pr, pg, pb;
    PolarityRgb(GameState::player.polarity, pr, pg, pb);

    if (Has(Id::Siphon))
    {
        const float hw = GameState::player.halfWidth * SiphonScale();
        const float hh = GameState::player.halfHeight * SiphonScale();
        // the net: a thin dashed ellipse of the ship's colour that turns slowly, four hooks on it pointing in (quiet: it must not hide bullets)
        constexpr int DASH = 28;
        for (int i = 0; i < DASH; ++i)
        {
            const float a0 = t * 0.012f + 6.2831853f * static_cast<float>(i) / static_cast<float>(DASH);
            const float a1 = a0 + 6.2831853f / static_cast<float>(DASH) * 0.55f;
            GameVfx::DrawBar(x + std::cos(a0) * hw, y + std::sin(a0) * hh, x + std::cos(a1) * hw, y + std::sin(a1) * hh, 0.0016f, pr, pg, pb, 0.30f);
        }
        for (int i = 0; i < 4; ++i)
        {
            const float a = -t * 0.020f + 1.5707963f * static_cast<float>(i);
            const float ox = x + std::cos(a) * hw, oy = y + std::sin(a) * hh;
            const float ix = x + std::cos(a) * hw * 0.86f, iy = y + std::sin(a) * hh * 0.86f;
            GameVfx::DrawBar(ox, oy, ix, iy, 0.0030f, pr, pg, pb, 0.55f);
            GameVfx::DrawDiamond(ox, oy, 0.0055f, a, 1.0f, 1.0f, 1.0f, 0.60f);
        }
    }

    if (Has(Id::Momentum) && g_Momentum > 0.0f)
    {
        const int stacks = static_cast<int>(std::lround(g_Momentum));
        for (int k = 0; k < stacks; ++k)
        {
            const float cx = x - 0.085f - 0.014f * static_cast<float>(k);
            const float a = 0.70f - 0.06f * static_cast<float>(k);
            GameVfx::DrawLine(cx, y + 0.022f, cx - 0.010f, y, 0.0030f, 1.0f, 0.86f, 0.55f, a);
            GameVfx::DrawLine(cx - 0.010f, y, cx, y - 0.022f, 0.0030f, 1.0f, 0.86f, 0.55f, a);
        }
    }

    if (g_AfterglowTimer > 0)
    {
        const float k = static_cast<float>(g_AfterglowTimer) / static_cast<float>(Balance::AFTERGLOW_FRAMES);
        const float pulse = 0.5f + 0.5f * std::sin(t * 0.3f);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.30f, 1.0f, 0.78f, 0.30f, (0.18f + 0.10f * pulse) * (std::min)(1.0f, k * 3.0f));
        GameVfx::DrawRound(GameVfx::Sprite::Ring, x, y, 0.20f + 0.02f * pulse, 1.0f, 0.86f, 0.45f, 0.45f * (std::min)(1.0f, k * 3.0f));
    }

    if (Has(Id::Focus) && g_FocusStacks > 0 && g_FocusTarget >= 0)
    {
        float fx = 0.0f, fy = 0.0f, half = 0.07f;
        bool found = false;
        if (g_FocusTarget < GameState::ENEMY_MAX)
        {
            const Enemy& e = GameState::enemies[g_FocusTarget];
            if (e.active)
            {
                fx = GameTimer::Blend(e.prevX, e.x);
                fy = GameTimer::Blend(e.prevY, e.y);
                half = (std::max)(e.halfWidth, e.halfHeight * ASPECT_Y) * 1.3f;
                found = true;
            }
        }
        else
        {
            found = GameBoss::GetTargetPosition(fx, fy);
            half = 0.10f;
        }
        if (found)
        {
            const float k = static_cast<float>(g_FocusStacks) / static_cast<float>(Balance::FOCUS_MAX_STACKS);
            const bool locked = g_FocusStacks >= Balance::FOCUS_MAX_STACKS;
            const float r = half * (1.6f - 0.6f * k);
            const float leg = r * 0.45f;
            const float a = locked ? 0.95f : 0.45f + 0.40f * k;
            const float g = locked ? 0.72f : 0.95f, b = locked ? 0.25f : 0.80f;
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2)
                {
                    const float cx = fx + static_cast<float>(sx) * r, cy = fy + static_cast<float>(sy) * r / ASPECT_Y;
                    GameVfx::DrawLine(cx, cy, cx - static_cast<float>(sx) * leg, cy, 0.0035f, 1.0f, g, b, a);
                    GameVfx::DrawLine(cx, cy, cx, cy - static_cast<float>(sy) * leg / ASPECT_Y, 0.0035f, 1.0f, g, b, a);
                }
            if (locked) GameVfx::DrawRound(GameVfx::Sprite::Glow, fx, fy, half * 2.4f, 1.0f, 0.72f, 0.25f, 0.18f + 0.10f * std::sin(t * 0.4f));
        }
    }

    DrawMendFx(x, y, t);

    if (Has(Id::Backwater) && LowHp())
    {
        const float flicker = 0.5f + 0.5f * std::sin(t * 0.55f) * std::sin(t * 0.21f);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.34f, 1.0f, 0.30f, 0.18f, 0.16f + 0.14f * flicker);
    }
}

bool BackwaterActive()
{
    return Has(Id::Backwater) && LowHp();
}

void DrawAegis()
{
    if (!Has(Id::Aegis) || GameState::playerHp <= 0) return;
    const float x = GamePlayer::GetRenderX(GameState::player);
    const float y = GamePlayer::GetRenderY(GameState::player);
    const float time = static_cast<float>(g_RunFrames);
    if (g_AegisFlash > 0)
    {
        const float k = 1.0f - static_cast<float>(g_AegisFlash) / 30.0f;
        GameVfx::DrawRound(GameVfx::Sprite::Ring, x, y, 0.14f + 0.34f * k, 0.80f, 0.95f, 1.0f, (1.0f - k) * 0.90f);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.08f + 0.30f * (1.0f - k), 0.70f, 0.90f, 1.0f, (1.0f - k) * 0.70f);
    }
    constexpr float R = 0.118f;
    if (g_AegisTimer <= 0)
    {
        const float pulse = 0.5f + 0.5f * std::sin(time * 0.12f);
        for (int i = 0; i < 6; ++i)
        {
            const float a0 = time * 0.01f + 6.2831853f * static_cast<float>(i) / 6.0f;
            const float a1 = time * 0.01f + 6.2831853f * static_cast<float>(i + 1) / 6.0f;
            GameVfx::DrawLine(x + std::cos(a0) * R, y + std::sin(a0) * R / ASPECT_Y, x + std::cos(a1) * R, y + std::sin(a1) * R / ASPECT_Y, 0.0030f, 0.70f, 0.92f, 1.0f, 0.42f + 0.18f * pulse);
        }
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, R * 2.2f, 0.55f, 0.85f, 1.0f, 0.07f + 0.04f * pulse);
        return;
    }
    constexpr int SEGMENTS = 24;
    const int lit = static_cast<int>(static_cast<float>(SEGMENTS) * AegisCharge());
    for (int i = 0; i < lit; ++i)
    {
        const float a0 = 6.2831853f * static_cast<float>(i) / static_cast<float>(SEGMENTS) - 1.5707963f;
        const float a1 = 6.2831853f * (static_cast<float>(i) + 0.55f) / static_cast<float>(SEGMENTS) - 1.5707963f;
        GameVfx::DrawLine(x + std::cos(a0) * 0.112f, y + std::sin(a0) * 0.112f / ASPECT_Y, x + std::cos(a1) * 0.112f, y + std::sin(a1) * 0.112f / ASPECT_Y, 0.0022f, 0.70f, 0.92f, 1.0f, 0.38f);
    }
}

// ---------------------------------------------------------------- EMBER: the band of fire that the beam leaves
// The look is BeamFx::DrawFlameBand (flames that lean into the wind, a bed of coals, embers, two dashed lines for the edge of the zone: see BeamFx.h); this only hands it the
// zone (the bullets that fly into it burn into energy: InEmber) and how old it is.
void DrawEmberFire()
{
    if (g_EmberTimer <= 0) return;
    BeamFx::FlameParams band;
    band.x0 = g_EmberStartX;
    band.x1 = g_EmberEndX;
    band.y = g_EmberY;
    band.halfY = g_EmberHalf;
    band.lifetime = g_EmberLife > 0 ? g_EmberLife : EMBER_FRAMES;
    band.age = band.lifetime - g_EmberTimer;
    BeamFx::DrawFlameBand(band);
}

// ---------------------------------------------------------------- BACKUP CORE: the blast
// 0 - 17   the light gathers: sixteen shards of the two colours (red from one side, blue from the other: the mirror of the theme) fly into the ship while a thread of
//          light grows through it - this happens during the hit stop, so it is a held breath
// 17       the flash (scaled by the FLASHES setting) and the three shock rings are born: red, white and blue, a little apart, crossing the whole screen; fourteen hard rays
//          run out of the ship; a column of light stands through it for a moment
// 17 - 72  the rings thin out and the light goes. The shards of both colours (GameEffect crystals) are thrown when the rings are born (UpdateUi).
void DrawReviveBlast()
{
    if (g_ReviveTimer <= 0) return;
    const int age = REVIVE_FRAMES - g_ReviveTimer;
    const float x = g_ReviveX, y = g_ReviveY;
    constexpr float RED[3] = {1.0f, 0.30f, 0.26f}, BLUE[3] = {0.30f, 0.62f, 1.0f};
    if (age < REVIVE_BURST_AT)
    {
        const float k = static_cast<float>(age) / static_cast<float>(REVIVE_BURST_AT);
        for (int i = 0; i < 16; ++i)
        {
            const bool red = (i & 1) == 0;
            const float ang = 6.2831853f * static_cast<float>(i) / 16.0f + 0.2f + 0.5f * (1.0f - k);                  // a slow turn as they come in
            const float r = 0.04f + 0.34f * std::pow(1.0f - k, 1.8f);
            const float len = 0.010f + 0.040f * k;
            const float px = x + std::cos(ang) * r, py = y + std::sin(ang) * r / ASPECT_Y;
            const float qx = x + std::cos(ang) * (r - len), qy = y + std::sin(ang) * (r - len) / ASPECT_Y;
            const float* c = red ? RED : BLUE;
            GameVfx::DrawBar(px, py, qx, qy, 0.0030f, c[0], c[1], c[2], 0.35f + 0.60f * k);
            GameVfx::DrawDiamond(px, py, 0.0050f, ang, 1.0f, 1.0f, 1.0f, 0.25f + 0.55f * k);
        }
        GameVfx::DrawBar(x, y - 1.4f, x, y + 1.4f, 0.0015f + 0.0060f * k, 0.80f, 0.92f, 1.0f, 0.15f + 0.55f * k);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.10f + 0.24f * k, 1.0f, 1.0f, 1.0f, 0.10f + 0.40f * k * k);
        return;
    }
    const float tb = static_cast<float>(age - REVIVE_BURST_AT) / static_cast<float>(REVIVE_FRAMES - REVIVE_BURST_AT); // 0 .. 1
    const float flashScale = static_cast<float>(GameSettings::Get().flashPercent) / 100.0f;
    const int since = age - REVIVE_BURST_AT;
    if (since < 4) Renderer2D_DrawQuad(-1.0f, 1.0f, 1.0f, -1.0f, 0.95f, 0.97f, 1.0f, 0.24f * flashScale * (1.0f - static_cast<float>(since) / 4.0f));
    const float reach = 2.0f;                                         // x units: more than the screen is wide, so the rings leave it
    const float life = std::pow(1.0f - tb, 1.4f);
    const float thick = 0.016f * (1.0f - tb) + 0.0030f;
    HardRing(x, y, reach * EaseOutPow(tb, 2.3f), 96, 0.92f, thick, 0.0f, RED[0], RED[1], RED[2], 0.85f * life);
    HardRing(x, y, reach * 0.88f * EaseOutPow(tb, 2.3f), 96, 0.92f, thick * 0.8f, 0.03f, 1.0f, 1.0f, 1.0f, 0.95f * life);
    HardRing(x, y, reach * 0.76f * EaseOutPow(tb, 2.3f), 96, 0.92f, thick, 0.06f, BLUE[0], BLUE[1], BLUE[2], 0.85f * life);
    for (int i = 0; i < 14; ++i)                                      // hard rays: they are born with the rings and die first
    {
        const float ang = 6.2831853f * static_cast<float>(i) / 14.0f + 0.11f;
        const float r0 = reach * 0.10f * EaseOutPow(tb, 2.0f), r1 = reach * 0.55f * EaseOutPow(tb, 2.0f);
        const float* c = (i & 1) ? BLUE : RED;
        GameVfx::DrawBar(x + std::cos(ang) * r0, y + std::sin(ang) * r0 / ASPECT_Y, x + std::cos(ang) * r1, y + std::sin(ang) * r1 / ASPECT_Y, 0.0030f * (1.0f - tb) + 0.0010f, c[0], c[1], c[2],
                         0.55f * std::pow(1.0f - tb, 2.4f));
    }
    GameVfx::DrawBar(x, y - 1.4f, x, y + 1.4f, 0.0100f * (1.0f - tb) + 0.0015f, 0.85f, 0.94f, 1.0f, 0.65f * std::pow(1.0f - tb * 1.7f > 0.0f ? 1.0f - tb * 1.7f : 0.0f, 1.2f));
    GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.55f * (1.0f - tb) + 0.12f, 1.0f, 1.0f, 1.0f, 0.30f * life * life);
    GameVfx::DrawRound(GameVfx::Sprite::Ring, x, y, 0.16f + 0.30f * EaseOutPow(tb, 2.0f), 1.0f, 1.0f, 1.0f, 0.40f * life);
}

void DrawWorld()
{
    if (g_Decoy.active) // the decoy: a ring of its colour that pulls; it fades in its last second
    {
        const Zone& z = g_Decoy;
        float r, g, b;
        PolarityRgb(z.polarity, r, g, b);
        const float life = (std::min)(1.0f, static_cast<float>(z.timer) / 60.0f);
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(z.timer) * 0.35f);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, z.x, z.y, DECOY_HALF_W * 2.6f, r, g, b, 0.22f * life);
        GameVfx::DrawRound(GameVfx::Sprite::Ring, z.x, z.y, DECOY_HALF_W * 2.0f * (0.92f + 0.08f * pulse), r, g, b, 0.75f * life);
        GameVfx::DrawRound(GameVfx::Sprite::Ring, z.x, z.y, Balance::DECOY_PULL_RADIUS * 2.0f * (1.0f - 0.15f * pulse), r, g, b, 0.18f * life);
        GameVfx::DrawRound(GameVfx::Sprite::Ring, z.x, z.y, DECOY_HALF_W * 1.4f, 1.0f, 1.0f, 1.0f, 0.25f * life);
    }
    // ECHO: lightning in the ship's colour - a jagged bolt that changes its shape every two frames, a short fork, a white core,
    // a hard flash where it strikes. It burns out in 10 frames.
    for (int ai = 0; ai < 6; ++ai)
    {
        const EchoArc& arc = g_Arcs[ai];
        if (arc.timer <= 0) continue;
        float r, g, b;
        PolarityRgb(arc.polarity, r, g, b);
        const float life = static_cast<float>(arc.timer) / 10.0f;
        const int shape = arc.timer / 2 + ai * 17; // (a new jag every two frames)
        const float dx = arc.x2 - arc.x1, dy = (arc.y2 - arc.y1) * ASPECT_Y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) continue;
        const float nx = -dy / len, ny = dx / len / ASPECT_Y; // the normal (in y units)
        constexpr int SEG = 9;
        float px[SEG + 1], py[SEG + 1];
        for (int i = 0; i <= SEG; ++i)
        {
            const float f = static_cast<float>(i) / SEG;
            const float env = std::sin(f * 3.1415926f); // (no jag at the ends)
            const float off = (GameVfx::Hash(shape * 31 + i * 7) - 0.5f) * 2.0f * (0.018f + 0.06f * len) * env;
            px[i] = arc.x1 + (arc.x2 - arc.x1) * f + nx * off;
            py[i] = arc.y1 + (arc.y2 - arc.y1) * f + ny * off;
        }
        for (int i = 0; i < SEG; ++i)
        {
            GameVfx::DrawLine(px[i], py[i], px[i + 1], py[i + 1], 0.020f, r, g, b, 0.35f * life);                   // the glow
            GameVfx::DrawBar(px[i], py[i], px[i + 1], py[i + 1], 0.0055f * life + 0.0015f, r, g, b, 0.95f * life);   // the bolt
            GameVfx::DrawBar(px[i], py[i], px[i + 1], py[i + 1], 0.0018f, 1.0f, 1.0f, 1.0f, life);                    // its core
        }
        // a fork off the middle
        {
            const int k = 3 + (shape % 4);
            const float side = (shape & 1) ? 1.0f : -1.0f;
            float fx = px[k], fy = py[k];
            for (int j = 0; j < 3; ++j)
            {
                const float step = 0.035f * (1.0f - 0.25f * static_cast<float>(j));
                const float wob = (GameVfx::Hash(shape * 13 + j) - 0.5f) * 0.6f;
                const float qx = fx + (dx / len * 0.6f + nx * side * 0.8f + wob * nx) * step;
                const float qy = fy + ((dy / len) / ASPECT_Y * 0.6f + ny * side * 0.8f + wob * ny) * step;
                GameVfx::DrawBar(fx, fy, qx, qy, 0.0030f * life + 0.001f, r, g, b, 0.80f * life);
                fx = qx;
                fy = qy;
            }
        }
        // the strike: a hard white diamond and a ring of four sparks; the source: a small flash
        GameVfx::DrawRound(GameVfx::Sprite::Glow, arc.x2, arc.y2, 0.07f * life, r, g, b, 0.6f * life);
        GameVfx::DrawDiamond(arc.x2, arc.y2, 0.014f * life + 0.004f, 0.7854f + static_cast<float>(arc.timer), 1.0f, 1.0f, 1.0f, life);
        for (int i = 0; i < 4; ++i)
        {
            const float sa = 0.4f + 1.5707963f * static_cast<float>(i) + static_cast<float>(shape);
            const float d0 = 0.012f, d1 = 0.012f + 0.03f * life;
            GameVfx::DrawBar(arc.x2 + std::cos(sa) * d0, arc.y2 + std::sin(sa) * d0 / ASPECT_Y, arc.x2 + std::cos(sa) * d1, arc.y2 + std::sin(sa) * d1 / ASPECT_Y,
                             0.0024f, 1.0f, 1.0f, 1.0f, life);
        }
        GameVfx::DrawDiamond(arc.x1, arc.y1, 0.008f * life, 0.0f, r, g, b, life);
    }
    if (g_PulseTimer > 0)
    {
        const float t = 1.0f - static_cast<float>(g_PulseTimer) / 24.0f;
        const float size = g_PulseRadius * 2.0f * (0.25f + 0.75f * t);
        const float life = 1.0f - t;
        // a hard shock ring of 24 segments (its colour: the survive modules' rose), a thin white ring inside, a soft fill only at the start
        const float rad = size * 0.5f;
        constexpr int SEG = 24;
        for (int i = 0; i < SEG; ++i)
        {
            const float a0 = 6.2831853f * static_cast<float>(i) / SEG, a1 = a0 + 6.2831853f / SEG * 0.78f;
            GameVfx::DrawBar(g_PulseX + std::cos(a0) * rad, g_PulseY + std::sin(a0) * rad / ASPECT_Y, g_PulseX + std::cos(a1) * rad,
                             g_PulseY + std::sin(a1) * rad / ASPECT_Y, 0.008f * life + 0.002f, 1.0f, 0.55f, 0.60f, 0.90f * life);
        }
        GameVfx::DrawRound(GameVfx::Sprite::Ring, g_PulseX, g_PulseY, size * 0.80f, 1.0f, 1.0f, 1.0f, 0.35f * life);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, g_PulseX, g_PulseY, size, 1.0f, 0.55f, 0.60f, 0.12f * life * life);
    }
    DrawModuleAuras();
    DrawAegis();
    DrawShipMarks();
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    if (g_TacticFlash > 0) // the beam of the tactic CHARGE: the plasma of every beam of the game, in violet (BeamFx)
    {
        constexpr int TACTIC_FLASH_FRAMES = 16;
        BeamFx::BeamParams beam;
        beam.x0 = g_TacticBeamX;
        beam.y0 = g_TacticBeamY;
        beam.x1 = 1.15f;
        beam.y1 = g_TacticBeamY;
        beam.halfWidth = g_TacticBeamHalf * ASPECT_Y;
        beam.time = static_cast<float>(TACTIC_FLASH_FRAMES - g_TacticFlash);
        beam.envelope = static_cast<float>(g_TacticFlash) / static_cast<float>(TACTIC_FLASH_FRAMES);
        beam.turbulence = 0.8f;
        beam.arcs = 3;
        beam.pulseSpeed = 2.0f;
        beam.seed = 41;
        BeamFx::DrawBeam(beam, BeamFx::VioletPalette());
    }
    DrawEmberFire();
    DrawReviveBlast();
    Renderer2D_SetBlendMode(previous);
    DrawMotes();
}

// ---------------------------------------------------------------- HUD: owned modules as icon chips (+ resonance / synergy line + discovery toast)
void DrawToast()
{
    if (g_ToastTimer <= 0 || g_ToastCombo < 0) return;
    const float fadeIn = (std::min)(1.0f, static_cast<float>(260 - g_ToastTimer) / 14.0f);
    const float fadeOut = (std::min)(1.0f, static_cast<float>(g_ToastTimer) / 30.0f);
    const float a = (std::min)(fadeIn, fadeOut);
    const Meta::ComboInfo& combo = Meta::COMBOS[g_ToastCombo];
    const float w = 440.0f, h = 84.0f, x = 640.0f - w * 0.5f, y = 118.0f;
    GameMenuUi::Panel(x + 6.0f, y + 5.0f, w, h, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f * a), UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.0f));
    GameMenuUi::Panel(x, y, w, h, 0.0f, UiKit::Rgba(0.03f, 0.04f, 0.08f, 0.90f * a), Alpha(GOLD, 0.75f * a));
    UiKit::Fill(x, y, 5.0f, h, Alpha(GOLD, a));
    DrawIcon(static_cast<int>(combo.a), x + 20.0f, y + 26.0f, 32.0f, Alpha(VerbColor(INFO[static_cast<int>(combo.a)].verb), a));
    DrawIcon(ICON_PLUS, x + 56.0f, y + 32.0f, 20.0f, Alpha(GOLD, a));
    DrawIcon(static_cast<int>(combo.b), x + 80.0f, y + 26.0f, 32.0f, Alpha(VerbColor(INFO[static_cast<int>(combo.b)].verb), a));
    const float textX = x + 128.0f;
    const float textW = w - 128.0f - 14.0f;
    Txt(Loc::T("NEW SYNERGY"), textX, y + 20.0f, 12.0f, Alpha(GOLD, a), TextAlignment::Left, textW);
    const std::string name = Loc::T(combo.name);
    Txt(name, textX, y + 44.0f, UiKit::FitFontPx(name, 22.0f, textW, 12.0f), Alpha(WHITE, a), TextAlignment::Left, textW);
    const std::string desc = Loc::T(combo.desc);
    Txt(desc, textX, y + 68.0f, UiKit::FitFontPx(desc, 13.0f, textW, 9.0f), Alpha(DIM, a), TextAlignment::Left, textW);
}

// small tag under the score panel: challenge level or boss practice
void DrawModeTag()
{
    if (g_PracticePhase <= 0 && g_RunLevel <= 0) return;
    const std::string label = g_PracticePhase > 0 ? Loc::T("BOSS PRACTICE") : Loc::Fmt("CHALLENGE {0}", g_RunLevel);
    const UiKit::Color tint = g_PracticePhase > 0 ? PRACTICE_BLUE : LevelColor(g_RunLevel);
    UiKit::Image(UiAtlas::Id::Diamond, 40.0f, 109.0f, 9.0f, 9.0f, tint);
    Txt(label, 56.0f, 113.0f, UiKit::FitFontPx(label, 12.0f, 230.0f, 8.0f), tint, TextAlignment::Left, 240.0f);
}

void DrawChips()
{
    DrawModeTag();
    // Modules are icons with a line under them in the colour of their type. A second gold line means resonance
    // (two of the same type), a gold diamond above an icon means it is part of an active synergy.
    // The names are on the pick screen.
    constexpr float SIZE = 22.0f;
    constexpr float GAP = 10.0f;
    const float y = 606.0f;
    float x = 40.0f;
    float centre[COUNT];
    for (int i = 0; i < COUNT; ++i)
        centre[i] = -1.0f;
    for (int i = 0; i < g_OrderCount; ++i)
    {
        const int index = g_Order[i];
        const bool spent = ((index == static_cast<int>(Id::BackupCore)) && g_BackupUsed) || ((index == static_cast<int>(Id::Aegis)) && g_AegisTimer > 0);
        const float alpha = spent ? 0.35f : 1.0f;
        const UiKit::Color tint = VerbColor(INFO[index].verb);
        DrawIcon(index, x, y, SIZE, Alpha(tint, alpha));
        if (g_Level[index] >= 2) // the level of an upgraded module
        {
            Txt(g_Level[index] == 2 ? "II" : "III", x + SIZE + 2.0f, y - 5.0f, 11.0f, Alpha(GOLD, alpha), TextAlignment::Right, 30.0f);
        }
        if (index == static_cast<int>(Id::Momentum) && g_Momentum > 0.0f) // the stacks of MOMENTUM: a gold bar above the icon
        {
            UiKit::Fill(x, y - 6.0f, SIZE, 2.0f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.12f));
            UiKit::Fill(x, y - 6.0f, SIZE * g_Momentum / static_cast<float>(Balance::MOMENTUM_MAX_STACKS), 2.0f, Alpha(GOLD, alpha));
        }
        UiKit::Fill(x, y + SIZE + 3.0f, SIZE, 2.0f, Alpha(tint, alpha));
        if (Resonant(INFO[index].verb))
        {
            UiKit::Fill(x, y + SIZE + 7.0f, SIZE, 2.0f, Alpha(GOLD, alpha));
        }
        centre[index] = x + SIZE * 0.5f;
        x += SIZE + GAP;
    }
    for (int c = 0; c < Meta::COMBO_COUNT; ++c)
    {
        if (!ComboOn(c)) continue;
        const float ax = centre[static_cast<int>(Meta::COMBOS[c].a)];
        const float bx = centre[static_cast<int>(Meta::COMBOS[c].b)];
        if (ax < 0.0f || bx < 0.0f) continue;
        UiKit::Image(UiAtlas::Id::Diamond, ax - 3.5f, y - 11.0f, 7.0f, 7.0f, GOLD);
        UiKit::Image(UiAtlas::Id::Diamond, bx - 3.5f, y - 11.0f, 7.0f, 7.0f, GOLD);
    }
    DrawToast();
}

// ---------------------------------------------------------------- pick screen
// Three tall cards under a banner. The world behind them is slowed down (see TimeScale in ModulesPick.cpp), the cards rise one after
// the other, the highlighted card lifts off the page with a hard red offset (the same cut as the menu rows).
constexpr float NAME_PX = 26.0f;
constexpr float ROW_PX = 15.0f;
constexpr float ROW_MIN_PX = 10.0f;

UiKit::Color Mix(UiKit::Color a, UiKit::Color b, float t)
{
    return UiKit::Rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

// "+" / "-" row of a module (used by the cards and the hangar details). Returns how far the next row has to move down:
// a text that is too long for the row breaks into two lines at the space nearest to its middle.
float DrawEffectRowText(int iconCell, UiKit::Color iconColor, const std::string& text, UiKit::Color textColor, float x, float y,
                        float maxWidth, float px)
{
    const float textX = x + 24.0f;
    const float textW = maxWidth - 24.0f;
    if (iconCell >= 0) DrawIcon(iconCell, x, y - 9.0f, 18.0f, iconColor);
    if (UiKit::TextWidth(text, px) <= textW * 0.94f)
    {
        Txt(text, textX, y, px, textColor, TextAlignment::Left, textW);
        return 28.0f;
    }
    size_t split = std::string::npos;
    float best = 1.0e9f;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != ' ') continue;
        const float diff = std::fabs(static_cast<float>(i) - static_cast<float>(text.size()) * 0.5f);
        if (diff < best)
        {
            best = diff;
            split = i;
        }
    }
    if (split == std::string::npos)
    {
        Txt(text, textX, y, UiKit::FitFontPx(text, px, textW, ROW_MIN_PX), textColor, TextAlignment::Left, textW);
        return 28.0f;
    }
    const std::string first = text.substr(0, split);
    const std::string second = text.substr(split + 1);
    const float lineH = px * 1.22f;
    Txt(first, textX, y - lineH * 0.5f, UiKit::FitFontPx(first, px, textW, ROW_MIN_PX), textColor, TextAlignment::Left, textW);
    Txt(second, textX, y + lineH * 0.5f, UiKit::FitFontPx(second, px, textW, ROW_MIN_PX), textColor, TextAlignment::Left, textW);
    return lineH + 26.0f;
}

float DrawEffectRow(int iconCell, UiKit::Color iconColor, const char* key, UiKit::Color textColor, float x, float y, float maxWidth,
                    float px)
{
    return DrawEffectRowText(iconCell, iconColor, Loc::T(key), textColor, x, y, maxWidth, px);
}

float DrawGainRow(const char* key, float x, float y, float maxWidth, float px, bool onLight)
{
    const CardInk ink = InkFor(onLight);
    return DrawEffectRow(ICON_PLUS, ink.gainIcon, key, ink.gainText, x, y, maxWidth, px);
}

float DrawCostRow(const char* key, float x, float y, float maxWidth, float px, bool onLight)
{
    const CardInk ink = InkFor(onLight);
    return DrawEffectRow(ICON_MINUS, ink.costIcon, key, ink.costText, x, y, maxWidth, px);
}

void DrawCardFrame(float x, float y, float w, float h, float sel, float alpha, UiKit::Color tint)
{
    const UiKit::Color none = UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.0f);
    // THE SEAM, quiet in battle: the card is on top of the fight, so the selected one stays DARK (a big light plate over the bullets is
    // glare). It is marked by the edge: an ivory frame, a hard offset of the polarity colour (a strip, not a plate), a heavier type bar,
    // a glass line of the other colour that glides over the top (P3) and the seam diamond as the cursor.
    if (sel > 0.02f)
    {
        GameMenuUi::Panel(x + 8.0f * sel, y + 7.0f * sel, w, h, 0.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f * sel * alpha), none);
        GameMenuUi::Panel(x + 4.0f * sel, y + 4.0f * sel, w, h, 0.0f, GameMenuUi::Accent(0.85f * sel * alpha), none);
    }
    const UiKit::Color fill = Mix(UiKit::Rgba(0.020f, 0.028f, 0.050f, 0.92f), UiKit::Rgba(0.040f, 0.050f, 0.080f, 1.0f), sel);
    const UiKit::Color line = Mix(UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.20f), UiKit::Rgba(0.93f, 0.92f, 0.88f, 0.80f), sel);
    GameMenuUi::Panel(x, y, w, h, 0.0f, Alpha(fill, alpha), Alpha(line, alpha));
    UiKit::Fill(x, y, w, 4.0f + 2.0f * sel, Alpha(tint, alpha)); // the colour of the type along the top edge
    const float glide = GameMenuUi::EaseOut(GameMenuUi::Clamp01((sel - 0.35f) / 0.65f));
    if (glide > 0.0f) UiKit::Fill(x + w * (1.0f - glide), y - 7.0f, w * glide, 1.5f, GameMenuUi::Counter(0.9f * glide * alpha));
    if (sel > 0.02f) GameMenuUi::SeamDiamond(x - 18.0f - (1.0f - sel) * 12.0f, y + h * 0.5f, 8.0f, sel * alpha);
    GameMenuUi::SeamRule(x + 24.0f, y + h - 12.0f, w - 48.0f, 1.0f, (1.0f - sel) * 0.45f * alpha);
}

// the icon stands on a diamond (a square turned by 45 degrees): one shape for every card, no ring
void DrawCardIcon(int iconCell, float cx, float cy, UiKit::Color tint, float sel, float alpha)
{
    GameMenuUi::DiamondFill(cx, cy, 62.0f, Alpha(tint, 0.10f * alpha));
    GameMenuUi::DiamondOutline(cx, cy, 62.0f, 2.0f, Alpha(tint, (0.55f + 0.40f * sel) * alpha));
    GameMenuUi::DiamondOutline(cx, cy, 72.0f, 1.0f, Alpha(tint, 0.25f * alpha));
    DrawIcon(iconCell, cx - 36.0f, cy - 36.0f, 72.0f, Alpha(tint, alpha));
}

// a headline number as the upgrade card shows it: "+58%", "2.6 S", "32 HP", "0.34"
std::string FormatStat(StatFmt fmt, float value)
{
    char text[32];
    switch (fmt)
    {
    case StatFmt::PlusPercent:
        std::snprintf(text, sizeof text, "+%d%%", static_cast<int>(std::lround(value)));
        break;
    case StatFmt::Seconds:
        std::snprintf(text, sizeof text, "%.1f S", static_cast<double>(value));
        break;
    case StatFmt::Count:
        std::snprintf(text, sizeof text, "%d", static_cast<int>(std::lround(value)));
        break;
    case StatFmt::Hp:
        std::snprintf(text, sizeof text, "%d HP", static_cast<int>(std::lround(value)));
        break;
    case StatFmt::Percent:
        std::snprintf(text, sizeof text, "%d%%", static_cast<int>(std::lround(value)));
        break;
    case StatFmt::Decimal1:

        std::snprintf(text, sizeof text, "%.1f", static_cast<double>(value));
        break;
    default:
        std::snprintf(text, sizeof text, "%.2f", static_cast<double>(value));
        break;
    }
    return text;
}

void DrawCard(int slot, int count)
{
    float x, y, w, h;
    CardPixels(slot, count, x, y, w, h);
    const float rise = GameMenuUi::EaseOut(static_cast<float>(g_ChoiceTimer - (8 + slot * 6)) / 14.0f);
    if (rise <= 0.0f) return;
    const float sel = g_CardAnim[slot];
    y += (1.0f - rise) * 70.0f - 14.0f * sel;
    const float alpha = rise;
    const int module = g_Choice[slot];
    const Info& info = INFO[module];
    const bool lit = false; // (a selected card stays dark: the ink of a card on a light plate is not used any more)
    const CardInk ink = InkFor(lit);
    const bool upgrade = g_ChoiceUpgrade[slot];
    // an upgrade card is gold all over (the bar on top, the diamond round the icon): it is not a new module, it makes one that the ship has better
    const UiKit::Color verbTint = upgrade ? GOLD : VerbColor(info.verb);
    const UiKit::Color tint = lit ? UiKit::Rgba(verbTint.r * 0.50f, verbTint.g * 0.50f, verbTint.b * 0.50f, 1.0f) : verbTint;
    DrawCardFrame(x, y, w, h, sel, alpha, tint);
    // the key that picks the card, the type of the module
    UiKit::KeyCap(std::to_string(slot + 1), x + 18.0f, y + 18.0f, 26.0f,
                  Alpha(lit ? UiKit::Rgba(0.20f, 0.22f, 0.30f, 1.0f) : UiKit::Rgba(0.85f, 0.92f, 1.0f, 0.85f), alpha));
    const std::string verb = Loc::T(VERB_KEY[static_cast<int>(info.verb)]);
    const float verbPx = 13.0f;
    const float verbW = UiKit::TextWidth(verb, verbPx);
    const float verbRight = x + w - 20.0f;
    Txt(verb, verbRight, y + 28.0f, verbPx, Alpha(ink.sub, alpha), TextAlignment::Right, verbW + 30.0f);
    UiKit::Fill(verbRight - verbW, y + 38.0f, verbW, 2.0f, Alpha(tint, alpha));
    // resonance / synergy hints
    float badgeX = x + 18.0f;
    const float badgeEnd = x + w - 18.0f;
    if (upgrade)
    {
        badgeX += Chip(badgeX, y + 54.0f, 22.0f, Loc::T("UPGRADE"), 11.0f, Alpha(ink.gold, alpha), badgeEnd - badgeX) + 6.0f;
    }
    if (!upgrade && Meta::VerbCount(g_Owned, info.verb) >= 1)
    {
        badgeX += Chip(badgeX, y + 54.0f, 22.0f, Loc::T("RESONANCE"), 11.0f, Alpha(ink.gold, alpha), badgeEnd - badgeX) + 6.0f;
    }
    const int combo = Meta::ComboCompletedBy(g_Owned, module);
    if (combo >= 0 && badgeEnd - badgeX > 26.0f)
    {
        const std::string label = Meta::ComboFound(combo) ? Loc::T(Meta::COMBOS[combo].name) : std::string("?");
        Chip(badgeX, y + 54.0f, 22.0f, label, 11.0f, Alpha(ink.gold, alpha), badgeEnd - badgeX);
    }
    DrawCardIcon(module, x + w * 0.5f, y + 136.0f, tint, sel, alpha);
    if (upgrade)
    {
        // the level that the module reaches, as a gold plate on the corner of the icon: II or III
        Chip(x + w * 0.5f + 30.0f, y + 78.0f, 24.0f, g_Level[module] + 1 == 2 ? "II" : "III", 13.0f, Alpha(ink.gold, alpha), 80.0f);
    }
    const std::string name = Loc::T(info.name);
    {
        // the name in heavy letters with a hard offset in the colour of the polarity (THE SEAM)
        const float namePx = UiKit::FitFontPx(name, NAME_PX, w - 32.0f, 13.0f);
        GameMenuUi::DrawHeavyText(name, x + w * 0.5f + 2.0f, y + 232.0f + 1.6f, namePx, GameMenuUi::Accent((lit ? 0.75f : 0.55f) * alpha), TextAlignment::Center, w - 24.0f);
        GameMenuUi::DrawHeavyText(name, x + w * 0.5f, y + 232.0f, namePx, Alpha(ink.text, alpha), TextAlignment::Center, w - 24.0f);
    }
    UiKit::Fill(x + 24.0f, y + 258.0f, w - 48.0f, 1.0f, Alpha(ink.sub, 0.35f * alpha));
    // + gain (up to two) / - cost
    const float rowMaxW = w - 48.0f;
    float rowY = y + 286.0f;
    if (upgrade)
    {
        // An upgrade says what it does in numbers, like the preview of an upgraded card in Slay the Spire: the level it gives (LEVEL 1 > LEVEL 2), then the one number of the module that
        // changes, before and after (HEADLINE in ModulesData.cpp), and what it costs less. The costs of the module shrink with every level (CostAt), the line says by how much.
        const int level = g_Level[module];
        const int nextLevel = level + 1;
        const float levelY = y + 284.0f;
        const std::string from = Loc::Fmt("LEVEL {0}", level);
        const std::string to = Loc::Fmt("LEVEL {0}", nextLevel);
        const float fromW = UiKit::TextWidth(from, 15.0f);
        const float toW = UiKit::TextWidth(to, 21.0f);
        const float arrow = 38.0f;
        const float left = x + w * 0.5f - (fromW + arrow + toW) * 0.5f;
        Txt(from, left, levelY, 15.0f, Alpha(ink.sub, alpha), TextAlignment::Left, fromW + 24.0f);
        UiKit::ImageRotated(UiAtlas::Id::Chevron, left + fromW + arrow * 0.5f, levelY, 20.0f, 20.0f, 3.1415926f, Alpha(ink.gold, alpha));
        Txt(to, left + fromW + arrow, levelY, 21.0f, Alpha(ink.gold, alpha), TextAlignment::Left, toW + 24.0f);

        UiKit::Fill(left + fromW + arrow, levelY + 15.0f, toW, 2.0f, Alpha(ink.gold, alpha));
        const Id moduleId = static_cast<Id>(module);
        const Headline& headline = HEADLINE[module];
        const std::string label = Loc::T(headline.label);
        Txt(label, x + w * 0.5f, y + 322.0f, UiKit::FitFontPx(label, 13.0f, rowMaxW, 9.0f), Alpha(ink.sub, alpha), TextAlignment::Center, w - 24.0f);
        const std::string before = FormatStat(headline.fmt, HeadlineValue(moduleId, level));
        const std::string after = FormatStat(headline.fmt, HeadlineValue(moduleId, nextLevel));
        const float beforeW = UiKit::TextWidth(before, 17.0f);
        const float afterW = UiKit::TextWidth(after, 28.0f);
        const float gap = 40.0f;
        const float valueLeft = x + w * 0.5f - (beforeW + gap + afterW) * 0.5f;
        const float valueY = y + 356.0f;
        Txt(before, valueLeft, valueY, 17.0f, Alpha(ink.sub, alpha), TextAlignment::Left, beforeW + 24.0f);
        UiKit::ImageRotated(UiAtlas::Id::Chevron, valueLeft + beforeW + gap * 0.5f, valueY, 20.0f, 20.0f, 3.1415926f, Alpha(ink.gold, alpha));
        Txt(after, valueLeft + beforeW + gap, valueY, 28.0f, Alpha(ink.gainText, alpha), TextAlignment::Left, afterW + 30.0f);
        UiKit::Fill(valueLeft + beforeW + gap, valueY + 19.0f, afterW, 2.0f, Alpha(ink.gainIcon, alpha));
        if (info.cost != nullptr)
        {
            // (level 2 has no drawback at all: Balance::LEVEL_COST)
            DrawEffectRowText(ICON_PLUS, Alpha(ink.gainIcon, alpha), Loc::T("NO DRAWBACK"), Alpha(ink.gainText, alpha), x + 24.0f, y + 392.0f, rowMaxW, 13.0f);
        }
        return;
    }
    rowY += DrawEffectRow(ICON_PLUS, Alpha(ink.gainIcon, alpha), info.gain, Alpha(ink.gainText, alpha), x + 24.0f, rowY, rowMaxW, ROW_PX);
    if (info.gain2 != nullptr)
    {
        rowY += DrawEffectRow(ICON_PLUS, Alpha(ink.gainIcon, alpha), info.gain2, Alpha(ink.gainText, alpha), x + 24.0f, rowY, rowMaxW, ROW_PX);
    }
    if (info.cost != nullptr)
    {
        DrawEffectRow(ICON_MINUS, Alpha(ink.costIcon, alpha), info.cost, Alpha(ink.costText, alpha), x + 24.0f, rowY, rowMaxW, ROW_PX);
    }
}

// ---------------------------------------------------------------- route screen (after wave 2)
// what a route card says (Loc keys): the name, the icon, up to two "+" lines, one "-" line
struct RouteText
{
    const char* name;
    int icon;
    const char* gain1;
    const char* gain2;
    const char* cost;
    UiKit::Color tint;
};

const RouteText ROUTES[5] = {

    {"ELITE FLEET", ICON_ELITE, "TWO MODULE PICKS", "AN UPGRADE AMONG THE CARDS", "ELITES JOIN WAVES 3 AND 4", UiKit::Rgba(1.00f, 0.62f, 0.36f, 1.0f)},
    {"SUPPLY RUN", ICON_SUPPLY, "+30 HP AFTER THE WAVE", "A MODULE AND AN UPGRADE", "ONLY ONE NEW MODULE", UiKit::Rgba(0.55f, 1.00f, 0.72f, 1.0f)},
    {"NEW MODULE", ICON_PLUS, "PICK ONE OF THREE", nullptr, nullptr, UiKit::Rgba(0.55f, 0.86f, 1.00f, 1.0f)},
    {"FORGE", ICON_FORGE, "TWO MODULE UPGRADES", "BIGGER EFFECTS, LOWER COSTS", "NO NEW MODULE", UiKit::Rgba(1.00f, 0.80f, 0.30f, 1.0f)},
    {"WAGER", ICON_WAGER, "A MODULE AND AN UPGRADE", nullptr, "PAY 30% OF MAX HP", UiKit::Rgba(1.00f, 0.50f, 0.46f, 1.0f)}};

void DrawBranchCard(int slot)
{
    float x, y, w, h;
    CardPixels(slot, g_RouteCount, x, y, w, h);

    const float rise = GameMenuUi::EaseOut(static_cast<float>(g_BranchTimer - (8 + slot * 6)) / 14.0f);
    if (rise <= 0.0f) return;
    const int kind = g_RouteKind[slot];
    const RouteText& route = ROUTES[kind];
    const bool locked = RouteLocked(kind);
    const float sel = locked ? 0.0f : g_BranchAnim[slot];
    y += (1.0f - rise) * 70.0f - 14.0f * sel;
    const float fade = (locked ? 0.45f : 1.0f) * rise;
    const bool lit = false; // (a selected card stays dark)
    const CardInk ink = InkFor(lit);
    const UiKit::Color verbTint = route.tint;
    const UiKit::Color tint = lit ? UiKit::Rgba(verbTint.r * 0.50f, verbTint.g * 0.50f, verbTint.b * 0.50f, 1.0f) : verbTint;
    DrawCardFrame(x, y, w, h, sel, fade, tint);
    UiKit::KeyCap(std::to_string(slot + 1), x + 18.0f, y + 18.0f, 26.0f,
                  Alpha(lit ? UiKit::Rgba(0.20f, 0.22f, 0.30f, 1.0f) : UiKit::Rgba(0.85f, 0.92f, 1.0f, 0.85f), fade));
    const std::string routeLabel = Loc::T("ROUTE");
    const float routeW = UiKit::TextWidth(routeLabel, 13.0f);
    const float routeRight = x + w - 20.0f;
    Txt(routeLabel, routeRight, y + 28.0f, 13.0f, Alpha(ink.sub, fade), TextAlignment::Right, routeW + 30.0f);
    UiKit::Fill(routeRight - routeW, y + 38.0f, routeW, 2.0f, Alpha(tint, fade));
    // a locked card says why
    if (locked)
    {
        const char* why = "NOT ENOUGH HP";
        if (kind == ROUTE_SUPPLY)
            why = g_BranchMessage > 0 ? "NEEDS DAMAGE" : "FULL HP";
        else if (kind == ROUTE_FORGE || !AnyUpgradable())
            why = "NOTHING TO UPGRADE";

        Chip(x + 18.0f, y + 54.0f, 22.0f, Loc::T(why), 11.0f, Alpha(g_BranchMessage > 0 ? DANGER_RED : DIM, rise), w - 36.0f);
    }
    DrawCardIcon(route.icon, x + w * 0.5f, y + 136.0f, tint, sel, fade);
    const std::string name = Loc::T(route.name);
    {
        const float namePx = UiKit::FitFontPx(name, NAME_PX, w - 32.0f, 13.0f);
        GameMenuUi::DrawHeavyText(name, x + w * 0.5f + 2.0f, y + 232.0f + 1.6f, namePx, GameMenuUi::Accent((lit ? 0.75f : 0.55f) * fade), TextAlignment::Center, w - 24.0f);
        GameMenuUi::DrawHeavyText(name, x + w * 0.5f, y + 232.0f, namePx, Alpha(ink.text, fade), TextAlignment::Center, w - 24.0f);
    }
    UiKit::Fill(x + 24.0f, y + 258.0f, w - 48.0f, 1.0f, Alpha(ink.sub, 0.35f * fade));
    const float rowMaxW = w - 48.0f;
    float rowY = y + 286.0f;
    const char* gains[2] = {route.gain1, route.gain2};
    for (const char* key : gains)
    {
        if (key == nullptr) continue;
        rowY += DrawEffectRow(ICON_PLUS, Alpha(ink.gainIcon, fade), key, Alpha(ink.gainText, fade), x + 24.0f, rowY, rowMaxW, ROW_PX);
    }
    if (route.cost != nullptr)
    {
        DrawEffectRow(ICON_MINUS, Alpha(ink.costIcon, fade), route.cost, Alpha(ink.costText, fade), x + 24.0f, rowY, rowMaxW, ROW_PX);
    }
}

// banner (top left, slides in from the left): what happened, what to do. The same banner as every other screen.
void DrawPickBanner(const std::string& big, const std::string& sub)
{
    // the WAVE CLEAR card has just docked here (ui/CutIn): the header is there already, no second entrance
    GameMenuUi::DrawHeader(big, sub, CutIn::Docked() ? 1.0f : static_cast<float>(g_SlowT) / 12.0f);
}

// the game stays visible between two black corners (not the red / blue field of the title)
void DrawPickBackground()
{
    Renderer2D_ClearCameraOffset();
    const float f = GameMenuUi::Clamp01(static_cast<float>(g_SlowT) / 20.0f);
    Renderer2D_DrawQuad(-1.00f, 1.00f, 1.00f, -1.00f, 0.00f, 0.005f, 0.025f, 0.40f * f);
    GameMenuUi::DrawStage(GameMenuUi::Stage::Pick, 0.55f * f);
}

void DrawBranch()
{
    DrawPickBackground();
    DrawPickBanner(Loc::T("WAVE CLEAR"), Loc::T(g_RouteStage == 2 ? "CHOOSE YOUR REWARD" : "CHOOSE YOUR ROUTE"));
    for (int i = 0; i < g_RouteCount; ++i)
    {
        DrawBranchCard(i);
    }
    const std::string hint = Loc::T(g_RouteCount >= 3 ? "PRESS 1 2 3 OR CLICK" : "PRESS 1 2 OR CLICK");
    Txt(hint, 640.0f, 676.0f, UiKit::FitFontPx(hint, 13.0f, 900.0f), UiKit::Rgba(0.56f, 0.64f, 0.78f, 0.90f), TextAlignment::Center, 1000.0f);
}

// The reroll button under the cards. The same grammar as the cards and the menu rows: a dark plate with a hairline at rest, an ivory plate on a red offset when it is
// highlighted (the mouse points at it, or DOWN moved the highlight onto it). The key is on its left, one diamond on its right for every reroll of the run (filled while
// it is left, an outline once it is used). Pressed with none left it shakes, and the line of help under it says why.
void DrawRerollButton()
{
    const float rise = GameMenuUi::EaseOut(static_cast<float>(g_ChoiceTimer - (8 + g_ChoiceCount * 6 + 6)) / 14.0f);
    if (rise <= 0.0f) return;
    const bool can = g_RerollsLeft > 0;
    const float hot = g_RerollAnim;
    float x = 640.0f - REROLL_W * 0.5f;
    if (g_RerollShake > 0) x += std::sin(static_cast<float>(g_RerollShake) * 1.9f) * 6.0f * static_cast<float>(g_RerollShake) / 16.0f;
    const float y = REROLL_Y + (1.0f - rise) * 36.0f - 5.0f * hot;
    const float a = rise;
    const UiKit::Color ivory = UiKit::Rgba(0.93f, 0.92f, 0.88f, 0.98f);
    if (hot > 0.02f)
    {
        GameMenuUi::Plate(x + 9.0f * hot, y + 7.0f * hot, REROLL_W, REROLL_H, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.60f * hot * a));
        GameMenuUi::Plate(x + 5.0f * hot, y + 4.0f * hot, REROLL_W, REROLL_H, GameMenuUi::Accent(hot * a));
    }
    GameMenuUi::Plate(x, y, REROLL_W, REROLL_H, Alpha(Mix(UiKit::Rgba(0.020f, 0.028f, 0.050f, can ? 0.88f : 0.55f), ivory, hot), a));
    GameMenuUi::PlateLine(x, y, REROLL_W, REROLL_H, UiKit::Rgba(1.0f, 1.0f, 1.0f, (can ? 0.30f : 0.14f) * (1.0f - hot) * a));
    if (hot > 0.02f) GameMenuUi::SeamDiamond(x - 16.0f, y + REROLL_H * 0.5f, 7.0f, hot * a);
    const bool lit = hot > 0.5f;
    const float cy = y + REROLL_H * 0.5f;
    const float capH = 26.0f;
    const float capW = UiKit::KeyCap("R", x + 22.0f, cy - capH * 0.5f, capH,
                                     Alpha(lit ? UiKit::Rgba(0.20f, 0.22f, 0.30f, 1.0f) : UiKit::Rgba(0.85f, 0.92f, 1.0f, can ? 0.85f : 0.35f), a));
    const std::string label = Loc::T("REROLL");
    const float labelX = x + 22.0f + capW + 16.0f;
    Txt(label, labelX, cy, UiKit::FitFontPx(label, 16.0f, REROLL_W - (labelX - x) - 56.0f, 10.0f), Alpha(lit ? INK : (can ? WHITE : DIM), a * (can ? 1.0f : 0.60f)),
        TextAlignment::Left, REROLL_W);
    const int total = Balance::REROLLS_PER_RUN;
    const UiKit::Color gold = lit ? GOLD_ON_LIGHT : GOLD;
    for (int i = 0; i < total; ++i)
    {
        const float px = x + REROLL_W - 28.0f - static_cast<float>(total - 1 - i) * 20.0f;
        if (i < g_RerollsLeft)
            GameMenuUi::DiamondFill(px, cy, 6.0f, Alpha(gold, a));
        else
            GameMenuUi::DiamondOutline(px, cy, 6.0f, 1.5f, Alpha(lit ? INK : DIM, 0.55f * a));
    }
}

void DrawChoice()
{
    if (g_BranchOpen)
    {
        DrawBranch();
        return;
    }
    DrawPickBackground();
    DrawPickBanner(Loc::T(GameWave::IsWaveCleared() ? "WAVE CLEAR" : "NEW RUN"), Loc::T(g_ForgeNext ? "UPGRADE A MODULE" : "CHOOSE A MODULE")); // (the first pick comes before the first wave)
    for (int i = 0; i < g_ChoiceCount; ++i)
    {
        DrawCard(i, g_ChoiceCount);
    }
    DrawRerollButton();
    // the modules the ship already has, bottom left (they stop short of the button)
    if (g_OrderCount > 0)
    {
        const float size = 26.0f;
        const std::string label = Loc::T("MODULES");
        const float labelW = UiKit::TextWidth(label, 12.0f);
        Txt(label, 84.0f, 640.0f, 12.0f, DIM, TextAlignment::Left, labelW + 30.0f);
        const float first = 84.0f + labelW + 16.0f;
        const float room = 640.0f - REROLL_W * 0.5f - 24.0f - first;
        const float step = (std::min)(size + 10.0f, room / static_cast<float>(g_OrderCount));
        float x = first;
        for (int i = 0; i < g_OrderCount; ++i)
        {
            DrawIcon(g_Order[i], x, 627.0f, size, VerbColor(INFO[g_Order[i]].verb));
            x += step;
        }
    }
    // one line of help at the bottom: how to pick, or what the reroll button does
    const bool onButton = MouseOnReroll() || g_RerollFocus;
    const char* hintKey = "PRESS 1 2 3 OR CLICK";
    if (g_RerollShake > 0 || (onButton && g_RerollsLeft <= 0))
        hintKey = "NO REROLL LEFT IN THIS RUN";
    else if (onButton)
        hintKey = "NEW CARDS. THE ONES ON OFFER DO NOT COME BACK";
    const std::string hint = Loc::T(hintKey);
    Txt(hint, 640.0f, 694.0f, UiKit::FitFontPx(hint, 13.0f, 900.0f), (g_RerollShake > 0) ? DANGER_RED : UiKit::Rgba(0.56f, 0.64f, 0.78f, 0.90f), TextAlignment::Center, 1000.0f);
}

// energy motes (bullets turned into energy) on their way to the ship
void DrawMotes()
{
    for (const Mote& m : g_Motes)
    {
        if (!m.active) continue;
        float r, g, b;
        PolarityRgb(m.polarity, r, g, b);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, m.x, m.y, 0.040f, r * 0.5f + 0.5f, g * 0.5f + 0.5f, b * 0.5f + 0.35f, 0.65f);
        GameVfx::DrawRound(GameVfx::Sprite::Glow, m.x, m.y, 0.014f, 1.0f, 1.0f, 1.0f, 0.95f);
    }
}
}





