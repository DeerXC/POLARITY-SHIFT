#include "game/scene/PlayScene.h"
#include "ui/CutIn.h"
#include "boss/GameBoss.h"
#include "core/DebugOptions.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/Loc.h"
#include "core/PostFx.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "game/Combat.h"
#include "game/GameBackground.h"
#include "game/GameEffect.h"
#include "game/GameEnemy.h"
#include "game/GamePlayer.h"
#include "game/GamePlayerBullet.h"
#include "game/GameSave.h"
#include "game/GameSpecialShot.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/GameWave.h"
#include "game/ScreenShake.h"
#include "core/GameTimer.h"
#include "game/scene/GameClearScene.h"
#include "game/scene/GameOverScene.h"
#include "rogue/Modules.h"
#include "rogue/Balance.h"
#include "ui/GameHud.h"
#include "ui/GameMenuUi.h"
#include "ui/TextTheme.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
int g_LastBossPhase = 1;
bool g_BossDefeatShakePlayed = false;
int g_BossPhaseChangeTimer = 0;

HudState MakeHudState()
{
    HudState state;
    state.score = GameState::score;
    state.hp = GameState::playerHp;
    state.maxHp = Mod::MaxHp();
    state.chain = Mod::ChainRate();
    state.energy = GameState::energy;
    state.maxEnergy = Mod::EnergyMax();
    state.combo = GameState::combo;
    state.multiplier = GameState::GetScoreMultiplier();
    state.comboRate = Mod::StreakProgress();
    state.polarity = GameState::player.polarity;
    state.playerX = GameState::player.x;
    state.playerY = GameState::player.y;
    state.wave = GameWave::GetCurrentWave();
    state.waveTotal = Balance::WAVE_COUNT + 1; // the waves + the boss marker
    state.bossActive = GameBoss::IsActive();
    return state;
}

void DrawScoreAndHp()
{
    if (GameState::scene != GameState::Scene::Game)
    {
        return;
    }

    GameHud::Draw(MakeHudState());
}

void DrawComboRankEffect()
{
    if (GameState::comboRankEffectTimer <= 0)
    {
        return;
    }

    const float lifeRate = static_cast<float>(GameState::comboRankEffectTimer) / static_cast<float>(GameState::COMBO_RANK_EFFECT_DURATION);

    float textSize = 0.030f;

    if (GameState::comboRankEffectTimer > GameState::COMBO_RANK_EFFECT_DURATION - 10)
    {
        textSize = 0.040f;
    }

    float red = 1.00f;
    float green = 0.75f;
    float blue = 0.15f;

    if (GameState::comboRankEffectValue >= 4)
    {
        red = 1.00f;
        green = 0.25f;
        blue = 0.15f;
    }
    else if (GameState::comboRankEffectValue >= 3)
    {
        red = 0.85f;
        green = 0.35f;
        blue = 1.00f;
    }

    Renderer2D_DrawQuad(-0.18f, 0.22f, 0.18f, 0.07f, 0.01f, 0.025f, 0.065f, lifeRate * 0.58f);

    Renderer2D_DrawQuad(-0.18f, 0.22f, 0.18f, 0.212f, red, green, blue, lifeRate);

    TextStyle rankStyle = TextTheme::GetStyle(TextRole::HudValue);

    rankStyle.size = textSize;

    rankStyle.red = red;

    rankStyle.green = green;

    rankStyle.blue = blue;

    rankStyle.alpha = lifeRate;

    rankStyle.alignment = TextAlignment::Center;

    rankStyle.layoutWidth = 0.30f;

    TextRenderer::QueueText(std::string("X") + std::to_string(GameState::comboRankEffectValue), -0.15f, 0.18f, rankStyle);
}

void DrawWaveUI()
{
    if (GameBoss::IsWarning())
    {
        if (CutIn::Active()) return; // (the WARNING card is up: the red bars come after it, they do not fight it)
        const bool warningPulse = (GameBackground::GetFrame() / 8) % 2 == 0;

        const float warningAlpha = warningPulse ? 1.00f : 0.62f;

        Renderer2D_DrawQuad(-1.00f, 0.72f, 1.00f, 0.695f, 0.80f, 0.04f, 0.03f, warningAlpha * 0.75f);

        Renderer2D_DrawQuad(-1.00f, 0.712f, 1.00f, 0.705f, 1.00f, 0.54f, 0.42f, warningAlpha * 0.55f);

        Renderer2D_DrawQuad(-1.00f, -0.695f, 1.00f, -0.72f, 0.80f, 0.04f, 0.03f, warningAlpha * 0.75f);

        Renderer2D_DrawQuad(-1.00f, -0.705f, 1.00f, -0.712f, 1.00f, 0.54f, 0.42f, warningAlpha * 0.55f);

        return;
    }

    if (GameWave::IsWaveStarting() || GameWave::IsWaveCleared())
    {
        return; // (the card: CutIn, triggered in UpdateCutIns)
    }

    static int s_Approach = 0;
    if (!(GameWave::IsAllFinished() && !GameBoss::IsActive())) s_Approach = 0;
    if (GameWave::IsAllFinished() && !GameBoss::IsActive())
    {
        // the notice of THE SEAM (it comes in once and stays until the warning)
        if (s_Approach < 400) ++s_Approach;
        GameMenuUi::Notice(Loc::T("BOSS APPROACHING"), 640.0f, 300.0f, 400 - s_Approach + 11, 400, true);
    }
}

void DrawCombatBackgroundShade()
{
    float shadeAlpha = 0.16f;

    if (GameBoss::IsWarning())
    {
        shadeAlpha = 0.22f;
    }
    else if (GameBoss::IsBattleStarted() && !GameBoss::IsDefeated())
    {
        shadeAlpha = 0.18f;
    }

    Renderer2D_ClearCameraOffset();

    Renderer2D_DrawQuad(-1.00f, 1.00f, 1.00f, -1.00f, 0.002f, 0.008f, 0.024f, shadeAlpha);
}
}

namespace
{
bool g_CutWarn = false, g_CutClear = false;

// the cards of the run (ui/CutIn): a wave starts, a wave is cleared, the boss is coming
void UpdateCutIns()
{
    const bool warn = GameBoss::IsWarning();
    if (warn && !g_CutWarn) CutIn::Trigger(CutIn::Kind::Warning, Loc::T("ARBITER"), Loc::T("HIGH ENERGY SIGNATURE DETECTED"), CutIn::AccentRed); // (the name of the boss; the card writes WARNING itself)
    g_CutWarn = warn;
    // (no card when a wave starts: it covered the field. The wave number is in the HUD at the top.)
    const bool clear = GameWave::IsWaveCleared() && !warn;
    if (clear && !g_CutClear)
    {
        std::string line = Loc::Fmt("CLEARED {0}%", Mod::LastClearPercent());
        if (Mod::LastClearHeal() > 0) line += "    " + Loc::Fmt("REPAIR +{0}", Mod::LastClearHeal());
        CutIn::Trigger(CutIn::Kind::Clear, Loc::T("WAVE CLEAR"), line, CutIn::AccentWhite);
    }
    g_CutClear = clear;
    CutIn::Hold(Mod::ChoiceOpen()); // (the module pick covers the game: the cards wait)
    CutIn::Update();
}
}

void PlayScene::Update()
{
    GameHud::Update(MakeHudState());
    Mod::UpdateUi(); // (before the cards: the clear rate of a wave is worked out here, and the WAVE CLEAR tag shows it)
    UpdateCutIns();
    if (GameState::hitStop > 0)
    {
        GameState::hitStop--;
        return;
    }
    // roguelite: the module pick screen slows the world down (about 0.4 s) until it stands still, and speeds it up again after the pick
    if (Mod::ChoiceOpen())
    {
        Mod::UpdateChoice();
    }
    if (!Mod::StepWorld())
    {
        GameBackground::Update();
        return;
    }
    // this update step moves the world: remember the positions it starts from, the drawing blends from there (see GameTimer::Blend)
    GameTimer::SetWorldAdvanced(true);
    GamePlayer::BeginStep(GameState::player);
    GamePlayerBullet::BeginStep();
    GameEnemy::BeginStep(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX);
    Mod::Tick();

    GameBackground::Update();

    ScreenShake::Update();

    GameEffect::Update();

    if (g_BossPhaseChangeTimer > 0)
    {
        g_BossPhaseChangeTimer--;
    }

    if (GameState::playerInvincibleTimer > 0)
    {
        GameState::playerInvincibleTimer--;
    }

    if (DebugOptions::GodMode())
    {
        GameState::playerInvincibleTimer = 2;
    }

    if (GameState::energyFlashTimer > 0)
    {
        GameState::energyFlashTimer--;
    }

    if (GameState::comboRankEffectTimer > 0)
    {
        GameState::comboRankEffectTimer--;
    }

    if (GameState::playerDeathTimer > 0)
    {
        GameState::playerDeathTimer--;

        if (GameState::playerDeathTimer <= 0)
        {
            GameState::playerDeathTimer = 0;

            GameAudio::Stop(GameSound::StageBgm);

            GameAudio::Stop(GameSound::BossBgm);
            GameAudio::Stop(GameSound::BossBerserkBgm);
            GameAudio::Stop(GameSound::GameOverBgm);
            GameAudio::Stop(GameSound::ClearBgm);

            if (!GameState::resultRecorded)
            {
                GameSave::RecordGameOver(GameState::score, GameState::maxCombo);

                GameState::resultRecorded = true;
            }

            GameOverScene::ResetSelection();
            Mod::CaptureDeath();

            GameBoss::FreezeForResult();
            GameAudio::PlayLoop(GameSound::GameOverBgm);

            GameState::scene = GameState::Scene::GameOver;
        }

        return;
    }

    if (!GameBoss::IsDefeated())
    {
        GamePlayer::SetHold(GameBoss::IsArriving()); // (the WARNING: a short cut scene, the ship waits at its place)
        GamePlayer::Update(GameState::player, GameState::shotCoolTime);

        Mod::AfterPlayerUpdate();

        GamePlayerBullet::Update();

        const bool specialShotActivated = !GamePlayer::IsHeld() && GameSpecialShot::TryActivate(GameState::player, GameState::energy, Mod::EnergyMax());

        if (specialShotActivated)
        {
            Mod::OnSpecialActivated();

            ScreenShake::Start(10, 0.009f);
        }
    }

    const bool shouldApplySpecialDamage = GameSpecialShot::Update(GameState::player);

    if (shouldApplySpecialDamage && !GameBoss::IsDefeated())
    {
        Combat::ApplySpecialShotDamage();
    }

    Mod::UpdateSpecial();

#ifdef _DEBUG
    if (GameInput::IsTriggered('1'))
    {
        GameState::score = 5000;
        GameState::maxCombo = 4;
    }

    if (GameInput::IsTriggered('2'))
    {
        GameState::score = 12000;
        GameState::maxCombo = 8;
    }

    if (GameInput::IsTriggered('3'))
    {
        GameState::score = 22000;
        GameState::maxCombo = 12;
    }

    if (GameInput::IsTriggered('4'))
    {
        GameState::score = 35000;
        GameState::maxCombo = 25;
    }

    if (GameInput::IsTriggered('B'))
    {
        GameWave::DebugFinishAllWaves();
    }

    if (GameInput::IsTriggered('E'))
    {
        GameState::energy = Mod::EnergyMax();
    }

    if (GameInput::IsTriggered('N'))
    {
        GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Sniper, Polarity::Blue, 0.0f, 998);
    }

    if (GameInput::IsTriggered('T'))
    {
        GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Turret, Polarity::Red, 0.0f, 990);
    }

    if (GameInput::IsTriggered('O'))
    {
        static const EnemyType TEMPLATES[4] = {EnemyType::Turret, EnemyType::Sniper, EnemyType::Heavy, EnemyType::Charger}; // (debug: an elite of a random template)
        GameEnemy::SpawnElite(GameState::enemies, GameState::ENEMY_MAX, TEMPLATES[std::rand() % 4], Polarity::Red, 0.0f, 992);
    }

    if (GameInput::IsTriggered('M'))
    {
        GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Rival, Polarity::Blue, 0.0f, 993);
    }

#endif

    if (!GameBoss::IsDefeated())
    {
        GameWave::Update(GameState::enemies, GameState::ENEMY_MAX);

        Mod::UpdateWaveEvents();

        if (GameWave::IsAllFinished() && !GameBoss::IsActive())
        {
            GameAudio::Stop(GameSound::StageBgm);

            GameAudio::Stop(GameSound::BossBgm);
            GameAudio::Stop(GameSound::BossBerserkBgm);
            GameAudio::Stop(GameSound::GameOverBgm);
            GameAudio::Stop(GameSound::ClearBgm);

            GameAudio::PlayLoop(GameSound::BossBgm);

            GameBoss::Start();
            Mod::LogEvent("boss_start", "");
        }

        GameEnemy::Update(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX, GameState::player);
    }

    GameBoss::Update(GameState::player);

    // ---- test switch (config.ini boss_phase=2|3): push the boss to that phase ----
    if (DebugOptions::BossPhase() > 1 && GameBoss::IsBattleStarted() && !GameBoss::IsTransitioning()
        && GameBoss::GetPhase() < DebugOptions::BossPhase())
    {
        GameBoss::TakeDamage(9999);
    }

    // ---- boss requests: hit stop, extra shakes, bullets turned into energy --------
    {
        const int hitStop = GameBoss::ConsumeHitStop();
        if (hitStop > GameState::hitStop)
        {
            GameState::hitStop = hitStop;
        }

        int shakeDuration = 0;
        float shakeStrength = 0.0f;
        while (GameBoss::ConsumeShake(shakeDuration, shakeStrength))
        {
            ScreenShake::Start(shakeDuration, shakeStrength);
        }

        GameState::energy += GameBoss::ConsumeEnergyReward();
        if (GameState::energy > Mod::EnergyMax())
        {
            GameState::energy = Mod::EnergyMax();
        }

        static bool s_BerserkMusic = false;
        const bool berserk = GameBoss::IsBerserk();
        if (berserk && !s_BerserkMusic)
        {
            GameAudio::Stop(GameSound::BossBgm);
            GameAudio::PlayLoop(GameSound::BossBerserkBgm);
        }
        s_BerserkMusic = berserk;
    }

    if (!GameBoss::IsDefeated())
    {
        Mod::UpdateZones();

        Combat::CheckBulletEnemyCollision();

        Bullet* playerBullets = GamePlayerBullet::GetBullets();

        const int playerBulletCount = GamePlayerBullet::GetBulletCount();

        GameBoss::CheckPlayerBullets(playerBullets, playerBulletCount); // (the score of the boss is paid while its bar goes down: Mod::UpdateBossScore)

        if (GameState::playerHp > 0)
        {
            const bool playerIsInvincible = GameState::playerInvincibleTimer > 0; // (the piercing beam does not protect the ship)

            const BossBulletHitResult bossHitResult = GameBoss::CheckPlayerCollision(GameState::player, playerIsInvincible);

            if (bossHitResult == BossBulletHitResult::Absorbed)
            {
                GameEffect::SpawnAbsorb(GameState::player.x, GameState::player.y, 0.095f, GameState::player.polarity);

                GameAudio::PlayAbsorb(GameState::player.polarity == Polarity::Red);

                Mod::OnAbsorb(GameState::ENERGY_PER_ABSORB_BOSS);
                Mod::FlushBossAbsorbBonus();
            }
            else if (bossHitResult == BossBulletHitResult::Damaged)
            {
                Combat::HurtPlayer(GameBoss::GetLastHitDamage(), GameBoss::GetLastHitSource());
            }
        }

        Combat::CheckPlayerEnemyCollision();
        Combat::CheckPlayerEnemyBulletCollision();
    }

    const int currentBossPhase = GameBoss::GetPhase();

    if (GameBoss::IsBattleStarted() && currentBossPhase > g_LastBossPhase)
    {
        ScreenShake::Start(18, 0.018f);

        g_BossPhaseChangeTimer = GameState::BOSS_PHASE_CHANGE_DURATION;

        g_LastBossPhase = currentBossPhase;
    }

    if (GameBoss::IsDefeated() && !g_BossDefeatShakePlayed)
    {
        ScreenShake::Start(28, 0.028f);

        g_BossDefeatShakePlayed = true;
    }

    if (GameBoss::IsDefeatAnimationFinished())
    {
        GameAudio::Stop(GameSound::BossBgm);
        GameAudio::Stop(GameSound::BossBerserkBgm);
        GameAudio::Stop(GameSound::GameOverBgm);
        GameAudio::Stop(GameSound::ClearBgm);

        if (!GameState::resultRecorded)
        {
            GameSave::RecordGameClear(GameState::score, GameState::maxCombo);

            GameState::resultRecorded = true;
        }

        GameClearScene::ResetSelection();
        Mod::LogEvent("boss_clear", "");

        GameAudio::PlayLoop(GameSound::ClearBgm);

        GameState::scene = GameState::Scene::GameClear;

        return;
    }
}

// The look of the picture (PostFx) for this moment of the run: every stage has its own light, the boss its own per phase, the overload
// darkens the screen a little while it charges, and the edges glow red when the ship is nearly dead (or BACKWATER is on).
// The style stays cold and grey: the tints are small.
namespace
{
PostFx::Look StageLook()
{
    PostFx::Look look = PostFx::DefaultLook();
    auto tint = [&](float r, float g, float b)
    {
        look.tint[0] = r;
        look.tint[1] = g;
        look.tint[2] = b;
    };
    if (GameBoss::IsActive())
    {
        const BossHudInfo info = GameBoss::GetHudInfo();
        if (info.phase >= 3)
        {
            tint(1.05f, 0.96f, 0.95f); // the core: hot
            look.contrast = 1.09f;
            look.bloom = 0.75f;
        }
        else if (info.phase == 2)
        {
            tint(1.02f, 0.99f, 0.98f);
            look.bloom = 0.70f;
        }
        else
        {
            tint(0.95f, 1.0f, 1.06f); // the dreadnought: steel blue
            look.bloom = 0.68f;
        }
        if (info.overload) // the charge of the overload: the world dims, its light stands out
        {
            look.exposure = 0.90f;
            look.bloom = 0.90f;
            look.vignette = 0.40f;
        }
    }
    else
    {
        const int wave = GameWave::GetCurrentWave();
        if (wave >= 5)
            tint(1.02f, 0.97f, 1.03f); // the rival twins: violet
        else if (wave >= 3)
            tint(0.98f, 0.99f, 1.05f);
        else
            tint(0.96f, 1.0f, 1.05f); // the first waves: cold cyan
    }
    // danger: under 35 % of the HP the edges glow red, more the lower it goes, with a slow pulse
    const int maxHp = Mod::MaxHp() > 0 ? Mod::MaxHp() : 1;
    const float hp = static_cast<float>(GameState::playerHp > 0 ? GameState::playerHp : 0) / static_cast<float>(maxHp);
    float danger = hp < 0.35f && GameState::playerHp > 0 ? (0.35f - hp) / 0.35f : 0.0f;
    if (Mod::BackwaterActive()) danger = (std::max)(danger, 0.30f);
    if (danger > 0.0f)
    {
        static int pulse = 0;
        ++pulse;
        look.danger = danger * (0.75f + 0.25f * std::sin(static_cast<float>(pulse) * 0.08f));
    }
    return look;
}
}

// World only (no HUD), used behind the pause and game over screens
void PlayScene::DrawWorld()
{
    PostFx::SetLook(StageLook());

    DrawCombatBackgroundShade();

    ScreenShake::Apply();

    GamePlayer::DrawUnder(GameState::player, GameState::playerHp, GameState::playerInvincibleTimer);

    GamePlayerBullet::Draw();

    GameSpecialShot::Draw(GameState::player);

    GameEnemy::Draw(GameState::enemies, GameState::ENEMY_MAX);

    GameBoss::Draw();

    GameEffect::Draw();

    Mod::DrawWorld();


    GameEnemy::DrawBullets(GameState::enemyBullets, GameState::ENEMY_BULLET_MAX);

    // the ship is drawn last: bullets pass behind it, so it never gets lost in a dense pattern
    GamePlayer::Draw(GameState::player, GameState::playerHp, GameState::playerDeathTimer, GameState::playerInvincibleTimer);

    Renderer2D_ClearCameraOffset();
}

void PlayScene::DrawObjects()
{
    DrawWorld();

    if (Mod::ChoiceOpen())
    {
        Mod::DrawChoice();
        return;
    }

    DrawScoreAndHp();
    DrawComboRankEffect();
    DrawWaveUI();
    CutIn::Draw();
    Mod::DrawChips();
}

void PlayScene::Reset()
{
    CutIn::Reset();
    g_CutWarn = g_CutClear = false;
    g_BossPhaseChangeTimer = 0;
    g_LastBossPhase = 1;
    g_BossDefeatShakePlayed = false;
}









