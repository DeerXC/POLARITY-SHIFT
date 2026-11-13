#pragma once

// Camera shake for hits and explosions. The strongest request wins while a shake is running.
namespace ScreenShake
{
void Start(int duration, float strength);
void Update();      // once per game update
void Apply();       // before drawing the world: moves the camera, or clears the offset when no shake is running
void Reset();
}
