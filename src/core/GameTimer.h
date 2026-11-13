#pragma once

namespace GameTimer
{
void Initialize();
void BeginFrame();
bool ShouldUpdate();
void ConsumeUpdate();

// ---- frame statistics for the FPS overlay (Settings: SHOW FPS, or F3) ----------------------------------------------------
// Call StatsFrameBegin() at the top of every loop iteration, StatsBeforePresent() right before Present, StatsFrameEnd() after it.
struct FrameStats
{
    float fps = 0.0f;             // presented frames per second
    float avgMs = 0.0f;           // average time between two frames
    float maxMs = 0.0f;           // the longest frame of the last half second (a stutter shows up here)
    float cpuMs = 0.0f;           // time the CPU needed to build a frame (update + draw calls), without waiting for the display
    float updatesPerFrame = 0.0f; // 60 Hz update steps per presented frame
};
// ---- render interpolation ---------------------------------------------------------------------------------------------------
// The game logic runs in fixed steps of 1/60 s, the screen is drawn as often as the display allows (100, 144 ... frames per second).
// Without interpolation every position would be drawn as it was after the last step: on a 144 Hz display a bullet then stands still for
// one frame and jumps in the next. With interpolation an object is drawn between where it was one step ago and where it is now:
//     drawn = previous + (current - previous) * RenderAlpha()
// "Advanced" tells whether the last step really moved the world (false during the pause menu, hit stop and slow motion: nothing moves, so
// nothing is interpolated).
float RenderAlpha();                              // 0..1: how far the time has come between the last step and the next one
void SetWorldAdvanced(bool advanced);             // Game_Update clears it, PlayScene sets it when the world takes a step
void SetBackgroundAdvanced(bool advanced);        // the same for the scrolling background (GameBackground::Update)
float Blend(float previous, float current);       // a position of the world (ship, bullets, enemies, boss)
float BackgroundAlpha();                          // 0..1, or 1 when the background did not move in the last step

// The frame rate cap of the settings (GameSettings: fpsCap): waits until a frame has lasted 1 / cap seconds - a sleep for the first part, a short spin for the
// last 1.5 ms, so the frames are even. Call it once per loop, after Present. Does nothing with no cap.
void LimitFrameRate();

void StatsFrameBegin();
void StatsBeforePresent();
void StatsFrameEnd();
FrameStats GetStats(); // refreshed twice a second
}

