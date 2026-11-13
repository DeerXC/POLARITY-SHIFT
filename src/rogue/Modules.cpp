#include "rogue/Modules.h"
#include "boss/GameBoss.h"
#include "core/DebugOptions.h"
#include "core/GameAudio.h"
#include "game/GameEffect.h"
#include "rogue/Balance.h"
#include "game/GameSave.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/GameWave.h"
#include "rogue/Meta.h"
#include "rogue/ModulesInternal.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <string>

// Module state, queries, run log, start / end of a run.
namespace Mod
{
bool g_Owned[COUNT]{};

int g_Order[COUNT]{};

int g_OrderCount = 0;

bool g_BackupUsed = false;

bool g_ChoiceOpen = false;

int g_Choice[3]{};

int g_ChoiceCount = 0;

int g_ChoiceSel = 0;

int g_ChoiceTimer = 0;

bool g_ChoiceDone = true;

bool g_PrevStarting = false;

bool g_PrevCleared = false;

int g_PicksLeft = 1;     // picks still to make on this pick screen sequence (2 after an elite wave)

bool g_RareNext = false; // the next pick offers an upgrade card (when there is a module to upgrade)






int g_EchoCounter = 0;

int g_IdleAbsorbFrames = 0;

int g_SoundCooldown = 0;

bool g_StartPickDone = false;
int g_RerollsLeft = 0;
int g_PrevChoice[3]{};
int g_PrevChoiceCount = 0;
bool g_Rerolling = false;
int g_AegisTimer = 0;
int g_AegisFlash = 0;
float g_Momentum = 0.0f;
int g_MomentumIdle = 0;
int g_Level[COUNT]{};
bool g_ChoiceUpgrade[3]{};
bool g_ForgeNext = false;
bool g_WagerForge = false;
int g_RouteStage = 0;
int g_RouteKind[3]{};
int g_RouteCount = 0;
bool g_Branch2Done = false;
float g_ChainHeat = 0.0f;
float g_EnergyFrac = 0.0f;
int g_EmberLife = EMBER_FRAMES;




EchoArc g_Arcs[6]{};
Zone g_Decoy{};
Polarity g_PrevPolarity = Polarity::Blue;
float g_RateAcc = 0.0f;
int g_SinceHurt = 0;
float g_Streak = 0.0f;
int g_WaveSpawned = 0;
int g_WaveKilled = 0;
int g_LastClearPercent = 100;
int g_LastClearHeal = 0;
float g_StatKillEnergy = 0.0f;
float g_StatAbsorbEnergy = 0.0f;
float g_MendAcc = 0.0f;
int g_OverflowStore = 0;
float g_BeamDamageScale = 1.0f;
bool g_BeamIsHalf = false;
int g_AfterglowTimer = 0;
int g_FocusTarget = -1;
int g_FocusStacks = 0;
int g_FocusIdle = 0;
int g_PulseCooldown = 0;
int g_ResArcCounter = 0;
int g_Tactic = -1;
int g_TacticCooldown = 0;
int g_DashTimer = 0;
float g_DashX = 0.0f, g_DashY = 0.0f;
int g_ChargeFrames = 0;
int g_TacticFlash = 0;
float g_TacticBeamX = 0.0f, g_TacticBeamY = 0.0f, g_TacticBeamHalf = 0.0f;
bool g_PrevTacticDown = false;



int g_PulseTimer = 0;

float g_PulseX = 0.0f, g_PulseY = 0.0f, g_PulseRadius = 0.0f;

bool g_PrevSpecialActive = false;

float g_BeamStartX = 0.0f, g_BeamEndX = 0.0f, g_BeamY = 0.0f, g_BeamHalf = 0.0f;

int g_MendStartTimer = 0;
int g_MendTickFlash = 0;
bool g_MendWasOn = false;
int g_ReviveTimer = 0;
float g_ReviveX = 0.0f, g_ReviveY = 0.0f;

int g_EmberTimer = 0;

int g_EmberTick = 0;

float g_EmberStartX = 0.0f, g_EmberEndX = 0.0f, g_EmberY = 0.0f, g_EmberHalf = 0.0f;

long long g_RunId = 0;

int g_RunFrames = 0;

int g_WaveStartFrame = 0;

int g_RunAbsorbs = 0;

int g_RunHits = 0;

int g_WavesCleared = 0;

bool g_ReachedBoss = false;

int g_MaxBossPhase = 1;

int g_DeathWave = 1;

bool g_DeathInBoss = false;

int g_DeathBossPhase = 1;

int g_DeathSeconds = 0;

int g_EndMission = 0;
int g_EndBoss = 0;
int g_EndBestMission = 0;
int g_EndBestBoss = 0;

int g_BossScorePaid = 0;
bool g_BossClearPaid = false;

int g_GameOverLock = 0;

bool g_ResultAwarded = false;

bool g_HasResult = false;

Meta::RunResult g_Result;

int g_ToastTimer = 0;

int g_ToastCombo = -1;

// ---- challenge / route state
int g_RunLevel = 0;      // challenge level of the current run (0 = standard)

int g_PracticePhase = 0; // 1 = boss practice (whole fight), 0 = normal run

int g_Branch = 0;        // wave route: 0 not chosen, 1 elite fleet, 2 supply run

bool g_BranchOpen = false;

bool g_BranchDone = false;

int g_BranchSel = 0;

int g_BranchTimer = 0;

int g_BranchMessage = 0; // frames left of the "needs damage" message

int g_PlayFrames = 0;    // frames since the current wave started to play

int g_ElitesSpawned = 0;

bool g_PrevEnemyActive[GameState::ENEMY_MAX]{};

bool g_PrevBulletActive[GameState::ENEMY_BULLET_MAX]{};

// hangar (title screen overlay)
bool g_HangarOpen = false;

int g_HangarSel = 0; // module id
int g_HangarSyn = -1;
bool g_HangarSynFocus = false;

int g_HangarTimer = 0;

int g_HangarMessage = 0; // frames left of the "not enough shards" message

int g_HangarFlash = 0;   // frames left of the unlock flash

int g_HangarFlashId = -1;

// ---------------------------------------------------------------- queries
bool Has(Id id)
{
    return g_Owned[static_cast<int>(id)];
}

int VerbCount(Verb verb)
{
    return Meta::VerbCount(g_Owned, verb);
}

// resonance: two and three modules of one type each add something (Balance::RES_*)
bool Resonant(Verb verb, int count)
{
    return VerbCount(verb) >= count;
}

bool ComboOn(int combo)
{
    return Meta::ComboOwned(g_Owned, combo);
}

// ---- levels: a module the ship owns again is upgraded (pick screen, forge, wager). Level 2 multiplies the good numbers of the module by
// Gain() and its costs by CostK() (ModulesData.cpp: GainAt / CostAt, from Balance::LEVEL_GAIN / LEVEL_COST). A module can be upgraded once: level 2.
int Level(Id id)
{
    return g_Level[static_cast<int>(id)];
}

float Gain(Id id)
{
    return GainAt(id, Level(id));
}

float CostK(Id id)
{
    return CostAt(id, Level(id));
}

int LevelCap(int id)
{
    (void)id;
    return Balance::LEVEL_CAP;
}

bool CanUpgrade(int id)
{
    return id >= 0 && id < COUNT && g_Owned[id] && g_Level[id] < LevelCap(id);
}

bool AnyUpgradable()
{
    for (int i = 0; i < COUNT; ++i)
    {
        if (CanUpgrade(i)) return true;
    }
    return false;
}

void UpgradeModule(Id id)
{
    if (CanUpgrade(static_cast<int>(id))) ++g_Level[static_cast<int>(id)];
}

bool LowHp()
{
    return GameState::playerHp > 0 && GameState::playerHp * 2 < MaxHp();
}

static float TalentAt(Talent talent)
{
    return TalentValue(talent, Meta::TalentLevel(talent));
}

// ---- damage: the ship deals Balance::HP_UNIT points per shot. AFTERGLOW makes every shot hit harder for a while after the beam.
float DamageMul()
{
    float mul = g_AfterglowTimer > 0 ? 1.0f + Balance::AFTERGLOW_BONUS : 1.0f;
    if (StreakTier() >= 2) mul *= 1.0f + Balance::STREAK_DAMAGE_TIER2;
    return mul;
}

int StreakTier()
{
    if (g_Streak >= static_cast<float>(Balance::STREAK_TIER2)) return 2;
    if (g_Streak >= static_cast<float>(Balance::STREAK_TIER1)) return 1;
    return 0;
}

float StreakProgress()
{
    const float t1 = static_cast<float>(Balance::STREAK_TIER1), t2 = static_cast<float>(Balance::STREAK_TIER2);
    if (g_Streak >= t2) return 1.0f;
    if (g_Streak >= t1) return (g_Streak - t1) / (t2 - t1);
    return g_Streak / t1;
}

static void SetStreak(float value)
{
    const int before = GameState::GetScoreMultiplier();
    g_Streak = value < 0.0f ? 0.0f : value;
    GameState::combo = static_cast<int>(g_Streak);
    if (GameState::combo > GameState::maxCombo) GameState::maxCombo = GameState::combo;
    const int after = GameState::GetScoreMultiplier();
    if (after > before) // a new tier: the "X2" / "X3" flash of the scene
    {
        GameState::comboRankEffectValue = after;
        GameState::comboRankEffectTimer = GameState::COMBO_RANK_EFFECT_DURATION;
    }
}

void NoteDamage(float shots)
{
    if (shots > 0.0f && GameState::playerHp > 0) SetStreak(g_Streak + shots);
}

int LastClearPercent()
{
    return g_LastClearPercent;
}

int LastClearHeal()
{
    return g_LastClearHeal;
}

int PlayerShotDamage(float armorMul)
{
    const int damage = static_cast<int>(std::lround(static_cast<float>(Balance::HP_UNIT) * DamageMul() * armorMul));
    return damage < 1 ? 1 : damage;
}

// FOCUS: a hit on the same target as the hit before adds a stack; another target starts again. Returns what this hit does (1 = plain).
float OnShotHit(int target)
{
    if (!Has(Id::Focus)) return 1.0f;
    if (target != g_FocusTarget)
    {
        g_FocusTarget = target;
        // synergy: weakpoint lock - a new target after a full focus keeps half of the stacks
        g_FocusStacks = (g_FocusStacks >= Balance::FOCUS_MAX_STACKS && ComboOn(Meta::ComboLock)) ? Balance::FOCUS_MAX_STACKS / 2 : 0;
    }
    float mul = 1.0f + Balance::FOCUS_STEP * Gain(Id::Focus) * static_cast<float>(g_FocusStacks);
    if (g_FocusStacks >= Balance::FOCUS_MAX_STACKS && ComboOn(Meta::ComboLock)) mul *= 1.25f; // synergy: weakpoint lock
    if (g_FocusStacks < Balance::FOCUS_MAX_STACKS) ++g_FocusStacks;
    g_FocusIdle = 0;
    return mul;
}

// synergy WEAKPOINT LOCK: FOCUS at its full stacks with BREACH hits 25 % harder (OnShotHit), and a new target keeps half of the stacks
bool FocusLocked()
{
    return Has(Id::Focus) && g_FocusStacks >= Balance::FOCUS_MAX_STACKS && ComboOn(Meta::ComboLock);
}

float SpecialDamageMul()
{
    return g_BeamDamageScale;
}

int SpecialTickDamage()
{
    return static_cast<int>(std::lround(static_cast<float>(PlayerShotDamage(1.0f)) * SpecialDamageMul()));
}

int AegisAbsorbsNeeded()
{
    // the shield comes back after this many absorbed bullets: it needs the core of the game, and it needs fewer of them at level 2
    const float absorbs = static_cast<float>(Balance::AEGIS_ABSORBS) / Gain(Id::Aegis);
    return (std::max)(1, static_cast<int>(absorbs));
}

float DeflectChance()
{
    return Has(Id::Deflect) ? Balance::DEFLECT_CHANCE : 0.0f;
}

float DeflectEnergyMul()
{
    return Balance::DEFLECT_ENERGY_MUL * Gain(Id::Deflect);
}

bool TryDeflect()
{
    const float chance = DeflectChance();
    return chance > 0.0f && static_cast<float>(std::rand() % 1000) < chance * 1000.0f;
}

// a body that flies into the ship: an enemy ("ram") or the hull of the boss ("boss_body_...")
static bool IsBodyHit(const char* source)
{
    return source != nullptr && (std::strcmp(source, "ram") == 0 || std::strncmp(source, "boss_body", 9) == 0);
}

// what AEGIS stops: a white bullet ("white_...") or a beam of the boss ("boss_beam_...")
static bool IsWhiteOrBeam(const char* source)
{
    return source != nullptr && (std::strncmp(source, "white", 5) == 0 || std::strncmp(source, "boss_beam", 9) == 0);
}

int InvincibleFrames()
{
    float frames = static_cast<float>(Balance::INVINCIBLE_FRAMES) * (1.0f + TalentAt(Talent::Buffer));
    if (Has(Id::Backwater) && LowHp()) frames *= 1.0f + Balance::BACKWATER_INVINCIBLE;
    return static_cast<int>(std::lround(frames));
}

float ShotDurationScale()
{
    return 1.0f + TalentAt(Talent::Pierce);
}

void OnPhaseStart(bool bossPhase)
{
    if (GameState::playerHp <= 0) return;
    if (bossPhase && Resonant(Verb::Survive, 3)) HealPlayer(Balance::RES_SURVIVE_PHASE_HEAL); // resonance: three survive modules
    const int add = static_cast<int>(static_cast<float>(EnergyMax()) * TalentAt(Talent::Charge));
    if (add > 0 && GameState::energy < add) GameState::energy = add; // (the talent CHARGE fills the bar up to that much, it never takes anything away)
}

// AEGIS: the shield takes a white bullet or a beam (0 damage) and has to recharge. Combat::HurtPlayer sees the 0 and plays the clang.
int ScaleDamageTaken(int amount, const char* source)
{
    if (amount > 0 && Has(Id::Aegis) && g_AegisTimer <= 0 && IsWhiteOrBeam(source))
    {
        g_AegisTimer = AegisAbsorbsNeeded();
        g_AegisFlash = 30;
        GameAudio::Play(GameSound::AegisBlock);
        return 0;
    }
    // PULSE: a body that flies into the ship hits softer
    if (amount > 0 && Has(Id::Pulse) && IsBodyHit(source))
    {
        amount = static_cast<int>(std::lround(static_cast<float>(amount) * (1.0f - Balance::PULSE_BODY_CUT)));
    }
    // mercy: a ship that is nearly dead is hit a little less hard (a good player, who stays at full HP, never notices it)
    const float hpFraction = static_cast<float>(GameState::playerHp) / static_cast<float>(MaxHp() > 0 ? MaxHp() : 1);
    const float mercy = 1.0f - Balance::MERCY_MAX * (1.0f - (hpFraction < 0.0f ? 0.0f : (hpFraction > 1.0f ? 1.0f : hpFraction)));
    amount = static_cast<int>(std::lround(static_cast<float>(amount) * mercy));
    return amount < 1 ? 1 : amount;
}

// what a plain shot does to an enemy that still has its shield (an elite, the rival): BREACH and WEAKPOINT LOCK go through it
float ShieldFactor()
{
    if (FocusLocked() || Has(Id::Breach)) return 1.0f;
    return Balance::ELITE_SHIELD;
}

// what the boss lets through of a shot in phase 1 and 2 (it resists BOSS_RESIST of it; BREACH takes some of that away; WEAKPOINT LOCK all of it)
float BossResistFactor()
{
    if (FocusLocked() || Has(Id::Breach)) return 1.0f; // BREACH ignores the resistance of the boss
    return 1.0f - Balance::BOSS_RESIST;
}

// BREACH level 2: every hit on an elite, a rival or the boss does this much more
float BreachBonus()
{
    return (Has(Id::Breach) && Level(Id::Breach) >= 2) ? 1.0f + Balance::BREACH_LEVEL2_BONUS : 1.0f;
}

void HealPlayer(int amount)
{
    if (GameState::playerHp <= 0 || GameState::playerHp >= MaxHp() || amount <= 0) return;
    GameState::playerHp = (std::min)(MaxHp(), GameState::playerHp + amount);
    GameEffect::SpawnAbsorb(GameState::player.x, GameState::player.y, 0.12f, GameState::player.polarity);
    GameAudio::Play(GameSound::Heal); // (it was EnergyFull: the sound of the bar of energy being full - a repair has its own)
}

int RerollsLeft()
{
    return g_RerollsLeft;
}

float AegisCharge()
{
    if (!Has(Id::Aegis)) return -1.0f;
    if (g_AegisTimer <= 0) return 1.0f;
    const int total = AegisAbsorbsNeeded();
    return total <= 0 ? 1.0f : 1.0f - static_cast<float>(g_AegisTimer) / static_cast<float>(total);
}

float ChainRate()
{
    return Balance::ChainScale(g_ChainHeat);
}

// what the piercing beam needs: the resonance of two break modules and BACKWATER (under half of the HP) make it cheaper
int EnergyMax()
{
    int value = GameState::MAX_ENERGY;
    if (Resonant(Verb::Break)) value -= Balance::RES_BREAK_ENERGY_CUT;
    if (Has(Id::Backwater) && LowHp()) value -= Balance::BACKWATER_ENERGY_CUT;
    return value;
}

// the absorb box of the ship: SIPHON, the resonance of two absorb modules and the talent DUAL (while blue)
float SiphonScale()
{
    float scale = 1.0f;
    if (Has(Id::Siphon)) scale += (SIPHON_RANGE - 1.0f) * Gain(Id::Siphon);
    if (Resonant(Verb::Absorb)) scale += Balance::RES_ABSORB_RANGE;
    if (GameState::player.polarity == Polarity::Blue) scale += TalentAt(Talent::Dual);
    return scale;
}

// the fire rate bonus of this step (a fraction: 0.4 = 40 % faster): MOMENTUM, AFTERGLOW, and the talent DUAL while red
float FireRateBonus()
{
    float bonus = 0.0f;
    if (Has(Id::Momentum)) bonus += Balance::MOMENTUM_STEP * g_Momentum * Gain(Id::Momentum);
    if (g_AfterglowTimer > 0) bonus += Balance::AFTERGLOW_BONUS;
    if (GameState::player.polarity == Polarity::Red) bonus += TalentAt(Talent::Dual);
    return bonus;
}

// the module pick screen and the route screen both freeze the game
bool ChoiceOpen()
{
    return g_ChoiceOpen || g_BranchOpen;
}

// ---- challenge / route queries
int OverflowStored()
{
    return Has(Id::Overflow) ? g_OverflowStore : 0;
}
int OverflowStoredMax()
{
    return Balance::OVERFLOW_STORE_MAX;
}

int MaxHp()
{
    int hp = Balance::PLAYER_MAX_HP - (g_RunLevel >= Meta::CHALLENGE_MAX ? Balance::CHALLENGE_MAX_HP_PENALTY : 0);
    if (Resonant(Verb::Survive)) hp += Balance::RES_SURVIVE_HP; // resonance: two survive modules
    if (Has(Id::Aegis)) // the price of the shield
    {
        const float percent = static_cast<float>(Balance::AEGIS_MAXHP_PERCENT) * CostK(Id::Aegis);
        hp -= static_cast<int>(std::lround(static_cast<float>(Balance::PLAYER_MAX_HP) * percent / 100.0f));
    }
    return hp;
}

bool SupplyAvailable()
{
    return GameState::playerHp > 0 && GameState::playerHp < MaxHp();
}

// ---------------------------------------------------------------- run log (run_log.csv)
std::string ModuleCodes(char separator)
{
    std::string text;
    for (int i = 0; i < g_OrderCount; ++i)
    {
        if (!text.empty()) text += separator;
        text += INFO[g_Order[i]].code;
    }
    return text;
}

void WriteLogRow(const char* event, const char* note)
{
    const char* path = "run_log.csv";
    bool needHeader = true;
    {
        std::ifstream probe(path, std::ios::binary);
        needHeader = !(probe.good() && probe.peek() != std::ifstream::traits_type::eof());
    }
    std::ofstream file(path, std::ios::app);
    if (!file) return;
    if (needHeader)
    {
        file << "run_id,event,wave,run_seconds,wave_seconds,hp,score,absorbs,hits,modules,note\n";
    }
    const int wave = GameBoss::IsActive() ? Balance::BOSS_WAVE : GameWave::GetCurrentWave();
    file << g_RunId << ',' << event << ',' << wave << ',' << std::fixed << std::setprecision(1)
         << static_cast<double>(g_RunFrames) / 60.0 << ',' << static_cast<double>(g_RunFrames - g_WaveStartFrame) / 60.0 << ','
         << GameState::playerHp << ',' << GameState::score << ',' << g_RunAbsorbs << ',' << g_RunHits << ',' << ModuleCodes('|') << ',' << note
         << '\n';
}

// ---------------------------------------------------------------- run history (records page)
void RecordHistory(bool cleared, bool abandoned, int shards)
{
    if (abandoned && g_RunFrames < Meta::ABANDON_MIN_FRAMES) return; // left within 20 seconds: nothing worth a line
    RunRecord record;
    record.time = static_cast<long long>(std::time(nullptr));
    record.result = cleared ? 1 : (abandoned ? 2 : 0);
    const bool boss = cleared || g_ReachedBoss;
    record.wave = boss ? Balance::BOSS_WAVE : (std::min)(Balance::WAVE_COUNT, (std::max)(1, GameWave::GetCurrentWave()));
    record.bossPhase = boss ? (cleared ? 3 : (std::max)(1, (std::min)(3, g_MaxBossPhase))) : 0;
    record.mode = g_PracticePhase > 0 ? RUN_MODE_PRACTICE : g_RunLevel;
    record.score = GameState::score;
    record.maxCombo = GameState::maxCombo;
    record.seconds = g_RunFrames / 60;
    record.absorbs = g_RunAbsorbs;
    record.hits = g_RunHits;
    record.shards = shards;
    record.modules = ModuleCodes(',');
    GameSave::AddRunRecord(record);
}

// ---------------------------------------------------------------- how far the run came (the defeat screen)
// Percent, rounded DOWN and never 100 unless the boss is down: "99 %" has to mean that something is left.
void CaptureProgress(bool cleared)
{
    const float boss = cleared ? 1.0f : (g_ReachedBoss ? GameBoss::GetProgress() : 0.0f);
    float mission = cleared ? 1.0f : Balance::MissionProgress(GameWave::GetCurrentWave(), GameWave::GetCurrentSegment(), g_ReachedBoss, boss);
    if (g_PracticePhase > 0) mission = boss; // practice is the boss alone
    const int cap = cleared ? 100 : 99;
    g_EndBoss = (std::min)(cap, static_cast<int>(boss * 100.0f));
    g_EndMission = (std::min)(cap, static_cast<int>(mission * 100.0f));
    g_EndBestMission = Meta::BestMission();
    g_EndBestBoss = Meta::BestBoss();
    Meta::NoteProgress(g_EndMission, g_EndBoss, g_PracticePhase > 0);
}

// ---------------------------------------------------------------- shards at the end of a run
void EndRun(bool cleared, bool abandoned)
{
    if (g_ResultAwarded) return;
    g_ResultAwarded = true;
    CaptureProgress(cleared);

    // boss practice: no shards, no unlocks; its score counts like that of a run (the defeat and the clear record theirs in PlayScene, a run that is left here)
    if (g_PracticePhase > 0)
    {
        if (Balance::PRACTICE_SCORES && abandoned && !cleared) GameSave::RecordAbandoned(GameState::score, GameState::maxCombo);
        RecordHistory(cleared, abandoned, 0);
        WriteLogRow("practice_end", cleared ? "cleared" : "left");
        return;
    }

    if (g_OrderCount > 0) Meta::SetLastModules(ModuleCodes(',')); // boss practice starts with these

    // a run that is left (RESTART, TITLE, the window closed) keeps its score: a defeat and a clear record theirs in PlayScene, an abandoned run never gets there
    if (abandoned && !cleared) GameSave::RecordAbandoned(GameState::score, GameState::maxCombo);

    Meta::RunStats stats;
    stats.wavesCleared = g_WavesCleared;
    stats.reachedBoss = g_ReachedBoss;
    stats.bossPhase = g_MaxBossPhase;
    stats.bossCleared = cleared;
    stats.absorbs = g_RunAbsorbs;
    stats.frames = g_RunFrames;
    stats.abandoned = abandoned;
    stats.level = g_RunLevel;

    g_Result = Meta::AwardRun(stats);
    g_HasResult = g_Result.valid;
    RecordHistory(cleared, abandoned, g_Result.valid ? g_Result.total : 0);
    if (g_Result.valid)
    {
        WriteLogRow("shards", ("+" + std::to_string(g_Result.total)).c_str());
    }
    if (g_Result.unlockedLevel >= 0)
    {
        WriteLogRow("challenge_unlocked", std::to_string(g_Result.unlockedLevel).c_str());
    }
}

void LogEvent(const char* event, const char* note)
{
    WriteLogRow(event, note);

    const std::string name = event;
    if (name == "boss_clear")
        EndRun(true, false);
    else if (name == "restart" || name == "quit_to_title")
        EndRun(false, true);
}

// ---------------------------------------------------------------- lifecycle
void Reset()
{
    for (bool& owned : g_Owned)
        owned = false;
    g_OrderCount = 0;
    g_BackupUsed = false;
    g_ChoiceOpen = false;
    g_ChoiceCount = 0;
    g_ChoiceSel = 0;
    g_ChoiceTimer = 0;
    g_ChoiceDone = true;
    g_PrevStarting = false;
    g_PrevCleared = false;
    g_PicksLeft = 1;
    g_RareNext = false;
    g_PrevPolarity = GameState::player.polarity;
    g_EchoCounter = 0;
    g_IdleAbsorbFrames = 0;
    g_SoundCooldown = 0;
    g_RerollsLeft = Balance::REROLLS_PER_RUN + static_cast<int>(TalentValue(Talent::Veteran, Meta::TalentLevel(Talent::Veteran)));
    g_StartPickDone = false;
    g_PrevChoiceCount = 0;
    g_Rerolling = false;
    g_AegisTimer = 0;
    g_AegisFlash = 0;
    g_Momentum = 0.0f;
    g_MomentumIdle = 0;
    g_RateAcc = 0.0f;
    g_SinceHurt = 0;
    g_Streak = 0.0f;
    g_WaveSpawned = 0;
    g_WaveKilled = 0;
    g_LastClearPercent = 100;
    g_LastClearHeal = 0;
    g_StatKillEnergy = 0.0f;
    g_StatAbsorbEnergy = 0.0f;
    g_MendAcc = 0.0f;
    g_OverflowStore = 0;
    g_BeamDamageScale = 1.0f;
    g_BeamIsHalf = false;
    g_AfterglowTimer = 0;
    g_FocusTarget = -1;
    g_FocusStacks = 0;
    g_FocusIdle = 0;
    g_PulseCooldown = Balance::PULSE_PERIOD;
    g_ResArcCounter = 0;
    g_Tactic = Meta::EquippedTactic();
    g_TacticCooldown = 0;
    g_DashTimer = 0;
    g_ChargeFrames = 0;
    g_TacticFlash = 0;
    g_PrevTacticDown = false;
    g_Decoy = Zone{};
    for (int& level : g_Level)
        level = 0;
    for (bool& flag : g_ChoiceUpgrade)
        flag = false;
    g_ForgeNext = false;
    g_WagerForge = false;
    g_RouteStage = 0;
    g_RouteCount = 0;
    g_Branch2Done = false;
    g_ChainHeat = 0.0f;
    g_EnergyFrac = 0.0f;
    g_EmberLife = EMBER_FRAMES;
    for (EchoArc& a : g_Arcs)
        a = EchoArc{};
    g_PulseTimer = 0;
    g_PrevSpecialActive = false;
    g_EmberTimer = 0;
    g_EmberTick = 0;
    g_MendStartTimer = 0;
    g_MendTickFlash = 0;
    g_MendWasOn = false;
    g_ReviveTimer = 0;
    g_RunId = static_cast<long long>(std::time(nullptr));
    g_RunFrames = 0;
    g_WaveStartFrame = 0;
    g_RunAbsorbs = 0;
    g_RunHits = 0;
    g_WavesCleared = 0;
    g_ReachedBoss = false;
    g_MaxBossPhase = 1;
    g_GameOverLock = 0;
    g_BossScorePaid = 0;
    g_BossClearPaid = false;
    g_EndMission = g_EndBoss = g_EndBestMission = g_EndBestBoss = 0;
    g_ResultAwarded = false;
    g_HasResult = false;
    g_Result = Meta::RunResult();
    g_ToastTimer = 0;
    g_ToastCombo = -1;
    g_HangarOpen = false;
    Meta::Ensure();

    // the mode selected on the title screen decides level / practice for this run
    const int mode = Meta::Mode();
    g_PracticePhase = Meta::ModePhase(mode);
    g_RunLevel = Meta::ModeLevel(mode);
    GameState::playerHp = MaxHp();
    g_Branch = 0;
    g_BranchOpen = false;
    g_BranchDone = false;
    g_BranchSel = 0;
    g_BranchTimer = 0;
    g_BranchMessage = 0;
    ResetPickUi();
    g_PlayFrames = 0;
    g_ElitesSpawned = 0;
    for (bool& flag : g_PrevEnemyActive)
        flag = false;
    for (bool& flag : g_PrevBulletActive)
        flag = false;
}

void Grant(Id id)
{
    const int index = static_cast<int>(id);
    if (g_Owned[index]) return;
    g_Owned[index] = true;
    g_Level[index] = 1;
    g_Order[g_OrderCount++] = index;
    if (id == Id::Aegis) g_AegisTimer = 0; // the shield is up at once
    if (GameState::playerHp > MaxHp()) GameState::playerHp = MaxHp(); // (AEGIS lowers the maximum)
}

// Boss practice: jump into the boss fight (the same sequence as start_at_boss) with the modules of the last run.
void ApplyPractice()
{
    if (g_PracticePhase <= 0) return;

    for (const int id : Meta::ModulesFromCsv(Meta::LastModules()))
    {
        Grant(static_cast<Id>(id));
    }

    if (DebugOptions::StartAtBoss()) return; // ApplyDebugStart() has already done it

    GameAudio::Stop(GameSound::StageBgm);
    GameWave::PrepareBossRetry();
    GameBoss::Initialize();
    GameBoss::Start();
    GameAudio::PlayLoop(GameSound::BossBgm);
    WriteLogRow("practice_start", std::to_string(g_PracticePhase).c_str());
}

// config.ini  modules=0,4,8  or  modules=all   (test switch, see DebugOptions.h); also starts boss practice
void ApplyDebug()
{
    const std::string list = DebugOptions::ModuleList();
    if (list == "all")
    {
        for (int i = 0; i < COUNT; ++i)
            Grant(static_cast<Id>(i));
    }
    else
    {
        size_t pos = 0;
        while (pos < list.size())
        {
            size_t end = list.find(',', pos);
            if (end == std::string::npos) end = list.size();
            const int value = std::atoi(list.substr(pos, end - pos).c_str());
            if (value >= 0 && value < COUNT) Grant(static_cast<Id>(value));
            pos = end + 1;
        }
    }

    ApplyPractice();
}

// ---------------------------------------------------------------- death summary
void CaptureDeath()
{
    g_DeathInBoss = GameBoss::IsActive();
    g_DeathBossPhase = GameBoss::GetPhase();
    g_DeathWave = GameWave::GetCurrentWave();
    g_DeathSeconds = g_RunFrames / 60;
    g_GameOverLock = 40;
    LogEvent("death", g_DeathInBoss ? "boss" : "wave");
    EndRun(false, false);
}

bool ConsumeGameOverLock()
{
    if (g_GameOverLock > 0)
    {
        --g_GameOverLock;
        return true;
    }

    return false;
}
}









