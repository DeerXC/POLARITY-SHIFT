#pragma once

// Pause menu and its settings page (the whole page of the title: SettingsScene, shown over the game). Opened with ESC while playing.
namespace PauseMenu
{
bool IsOpen();
void Open();
void Update();
void Draw();

// Closed, first row selected (new run, back to title).
void Reset();
}

