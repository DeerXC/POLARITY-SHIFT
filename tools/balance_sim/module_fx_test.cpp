// module_fx_test: the state behind three module effects and the sounds around them (the real Mod code; the pictures are ModulesDraw, tools/balance_sim/preview_fx.cpp).
//   1. MEND shows itself: when the repair BEGINS the ship gets a ring (g_MendStartTimer, once, on the rising edge), every repaired point is flashed (g_MendTickFlash),
//      and after a new hit the next repair opens its ring again
//   2. PULSE has no sound of its own (the shockwave is seen; its energy is heard when the bar is full)
//   3. the BACKUP CORE: the blast runs for REVIVE_FRAMES on the clock of the screen (UpdateUi), once per run
//   module_fx_test returns 1 when a check fails
#include "game/GameState.h"
#include "rogue/Balance.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include "sim_input.h"
#include <cstdio>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

static void NewRun()
{
    GameState::ResetRun();
    Mod::Reset();
    Mod::g_PracticePhase = 0;
    GameState::player.x = -0.6f;
    GameState::player.y = 0.0f;
    GameState::player.halfWidth = 0.06f;
    GameState::player.halfHeight = 0.08f;
}

int main()
{
    std::printf("1. MEND is seen on the ship\n");
    NewRun();
    Mod::Grant(Mod::Id::Mend);
    GameState::playerHp = Mod::MaxHp() - 30;
    Mod::g_SinceHurt = 0;
    int starts = 0, tickFlashes = 0;
    int firstStartAt = -1;
    int prevHp = GameState::playerHp;
    bool waited = true;
    for (int i = 1; i <= 4500; ++i)
    {
        const int before = Mod::g_MendStartTimer;
        Mod::Tick();
        if (Mod::g_MendStartTimer > before)
        {
            ++starts;
            if (firstStartAt < 0) firstStartAt = i;
        }
        if (GameState::playerHp > prevHp)
        {
            if (Mod::g_MendTickFlash > 0) ++tickFlashes;
            else waited = false;
        }
        prevHp = GameState::playerHp;
        if (i < Balance::MEND_DELAY && Mod::MendActive()) waited = false; // (it must not show a repair that has not begun)
    }
    std::printf("     the ring opened %d time(s), first at step %d; %d repaired points flashed\n", starts, firstStartAt, tickFlashes);
    Check(starts == 1, "the ring opens once, when the repair begins (not every step of it)");
    Check(firstStartAt >= Balance::MEND_DELAY && firstStartAt <= Balance::MEND_DELAY + 2, "... exactly when the delay is over (MEND_DELAY)");
    Check(tickFlashes == 30 && waited, "every one of the 30 repaired points flashes the ship, and nothing shows before the repair begins");
    Check(GameState::playerHp == Mod::MaxHp() && !Mod::MendActive(), "the ship is whole at the end and the aura is gone with the repair");

    std::printf("   hurt again: a new repair opens the ring again\n");
    GameState::playerHp = Mod::MaxHp() - 10;
    Mod::g_SinceHurt = 0;
    int starts2 = 0;
    for (int i = 0; i < 600; ++i)
    {
        const int before = Mod::g_MendStartTimer;
        Mod::Tick();
        if (Mod::g_MendStartTimer > before) ++starts2;
    }
    Check(starts2 == 1, "the second repair has its own ring");
    Check(Mod::MendProgress() >= 0.0f && Mod::MendProgress() <= 1.0f, "the progress to the next point is a share (0 .. 1)");

    std::printf("2. PULSE has no sound of its own\n");
    NewRun();
    Mod::Grant(Mod::Id::Pulse);
    SimAudio::Reset();
    Mod::FirePulse();
    Check(Mod::g_PulseTimer > 0, "the shockwave is on (it is drawn)");
    Check(SimAudio::total == 0, "... and nothing was played for it");

    std::printf("3. the BACKUP CORE\n");
    NewRun();
    Mod::Grant(Mod::Id::BackupCore);
    GameState::playerHp = 0;
    SimAudio::Reset();
    const bool revived = Mod::TryBackupCore();
    Check(revived && GameState::playerHp > 0, "the ship is back");
    Check(Mod::g_ReviveTimer == Mod::REVIVE_FRAMES, "the blast starts");
    Check(SimAudio::Count(GameSound::BackupRevive) == 1, "its sound plays once");
    int steps = 0;
    while (Mod::g_ReviveTimer > 0 && steps < 1000)
    {
        Mod::UpdateUi();
        ++steps;
    }
    Check(steps == Mod::REVIVE_FRAMES, "it lasts REVIVE_FRAMES steps of the screen clock");
    GameState::playerHp = 0;
    Check(!Mod::TryBackupCore() && Mod::g_ReviveTimer == 0, "one revive per run: a second death does not blast again");

    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
