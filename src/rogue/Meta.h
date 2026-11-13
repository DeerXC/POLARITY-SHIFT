#pragma once

#include "rogue/Balance.h"
#include "rogue/ModuleDefs.h"

#include <string>
#include <vector>

// Progression between runs: data shards, hangar unlocks, synergies, challenge levels and boss practice.
// Works on plain arguments only (no game state). Saved in meta.sav next to the exe: one key=value per line,
// modules are stored by their code.
namespace Meta
{
// ---------------------------------------------------------------- synergies (hidden pairs)
constexpr int COMBO_COUNT = 7; // the hangar list has room for seven (ModulesDrawMenus.cpp: DrawHangarSynergies)

enum ComboId
{
    ComboGravity = 0,    // SIPHON + PULSE: the shockwave reaches twice as far and gives up to half as much energy more
    ComboStorm = 1,      // ECHO + EMBER: the fire calls lightning (an arc every half second); an arc hits an enemy in the fire once more
    ComboPoised = 2,     // OVERFLOW + TWIN: the stored bullets make the half beam stronger too (it spends only half of them)
    ComboCombo = 3,      // MOMENTUM + AFTERGLOW: the end of the beam fills the stacks, and they do not fade while the afterglow lasts
    ComboLock = 4,       // FOCUS + BREACH: at full stacks +25 % damage; a new target keeps half of the stacks
    ComboUnyielding = 5, // BACKWATER + MEND: under half of the HP MEND repairs 50 % faster, without the wait
    ComboRevenge = 6     // BACKUP CORE + DEFLECT: the revive keeps half a bar, and its blast turns the white bullets into energy too
};

struct ComboInfo
{
    Mod::Id a;
    Mod::Id b;
    const char* name; // Loc key
    const char* desc; // Loc key
    const char* code; // save file / run log
};

extern const ComboInfo COMBOS[COMBO_COUNT];

// ---------------------------------------------------------------- shard rewards (starting values, tune with run_log.csv)
constexpr int SHARDS_PER_WAVE = 8; // per cleared wave (4 waves)
constexpr int SHARDS_BOSS_REACHED = 12;
constexpr int SHARDS_PER_BOSS_PHASE = 8; // for phase 2 and phase 3 reached
constexpr int SHARDS_BOSS_CLEARED = 50;
constexpr int SHARDS_ABSORB_CAP = 20; // 1 shard per 8 absorbs
constexpr int SHARDS_FLOOR = 6;       // a run that lasted at least 10 seconds always gives this much
constexpr int FLOOR_MIN_FRAMES = 600;
constexpr int ABANDON_MIN_FRAMES = 1200; // quitting / restarting earlier than 20 seconds gives nothing

// ---------------------------------------------------------------- challenge levels / boss practice
constexpr int CHALLENGE_MAX = 3;            // levels 0 (standard) .. 3
constexpr int PRACTICE_PHASES = 1;          // boss practice: one entry, the whole boss fight from phase 1
constexpr int CHALLENGE_BONUS_PERCENT = 25; // extra shards per level

struct RunStats
{
    int wavesCleared = 0;
    bool reachedBoss = false;
    int bossPhase = 1;
    bool bossCleared = false;
    int absorbs = 0;
    int frames = 0;
    bool abandoned = false; // restart / quit to title instead of dying or winning
    int level = 0;          // challenge level of the run
};

struct RunResult
{
    int waves = 0;
    int boss = 0;
    int phases = 0;
    int clear = 0;
    int absorbs = 0;
    int bonus = 0; // challenge bonus (already included in total)
    int level = 0;
    int unlockedLevel = -1; // >= 0: this run unlocked that challenge level
    int total = 0;          // shards given for this run
    int balance = 0;        // shard balance after the run
    bool valid = false;     // false: nothing was awarded
};

std::vector<int> ModulesFromCsv(const std::string& csv);
void Ensure();
int Shards();
bool IsUnlocked(int id);          // a hangar id (ModulesInternal.h HANGAR_LAYOUT): a module (always), a tactic that was bought, a talent at level 1 or more
int Price(int id);                // what ENTER on a hangar id costs now (0: nothing to buy)
bool CanAfford(int id);
bool TryUnlock(int id);           // buys the tactic / the next level of the talent
bool AnyAffordable();
// ---- tactics: bought once, one of them is taken into a run
bool OwnsTactic(Mod::Tactic tactic);
int EquippedTactic();             // -1 = none
void EquipTactic(int tactic);     // -1 = none (the same one again takes it off)
// ---- talents: three levels each
int TalentLevel(Mod::Talent talent);
// ---- the break of an overload: the HUD explains it until the player has done it three times
int OverloadBreaks();
void NoteOverloadBreak();
// ---- how far the best run came (percent): the defeat screen shows it next to the run that has just ended
int BestMission();  // the whole mission (waves + boss), normal runs
int BestBoss();     // how much of the boss bar was shot away, normal runs and practice
void NoteProgress(int missionPercent, int bossPercent, bool practice); // saved (a practice run only counts for the boss)
int NextTarget(int& missing);
int UnlockedCount(); // the tactics that were bought and the levels of the talents
int UnlockTotal();   // ... out of this many
int ChallengeMax();
bool BossSeen();
int ModeCount();
bool ModeIsPractice(int mode);
int ModeLevel(int mode);
int ModePhase(int mode);
int Mode();
void CycleMode(int direction);
void MarkBossSeen();
void SetLastModules(const std::string& csv);
RunResult Compute(const RunStats& stats);
RunResult AwardRun(const RunStats& stats);
bool ComboOwned(const bool owned[], int combo);
bool ComboFound(int combo);
int DiscoverNew(const bool owned[], int found[], int capacity);
int ComboCompletedBy(const bool owned[], int candidate);
int VerbCount(const bool owned[], Mod::Verb verb);
std::string LastModules(); // module codes of the last finished run, comma separated
}


