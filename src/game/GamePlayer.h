#pragma once

#include "game/GameTypes.h"

namespace GamePlayer
{
void Initialize(Player& player);
// A run starts with the ship flying in from the left edge of the screen (about 0.7 s, the controls are locked meanwhile).
// The ship flies in from the left. matchCut: the ship has just left the title screen shrinking (TitleScene), so it enters right at the edge, a little faster.
void BeginIntro(Player& player, bool matchCut = false);
// Width of the ship as it is drawn (x units): the title shrinks its hero ship to exactly this size before the game takes over.
float DrawWidth();
bool IsIntro();
// the boss arrives (WARNING + entrance): no input, the ship glides back to its place and waits there
void SetHold(bool hold);
bool IsHeld(); // the ship is still flying in

void Update(Player& player, int& shotCoolTime);
// Render interpolation: BeginStep() first in every world step; the ship is drawn between the last two positions.
void BeginStep(const Player& player);
float GetRenderX(const Player& player);
float GetRenderY(const Player& player);

void Draw(const Player& player, int playerHp, int playerDeathTimer, int invincibleTimer);

// Only a small core of the ship can be hurt (GetHurtHalfWidth / Height); the whole ship absorbs the bullets of its colour.
// Player::halfWidth / halfHeight stay the ABSORB box (the whole ship); damage uses the hurt box below.
// Colour switch lock: after a switch the ship cannot switch again for 0.3 s (the key press is ignored).
// 1 = just switched, 0 = free. Drawn as a thin white ring around the ship so the player sees why a press was ignored.
float GetSwitchLockRate();

// Drawn BEFORE the enemy bullets: a dark halo and a polarity rim light that separate the ship from dense bullet fields.
// GamePlayer::Draw() itself is called AFTER the enemy bullets, so bullets never hide the ship.
void DrawUnder(const Player& player, int playerHp, int invincibleTimer);

float GetHurtHalfWidth(const Player& player);
float GetHurtHalfHeight(const Player& player);
void GetMoveInput(float& x, float& y); // the move of the last step (-1..1 each; the tactic DASH goes this way)
void ClampToScreen(Player& player);   // keeps the ship on the screen
}


