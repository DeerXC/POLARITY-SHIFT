#include "core/GameInput.h"
#include <Windows.h>
#include <xinput.h>
#include <cmath>

#pragma comment(lib, "xinput.lib")

namespace
{
constexpr int KEY_COUNT = 256;
bool g_CurrentKeys[KEY_COUNT]{};
bool g_PreviousKeys[KEY_COUNT]{};
bool g_RealKeys[KEY_COUNT]{}; // what the keyboard / mouse really reports (without the keys that the gamepad adds)
bool g_PreviousRealKeys[KEY_COUNT]{};
float g_MouseX = 0.0f;
float g_MouseY = 0.0f;
bool g_IsMouseInsideWindow = false;
bool g_MouseMoved = false;
float g_PreviousMouseX = 0.0f;
float g_PreviousMouseY = 0.0f;
HWND g_WindowHandle = nullptr;
bool g_WindowActive = false;

GameInput::Device g_LastDevice = GameInput::Device::Keyboard;
bool g_MouseSteering = false;
bool g_SteeringBlocked = false;
int g_SteerGrace = 0; // update steps after a block has ended in which the cursor still cannot take the ship
constexpr int STEER_GRACE_FRAMES = 36;
float g_MouseTravel = 0.0f;
bool g_PointerCaptured = false;
bool g_ActionDown[GameSettings::ACTION_COUNT]{};
bool g_PreviousActionDown[GameSettings::ACTION_COUNT]{};

// ---- gamepad ----------------------------------------------------------------------------------------------------------
float StickDeadZone() { return static_cast<float>(GameSettings::Get().deadZonePercent) / 100.0f; } // radial dead zone of the analog move (settings: STICK DEAD ZONE)
constexpr float STICK_PRESS = 0.55f;      // the stick presses an arrow key (menus) beyond this
constexpr BYTE TRIGGER_PRESS = 90;        // 0..255
bool g_PadConnected = false;
int g_PadIndex = -1;
int g_PadScan = 60; // update steps until the next search for a pad (searching an empty slot is slow, so it is not done every step)
WORD g_PadButtons = 0;
WORD g_PreviousPadButtons = 0;
float g_StickX = 0.0f;
float g_StickY = 0.0f;
float g_StickRawX = 0.0f;
float g_StickRawY = 0.0f;
bool g_LeftTrigger = false;
bool g_RightTrigger = false;
bool g_PadActivity = false;
unsigned g_PadMask = 0;         // bit n = the PadButton n is down (the bindable buttons of the pad, the triggers included)
unsigned g_PreviousPadMask = 0;
// ---- rumble: a pulse that fades out (strength 0..1, frames at 60 Hz), scaled by the setting VIBRATION
float g_RumbleStrength = 0.0f;
int g_RumbleFrames = 0;
int g_RumbleTotal = 0;
bool g_RumbleTest = false; // the pulse is a test of the settings page: it shakes a pad that is not the device in use too
WORD g_RumbleSentLow = 0;
WORD g_RumbleSentHigh = 0;

bool IsValidKey(int key)
{
    return key >= 0 && key < KEY_COUNT;
}

void ClearCurrentInput()
{
    for (int key = 0; key < KEY_COUNT; key++)
    {
        g_CurrentKeys[key] = false;
        g_PreviousKeys[key] = false;
        g_RealKeys[key] = false;
        g_PreviousRealKeys[key] = false;
    }
    g_PadButtons = 0;
    g_PreviousPadButtons = 0;
    g_StickX = g_StickY = g_StickRawX = g_StickRawY = 0.0f;
    g_LeftTrigger = g_RightTrigger = false;
    g_PadActivity = false;
    g_PadMask = 0;
    g_PreviousPadMask = 0;
}

void UpdateMousePosition()
{
    if (g_WindowHandle == nullptr)
    {
        g_IsMouseInsideWindow = false;
        return;
    }
    POINT mousePosition{};
    if (!GetCursorPos(&mousePosition))
    {
        g_IsMouseInsideWindow = false;
        return;
    }
    if (!ScreenToClient(g_WindowHandle, &mousePosition))
    {
        g_IsMouseInsideWindow = false;
        return;
    }
    RECT clientRect{};
    if (!GetClientRect(g_WindowHandle, &clientRect))
    {
        g_IsMouseInsideWindow = false;
        return;
    }
    const int clientWidth = clientRect.right - clientRect.left;
    const int clientHeight = clientRect.bottom - clientRect.top;
    if (clientWidth <= 0 || clientHeight <= 0)
    {
        g_IsMouseInsideWindow = false;
        return;
    }
    // the game is the largest 16:9 rectangle in the middle of the window (DirectXSystem::GetGameRect): the mouse is measured in it
    int gameW = clientWidth;
    int gameH = clientWidth * 9 / 16;
    if (gameH > clientHeight)
    {
        gameH = clientHeight;
        gameW = clientHeight * 16 / 9;
    }
    if (clientWidth - gameW <= 2 && clientHeight - gameH <= 2)
    {
        gameW = clientWidth;
        gameH = clientHeight;
    }
    const int gameX = (clientWidth - gameW) / 2;
    const int gameY = (clientHeight - gameH) / 2;
    const int localX = mousePosition.x - gameX;
    const int localY = mousePosition.y - gameY;
    g_IsMouseInsideWindow = localX >= 0 && localX < gameW && localY >= 0 && localY < gameH;
    const float normalizedX = static_cast<float>(localX) / static_cast<float>(gameW);
    const float normalizedY = static_cast<float>(localY) / static_cast<float>(gameH);
    g_MouseX = normalizedX * 2.0f - 1.0f;
    g_MouseY = 1.0f - normalizedY * 2.0f;
    g_MouseMoved = g_IsMouseInsideWindow
                   && (std::fabs(g_MouseX - g_PreviousMouseX) > 0.002f || std::fabs(g_MouseY - g_PreviousMouseY) > 0.002f);
    // how far the cursor has been moved lately (it fades away): a hand resting on the mouse does not take the ship, a sweep does
    g_MouseTravel *= 0.92f;
    if (g_MouseMoved)
    {
        g_MouseTravel += std::fabs(g_MouseX - g_PreviousMouseX) + std::fabs(g_MouseY - g_PreviousMouseY);
    }
    g_PreviousMouseX = g_MouseX;
    g_PreviousMouseY = g_MouseY;
}

float Axis(SHORT value)
{
    const float v = static_cast<float>(value) / 32767.0f;
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

unsigned MaskOf(WORD buttons, bool leftTrigger, bool rightTrigger)
{
    using namespace GameSettings;
    unsigned mask = 0;
    auto set = [&](PadButton button, bool down)
    {
        if (down) mask |= 1u << static_cast<unsigned>(button);
    };
    set(PadA, (buttons & XINPUT_GAMEPAD_A) != 0);
    set(PadB, (buttons & XINPUT_GAMEPAD_B) != 0);
    set(PadX, (buttons & XINPUT_GAMEPAD_X) != 0);
    set(PadY, (buttons & XINPUT_GAMEPAD_Y) != 0);
    set(PadLB, (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
    set(PadRB, (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    set(PadLT, leftTrigger);
    set(PadRT, rightTrigger);
    set(PadL3, (buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
    set(PadR3, (buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
    return mask;
}

bool PadDown(int button)
{
    return button > 0 && button < GameSettings::PadButtonCount && (g_PadMask & (1u << static_cast<unsigned>(button))) != 0;
}

void SendRumble(WORD low, WORD high)
{
    if (g_PadIndex < 0 || (low == g_RumbleSentLow && high == g_RumbleSentHigh)) return;
    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = low;
    vibration.wRightMotorSpeed = high;
    XInputSetState(static_cast<DWORD>(g_PadIndex), &vibration);
    g_RumbleSentLow = low;
    g_RumbleSentHigh = high;
}

// once per update step: the pulse of the rumble fades out. Only a pad that is in use shakes (a pad on the desk next to the keyboard stays quiet).
void UpdateRumble()
{
    const bool wanted = g_RumbleFrames > 0 && g_PadConnected && (g_LastDevice == GameInput::Device::Gamepad || g_RumbleTest) && GameSettings::Get().vibrationPercent > 0;
    if (!wanted)
    {
        g_RumbleFrames = 0;
        SendRumble(0, 0);
        return;
    }
    const float life = static_cast<float>(g_RumbleFrames) / static_cast<float>(g_RumbleTotal > 0 ? g_RumbleTotal : 1);
    const float amount = g_RumbleStrength * life * static_cast<float>(GameSettings::Get().vibrationPercent) / 100.0f;
    // the left motor is the heavy one (the body of a hit), the right one the light one (the crack of it)
    const float low = amount > 1.0f ? 1.0f : amount;
    const float high = low * (0.45f + 0.55f * (1.0f - life));
    SendRumble(static_cast<WORD>(low * 65535.0f), static_cast<WORD>(high * 65535.0f));
    --g_RumbleFrames;
    if (g_RumbleFrames <= 0) SendRumble(0, 0);
}

void PollGamepad()
{
    g_PreviousPadButtons = g_PadButtons;
    g_PreviousPadMask = g_PadMask;
    XINPUT_STATE state{};
    bool got = false;
    if (g_PadIndex >= 0)
    {
        got = XInputGetState(static_cast<DWORD>(g_PadIndex), &state) == ERROR_SUCCESS;
        if (!got) g_PadIndex = -1;
    }
    if (!got)
    {
        if (++g_PadScan >= 60)
        {
            g_PadScan = 0;
            for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
            {
                if (XInputGetState(i, &state) == ERROR_SUCCESS)
                {
                    g_PadIndex = static_cast<int>(i);
                    got = true;
                    break;
                }
            }
        }
    }
    g_PadConnected = got;
    if (!got)
    {
        g_PadMask = 0;
        g_RumbleSentLow = g_RumbleSentHigh = 0;
        g_PadButtons = 0;
        g_StickX = g_StickY = g_StickRawX = g_StickRawY = 0.0f;
        g_LeftTrigger = g_RightTrigger = false;
        g_PadActivity = false;
        return;
    }
    g_PadButtons = state.Gamepad.wButtons;
    g_StickRawX = Axis(state.Gamepad.sThumbLX);
    g_StickRawY = Axis(state.Gamepad.sThumbLY);
    g_LeftTrigger = state.Gamepad.bLeftTrigger > TRIGGER_PRESS;
    g_RightTrigger = state.Gamepad.bRightTrigger > TRIGGER_PRESS;
    g_PadMask = MaskOf(g_PadButtons, g_LeftTrigger, g_RightTrigger);
    // radial dead zone: the length counts from the edge of the dead zone, so the ship does not start with a jump
    const float length = std::sqrt(g_StickRawX * g_StickRawX + g_StickRawY * g_StickRawY);
    const float deadZone = StickDeadZone();
    if (length < deadZone)
    {
        g_StickX = g_StickY = 0.0f;
    }
    else
    {
        float scaled = (length - deadZone) / (1.0f - deadZone);
        if (scaled > 1.0f) scaled = 1.0f;
        g_StickX = g_StickRawX / length * scaled;
        g_StickY = g_StickRawY / length * scaled;
    }
    g_PadActivity = ((g_PadButtons & ~g_PreviousPadButtons) != 0) || length > 0.6f || g_LeftTrigger || g_RightTrigger;
}

// the gamepad adds keys to g_CurrentKeys: the menus and the game read keys only
void ApplyGamepadKeys()
{
    auto press = [](int key)
    {
        if (IsValidKey(key)) g_CurrentKeys[key] = true;
    };
    const WORD b = g_PadButtons;
    if (b & XINPUT_GAMEPAD_DPAD_UP || g_StickRawY > STICK_PRESS) press(VK_UP);
    if (b & XINPUT_GAMEPAD_DPAD_DOWN || g_StickRawY < -STICK_PRESS) press(VK_DOWN);
    if (b & XINPUT_GAMEPAD_DPAD_LEFT || g_StickRawX < -STICK_PRESS) press(VK_LEFT);
    if (b & XINPUT_GAMEPAD_DPAD_RIGHT || g_StickRawX > STICK_PRESS) press(VK_RIGHT);
    if (b & XINPUT_GAMEPAD_A)
    {
        press(VK_RETURN); // menu: confirm
        press(VK_SPACE);  // game: shoot
    }
    if (g_RightTrigger) press(VK_SPACE);
    if (b & XINPUT_GAMEPAD_B)
    {
        press(VK_BACK);     // menu: back
        press(VK_XBUTTON1); // game: tactic
    }
    if (b & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_LEFT_SHOULDER)) press('C'); // switch colour
    if (b & (XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_SHOULDER)) press('V'); // special
    if (g_LeftTrigger || (b & XINPUT_GAMEPAD_LEFT_THUMB)) press(VK_SHIFT);   // tactic
    if (b & XINPUT_GAMEPAD_START) press(VK_ESCAPE);                          // pause / back
    if (b & XINPUT_GAMEPAD_BACK) press('H');                                 // hangar
    if (b & XINPUT_GAMEPAD_RIGHT_THUMB) press('R');                          // reroll (module pick screen)
}

bool MovementKeyDown()
{
    using GameSettings::Action;
    const GameSettings::Values& v = GameSettings::Get();
    return g_RealKeys[VK_LEFT] || g_RealKeys[VK_RIGHT] || g_RealKeys[VK_UP] || g_RealKeys[VK_DOWN] || g_RealKeys['A'] || g_RealKeys['D'] || g_RealKeys['W']
           || g_RealKeys['S'] || g_RealKeys[v.keys[static_cast<int>(Action::MoveUp)]] || g_RealKeys[v.keys[static_cast<int>(Action::MoveDown)]]
           || g_RealKeys[v.keys[static_cast<int>(Action::MoveLeft)]] || g_RealKeys[v.keys[static_cast<int>(Action::MoveRight)]];
}

// The buttons that a button action has besides the one the player bound (they have always worked: RT shoots, LB switches, RB fires the beam, B and the stick click
// are the tactic; SIDE 2 fires the beam). An extra button works until the player binds it to another action.
struct ExtraButton
{
    int slot;
    int button;
};
constexpr ExtraButton PAD_EXTRAS[] = {{0, GameSettings::PadRT}, {1, GameSettings::PadLB}, {2, GameSettings::PadRB}, {3, GameSettings::PadB}, {3, GameSettings::PadL3}};

bool PadBoundTo(const GameSettings::Values& v, int button, int exceptSlot)
{
    for (int i = 0; i < GameSettings::BUTTON_ACTION_COUNT; ++i)
        if (i != exceptSlot && v.padButtons[i] == button) return true;
    return false;
}

bool MouseBoundTo(const GameSettings::Values& v, int button, int exceptSlot)
{
    for (int i = 0; i < GameSettings::BUTTON_ACTION_COUNT; ++i)
        if (i != exceptSlot && v.mouseButtons[i] == button) return true;
    return false;
}

// The actions of the ship from the REAL keys (the bound key, the mouse buttons) and the pad. The keys that the pad adds to g_CurrentKeys are not used here: a pad
// button is read as itself, so rebinding the keyboard never changes the pad.
void UpdateActions()
{
    using GameSettings::Action;
    const GameSettings::Values& v = GameSettings::Get();
    auto real = [](int key) { return IsValidKey(key) && g_RealKeys[key]; };
    const WORD b = g_PadButtons;
    for (int i = 0; i < GameSettings::ACTION_COUNT; ++i) g_PreviousActionDown[i] = g_ActionDown[i];
    auto set = [](Action a, bool down) { g_ActionDown[static_cast<int>(a)] = down; };
    auto key = [&](Action a) { return real(v.keys[static_cast<int>(a)]); };
    set(Action::MoveUp, key(Action::MoveUp) || (b & XINPUT_GAMEPAD_DPAD_UP) != 0);
    set(Action::MoveDown, key(Action::MoveDown) || (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
    set(Action::MoveLeft, key(Action::MoveLeft) || (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
    set(Action::MoveRight, key(Action::MoveRight) || (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
    for (int slot = 0; slot < GameSettings::BUTTON_ACTION_COUNT; ++slot)
    {
        const Action action = GameSettings::ButtonAction(slot);
        bool down = key(action) || real(v.mouseButtons[slot]) || PadDown(v.padButtons[slot]);
        if (slot == 2 && !MouseBoundTo(v, VK_XBUTTON2, slot)) down = down || real(VK_XBUTTON2); // SIDE 2 fires the beam until it is bound to something else
        if (v.padExtraButtons)
        {
            for (const ExtraButton& extra : PAD_EXTRAS)
                if (extra.slot == slot && !PadBoundTo(v, extra.button, slot)) down = down || PadDown(extra.button);
        }
        set(action, down);
    }
}
}

void GameInput::Initialize(HWND windowHandle)
{
    g_WindowHandle = windowHandle;
    ClearCurrentInput();
    g_MouseX = 0.0f;
    g_MouseY = 0.0f;
    g_IsMouseInsideWindow = false;
    g_LastDevice = Device::Keyboard;
    g_MouseSteering = false;
    g_MouseTravel = 0.0f;
    g_PadScan = 60;
}

void GameInput::Update()
{
    g_WindowActive = g_WindowHandle != nullptr && GetForegroundWindow() == g_WindowHandle;
    if (!g_WindowActive)
    {
        g_RumbleFrames = 0;
        SendRumble(0, 0); // (a pad must not go on shaking in a window that is not in front)
        ClearCurrentInput();
        g_IsMouseInsideWindow = false;
        g_MouseMoved = false;
        g_MouseSteering = false;
        return;
    }
    bool keyboardActivity = false;
    bool mouseButtonActivity = false;
    for (int key = 0; key < KEY_COUNT; key++)
    {
        g_PreviousKeys[key] = g_CurrentKeys[key];

        g_PreviousRealKeys[key] = g_RealKeys[key];
        g_RealKeys[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        g_CurrentKeys[key] = g_RealKeys[key];
        if (g_RealKeys[key] && !g_PreviousRealKeys[key])
        {
            if (key >= VK_LBUTTON && key <= VK_XBUTTON2 && key != VK_CANCEL)
                mouseButtonActivity = true; // 0x01..0x06 are the mouse buttons
            else
                keyboardActivity = true;
        }
    }
    PollGamepad();
    ApplyGamepadKeys();
    UpdateActions();
    UpdateMousePosition();
    // the mouse buttons that the game reads: the middle button fires the special (like V)
    if (g_RealKeys[VK_MBUTTON]) g_CurrentKeys['V'] = true;
    // which device is in use
    if (g_PadActivity)
        g_LastDevice = Device::Gamepad;
    else if (keyboardActivity)
        g_LastDevice = Device::Keyboard;
    else if (g_MouseMoved || mouseButtonActivity)
        g_LastDevice = Device::Mouse;
    UpdateRumble();
    // the cursor steers the ship after it has travelled a little; a movement key or the stick takes the ship back
    if (g_SteeringBlocked || g_SteerGrace > 0)
    {
        // a menu, the title screen, the entrance of the ship: the cursor belongs to somebody else. The travel of the cursor is not counted
        // meanwhile, and for a short time after it ends, so that a cursor that is still moving (the click on START GAME) cannot take the ship.
        if (!g_SteeringBlocked) --g_SteerGrace;
        g_MouseSteering = false;
        g_MouseTravel = 0.0f;
    }
    else if (MovementKeyDown() || g_StickX != 0.0f || g_StickY != 0.0f)
    {
        g_MouseSteering = false;
        g_MouseTravel = 0.0f;
    }
    else if (!g_MouseSteering && g_MouseTravel > 0.08f)
    {
        g_MouseSteering = true;
    }
}

bool GameInput::IsDown(int key)
{
    if (!IsValidKey(key))
    {
        return false;
    }
    return g_CurrentKeys[key];
}

bool GameInput::IsTriggered(int key)
{
    if (!IsValidKey(key))
    {
        return false;
    }
    return g_CurrentKeys[key] && !g_PreviousKeys[key];
}

bool GameInput::ActionDown(GameSettings::Action action)
{
    return g_ActionDown[static_cast<int>(action)];
}

bool GameInput::ActionTriggered(GameSettings::Action action)
{
    const int i = static_cast<int>(action);
    return g_ActionDown[i] && !g_PreviousActionDown[i];
}

int GameInput::PollBindableKey()
{
    for (int key = 0; key < KEY_COUNT; ++key)
    {
        if (g_RealKeys[key] && !g_PreviousRealKeys[key] && GameSettings::IsBindable(key)) return key;
    }
    return 0;
}

int GameInput::PollMouseButton()
{
    for (int key = VK_LBUTTON; key <= VK_XBUTTON2; ++key)
    {
        if (key == VK_CANCEL) continue;
        if (g_RealKeys[key] && !g_PreviousRealKeys[key] && GameSettings::IsMouseBindable(key)) return key;
    }
    return 0;
}

int GameInput::PollPadButton()
{
    for (int button = GameSettings::PadA; button < GameSettings::PadButtonCount; ++button)
    {
        const unsigned bit = 1u << static_cast<unsigned>(button);
        if ((g_PadMask & bit) != 0 && (g_PreviousPadMask & bit) == 0) return button;
    }
    return 0;
}

void GameInput::Rumble(float strength, int frames, bool test)
{
    if (frames <= 0 || strength <= 0.0f) return;
    if (strength > 1.0f) strength = 1.0f;
    const float current = g_RumbleTotal > 0 ? g_RumbleStrength * static_cast<float>(g_RumbleFrames) / static_cast<float>(g_RumbleTotal) : 0.0f;
    if (g_RumbleFrames > 0 && strength < current) return; // the stronger pulse wins while one is running
    g_RumbleStrength = strength;
    g_RumbleFrames = frames;
    g_RumbleTotal = frames;
    g_RumbleTest = test;
}

bool GameInput::IsReleased(int key)
{
    if (!IsValidKey(key))
    {
        return false;
    }
    return !g_CurrentKeys[key] && g_PreviousKeys[key];
}

float GameInput::GetMouseX()
{
    return g_MouseX;
}

float GameInput::GetMouseY()
{
    return g_MouseY;
}

bool GameInput::IsMouseInsideWindow()
{
    return g_IsMouseInsideWindow;
}

bool GameInput::MouseMoved()
{
    return g_MouseMoved;
}

bool GameInput::IsWindowActive()
{
    return g_WindowActive;
}

GameInput::Device GameInput::LastDevice()
{
    return g_LastDevice;
}

bool GameInput::UsingGamepad()
{
    return g_LastDevice == Device::Gamepad && g_PadConnected;
}

bool GameInput::GamepadConnected()
{
    return g_PadConnected;
}

bool GameInput::GetStickMove(float& x, float& y)
{
    x = g_StickX;
    y = g_StickY;
    return g_StickX != 0.0f || g_StickY != 0.0f;
}

void GameInput::SetMouseSteeringBlocked(bool blocked)
{
    if (g_SteeringBlocked && !blocked) g_SteerGrace = STEER_GRACE_FRAMES;
    g_SteeringBlocked = blocked;
    if (blocked)
    {
        g_MouseSteering = false;
        g_MouseTravel = 0.0f;
    }
}

bool GameInput::MouseSteering()
{
    return g_MouseSteering && g_IsMouseInsideWindow && GameSettings::Get().mouseSteering;
}

void GameInput::SetPointerCaptured(bool captured)
{
    g_PointerCaptured = captured;
}

bool GameInput::IsPointerCaptured()
{
    return g_PointerCaptured;
}

std::string GameInput::RebindText(const std::string& text)
{
    const std::string c = PromptLabel("C");  // the bound key, or the bound pad button while the pad is in use
    const std::string vv = PromptLabel("V");
    if (c == "C" && vv == "V") return text;
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];
        const bool before = i == 0 || text[i - 1] == ' ' || static_cast<unsigned char>(text[i - 1]) >= 0x80;
        const bool after = i + 1 >= text.size() || text[i + 1] == ' ' || static_cast<unsigned char>(text[i + 1]) >= 0x80 || text[i + 1] == ',' || text[i + 1] == '.';
        if ((ch == 'C' || ch == 'V') && before && after) out += ch == 'C' ? c : vv;
        else out += ch;
    }
    return out;
}

std::string GameInput::ActionLabel(GameSettings::Action action)
{
    const GameSettings::Values& v = GameSettings::Get();
    const int slot = GameSettings::ButtonSlot(action);
    if (UsingGamepad() && slot >= 0) return GameSettings::PadButtonName(v.padButtons[slot]);
    if (g_LastDevice == Device::Mouse && slot >= 0) return GameSettings::MouseButtonName(v.mouseButtons[slot]);
    return GameSettings::KeyName(v.keys[static_cast<int>(action)]);
}

std::string GameInput::PromptLabel(const std::string& keyboardLabel)
{
    using GameSettings::Action;
    const GameSettings::Values& v = GameSettings::Get();
    if (!UsingGamepad())
    {
        // the prompts of the game are written for the default keys: a rebound action shows its own key
        if (keyboardLabel == "C") return GameSettings::KeyName(v.keys[static_cast<int>(Action::Switch)]);
        if (keyboardLabel == "V") return GameSettings::KeyName(v.keys[static_cast<int>(Action::Special)]);
        if (keyboardLabel == "SHIFT") return GameSettings::KeyName(v.keys[static_cast<int>(Action::Tactic)]);
        if (keyboardLabel == "SPACE") return GameSettings::KeyName(v.keys[static_cast<int>(Action::Shoot)]);
        if (keyboardLabel == "WASD")
        {
            // the four move keys one after the other when each is a single character (WASD, IJKL ...), else the name of the group
            std::string keys;
            const Action order[4] = {Action::MoveUp, Action::MoveLeft, Action::MoveDown, Action::MoveRight};
            bool single = true;
            for (const Action a : order)
            {
                const std::string name = GameSettings::KeyName(v.keys[static_cast<int>(a)]);
                if (name.size() != 1) single = false;
                keys += name;
            }
            if (!single) return "MOVE KEYS";
            return keys == "WASD" ? "WASD" : keys;
        }
        return keyboardLabel;
    }
    // the pad: the buttons that the player bound
    if (keyboardLabel == "WASD" || keyboardLabel == "ARROWS") return keyboardLabel == "WASD" ? "L STICK" : "D-PAD";
    if (keyboardLabel == "ENTER") return "A"; // (the menus: confirm)
    if (keyboardLabel == "SPACE") return GameSettings::PadButtonName(v.padButtons[GameSettings::ButtonSlot(Action::Shoot)]);
    if (keyboardLabel == "SHIFT") return GameSettings::PadButtonName(v.padButtons[GameSettings::ButtonSlot(Action::Tactic)]);
    if (keyboardLabel == "C") return GameSettings::PadButtonName(v.padButtons[GameSettings::ButtonSlot(Action::Switch)]);
    if (keyboardLabel == "V") return GameSettings::PadButtonName(v.padButtons[GameSettings::ButtonSlot(Action::Special)]);
    if (keyboardLabel == "H") return "SELECT";
    if (keyboardLabel == "ESC") return "B";
    if (keyboardLabel == "R") return "R3";
    return keyboardLabel;
}
