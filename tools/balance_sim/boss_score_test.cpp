// boss_score_test: the score of the boss and the progress numbers of the defeat screen (the real GameBoss / Mod / Balance).
//   1. a boss that is not beaten pays: the score follows the bar that is shot away, in steps, never goes down, and is 0 before the first hit
//   2. the progress at the end of phase 1 and 2 is the share of their life (GameBoss::GetProgress), and 1 once the boss is down
//   3. a clear pays exactly BOSS_SCORE_DAMAGE + BOSS_SCORE_CLEAR (the 5000 that the kill alone used to give: the ranks of the clear screen do not move)
//   4. boss practice scores like a run (Balance::PRACTICE_SCORES): the same 5000 for a boss that is shot down, and the score is not reset
//   5. the mission percentage (Balance::MissionProgress) only rises with the run, is 0 at the start and 1 at the end, and the waves are MISSION_WAVES_SHARE of it
//   boss_score_test returns 1 when a check fails
#include "boss/GameBoss.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "rogue/Balance.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include <cmath>
#include <cstdio>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

static Player MakePlayer()
{
    Player p{};
    p.x = -0.85f;
    p.y = 0.85f;
    p.halfWidth = 0.06f;
    p.halfHeight = 0.08f;
    p.speed = 0.018f;
    p.polarity = Polarity::Blue;
    return p;
}

static void BeginFight(Player& player, bool practice = false)
{
    GameState::ResetRun();
    GameBoss::Initialize();
    Mod::Reset();
    Mod::g_PracticePhase = practice ? 1 : 0; // (Reset takes it from the mode that is selected on the title screen)
    GameBoss::Start();
    for (int i = 0; i < 3000 && !(GameBoss::IsBattleStarted() && !GameBoss::IsTransitioning()); ++i) GameBoss::Update(player);
    Mod::Tick(); // (the run has met the boss)
}

int main()
{
    Player player = MakePlayer();

    std::printf("1. the score follows the bar\n");
    BeginFight(player);
    Check(GameBoss::GetProgress() == 0.0f && GameState::score == 0, "nothing shot away: progress 0, score 0");
    int last = 0;
    bool monotone = true, steps = true;
    float firstAt = -1.0f;
    for (int i = 0; i < 400 && GameBoss::GetPhase() == 1; ++i)
    {
        GameBoss::TakeDamage(1);
        Mod::Tick();
        if (GameState::score < last) monotone = false;
        if (GameState::score % Balance::BOSS_SCORE_STEP != 0) steps = false;
        if (firstAt < 0.0f && GameState::score > 0) firstAt = GameBoss::GetProgress();
        last = GameState::score;
        if (GameBoss::GetProgress() >= 0.10f) break;
    }
    std::printf("     progress %.3f  score %d\n", GameBoss::GetProgress(), GameState::score);
    Check(GameState::score > 0, "a boss at 10 % pays");
    Check(monotone, "the score never goes down");
    Check(steps, "the score moves in steps of BOSS_SCORE_STEP");
    const float expect = GameBoss::GetProgress() * Balance::BOSS_SCORE_DAMAGE;
    Check(std::fabs(static_cast<float>(GameState::score) - expect) <= Balance::BOSS_SCORE_STEP, "score = progress x BOSS_SCORE_DAMAGE (within one step)");

    std::printf("2. progress at the phase ends\n");
    GameBoss::TakeDamage(9999);
    for (int i = 0; i < 3000 && GameBoss::IsTransitioning(); ++i) GameBoss::Update(player);
    Mod::Tick();
    std::printf("     end of phase 1: progress %.3f  score %d\n", GameBoss::GetProgress(), GameState::score);
    Check(GameBoss::GetPhase() == 2 && std::fabs(GameBoss::GetProgress() - 210.0f / 660.0f) < 0.01f, "end of phase 1 = 210 / 660 of the bar");
    Check(std::abs(GameState::score - static_cast<int>(Balance::BOSS_SCORE_DAMAGE * 210.0f / 660.0f)) <= Balance::BOSS_SCORE_STEP, "... and its share of the score");
    GameBoss::TakeDamage(9999);
    for (int i = 0; i < 3000 && GameBoss::IsTransitioning(); ++i) GameBoss::Update(player);
    Mod::Tick();
    std::printf("     end of phase 2: progress %.3f  score %d\n", GameBoss::GetProgress(), GameState::score);
    Check(GameBoss::GetPhase() == 3 && std::fabs(GameBoss::GetProgress() - 460.0f / 660.0f) < 0.01f, "end of phase 2 = 460 / 660 of the bar");

    std::printf("3. a clear\n");
    for (int i = 0; i < 40 && !GameBoss::IsDefeated(); ++i)
    {
        GameBoss::TakeDamage(9999);
        Mod::Tick();
    }
    Mod::Tick();
    Mod::Tick();
    Check(GameBoss::IsDefeated() && GameBoss::GetProgress() == 1.0f, "the boss is down: progress 1");
    std::printf("     score %d\n", GameState::score);
    Check(GameState::score == Balance::BOSS_SCORE_DAMAGE + Balance::BOSS_SCORE_CLEAR, "the whole fight pays 4000 + 1000 = 5000 (and nothing twice)");

    std::printf("4. boss practice\n");
    BeginFight(player, true);
    GameBoss::TakeDamage(9999);
    for (int i = 0; i < 3000 && GameBoss::IsTransitioning(); ++i) GameBoss::Update(player);
    Mod::Tick();
    for (int round = 0; round < 12 && !GameBoss::IsDefeated(); ++round)
    {
        GameBoss::TakeDamage(9999);
        for (int i = 0; i < 3000 && GameBoss::IsTransitioning(); ++i) GameBoss::Update(player);
        Mod::Tick();
    }
    Mod::Tick();
    Mod::Tick();
    std::printf("     practice score %d\n", GameState::score);
    Check(GameState::score == Balance::BOSS_SCORE_DAMAGE + Balance::BOSS_SCORE_CLEAR, "practice: the boss pays 4000 + 1000 like in a run");

    std::printf("5. the mission percentage\n");
    Check(Balance::MissionProgress(1, 0, false, 0.0f) == 0.0f, "the start of wave 1 is 0");
    Check(std::fabs(Balance::MissionProgress(5, 3, true, 0.0f) - Balance::MISSION_WAVES_SHARE) < 1e-5f, "the boss reached, nothing shot: exactly the share of the waves");
    Check(std::fabs(Balance::MissionProgress(5, 3, true, 1.0f) - 1.0f) < 1e-5f, "the boss down: 1");
    bool rises = true;
    float prev = -1.0f;
    for (int w = 1; w <= Balance::WAVE_COUNT; ++w)
        for (int s = 0; s <= 3; ++s)
        {
            const float v = Balance::MissionProgress(w, s, false, 0.0f);
            if (v + 1e-6f < prev) rises = false;
            prev = v;
        }
    for (float b = 0.0f; b <= 1.0f; b += 0.05f)
    {
        const float v = Balance::MissionProgress(5, 3, true, b);
        if (v + 1e-6f < prev) rises = false;
        prev = v;
    }
    Check(rises, "it only rises: wave by wave, segment by segment, then the boss bar");
    Check(std::fabs(Balance::MissionProgress(2, 0, false, 0.0f) - Balance::MISSION_WAVES_SHARE / 5.0f) < 1e-5f, "a wave is a fifth of the waves' share");

    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}

