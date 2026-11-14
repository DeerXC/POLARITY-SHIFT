#pragma once

#include <Windows.h>

enum class DisplayMode
{
    Windowed = 0,
    Fullscreen = 1
};

struct DisplayResolution
{
    int width;
    int height;
};

namespace DisplaySystem
{
void Initialize(HWND windowHandle);

bool Apply(DisplayMode displayMode, int resolutionIndex);
// WM_ACTIVATE: in full screen the game and its black borders go on top of the task bar while the game is active.
void OnActivate(bool active);



// The sizes the Settings page offers: windowed = the windows that fit on the screen, full screen = the native size and the supersamples up to twice as wide.
int FirstResolutionIndex(DisplayMode mode);
int LastResolutionIndex(DisplayMode mode);
int NormalizeResolution(DisplayMode mode, int resolutionIndex);
int StepResolution(DisplayMode mode, int resolutionIndex, int direction); // +1 / -1, wraps around

DisplayResolution GetResolution(int resolutionIndex);

// Full screen at a size larger than the picture on the screen: the world is drawn at that size and scaled down (a supersample, see DisplayMath.h).
bool IsSupersampled(DisplayMode mode, int resolutionIndex);

// The monitor changed (WM_DISPLAYCHANGE: a resolution change, a monitor that was plugged in or out): in full screen the back buffer is made the size of the screen again.
void OnDisplayChange();

}

