#include "game/GameFlow.h"
#include "boss/GameBoss.h"
#include "core/DebugOptions.h"
#include "core/GameAudio.h"
#include "core/Renderer2D.h"
#include "game/GameBackground.h"
#include "game/GameEffect.h"
#include "game/GameEnemy.h"
#include "game/GamePlayer.h"
#include "game/GamePlayerBullet.h"
#include "game/GameSave.h"
#include "game/GameSpecialShot.h"
#include "game/GameState.h"
#include "game/GameWave.h"
#include "game/ScreenShake.h"
#include "game/scene/PauseMenu.h"
#include "game/scene/PlayScene.h"
#include "game/scene/TitleScene.h"
#include "rogue/Modules.h"
#include "ui/GameHud.h"

namespace
{
// Test switch from config.ini (start_at_boss=1): jump straight into the boss fight.
void ApplyDebugStart()
{
    if (!DebugOptions::StartAtBoss())
    {
        return;
    }

    GameAudio::Stop(GameSound::StageBgm);

    GameWave::PrepareBossRetry();
    GameBoss::Initialize();
    GameBoss::Start();

    GameAudio::PlayLoop(GameSound::BossBgm);
}

Polarity g_StartPolarity = Polarity::Blue; // read by ResetGameData while StartNewGame runs

void StopAllMusic()
{
    GameAudio::Stop(GameSound::TitleBgm);
    GameAudio::Stop(GameSound::StageBgm);
    GameAudio::Stop(GameSound::BossBgm);
    GameAudio::Stop(GameSound::BossBerserkBgm);
    GameAudio::Stop(GameSound::GameOverBgm);
    GameAudio::Stop(GameSound::ClearBgm);
}
}

void GameFlow::ResetGameData()
{
    GameHud::Reset();

    GamePlayer::Initialize(GameState::player);
    GameState::player.polarity = g_StartPolarity;
    GamePlayerBullet::Initialize();
    GameSpecialShot::Initialize();
    GameEffect::Initialize();

    GameEnemy::Initialize(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX);

    GameWave::Initialize();
    GameBoss::Initialize();

    GameState::ResetRun();

    PlayScene::Reset();
    GameBackground::Reset();
    ScreenShake::Reset();
    PauseMenu::Reset();

    Mod::Reset();

    Renderer2D_ClearCameraOffset();
}

void GameFlow::StartNewGame(Polarity startPolarity, bool fromTitle)
{
    g_StartPolarity = startPolarity;
    if (GameState::scene == GameState::Scene::Game)
    {
        Mod::LogEvent("restart", "");
    }

    PauseMenu::Reset();

    Renderer2D_ClearCameraOffset();

    StopAllMusic();
    GameAudio::PlayLoop(GameSound::StageBgm);

    GameSave::StartNewPlay();
    ResetGameData();
    g_StartPolarity = Polarity::Blue;
    ApplyDebugStart();
    Mod::ApplyDebug();
    // the ship flies in from the left edge and the HUD slides in around it
    GamePlayer::BeginIntro(GameState::player, fromTitle);
    GameHud::BeginIntro();
    GameState::scene = GameState::Scene::Game;
}

void GameFlow::ReturnToTitle()
{
    if (GameState::scene == GameState::Scene::Game)
    {
        Mod::LogEvent("quit_to_title", "");
    }

    PauseMenu::Reset();
    ScreenShake::Reset();

    Renderer2D_ClearCameraOffset();

    StopAllMusic();

    GameState::scene = GameState::Scene::Title;
    TitleScene::Reset();

    if (GameAudio::GetBgmVolume() > 0.0f)
    {
        GameAudio::PlayLoop(GameSound::TitleBgm);
    }
}

