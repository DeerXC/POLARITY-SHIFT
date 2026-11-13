#pragma once

namespace GameBackground
{
// Stateless drawing of one frame of the background.
void Draw(float frame, bool bossWarning, bool bossBattle, int bossPhase); // frame may be fractional (between two steps)

// The same, driven by the frame counter below and the current boss state.
void DrawCurrent();

// The title screen is handing over to the game (START GAME): clouds, light shafts and streaks of the game fade in with this value (0..1).
void SetTitleLook(float look);
void Reset();       // new run: restart the frame counter (the stars keep their own clock, they must not jump)
void Update();      // once per game update
int GetFrame();     // frames since Reset(); other screens use it for their pulse animations
}
