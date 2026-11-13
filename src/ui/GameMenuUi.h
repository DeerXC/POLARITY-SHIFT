#pragma once
#include <string>
#include "core/GameAudio.h"
#include "ui/TextTheme.h"
#include "ui/UiKit.h"

// Menu look of the game: slanted plates and hard offsets (P5), hairlines and small marks (NieR), and one backdrop that every screen
// shares: a red shape and a blue shape made of straight edges. Each screen is a layout of those edges (the title has a split panel on
// the right, the settings have a corner of each colour, the game over screen is cut low ...). A change of screen moves the edges from
// the old layout to the new one with staggered, slightly overshooting motion, so the colour is always a continuous shape that grows,
// turns and shrinks, never a wipe or a jump.
//
// Panels of the UI are hard-edged, no rounded corners, no glow. Colour is for polarity (red / blue) and for rewards (gold) only.
//
// Rectangles of the old API are in the game's normalized coordinates (-1..1), the primitives at the bottom are in virtual
// 1280x720 pixels (origin top-left).
namespace GameMenuUi
{
enum class RowStyle
{
    Quiet, // hairline only
    Normal,
    Selected,
    Danger, // restart / title buttons, not selected
    DangerSelected,
    Important // records: highlighted entry
};

// What the backdrop is drawn for. Every stage except Pause and Pick has a layout of its own (see LAYOUTS in GameMenuUi.cpp); when the
// stage changes, the shapes of the old layout turn and slide into the new one.
enum class Stage
{
    Title,     // panel on the right, split by the cut
    HowTo,     // the panel grows to the left, blue takes over
    Records,   // the panel flips to the left, red takes over
    Settings,  // two corners
    GameOver,  // the whole screen, the cut sits low
    GameClear, // the whole screen, the cut sits high
    Play,      // the same split as a faint tint behind the game
    Pause,     // two corners over the game (no layout: drawn on top of the world)
    Pick,      // black corners over the dimmed game (module pick, route)
    Hangar     // blueprint grid, corners
};

// ---- time ------------------------------------------------------------------------------------------------------------
void BeginFrame();

// ---- selection -------------------------------------------------------------------------------------------------------
// When the mouse moves and is over no row, the highlight is cleared: a menu only shows a highlight for what the mouse (or a key) points at.
// holdSelection = the mouse is over something that belongs to the highlighted row (the mode selector next to START GAME).
int UpdateMenu(int& selected, int count, int hovered, bool horizontal = false, GameSound moveSound = GameSound::MenuMove,
               bool holdSelection = false);

// ---- rows ------------------------------------------------------------------------------------------------------------
// flat = Persona style: no frame behind the row at rest, only a hairline; the highlight is still the plate that slides in.
void DrawRow(float left, float top, float right, float bottom, RowStyle style, bool flat = false);
float SelectionAmount(float left, float top);

// ---- backdrop --------------------------------------------------------------------------------------------------------
void SetBackdropPolarity(bool red, bool instant = false);
// the field behind the game (a flat red / blue over the black plate): 0..100 -> 5..22 % (settings: BACKGROUND)
void SetPlayFieldBrightness(int percent);
void UpdateBackdrop(); // once per update step (GameBackground::Update)
void SetStage(Stage stage);
void DrawBackdrop(float strength);
void DrawStage(Stage stage, float strength);
// Screen change (SceneFx): a moment into the change the red / blue cut flashes and throws its shards.
void Impact();
// x (virtual px) where the steep edge of the panel crosses the middle of the screen, as it is drawn right now (0 = no panel).

// ---- panels ----------------------------------------------------------------------------------------------------------
void DrawCard(float left, float top, float right, float bottom, float red, float green, float blue);
void DrawArrow(float centerX, float centerY, bool left, float alpha);

// ---- primitives for custom layouts (virtual pixels) -------------------------------------------------------------------
void Slab(float cx, float cy, float w, float h, float rotation, UiKit::Color c); // rotation > 0 rises to the right
void Segment(float x0, float y0, float x1, float y1, float thickness, UiKit::Color c);
void Plate(float x, float y, float w, float h, UiKit::Color c);
void PlateLine(float x, float y, float w, float h, UiKit::Color c);
void Panel(float x, float y, float w, float h, float corner, UiKit::Color fill, UiKit::Color line);
void Hairline(float x, float y, float w, float h, float thickness, UiKit::Color c);
void DiamondFill(float cx, float cy, float radius, UiKit::Color c);
void DiamondOutline(float cx, float cy, float radius, float thickness, UiKit::Color c);
void DrawHeader(const std::string& title, const std::string& sub, float progress);
void DrawPageHeader(const std::string& title); // the title of a page, red band top left (progress follows the screen entrance)

UiKit::Color Ivory(float alpha = 1.0f);
UiKit::Color Signal(float alpha = 1.0f);
UiKit::Color Ink(float alpha = 1.0f);
// THE SEAM (the language of this game: P5R red half + P3R blue half, joined): the colour of the polarity of the moment and the other one,
// both follow the turn of the backdrop smoothly
// distance (virtual px) of a point from the red / blue cut of the backdrop as it is drawn now: > 0 on the red side, < 0 on the blue side
float CutSide(float x, float y);
UiKit::Color Accent(float alpha = 1.0f);
UiKit::Color Counter(float alpha = 1.0f);
void SeamDiamond(float cx, float cy, float r, float alpha);
void SeamRule(float x, float y, float w, float progress, float alpha);
// widgets of the sub pages in THE SEAM language (virtual px)
// A notice (THE SEAM): a slanted ivory tag on a hard offset (polarity colour, or red for a refusal), a seam diamond in front, heavy ink
// letters. It slides in from the right and folds away. `timer` counts down from `total`. Replaces every coloured status text.
void Notice(const std::string& text, float cx, float cy, int timer, int total, bool refusal = false);
// a slanted band across the screen (the result screens): `slope` px of y per px of x, centred on (cx, cy), h tall
void Band(float x0, float x1, float cx, float cy, float h, float slope, UiKit::Color c);
// halftone dots (P5 print) in a slanted strip: big at x1, fading towards x0
void Halftone(float x0, float x1, float cy, float slope, float step, float maxR, UiKit::Color c);
// THE ONE MARK of every menu: the seam diamond. It is the CURSOR: it exists only in front of the row that is selected (a row at rest has none);
// the spine of a column ends in it, and so does a rule. `danger`: both halves red (the exit, a restart).
void RowMark(float cx, float cy, float lit, float alpha, bool danger = false);
// THE SELECTED TAB / TILE / CELL of every page: the ivory plate on a hard offset in the colour of the polarity (P5), a glass line of the other colour over its top edge (P3).
// Nothing is drawn OUTSIDE the plate and its offset: the neighbours of a tab or a tile stay clear. SelectTile (a rectangle) carries the seam diamond (the cursor)
// INSIDE its top left corner; a slanted tab (SelectTab) has the whole plate as its cursor.
void SelectTab(float x, float y, float w, float h, float alpha, float offset = 5.0f);
void SelectTile(float x, float y, float w, float h, float alpha, bool mark = true);
void SectionTitle(const std::string& text, float x, float y, float width, float alpha);       // seam diamond + heavy label + glass line
void SeamGauge(float x, float cy, float width, float rate, float alpha, bool lit);             // slanted cells, a seam diamond knob
void ValueTag(const std::string& text, float cx, float cy, float fontPx, float alpha, bool lit); // the value of a setting
void DrawHeavyText(const std::string& text, float x, float y, float fontPx, UiKit::Color c, TextAlignment align = TextAlignment::Left,
                   float boxWidth = 900.0f);
float EaseOut(float t);
float Clamp01(float v);

// ---- entrance of a screen (the parts do not appear together) -----------------------------------------------------------------
// StartEntrance() is called when a screen opens (SceneFx::Begin, the pause menu). Entrance(delay, duration) is 0..1 (eased) once `delay`
// frames have passed, so every part of a screen can have its own start. Rows and cards use their height on the screen as the delay: the list
// builds up from the top to the bottom, each row sliding in from the left. RowShift / RowAlpha give the same values to the texts of a row.
void StartEntrance();
float Entrance(float delayFrames, float durationFrames);
// A row that leaves the screen (the title menu when the game starts): the next DrawRow calls are moved left by leftPx and faded by alpha.
// Call SetRowExit(0, 1) afterwards.
void SetRowExit(float leftPx, float alpha);
float RowShift(float topNormalized); // x offset in normalized units (<= 0)
float RowAlpha(float topNormalized); // 0..1

void DrawCutText(const std::string& text, float x, float y, float fontPx, UiKit::Color offsetColor, float offsetPx, float alpha,
                 TextAlignment align = TextAlignment::Left, float boxWidth = 900.0f);

// ---- result screens (game over / clear) -----------------------------------------------------------------------------
constexpr float RESULT_MENU_LEFT = -0.90f;
constexpr float RESULT_MENU_RIGHT = -0.38f;
constexpr float RESULT_MENU_HEIGHT = 0.110f;
void DrawResultMenuItem(const std::string& text, float centerY, bool isSelected, bool danger);
bool IsMouseInsideRect(float left, float top, float right, float bottom);
}







