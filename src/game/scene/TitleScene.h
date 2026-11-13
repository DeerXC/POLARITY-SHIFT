#pragma once

// Title screen: logo, menu, the ship that flies off when the game starts.
namespace TitleScene
{
void Update();
void Draw();

// Menu selection and the start animation go back to their initial state.
void Reset();

// The hangar mode selector only shows while START GAME is highlighted.
bool IsStartGameSelected();
// START GAME was chosen and the title is leaving (the backdrop is already the field of the game).
bool IsStarting();
}
