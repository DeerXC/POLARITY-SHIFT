#pragma once

#include "game/GameTypes.h"

// Everything the in-game HUD needs from PlayScene. GameHud keeps no references
// to the game's globals, so it can be tested on its own.
struct HudState
{
    int score = 0;
    int hp = 0;
    int maxHp = 5;
    int energy = 0;
    int maxEnergy = 100;
    int combo = 0;
    int multiplier = 1;
    float comboRate = 0.0f; // 0..1, remaining combo time
    Polarity polarity = Polarity::Blue;
    float playerX = 0.0f; // normalized game coordinates
    float playerY = 0.0f;
    int wave = 1;
    int waveTotal = 6;
    bool bossActive = false; // boss fight started (warning included)
    float chain = 1.0f;      // 0..1: the energy an absorbed bullet gives right now (the chain of absorbs cools down)
};

namespace GameHud
{
void Reset();                       // new run / retry
void BeginIntro();                  // a run starts: the corners of the HUD slide in one after the other
void Update(const HudState& state); // once per game update (60 Hz)
void Draw(const HudState& state);   // once per rendered frame

// Pause button for mouse-only play: drawn at the top right while the mouse steers the ship, clicking it opens the pause menu.
bool PauseButtonHovered();
}

