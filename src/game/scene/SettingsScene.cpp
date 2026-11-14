#include "game/scene/SettingsScene.h"
#include <cstdlib>
#include "core/DisplaySystem.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/GameSettings.h"
#include "core/Loc.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/GameBackground.h"
#include "game/GameSave.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "ui/GameMenuUi.h"
#include "ui/TextTheme.h"
#include <Windows.h>
#include <string>
namespace
{
using SettingsMenuItem = SettingsScene::Item;
using GameSettings::Action;

// ---- the pages ----------------------------------------------------------------------------------------------------------------------------
// Every setting takes effect as it is changed (there is no APPLY button). The display mode and the resolution wait until the player has stopped on a value for
// DISPLAY_SETTLE frames (stepping through the resolutions would otherwise resize the window at every step).
constexpr int PAGE_COUNT = 6;
const char* const PAGE_NAMES[PAGE_COUNT] = {"DISPLAY", "GAME", "AUDIO", "KEYBOARD", "MOUSE", "GAMEPAD"};
constexpr int PAGE_KEYBOARD = 3, PAGE_MOUSE = 4, PAGE_GAMEPAD = 5;
const SettingsMenuItem PAGE_ROWS[PAGE_COUNT][10] = {
    {SettingsMenuItem::DisplayMode, SettingsMenuItem::Resolution, SettingsMenuItem::VSync, SettingsMenuItem::FpsCap, SettingsMenuItem::ShowFps},
    {SettingsMenuItem::Language, SettingsMenuItem::Background, SettingsMenuItem::ScreenShake, SettingsMenuItem::Flashes},
    {SettingsMenuItem::MasterVolume, SettingsMenuItem::BgmVolume, SettingsMenuItem::SeVolume, SettingsMenuItem::CueEmphasis, SettingsMenuItem::Heartbeat,
     SettingsMenuItem::MuteInBackground},
    {SettingsMenuItem::KeyMoveUp, SettingsMenuItem::KeyMoveDown, SettingsMenuItem::KeyMoveLeft, SettingsMenuItem::KeyMoveRight, SettingsMenuItem::KeyShoot,
     SettingsMenuItem::KeySwitch, SettingsMenuItem::KeySpecial, SettingsMenuItem::KeyTactic, SettingsMenuItem::ResetKeys},
    {SettingsMenuItem::MouseSteering, SettingsMenuItem::MouseShoot, SettingsMenuItem::MouseSwitch, SettingsMenuItem::MouseSpecial, SettingsMenuItem::MouseTactic,
     SettingsMenuItem::ResetMouse},
    {SettingsMenuItem::PadShoot, SettingsMenuItem::PadSwitch, SettingsMenuItem::PadSpecial, SettingsMenuItem::PadTactic, SettingsMenuItem::PadExtraButtons,
     SettingsMenuItem::Vibration, SettingsMenuItem::DeadZone, SettingsMenuItem::ResetPad}};
const int PAGE_ROW_COUNT[PAGE_COUNT] = {5, 4, 6, 9, 6, 8};

// geometry (virtual 1280 x 720 px)
constexpr float TAB_X = 345.0f, TAB_Y = 148.0f, TAB_W = 90.0f, TAB_H = 32.0f, TAB_GAP = 10.0f; // (six tabs across the 590 px of the rows)
constexpr float ROW_TOP = 206.0f, ROW_PITCH = 44.0f, ROW_H = 38.0f;
constexpr float ROW_LEFT_PX = 345.0f, ROW_RIGHT_PX = 935.0f;
constexpr int DISPLAY_SETTLE = 30;
constexpr int NOTICE_FRAMES = 100;

int g_Page = 0;
int g_PageT = 100;               // frames since the page changed (the rows slide in)
int g_Selected = -1;             // the row, -1 = nothing highlighted; the last row of a page is BACK
DisplayMode g_PendingDisplayMode = DisplayMode::Windowed;
int g_PendingResolutionIndex = 0;
int g_SettingsAppliedTimer = 0;  // the notice (SETTINGS APPLIED / failed)
bool g_NoticeRefusal = false;
int g_NoticeKind = 0;            // 0 settings applied, 1 display failed, 2 key set, 3 keys reset, 4 button set, 5 mouse reset, 6 pad reset, 7 no pad
int g_DisplaySettle = 0;         // > 0: a display change waits to be applied
bool g_Embedded = false;         // the page is open over the game (the pause menu), not as a scene of its own
bool g_EmbeddedClosed = false;   // ... and the player has left it
int g_LocalFrame = 0;            // the clock of the page while it is embedded (the background stands still while the game is paused)
bool g_Capturing = false;        // waiting for the key / the mouse button / the pad button of an action
int g_CaptureT = 0;
int g_CaptureKind = 0;           // 0 a key, 1 a mouse button, 2 a pad button

int Frame() { return g_Embedded ? g_LocalFrame : GameBackground::GetFrame(); }
float NormY(float py) { return 1.0f - py / 360.0f; }
float NormX(float px) { return px / 640.0f - 1.0f; }

int RowCount() { return PAGE_ROW_COUNT[g_Page] + 1; } // + BACK
SettingsMenuItem ItemOfRow(int row) { return row >= PAGE_ROW_COUNT[g_Page] ? SettingsMenuItem::Back : PAGE_ROWS[g_Page][row]; }
float RowCenterPx(int row) { return ROW_TOP + ROW_PITCH * static_cast<float>(row) + ROW_H * 0.5f; }
float RowTopNorm(int row) { return NormY(RowCenterPx(row) - ROW_H * 0.5f); }

bool IsKeyItem(SettingsMenuItem item) { return item >= SettingsMenuItem::KeyMoveUp && item <= SettingsMenuItem::KeyTactic; }
bool IsMouseButtonItem(SettingsMenuItem item) { return item >= SettingsMenuItem::MouseShoot && item <= SettingsMenuItem::MouseTactic; }
bool IsPadButtonItem(SettingsMenuItem item) { return item >= SettingsMenuItem::PadShoot && item <= SettingsMenuItem::PadTactic; }
bool IsButtonItem(SettingsMenuItem item) { return IsKeyItem(item) || IsMouseButtonItem(item) || IsPadButtonItem(item); }
bool IsResetItem(SettingsMenuItem item) { return item == SettingsMenuItem::ResetKeys || item == SettingsMenuItem::ResetMouse || item == SettingsMenuItem::ResetPad; }
Action ActionOf(SettingsMenuItem item) { return static_cast<Action>(static_cast<int>(item) - static_cast<int>(SettingsMenuItem::KeyMoveUp)); }
Action MouseActionOf(SettingsMenuItem item) { return GameSettings::ButtonAction(static_cast<int>(item) - static_cast<int>(SettingsMenuItem::MouseShoot)); }
Action PadActionOf(SettingsMenuItem item) { return GameSettings::ButtonAction(static_cast<int>(item) - static_cast<int>(SettingsMenuItem::PadShoot)); }

bool IsPercentItem(SettingsMenuItem item)
{
    return item == SettingsMenuItem::Background || item == SettingsMenuItem::ScreenShake || item == SettingsMenuItem::Flashes || item == SettingsMenuItem::BgmVolume
           || item == SettingsMenuItem::SeVolume || item == SettingsMenuItem::Vibration || item == SettingsMenuItem::DeadZone || item == SettingsMenuItem::MasterVolume
           || item == SettingsMenuItem::CueEmphasis;
}

int PercentOf(SettingsMenuItem item)
{
    switch (item)
    {
    case SettingsMenuItem::Background: return GameSave::GetRecordData().backgroundPercent;
    case SettingsMenuItem::ScreenShake: return GameSettings::Get().shakePercent;
    case SettingsMenuItem::Flashes: return GameSettings::Get().flashPercent;
    case SettingsMenuItem::BgmVolume: return static_cast<int>(GameAudio::GetBgmVolume() * 100.0f + 0.5f);
    case SettingsMenuItem::SeVolume: return static_cast<int>(GameAudio::GetSeVolume() * 100.0f + 0.5f);
    case SettingsMenuItem::Vibration: return GameSettings::Get().vibrationPercent;
    case SettingsMenuItem::DeadZone: return GameSettings::Get().deadZonePercent;
    case SettingsMenuItem::MasterVolume: return GameSettings::Get().masterPercent;
    case SettingsMenuItem::CueEmphasis: return GameSettings::Get().cuePercent;
    default: return 0;
    }
}

// how far the gauge is filled: the dead zone runs 5..40 %, everything else 0..100 %
float GaugeOf(SettingsMenuItem item)
{
    if (item == SettingsMenuItem::DeadZone) return static_cast<float>(PercentOf(item) - 5) / 35.0f;
    return static_cast<float>(PercentOf(item)) / 100.0f;
}

const char* LabelKey(SettingsMenuItem item)
{
    switch (item)
    {
    case SettingsMenuItem::DisplayMode: return "DISPLAY MODE";
    case SettingsMenuItem::Resolution: return "RESOLUTION";
    case SettingsMenuItem::VSync: return "VSYNC";
    case SettingsMenuItem::FpsCap: return "FPS LIMIT";
    case SettingsMenuItem::ShowFps: return "SHOW FPS";
    case SettingsMenuItem::Language: return "LANGUAGE";
    case SettingsMenuItem::Background: return "BACKGROUND";
    case SettingsMenuItem::ScreenShake: return "SCREEN SHAKE";
    case SettingsMenuItem::Flashes: return "FLASHES";
    case SettingsMenuItem::MouseSteering: return "MOUSE STEERING";
    case SettingsMenuItem::BgmVolume: return "BGM VOLUME";
    case SettingsMenuItem::SeVolume: return "SE VOLUME";
    case SettingsMenuItem::MasterVolume: return "MASTER VOLUME";
    case SettingsMenuItem::CueEmphasis: return "CUE EMPHASIS";
    case SettingsMenuItem::Heartbeat: return "LOW HP HEARTBEAT";
    case SettingsMenuItem::MuteInBackground: return "MUTE IN BACKGROUND";
    case SettingsMenuItem::KeyMoveUp: return "MOVE UP";
    case SettingsMenuItem::KeyMoveDown: return "MOVE DOWN";
    case SettingsMenuItem::KeyMoveLeft: return "MOVE LEFT";
    case SettingsMenuItem::KeyMoveRight: return "MOVE RIGHT";
    case SettingsMenuItem::KeyShoot: return "SHOT";
    case SettingsMenuItem::KeySwitch: return "SWITCH POLARITY";
    case SettingsMenuItem::KeySpecial: return "SPECIAL";
    case SettingsMenuItem::KeyTactic: return "TACTIC";
    case SettingsMenuItem::ResetKeys: return "RESET KEYS";
    case SettingsMenuItem::MouseShoot: case SettingsMenuItem::PadShoot: return "SHOT";
    case SettingsMenuItem::MouseSwitch: case SettingsMenuItem::PadSwitch: return "SWITCH POLARITY";
    case SettingsMenuItem::MouseSpecial: case SettingsMenuItem::PadSpecial: return "SPECIAL";
    case SettingsMenuItem::MouseTactic: case SettingsMenuItem::PadTactic: return "TACTIC";
    case SettingsMenuItem::ResetMouse: return "RESET MOUSE BUTTONS";
    case SettingsMenuItem::PadExtraButtons: return "EXTRA BUTTONS";
    case SettingsMenuItem::Vibration: return "VIBRATION";
    case SettingsMenuItem::DeadZone: return "STICK DEAD ZONE";
    case SettingsMenuItem::ResetPad: return "RESET GAMEPAD";
    default: return "BACK";
    }
}

// the one-line explanation under the page (the row that is highlighted)
const char* HintKey(SettingsMenuItem item)
{
    switch (item)
    {
    case SettingsMenuItem::VSync: return "HINT VSYNC";
    case SettingsMenuItem::FpsCap: return "HINT FPS LIMIT";
    case SettingsMenuItem::ShowFps: return "HINT SHOW FPS";
    case SettingsMenuItem::Background: return "HINT BACKGROUND";
    case SettingsMenuItem::ScreenShake: return "HINT SCREEN SHAKE";
    case SettingsMenuItem::Flashes: return "HINT FLASHES";
    case SettingsMenuItem::MouseSteering: return "HINT MOUSE STEERING";
    case SettingsMenuItem::ResetKeys: return "HINT RESET KEYS";
    case SettingsMenuItem::ResetMouse: return "HINT RESET MOUSE";
    case SettingsMenuItem::ResetPad: return "HINT RESET PAD";
    case SettingsMenuItem::PadExtraButtons: return "HINT PAD EXTRA";
    case SettingsMenuItem::Vibration: return "HINT VIBRATION";
    case SettingsMenuItem::DeadZone: return "HINT DEAD ZONE";
    case SettingsMenuItem::MasterVolume: return "HINT MASTER VOLUME";
    case SettingsMenuItem::CueEmphasis: return "HINT CUE EMPHASIS";
    case SettingsMenuItem::Heartbeat: return "HINT HEARTBEAT";
    case SettingsMenuItem::MuteInBackground: return "HINT MUTE IN BACKGROUND";
    default:
        if (IsKeyItem(item)) return "HINT KEY";
        if (IsMouseButtonItem(item)) return "HINT MOUSE BUTTON";
        if (IsPadButtonItem(item)) return "HINT PAD BUTTON";
        return nullptr;
    }
}

std::string FpsCapText(int cap) { return cap <= 0 ? Loc::T("NO LIMIT") : std::to_string(cap); }

void SaveCurrentAudioVolumes()
{
    const int bgmVolumePercent = static_cast<int>(GameAudio::GetBgmVolume() * 100.0f + 0.5f);
    const int seVolumePercent = static_cast<int>(GameAudio::GetSeVolume() * 100.0f + 0.5f);
    GameSave::SetAudioVolumes(bgmVolumePercent, seVolumePercent);
}

void Notify(int kind, bool refusal)
{
    g_SettingsAppliedTimer = NOTICE_FRAMES;
    g_NoticeKind = kind;
    g_NoticeRefusal = refusal;
}

void ApplyPendingDisplaySettings()
{
    const bool applied = DisplaySystem::Apply(g_PendingDisplayMode, g_PendingResolutionIndex);
    if (!applied)
    {
        GameAudio::Play(GameSound::MenuError);
        Notify(1, true);
        return;
    }
    GameSave::SetDisplaySettings(g_PendingDisplayMode == DisplayMode::Fullscreen ? 1 : 0, g_PendingResolutionIndex);
    Notify(0, false);
    GameAudio::Play(GameSound::MenuApply);
}

void SetPage(int page)
{
    page = (page % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT;
    if (page == g_Page) return;
    g_Page = page;
    g_PageT = 0;
    g_Selected = -1;
    g_Capturing = false;
    GameAudio::Play(GameSound::MenuAdjust);
}

void ChangeSetting(SettingsMenuItem item, int direction)
{
    if (direction == 0) return;
    GameSettings::Values& v = GameSettings::Edit();
    bool saveSettings = true;
    switch (item)
    {
    case SettingsMenuItem::DisplayMode:
        g_PendingDisplayMode = g_PendingDisplayMode == DisplayMode::Windowed ? DisplayMode::Fullscreen : DisplayMode::Windowed;
        g_PendingResolutionIndex = DisplaySystem::NormalizeResolution(g_PendingDisplayMode, g_PendingResolutionIndex);
        g_DisplaySettle = DISPLAY_SETTLE;
        saveSettings = false;
        break;
    case SettingsMenuItem::Resolution:
        g_PendingResolutionIndex = DisplaySystem::StepResolution(g_PendingDisplayMode, g_PendingResolutionIndex, direction);
        g_DisplaySettle = DISPLAY_SETTLE;
        saveSettings = false;
        break;
    case SettingsMenuItem::VSync:
        v.vsync = !v.vsync;
        break;
    case SettingsMenuItem::FpsCap:
    {
        int index = 0;
        for (int i = 0; i < GameSettings::FPS_CAP_COUNT; ++i)
            if (GameSettings::FPS_CAPS[i] == v.fpsCap) index = i;
        index = (index + direction + GameSettings::FPS_CAP_COUNT) % GameSettings::FPS_CAP_COUNT;
        v.fpsCap = GameSettings::FPS_CAPS[index];
        break;
    }
    case SettingsMenuItem::ShowFps:
        GameSave::SetShowFps(GameSave::GetRecordData().showFps == 0);
        saveSettings = false;
        break;
    case SettingsMenuItem::Language:
        Loc::SetLanguage((Loc::GetLanguage() + direction + 3) % 3);
        saveSettings = false;
        break;
    case SettingsMenuItem::Background:
        GameSave::SetBackgroundPercent(GameSettings::StepPercent(GameSave::GetRecordData().backgroundPercent, direction));
        GameMenuUi::SetPlayFieldBrightness(GameSave::GetRecordData().backgroundPercent);
        saveSettings = false;
        break;
    case SettingsMenuItem::ScreenShake:
        v.shakePercent = GameSettings::StepPercent(v.shakePercent, direction);
        break;
    case SettingsMenuItem::Flashes:
        v.flashPercent = GameSettings::StepPercent(v.flashPercent, direction);
        break;
    case SettingsMenuItem::MouseSteering:
        v.mouseSteering = !v.mouseSteering;
        break;
    case SettingsMenuItem::PadExtraButtons:
        v.padExtraButtons = !v.padExtraButtons;
        break;
    case SettingsMenuItem::Vibration:
        v.vibrationPercent = GameSettings::StepPercent(v.vibrationPercent, direction);
        GameInput::Rumble(0.8f, 22, true); // (a test pulse, even when the pad is not the device in use)
        break;
    case SettingsMenuItem::DeadZone:
        v.deadZonePercent = GameSettings::StepDeadZone(v.deadZonePercent, direction);
        break;
    case SettingsMenuItem::MasterVolume:
        v.masterPercent = GameSettings::StepPercent(v.masterPercent, direction);
        break; // (the sound of the adjustment plays at the new volume)
    case SettingsMenuItem::CueEmphasis:
        v.cuePercent = GameSettings::StepPercent(v.cuePercent, direction);
        GameAudio::Play(GameSound::WhiteWarn); // (a sample: how loud a warning is at this setting)
        GameSave::SaveSettings();
        return;
    case SettingsMenuItem::Heartbeat:
        v.heartbeat = !v.heartbeat;
        if (v.heartbeat) GameAudio::Play(GameSound::LowHpPulse); // (a sample)
        GameSave::SaveSettings();
        return;
    case SettingsMenuItem::MuteInBackground:
        v.muteInBackground = !v.muteInBackground;
        break;
    case SettingsMenuItem::BgmVolume:
    case SettingsMenuItem::SeVolume:
        SettingsScene::AdjustVolume(item, direction);
        return; // (AdjustVolume plays the sound and saves)
    default:
        return;
    }
    if (saveSettings) GameSave::SaveSettings();
    GameAudio::Play(GameSound::MenuAdjust);
}

void LeavePage()
{
    if (g_DisplaySettle > 0)
    {
        g_DisplaySettle = 0; // (leaving the page: a waiting display change is applied now)
        ApplyPendingDisplaySettings();
    }
    if (g_Embedded)
    {
        g_EmbeddedClosed = true; // (the pause menu takes over; the scene is still the game)
    }
    else
    {
        GameState::scene = GameState::Scene::Title;
    }
    GameAudio::Play(GameSound::MenuDecide);
}

int HoveredTab()
{
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        const float x = TAB_X + static_cast<float>(i) * (TAB_W + TAB_GAP);
        if (GameMenuUi::IsMouseInsideRect(NormX(x), NormY(TAB_Y), NormX(x + TAB_W), NormY(TAB_Y + TAB_H))) return i;
    }
    return -1;
}

int HoveredRow()
{
    if (!GameInput::IsMouseInsideWindow()) return -1;
    for (int i = 0; i < RowCount(); ++i)
    {
        const float top = RowCenterPx(i) - ROW_H * 0.5f;
        if (GameMenuUi::IsMouseInsideRect(NormX(ROW_LEFT_PX), NormY(top), NormX(ROW_RIGHT_PX), NormY(top + ROW_H))) return i;
    }
    return -1;
}

// ---- drawing ------------------------------------------------------------------------------------------------------------------------------
void DrawTabs()
{
    const float a = GameMenuUi::Clamp01(GameMenuUi::Entrance(2.0f, 12.0f));
    const int hovered = HoveredTab();
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        const float x = TAB_X + static_cast<float>(i) * (TAB_W + TAB_GAP);
        const bool selected = i == g_Page;
        if (selected)
        {
            GameMenuUi::SelectTab(x, TAB_Y, TAB_W, TAB_H, a, 5.0f);
        }
        else
        {
            if (i == hovered) GameMenuUi::Plate(x, TAB_Y, TAB_W, TAB_H, GameMenuUi::Ivory(0.10f * a));
            GameMenuUi::PlateLine(x, TAB_Y, TAB_W, TAB_H, GameMenuUi::Ivory(0.30f * a));
        }
        const std::string name = Loc::T(PAGE_NAMES[i]);
        GameMenuUi::DrawHeavyText(name, x + TAB_W * 0.5f, TAB_Y + TAB_H * 0.5f, UiKit::FitFontPx(name, 14.0f, TAB_W - 20.0f, 8.0f),
                                  selected ? GameMenuUi::Ink(a) : UiKit::Rgba(0.72f, 0.78f, 0.88f, 0.95f * a), TextAlignment::Center, TAB_W);
    }
}

void DrawRowOf(int row)
{
    const SettingsMenuItem item = ItemOfRow(row);
    const bool isSelected = g_Selected == row;
    const float cyPx = RowCenterPx(row);
    const float top = RowTopNorm(row);
    const float bottom = NormY(cyPx + ROW_H * 0.5f);
    // the rows of a page build up from the top (the page change restarts the delay)
    const float pageIn = GameMenuUi::EaseOut(GameMenuUi::Clamp01((static_cast<float>(g_PageT) - static_cast<float>(row) * 2.0f) / 10.0f));
    const float shiftX = GameMenuUi::RowShift(top);
    const float rowA = GameMenuUi::RowAlpha(top) * pageIn;
    const float sPx = shiftX * 640.0f - (1.0f - pageIn) * 40.0f;
    GameMenuUi::DrawRow(NormX(ROW_LEFT_PX), top, NormX(ROW_RIGHT_PX), bottom, isSelected ? GameMenuUi::RowStyle::Selected : GameMenuUi::RowStyle::Normal, true);
    const bool lit = GameMenuUi::SelectionAmount(NormX(ROW_LEFT_PX), top) > 0.5f;
    const UiKit::Color ink = lit ? GameMenuUi::Ink(rowA) : UiKit::Rgba(0.82f, 0.85f, 0.92f, 0.95f * rowA);
    const std::string label = Loc::T(LabelKey(item));
    if (item == SettingsMenuItem::Back)
    {
        GameMenuUi::DrawHeavyText(label, 640.0f + sPx, cyPx, 16.0f, lit ? GameMenuUi::Ink(rowA) : GameMenuUi::Ivory(0.92f * rowA), TextAlignment::Center, 560.0f);
        return;
    }
    if (IsResetItem(item))
    {
        GameMenuUi::DrawHeavyText(label, ROW_LEFT_PX + 28.0f + sPx, cyPx, 15.0f, ink, TextAlignment::Left, 400.0f);
        return;
    }
    GameMenuUi::DrawHeavyText(label, ROW_LEFT_PX + 28.0f + sPx, cyPx, 15.0f, ink, TextAlignment::Left, 300.0f);
    // a display change that waits to be applied: a thin bar under the value runs out, then it applies
    if ((item == SettingsMenuItem::DisplayMode || item == SettingsMenuItem::Resolution) && g_DisplaySettle > 0)
    {
        const float k = static_cast<float>(g_DisplaySettle) / static_cast<float>(DISPLAY_SETTLE);
        UiKit::Fill(733.0f + sPx, cyPx + 13.0f, 120.0f * k, 2.0f, GameMenuUi::Counter(0.9f * rowA));
    }
    const float vx = 793.0f + sPx;
    if (IsPercentItem(item))
    {
        const int percent = PercentOf(item);
        GameMenuUi::SeamGauge(696.0f + sPx, cyPx, 150.0f, GaugeOf(item), rowA, lit);
        GameMenuUi::DrawHeavyText(std::to_string(percent), 884.0f + sPx, cyPx, 14.0f, lit ? GameMenuUi::Ink(rowA) : GameMenuUi::Ivory(0.95f * rowA), TextAlignment::Center, 56.0f);
    }
    else if (IsButtonItem(item))
    {
        const bool waiting = g_Capturing && isSelected;
        std::string text;
        if (waiting)
            text = Loc::T(IsKeyItem(item) ? "PRESS A KEY" : "PRESS A BUTTON");
        else if (IsKeyItem(item))
            text = GameSettings::KeyName(GameSettings::Get().keys[static_cast<int>(ActionOf(item))]);
        else if (IsMouseButtonItem(item))
            text = Loc::T(GameSettings::MouseButtonName(GameSettings::Get().mouseButtons[GameSettings::ButtonSlot(MouseActionOf(item))]).c_str());
        else
            text = GameSettings::PadButtonName(GameSettings::Get().padButtons[GameSettings::ButtonSlot(PadActionOf(item))]);
        const bool blink = waiting && (g_CaptureT / 15) % 2 == 0;
        GameMenuUi::ValueTag(text, 800.0f + sPx, cyPx, waiting ? 13.0f : 15.0f, blink ? rowA * 0.45f : rowA, lit);
        return; // (a button is changed with ENTER: no arrows)
    }
    else
    {
        std::string value;
        switch (item)
        {
        case SettingsMenuItem::DisplayMode: value = Loc::T(g_PendingDisplayMode == DisplayMode::Fullscreen ? "FULLSCREEN" : "WINDOWED"); break;
        case SettingsMenuItem::Resolution:
        {
            const DisplayResolution r = DisplaySystem::GetResolution(g_PendingResolutionIndex);
            value = std::to_string(r.width) + " X " + std::to_string(r.height);
            if (DisplaySystem::IsSupersampled(g_PendingDisplayMode, g_PendingResolutionIndex)) value += "  " + Loc::T("SUPERSAMPLED"); // (drawn larger than the screen and scaled down: smoother edges)
            break;
        }
        case SettingsMenuItem::VSync: value = Loc::T(GameSettings::Get().vsync ? "ON" : "OFF"); break;
        case SettingsMenuItem::FpsCap: value = FpsCapText(GameSettings::Get().fpsCap); break;
        case SettingsMenuItem::ShowFps: value = Loc::T(GameSave::GetRecordData().showFps != 0 ? "ON" : "OFF"); break;
        case SettingsMenuItem::Language: value = Loc::GetLanguageName(); break;
        case SettingsMenuItem::MouseSteering: value = Loc::T(GameSettings::Get().mouseSteering ? "ON" : "OFF"); break;
        case SettingsMenuItem::PadExtraButtons: value = Loc::T(GameSettings::Get().padExtraButtons ? "ON" : "OFF"); break;
        case SettingsMenuItem::Heartbeat: value = Loc::T(GameSettings::Get().heartbeat ? "ON" : "OFF"); break;
        case SettingsMenuItem::MuteInBackground: value = Loc::T(GameSettings::Get().muteInBackground ? "ON" : "OFF"); break;
        default: break;
        }
        GameMenuUi::ValueTag(value, vx, cyPx, item == SettingsMenuItem::Resolution ? 14.0f : 15.0f, rowA, lit);
    }
    float arrowAlpha = 0.38f;
    if (isSelected) arrowAlpha = 0.76f + static_cast<float>((Frame() / 8) % 3) * 0.08f;
    GameMenuUi::DrawArrow(NormX(669.0f) + shiftX, NormY(cyPx), true, arrowAlpha * rowA);
    GameMenuUi::DrawArrow(NormX(918.0f) + shiftX, NormY(cyPx), false, arrowAlpha * rowA);
}
}

void SettingsScene::AdjustVolume(Item item, int direction)
{
    constexpr float VOLUME_STEP = 0.10f;
    const float delta = direction < 0 ? -VOLUME_STEP : VOLUME_STEP;
    switch (item)
    {
    case SettingsMenuItem::BgmVolume:
        GameAudio::SetBgmVolume(GameAudio::GetBgmVolume() + delta);
        break;
    case SettingsMenuItem::SeVolume:
        GameAudio::SetSeVolume(GameAudio::GetSeVolume() + delta);
        break;
    default:
        return;
    }
    SaveCurrentAudioVolumes();
}

void SettingsScene::LoadSavedDisplaySettings()
{
    const GameRecordData& recordData = GameSave::GetRecordData();
    g_PendingDisplayMode = recordData.displayMode == 1 ? DisplayMode::Fullscreen : DisplayMode::Windowed;
    g_PendingResolutionIndex = DisplaySystem::NormalizeResolution(g_PendingDisplayMode, recordData.resolutionIndex);
    GameMenuUi::SetPlayFieldBrightness(recordData.backgroundPercent);
}

void SettingsScene::Open()
{
    g_Embedded = false;
    LoadSavedDisplaySettings();
    g_Selected = -1;
    g_Page = 0;
    g_PageT = 100;
    g_Capturing = false;
    g_SettingsAppliedTimer = 0;
    GameState::scene = GameState::Scene::Settings;
    GameAudio::Play(GameSound::MenuDecide);
}

namespace
{
void UpdatePage()
{
    if (g_Embedded)
        ++g_LocalFrame;
    else
        GameBackground::Update();
    ++g_PageT;
    if (g_SettingsAppliedTimer > 0) g_SettingsAppliedTimer--;
    if (g_DisplaySettle > 0 && --g_DisplaySettle == 0) ApplyPendingDisplaySettings();

    // ---- waiting for the key / the button of an action: nothing else works; ESC (START on the pad; the right button too, while a KEY is awaited) cancels
    if (g_Capturing)
    {
        ++g_CaptureT;
        if (GameInput::IsTriggered(VK_ESCAPE) || (g_CaptureKind == 0 && GameInput::IsTriggered(VK_RBUTTON)))
        {
            g_Capturing = false;
            GameAudio::Play(GameSound::MenuDecide);
            return;
        }
        const SettingsMenuItem item = ItemOfRow(g_Selected);
        bool bound = false;
        if (g_CaptureKind == 0)
        {
            const int key = GameInput::PollBindableKey();
            if (key != 0 && IsKeyItem(item))
            {
                GameSettings::BindKey(ActionOf(item), key);
                bound = true;
            }
        }
        else if (g_CaptureKind == 1)
        {
            const int button = GameInput::PollMouseButton();
            if (button != 0 && IsMouseButtonItem(item))
            {
                GameSettings::BindMouseButton(MouseActionOf(item), button);
                bound = true;
            }
        }
        else
        {
            const int button = GameInput::PollPadButton();
            if (button != 0 && IsPadButtonItem(item))
            {
                GameSettings::BindPadButton(PadActionOf(item), button);
                bound = true;
            }
        }
        if (bound)
        {
            GameSave::SaveSettings();
            Notify(g_CaptureKind == 0 ? 2 : 4, false);
            GameAudio::Play(GameSound::MenuApply);
            g_Capturing = false;
        }
        return;
    }

    if (GameInput::IsTriggered(VK_ESCAPE) || GameInput::IsTriggered(VK_BACK) || GameInput::IsTriggered(VK_RBUTTON))
    {
        LeavePage();
        return;
    }
    // the pages: TAB / Q / E, the shoulder buttons of the pad, a click on a tab
    const bool pad = GameInput::UsingGamepad();
    if (GameInput::IsTriggered('E') || GameInput::IsTriggered(VK_TAB) || (pad && GameInput::IsTriggered('V'))) SetPage(g_Page + 1);
    else if (GameInput::IsTriggered('Q') || (pad && GameInput::IsTriggered('C'))) SetPage(g_Page - 1);
    const int hoveredTab = HoveredTab();
    if (hoveredTab >= 0 && GameInput::IsTriggered(VK_LBUTTON))
    {
        SetPage(hoveredTab);
        return;
    }

    const int activated = GameMenuUi::UpdateMenu(g_Selected, RowCount(), hoveredTab >= 0 ? -1 : HoveredRow());
    // LEFT / RIGHT change the value of the highlighted row
    if (g_Selected >= 0)
    {
        const SettingsMenuItem item = ItemOfRow(g_Selected);
        if (GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A')) ChangeSetting(item, -1);
        if (GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D')) ChangeSetting(item, 1);
    }
    if (activated < 0) return;
    const SettingsMenuItem item = ItemOfRow(activated);
    if (item == SettingsMenuItem::Back)
    {
        LeavePage();
    }
    else if (IsButtonItem(item))
    {
        if (IsPadButtonItem(item) && !GameInput::GamepadConnected())
        {
            GameAudio::Play(GameSound::MenuError);
            Notify(7, true); // (nothing to press: plug in a pad first)
            return;
        }
        g_Capturing = true;
        g_CaptureT = 0;
        g_CaptureKind = IsKeyItem(item) ? 0 : (IsMouseButtonItem(item) ? 1 : 2);
        GameAudio::Play(GameSound::MenuDecide);
    }
    else if (IsResetItem(item))
    {
        if (item == SettingsMenuItem::ResetKeys)
        {
            GameSettings::ResetKeys();
            Notify(3, false);
        }
        else if (item == SettingsMenuItem::ResetMouse)
        {
            GameSettings::ResetMouse();
            Notify(5, false);
        }
        else
        {
            GameSettings::ResetPad();
            Notify(6, false);
        }
        GameSave::SaveSettings();
        GameAudio::Play(GameSound::MenuApply);
    }
    else
    {
        // value rows: ENTER steps forward, a click on the left half of the row steps back, on the right half forward
        int direction = 1;
        if (GameInput::IsTriggered(VK_LBUTTON) && GameInput::GetMouseX() < 0.25f) direction = -1;
        ChangeSetting(item, direction);
    }
}

void DrawPage()
{
    GameMenuUi::DrawPageHeader(Loc::T("SETTINGS"));
    DrawTabs();
    for (int row = 0; row < RowCount(); ++row) DrawRowOf(row);
    // under the page: what the highlighted row does, and one quiet line about the page
    const float fa = GameMenuUi::Entrance(12.0f, 12.0f);
    const SettingsMenuItem item = g_Selected >= 0 ? ItemOfRow(g_Selected) : SettingsMenuItem::Back;
    const char* hint = g_Selected >= 0 ? HintKey(item) : nullptr;
    if (hint == nullptr && g_Page == PAGE_KEYBOARD) hint = "HINT KEY";
    if (hint == nullptr && g_Page == PAGE_MOUSE) hint = "HINT MOUSE BUTTON";
    if (hint == nullptr && g_Page == PAGE_GAMEPAD) hint = "HINT PAD BUTTON";
    if (hint != nullptr)
        UiKit::Text(Loc::T(hint), 640.0f, 656.0f, 12.5f, UiKit::Rgba(0.70f, 0.76f, 0.88f, 0.90f * fa), TextAlignment::Center, 900.0f, false);
    const UiKit::Color quiet = UiKit::Rgba(0.52f, 0.58f, 0.72f, 0.80f * fa);
    if (g_Page == PAGE_KEYBOARD)
    {
        UiKit::Text(Loc::T("MENUS ALWAYS USE ARROWS ENTER ESC"), 640.0f, 684.0f, 11.5f, quiet, TextAlignment::Center, 900.0f, false);
    }
    else if (g_Page == PAGE_MOUSE)
    {
        UiKit::Text(Loc::T("MENUS ALWAYS USE LEFT RIGHT CLICK"), 640.0f, 684.0f, 11.5f, quiet, TextAlignment::Center, 900.0f, false);
    }
    else if (g_Page == PAGE_GAMEPAD)
    {
        const bool connected = GameInput::GamepadConnected();
        UiKit::Text(Loc::T(connected ? "GAMEPAD CONNECTED" : "NO GAMEPAD FOUND"), 640.0f, 684.0f, 11.5f,
                    connected ? UiKit::Rgba(0.55f, 1.00f, 0.72f, 0.85f * fa) : UiKit::Rgba(1.00f, 0.55f, 0.48f, 0.90f * fa), TextAlignment::Center, 900.0f, false);
        UiKit::Text(Loc::T("MENUS ALWAYS USE A B START"), 640.0f, 702.0f, 11.5f, quiet, TextAlignment::Center, 900.0f, false);
    }
    else
        UiKit::Text(Loc::T("CHANGES APPLY AT ONCE"), 640.0f, 684.0f, 11.5f, quiet, TextAlignment::Center, 900.0f, false);
    const char* notices[8] = {"SETTINGS APPLIED", "DISPLAY CHANGE FAILED", "KEY SET", "KEYS RESET", "BUTTON SET", "MOUSE BUTTONS RESET", "GAMEPAD RESET", "NO GAMEPAD FOUND"};
    GameMenuUi::Notice(Loc::T(notices[g_NoticeKind]), 1060.0f, 164.0f, g_SettingsAppliedTimer, NOTICE_FRAMES, g_NoticeRefusal);
}
}

void SettingsScene::Update()
{
    g_Embedded = false;
    UpdatePage();
}

void SettingsScene::Draw()
{
    Renderer2D_ClearCameraOffset();
    GameMenuUi::DrawBackdrop(0.55f);
    DrawPage();
}

void SettingsScene::OpenEmbedded()
{
    LoadSavedDisplaySettings();
    g_Embedded = true;
    g_EmbeddedClosed = false;
    g_Selected = -1;
    g_Page = 0;
    g_PageT = 0;
    g_Capturing = false;
    g_SettingsAppliedTimer = 0;
    GameMenuUi::StartEntrance();
}

bool SettingsScene::UpdateEmbedded()
{
    g_Embedded = true;
    UpdatePage();
    if (!g_EmbeddedClosed) return false;
    g_EmbeddedClosed = false;
    g_Embedded = false;
    return true;
}

void SettingsScene::DrawEmbedded()
{
    g_Embedded = true;
    Renderer2D_ClearCameraOffset();
    Renderer2D_DrawQuad(-1.00f, 1.00f, 1.00f, -1.00f, 0.00f, 0.005f, 0.025f, 0.66f); // (the pause menu: the game is dimmed, the seam is quiet)
    GameMenuUi::DrawBackdrop(0.34f);
    DrawPage();
}

void SettingsScene::CloseEmbedded()
{
    if (!g_Embedded) return;
    if (g_DisplaySettle > 0)
    {
        g_DisplaySettle = 0;
        ApplyPendingDisplaySettings();
    }
    g_Capturing = false;
    g_Embedded = false;
    g_EmbeddedClosed = false;
}




