#include "game/Game.h"
#include "boss/GameBoss.h"
#include "core/GameAudio.h"
#include "core/DirectX.h"
#include "core/GameInput.h"
#include "core/GameTimer.h"
#include "core/Loc.h"
#include "core/TextRenderer.h"
#include "game/GameBackground.h"
#include "game/GameFlow.h"
#include "game/GamePlayer.h"
#include "game/GameSave.h"
#include "game/GameState.h"
#include "game/scene/GameClearScene.h"
#include "game/scene/GameOverScene.h"
#include "game/scene/HowToScene.h"
#include "game/scene/PauseMenu.h"
#include "game/scene/PlayScene.h"
#include "game/scene/RecordsScene.h"
#include "game/scene/SettingsScene.h"
#include "game/scene/TitleScene.h"
#include "ui/GameMenuUi.h"
#include "ui/SceneFx.h"
#include "ui/GameHud.h"
#include "ui/UiKit.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>

// Scene dispatcher: routes update and draw to the scene that is active.
namespace
{
// A "screen" is what SceneFx plays for: a scene, or the hangar on top of the title.
// (The pause menu and the module pick screen have their own entrance, they do not use the slices.)
struct Screen
{
    GameState::Scene scene;
    bool hangar;
};

Screen CurrentScreen()
{
    return Screen{GameState::scene, GameState::scene == GameState::Scene::Title && Mod::g_HangarOpen};
}

// how deep a screen is: going deeper pushes the old screen to the left, going back pushes it to the right
int Depth(const Screen& screen)
{
    switch (screen.scene)
    {
    case GameState::Scene::Title:
        return screen.hangar ? 1 : 0;
    case GameState::Scene::HowTo:
    case GameState::Scene::Records:
    case GameState::Scene::Settings:
        return 1;
    case GameState::Scene::Game:
        return 2;
    case GameState::Scene::GameClear:
    case GameState::Scene::GameOver:
        return 3;
    }
    return 0;
}

GameMenuUi::Stage StageFor(GameState::Scene scene)
{
    switch (scene)
    {
    case GameState::Scene::Title:
        return TitleScene::IsStarting() ? GameMenuUi::Stage::Play : GameMenuUi::Stage::Title; // START GAME: the backdrop turns into the game's
    case GameState::Scene::HowTo:
        return GameMenuUi::Stage::HowTo;
    case GameState::Scene::Records:
        return GameMenuUi::Stage::Records;
    case GameState::Scene::Settings:
        return GameMenuUi::Stage::Settings;
    case GameState::Scene::GameClear:
        return GameMenuUi::Stage::GameClear;
    case GameState::Scene::GameOver:
        return GameMenuUi::Stage::GameOver;
    case GameState::Scene::Game:
        break;
    }
    return GameMenuUi::Stage::Play; // in the game the backdrop is only a quiet field
}
}

namespace
{
UiKit::Color FpsColor(float fps)
{
    if (fps >= 58.0f) return UiKit::Rgba(0.55f, 1.00f, 0.72f, 1.0f);
    if (fps >= 45.0f) return UiKit::Rgba(1.00f, 0.80f, 0.30f, 1.0f);
    return UiKit::Rgba(1.00f, 0.42f, 0.36f, 1.0f);
}

// FPS overlay (Settings: SHOW FPS, or F3), bottom right:
//   FPS            presented frames per second
//   ms / worst     average frame time / the longest frame of the last half second (a stutter shows up as a large worst)
//   cpu            time the CPU needs to build one frame (update + draw calls); the rest of a frame is waiting for the display / GPU
//   upd            60 Hz update steps per presented frame (0.42 on a 144 Hz display is normal)
//   last line      size of the picture and the graphics chip that draws it
void DrawFpsOverlay()
{
    if (GameSave::GetRecordData().showFps == 0)
    {
        return;
    }

    const GameTimer::FrameStats stats = GameTimer::GetStats();

    char line1[32];
    char line2[96];
    char line3[192];
    std::snprintf(line1, sizeof line1, "%d FPS", static_cast<int>(stats.fps + 0.5f));
    std::snprintf(line2, sizeof line2, "%.1f ms   worst %.1f   cpu %.1f   upd %.2f", stats.avgMs, stats.maxMs, stats.cpuMs,
                  stats.updatesPerFrame);
    std::snprintf(line3, sizeof line3, "%dx%d%s   %s", DirectXSystem::GetBackBufferWidth(), DirectXSystem::GetBackBufferHeight(),
                  DirectXSystem::IsFlipModel() ? " FLIP" : "", DirectXSystem::GetAdapterName());

    const float width = (std::max)(UiKit::TextWidth(line2, 11.0f), UiKit::TextWidth(line3, 11.0f)) + 28.0f;
    const float right = 1266.0f;
    const bool slowFrame = stats.maxMs > 25.0f;

    UiKit::Fill(right - width, 670.0f, width, 48.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.55f)); // (low enough to stay clear of the hangar list)
    UiKit::Fill(right - width, 670.0f, 3.0f, 48.0f, FpsColor(stats.fps));
    UiKit::Text(line1, right - 12.0f, 686.0f, 17.0f, FpsColor(stats.fps), TextAlignment::Right, width, false);
    UiKit::Text(line2, right - 12.0f, 701.0f, 11.0f,
                slowFrame ? UiKit::Rgba(1.00f, 0.80f, 0.30f, 1.0f) : UiKit::Rgba(0.78f, 0.84f, 0.94f, 0.95f), TextAlignment::Right,
                width, false);
    UiKit::Text(line3, right - 12.0f, 712.0f, 11.0f, UiKit::Rgba(0.62f, 0.70f, 0.85f, 0.95f), TextAlignment::Right, width, false);
}
}

void Game_Initialize()
{
    Loc::Initialize();
    std::srand(static_cast<unsigned int>(GetTickCount64()));
    GameSave::Initialize();
    SettingsScene::LoadSavedDisplaySettings();
    const GameRecordData& recordData = GameSave::GetRecordData();
    GameAudio::SetBgmVolume(static_cast<float>(recordData.bgmVolumePercent) / 100.0f);
    GameAudio::SetSeVolume(static_cast<float>(recordData.seVolumePercent) / 100.0f);
    GameFlow::ResetGameData();
    GameState::scene = GameState::Scene::Title;
    TitleScene::Reset();
    GameAudio::PlayLoop(GameSound::TitleBgm);
}

void Game_OnExit()
{
    if (GameState::scene == GameState::Scene::Game)
    {
        Mod::LogEvent("quit_to_title", "");
    }
}

bool Game_ShouldHideMouseCursor()
{
    return GameState::scene == GameState::Scene::Game && !PauseMenu::IsOpen() && !Mod::ChoiceOpen();
}

void Game_Update()
{
    GameAudio::Update(); // fades of the music, ducking, the pause filter
    GameInput::SetPointerCaptured(false);
    GameTimer::SetWorldAdvanced(false); // PlayScene::Update sets it again when the world really takes this step
    GameTimer::SetBackgroundAdvanced(false);
    if (GameInput::IsTriggered(VK_F3))
    {
        GameSave::SetShowFps(GameSave::GetRecordData().showFps == 0);
    }

    const Screen before = CurrentScreen();
    switch (GameState::scene)
    {
    case GameState::Scene::Title:
        TitleScene::Update();
        break;
    case GameState::Scene::HowTo:
        HowToScene::Update();
        break;
    case GameState::Scene::Records:
        RecordsScene::Update();
        break;
    case GameState::Scene::Settings:
        SettingsScene::Update();
        break;
    case GameState::Scene::Game:
        if (PauseMenu::IsOpen())
        {
            PauseMenu::Update();
            break;
        }
        if (GameInput::IsTriggered(VK_ESCAPE))
        {
            PauseMenu::Open();
            GameAudio::Play(GameSound::MenuDecide);
            break;
        }
        if (!GameInput::IsWindowActive())
        {
            PauseMenu::Open(); // the player switched to another program: the game waits
            break;
        }
        // mouse only: the "II" button at the top right pauses (the ship does not follow the cursor while it is over the button)
        if (GameHud::PauseButtonHovered())
        {
            GameInput::SetPointerCaptured(true);
            if (GameInput::IsTriggered(VK_LBUTTON))
            {
                PauseMenu::Open();
                GameAudio::Play(GameSound::MenuDecide);
                break;
            }
        }
        PlayScene::Update();
        break;
    case GameState::Scene::GameClear:
        GameClearScene::Update();
        break;
    case GameState::Scene::GameOver:
        GameOverScene::Update();
        break;
    }
    // the pause menu and the module pick screen use the cursor: it must not steer the ship meanwhile (nor right after they close)
    GameInput::SetMouseSteeringBlocked(GameState::scene != GameState::Scene::Game || PauseMenu::IsOpen() || Mod::ChoiceOpen() || GamePlayer::IsIntro() || GamePlayer::IsHeld());

    GameAudio::SetPaused(GameState::scene == GameState::Scene::Game && PauseMenu::IsOpen()); // the music is lowered and muffled in the pause menu
    GameAudio::SetPickScreen(GameState::scene == GameState::Scene::Game && Mod::ChoiceOpen()); // the card / route screen: only the foundation of the music, muffled
    {
        // the fight, for the ears: the heartbeat and the muffled music of a ship that is nearly lost, the music that holds its breath while an overload charges
        GameAudio::FightState fight;
        fight.active = GameState::scene == GameState::Scene::Game && !PauseMenu::IsOpen() && !Mod::ChoiceOpen();
        const int maxHp = Mod::MaxHp();
        fight.hpRate = maxHp > 0 ? static_cast<float>(GameState::playerHp) / static_cast<float>(maxHp) : 1.0f;
        fight.alive = GameState::playerHp > 0;
        if (GameBoss::IsActive())
        {
            const BossHudInfo info = GameBoss::GetHudInfo();
            fight.bossFight = true;
            fight.overload = info.overload;
            fight.overloadRate = info.overloadRate;
        }
        GameAudio::SetFightState(fight);
        if (GameState::scene == GameState::Scene::Game) GameAudio::SetMusicPolarity(GameState::player.polarity == Polarity::Red); // the faces of the music follow the colour of the ship
    }

    const Screen after = CurrentScreen();
    if (after.scene != before.scene || after.hangar != before.hangar)
    {
        // title -> game has its own choreography (the title leaves, the ship flies in, the HUD slides in): no wipe, no fade of the texts
        const bool seamless = before.scene == GameState::Scene::Title && !before.hangar && after.scene == GameState::Scene::Game;
        if (!seamless)
        {
            SceneFx::Begin(Depth(after) >= Depth(before));
        }
    }
}

void Game_Draw()
{
    static bool s_Intro = false;
    if (!s_Intro)
    {
        s_Intro = true;
        SceneFx::Begin(true); // the title builds itself up when the game starts
    }
    TextRenderer::SetGlobalAlpha(SceneFx::TextAlpha());
    GameMenuUi::BeginFrame();
    GameMenuUi::SetStage(StageFor(GameState::scene));
    GameBackground::DrawCurrent();
    switch (GameState::scene)
    {
    case GameState::Scene::Title:
        TitleScene::Draw();
        break;
    case GameState::Scene::HowTo:
        HowToScene::Draw();
        break;
    case GameState::Scene::Records:
        RecordsScene::Draw();
        break;
    case GameState::Scene::Settings:
        SettingsScene::Draw();
        break;
    case GameState::Scene::Game:
        if (PauseMenu::IsOpen())
        {
            PlayScene::DrawWorld();
            GameMenuUi::SetStage(GameMenuUi::Stage::Pause);
            PauseMenu::Draw();
        }
        else
        {
            PlayScene::DrawObjects();
        }
        break;
    case GameState::Scene::GameClear:
        GameClearScene::Draw();
        break;
    case GameState::Scene::GameOver:
        PlayScene::DrawWorld();
        GameOverScene::Draw();
        break;
    }
    SceneFx::Draw(); // last: the screen change, wiping over everything that was just drawn

    TextRenderer::SetGlobalAlpha(1.0f); // the overlay is never faded
    DrawFpsOverlay();
}




