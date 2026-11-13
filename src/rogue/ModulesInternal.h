#pragma once

#include "game/GameState.h"
#include "game/GameTypes.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/ModuleDefs.h"
#include "rogue/Modules.h"

#include <string>

// Shared by the rogue/*.cpp files only: tuning values, the state of the module system and its internal functions.
namespace Mod
{
// ---------------------------------------------------------------- tuning (starting values)
constexpr float SIPHON_RANGE = Balance::SIPHON_RANGE; // absorb box scale for same-colour bullets (the area grows with the square of it)
constexpr float DECOY_HALF_W = 0.10f; // the box in which the decoy absorbs
constexpr float DECOY_HALF_H = 0.178f;
constexpr float DECOY_PULL_STEP = 0.0060f;
constexpr float PULSE_RADIUS = 0.32f;
constexpr float PULSE_RADIUS_GRAVITY = 0.56f;
constexpr int PULSE_REFUND_CAP = 22;
constexpr int EMBER_FRAMES = 120;
constexpr int EMBER_TICK = 15;
constexpr int BACKUP_INVINCIBLE_FRAMES = 180;
constexpr int MEND_START_FRAMES = 30; // the ring that marks the start of a repair
constexpr int MEND_TICK_FRAMES = 16;  // the small flash of every repaired point
constexpr int REVIVE_BURST_AT = 17;   // the step of the blast at which the rings are born (the hit stop of the revive is 18 steps long)
constexpr int REVIVE_FRAMES = 72;     // the blast of the backup core: 17 steps of light that gathers (the hit stop), then the shock rings
constexpr float CHARGE_HALF_MIN = 0.025f; // the beam of the tactic CHARGE: a short charge is this high ...
constexpr float CHARGE_HALF_PART = 0.020f; // ... a longer one up to this much more (still lower than GameBoss BREAK_MIN_HALF_H) ...
constexpr float CHARGE_HALF_FULL = 0.060f; // ... a full one is this high: high enough to break an overload of the boss
constexpr int CHOICE_INPUT_LOCK = 30;
constexpr int CHOICE_OPEN_AT_TIMER = 14; // wave-clear timer value at which the pick screen opens (the WAVE CLEAR card shows ~1.25 s, then docks as the pick's header)
constexpr float ASPECT_Y = 720.0f / 1280.0f;

// ---- tuning: challenge levels (per level) and the wave route
constexpr float LEVEL_BULLET_STEP = 0.08f; // enemy bullets +8 % speed per level
constexpr float LEVEL_HP_STEP = 0.15f;     // enemies with 2+ hp get +15 % hp per level
constexpr int ELITE_FIRST_FRAME = 360;     // first extra elite arrives this many frames into the wave
constexpr int ELITE_GAP_FRAMES = 420;

constexpr int ELITE_EARLY_MIN_FRAME = 150; // an empty field brings the next elite forward, but not before this
constexpr int ROUTE_WAVE = 3;              // the route chosen after wave 2 applies to this wave

// pick / route cards (virtual 1280x720 pixels, shared by update and draw)
constexpr float CARD_Y = 196.0f;
constexpr float CARD_H = 410.0f;
// the reroll button under the cards of the pick screen (virtual pixels, shared by update and draw): centred, 16 px below the cards
constexpr float REROLL_W = 264.0f;
constexpr float REROLL_H = 42.0f;
constexpr float REROLL_Y = 622.0f;
constexpr int SLOW_FRAMES = 24;   // the world slows down over this many frames when a pick screen opens
constexpr int RESUME_FRAMES = 20; // ... and speeds up again over this many frames after the pick
// ---------------------------------------------------------------- hangar (opened with H on the title screen)
constexpr int HANGAR_ROWS = 5;
constexpr int HANGAR_COLS = 5;
// hangar ids: 0 .. COUNT-1 the modules (free: shown for what they do), then the TACTICS (HANGAR_TACTIC + tactic) and the TALENTS (HANGAR_TALENT + talent); -1 = empty cell
constexpr int HANGAR_TACTIC = TACTIC_ID0;
constexpr int HANGAR_TALENT = TALENT_ID0;
constexpr int HANGAR_LAYOUT[HANGAR_ROWS][HANGAR_COLS] = {
    {0, 1, 2, 3, 4},                                                       // absorb
    {5, 6, 7, 8, 9},                                                       // break
    {10, 11, 12, 13, 14},                                                  // survive
    {HANGAR_TACTIC, HANGAR_TACTIC + 1, HANGAR_TACTIC + 2, -1, -1},         // tactics
    {HANGAR_TALENT, HANGAR_TALENT + 1, HANGAR_TALENT + 2, HANGAR_TALENT + 3, HANGAR_TALENT + 4} // talents
};
constexpr float HANGAR_TILE = 60.0f;
constexpr float HANGAR_GAP = 12.0f; // (wide enough for the offset plates of the selected tile: they stay in the gap)
constexpr float HANGAR_X = 222.0f; // pixels in the virtual 1280x720 layout
constexpr float HANGAR_Y = 150.0f;
// title screen hint rectangle (normalized coordinates) - drawn by DrawTitleHint
constexpr float HINT_LEFT = -0.9625f;
constexpr float HINT_RIGHT = -0.5200f;
constexpr float HINT_TOP = -0.7200f;
constexpr float HINT_BOTTOM = -0.8100f;
// mode selector (title screen, right of the START GAME row): virtual 1280x720 pixels
constexpr float MODE_X = 486.0f;
constexpr float MODE_W = 230.0f;
constexpr float MODE_CY = 332.0f;
constexpr float MODE_H = 36.0f;

struct Zone // the decoy of the tactic DECOY
{
    float x, y;
    Polarity polarity;
    int timer;
    bool active;
};
struct EchoArc
{
    float x1, y1, x2, y2;
    Polarity polarity;
    int timer;
};
// an enemy bullet that was turned into energy at the end of a wave: it flies to the ship
struct Mote
{
    float x, y, vx, vy, speed;
    int delay;
    Polarity polarity;
    bool active;
    int value; // energy it gives (0 = 1)
};
constexpr int MOTE_MAX = 64;

// the cards of a route screen (after wave 2: elite fleet / supply run, after wave 3: new module / forge / wager)
enum RouteKind
{
    ROUTE_ELITE,
    ROUTE_SUPPLY,
    ROUTE_MODULE,
    ROUTE_FORGE,
    ROUTE_WAGER
};
constexpr int ROUTE2_WAVE = 3; // the second route screen opens after this wave
int WagerCost();               // HP that the wager costs
bool RouteLocked(int kind);

// ---------------------------------------------------------------- state (defined in Modules.cpp)
extern bool g_Owned[COUNT];
extern int g_Order[COUNT];
extern int g_OrderCount;
extern bool g_BackupUsed;
extern bool g_ChoiceOpen;
extern int g_Choice[3];
extern int g_ChoiceCount;
extern int g_ChoiceSel;
extern float g_CardAnim[3];
extern float g_BranchAnim[3];
extern float g_RerollAnim;      // 0..1: how much the reroll button is highlighted (smoothed)
extern bool g_RerollFocus;      // keyboard / pad: the highlight is on the reroll button (DOWN from the cards)
extern int g_RerollShake;       // frames left of the shake of the button (pressed with no reroll left)
extern int g_Level[COUNT];       // 0 = not owned in this run, 1..2 = the level of the module
extern bool g_ChoiceUpgrade[3];  // the card of the pick screen is an upgrade of a module that the ship owns
extern bool g_ForgeNext;         // the next pick screen offers upgrades only
extern bool g_WagerForge;        // the wager gives a module AND an upgrade: after the first pick the second one is a forge
extern int g_RouteStage;         // 1 = after wave 2, 2 = after wave 3
extern int g_RouteKind[3];
extern int g_RouteCount;
extern bool g_Branch2Done;
extern float g_ChainHeat;
extern float g_EnergyFrac;
extern int g_EmberLife;
extern bool g_StartPickDone; // the module pick at the start of the run has been offered
extern int g_RerollsLeft;       // rerolls of this run that are still unused (Balance::REROLLS_PER_RUN at the start)
extern int g_PrevChoice[3];     // the cards that were on offer before a reroll: they are not dealt again
extern int g_PrevChoiceCount;
extern bool g_Rerolling;
extern int g_AegisTimer;        // absorbed bullets until the AEGIS shield is back (0 = the shield is up)
extern int g_AegisFlash;        // frames left of the flash when the shield took a hit
extern float g_Momentum;        // MOMENTUM stacks
extern int g_MomentumIdle;      // frames since the last absorb
extern float g_RateAcc;         // the fire rate bonuses (MOMENTUM, AFTERGLOW, DUAL) take frames off the cool time of the gun through this
extern int g_SinceHurt;         // frames since the ship was last hurt (MEND)
extern float g_Streak;          // the damage dealt since the ship was last hurt (shots); GameState::combo shows it
extern int g_WaveSpawned;       // ships that came in this wave ...
extern int g_WaveKilled;        // ... and that were shot down
extern int g_LastClearPercent;
extern int g_LastClearHeal; // the HP that the repair of the last cleared wave gave
extern float g_StatKillEnergy;   // (the simulator: energy that kills dropped / that absorbing gave, this run)
extern float g_StatAbsorbEnergy;
extern float g_MendAcc;
extern int g_OverflowStore;     // OVERFLOW: absorbed bullets stored for the next beam
extern float g_BeamDamageScale; // the beam that is on: what a tick of it does (TWIN, OVERFLOW)
extern bool g_BeamIsHalf;       // ... it is a half beam (TWIN)
extern int g_AfterglowTimer;    // AFTERGLOW: frames left
extern int g_FocusTarget;       // FOCUS: the target that is being hit (an enemy slot, 1000 + the part of the boss), -1 = none
extern int g_FocusStacks;
extern int g_FocusIdle;
extern int g_PulseCooldown;     // PULSE: frames to the next shockwave
extern int g_ResArcCounter;     // resonance of three absorb modules: absorbs since the last arc
extern int g_Tactic;            // the tactic of this run (Tactic as a number), -1 = none
extern int g_TacticCooldown;
extern int g_DashTimer;         // DASH: frames left of the dash ...
extern float g_DashX, g_DashY;  // ... and the step of it
extern int g_ChargeFrames;      // CHARGE: frames charged (0 = not charging)
extern int g_TacticFlash;       // the beam of the charge, drawn for a moment
extern float g_TacticBeamX, g_TacticBeamY, g_TacticBeamHalf;
extern bool g_PrevTacticDown;
extern int g_SlowT;
extern Mote g_Motes[MOTE_MAX];
extern int g_ChoiceTimer;
extern bool g_ChoiceDone;
extern bool g_PrevStarting;
extern bool g_PrevCleared;
extern int g_PicksLeft;
extern bool g_RareNext;
extern int g_EchoCounter;
extern int g_IdleAbsorbFrames;
extern int g_SoundCooldown;
extern Zone g_Decoy;
extern Polarity g_PrevPolarity; // the colour of the ship a step ago (the charge of the tactic CHARGE is halved by a switch)
extern EchoArc g_Arcs[6];
extern int g_PulseTimer;
extern float g_PulseX, g_PulseY, g_PulseRadius;
extern bool g_PrevSpecialActive;
extern float g_BeamStartX, g_BeamEndX, g_BeamY, g_BeamHalf;
extern int g_MendStartTimer; // MEND: frames left of the ring that opens when a repair begins
extern int g_MendTickFlash;  // ... of the flash of a repaired point
extern bool g_MendWasOn;     // it was repairing a step ago (the start is the rising edge)
extern int g_ReviveTimer;    // BACKUP CORE: frames left of the blast (runs in UpdateUi, so the light gathers during the hit stop too)
extern float g_ReviveX, g_ReviveY;
extern int g_EmberTimer;
extern int g_EmberTick;
extern float g_EmberStartX, g_EmberEndX, g_EmberY, g_EmberHalf;
extern long long g_RunId;
extern int g_RunFrames;
extern int g_WaveStartFrame;
extern int g_RunAbsorbs;
extern int g_RunHits;
extern int g_WavesCleared;
extern bool g_ReachedBoss;
extern int g_MaxBossPhase;
extern int g_DeathWave;
extern bool g_DeathInBoss;
extern int g_DeathBossPhase;
extern int g_DeathSeconds;
extern int g_EndMission;      // the end of the run in percent: how far the mission came (waves + boss) ...
extern int g_EndBoss;         // ... how much of the boss bar is gone ...
extern int g_EndBestMission;  // ... and the best of both from BEFORE this run (0 = none yet)
extern int g_EndBestBoss;
extern int g_BossScorePaid;   // points of BOSS_SCORE_DAMAGE paid so far
extern bool g_BossClearPaid;
extern int g_GameOverLock;
extern bool g_ResultAwarded;
extern bool g_HasResult;
extern Meta::RunResult g_Result;
extern int g_ToastTimer;
extern int g_ToastCombo;
extern int g_RunLevel;
extern int g_PracticePhase;
extern int g_Branch;
extern bool g_BranchOpen;
extern bool g_BranchDone;
extern int g_BranchSel;
extern int g_BranchTimer;
extern int g_BranchMessage;
extern int g_PlayFrames;
extern int g_ElitesSpawned;
extern bool g_PrevEnemyActive[GameState::ENEMY_MAX];
extern bool g_PrevBulletActive[GameState::ENEMY_BULLET_MAX];
extern bool g_HangarOpen;
extern int g_HangarSel;
extern int g_HangarSyn;       // the synergy that is highlighted in the hangar (an index into Meta::COMBOS), -1 = none. It excludes a highlighted tile.
extern bool g_HangarSynFocus; // the keyboard cursor is in the list of synergies (TAB)
extern int g_HangarTimer;
extern int g_HangarMessage;
extern int g_HangarFlash;
extern int g_HangarFlashId;

// ---------------------------------------------------------------- internal functions
bool Has(Id id);
int Level(Id id);
float Gain(Id id);  // the good numbers of a module at its level (1.0, 1.3, 1.6)
float CostK(Id id); // the costs of a module at its level (1.0, 0.9, 0.8)
int LevelCap(int id);
bool CanUpgrade(int id);
bool AnyUpgradable();
void UpgradeModule(Id id);
int VerbCount(Verb verb);
bool Resonant(Verb verb, int count = 2); // the ship owns at least this many modules of this type
bool LowHp();                            // under half of the maximum HP (BACKWATER)
bool MendActive();                       // MEND is repairing the ship right now (the delay is over and the ship is not whole)
float MendProgress();                    // 0..1: how far the next repaired point is
float FireRateBonus();                   // MOMENTUM, AFTERGLOW, the talent DUAL: how much faster the gun fires this step (0.4 = 40 %)
int PulsePeriod();                       // PULSE: frames between two shockwaves
void UpdateTactic();                     // the TACTIC key: decoy / dash / charge
void FireChargeBeam(float charge);       // the beam of the tactic CHARGE (charge 0..1)
bool ComboOn(int combo);
bool SupplyAvailable();
std::string ModuleCodes(char separator);
void WriteLogRow(const char* event, const char* note);
void EndRun(bool cleared, bool abandoned);
void Grant(Id id);
void ApplyPractice();
void GainEnergy(int amount);
float ScaleAbsorbEnergy(float base);
void FireEchoArc(float ox, float oy, float hits = -1.0f, bool jumps = true); // hits < 0: the damage of ECHO; jumps: ECHO jumps to more enemies
int AegisAbsorbsNeeded();
bool MouseOnReroll(); // the mouse is over the REROLL hint of the pick screen (a click on it rerolls)
void AbsorbBossBullets(int count, float x, float y, Polarity polarity);
void KillEnemy(Enemy& enemy);
void DamageEnemy(Enemy& enemy, float hits);
void DamageBand(float startX, float endX, float centerY, float halfHeight, float damage);
void FirePulse();
bool IsPlaying();
float BulletSpeedScale();
float EnemyHpScale();
int ElitesPlanned(int wave);
EnemyType EliteType(int wave, int index);
bool IsEliteType(EnemyType type);
int ActiveEnemyCount();
bool EliteOnField();
void DirectorScaleNew();
void DirectorSpawnElites();
void DirectorSnapshot();
void RollChoices();
void OpenChoice();
void OpenBranch(int stage);
void CardPixels(int index, int count, float& x, float& y, float& w, float& h); // final position of a card
int HoveredCard(int count);                                                    // card under the mouse or -1
void ResetPickUi();
void ConvertBulletsToMotes();
void DrawMotes();
void ConfirmChoice(int slot);
void ConfirmBranch(int slot);
void UpdateBranch();
void HangarTileXY(int row, int col, float& x, float& y);
bool HangarFind(int id, int& row, int& col);
void HangarMove(int dRow, int dCol);
void OpenHangar();
void UnlockSelected();
void UpdateModeSelector(bool mouseInside, float mx, float my);
}








