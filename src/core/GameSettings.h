#pragma once

#include <string>

// The settings of the player that the engine layer needs (the rest of the game reads them through here; game/GameSave.cpp saves and loads them).
//   display:  VSYNC, the frame rate cap
//   feel:     screen shake and flash strength (percent), mouse steering on / off
//   controls: one key for each action of the ship, one mouse button and one gamepad button for the four buttons actions (SHOT, SWITCH, SPECIAL, TACTIC)
//   gamepad:  vibration strength, the dead zone of the stick, the extra buttons (RT / LB / RB / B / stick click) that the pad has always had
// Menus keep their fixed keys (arrows / WASD, ENTER, SPACE, ESC, BACKSPACE, the A / B / START of the pad): only the ACTIONS of the ship are rebindable.
namespace GameSettings
{
enum class Action
{
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Shoot,
    Switch,  // the polarity
    Special, // the piercing beam
    Tactic,
    Count
};
constexpr int ACTION_COUNT = static_cast<int>(Action::Count);

// The four actions that have a mouse button and a gamepad button (the ship moves with keys, the cursor or the stick).
constexpr int BUTTON_ACTION_COUNT = 4;
inline bool IsButtonAction(Action action)
{
    return action == Action::Shoot || action == Action::Switch || action == Action::Special || action == Action::Tactic;
}
inline int ButtonSlot(Action action) // 0 shot, 1 switch, 2 special, 3 tactic (-1: the action has no button)
{
    return action == Action::Shoot ? 0 : (action == Action::Switch ? 1 : (action == Action::Special ? 2 : (action == Action::Tactic ? 3 : -1)));
}
inline Action ButtonAction(int slot) // the other way round
{
    return slot == 0 ? Action::Shoot : (slot == 1 ? Action::Switch : (slot == 2 ? Action::Special : Action::Tactic));
}

// The buttons of the gamepad that can be bound (the stick, the D-pad, START and BACK belong to the ship's move and to the menus).
enum PadButton
{
    PadNone = 0,
    PadA,
    PadB,
    PadX,
    PadY,
    PadLB,
    PadRB,
    PadLT,
    PadRT,
    PadL3,
    PadR3,
    PadButtonCount
};

struct Values
{
    bool vsync = true;
    int fpsCap = 0;          // 0 = no cap
    int shakePercent = 100;  // 0..100: how much the camera shakes
    int flashPercent = 100;  // 0..100: the white flashes and the colour fringe of hits and cut-ins
    bool mouseSteering = true;
    int keys[ACTION_COUNT] = {};
    int mouseButtons[BUTTON_ACTION_COUNT] = {}; // VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 (0 = none)
    int padButtons[BUTTON_ACTION_COUNT] = {};   // PadButton
    int vibrationPercent = 60;                  // 0..100: the rumble of the pad (0 = off)
    int deadZonePercent = 22;                   // 5..40: the dead zone of the left stick
    bool padExtraButtons = true;                // RT shoots, LB switches, RB fires the beam, B / L3 are the tactic - until the button is bound to another action
    // ---- audio (the volumes of the music and of the effects are in GameRecordData, next to the old settings)
    int masterPercent = 100;                    // 0..100: the volume of everything
    int cuePercent = 50;                        // 0..100: CUE EMPHASIS - how much the warnings are lifted and how hard they push the rest down (see GameAudio.cpp)
    bool heartbeat = true;                      // the heartbeat and the muffled music of a ship that is nearly lost
    bool muteInBackground = true;               // the sound stops while the window is not in front
};

constexpr int FPS_CAP_COUNT = 7;
constexpr int FPS_CAPS[FPS_CAP_COUNT] = {0, 60, 90, 120, 144, 165, 240};

const Values& Get();
Values& Edit(); // the settings page changes a value here; call GameSave::SaveSettings() afterwards
void ResetKeys();
void ResetMouse();
void ResetPad();
void ResetAudio(); // (the master volume, the cues, the heartbeat, the sound in the background)
int DefaultKey(Action action);
int DefaultMouseButton(Action action);
int DefaultPadButton(Action action);

// True for a key that can be bound: letters, digits, the arrows, SPACE, SHIFT, CTRL, ALT, TAB, the F keys, the numpad and the punctuation keys.
// (ESC, ENTER, BACKSPACE and the mouse buttons are not: they belong to the menus.)
bool IsBindable(int key);
// Binds `key` to `action`. When another action has the key, the two swap (the other one gets the old key of this one): there is never a key with two actions.
void BindKey(Action action, int key);
// Binds a mouse button / a pad button to an action; the one that has the button gets the old button of this one (a swap, never two actions on one button).
bool IsMouseBindable(int vk);
void BindMouseButton(Action action, int vk);
void BindPadButton(Action action, int padButton);
// The name of a key as the player reads it: "A", "SPACE", "LEFT", "SHIFT", "F5", "NUM 4" ...
std::string KeyName(int key);
std::string MouseButtonName(int vk);    // "LEFT CLICK", "RIGHT CLICK", "MIDDLE CLICK", "SIDE 1", "SIDE 2"
std::string PadButtonName(int padButton); // "A", "LB", "RT", "L STICK CLICK" ...
// The step of a percent setting for LEFT / RIGHT.
inline int StepPercent(int value, int direction)
{
    value += direction < 0 ? -10 : 10;
    return value < 0 ? 0 : (value > 100 ? 100 : value);
}
// The step of the dead zone (5 percent, 5..40).
inline int StepDeadZone(int value, int direction)
{
    value += direction < 0 ? -5 : 5;
    return value < 5 ? 5 : (value > 40 ? 40 : value);
}
}


