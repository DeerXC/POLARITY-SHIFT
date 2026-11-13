#include "game/scene/TitleScene.h"
#include "core/AssetManager.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/GameBackground.h"
#include "game/GameFlow.h"
#include "game/GamePlayer.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/scene/HowToScene.h"
#include "game/scene/RecordsScene.h"
#include "game/scene/SettingsScene.h"
#include "rogue/Modules.h"
#include "ui/GameMenuUi.h"
#include "ui/TextTheme.h"
#include "ui/UiKit.h"
#include "ui/Wordmark.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <string>

// Title screen. Left: logo, menu, hints (on the stars of the background). Right: the hero panel with the ship on the red / blue cut.
// Nothing on the left reaches into the panel, and the ship never reaches into the logo.
namespace
{
enum class TitleMenuItem
{
    StartGame,
    HowToPlay,
    Records,
    Settings,
    Hangar, // (above EXIT: the exit is the last row)
    Exit
};

constexpr int TITLE_START_TRANSITION_DURATION = 16; // (the hero ship has left the picture by frame 15; the game takes the ship over at the edge)
constexpr int TITLE_MENU_ITEM_COUNT = 6;

// layout in virtual 1280 x 720 pixels (the same numbers as MODE_* in rogue/ModulesInternal.h: the mode selector sits right of row 0)
constexpr float ROW_LEFT = 96.0f;
constexpr float ROW_WIDTH = 372.0f;
constexpr float ROW_HEIGHT = 46.0f;
constexpr float ROW_FIRST_CENTER = 332.0f;
constexpr float ROW_STEP = 60.0f;

constexpr float NormX(float px) { return px / 640.0f - 1.0f; }
constexpr float NormY(float py) { return 1.0f - py / 360.0f; }

constexpr float TITLE_MENU_LEFT = NormX(ROW_LEFT);
constexpr float TITLE_MENU_RIGHT = NormX(ROW_LEFT + ROW_WIDTH);
constexpr float TITLE_MENU_HEIGHT = ROW_HEIGHT / 360.0f;

float RowCenterNorm(int index)
{
    return NormY(ROW_FIRST_CENTER + ROW_STEP * static_cast<float>(index));
}

// THE KEY ART. The title is built like the covers of Persona 3 Reload, in the logic of this game: one flat saturated colour per half (the backdrop,
// Layout::flat), ONE big ship - tilted, nose up, with a hard offset shadow - black ink blades flowing out behind it, translucent crystal shards.
// Nothing here is soft: flat polygons only. The ship is the hero texture (768 x 768, the same model as the game ship).
constexpr float SHIP_X = NormX(1004.0f);
constexpr float SHIP_Y = NormY(372.0f);
constexpr float SHIP_W = 0.880f;  // 563 px wide
constexpr float SHIP_H = 1.564f;  // = SHIP_W / (720 / 1280): square on screen
constexpr float SHIP_SCALE = SHIP_W / 0.170f;
constexpr float TITLE_TILT = 0.30f; // radians, nose up
constexpr float TITLE_ASPECT = 720.0f / 1280.0f;

// a point in the local coordinates of the ship (x in x-units, y in y-units, 0 = its centre) on the screen, for a ship drawn at (cx, cy) turned by rot
// (the same transform as Renderer2D_DrawTextureEx)
void ShipPoint(float cx, float cy, float rot, float lx, float ly, float& ox, float& oy)
{
    const float c = std::cos(rot), s = std::sin(rot);
    ox = cx + lx * c - ly * TITLE_ASPECT * s;
    oy = cy + lx / TITLE_ASPECT * s + ly * c;
}

// a flat triangle (normalized coordinates)
void FlatTri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a)
{
    if (a <= 0.002f) return;
    const Renderer2DVertex v[3] = {{x0, y0, r, g, b, a}, {x1, y1, r, g, b, a}, {x2, y2, r, g, b, a}};
    Renderer2D_DrawPolygon(v, 3);
}

float HashF(int seed)
{
    unsigned int x = static_cast<unsigned int>(seed) * 374761393u + 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u;
    x ^= x >> 16;
    return static_cast<float>(x & 0xffffu) / 65535.0f;
}

// translucent crystals on the panel, warm on the red half, cold on the blue half (the side of the cut is read from the backdrop as it is drawn).
// They are STILL: a fixed place and a fixed turn each, a breath of 1 - 2 px every 15 - 25 s, and now and then one of them catches the light (its opacity
// rises for about a second and falls again, one at a time, never in step). Real glass does not drift. `time` is in seconds. `front`: the small ones
// (drawn after the ship).
void DrawShards(float time, float alpha, bool front)
{
    constexpr int COUNT = 26;
    for (int i = 0; i < COUNT; ++i)
    {
        const bool tiny = (i % 3) == 0; // (not `small`: Windows.h defines that word as a macro, `char`)
        if (tiny != front) continue;
        const float h0 = HashF(i * 5 + 1), h1 = HashF(i * 5 + 2), h2 = HashF(i * 5 + 3), h3 = HashF(i * 5 + 4), h4 = HashF(i * 5 + 5);
        const float py = 46.0f + h1 * 630.0f + std::sin(time * (0.26f + 0.18f * h4) + h0 * 6.0f) * 1.6f;
        const float edge = 797.0f - 0.214f * py + 18.0f; // the steep edge of the panel at this height: the shards live on the panel only
        const float px = edge + h0 * (1262.0f - edge) + std::sin(time * (0.24f + 0.16f * h3) + h2 * 6.0f) * 1.2f;
        const float size = tiny ? 5.0f + 8.0f * h2 : 10.0f + 24.0f * h2;
        const float rot = h3 * 6.2831853f + std::sin(time * 0.21f + h0 * 6.0f) * 0.025f;
        const bool red = GameMenuUi::CutSide(px, py) > 0.0f;
        const float cr = red ? 1.00f : 0.72f, cg = red ? 0.70f : 0.95f, cb = red ? 0.52f : 1.00f;
        // the catch of the light: a narrow bump of a sine, each shard at its own time
        const float glint = std::pow(std::max(0.0f, std::sin(time * (0.30f + 0.12f * h4) + h0 * 40.0f)), 16.0f);
        const float a = alpha * ((tiny ? 0.50f : 0.16f + 0.26f * h4) + 0.26f * glint);
        // a sliver: three points, one long (the crystal)
        const float c = std::cos(rot), sn = std::sin(rot);
        const float lx[3] = {size, -size * 0.55f, -size * 0.25f}, ly[3] = {0.0f, size * 0.42f, -size * 0.50f};
        float x[3], y[3];
        for (int k = 0; k < 3; ++k)
        {
            x[k] = UiKit::NX(px + lx[k] * c - ly[k] * sn);
            y[k] = UiKit::NY(py + lx[k] * sn + ly[k] * c);
        }
        FlatTri(x[0], y[0], x[1], y[1], x[2], y[2], cr, cg, cb, a);
    }
}

int g_SelectedTitleMenuItem = -1; // -1: nothing highlighted (a menu opens without a highlight)
float g_TitleShipOrbitAngle = 0.0f;
Polarity g_TitleShipPolarity = Polarity::Blue;
bool g_TitleStartTransition = false;
int g_TitleStartTransitionTimer = 0;
float g_TitleShipLaunchOffsetX = 0.0f;
float g_TitleShipLaunchOffsetY = 0.0f;

// START GAME - a match cut, not a change of ship. The parts of the title leave one after the other, the backdrop turns into the field of the game; the
// ship levels out and pulls back, then rockets off to the right while the camera pulls away: it SHRINKS as it flies, so that when it leaves the screen it has
// exactly the size of the ship of the game (GamePlayer::DrawWidth). The game then lets that ship fly in from the left edge (GamePlayer::BeginIntro, matchCut)
// at the same size, level, nose to the right: one ship that went round the screen, not a big one that is replaced by a small one.
constexpr int TITLE_CHARGE_FRAMES = 8;  // the ship levels out and pulls back
constexpr int TITLE_LAUNCH_FRAMES = 12; // ... and accelerates out of the screen, shrinking

float Clamp01Local(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// 0 at rest, 1 when the part has left; eased in (the exits are short and they accelerate)
float ExitK(float delay, float duration)
{
    const float t = Clamp01Local((static_cast<float>(g_TitleStartTransitionTimer) - delay) / duration);
    return t * t;
}

// How far the shrinking has gone: 1 at the start of the launch, GamePlayer::DrawWidth() / SHIP_W when the ship leaves the picture (at 60 % of the launch).
float ShipScaleAt(float launch)
{
    const float q = Clamp01Local(launch / 0.60f);
    const float e = q * q * (3.0f - 2.0f * q);
    return 1.0f + (GamePlayer::DrawWidth() / SHIP_W - 1.0f) * e;
}
float ShipLevelAt(float launch) // 0 .. 1: how far the ship has come down to the line of flight of the game ship (y = 0)
{
    const float q = Clamp01Local(launch / 0.60f);
    return q * q * (3.0f - 2.0f * q);
}
float ShipOffsetXAt(float launch)
{
    return -0.05f + 1.75f * launch * launch;
}

float LaunchProgress()
{
    return Clamp01Local((static_cast<float>(g_TitleStartTransitionTimer) - static_cast<float>(TITLE_CHARGE_FRAMES)) / static_cast<float>(TITLE_LAUNCH_FRAMES));
}

bool IsPointInsideTitleMenuItem(float mouseX, float mouseY, int menuIndex)
{
    if (menuIndex < 0 || menuIndex >= TITLE_MENU_ITEM_COUNT) return false;
    const float centerY = RowCenterNorm(menuIndex);
    const float top = centerY + TITLE_MENU_HEIGHT * 0.5f;
    const float bottom = centerY - TITLE_MENU_HEIGHT * 0.5f;
    return mouseX >= TITLE_MENU_LEFT && mouseX <= TITLE_MENU_RIGHT && mouseY <= top && mouseY >= bottom;
}

int GetHoveredTitleMenuIndex()
{
    if (!GameInput::IsMouseInsideWindow()) return -1;
    const float mouseX = GameInput::GetMouseX();
    const float mouseY = GameInput::GetMouseY();
    for (int index = 0; index < TITLE_MENU_ITEM_COUNT; index++)
    {
        if (IsPointInsideTitleMenuItem(mouseX, mouseY, index)) return index;
    }
    return -1;
}

void BeginTitleStartTransition()
{
    if (g_TitleStartTransition) return;
    g_TitleStartTransition = true;
    g_TitleStartTransitionTimer = 0;
    g_TitleShipLaunchOffsetX = 0.0f;
    g_TitleShipLaunchOffsetY = 0.0f;
    GameAudio::Play(GameSound::MenuDecide);
}

void CompleteTitleStartTransition()
{
    GameFlow::StartNewGame(g_TitleShipPolarity, true); // the game starts with the colour of the title ship, and takes the ship over at the edge
    g_TitleStartTransition = false;
    g_TitleStartTransitionTimer = 0;
    g_TitleShipLaunchOffsetX = 0.0f;
    g_TitleShipLaunchOffsetY = 0.0f;
}

void ExecuteTitleMenuItem(int index)
{
    switch (static_cast<TitleMenuItem>(index))
    {
    case TitleMenuItem::StartGame:
        BeginTitleStartTransition();
        break;
    case TitleMenuItem::HowToPlay:
        HowToScene::Open();
        GameState::scene = GameState::Scene::HowTo;
        break;
    case TitleMenuItem::Records:
        RecordsScene::Open();
        GameState::scene = GameState::Scene::Records;
        break;
    case TitleMenuItem::Settings:
        SettingsScene::Open();
        break;
    case TitleMenuItem::Exit:
        PostQuitMessage(0);
        break;
    case TitleMenuItem::Hangar:
        Mod::OpenHangarFromTitle();
        break;
    }
}

void DrawTitleMenuItem(const std::string& text, int menuIndex)
{
    if (menuIndex < 0 || menuIndex >= TITLE_MENU_ITEM_COUNT) return;
    const bool isSelected = g_SelectedTitleMenuItem == menuIndex;
    const float centerY = RowCenterNorm(menuIndex);
    const float top = centerY + TITLE_MENU_HEIGHT * 0.5f;
    const float bottom = centerY - TITLE_MENU_HEIGHT * 0.5f;
    const float exit = ExitK(static_cast<float>(menuIndex) * 1.2f, 12.0f);
    GameMenuUi::SetRowExit(exit * 620.0f, 1.0f - exit);
    GameMenuUi::DrawRow(TITLE_MENU_LEFT, top, TITLE_MENU_RIGHT, bottom,
                        isSelected ? GameMenuUi::RowStyle::Selected : GameMenuUi::RowStyle::Normal);
    GameMenuUi::SetRowExit(0.0f, 1.0f);
    const bool lit = GameMenuUi::SelectionAmount(TITLE_MENU_LEFT, top) > 0.5f;
    TextStyle textStyle = TextTheme::GetStyle(lit ? TextRole::ButtonSelected : TextRole::Button);
    textStyle.size = 0.0130f; // one size, selected or not: only the colour changes (the letters keep their shape, width and place)
    textStyle.alignment = TextAlignment::Left; // left aligned: the rows read like a list, and the selected one keeps its place
    textStyle.layoutWidth = (ROW_WIDTH - 44.0f) / 640.0f;
    textStyle.shadow = false;
    if (lit)
    {
        textStyle.red = 0.05f;
        textStyle.green = 0.06f;
        textStyle.blue = 0.09f;
        textStyle.alpha = 1.00f;
    }
    else
    {
        textStyle.red = 0.72f;
        textStyle.green = 0.77f;
        textStyle.blue = 0.86f;
        textStyle.alpha = 0.95f;
    }
    textStyle.alpha *= GameMenuUi::RowAlpha(top) * (1.0f - exit);
    TextRenderer::QueueText(text, NormX(ROW_LEFT + 40.0f) + GameMenuUi::RowShift(top) - exit * 620.0f / 640.0f, centerY + 0.019f, textStyle);
}

// "SWITCH <> ABSORB <> BREAK" under the logo, left aligned
void DrawTagline(float x, float y, float alpha)
{
    const std::string words[3] = {Loc::T("SWITCH"), Loc::T("ABSORB"), Loc::T("BREAK")};
    constexpr float PX = 14.0f;
    constexpr float GAP = 14.0f;
    constexpr float DIAMOND = 7.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float width = UiKit::TextWidth(words[i], PX);
        UiKit::Text(words[i], x, y, PX, UiKit::Rgba(0.66f, 0.70f, 0.78f, 0.86f * alpha), TextAlignment::Left, width + 24.0f, false);
        x += width;
        if (i < 2)
        {
            x += GAP;
            GameMenuUi::DiamondFill(x + DIAMOND * 0.5f, y, DIAMOND * 0.62f, UiKit::Rgba(1.0f, 1.0f, 1.0f, 0.45f * alpha));
            x += DIAMOND + GAP;
        }
    }
}

// logo block: red offset behind the ivory letters, a rule that shows the polarity, the tagline. Every part has its own start (and its own exit).
void DrawLogo()
{
    const float exitA = ExitK(1.0f, 14.0f);
    const float exitB = ExitK(4.0f, 14.0f);
    const float exitRule = ExitK(2.0f, 10.0f);
    const float line1 = GameMenuUi::Entrance(1.0f, 16.0f) * (1.0f - exitA);
    const float line2 = GameMenuUi::Entrance(5.0f, 16.0f) * (1.0f - exitB);
    const float rule = GameMenuUi::Entrance(10.0f, 14.0f) * (1.0f - exitRule);
    const float tagline = GameMenuUi::Entrance(14.0f, 14.0f) * (1.0f - exitRule);
    // the wordmark: block letters drawn from polygons (ui/Wordmark), the seam diamond in the O, a hard red / blue offset, the white block after SHIFT.
    // The two lines come in one after the other from the left and leave to the left, as the text did before.
    const float slide1 = (1.0f - GameMenuUi::Entrance(1.0f, 16.0f)) * 120.0f + exitA * 760.0f;
    const float slide2 = (1.0f - GameMenuUi::Entrance(5.0f, 16.0f)) * 120.0f + exitB * 760.0f;
    const float block = GameMenuUi::Entrance(9.0f, 14.0f);
    Wordmark::DrawLogo(ROW_LEFT, 84.0f, 62.0f, line1, line2, slide1, slide2, block * (1.0f - exitRule));
    // the rule under it: the SAME rule as every page (GameMenuUi::SeamRule: red | blue, the bright half is the colour of the ship, the seam diamond at the split)
    GameMenuUi::SeamRule(ROW_LEFT, 244.0f, ROW_WIDTH, rule, 1.0f);
    DrawTagline(ROW_LEFT, 272.0f, tagline);
}
}

void TitleScene::Update()
{
    // hangar (H on the title screen): the title does not react while it is open
    if (!g_TitleStartTransition && Mod::UpdateTitleOverlay())
    {
        GameBackground::Update();
        return;
    }
    GameBackground::Update();
    if (g_TitleStartTransition)
    {
        g_TitleStartTransitionTimer++;
        const float launch = LaunchProgress();
        if (launch <= 0.0f)
        {
            // charge: the ship pulls back a little
            const float c = Clamp01Local(static_cast<float>(g_TitleStartTransitionTimer) / static_cast<float>(TITLE_CHARGE_FRAMES));
            g_TitleShipLaunchOffsetX = -0.05f * (1.0f - (1.0f - c) * (1.0f - c));
            g_TitleShipLaunchOffsetY = 0.0f;
        }
        else
        {
            // launch: it accelerates out of the screen on the right, coming down to the line of flight of the game ship
            g_TitleShipLaunchOffsetX = ShipOffsetXAt(launch);
            g_TitleShipLaunchOffsetY = -SHIP_Y * ShipLevelAt(launch);
        }
        // the clouds, light shafts and streaks of the game fade in behind the leaving title
        GameBackground::SetTitleLook(Clamp01Local(static_cast<float>(g_TitleStartTransitionTimer) / static_cast<float>(TITLE_START_TRANSITION_DURATION))); // (1 when the game takes over: no jump)
        if (g_TitleStartTransitionTimer >= TITLE_START_TRANSITION_DURATION) CompleteTitleStartTransition();
        return;
    }
    // The phase only grows, it is never wrapped: every motion of the title is a sine of a multiple of it, and the multiples are not whole numbers, so a wrap
    // at 2 pi (after 8.7 s) used to make all of them jump at once. A float keeps 0.0002 rad of precision after ten hours: more than enough.
    g_TitleShipOrbitAngle += 0.012f;
    const float shipDrawX = SHIP_X;
    const float shipDrawY = SHIP_Y + std::sin(g_TitleShipOrbitAngle * 0.55f) * 0.0045f;
    bool shipClicked = false;
    if (GameInput::IsMouseInsideWindow())
    {
        const float mouseX = GameInput::GetMouseX();
        const float mouseY = GameInput::GetMouseY();
        constexpr float SHIP_CLICK_HALF_WIDTH = 0.078f * SHIP_SCALE;
        constexpr float SHIP_CLICK_HALF_HEIGHT = 0.095f * SHIP_SCALE;
        const bool mouseOnShip = mouseX >= shipDrawX - SHIP_CLICK_HALF_WIDTH && mouseX <= shipDrawX + SHIP_CLICK_HALF_WIDTH
                                 && mouseY >= shipDrawY - SHIP_CLICK_HALF_HEIGHT && mouseY <= shipDrawY + SHIP_CLICK_HALF_HEIGHT;
        if (mouseOnShip && GameInput::IsTriggered(VK_LBUTTON))
        {
            shipClicked = true;
            g_TitleShipPolarity = (g_TitleShipPolarity == Polarity::Blue) ? Polarity::Red : Polarity::Blue;
            GameMenuUi::SetBackdropPolarity(g_TitleShipPolarity == Polarity::Red);
            GameAudio::PlayPolaritySwitch(g_TitleShipPolarity == Polarity::Red);
            GameAudio::SetMusicPolarity(g_TitleShipPolarity == Polarity::Red); // (the title music changes its face with the ship)
        }
    }
    if (GameInput::ActionTriggered(GameSettings::Action::Switch))
    {
        g_TitleShipPolarity = (g_TitleShipPolarity == Polarity::Blue) ? Polarity::Red : Polarity::Blue;
        GameMenuUi::SetBackdropPolarity(g_TitleShipPolarity == Polarity::Red);
        GameAudio::PlayPolaritySwitch(g_TitleShipPolarity == Polarity::Red);
        GameAudio::SetMusicPolarity(g_TitleShipPolarity == Polarity::Red);
    }
    // the ship click must not also press a menu row
    const int hoveredIndex = shipClicked ? -1 : GetHoveredTitleMenuIndex();
    const int activated = GameMenuUi::UpdateMenu(g_SelectedTitleMenuItem, TITLE_MENU_ITEM_COUNT, hoveredIndex, false, GameSound::MenuMove,
                                                 Mod::IsMouseOnModeSelector());
    if (activated >= 0)
    {
        if (static_cast<TitleMenuItem>(activated) != TitleMenuItem::StartGame && static_cast<TitleMenuItem>(activated) != TitleMenuItem::Hangar)
            GameAudio::Play(GameSound::MenuDecide);
        ExecuteTitleMenuItem(activated);
    }
}

void TitleScene::Draw()
{
    if (Mod::DrawTitleOverlay()) return;
    GameMenuUi::DrawBackdrop(1.0f);
    // while the game starts, the backdrop is already the field of the game (Game_Draw sets that stage); the dark film that lies over the
    // background in the game fades in with it
    const float look = g_TitleStartTransition ? Clamp01Local(static_cast<float>(g_TitleStartTransitionTimer) / 26.0f) : 0.0f;
    if (look > 0.0f)
    {
        Renderer2D_DrawQuad(-1.00f, 1.00f, 1.00f, -1.00f, 0.002f, 0.008f, 0.024f, 0.16f * look);
    }
    // a thin vertical rule at the left edge with a red mark: the spine of the left column
    const float spine = 1.0f - ExitK(2.0f, 12.0f);
    UiKit::Fill(72.0f, 66.0f, 1.5f, 594.0f, GameMenuUi::Ivory(0.20f * spine));
    GameMenuUi::SeamDiamond(72.75f, 60.0f, 6.0f, spine); // (the spine ends in the mark of the game, like every rule and every cursor)
    DrawLogo();
    const float idleOffsetY = std::sin(g_TitleShipOrbitAngle * 0.55f) * 0.0045f; // (the orbit stands still while the game starts)
    const float shipIn = g_TitleStartTransition ? 1.0f : GameMenuUi::Entrance(6.0f, 22.0f);
    const float drawX = SHIP_X + g_TitleShipLaunchOffsetX + (1.0f - shipIn) * 0.45f;
    const float drawY = SHIP_Y + idleOffsetY + g_TitleShipLaunchOffsetY;
    const float launch = LaunchProgress();
    // the size of the ship: the title's hero size at rest, shrinking to the size of the game ship while it launches (see the match cut at the top)
    const float scale = g_TitleStartTransition ? ShipScaleAt(launch) : 1.0f;
    const float sw = SHIP_W * scale, sh = SHIP_H * scale;
    const float charge = g_TitleStartTransition ? Clamp01Local(static_cast<float>(g_TitleStartTransitionTimer) / static_cast<float>(TITLE_CHARGE_FRAMES)) : 0.0f;
    // nose up; it levels out while the ship pulls back to launch, and breathes a little at rest
    const float shipRotation = (TITLE_TILT + std::sin(g_TitleShipOrbitAngle * 0.9f) * 0.010f) * (1.0f - charge);
    const bool isRed = g_TitleShipPolarity == Polarity::Red;
    const float time = g_TitleShipOrbitAngle / 0.012f / 60.0f; // seconds of the title (60 steps a second)
    const float partsOut = 1.0f - ExitK(0.0f, 12.0f); // the ink and the shards leave first when the game starts
    // the ink: three slender blades that flow back out of the tail, like the ribbons of the covers (flat near-black, ending in needles; on the black of
    // the page they disappear into it)
    {
        const float ink = 0.94f * shipIn * partsOut;
        const float len = 1.0f + 0.04f * std::sin(g_TitleShipOrbitAngle * 1.3f);
        struct Blade { float b0x, b0y, b1x, b1y, ax, ay; };
        static const Blade BLADES[3] = {{-0.30f, 0.15f, -0.46f, 0.24f, -1.55f, 0.21f},
                                        {-0.30f, -0.04f, -0.50f, -0.14f, -1.38f, -0.20f},
                                        {-0.34f, 0.30f, -0.50f, 0.40f, -1.05f, 0.47f}};
        for (const Blade& bl : BLADES)
        {
            float p0x, p0y, p1x, p1y, ax, ay;
            ShipPoint(drawX, drawY, shipRotation, bl.b0x * sw, bl.b0y * sh, p0x, p0y);
            ShipPoint(drawX, drawY, shipRotation, bl.b1x * sw, bl.b1y * sh, p1x, p1y);
            ShipPoint(drawX, drawY, shipRotation, bl.ax * sw * len, bl.ay * sh, ax, ay);
            FlatTri(p0x, p0y, p1x, p1y, ax, ay, 0.012f, 0.012f, 0.022f, ink);
        }
    }
    DrawShards(time, shipIn * partsOut, false);
    // the same ship art as in the game, but large (the hero texture)
    const int shipTexture = AssetManager::Get(isRed ? AssetManager::Tex::TitleShipRed : AssetManager::Tex::TitleShipBlue);
    float engineRed = 0.12f, engineGreen = 0.66f, engineBlue = 1.00f;
    if (isRed)
    {
        engineRed = 1.00f;
        engineGreen = 0.22f;
        engineBlue = 0.16f;
    }
    // the engines: two flat flames at the nozzles (x -0.477, y +-0.080 of the texture), turned with the ship. At rest they flicker; they stretch while it launches.
    {
        const float pulse = 0.5f + 0.5f * std::sin(g_TitleShipOrbitAngle * 0.85f);
        float length = 0.13f + 0.06f * pulse;
        if (g_TitleStartTransition) length = launch <= 0.0f ? 0.13f + 0.20f * charge : 0.33f + launch * 1.35f;
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
        for (int side = -1; side <= 1; side += 2)
        {
            const float ly = 0.080f * sh * static_cast<float>(side), halfW = 0.020f * sh;
            float bx0, by0, bx1, by1, tx, ty;
            ShipPoint(drawX, drawY, shipRotation, -0.470f * sw, ly + halfW, bx0, by0);
            ShipPoint(drawX, drawY, shipRotation, -0.470f * sw, ly - halfW, bx1, by1);
            ShipPoint(drawX, drawY, shipRotation, (-0.470f - length) * sw, ly, tx, ty);
            FlatTri(bx0, by0, bx1, by1, tx, ty, engineRed, engineGreen, engineBlue, 0.55f * shipIn);
            ShipPoint(drawX, drawY, shipRotation, -0.470f * sw, ly + halfW * 0.45f, bx0, by0);
            ShipPoint(drawX, drawY, shipRotation, -0.470f * sw, ly - halfW * 0.45f, bx1, by1);
            ShipPoint(drawX, drawY, shipRotation, (-0.470f - length * 0.62f) * sw, ly, tx, ty);
            FlatTri(bx0, by0, bx1, by1, tx, ty, 1.0f, 1.0f, 1.0f, 0.70f * shipIn);
        }
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    }
    if (g_TitleStartTransition && shipTexture >= 0 && launch > 0.0f)
    {
        // afterimages of the ship as it was 1 / 2 / 3 frames ago (its own position and size): a motion blur while it accelerates, and three flat speed lines
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
        for (int k = 1; k <= 3; ++k)
        {
            const float lagged = Clamp01Local(launch - 0.085f * static_cast<float>(k));
            const float ls = ShipScaleAt(lagged);
            Renderer2D_DrawTextureEx(shipTexture, SHIP_X + ShipOffsetXAt(lagged), SHIP_Y + (-SHIP_Y) * ShipLevelAt(lagged), SHIP_W * ls, SHIP_H * ls, 0.0f,
                                     engineRed * 1.4f, engineGreen * 1.4f, engineBlue * 1.4f, (0.34f - 0.09f * static_cast<float>(k)) * launch, 0.00f, 0.00f, 1.00f, 1.00f);
        }
        for (int k = 0; k < 3; ++k)
        {
            const float ly = (static_cast<float>(k) - 1.0f) * 0.085f * sh;
            const float len = (0.10f + 0.62f * launch) * (k == 1 ? 1.25f : 1.0f); // in x units, not in ship widths: the lines stay long while the ship gets small
            float x0, y0, x1, y1;
            ShipPoint(drawX, drawY, shipRotation, -0.50f * sw, ly, x0, y0);
            ShipPoint(drawX, drawY, shipRotation, -0.50f * sw - len, ly, x1, y1);
            const Renderer2DVertex v[4] = {{x0, y0 + 0.006f, engineRed, engineGreen, engineBlue, 0.55f * launch},
                                           {x1, y1 + 0.001f, engineRed, engineGreen, engineBlue, 0.0f},
                                           {x1, y1 - 0.001f, engineRed, engineGreen, engineBlue, 0.0f},
                                           {x0, y0 - 0.006f, engineRed, engineGreen, engineBlue, 0.55f * launch}};
            Renderer2D_DrawPolygon(v, 4);
        }
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    }
    if (shipTexture >= 0)
    {
        // the hard offset shadow of the covers (down and to the right), then the ship
        Renderer2D_DrawTextureEx(shipTexture, drawX + 0.018f * scale, drawY - 0.030f * scale, sw, sh, shipRotation, 0.0f, 0.0f, 0.02f, 0.62f * shipIn * partsOut,
                                 0.00f, 0.00f, 1.00f, 1.00f);
        Renderer2D_DrawTextureEx(shipTexture, drawX, drawY, sw, sh, shipRotation, 1.00f, 1.00f, 1.00f, shipIn, 0.00f, 0.00f, 1.00f, 1.00f);
    }
    DrawShards(time, shipIn * partsOut, true);
    // how to change the colour: bottom right of the panel, on a dark plate. This corner is always inside the blue field and far from
    // the red / blue cut (which moves when the colour changes), so the text never sits on the line. It leaves to the right.
    {
        const float exit = ExitK(0.0f, 12.0f);
        const float hintIn = GameMenuUi::Entrance(18.0f, 12.0f) * (1.0f - exit);
        const std::string hint = Loc::T("SWITCH POLARITY");
        const float textW = UiKit::TextWidth(hint, 14.0f);
        const float plateW = 24.0f + 26.0f + 10.0f + textW + 30.0f;
        const float plateH = 40.0f;
        const float plateX = 1244.0f - plateW + (1.0f - GameMenuUi::Entrance(18.0f, 12.0f)) * 60.0f + exit * 420.0f;
        const float plateY = 596.0f;
        GameMenuUi::Plate(plateX, plateY, plateW, plateH, GameMenuUi::Ink(0.80f * hintIn));
        GameMenuUi::PlateLine(plateX, plateY, plateW, plateH, GameMenuUi::Ivory(0.30f * hintIn));
        UiKit::KeyCap("C", plateX + 24.0f, plateY + 7.0f, 26.0f, UiKit::Rgba(0.85f, 0.92f, 1.0f, 0.95f * hintIn));
        UiKit::Text(hint, plateX + 60.0f, plateY + plateH * 0.5f, 14.0f, UiKit::Rgba(0.92f, 0.95f, 1.0f, 0.95f * hintIn), TextAlignment::Left,
                    textW + 20.0f, false);
    }
    DrawTitleMenuItem(Loc::T("START GAME"), 0);
    DrawTitleMenuItem(Loc::T("HOW TO PLAY"), 1);
    DrawTitleMenuItem(Loc::T("RECORDS"), 2);
    DrawTitleMenuItem(Loc::T("SETTINGS"), 3);
    DrawTitleMenuItem(Loc::T("HANGAR"), 4);
    DrawTitleMenuItem(Loc::T("EXIT"), 5);
    {
        // the shard balance (and the NEW tag) at the right end of the HANGAR row
        const float top = RowCenterNorm(4) + TITLE_MENU_HEIGHT * 0.5f;
        const bool lit = GameMenuUi::SelectionAmount(TITLE_MENU_LEFT, top) > 0.5f;
        const float exit = ExitK(4.0f * 1.2f, 12.0f);
        Mod::DrawHangarRowInfo(ROW_LEFT + GameMenuUi::RowShift(top) * 640.0f - exit * 620.0f, ROW_FIRST_CENTER + ROW_STEP * 4.0f, ROW_WIDTH, lit,
                               GameMenuUi::RowAlpha(top) * (1.0f - exit));
    }
    {
        const float footer = 1.0f - ExitK(0.0f, 8.0f);
        UiKit::Text(Loc::T("ENTER / LEFT CLICK"), ROW_LEFT, 690.0f, 12.0f, UiKit::Rgba(0.54f, 0.62f, 0.76f, 0.78f * footer), TextAlignment::Left,
                    360.0f, false);
    }
    if (!g_TitleStartTransition)
    {
        Mod::DrawTitleHint(); // hangar key + shards, and the mode selector right of the START row
    }
}

void TitleScene::Reset()
{
    GameBackground::SetTitleLook(0.0f);
    g_SelectedTitleMenuItem = -1;
    GameMenuUi::SetBackdropPolarity(g_TitleShipPolarity == Polarity::Red, true);
    GameAudio::SetMusicPolarity(g_TitleShipPolarity == Polarity::Red);
    g_TitleStartTransition = false;
    g_TitleStartTransitionTimer = 0;
    g_TitleShipLaunchOffsetX = 0.0f;
    g_TitleShipLaunchOffsetY = 0.0f;
}

bool TitleScene::IsStarting()
{
    return g_TitleStartTransition;
}

bool TitleScene::IsStartGameSelected()
{
    return g_SelectedTitleMenuItem == static_cast<int>(TitleMenuItem::StartGame);
}







