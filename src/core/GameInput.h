#pragma once
#include <Windows.h>
#include <string>
#include "core/GameSettings.h"
#undef min
#undef max

// One input layer for keyboard, mouse and gamepad (XInput). The gamepad is translated into the keys the rest of the game already reads,
// so every menu and every action works with any of the three devices alone:
//
// (the defaults; the settings page rebinds the keys, the mouse buttons and the pad buttons of the four button actions)
//   action            keyboard              mouse                      gamepad
//   move              WASD / arrows         the ship follows the cursor left stick (analog) / D-pad
//   shoot (hold)      SPACE                 left button                A / right trigger
//   switch colour     C                     right button               X / left bumper
//   special           V                     middle button / side 2     Y / right bumper
//   tactic            SHIFT                 side button 1              left trigger / B / stick click
//   pause             ESC                   the "II" button (top right) START (also pauses when the window loses focus)
//   menu move         arrows / WASD         hover                      D-pad / left stick
//   menu confirm      ENTER / SPACE         left click                 A
//   back              ESC / BACKSPACE       right click                B
//   hangar (title)    H                     the HANGAR row             the HANGAR row / SELECT
//   reroll (pick)     R                     a click on the hint        right stick button (R3)
//
// (menu rows are mapped as keys: stick / D-pad -> arrow keys, A -> ENTER + SPACE, B -> BACKSPACE, START -> ESC, X -> C, Y -> V)
namespace GameInput
{
enum class Device
{
    Keyboard,
    Mouse,
    Gamepad
};

void Initialize(HWND windowHandle);
void Update();
bool IsDown(int key);
bool IsTriggered(int key);
bool IsReleased(int key);
// ---- the actions of the ship (the keys come from GameSettings: the player can rebind them) ----------------------------------------
// An action is down when its key is down, or its mouse button, or its gamepad button: SHOOT = the key / left button / A / right trigger, SWITCH = the key /
// right button / X / left bumper, SPECIAL = the key / middle button / side 2 / Y / right bumper, TACTIC = the key / side 1 / left trigger / B / stick click,
// MOVE = the key / the D-pad (the stick is read by GetStickMove). Use these in the game; the menus keep reading fixed keys with IsDown / IsTriggered.
bool ActionDown(GameSettings::Action action);
bool ActionTriggered(GameSettings::Action action);
// For the key page of the settings: the key that has just been pressed and can be bound (0 = none yet; ESC cancels: see IsTriggered(VK_ESCAPE)).
int PollBindableKey();
// The same for the mouse page (VK_LBUTTON ... VK_XBUTTON2) and the gamepad page (GameSettings::PadButton): the button that has just been pressed, 0 = none yet.
int PollMouseButton();
int PollPadButton();
// The prompt of an action for the device in use: the pad button, the mouse button (a Loc key: "RIGHT CLICK") or the key. Use it in texts: Loc::FmtText("PRESS {0} ...", ...).
std::string ActionLabel(GameSettings::Action action);
// A pulse of the rumble of the pad (strength 0..1 before the setting VIBRATION, frames at 60 Hz). Only a pad that is in use shakes (test: also a pad that is not, for the
// test pulse of the settings page); the stronger pulse wins while one runs.
void Rumble(float strength, int frames, bool test = false);
float GetMouseX();
float GetMouseY();
bool IsMouseInsideWindow();
// True in the update step in which the mouse moved (menus use it: a resting mouse must not take the highlight).
bool MouseMoved();

// ---- device state ---------------------------------------------------------------------------------------------------------
bool IsWindowActive();
Device LastDevice();        // the device that was used last (key press / cursor move / pad input)
bool UsingGamepad();        // the last input came from the gamepad: prompts show X / Y / A ... instead of C / V / SPACE
bool GamepadConnected();
// Left stick as an analog move vector (dead zone removed, length <= 1, y up). False while the stick rests.
bool GetStickMove(float& x, float& y);
// The cursor steers the ship: it starts when the cursor travels a little, ends when a movement key or the stick is used.
bool MouseSteering();
// A menu is open over the game (pause, module pick), the title screen is showing or the ship is flying in: the cursor belongs to the menu. While blocked, the cursor never steers the ship, and
// the cursor travel of the menu does not count: after the menu closes the ship only follows the cursor again once it is moved anew.
void SetMouseSteeringBlocked(bool blocked);
// Something on the screen (a button) owns the cursor: the ship does not follow it while this is true.
void SetPointerCaptured(bool captured);
bool IsPointerCaptured();
// Text of a key prompt: the keyboard label ("C", "WASD", "SPACE" ...) or, when the gamepad is in use, the gamepad button.
std::string PromptLabel(const std::string& keyboardLabel);
// A sentence written for the default keys ("PRESS C TO CHANGE ..." / the same sentence in Chinese or Japanese): the single letters C and V (a word of their own) are replaced by the keys / pad buttons
// that the player has bound to the polarity and the beam. Texts for the mouse or the pad are not touched (they do not contain those letters as words).
std::string RebindText(const std::string& text);
}



