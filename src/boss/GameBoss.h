#pragma once

#include "game/GameTypes.h"

enum class BossBulletHitResult
{
    None,
    Absorbed,
    Damaged
};

// Everything the HUD needs to draw the boss bar. GameBoss owns the data,
// GameHud only reads it, so the HUD never has to know about attack logic.
struct BossHudInfo
{
    bool visible = false;
    int phase = 1; // 1..3
    float segmentWeight[3] = {0.313f, 0.361f, 0.326f};
    float segmentRate[3] = {1.0f, 1.0f, 1.0f}; // remaining HP of each phase segment, 0..1
    float shipRate[2] = {0.0f, 0.0f};          // phase 2 and 3: the life of the drones (0: the colour drone, 1: the white drone)
    bool shipAlive[2] = {false, false};
    float shipRespawn[2] = {0.0f, 0.0f};       // ... a drone that is down: 0..1, how far it is from coming back
    bool shellClosed = false; // phase 3: the shell of the core is closed (shoot the drones)
    bool overheated = false; // the boss is staggered (a broken overload): it does not resist a shot
    bool overload = false;   // the overload charges: a piercing beam on the weak point breaks it
    float overloadRate = 0.0f; // 0..1: how far the charge has come
    bool overloadHint = false; // the player has broken fewer than three overloads in all: the HUD says what to do
    bool berserk = false;
    bool transition = false;
    int bannerPhase = 0; // > 0 while the phase banner is visible
    float bannerAlpha = 0.0f;
    int breaks = 0;          // overloads broken in this fight (the HUD plays the BREAK cut-in when it goes up)
};

namespace GameBoss
{
void Initialize();
void Start();
void Update(const Player& player);
void Draw();

bool CheckPlayerBullets(Bullet bullets[], int bulletCount);
bool CheckSpecialShotHit(float shotStartX, float shotEndX, float shotCenterY, float shotHalfHeight, int damage, float scale = 1.0f); // scale: the beam that is on (TWIN, OVERFLOW)
bool TakeDamage(int damage);
bool DamagePhaseShare(float share); // BACKUP CORE: takes this share of the life of the phase the boss is in (true: the boss is down)
// HP the ship loses for the hit that CheckPlayerCollision() has just reported as Damaged (Balance::DMG_BOSS_*).
int GetLastHitDamage();
// What hurt the ship in that hit: "boss_ram", "boss_polarity_storm", ... (the attack that was running; run_log.csv)
const char* GetLastHitSource();
BossBulletHitResult CheckPlayerCollision(const Player& player, bool playerIsInvincible);

bool IsActive();
bool IsWarning();
bool IsArriving(); // the WARNING and the entrance of the boss: the ship is held at its place (a short cut scene)
bool IsBattleStarted();
bool IsDefeated();
bool IsDefeatAnimationFinished();
bool IsBerserk();
bool IsTransitioning();

int GetPhase();
float GetProgress(); // 0..1: how much of the whole bar (phase 1 + 2 + 3, by their life) has been shot away; 1 once the boss is down
BossHudInfo GetHudInfo();

// One-shot requests that PlayScene polls every frame.
int ConsumeHitStop();                              // frames to freeze the game
bool ConsumeShake(int& duration, float& strength); // false when the queue is empty
int ConsumeAbsorbBonus();                           // homing bolts absorbed since the last call (each gives extra energy: Mod::FlushBossAbsorbBonus)
int ConsumeEnergyReward();                         // converted bullets that reached the player

// Called when the player dies so the game over screen does not show a frozen beam / telegraph.
void FreezeForResult();

// Support for the roguelite modules (rogue/).
int AbsorbSamePolarityInZone(float centerX, float centerY, float halfWidth, float halfHeight, Polarity polarity);
void ConvertBulletsToEnergy(); // boss bullets fly to the player and come back as energy
void ClearBullets();
bool GetTargetPosition(float& x, float& y); // weak point that module attacks aim at; false outside the battle

// Test hook for tools/balance_sim (the simulated player): a read-only view of the bullets and of the lanes of the white beams. The game does not call it.
struct SimBolt
{
    float x, y, vx, vy, halfWidth, halfHeight;
    bool red;
    bool white; // a needle of a drone: it hurts whatever the colour of the ship is
};
struct SimLane
{
    float y, halfHeight;
    int framesUntil; // 0 = the beam is on
    float chase;     // speed at which the lane follows the ship while it charges (0 = fixed)
    bool mirror;     // the lane follows the mirror image of the ship
    float minAbs;    // the lane never comes closer to the middle than this
    int delay = 0;   // frames after framesUntil before the lane can hurt (the lane of a dash is locked well before the rival reaches the ship)
};
struct SimPod
{
    float x, y; // the centre of the ring of the gunboat (the muzzle is a little to the left of it)
    bool red, alive;
};
void GetPodsForSim(SimPod out[2]);      // phase 2 and 3: where the drones are (red: drone 0 fires its red fan next; alive: it is up)
bool GetHullForSim(float& x, float& y); // the hull (the open strip of its body is centred on this point)
bool IsCoreExposedForSim();              // phase 3: the shell of the core is open (a shot on the core hurts)
int GetDroneRespawnForSim(int i);        // frames until drone i is back (0 while it is up)
int GetLaserStateForSim();               // the laser of drone 1: 0 off, 1 shown, 2 firing
int GetDroneVolleysForSim(int i);        // volleys that drone i has fired so far
int GetBoltsForSim(SimBolt* out, int capacity);
int GetLanesForSim(SimLane* out, int capacity);
int GetAttackForSim(); // the attack that is running (AtkId as a number), 0 = none
float GetLaneSlopeForSim(); // the slope (y per x) of the lane of the ram / the beam that is running (0 = a level one)
bool GetInterruptWindowForSim(int& framesLeft); // the overload charges: a piercing beam on the weak point breaks it (frames left to do it)
void GetBreakStatsForSim(int& overloads, int& breaks);
}




