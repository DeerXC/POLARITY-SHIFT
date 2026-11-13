// save_test: the real GameSave (core/GameSettings behind it) against files in the working directory.
//   1. a history with a left run of 24000 and a save with HIGH SCORE 0 (what the records page showed): Initialize() lifts the best score and the best combo, and saves
//   2. RecordAbandoned keeps the bests (a lower run changes nothing, a higher one does), and does not count a clear
//   3. a practice run in the history counts like any other (Balance::PRACTICE_SCORES)
//   4. a save that is already right is left alone
//   save_test returns 1 when a check fails
#include "game/GameSave.h"
#include "core/GameSettings.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

// the settings behind the save are not what is tested here
namespace GameSettings
{
static Values g_Values;
Values& Edit() { return g_Values; }
const Values& Get() { return g_Values; }
bool IsBindable(int) { return true; }
bool IsMouseBindable(int) { return true; }
void ResetKeys() {}
void ResetMouse() {}
void ResetPad() {}
}

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

static void WriteHistory(const char* rows)
{
    std::ofstream f("history.dat", std::ios::trunc);
    f << "1\n" << rows;
}

int main()
{
    std::remove("save.dat");
    std::remove("history.dat");

    std::printf("1. a left run in the history, a save without it\n");
    // time result wave bossPhase mode score maxCombo seconds absorbs hits shards modules
    WriteHistory("1700000000\t2\t3\t0\t0\t24000\t11\t95\t40\t3\t0\t\n");
    GameSave::Initialize();
    Check(GameSave::GetRecordData().highScore == 24000, "HIGH SCORE is 24000");
    Check(GameSave::GetRecordData().maxCombo == 11, "BEST COMBO is 11");
    GameSave::Initialize(); // (the next start reads the saved file)
    Check(GameSave::GetRecordData().highScore == 24000, "... and it was saved");

    std::printf("2. RecordAbandoned\n");
    const int clears = GameSave::GetRecordData().clearCount;
    GameSave::RecordAbandoned(5000, 3);
    Check(GameSave::GetRecordData().highScore == 24000 && GameSave::GetRecordData().maxCombo == 11, "a lower run changes nothing");
    GameSave::RecordAbandoned(30000, 14);
    Check(GameSave::GetRecordData().highScore == 30000 && GameSave::GetRecordData().maxCombo == 14, "a higher run is the new best");
    Check(GameSave::GetRecordData().clearCount == clears, "the number of clears is not touched");
    GameSave::RecordAbandoned(0, 0);
    Check(GameSave::GetRecordData().highScore == 30000, "an empty run does nothing");

    std::printf("3. practice\n");
    std::remove("save.dat");
    WriteHistory("1700000000\t2\t5\t2\t9\t99999\t30\t95\t40\t3\t0\t\n1700000100\t0\t2\t0\t0\t1200\t4\t60\t10\t3\t5\t\n");
    GameSave::Initialize();
    Check(GameSave::GetRecordData().highScore == 99999 && GameSave::GetRecordData().maxCombo == 30, "the practice run (mode 9) counts like the normal run: the best of both");

    std::printf("4. a save that is right\n");
    GameSave::Initialize();
    Check(GameSave::GetRecordData().highScore == 99999, "unchanged");

    std::remove("save.dat");
    std::remove("history.dat");
    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}

