// abandon_test: a run that is left keeps its score (the real Mod::EndRun path, GameSave stubbed: stubs.cpp records what it was told).
//   1. RESTART / TITLE after a minute of play with a score: GameSave::RecordAbandoned gets that score (it used to be written to the history only)
//   2. the same run left twice (restart, then the window closes) reports once
//   3. a run that was left within 20 seconds and had no score reports nothing worth a line, and nothing is awarded
//   4. a dead run is not reported as abandoned (PlayScene records a defeat itself)
//   5. boss practice reports its score like a run (Balance::PRACTICE_SCORES)
//   6. a save that has never met the boss (a fresh one, a deleted build folder) still has boss practice on the title screen (run this in an empty folder)
//   abandon_test returns 1 when a check fails
#include "game/GameState.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include <cstdio>

namespace GameSave
{
extern int g_AbandonedScore;
extern int g_AbandonedCalls;
}

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

static void BeginRun(bool practice, int frames, int score)
{
    GameState::ResetRun();
    Mod::Reset();
    Mod::g_PracticePhase = practice ? 1 : 0;
    for (int i = 0; i < frames; ++i) Mod::Tick();
    GameState::score = score;
    GameState::maxCombo = 7;
    GameSave::g_AbandonedScore = -1;
    GameSave::g_AbandonedCalls = 0;
}

int main()
{
    std::printf("1. left after a minute\n");
    BeginRun(false, 3600, 24000);
    Mod::LogEvent("quit_to_title", "");
    Check(GameSave::g_AbandonedCalls == 1 && GameSave::g_AbandonedScore == 24000, "the score of the run (24000) is reported");

    std::printf("2. left twice\n");
    Mod::LogEvent("restart", "");
    Mod::LogEvent("quit_to_title", "");
    Check(GameSave::g_AbandonedCalls == 1, "it is reported once (a run ends once)");

    std::printf("3. left within 20 seconds\n");
    BeginRun(false, 300, 0);
    Mod::LogEvent("restart", "");
    Check(GameSave::g_AbandonedScore <= 0, "a score of 0 is not worth anything (the stub is told 0 and does not keep it)");

    std::printf("4. a defeat\n");
    BeginRun(false, 3600, 9000);
    Mod::CaptureDeath();
    Mod::LogEvent("quit_to_title", "");
    Check(GameSave::g_AbandonedCalls == 0, "dead runs are recorded by the defeat path, not as abandoned");

    std::printf("5. boss practice\n");
    BeginRun(true, 3600, 3200);
    Mod::LogEvent("quit_to_title", "");
    Check(GameSave::g_AbandonedCalls == 1 && GameSave::g_AbandonedScore == 3200, "practice reports its score (3200)");

    std::printf("6. boss practice on a new save\n");
    Check(Meta::ModeCount() >= 2 && Meta::ModeIsPractice(Meta::ModeCount() - 1), "the last mode of a new save is the boss practice");

    std::printf("7. the harder mode is off\n");
    Check(Meta::ChallengeMax() == 0 && Meta::ModeCount() == 2, "the modes are STANDARD and BOSS PRACTICE: nothing else");
    Check(Meta::ModeLevel(0) == 0 && Meta::ModeLevel(1) == 0 && Meta::ModePhase(1) == 1 && Meta::ModePhase(0) == 0, "mode 0 is the standard run, mode 1 the practice");

    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}

