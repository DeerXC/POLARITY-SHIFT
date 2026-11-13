#include "game/scene/HowToScene.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/GameSettings.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/GameBackground.h"
#include "game/GameFlow.h"
#include "game/scene/HowToTraining.h"
#include "rogue/ModulesDrawInternal.h"
#include "ui/DemoKit.h"
#include "ui/GameMenuUi.h"
#include "ui/TextTheme.h"
#include "ui/UiKit.h"

#include <Windows.h>
#include <cmath>
#include <string>

namespace
{
namespace D = DemoKit;

constexpr int PAGE_COUNT = 3;
constexpr int PAGE_RULES = 0, PAGE_TACTICS = 1, PAGE_TRAINING = 2;
const char* const PAGE_KEYS[PAGE_COUNT] = {"BASICS", "TACTICS", "TRAINING"};

int g_Page = PAGE_RULES;
int g_PageT = 0; // frames since the page changed (its contents slide in)

// the pages: big tabs under the header
constexpr float PAGE_X = 365.0f;
constexpr float PAGE_Y = 158.0f;
constexpr float PAGE_W = 176.0f;
constexpr float PAGE_H = 34.0f;
constexpr float PAGE_GAP = 11.0f;

constexpr float CONTENT_X = 365.0f;
constexpr float CONTENT_W = 550.0f;

float NormX(float px) { return px / 640.0f - 1.0f; }
float NormY(float py) { return 1.0f - py / 360.0f; }
float RowA(float topPx) { return GameMenuUi::RowAlpha(NormY(topPx)); }
float RowS(float topPx) { return GameMenuUi::RowShift(NormY(topPx)) * 640.0f; }
float PageLeft(int index) { return PAGE_X + static_cast<float>(index) * (PAGE_W + PAGE_GAP); }

int HoveredPage()
{
    if (!GameInput::IsMouseInsideWindow()) return -1;
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        if (GameMenuUi::IsMouseInsideRect(NormX(PageLeft(i)), NormY(PAGE_Y), NormX(PageLeft(i) + PAGE_W), NormY(PAGE_Y + PAGE_H))) return i;
    }
    return -1;
}

// the contents of a page slide in from the right when the page changes
float PageIn() { return GameMenuUi::EaseOut(GameMenuUi::Clamp01(static_cast<float>(g_PageT) / 12.0f)); }

// the key / button that is bound now, for the device in use
std::string Label(GameSettings::Action action)
{
    return Loc::T(GameInput::ActionLabel(action).c_str());
}

void DrawPages()
{
    const float a = RowA(PAGE_Y);
    const float s = RowS(PAGE_Y);
    const int hovered = HoveredPage();
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        const float x = PageLeft(i) + s;
        const bool selected = i == g_Page;
        if (selected)
        {
            GameMenuUi::SelectTab(x, PAGE_Y, PAGE_W, PAGE_H, a, 5.0f);
        }
        else
        {
            if (i == hovered) GameMenuUi::Plate(x, PAGE_Y, PAGE_W, PAGE_H, GameMenuUi::Ivory(0.10f * a));
            GameMenuUi::PlateLine(x, PAGE_Y, PAGE_W, PAGE_H, GameMenuUi::Ivory(0.30f * a));
        }
        const std::string name = Loc::T(PAGE_KEYS[i]);
        GameMenuUi::DrawHeavyText(name, x + PAGE_W * 0.5f, PAGE_Y + PAGE_H * 0.5f, UiKit::FitFontPx(name, 15.0f, PAGE_W - 26.0f, 9.0f),
                                  selected ? GameMenuUi::Ink(a) : UiKit::Rgba(0.72f, 0.78f, 0.88f, 0.95f * a), TextAlignment::Center, PAGE_W);
    }
}

// ---- the basics: three things that every player has to know, one line each; the animation says the rest -------------------------------------------------------------
constexpr float STRIP_W = 248.0f;
constexpr float STRIP_H = 84.0f;
constexpr float ROW_H = 112.0f;
constexpr float ROW_GAP = 10.0f;
constexpr float ROW_Y = 208.0f;

void DrawCardFrame(float x, float y, float w, float h, float a)
{
    GameMenuUi::Plate(x + 5.0f, y + 4.0f, w, h, GameMenuUi::Accent(0.55f * a));
    GameMenuUi::Plate(x, y, w, h, UiKit::Rgba(0.020f, 0.026f, 0.044f, 0.94f * a));
    GameMenuUi::PlateLine(x, y, w, h, GameMenuUi::Ivory(0.18f * a));
}

// ---- the three animations of the basics (a strip of 248 x 84, loop of 180 steps) -----------------------------------------------------------------------------------------
constexpr int RULE_LOOP = 180;

// 1 SWITCH: the ship is blue, the key is pressed, the ship and the ring of its reach turn red; it is pressed again and they turn blue.
void SceneSwitch(const D::Scene& s)
{
    const float cx = s.x + 150.0f, cy = s.cy(), t = s.t;
    const bool red = t >= 50.0f && t < 118.0f;
    const D::Color tint = red ? D::RED : D::BLUE;
    const float press1 = D::Pulse(t, 44.0f, 62.0f), press2 = D::Pulse(t, 112.0f, 130.0f);
    D::Ring(cx, cy, 24.0f, 1.8f, D::Fade(tint, 0.85f * s.a));
    D::DashedRing(cx, cy, 33.0f, 1.2f, D::Fade(tint, 0.35f * s.a), 14);
    const float burst = std::max(D::Seg01(t, 50.0f, 66.0f) * (1.0f - D::Seg01(t, 66.0f, 70.0f)), D::Seg01(t, 118.0f, 134.0f) * (1.0f - D::Seg01(t, 134.0f, 138.0f)));
    if (burst > 0.0f) D::Ring(cx, cy, 24.0f + 30.0f * burst, 1.6f, D::Fade(tint, 0.7f * s.a * (1.0f - burst)));
    D::Ship(cx, cy, tint, s.a, 1.4f);
    // the two colours at the sides
    D::Dia(s.x + 30.0f, cy - 14.0f, 6.0f, D::Fade(D::BLUE, s.a * (red ? 0.25f : 1.0f)));
    D::Dia(s.x + 30.0f, cy + 14.0f, 6.0f, D::Fade(D::RED, s.a * (red ? 1.0f : 0.25f)));
    // the key
    const std::string key = Label(GameSettings::Action::Switch);
    const float k = std::max(press1, press2);
    UiKit::KeyCap(key, s.x + s.w - 62.0f, cy - 13.0f + 2.0f * k, 26.0f, UiKit::Rgba(0.85f + 0.15f * k, 0.92f, 1.0f, (0.55f + 0.4f * k) * s.a));
}

// 2 ABSORB: a blue bullet is taken by the blue ship and becomes energy; a red one hurts (the bar of HP goes down, the ship flashes).
void SceneMatch(const D::Scene& s)
{
    const float sx = s.x + 70.0f, cy = s.cy(), t = s.t;
    const float hit2 = D::FlightHitTime(s, D::Flight{92.0f, cy, D::RED, false, false, sx + 10.0f, s.x + s.w - 18.0f}, 2.2f);
    const float hurt = D::Pulse(t, hit2, hit2 + 22.0f);
    D::Ring(sx, cy, 24.0f, 1.4f, D::Fade(D::BLUE, 0.5f * s.a));
    D::Ship(sx, cy, D::BLUE, s.a * (hurt > 0.0f && static_cast<int>(t) % 4 < 2 ? 0.4f : 1.0f), 1.4f);
    D::Fly(s, D::Flight{10.0f, cy - 4.0f, D::BLUE, false, true, sx + 24.0f, s.x + s.w - 18.0f}, sx, cy, 2.2f);
    const float hitB = D::FlightHitTime(s, D::Flight{10.0f, cy - 4.0f, D::BLUE, false, true, sx + 24.0f, s.x + s.w - 18.0f}, 2.2f);
    // the red bullet flies on into the ship
    const float rx = s.x + s.w - 18.0f - 2.2f * (t - 92.0f);
    if (t >= 92.0f && rx > sx + 6.0f) D::Bullet(rx, cy, D::RED, false, 1.0f, s.a);
    if (hurt > 0.0f) D::Cross(sx, cy - 20.0f, 6.0f, 2.2f, D::Fade(D::RED, s.a * hurt));
    // the bars: energy up by the absorb, HP down by the hit
    D::Bar(s.x + s.w - 112.0f, s.y + 14.0f, 96.0f, 6.0f, t >= hitB + 8.0f ? 0.55f : 0.30f, D::BLUE, s.a);
    D::Bar(s.x + s.w - 112.0f, s.y + 28.0f, 96.0f, 6.0f, t >= hit2 ? 0.62f : 0.9f, hurt > 0.0f ? D::RED : D::GREEN, s.a);
    D::Num("EN", s.x + s.w - 124.0f, s.y + 17.0f, 10.0f, D::DIM, s.a);
    D::Num("HP", s.x + s.w - 124.0f, s.y + 31.0f, 10.0f, D::DIM, s.a);
    if (t >= hitB && t < hitB + 30.0f) D::Num("+", sx + 4.0f, cy - 26.0f - (t - hitB) * 0.4f, 14.0f, D::GREEN, s.a * (1.0f - (t - hitB) / 30.0f));
}

// 3 BEAM: the bar fills, the key is pressed at full, the white beam goes out and breaks the shield of the elite.
void SceneBeam(const D::Scene& s)
{
    const float sx = s.x + 40.0f, cy = s.cy() + 6.0f, t = s.t;
    const float fill = t < 78.0f ? D::Seg01(t, 8.0f, 60.0f) : 0.0f;
    D::Bar(s.x + 14.0f, s.y + 12.0f, 100.0f, 6.0f, fill, fill >= 1.0f ? D::GOLD : D::BLUE, s.a);
    const float press = D::Pulse(t, 64.0f, 82.0f);
    const std::string key = Label(GameSettings::Action::Special);
    UiKit::KeyCap(key, s.x + s.w - 62.0f, s.y + 8.0f + 2.0f * press, 22.0f, UiKit::Rgba(0.85f + 0.15f * press, 0.92f, 1.0f, (0.55f + 0.4f * press) * s.a));
    const float ex = s.x + s.w - 36.0f;
    const bool broken = t >= 96.0f;
    D::Enemy(ex, cy, D::GOLD, broken ? D::Pulse(t, 96.0f, 110.0f) : 0.0f, s.a, 1.2f);
    if (!broken) D::Hex(ex, cy, 19.0f, 2.0f, D::Fade(D::GOLD, 0.9f * s.a));
    else if (t < 112.0f)
    {
        const float k = (t - 96.0f) / 16.0f;
        for (int i = 0; i < 6; ++i)
        {
            const float a = 1.0472f * static_cast<float>(i);
            D::Line(ex + std::cos(a) * (19.0f + 14.0f * k), cy + std::sin(a) * (17.0f + 12.0f * k), ex + std::cos(a) * (25.0f + 16.0f * k), cy + std::sin(a) * (22.0f + 14.0f * k), 1.8f,
                    D::Fade(D::GOLD, s.a * (1.0f - k)));
        }
    }
    D::Ship(sx, cy, D::BLUE, s.a, 1.2f);
    if (t >= 76.0f && t < 120.0f)
        D::Beam(sx + 12.0f, cy, (ex - sx - 12.0f) * D::Ease(D::Seg01(t, 76.0f, 90.0f)), 4.0f, D::WHITE, D::BLUE, s.a * (1.0f - D::Seg01(t, 106.0f, 120.0f)));
}

// a row: the animation at the left, the number and the title and one or two short lines at the right
void DrawRow(int index, float y, const std::string& title, const std::string& line1, const std::string& line2, bool line2Danger, int scene, float time)
{
    const float x = CONTENT_X;
    const float delay = GameMenuUi::Clamp01((static_cast<float>(g_PageT) - static_cast<float>(index) * 3.0f) / 12.0f);
    const float a = RowA(y) * GameMenuUi::EaseOut(delay) * (0.3f + 0.7f * PageIn());
    const float s = RowS(y) + (1.0f - GameMenuUi::EaseOut(delay)) * 40.0f;
    if (a <= 0.0f) return;
    DrawCardFrame(x + s, y, CONTENT_W, ROW_H, a);
    UiKit::SetOrigin(s, 0.0f);
    D::Scene sc = D::Begin(x + 14.0f, y + (ROW_H - STRIP_H) * 0.5f, STRIP_W, STRIP_H, static_cast<int>(time), RULE_LOOP);
    sc.a *= a;
    switch (scene)
    {
    case 0: SceneSwitch(sc); break;
    case 1: SceneMatch(sc); break;
    default: SceneBeam(sc); break;
    }
    UiKit::SetOrigin(0.0f, 0.0f);
    const float tx = x + s + STRIP_W + 38.0f, tw = CONTENT_W - STRIP_W - 56.0f;
    const std::string num = std::to_string(index + 1);
    GameMenuUi::DrawHeavyText(num, tx - 4.0f, y + 32.0f, 19.0f, GameMenuUi::Accent(a), TextAlignment::Center, 30.0f);
    GameMenuUi::DrawHeavyText(num, tx - 5.0f, y + 31.0f, 19.0f, GameMenuUi::Ivory(a), TextAlignment::Center, 30.0f);
    GameMenuUi::DrawHeavyText(title, tx + 14.0f, y + 32.0f, UiKit::FitFontPx(title, 16.0f, tw - 20.0f, 10.0f), UiKit::Rgba(0.98f, 0.99f, 1.0f, a), TextAlignment::Left, tw);
    UiKit::Text(line1, tx + 14.0f, y + 58.0f, UiKit::FitFontPx(line1, 13.0f, tw - 14.0f, 9.0f), UiKit::Rgba(0.72f, 0.80f, 0.92f, 0.96f * a), TextAlignment::Left, tw, false);
    if (!line2.empty())
        UiKit::Text(line2, tx + 14.0f, y + 79.0f, UiKit::FitFontPx(line2, 13.0f, tw - 14.0f, 9.0f), line2Danger ? UiKit::Rgba(1.0f, 0.55f, 0.48f, 0.96f * a) : UiKit::Rgba(0.72f, 0.80f, 0.92f, 0.96f * a),
                    TextAlignment::Left, tw, false);
}

void DrawRulesPage(float time)
{
    DrawRow(0, ROW_Y, Loc::T("SWITCH POLARITY"), Loc::FmtText("{0}: RED / BLUE", Label(GameSettings::Action::Switch)), "", false, 0, time);
    DrawRow(1, ROW_Y + (ROW_H + ROW_GAP), Loc::T("MATCH BULLET COLOR"), Loc::T("SAME: ABSORB"), Loc::T("OTHER: DAMAGE"), true, 1, time);
    DrawRow(2, ROW_Y + 2.0f * (ROW_H + ROW_GAP), Loc::T("USE SPECIAL ATTACK"), Loc::FmtText("FULL ENERGY: {0}", Label(GameSettings::Action::Special)), "", false, 2, time);
    // the one thing more: the white bullet (no card: a line)
    const float y = ROW_Y + 3.0f * (ROW_H + ROW_GAP) + 4.0f;
    const float a = GameMenuUi::Clamp01((static_cast<float>(g_PageT) - 10.0f) / 12.0f) * RowA(y);
    {
        // the white bullet: a diamond in a ring (drawn here directly: DemoKit clips to the strip that was drawn last)
        const float bx = CONTENT_X + 14.0f + RowS(y);
        GameMenuUi::DiamondFill(bx, y, 5.0f, UiKit::Rgba(0.97f, 0.98f, 1.0f, a));
        constexpr int N = 16;
        for (int i = 0; i < N; ++i)
        {
            const float a0 = 6.2831853f * static_cast<float>(i) / N, a1 = 6.2831853f * static_cast<float>(i + 1) / N;
            GameMenuUi::Segment(bx + std::cos(a0) * 9.0f, y + std::sin(a0) * 9.0f, bx + std::cos(a1) * 9.0f, y + std::sin(a1) * 9.0f, 1.3f, UiKit::Rgba(0.97f, 0.98f, 1.0f, 0.8f * a));
        }
    }
    UiKit::Text(Loc::T("WHITE: DODGE ONLY"), CONTENT_X + 36.0f + RowS(y), y, 13.0f, UiKit::Rgba(0.92f, 0.95f, 1.0f, 0.92f * a), TextAlignment::Left, CONTENT_W, false);
}

// ---- the tactics: the three tactics with their real demos of the hangar, a line for each ---------------------------------------------------------------------------------------
constexpr float TAC_H = 118.0f;
constexpr float TAC_GAP = 8.0f;
constexpr float TAC_Y = 232.0f;
const char* const TACTIC_LINES[3] = {"DROP A DECOY: IT TAKES BULLETS OF YOUR COLOR", "DASH WHERE YOU MOVE: INVINCIBLE WHILE DASHING", "HOLD TO CHARGE: A FULL CHARGE BREAKS AN OVERLOAD"};

void DrawTacticRow(int slot, float time)
{
    const float y = TAC_Y + static_cast<float>(slot) * (TAC_H + TAC_GAP);
    const float x = CONTENT_X;
    const float delay = GameMenuUi::Clamp01((static_cast<float>(g_PageT) - static_cast<float>(slot) * 4.0f) / 12.0f);
    const float a = RowA(y) * GameMenuUi::EaseOut(delay) * (0.3f + 0.7f * PageIn());
    const float s = RowS(y) + (1.0f - GameMenuUi::EaseOut(delay)) * 40.0f;
    if (a <= 0.0f) return;
    DrawCardFrame(x + s, y, CONTENT_W, TAC_H, a);
    const std::string name = Loc::T(Mod::TACTICS[slot].name);
    const std::string line = Loc::T(TACTIC_LINES[slot]);
    GameMenuUi::DrawHeavyText(name, x + s + 18.0f, y + 19.0f, UiKit::FitFontPx(name, 16.0f, 170.0f, 10.0f), UiKit::Rgba(0.98f, 0.99f, 1.0f, a), TextAlignment::Left, 200.0f);
    UiKit::Text(line, x + s + 200.0f, y + 19.0f, UiKit::FitFontPx(line, 13.0f, CONTENT_W - 214.0f, 9.0f), UiKit::Rgba(0.72f, 0.80f, 0.92f, 0.96f * a), TextAlignment::Left, CONTENT_W, false);
    UiKit::SetOrigin(s, 0.0f);
    if (a > 0.6f) Mod::DrawModuleDemo(Mod::TACTIC_ID0 + slot, x + (CONTENT_W - 380.0f) * 0.5f, y + 32.0f, 380.0f, STRIP_H, static_cast<int>(time));
    UiKit::SetOrigin(0.0f, 0.0f);
}

void DrawTacticsPage(float time)
{
    const float a = GameMenuUi::Clamp01((static_cast<float>(g_PageT) - 2.0f) / 10.0f) * RowA(TAC_Y - 22.0f);
    const std::string head = Loc::T("BOUGHT ONCE WITH SHARDS IN THE HANGAR, ONE PER RUN") + std::string("   ") + Loc::FmtText("USE IT WITH {0}", Label(GameSettings::Action::Tactic));
    UiKit::Text(head, CONTENT_X + RowS(TAC_Y - 22.0f), TAC_Y - 20.0f, UiKit::FitFontPx(head, 13.0f, CONTENT_W, 9.0f), UiKit::Rgba(0.92f, 0.95f, 1.0f, 0.92f * a), TextAlignment::Left, CONTENT_W + 40.0f, false);
    for (int i = 0; i < 3; ++i) DrawTacticRow(i, time);
}
}

void HowToScene::Open()
{
    g_Page = PAGE_RULES;
    g_PageT = 100;
    HowToTraining::Reset();
}

void HowToScene::Update()
{
    GameBackground::Update();
    ++g_PageT;
    auto setPage = [](int page)
    {
        page = (page % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT;
        if (page == g_Page) return;
        g_Page = page;
        g_PageT = 0;
        if (page == PAGE_TRAINING) HowToTraining::Reset();
        GameAudio::Play(GameSound::MenuAdjust);
    };
    const int hoveredPage = HoveredPage();
    if (GameInput::IsTriggered(VK_LBUTTON) && hoveredPage >= 0) setPage(hoveredPage);

    const bool training = g_Page == PAGE_TRAINING;
    // the pages: LEFT / RIGHT (not in the training: the ship is flown with them), TAB, Q / E, the shoulder buttons of the pad, a click on a tab
    if (!training && (GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A'))) setPage(g_Page - 1);
    else if (!training && (GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D'))) setPage(g_Page + 1);
    if (GameInput::IsTriggered(VK_TAB) || GameInput::IsTriggered('E')) setPage(g_Page + 1);
    else if (GameInput::IsTriggered('Q')) setPage(g_Page - 1);

    if (training) HowToTraining::Update();

    // back: ESC / BACKSPACE (B on the pad); the right button too, except in the training (it switches the polarity there)
    if (GameInput::IsTriggered(VK_ESCAPE) || GameInput::IsTriggered(VK_BACK) || (!training && GameInput::IsTriggered(VK_RBUTTON)))
    {
        GameAudio::Play(GameSound::MenuDecide);
        GameFlow::ReturnToTitle();
    }
}

void HowToScene::Draw()
{
    Renderer2D_ClearCameraOffset();
    GameMenuUi::DrawBackdrop(0.55f);
    GameMenuUi::DrawPageHeader(Loc::T("HOW TO PLAY"));
    DrawPages();
    const float time = static_cast<float>(GameBackground::GetFrame());
    if (g_Page == PAGE_RULES)
        DrawRulesPage(time);
    else if (g_Page == PAGE_TACTICS)
        DrawTacticsPage(time);
    else
        HowToTraining::Draw(RowA(HowToTraining::Y) * PageIn(), (1.0f - PageIn()) * 40.0f);

    const float footA = GameMenuUi::Clamp01(GameMenuUi::Entrance(18.0f, 12.0f));
    const bool training = g_Page == PAGE_TRAINING;
    const std::string back = Loc::T(GameInput::UsingGamepad() ? "PRESS B TO BACK" : (training ? "ESC TO BACK" : "ESC OR RIGHT CLICK TO BACK"));
    UiKit::Text(back, 640.0f, 650.0f, 12.0f, UiKit::Rgba(0.54f, 0.62f, 0.76f, 0.80f * footA), TextAlignment::Center, 720.0f, true);

    // key hints: the arrows change the page (TAB in the training: the arrows fly the ship there)
    const float hx = CONTENT_X;
    const float capW = UiKit::KeyCap(training ? "TAB" : "ARROWS", hx, 674.0f, 24.0f, UiKit::Rgba(0.85f, 0.92f, 1.0f, 0.85f * footA));
    UiKit::Text(Loc::T("SWITCH"), hx + capW + 10.0f, 686.0f, 13.0f, UiKit::Rgba(0.97f, 0.98f, 1.0f, footA), TextAlignment::Left, 200.0f, false);
}

