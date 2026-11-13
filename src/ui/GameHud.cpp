#include "ui/GameHud.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "boss/GameBoss.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "core/Loc.h"
#include "ui/CutIn.h"
#include "ui/UiKit.h"
#include "ui/GameMenuUi.h"
#include "core/Renderer2D.h"

#include <algorithm>
#include <cmath>
#include <string>

using UiAtlas::Id;
using UiKit::Color;
using UiKit::Rgba;

namespace
{
// ------------------------------------------------------------------ palette (flat glass)
const Color PANEL_LINE = Rgba(1.00f, 1.00f, 1.00f, 0.20f);
const Color TRACK = Rgba(1.00f, 1.00f, 1.00f, 0.14f);
const Color TEXT_DIM = Rgba(0.62f, 0.70f, 0.82f, 0.92f);
const Color TEXT_BRIGHT = Rgba(0.97f, 0.98f, 1.00f, 1.00f);
const Color GOLD = Rgba(1.00f, 0.80f, 0.30f, 1.00f);
const Color RED = Rgba(1.00f, 0.28f, 0.24f, 1.00f);
const Color BLUE = Rgba(0.20f, 0.60f, 1.00f, 1.00f);
const Color IVORY = Rgba(0.93f, 0.92f, 0.88f, 1.00f);
const Color INK = Rgba(0.05f, 0.06f, 0.09f, 1.00f);
const Color WHITE = Rgba(1.00f, 1.00f, 1.00f, 1.00f);

constexpr float PI = 3.1415926535f;

// The big banner in the middle of the screen when a boss phase begins ("PHASE 1 / AWAKENING / BREAK THE OVERLOAD = DAMAGE WINDOW"). It covers the
// fight, so it is off. Set to true to get it back.

Color PolarityColor(Polarity p)
{
    return p == Polarity::Red ? RED : BLUE;
}
Color Mul(Color c, float a)
{
    c.a *= a;
    return c;
}
Color Mix(Color a, Color b, float t)
{
    return Rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}
float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float g_IntroT = 1000.0f; // update steps since the run started (the HUD slides in over the first 30)

// 0..1 once `delay` update steps of the intro have passed (eased out)
float Intro(float delay, float duration)
{
    const float i = 1.0f - Clamp01((g_IntroT - delay) / duration);
    return 1.0f - i * i * i;
}

// ------------------------------------------------------------------ state
enum FadeRect
{
    FadeScore,
    FadeCluster,
    FadeBoss,
    FadeCombo,
    FadeCount
};

struct Rect
{
    float x, y, w, h;
};
const Rect FADE_RECTS[FadeCount] = {{30.0f, 16.0f, 250.0f, 76.0f},
                                    {24.0f, 628.0f, 330.0f, 72.0f},
                                    {354.0f, 8.0f, 572.0f, 64.0f},
                                    {1110.0f, 16.0f, 150.0f, 76.0f}};

int g_DrawTick = 0;
bool g_HavePrev = false;
int g_PrevEnergy = 0;
int g_PrevHp = 0;
int g_PrevScore = 0;
Polarity g_PrevPolarity = Polarity::Blue;
float g_ScoreShown = 0.0f;
float g_EnergyShown = 0.0f;
int g_ScorePop = 0;
int g_EnergyFlash = 0;
int g_HpLostFlash = 0;
int g_HpLostAmount = 0; // what the last hit took (the -N tag next to the HP)
int g_HpLostIndex = -1;
float g_HpGhost = 0.0f; // the chunk of HP that was just lost stays as a ghost and drains away
int g_ReadyTick = 0;
float g_ComboAlpha = 0.0f;
int g_LastCombo = 0;
int g_LastMultiplier = 1;
float g_Fade[FadeCount] = {1.0f, 1.0f, 1.0f, 1.0f};

// tutorial
int g_TutStep = 0; // 0 move/shoot, 1 colour, 2 special, 3 done
int g_TutTimer = 0;
int g_Absorbs = 0;
int g_Switches = 0;
bool g_TutReady = false;
bool g_WrongShown = false;
int g_WrongTimer = 0;

constexpr int HINT_MOVE_START = 90;
constexpr int HINT_MOVE_END = 510;
constexpr int WRONG_HINT_FRAMES = 200;

enum HintId
{
    HintNone,
    HintMove,
    HintColor,
    HintSpecial,
    HintWrong
};
int g_LastHint = HintNone;

int ActiveHint()
{
    if (g_WrongTimer > 0) return HintWrong;
    if (Loc::IsTutorialDone()) return HintNone;
    if (g_TutStep == 0 && g_TutTimer >= HINT_MOVE_START && g_TutTimer <= HINT_MOVE_END) return HintMove;
    if (g_TutStep == 1) return HintColor;
    if (g_TutStep == 2 && g_TutReady) return HintSpecial;
    return HintNone;
}

// ------------------------------------------------------------------ update helpers
void UpdateTutorial(const HudState& s, bool absorbed, bool damaged, bool switched, bool ready, bool special)
{
    if (Loc::IsTutorialDone())
    {
        g_TutStep = 3;
        return;
    }
    if (s.bossActive) return;

    ++g_TutTimer;
    if (switched) ++g_Switches;
    if (absorbed) ++g_Absorbs;

    switch (g_TutStep)
    {
    case 0:
        if (g_TutTimer > HINT_MOVE_END)
        {
            g_TutStep = 1;
            g_TutTimer = 0;
        }
        break;
    case 1:
        if ((g_Switches >= 1 && g_Absorbs >= 2 && g_TutTimer > 300) || g_TutTimer > 2400)
        {
            g_TutStep = 2;
            g_TutTimer = 0;
        }
        break;
    case 2:
        if (ready && !g_TutReady)
        {
            g_TutReady = true;
            g_TutTimer = 0;
        }
        if (g_TutReady && (special || g_TutTimer > 900))
        {
            g_TutStep = 3;
            Loc::SetTutorialDone();
        }
        break;
    default:
        break;
    }

    if (damaged && g_Absorbs == 0 && !g_WrongShown && g_TutStep <= 1)
    {
        g_WrongShown = true;
        g_WrongTimer = WRONG_HINT_FRAMES;
    }
    if (g_WrongTimer > 0) --g_WrongTimer;
}

float FadeTarget(const HudState& s, FadeRect which)
{
    const Rect r = FADE_RECTS[which];
    const float px = (s.playerX + 1.0f) * 640.0f;
    const float py = (1.0f - s.playerY) * 360.0f;
    const float margin = 70.0f;
    const bool inside = px > r.x - margin && px < r.x + r.w + margin && py > r.y - margin && py < r.y + r.h + margin;
    return inside ? 0.18f : 1.0f; // (the ship near a corner: the HUD there all but disappears)
}

// ------------------------------------------------------------------ draw helpers
// HUD ink (NieR-like: the HUD is part of the world, not on top of it). Thin ivory lines and small text at low opacity, no panels;
// the only colour is a small mark of the ship's polarity. Everything here must stay quieter than any bullet.
const Color INKL = Rgba(0.90f, 0.89f, 0.84f, 1.0f); // the ivory of the HUD
constexpr float HUD_A = 0.62f;                       // its overall opacity

// a thin bar: a hairline track and a fill a few px high; `ghost` (0 = none) is the part just lost
void ThinBar(float x, float y, float w, float h, float rate, float ghost, Color fill, float alpha)
{
    UiKit::Fill(x, y + h - 1.0f, w, 1.0f, Mul(INKL, 0.22f * alpha));
    if (ghost > rate) UiKit::Fill(x + w * rate, y, w * (ghost - rate), h, Mul(INKL, 0.40f * alpha));
    UiKit::Fill(x, y, w * Clamp01(rate), h, Mul(fill, alpha));
}

// small ticks under a bar (every 1 / n)
void Ticks(float x, float y, float w, int n, float alpha)
{
    for (int i = 0; i <= n; ++i) UiKit::Fill(x + w * static_cast<float>(i) / static_cast<float>(n), y, 1.0f, 3.0f, Mul(INKL, 0.30f * alpha));
}

void DrawPill(float x, float y, float w, float h, float rate, Color fill, float alpha)
{
    ThinBar(x, y, w, std::min(h, 3.0f), Clamp01(rate), 0.0f, fill, alpha);
}

void DrawScore(const HudState& s, float alpha)
{
    const float a = alpha * HUD_A;
    const float x = 36.0f, y = 26.0f;
    // the polarity: a small diamond (red) / circle (blue), the only colour in the corner
    if (s.polarity == Polarity::Red)
        UiKit::Image(Id::Diamond, x, y + 7.0f, 10.0f, 10.0f, Mul(RED, 0.85f * alpha));
    else
        UiKit::Image(Id::PipOn, x + 1.0f, y + 8.0f, 8.0f, 8.0f, Mul(BLUE, 0.85f * alpha));
    const float pop = static_cast<float>(g_ScorePop) / 10.0f;
    // THE SEAM, quietly: heavy digits with a small hard offset in the colour of the ship (P5), a thin seam rule under them (P3)
    const std::string score = std::to_string(static_cast<int>(g_ScoreShown + 0.5f));
    GameMenuUi::DrawHeavyText(score, x + 19.5f, y + 13.2f, 20.0f, Mul(PolarityColor(s.polarity), 0.75f * a), TextAlignment::Left, 260.0f);
    GameMenuUi::DrawHeavyText(score, x + 18.0f, y + 12.0f, 20.0f, Mul(Mix(INKL, GOLD, Clamp01(pop) * 0.6f), a), TextAlignment::Left, 260.0f);
    GameMenuUi::SeamRule(x + 18.0f, y + 26.0f, 96.0f, 1.0f, 0.55f * a);
    UiKit::Text(Loc::T("SCORE"), x + 18.0f, y + 38.0f, 9.0f, Mul(INKL, 0.6f * a), TextAlignment::Left, 100.0f, false);
}

void DrawWaveLabel(const HudState& s, float alpha)
{
    const float a = alpha * HUD_A;
    const int total = std::max(1, s.waveTotal);
    const float cellW = 14.0f, gap = 5.0f;
    const float barW = cellW * static_cast<float>(total) + gap * static_cast<float>(total - 1);
    const float x0 = 640.0f - barW * 0.5f;
    UiKit::Text(Loc::Fmt("WAVE {0}", s.wave), 640.0f, 24.0f, 10.0f, Mul(INKL, 0.8f * a), TextAlignment::Center, 300.0f, false);
    for (int i = 0; i < total; ++i)
    {
        const float px = x0 + (cellW + gap) * static_cast<float>(i);
        const bool reached = (i + 1) <= s.wave;
        const Color c = i == total - 1 ? Mul(RED, 0.75f) : (reached ? INKL : Mul(INKL, 0.25f));
        UiKit::Fill(px, 36.0f, cellW, 2.0f, Mul(c, a));
    }
}

void DrawCluster(const HudState& s, float alpha)
{
    const float a = alpha * HUD_A;
    const float x = 36.0f, y = 650.0f;
    const bool ready = s.energy >= s.maxEnergy;
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(g_ReadyTick) * 0.18f);

    // HP: a thin ivory bar (red when low), ticks every 10, the number small at its end
    {
        const float hw = 220.0f, hh = 4.0f;
        const float maxHp = static_cast<float>(std::max(1, s.maxHp));
        const float now = Clamp01(static_cast<float>(s.hp) / maxHp);
        const float ghost = Clamp01(g_HpGhost / maxHp);
        const Color c = now < 0.30f ? Mix(RED, Rgba(1.0f, 0.62f, 0.50f, 1.0f), pulse) : INKL;
        const float ca = now < 0.30f ? alpha : a; // (low HP: the bar is allowed to speak up)
        ThinBar(x, y, hw, hh, now, ghost, Mul(c, ca / std::max(a, 0.001f)), a);
        Ticks(x, y + hh + 2.0f, hw, std::max(1, s.maxHp / 10), a);
        UiKit::Text(std::to_string(std::max(0, s.hp)), x + hw + 10.0f, y + 2.0f, 14.0f, Mul(c, ca), TextAlignment::Left, 60.0f, false);
        UiKit::Text(Loc::T("HP"), x, y - 10.0f, 9.0f, Mul(INKL, 0.6f * a), TextAlignment::Left, 60.0f, false);
        if (g_HpLostFlash > 0 && g_HpLostAmount > 0) // the hit: a red tag slams in next to the number and fades (only while it matters)
        {
            const float k = static_cast<float>(g_HpLostFlash) / 24.0f;
            const std::string lost = "-" + std::to_string(g_HpLostAmount);
            const float tx = x + hw + 52.0f - (1.0f - std::min(1.0f, (1.0f - k) * 6.0f)) * 14.0f;
            const float tw = UiKit::TextWidth(lost, 13.0f) + 16.0f;
            const Renderer2DVertex v[4] = {{UiKit::NX(tx + 5.0f), UiKit::NY(y - 7.0f), RED.r, RED.g, RED.b, 0.95f * k * alpha},
                                           {UiKit::NX(tx + tw + 5.0f), UiKit::NY(y - 7.0f), RED.r, RED.g, RED.b, 0.95f * k * alpha},
                                           {UiKit::NX(tx + tw), UiKit::NY(y + 11.0f), RED.r, RED.g, RED.b, 0.95f * k * alpha},
                                           {UiKit::NX(tx), UiKit::NY(y + 11.0f), RED.r, RED.g, RED.b, 0.95f * k * alpha}};
            Renderer2D_DrawPolygon(v, 4);
            GameMenuUi::DrawHeavyText(lost, tx + 9.0f, y + 2.0f, 13.0f, Rgba(1.0f, 1.0f, 1.0f, k * alpha), TextAlignment::Left, 80.0f);
        }
    }

    // energy: thinner, in the ship's colour (dimmed); gold when the special is ready - that one is allowed to glow
    {
        const float ey = y + 30.0f, ew = 220.0f;
        Color c = ready ? Mix(GOLD, Rgba(1.0f, 0.93f, 0.70f, 1.0f), pulse) : Mix(PolarityColor(s.polarity), INKL, 0.35f);
        if (g_EnergyFlash > 0) c = Mix(c, WHITE, static_cast<float>(g_EnergyFlash) / 14.0f);
        ThinBar(x, ey, ew, 2.0f, Clamp01(g_EnergyShown / static_cast<float>(std::max(1, s.maxEnergy))), 0.0f, c, ready ? alpha : a);
        Ticks(x, ey + 4.0f, ew, 10, a);
        // OVERFLOW: the bullets that a full bar has stored for the next beam: small gold diamonds under the bar (they used to hang under the ship)
        {
            const int stored = Mod::OverflowStored();
            const int most = std::max(1, Mod::OverflowStoredMax());
            for (int i = 0; i < stored; ++i)
                GameMenuUi::DiamondFill(x + 4.0f + (ew - 8.0f) * static_cast<float>(i) / static_cast<float>(most - 1 > 0 ? most - 1 : 1), ey + 13.0f, 2.6f,
                                        Mul(GOLD, 0.85f * alpha));
        }
        if (ready)
        {
            const std::string label = Loc::T("READY");
            const float tw = UiKit::TextWidth(label, 11.0f) + 16.0f;
            const float ga = (0.75f + 0.25f * pulse) * alpha;
            const Renderer2DVertex v[4] = {{UiKit::NX(x + ew + 15.0f), UiKit::NY(ey - 8.0f), GOLD.r, GOLD.g, GOLD.b, ga},
                                           {UiKit::NX(x + ew + 15.0f + tw), UiKit::NY(ey - 8.0f), GOLD.r, GOLD.g, GOLD.b, ga},
                                           {UiKit::NX(x + ew + 10.0f + tw), UiKit::NY(ey + 9.0f), GOLD.r, GOLD.g, GOLD.b, ga},
                                           {UiKit::NX(x + ew + 10.0f), UiKit::NY(ey + 9.0f), GOLD.r, GOLD.g, GOLD.b, ga}};
            Renderer2D_DrawPolygon(v, 4);
            GameMenuUi::DrawHeavyText(label, x + ew + 19.0f, ey + 1.0f, 11.0f, Rgba(0.02f, 0.02f, 0.03f, ga), TextAlignment::Left, 100.0f);
        }
        if (s.chain < 0.95f && !ready)
        {
            const float shown = Clamp01((0.95f - s.chain) * 4.0f);
            UiKit::Text(Loc::Fmt("CHAIN {0}%", static_cast<int>(s.chain * 100.0f + 0.5f)), x, ey + 16.0f, 9.0f, Mul(INKL, 0.6f * shown * a),
                        TextAlignment::Left, 140.0f, false);
        }
        const Color keyTint = ready ? Mix(GOLD, WHITE, pulse * 0.4f) : Rgba(0.75f, 0.82f, 0.95f, 0.22f);
        UiKit::KeyCap("V", x + ew + (ready ? UiKit::TextWidth(Loc::T("READY"), 11.0f) + 40.0f : 12.0f), ey - 9.0f, 20.0f, Mul(keyTint, ready ? alpha : a));
    }
}

void DrawCombo(const HudState& s, float alpha)
{
    (void)s;
    if (g_ComboAlpha <= 0.01f) return;
    const float a = g_ComboAlpha * alpha * HUD_A;
    const float right = 1244.0f;
    UiKit::Text(std::to_string(g_LastCombo), right - 40.0f, 38.0f, 20.0f, Mul(INKL, a), TextAlignment::Right, 160.0f, false);
    UiKit::Text(std::string("x") + std::to_string(g_LastMultiplier), right, 38.0f, 14.0f, Mul(GOLD, 0.85f * a), TextAlignment::Right, 50.0f, false);
    UiKit::Fill(right - 140.0f, 52.0f, 140.0f * Clamp01(s.comboRate), 1.0f, Mul(INKL, 0.8f * a));
    UiKit::Text(Loc::T("STREAK"), right, 64.0f, 9.0f, Mul(INKL, 0.6f * a), TextAlignment::Right, 100.0f, false);
}

// ------------------------------------------------------------------ boss bar
Color SegmentColor(int index, const BossHudInfo& info)
{
    if (index == 2 && info.berserk)
    {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(g_DrawTick) * 0.3f);
        return Mix(RED, Rgba(1.0f, 0.62f, 0.50f, 1.0f), pulse);
    }
    return WHITE;
}

void DrawSegments(const BossHudInfo& info, float x, float y, float w, float h, float alpha)
{
    const float gap = 6.0f;
    const float usable = w - 2.0f * gap;
    float sx = x;
    for (int i = 0; i < 3; ++i)
    {
        // the three segments are the life of the BODY of the boss, one per phase (the drones have their own bars, DrawDroneBars)
        const float segW = usable * info.segmentWeight[i];
        const bool reached = info.phase >= i + 1;
        ThinBar(sx, y, segW, h, Clamp01(info.segmentRate[i]), 0.0f, Mul(SegmentColor(i, info), reached ? 1.0f : 0.35f), alpha);
        sx += segW + gap;
    }
}

// phase 2 and 3: the two drones under the bar of the body (the colour drone red, the white drone white). A drone that is down shows how soon it is back (a dim bar that fills up).
// Phase 3: while the shell of the core is closed, a chevron points at each drone that is still up (shoot those).
void DrawDroneBars(const BossHudInfo& info, float x, float y, float w, float alpha)
{
    if (info.phase < 2 || info.transition) return;
    const float bw = 150.0f, bh = 6.0f;
    for (int s = 0; s < 2; ++s)
    {
        const float bx = s == 0 ? x + w * 0.5f - bw - 10.0f : x + w * 0.5f + 10.0f;
        const Color tint = s == 0 ? Mul(RED, 0.8f) : INKL;
        if (info.shipAlive[s])
            DrawPill(bx, y, bw, bh, info.shipRate[s], tint, alpha);
        else
            DrawPill(bx, y, bw, bh, info.shipRespawn[s], tint, 0.28f * alpha);
        if (info.shellClosed && info.shipAlive[s])
        {
            UiKit::ImageRotated(Id::Chevron, s == 0 ? bx - 10.0f : bx + bw + 10.0f, y + bh * 0.5f, 12.0f, 12.0f, s == 0 ? 0.0f : PI, Mul(GOLD, alpha));
        }
    }
}

// The overload of the boss is charging: the edges of the screen light up, the word says what it is, and the line under it says what to do. The energy bar is
// read off the HUD state, so the cue also says "not yet" when the bar is not full: then there is nothing to do but to get out of the way (the overload takes a third of the HP).
void DrawOverloadCue(float rate, bool hint, const HudState& s, float alpha)
{
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(g_DrawTick) * 0.35f);
    const bool ready = s.energy >= s.maxEnergy;
    const float edge = (0.10f + 0.26f * rate) * (0.65f + 0.35f * pulse) * alpha;
    UiKit::Fill(0.0f, 0.0f, 1280.0f, 7.0f, Mul(GOLD, edge));
    UiKit::Fill(0.0f, 713.0f, 1280.0f, 7.0f, Mul(GOLD, edge));
    UiKit::Fill(0.0f, 0.0f, 5.0f, 720.0f, Mul(GOLD, edge * 0.7f));
    UiKit::Fill(1275.0f, 0.0f, 5.0f, 720.0f, Mul(GOLD, edge * 0.7f));
    const float y = 104.0f;
    UiKit::Text(Loc::T("OVERLOAD"), 640.0f, y, 22.0f, Mul(GOLD, (0.80f + 0.20f * pulse) * alpha), TextAlignment::Center, 760.0f, false);
    if (!ready)
    {
        UiKit::Text(Loc::T("ENERGY NOT FULL: DODGE IT"), 640.0f, y + 24.0f, 14.0f, Mul(TEXT_BRIGHT, 0.95f * alpha), TextAlignment::Center, 760.0f, false);
    }
    else if (hint)
    {
        UiKit::Text(GameInput::RebindText(Loc::T("ENERGY FULL = PRESS V")), 640.0f, y + 24.0f, 14.0f, Mul(TEXT_BRIGHT, 0.95f * alpha), TextAlignment::Center, 760.0f, false);
    }
    UiKit::Fill(640.0f - 110.0f, y + 36.0f, 220.0f * Clamp01(1.0f - rate), 4.0f, Mul(GOLD, alpha));
}

void DrawBossBar(const BossHudInfo& info, float alpha)
{
    const float a = alpha * HUD_A;
    const float x = 400.0f, y = 40.0f, w = 480.0f, h = 3.0f;
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(g_DrawTick) * 0.3f);
    DrawSegments(info, x, y, w, h, a);
    GameMenuUi::DrawHeavyText(Loc::T("ARBITER"), x, y - 12.0f, 12.0f, Mul(INKL, a), TextAlignment::Left, 240.0f);
    GameMenuUi::SeamDiamond(x - 10.0f, y + 1.5f, 4.0f, a);
    UiKit::Text(Loc::Fmt("PHASE {0}", info.phase), x + w, y - 12.0f, 10.0f, Mul(INKL, 0.6f * a), TextAlignment::Right, 200.0f, false);
    DrawDroneBars(info, x, y + h + 8.0f, w, a);
    const float noteY = y + h + (info.phase >= 2 ? 26.0f : 16.0f);
    if (info.overheated && !info.overload)
        UiKit::Text(Loc::T("BROKEN"), 640.0f, noteY, 12.0f, Mul(GOLD, (0.60f + 0.40f * pulse) * alpha), TextAlignment::Center, 300.0f, false);
    else if (info.shellClosed)
        UiKit::Text(Loc::T("SHELL CLOSED: SHOOT THE DRONES"), 640.0f, noteY, 11.0f, Mul(INKL, 0.7f * a), TextAlignment::Center, 420.0f, false);
}

// ------------------------------------------------------------------ pause button (mouse only)
constexpr float PAUSE_BTN_X = 1214.0f;
constexpr float PAUSE_BTN_Y = 104.0f;
constexpr float PAUSE_BTN_SIZE = 40.0f;

bool OverPauseButton()
{
    if (!GameInput::MouseSteering() || !GameInput::IsMouseInsideWindow()) return false;
    const float px = (GameInput::GetMouseX() + 1.0f) * 640.0f;
    const float py = (1.0f - GameInput::GetMouseY()) * 360.0f;
    const float m = 8.0f; // a little forgiving: the cursor is invisible during play
    return px >= PAUSE_BTN_X - m && px <= PAUSE_BTN_X + PAUSE_BTN_SIZE + m && py >= PAUSE_BTN_Y - m && py <= PAUSE_BTN_Y + PAUSE_BTN_SIZE + m;
}

void DrawPauseButton()
{
    if (!GameInput::MouseSteering()) return;
    const bool hot = OverPauseButton();
    const float x = PAUSE_BTN_X, y = PAUSE_BTN_Y, s = PAUSE_BTN_SIZE;
    UiKit::Panel9(Id::BarBack, x, y, s, s, 8.0f, hot ? Rgba(0.93f, 0.92f, 0.88f, 0.96f) : Rgba(0.0f, 0.0f, 0.0f, 0.45f));
    UiKit::Panel9(Id::BarFrame, x, y, s, s, 8.0f, hot ? Rgba(0.88f, 0.17f, 0.15f, 1.0f) : Mul(IVORY, 0.35f));
    const Color bar = hot ? INK : Mul(IVORY, 0.90f);
    UiKit::Fill(x + 13.0f, y + 11.0f, 5.0f, 18.0f, bar);
    UiKit::Fill(x + 22.0f, y + 11.0f, 5.0f, 18.0f, bar);
}

// ------------------------------------------------------------------ hints
struct HintGroup
{
    const char* key;
    const char* text;
};

float DrawHintGroup(const HintGroup& g, float x, float y, float alpha, bool measureOnly)
{
    const std::string label = Loc::T(g.text);
    const std::string key = GameInput::PromptLabel(g.key); // "C" on the keyboard, "X" on the gamepad
    const float keyH = 28.0f;
    const float keyW = key.size() > 1 ? std::max(keyH * 1.6f, UiKit::TextWidth(key, keyH * 0.46f) + keyH * 0.7f) : keyH;
    const float textW = UiKit::TextWidth(label, 15.0f);
    if (!measureOnly)
    {
        UiKit::KeyCap(key, x, y - keyH * 0.5f, keyH, Mul(Rgba(0.85f, 0.92f, 1.0f, 1.0f), alpha));
        UiKit::Text(label, x + keyW + 10.0f, y, 15.0f, Mul(TEXT_BRIGHT, alpha), TextAlignment::Left, textW + 40.0f, false);
    }
    return keyW + 10.0f + textW;
}

void DrawHint(const HintGroup* groups, int count, const char* subtitle, float alpha, bool warn)
{
    if (alpha <= 0.01f) return;
    const float cy = 96.0f;

    float total = 0.0f;
    for (int i = 0; i < count; ++i)
        total += DrawHintGroup(groups[i], 0, 0, 0, true) + (i > 0 ? 36.0f : 0.0f);
    const float subW = subtitle ? UiKit::TextWidth(Loc::T(subtitle), 13.0f) : 0.0f;
    const float panelW = std::max(total, subW) + 48.0f;
    const float panelH = subtitle ? 78.0f : 50.0f;
    const float px = 640.0f - panelW * 0.5f;
    const float py = cy - 25.0f;
    UiKit::Panel9(Id::PanelFill, px, py, panelW, panelH, 16.0f, Mul(Rgba(0.030f, 0.048f, 0.100f, 0.62f), alpha));
    UiKit::Panel9(Id::PanelLine, px, py, panelW, panelH, 16.0f, Mul(warn ? Mul(RED, 0.45f) : PANEL_LINE, alpha));

    float x = 640.0f - total * 0.5f;
    for (int i = 0; i < count; ++i)
    {
        x += DrawHintGroup(groups[i], x, cy, alpha, false) + 36.0f;
    }
    if (subtitle)
    {
        UiKit::Text(Loc::T(subtitle), 640.0f, cy + 36.0f, 13.0f, Mul(warn ? RED : TEXT_DIM, alpha), TextAlignment::Center,
                    panelW + 20.0f, false);
    }
}

void DrawTutorialHints()
{
    const float fadeFrames = 20.0f;
    const int hint = ActiveHint();
    if (hint == HintWrong)
    {

        const float a = Clamp01(std::min(static_cast<float>(WRONG_HINT_FRAMES - g_WrongTimer), static_cast<float>(g_WrongTimer))
                                / fadeFrames);
        const HintGroup groups[] = {{"C", "SWITCH COLOR"}};
        DrawHint(groups, 1, "WRONG COLOR HURTS", a, true);
    }
    else if (hint == HintMove)
    {
        const float t = static_cast<float>(g_TutTimer - HINT_MOVE_START);
        const float a = Clamp01(std::min(t, static_cast<float>(HINT_MOVE_END - g_TutTimer)) / fadeFrames);
        const HintGroup groups[] = {{"WASD", "MOVE"}, {"SPACE", "SHOT"}};
        DrawHint(groups, 2, nullptr, a, false);
    }
    else if (hint == HintColor)
    {
        const float a = Clamp01(static_cast<float>(g_TutTimer) / fadeFrames);
        const HintGroup groups[] = {{"C", "SWITCH COLOR"}};
        DrawHint(groups, 1, "MATCH THE BULLET COLOR TO ABSORB IT", a, false);
    }
    else if (hint == HintSpecial)
    {
        const float a = Clamp01(static_cast<float>(g_TutTimer) / fadeFrames);
        const HintGroup groups[] = {{"V", "FIRE SPECIAL"}};
        DrawHint(groups, 1, "ENERGY READY", a, false);
    }
}
}

// =====================================================================================
void GameHud::BeginIntro()
{
    g_IntroT = 0.0f;
}

void GameHud::Reset()
{
    g_IntroT = 1000.0f;
    g_HavePrev = false;
    g_PrevEnergy = 0;
    g_PrevHp = 0;
    g_PrevScore = 0;
    g_ScoreShown = 0.0f;
    g_EnergyShown = 0.0f;
    g_ScorePop = 0;
    g_EnergyFlash = 0;
    g_HpLostFlash = 0;
    g_HpLostIndex = -1;
    g_HpGhost = 0.0f;
    g_ReadyTick = 0;
    g_ComboAlpha = 0.0f;
    g_LastCombo = 0;
    g_LastMultiplier = 1;
    for (float& f : g_Fade)
        f = 1.0f;
    g_TutTimer = 0;
    g_Absorbs = 0;
    g_Switches = 0;
    g_WrongShown = false;
    g_WrongTimer = 0;
    g_LastHint = HintNone;
    if (g_TutStep == 3 && !Loc::IsTutorialDone()) g_TutStep = 0;
}

void GameHud::Update(const HudState& s)
{
    if (!g_HavePrev)
    {
        g_HavePrev = true;
        g_PrevEnergy = s.energy;
        g_PrevHp = s.hp;
        g_HpGhost = static_cast<float>(s.hp);
        g_PrevScore = s.score;
        g_PrevPolarity = s.polarity;
        g_ScoreShown = static_cast<float>(s.score);
        g_EnergyShown = static_cast<float>(s.energy);
    }

    const bool absorbed = s.energy > g_PrevEnergy;
    const bool damaged = s.hp < g_PrevHp;
    const bool switched = s.polarity != g_PrevPolarity;
    const bool ready = s.energy >= s.maxEnergy && g_PrevEnergy < s.maxEnergy;
    const bool special = g_PrevEnergy >= s.maxEnergy && s.energy < s.maxEnergy;

    if (absorbed) g_EnergyFlash = 14;
    if (damaged)
    {
        g_HpLostIndex = s.hp;
        g_HpLostAmount = g_PrevHp - s.hp;
        g_HpLostFlash = 24;
    }
    if (s.score > g_PrevScore) g_ScorePop = 10;

    g_ScoreShown += (static_cast<float>(s.score) - g_ScoreShown) * 0.22f;
    if (std::fabs(static_cast<float>(s.score) - g_ScoreShown) < 1.0f) g_ScoreShown = static_cast<float>(s.score);
    g_EnergyShown += (static_cast<float>(s.energy) - g_EnergyShown) * 0.25f;
    if (std::fabs(static_cast<float>(s.energy) - g_EnergyShown) < 0.2f) g_EnergyShown = static_cast<float>(s.energy);
    if (g_ScorePop > 0) --g_ScorePop;
    if (g_EnergyFlash > 0) --g_EnergyFlash;
    if (g_HpLostFlash > 0) --g_HpLostFlash;
    if (static_cast<float>(s.hp) >= g_HpGhost)
    {
        g_HpGhost = static_cast<float>(s.hp); // healed: the ghost follows at once
    }
    else if (g_HpLostFlash <= 0)
    {
        g_HpGhost += (static_cast<float>(s.hp) - g_HpGhost) * 0.08f;
        if (g_HpGhost - static_cast<float>(s.hp) < 0.5f) g_HpGhost = static_cast<float>(s.hp);
    }
    ++g_ReadyTick;

    if (g_IntroT < 1000.0f) g_IntroT += 1.0f;
    if (s.combo > 0)
    {
        g_LastCombo = s.combo;
        g_LastMultiplier = s.multiplier;
        g_ComboAlpha += (1.0f - g_ComboAlpha) * 0.25f;
    }
    else
    {
        g_ComboAlpha += (0.0f - g_ComboAlpha) * 0.12f;
    }

    for (int i = 0; i < FadeCount; ++i)
    {
        g_Fade[i] += (FadeTarget(s, static_cast<FadeRect>(i)) - g_Fade[i]) * 0.15f;
    }

    UpdateTutorial(s, absorbed, damaged, switched, ready, special);
    const int hint = ActiveHint();
    if (hint != g_LastHint && hint != HintNone) GameAudio::Play(GameSound::UiHint);
    g_LastHint = hint;

    g_PrevEnergy = s.energy;
    g_PrevHp = s.hp;
    g_PrevScore = s.score;
    g_PrevPolarity = s.polarity;
}

void GameHud::Draw(const HudState& s)
{
    ++g_DrawTick;
    const BossHudInfo boss = GameBoss::GetHudInfo();
    // the cut-ins of the boss fight (ui/CutIn): a new phase, a broken overload
    {
        // (no card when the boss changes its phase: the change itself is the show)
        static int s_LastBreaks = 0;
        if (boss.breaks > s_LastBreaks) CutIn::Trigger(CutIn::Kind::Break, Loc::T("BREAK!"), "", CutIn::AccentGold);
        s_LastBreaks = boss.breaks;
    }

    // the corners slide in one after the other when a run starts (UiKit::SetOrigin moves everything that is drawn meanwhile)
    const float inScore = Intro(3.0f, 16.0f);
    const float inTop = Intro(6.0f, 16.0f);
    const float inCluster = Intro(9.0f, 16.0f);
    const float inCombo = Intro(12.0f, 16.0f);
    UiKit::SetOrigin(-(1.0f - inScore) * 150.0f, 0.0f);
    DrawScore(s, g_Fade[FadeScore] * inScore);
    UiKit::SetOrigin(0.0f, 0.0f);
    if (boss.visible)
    {
        DrawBossBar(boss, g_Fade[FadeBoss]);
    }
    else if (!s.bossActive)
    {
        UiKit::SetOrigin(0.0f, -(1.0f - inTop) * 50.0f);
        DrawWaveLabel(s, inTop);
        UiKit::SetOrigin(0.0f, 0.0f);
    }
    {
        float cueRate = 0.0f;
        bool hint = false;
        bool cue = false;
        if (boss.visible && boss.overload)
        {
            cue = true;
            cueRate = boss.overloadRate;
            hint = boss.overloadHint;
        }
        if (cue) DrawOverloadCue(cueRate, hint, s, 1.0f);
    }
    UiKit::SetOrigin(-(1.0f - inCluster) * 150.0f, 0.0f);
    DrawCluster(s, g_Fade[FadeCluster] * inCluster);
    UiKit::SetOrigin((1.0f - inCombo) * 150.0f, 0.0f);
    DrawCombo(s, g_Fade[FadeCombo] * inCombo);
    UiKit::SetOrigin(0.0f, 0.0f);
    DrawTutorialHints();
    DrawPauseButton();
}

bool GameHud::PauseButtonHovered()
{
    return OverPauseButton();
}



