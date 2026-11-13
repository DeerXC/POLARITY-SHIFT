#include "rogue/Meta.h"
#include "rogue/Balance.h"
#include "core/DebugOptions.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace Meta
{
const ComboInfo COMBOS[COMBO_COUNT] = {
    {Mod::Id::Siphon, Mod::Id::Pulse, "GRAVITY WELL", "PULSE REACHES TWICE AS FAR AND GIVES UP TO 50% MORE ENERGY", "gravity_well"},
    {Mod::Id::Echo, Mod::Id::Ember, "THUNDERSTORM", "THE FIRE CALLS LIGHTNING: AN ARC EVERY 0.5 S, HARDER ON BURNING ENEMIES", "thunderstorm"},
    {Mod::Id::Overflow, Mod::Id::Twin, "POISED", "THE HALF BEAM USES THE STORED BULLETS TOO, BUT SPENDS ONLY HALF", "poised"},
    {Mod::Id::Momentum, Mod::Id::Afterglow, "COMBO", "THE END OF THE BEAM FILLS MOMENTUM, AND IT HOLDS DURING THE AFTERGLOW", "combo"},
    {Mod::Id::Focus, Mod::Id::Breach, "WEAKPOINT LOCK", "FULL FOCUS: +25% DAMAGE, AND A NEW TARGET KEEPS HALF THE STACKS", "weakpoint_lock"},
    {Mod::Id::Backwater, Mod::Id::Mend, "UNYIELDING", "UNDER HALF HP: MEND NEEDS NO WAIT AND REPAIRS 50% FASTER", "unyielding"},
    {Mod::Id::BackupCore, Mod::Id::Deflect, "REVENGE", "THE REVIVE KEEPS HALF A BAR, AND ITS BLAST TAKES WHITE BULLETS TOO", "revenge"}};

struct SaveData
{
    int shards = 0;
    int totalEarned = 0;
    int runs = 0;
    int bossClears = 0;

    int bestWave = 0;
    int bestMission = 0;       // percent of the mission of the best normal run
    int bestBoss = 0;          // percent of the boss bar of the best run at the boss
    int challengeUnlocked = 0; // highest unlocked challenge level
    bool bossSeen = false;     // boss practice becomes available
    int mode = 0;              // selected mode on the title screen (see ModeCount)
    std::string lastModules;   // module codes of the last finished run, comma separated
    bool tacticOwned[Mod::TACTIC_COUNT] = {};
    int tactic = -1;                         // the tactic that the ship takes into a run, -1 = none
    int talentLevel[Mod::TALENT_COUNT] = {}; // 0 = not bought
    int overloadBreaks = 0;    // overloads of the boss that the player has broken (the HUD explains the break until it is known)
    bool comboFound[COMBO_COUNT] = {};
};

namespace
{
constexpr const char* SAVE_FILE = "meta.sav";
SaveData g_Save;
bool g_Loaded = false;
bool g_DebugAll = false;
}

// ---------------------------------------------------------------- persistence
static int FindModuleByCode(const std::string& code)
{
    for (int i = 0; i < Mod::COUNT; ++i)
    {
        if (code == Mod::INFO[i].code) return i;
    }
    return -1;
}

// a module or a talent (the talents come after the modules)
static int FindTacticByCode(const std::string& code)
{
    for (int i = 0; i < Mod::TACTIC_COUNT; ++i)
        if (code == Mod::TACTICS[i].code) return i;
    return -1;
}

static int FindTalentByCode(const std::string& code)
{
    for (int i = 0; i < Mod::TALENT_COUNT; ++i)
        if (code == Mod::TALENTS[i].code) return i;
    return -1;
}

static int FindComboByCode(const std::string& code)
{
    for (int i = 0; i < COMBO_COUNT; ++i)
    {
        if (code == COMBOS[i].code) return i;
    }
    return -1;
}

// "echo,siphon" -> module ids (unknown codes are skipped)
std::vector<int> ModulesFromCsv(const std::string& csv)
{
    std::vector<int> ids;
    size_t pos = 0;
    while (pos <= csv.size())
    {
        size_t end = csv.find(',', pos);
        if (end == std::string::npos) end = csv.size();
        const int index = FindModuleByCode(csv.substr(pos, end - pos));
        if (index >= 0) ids.push_back(index);
        pos = end + 1;
    }
    return ids;
}

static void ResetSave()
{
    g_Save = SaveData();
}

static void Save()
{
    std::ofstream file(SAVE_FILE, std::ios::trunc);
    if (!file) return;

    file << "shards=" << g_Save.shards << "\n";
    file << "earned=" << g_Save.totalEarned << "\n";
    file << "runs=" << g_Save.runs << "\n";
    file << "boss_clears=" << g_Save.bossClears << "\n";
    file << "best_wave=" << g_Save.bestWave << "\n";
    file << "best_mission=" << g_Save.bestMission << "\n";
    file << "best_boss=" << g_Save.bestBoss << "\n";
    file << "challenge=" << g_Save.challengeUnlocked << "\n";
    file << "boss_seen=" << (g_Save.bossSeen ? 1 : 0) << "\n";
    file << "mode=" << g_Save.mode << "\n";
    file << "last_modules=" << g_Save.lastModules << "\n";

    std::string tactics;
    for (int i = 0; i < Mod::TACTIC_COUNT; ++i)
    {
        if (!g_Save.tacticOwned[i]) continue;
        if (!tactics.empty()) tactics += ',';
        tactics += Mod::TACTICS[i].code;
    }
    file << "tactics=" << tactics << "\n";
    file << "tactic=" << (g_Save.tactic >= 0 ? Mod::TACTICS[g_Save.tactic].code : "") << "\n";
    std::string talents;
    for (int i = 0; i < Mod::TALENT_COUNT; ++i)
    {
        if (g_Save.talentLevel[i] <= 0) continue;
        if (!talents.empty()) talents += ',';
        talents += std::string(Mod::TALENTS[i].code) + ":" + std::to_string(g_Save.talentLevel[i]);
    }
    file << "talents=" << talents << "\n";
    file << "overload_breaks=" << g_Save.overloadBreaks << "\n";

    std::string combos;
    for (int i = 0; i < COMBO_COUNT; ++i)
    {
        if (!g_Save.comboFound[i]) continue;
        if (!combos.empty()) combos += ',';
        combos += COMBOS[i].code;
    }
    file << "synergies=" << combos << "\n";
}

// "a,b,c" -> the items
static std::vector<std::string> SplitList(const std::string& list)
{
    std::vector<std::string> items;
    size_t pos = 0;
    while (pos <= list.size())
    {
        size_t end = list.find(',', pos);
        if (end == std::string::npos) end = list.size();
        if (end > pos) items.push_back(list.substr(pos, end - pos));
        pos = end + 1;
    }
    return items;
}

static void Load()
{
    g_Loaded = true;
    ResetSave();

    std::ifstream file(SAVE_FILE);
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "shards")
            g_Save.shards = (std::max)(0, std::atoi(value.c_str()));
        else if (key == "earned")
            g_Save.totalEarned = (std::max)(0, std::atoi(value.c_str()));
        else if (key == "runs")
            g_Save.runs = (std::max)(0, std::atoi(value.c_str()));
        else if (key == "boss_clears")
            g_Save.bossClears = (std::max)(0, std::atoi(value.c_str()));
        else if (key == "best_wave")
            g_Save.bestWave = (std::max)(0, std::atoi(value.c_str()));
        else if (key == "best_mission")
            g_Save.bestMission = (std::min)(100, (std::max)(0, std::atoi(value.c_str())));
        else if (key == "best_boss")
            g_Save.bestBoss = (std::min)(100, (std::max)(0, std::atoi(value.c_str())));
        else if (key == "challenge")
            g_Save.challengeUnlocked = (std::min)(CHALLENGE_MAX, (std::max)(0, std::atoi(value.c_str())));
        else if (key == "boss_seen")
            g_Save.bossSeen = std::atoi(value.c_str()) != 0;
        else if (key == "mode")
            g_Save.mode = (std::max)(0, std::atoi(value.c_str()));
        else if (key == "last_modules")
            g_Save.lastModules = value;
        else if (key == "unlocked") // (an old save: the talents of pass D were on / off; one that was bought is level 1 now)
        {
            for (const std::string& item : SplitList(value))
            {
                const int talent = FindTalentByCode(item);
                if (talent >= 0 && g_Save.talentLevel[talent] < 1) g_Save.talentLevel[talent] = 1;
            }
        }
        else if (key == "tactics")
        {
            for (const std::string& item : SplitList(value))
            {
                const int tactic = FindTacticByCode(item);
                if (tactic >= 0) g_Save.tacticOwned[tactic] = true;
            }
        }
        else if (key == "tactic")
            g_Save.tactic = FindTacticByCode(value);
        else if (key == "talents")
        {
            for (const std::string& item : SplitList(value))
            {
                const size_t colon = item.find(':');
                const int talent = FindTalentByCode(item.substr(0, colon));
                if (talent < 0) continue;
                const int level = colon == std::string::npos ? 1 : std::atoi(item.substr(colon + 1).c_str());
                g_Save.talentLevel[talent] = (std::min)(Mod::TALENT_LEVELS, (std::max)(0, level));
            }
        }
        else if (key == "synergies")
        {
            for (const std::string& item : SplitList(value))
            {
                const int combo = FindComboByCode(item);
                if (combo >= 0) g_Save.comboFound[combo] = true;
            }
        }
        else if (key == "overload_breaks")
            g_Save.overloadBreaks = (std::max)(0, std::atoi(value.c_str()));
    }

    // test switches (config.ini): shards=N sets the balance, unlock_all=1 opens every module / mode
    const int debugShards = DebugOptions::Shards();
    if (debugShards > 0) g_Save.shards = debugShards;
    g_DebugAll = DebugOptions::UnlockAll();
}

void Ensure()
{
    if (!g_Loaded) Load();
}

// ---------------------------------------------------------------- queries / hangar
int Shards()
{
    Ensure();
    return g_Save.shards;
}

// ---- tactics and talents (unlock_all opens them all, except for the simulator: no_talents=1 keeps them as they are in the save)
static bool DebugGrants()
{
    return g_DebugAll && !DebugOptions::NoTalents();
}

bool OwnsTactic(Mod::Tactic tactic)
{
    Ensure();
    return DebugGrants() || g_Save.tacticOwned[static_cast<int>(tactic)];
}

int EquippedTactic()
{
    Ensure();
    const int tactic = g_Save.tactic;
    return (tactic >= 0 && tactic < Mod::TACTIC_COUNT && OwnsTactic(static_cast<Mod::Tactic>(tactic))) ? tactic : -1;
}

void EquipTactic(int tactic)
{
    Ensure();
    if (tactic < 0 || tactic >= Mod::TACTIC_COUNT || !OwnsTactic(static_cast<Mod::Tactic>(tactic)) || g_Save.tactic == tactic)
        g_Save.tactic = -1;
    else
        g_Save.tactic = tactic;
    Save();
}

int TalentLevel(Mod::Talent talent)
{
    Ensure();
    if (DebugGrants()) return Mod::TALENT_LEVELS;
    return g_Save.talentLevel[static_cast<int>(talent)];
}

// a hangar id: a module (always there), a tactic that was bought, a talent at level 1 or more
bool IsUnlocked(int id)
{
    if (id >= 0 && id < Mod::COUNT) return true;
    if (id >= Mod::TACTIC_ID0 && id < Mod::TALENT_ID0) return OwnsTactic(static_cast<Mod::Tactic>(id - Mod::TACTIC_ID0));
    if (id >= Mod::TALENT_ID0 && id < Mod::HANGAR_ID_COUNT) return TalentLevel(static_cast<Mod::Talent>(id - Mod::TALENT_ID0)) > 0;
    return false;
}

// what ENTER buys now: a tactic that is not owned yet, the next level of a talent; 0 = nothing to buy
int Price(int id)
{
    if (id >= Mod::TACTIC_ID0 && id < Mod::TALENT_ID0) return IsUnlocked(id) ? 0 : Mod::TACTICS[id - Mod::TACTIC_ID0].price;
    if (id >= Mod::TALENT_ID0 && id < Mod::HANGAR_ID_COUNT)
    {
        const int level = TalentLevel(static_cast<Mod::Talent>(id - Mod::TALENT_ID0));
        return level < Mod::TALENT_LEVELS ? Mod::TALENT_PRICE[level] : 0;
    }
    return 0;
}

bool CanAfford(int id)
{
    const int price = Price(id);
    return price > 0 && Shards() >= price;
}

bool TryUnlock(int id)
{
    Ensure();
    if (!CanAfford(id) || DebugGrants()) return false;
    g_Save.shards -= Price(id);
    if (id < Mod::TALENT_ID0)
    {
        g_Save.tacticOwned[id - Mod::TACTIC_ID0] = true;
        if (g_Save.tactic < 0) g_Save.tactic = id - Mod::TACTIC_ID0; // the first tactic that is bought is taken along at once
    }
    else
    {
        ++g_Save.talentLevel[id - Mod::TALENT_ID0];
    }
    Save();
    return true;
}

bool AnyAffordable()
{
    for (int i = Mod::TACTIC_ID0; i < Mod::HANGAR_ID_COUNT; ++i)
    {
        if (CanAfford(i)) return true;
    }
    return false;
}

// the cheapest thing that can still be bought (the next goal); -1 when everything is bought
int NextTarget(int& missing)
{
    Ensure();
    int best = -1;
    for (int i = Mod::TACTIC_ID0; i < Mod::HANGAR_ID_COUNT; ++i)
    {
        if (Price(i) <= 0) continue;
        if (best < 0 || Price(i) < Price(best)) best = i;
    }
    missing = best >= 0 ? (std::max)(0, Price(best) - g_Save.shards) : 0;
    return best;
}

// what has been bought: the tactics, and every level of a talent
int UnlockedCount()
{
    int n = 0;
    for (int i = 0; i < Mod::TACTIC_COUNT; ++i)
        if (OwnsTactic(static_cast<Mod::Tactic>(i))) ++n;
    for (int i = 0; i < Mod::TALENT_COUNT; ++i)
        n += TalentLevel(static_cast<Mod::Talent>(i));
    return n;
}

int UnlockTotal()
{
    return Mod::TACTIC_COUNT + Mod::TALENT_COUNT * Mod::TALENT_LEVELS;
}

int OverloadBreaks()
{
    Ensure();
    return g_Save.overloadBreaks;
}

void NoteOverloadBreak()
{
    Ensure();
    if (g_Save.overloadBreaks >= 1000) return;
    ++g_Save.overloadBreaks;
    Save();
}

// ---------------------------------------------------------------- modes (title screen: LEFT / RIGHT while START GAME is selected)
//   0 .. ChallengeMax()                     standard run and the unlocked challenge levels
//   ChallengeMax()+1 (after the boss)       boss practice (the whole fight from phase 1)
int ChallengeMax()
{
    Ensure();
    if (!Balance::CHALLENGES_ENABLED) return 0; // (the standard run only: the mode selector is STANDARD and BOSS PRACTICE)
    return g_DebugAll ? CHALLENGE_MAX : g_Save.challengeUnlocked;
}

bool BossSeen()
{
    Ensure();
    return g_DebugAll || !Balance::PRACTICE_NEEDS_BOSS || g_Save.bossSeen;
}

int ModeCount()
{
    return ChallengeMax() + 1 + (BossSeen() ? PRACTICE_PHASES : 0);
}

bool ModeIsPractice(int mode)
{
    return mode > ChallengeMax();
}

int ModeLevel(int mode)
{
    return ModeIsPractice(mode) ? 0 : mode;
}

int ModePhase(int mode)
{
    return ModeIsPractice(mode) ? mode - ChallengeMax() : 0;
} // 1..3, 0 = not practice

int Mode()
{
    Ensure();
    const int count = ModeCount();
    return (g_Save.mode >= 0 && g_Save.mode < count) ? g_Save.mode : 0;
}

void CycleMode(int direction)
{
    Ensure();
    const int count = ModeCount();
    if (count <= 1) return;
    g_Save.mode = ((Mode() + direction) % count + count) % count;
    Save();
}

void MarkBossSeen()
{
    Ensure();
    if (g_Save.bossSeen) return;
    g_Save.bossSeen = true;
    Save();
}

void SetLastModules(const std::string& csv)
{
    Ensure();
    if (csv == g_Save.lastModules) return;
    g_Save.lastModules = csv;
    Save();
}

// ---------------------------------------------------------------- end of a run
RunResult Compute(const RunStats& stats)
{
    RunResult result;
    result.level = stats.level;

    if (stats.abandoned && stats.frames < ABANDON_MIN_FRAMES)
    {
        return result; // valid = false: nothing awarded
    }

    result.waves = SHARDS_PER_WAVE * (std::min)(Balance::WAVE_COUNT, (std::max)(0, stats.wavesCleared));
    if (stats.reachedBoss)
    {
        result.boss = SHARDS_BOSS_REACHED;
        result.phases = SHARDS_PER_BOSS_PHASE * (std::min)(2, (std::max)(0, stats.bossPhase - 1));
    }
    result.clear = stats.bossCleared ? SHARDS_BOSS_CLEARED : 0;
    result.absorbs = (std::min)(SHARDS_ABSORB_CAP, (std::max)(0, stats.absorbs) / 8);

    int total = result.waves + result.boss + result.phases + result.clear + result.absorbs;
    if (total < SHARDS_FLOOR && stats.frames >= FLOOR_MIN_FRAMES)
    {
        total = SHARDS_FLOOR;
    }

    if (total > 0 && stats.level > 0)
    {
        result.bonus = (total * CHALLENGE_BONUS_PERCENT * (std::min)(CHALLENGE_MAX, stats.level) + 50) / 100;
        total += result.bonus;
    }

    result.total = total;
    result.valid = total > 0;
    return result;
}

RunResult AwardRun(const RunStats& stats)
{
    Ensure();
    RunResult result = Compute(stats);
    if (!result.valid)
    {
        result.balance = g_Save.shards;
        return result;
    }

    g_Save.shards += result.total;
    g_Save.totalEarned += result.total;
    ++g_Save.runs;
    if (stats.bossCleared)
    {
        ++g_Save.bossClears;

        // clearing the highest unlocked level opens the next one
        if (Balance::CHALLENGES_ENABLED && stats.level == g_Save.challengeUnlocked && g_Save.challengeUnlocked < CHALLENGE_MAX)
        {
            ++g_Save.challengeUnlocked;
            result.unlockedLevel = g_Save.challengeUnlocked;
        }
    }
    g_Save.bestWave = (std::max)(g_Save.bestWave, stats.reachedBoss ? Balance::BOSS_WAVE : stats.wavesCleared + 1);
    Save();

    result.balance = g_Save.shards;
    return result;
}

// ---------------------------------------------------------------- best progress
int BestMission()
{
    Ensure();
    return g_Save.bestMission;
}

int BestBoss()
{
    Ensure();
    return g_Save.bestBoss;
}

void NoteProgress(int missionPercent, int bossPercent, bool practice)
{
    Ensure();
    const int mission = (std::min)(100, (std::max)(0, missionPercent));
    const int boss = (std::min)(100, (std::max)(0, bossPercent));
    bool changed = false;
    if (!practice && mission > g_Save.bestMission)
    {
        g_Save.bestMission = mission;
        changed = true;
    }
    if (boss > g_Save.bestBoss)
    {
        g_Save.bestBoss = boss;
        changed = true;
    }
    if (changed) Save();
}

// ---------------------------------------------------------------- synergies
bool ComboOwned(const bool owned[], int combo)
{
    return owned[static_cast<int>(COMBOS[combo].a)] && owned[static_cast<int>(COMBOS[combo].b)];
}

bool ComboFound(int combo)
{
    Ensure();
    return combo >= 0 && combo < COMBO_COUNT && g_Save.comboFound[combo];
}

// marks the synergies that the owned set completes; returns how many were new (their indices go to found[])
int DiscoverNew(const bool owned[], int found[], int capacity)
{
    Ensure();
    int count = 0;
    for (int i = 0; i < COMBO_COUNT; ++i)
    {

        if (g_Save.comboFound[i] || !ComboOwned(owned, i)) continue;
        g_Save.comboFound[i] = true;
        if (count < capacity) found[count] = i;
        ++count;
    }
    if (count > 0) Save();
    return count;
}

// would picking `candidate` complete a synergy? (returns its index or -1)
int ComboCompletedBy(const bool owned[], int candidate)
{
    for (int i = 0; i < COMBO_COUNT; ++i)
    {
        const int a = static_cast<int>(COMBOS[i].a);
        const int b = static_cast<int>(COMBOS[i].b);
        if (owned[a] && owned[b]) continue; // already complete
        if ((candidate == a && owned[b]) || (candidate == b && owned[a])) return i;
    }
    return -1;
}

int VerbCount(const bool owned[], Mod::Verb verb)
{
    int n = 0;
    for (int i = 0; i < Mod::COUNT; ++i)
    {
        if (owned[i] && Mod::INFO[i].verb == verb) ++n;
    }
    return n;
}

std::string LastModules()
{
    Ensure();
    return g_Save.lastModules;
}
}




