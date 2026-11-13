#pragma once

#include <string>

// CutIn: the big moments of a run as Persona-style cards (the style guide: P5 red / black, sharp, jagged, halftone; P3 blue, clean thin lines).
// A black band slams in across the screen at a slant, an accent band behind it, the title in heavy letters with a hard offset shadow, then
// the band snaps shut and leaves. Every card kicks a short colour fringe (PostFx::Kick).
//
//   Wave     "WAVE n"                    blue (P3): clean lines
//   Clear    "WAVE CLEAR" + the clear rate and the repair   a small quiet tag under the wave counter (it opens, stays about a second, folds away)
//   Warning  the boss is coming          red (P5): jagged edge, halftone, the band shakes
//   Phase    a boss phase starts         phase 1 red, 2 blue, 3 orange
//   Break    an overload broken          gold slash across the whole screen, short and loud
//
// Trigger() replaces the card that is showing. Update() once per game step (it keeps running in a hit stop), Draw() in the world pass
// (the band is in the HDR picture, so it glows), the texts are queued for the text pass.
namespace CutIn
{
enum class Kind
{
    Wave,
    Clear,
    Warning,
    Phase,
    Break
};

enum Accent
{
    AccentRed = 0,
    AccentBlue = 1,
    AccentWhite = 2,
    AccentGold = 3,
    AccentOrange = 4
};

// Trigger(): if a card is showing, it leaves first (a short exit) and this one comes after it - two cards never cut into each other.
// A BREAK cuts in at once (it is the answer to the player's hit).
void Trigger(Kind kind, const std::string& title, const std::string& subtitle, int accent);
// While a menu covers the game (the module pick), the cards stand still and are not drawn; when it closes, the card plays its entrance again.
void Hold(bool hold);
// the WAVE CLEAR card has docked at the top left as the header of the module pick: the pick draws its header there at once
bool Docked();
void Update();
void Draw();
void Reset();
bool Active();
}


