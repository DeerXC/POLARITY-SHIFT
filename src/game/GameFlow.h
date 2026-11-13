#pragma once
#include "game/GameTypes.h"

// Run life cycle shared by several scenes: start, restart and leave a run.
namespace GameFlow
{
// Puts every module back to the start of a run. Does not change the scene.
void ResetGameData();

// Restart (from the pause menu, the result screens or the how-to screen, or when leaving the title).
// startPolarity: the colour of the ship at the start (the title ship hands its colour over)
// fromTitle: the title screen has just shrunk its hero ship to the size of the game ship (TitleScene): the ship enters right at the edge.
void StartNewGame(Polarity startPolarity = Polarity::Blue, bool fromTitle = false);

void ReturnToTitle();
}

