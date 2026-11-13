#include "core/GameSettings.h"

#include <Windows.h>

namespace
{
GameSettings::Values g_Values;
bool g_Initialized = false;

void EnsureInitialized()
{
    if (g_Initialized) return;
    g_Initialized = true;
    GameSettings::ResetKeys();
    GameSettings::ResetMouse();
    GameSettings::ResetPad();
}
}

int GameSettings::DefaultKey(Action action)
{
    switch (action)
    {
    case Action::MoveUp: return 'W';
    case Action::MoveDown: return 'S';
    case Action::MoveLeft: return 'A';
    case Action::MoveRight: return 'D';
    case Action::Shoot: return VK_SPACE;
    case Action::Switch: return 'C';
    case Action::Special: return 'V';
    case Action::Tactic: return VK_SHIFT;
    default: return 0;
    }
}

int GameSettings::DefaultMouseButton(Action action)
{
    switch (action)
    {
    case Action::Shoot: return VK_LBUTTON;
    case Action::Switch: return VK_RBUTTON;
    case Action::Special: return VK_MBUTTON;
    case Action::Tactic: return VK_XBUTTON1;
    default: return 0;
    }
}

int GameSettings::DefaultPadButton(Action action)
{
    switch (action)
    {
    case Action::Shoot: return PadA;
    case Action::Switch: return PadX;
    case Action::Special: return PadY;
    case Action::Tactic: return PadLT;
    default: return PadNone;
    }
}

const GameSettings::Values& GameSettings::Get()
{
    EnsureInitialized();
    return g_Values;
}

GameSettings::Values& GameSettings::Edit()
{
    EnsureInitialized();
    return g_Values;
}

void GameSettings::ResetKeys()
{
    g_Initialized = true;
    for (int i = 0; i < ACTION_COUNT; ++i) g_Values.keys[i] = DefaultKey(static_cast<Action>(i));
}

void GameSettings::ResetMouse()
{
    g_Initialized = true;
    for (int i = 0; i < BUTTON_ACTION_COUNT; ++i) g_Values.mouseButtons[i] = DefaultMouseButton(ButtonAction(i));
}

void GameSettings::ResetAudio()
{
    g_Initialized = true;
    g_Values.masterPercent = 100;
    g_Values.cuePercent = 50;
    g_Values.heartbeat = true;
    g_Values.muteInBackground = true;
}

void GameSettings::ResetPad()
{
    g_Initialized = true;
    for (int i = 0; i < BUTTON_ACTION_COUNT; ++i) g_Values.padButtons[i] = DefaultPadButton(ButtonAction(i));
    g_Values.vibrationPercent = 60;
    g_Values.deadZonePercent = 22;
    g_Values.padExtraButtons = true;
}

bool GameSettings::IsMouseBindable(int vk)
{
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

void GameSettings::BindMouseButton(Action action, int vk)
{
    EnsureInitialized();
    const int slot = ButtonSlot(action);
    if (slot < 0 || !IsMouseBindable(vk)) return;
    const int old = g_Values.mouseButtons[slot];
    for (int i = 0; i < BUTTON_ACTION_COUNT; ++i)
    {
        if (i != slot && g_Values.mouseButtons[i] == vk) g_Values.mouseButtons[i] = old; // swap
    }
    g_Values.mouseButtons[slot] = vk;
}

void GameSettings::BindPadButton(Action action, int padButton)
{
    EnsureInitialized();
    const int slot = ButtonSlot(action);
    if (slot < 0 || padButton <= PadNone || padButton >= PadButtonCount) return;
    const int old = g_Values.padButtons[slot];
    for (int i = 0; i < BUTTON_ACTION_COUNT; ++i)
    {
        if (i != slot && g_Values.padButtons[i] == padButton) g_Values.padButtons[i] = old; // swap
    }
    g_Values.padButtons[slot] = padButton;
}

std::string GameSettings::MouseButtonName(int vk)
{
    switch (vk)
    {
    case VK_LBUTTON: return "LEFT CLICK";
    case VK_RBUTTON: return "RIGHT CLICK";
    case VK_MBUTTON: return "MIDDLE CLICK";
    case VK_XBUTTON1: return "SIDE 1";
    case VK_XBUTTON2: return "SIDE 2";
    default: return "NONE";
    }
}

std::string GameSettings::PadButtonName(int padButton)
{
    switch (padButton)
    {
    case PadA: return "A";
    case PadB: return "B";
    case PadX: return "X";
    case PadY: return "Y";
    case PadLB: return "LB";
    case PadRB: return "RB";
    case PadLT: return "LT";
    case PadRT: return "RT";
    case PadL3: return "L3";
    case PadR3: return "R3";
    default: return "NONE";
    }
}

bool GameSettings::IsBindable(int key)
{
    if (key >= 'A' && key <= 'Z') return true;
    if (key >= '0' && key <= '9') return true;
    if (key >= VK_LEFT && key <= VK_DOWN) return true;
    if (key >= VK_NUMPAD0 && key <= VK_DIVIDE) return true;
    if (key >= VK_F1 && key <= VK_F12 && key != VK_F3) return true; // (F3 is the FPS overlay)
    switch (key)
    {
    case VK_SPACE:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_TAB:
    case VK_CAPITAL:
    case VK_OEM_1:
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
        return true;
    default:
        return false;
    }
}

void GameSettings::BindKey(Action action, int key)
{
    EnsureInitialized();
    if (!IsBindable(key)) return;
    // the generic Shift / Ctrl keys: the left and the right one are both "the key"
    if (key == VK_LSHIFT || key == VK_RSHIFT) key = VK_SHIFT;
    if (key == VK_LCONTROL || key == VK_RCONTROL) key = VK_CONTROL;
    const int index = static_cast<int>(action);
    const int old = g_Values.keys[index];
    for (int i = 0; i < ACTION_COUNT; ++i)
    {
        if (i != index && g_Values.keys[i] == key) g_Values.keys[i] = old; // swap
    }
    g_Values.keys[index] = key;
}

std::string GameSettings::KeyName(int key)
{
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, static_cast<char>(key));
    switch (key)
    {
    case VK_SPACE: return "SPACE";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "SHIFT";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "CTRL";
    case VK_MENU: return "ALT";
    case VK_TAB: return "TAB";
    case VK_CAPITAL: return "CAPS";
    case VK_LEFT: return "LEFT";
    case VK_RIGHT: return "RIGHT";
    case VK_UP: return "UP";
    case VK_DOWN: return "DOWN";
    case VK_INSERT: return "INS";
    case VK_DELETE: return "DEL";
    case VK_HOME: return "HOME";
    case VK_END: return "END";
    case VK_PRIOR: return "PGUP";
    case VK_NEXT: return "PGDN";
    case VK_MULTIPLY: return "NUM *";
    case VK_ADD: return "NUM +";
    case VK_SUBTRACT: return "NUM -";
    case VK_DECIMAL: return "NUM .";
    case VK_DIVIDE: return "NUM /";
    default: break;
    }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return std::string("NUM ") + static_cast<char>('0' + key - VK_NUMPAD0);
    if (key >= VK_F1 && key <= VK_F12) return "F" + std::to_string(key - VK_F1 + 1);
    // punctuation: ask Windows for the label of the key on this keyboard
    const UINT scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
    char text[32]{};
    if (scan != 0 && GetKeyNameTextA(static_cast<LONG>(scan << 16), text, sizeof text) > 0) return text;
    return "KEY " + std::to_string(key);
}


