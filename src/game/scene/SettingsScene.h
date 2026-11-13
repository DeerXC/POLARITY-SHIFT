#pragma once
// The settings page: six pages (tabs) - DISPLAY (window, resolution, VSYNC, frame cap, FPS overlay), GAME (language, background, shake, flashes), AUDIO (music,
// effects), KEYBOARD (a key for every action of the ship), MOUSE (steering, a button for the four button actions), GAMEPAD (a button for the four button actions,
// vibration, the dead zone of the stick). The pause menu shows the SAME page over the game (OpenEmbedded / UpdateEmbedded / DrawEmbedded), so a setting is where the player
// expects it, in the title and in a fight.
namespace SettingsScene
{
enum class Item
{
    DisplayMode,
    Resolution,
    VSync,
    FpsCap,
    ShowFps,
    Language,
    Background, // the flat field behind the game: how bright (0..100 %)
    ScreenShake,
    Flashes,
    MouseSteering, // (on the MOUSE page)
    BgmVolume,
    SeVolume,
    MasterVolume,     // (the audio page)
    CueEmphasis,      // how much the warnings are lifted and how hard they push the rest down
    Heartbeat,        // the heartbeat and the muffled music of a ship that is nearly lost
    MuteInBackground, // the sound stops while the window is not in front
    KeyMoveUp,
    KeyMoveDown,
    KeyMoveLeft,
    KeyMoveRight,
    KeyShoot,
    KeySwitch,
    KeySpecial,
    KeyTactic,
    ResetKeys,
    MouseShoot,
    MouseSwitch,
    MouseSpecial,
    MouseTactic,
    ResetMouse,
    PadShoot,
    PadSwitch,
    PadSpecial,
    PadTactic,
    PadExtraButtons,
    Vibration,
    DeadZone,
    ResetPad,
    Back
};
// Opens the page from the title screen.
void Open();
void Update();
void Draw();
// The same page over the game (the pause menu): the scene stays "game", ESC / BACK close it and UpdateEmbedded() says so (true = go back to the pause menu).
void OpenEmbedded();
bool UpdateEmbedded();
void DrawEmbedded();
void CloseEmbedded(); // the pause menu is left some other way (the window was switched): a display change that waits is applied, nothing stays open
// Display mode and resolution that are shown on the page, read from the save file.
void LoadSavedDisplaySettings();
// Volume rows (BgmVolume / SeVolume), direction -1 / +1.
void AdjustVolume(Item item, int direction);
}



