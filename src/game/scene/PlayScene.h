#pragma once

// The game itself: waves, the boss fight, HUD and the world.
namespace PlayScene
{
void Update();      // one game frame (not called while the pause menu is open)

// Everything that moves, without HUD. Also drawn behind the pause menu and the game over screen.
void DrawWorld();

// World + HUD + wave messages + module chips, or the module pick screen.
void DrawObjects();

void Reset();       // new run
}
