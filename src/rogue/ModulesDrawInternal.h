#pragma once
#include "rogue/ModulesInternal.h"
#include "ui/UiKit.h"
#include <string>

// Shared by ModulesDraw.cpp and ModulesDrawMenus.cpp: icon cells, colours and the small drawing helpers.
namespace Mod
{
// ---------------------------------------------------------------- icon atlas (textures/ui/module_icons.png, 8 x 4 cells of 128 px)
// (tools/make_module_icons.py draws the atlas; keep ICON_CELL / ICON_ATLAS_W / ICON_ATLAS_H in sync with the texture)
constexpr int ICON_PLUS = 15; // cells 0..14 are the modules (cell = Mod::Id), 15..18 the symbols of the interface, 19 / 20 / 29 the symbols of the route screen,
constexpr int ICON_MINUS = 16; // 21..23 the tactics, 24..28 the talents (TacticInfo / TalentInfo::icon)
constexpr int ICON_LOCK = 17;
constexpr int ICON_SHARD = 18;
constexpr int ICON_ELITE = 19; // the starburst (elite fleet)
constexpr int ICON_FORGE = 20; // the arrow (forge)
constexpr int ICON_SUPPLY = 29; // the drop (supply run)
constexpr int ICON_WAGER = static_cast<int>(Id::Pulse);
constexpr int ICON_COLS = 8;
constexpr float ICON_CELL = 128.0f;
constexpr float ICON_ATLAS_W = 1024.0f;
constexpr float ICON_ATLAS_H = 512.0f;

inline const UiKit::Color GOLD = UiKit::Rgba(1.00f, 0.80f, 0.30f, 1.0f);
inline const UiKit::Color DIM = UiKit::Rgba(0.62f, 0.70f, 0.85f, 0.92f);
inline const UiKit::Color WHITE = UiKit::Rgba(0.97f, 0.98f, 1.00f, 1.0f);
inline const UiKit::Color PRACTICE_BLUE = UiKit::Rgba(0.55f, 0.86f, 1.00f, 1.0f);
inline const UiKit::Color DANGER_RED = UiKit::Rgba(1.00f, 0.50f, 0.46f, 1.0f);
inline const UiKit::Color INK = UiKit::Rgba(0.05f, 0.06f, 0.09f, 1.0f);
inline const UiKit::Color SIGNAL_RED = UiKit::Rgba(0.88f, 0.17f, 0.15f, 1.0f);
inline const UiKit::Color GOLD_ON_LIGHT = UiKit::Rgba(0.62f, 0.42f, 0.04f, 1.0f);

void DrawIcon(int cell, float x, float y, float size, UiKit::Color c);
UiKit::Color VerbColor(Verb verb);
UiKit::Color Alpha(UiKit::Color c, float a);
void Txt(const std::string& text, float x, float y, float px, UiKit::Color c, TextAlignment align, float boxWidth);
float Chip(float x, float y, float height, const std::string& text, float px, UiKit::Color tint, float maxWidth);
std::string ModeLabel(int mode);
UiKit::Color LevelColor(int level);
UiKit::Color ModeColor(int mode);
// both return how far the next row has to move down (a long text wraps into two lines)
// the looping animation of a synergy (a 76 px strip at x, y), `frame` = the animation clock
void DrawSynergyDemo(int combo, float x, float y, float w, float h, int frame);
// the looping animation of a module (hangar id 0 .. COUNT - 1) or a tactic (TACTIC_ID0 ..) in a strip at x, y (w x h px): what it does, drawn from the shapes of ui/DemoKit
void DrawModuleDemo(int hangarId, float x, float y, float w, float h, int frame);
// the hangar: which synergy cell (an index into Meta::COMBOS) is at a point of the screen (virtual px), -1 = none; the next found synergy from `current` by
// (dRow, dCol) in the two-column list (wraps; current < 0 gives the first); the first found one
int HangarSynergyAt(float px, float py);
int HangarSynergyStep(int current, int dRow, int dCol);
int HangarSynergyFirst();
float DrawGainRow(const char* key, float x, float y, float maxWidth, float px, bool onLight = false);
float DrawCostRow(const char* key, float x, float y, float maxWidth, float px, bool onLight = false);
}


