// Stand-ins for everything the game logic needs from the outside world: the renderer, the audio, the keyboard, the menu and the save file.
// The logic itself (enemies, waves, boss, modules, combat) is the REAL game code, compiled from ../../src.
#include "core/AssetManager.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/GameSettings.h"
#include "core/TextRenderer.h"
#include "core/GameTimer.h"
#include "core/Renderer2D.h"
#include "game/GameSave.h"
#include "sim_input.h"
#include "ui/GameMenuUi.h"

namespace SimInput
{
bool down[256]{};
bool prev[256]{};
}
namespace SimAudio
{
int count[MAX]{};
int total = 0;
void Note(GameSound s)
{
    const int i = static_cast<int>(s);
    if (i >= 0 && i < MAX) ++count[i];
    ++total;
}
}

#ifndef PREVIEW_RENDER
// ---- renderer: nothing is drawn
static Renderer2DBlendMode g_Blend = Renderer2DBlendMode::Alpha;
void Renderer2D_SetBlendMode(Renderer2DBlendMode m) { g_Blend = m; }
Renderer2DBlendMode Renderer2D_GetBlendMode() { return g_Blend; }
void Renderer2D_SetCameraOffset(float, float) {}
void Renderer2D_ClearCameraOffset() {}
void Renderer2D_DrawQuad(float, float, float, float, float, float, float, float) {}
void Renderer2D_DrawTexture(int, float, float, float, float, float, float, float, float, float, float, float, float) {}
void Renderer2D_DrawTextureEx(int, float, float, float, float, float, float, float, float, float, float, float, float, float) {}
void Renderer2D_DrawPolygon(const Renderer2DVertex*, int) {}
void Renderer2D_DrawPolygonTextured(int, const Renderer2DVertex*, int) {}
void Renderer2D_BindExternalTexture(ID3D11ShaderResourceView*) {}
void Renderer2D_Flush() {}
bool Renderer2D_DrawPolygonPattern(const Renderer2DVertex*, int, const Renderer2DPattern&) { return false; }
namespace AssetManager
{
int Get(Tex) { return 1; }
int EnemyGlow(int) { return 1; }
void BulletUv(int, float& u0, float& v0, float& u1, float& v1) { u0 = v0 = 0.0f; u1 = v1 = 1.0f; }
}

#endif

// ---- audio: silent
namespace GameAudio
{
bool Initialize() { return true; }
void Finalize() {}
void Update() {}
void Play(GameSound s) { SimAudio::Note(s); }
void PlayAt(GameSound s, float) { SimAudio::Note(s); }
void PlayEnemyHit(int, bool, float, bool) {}
void PlayPolaritySwitch(bool, float) {}
void PlayAbsorb(bool) {}
void PlayPlayerShot(bool) {}
void SetMusicPolarity(bool) {}
void PlayLoop(GameSound) {}
void Stop(GameSound) {}
void StopAll() {}
void SetPaused(bool) {}
void SetPickScreen(bool) {}
void SetFightState(const FightState&) {}
void SetBossPhase(int) {}
void OnWindowActivate(bool) {}
void SetBgmEnabled(bool) {}
void SetSeEnabled(bool) {}
bool IsBgmEnabled() { return true; }
bool IsSeEnabled() { return true; }
void SetBgmVolume(float) {}
void SetSeVolume(float) {}
float GetBgmVolume() { return 0.0f; }
float GetSeVolume() { return 0.0f; }
}

// ---- time: the drawing is never interpolated
namespace GameTimer
{
float Blend(float, float current) { return current; }
float RenderAlpha() { return 1.0f; }
}

// ---- input: the keys of the bot
namespace GameInput
{
namespace
{
// the keys of the actions of the ship: the defaults of the settings (GameSettings::DefaultKey)
int KeyOf(GameSettings::Action action)
{
    switch (action)
    {
    case GameSettings::Action::MoveUp: return 'W';
    case GameSettings::Action::MoveDown: return 'S';
    case GameSettings::Action::MoveLeft: return 'A';
    case GameSettings::Action::MoveRight: return 'D';
    case GameSettings::Action::Shoot: return VK_SPACE;
    case GameSettings::Action::Switch: return 'C';
    case GameSettings::Action::Special: return 'V';
    case GameSettings::Action::Tactic: return VK_SHIFT;
    default: return 0;
    }
}
}
bool ActionDown(GameSettings::Action action) { return SimInput::down[KeyOf(action)]; }
bool ActionTriggered(GameSettings::Action action) { return SimInput::down[KeyOf(action)] && !SimInput::prev[KeyOf(action)]; }
bool IsDown(int key) { return key >= 0 && key < 256 && SimInput::down[key]; }
bool IsTriggered(int key) { return key >= 0 && key < 256 && SimInput::down[key] && !SimInput::prev[key]; }
bool IsReleased(int key) { return key >= 0 && key < 256 && !SimInput::down[key] && SimInput::prev[key]; }
float GetMouseX() { return 0.0f; }
float GetMouseY() { return 0.0f; }
bool IsMouseInsideWindow() { return false; }
bool MouseMoved() { return false; }
bool IsWindowActive() { return true; }
Device LastDevice() { return Device::Keyboard; }
bool UsingGamepad() { return false; }
bool GamepadConnected() { return false; }
bool GetStickMove(float&, float&) { return false; }
bool MouseSteering() { return false; }
void SetMouseSteeringBlocked(bool) {}
void SetPointerCaptured(bool) {}
bool IsPointerCaptured() { return false; }
std::string PromptLabel(const std::string& k) { return k; }
void Rumble(float, int, bool) {}
}

// ---- menu and save
namespace GameMenuUi
{
int UpdateMenu(int&, int, int, bool, GameSound, bool) { return -1; }
void SetBackdropPolarity(bool, bool) {}
}
namespace GameSave
{
void AddRunRecord(const RunRecord&) {}
int g_AbandonedScore = -1; // (the score that the last abandoned run reported: tools/balance_sim/abandon_test.cpp)
int g_AbandonedCalls = 0;
void RecordAbandoned(int score, int) { g_AbandonedScore = score; ++g_AbandonedCalls; }
}


// ---- post effects: nothing
namespace PostFx
{
void Kick(float) {}
}

// ---- cut-ins: none
namespace CutIn
{
bool Docked() { return false; }
}

// ---- settings and text: the defaults (every setting at its default value) and no font
namespace GameSettings
{
const Values& Get()
{
    static const Values defaults{};
    return defaults;
}
}
namespace TextRenderer
{
void SetFontFamilyOverride(const wchar_t*) {}
}
