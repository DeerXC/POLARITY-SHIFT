#pragma once

#include "game/GameTypes.h"

// Roguelite layer of the game: 15 modules the player picks after each wave, hangar unlocks, challenge levels,
// wave routes and the run log. PlayScene and Combat call these hooks; everything behind them is private to rogue/.
namespace Mod
{
void Reset();
void ApplyDebug();
void Tick();
bool ChoiceOpen();
void UpdateChoice();
void UpdateUi();   // every PlayScene update: slow-motion timers and the energy motes (not affected by the slow-motion)
bool StepWorld();  // false while the world is slowed down and this update step is skipped
void DrawChoice();
void DrawChips();
void DrawWorld();
void AfterPlayerUpdate();
void OnAbsorb(float baseEnergy); // one absorbed bullet: energy through the chain (Balance::ChainScale) and the modules
void NoteHit();                 // the ship was hurt: the streak drops a tier
void NoteDamage(float shots);   // the ship dealt this much damage (in shots): the streak grows
void OnEnemyKilled(const Enemy& enemy); // a kill: energy motes (more with the streak), the clear rate of the wave
int StreakTier();               // 0 / 1 / 2
float StreakProgress();         // 0..1: how far the streak is from its next tier (1 at the top)
int LastClearHeal();            // the HP that the last cleared wave repaired (0: none, or the ship was whole)
int LastClearPercent();         // the clear rate of the wave that was cleared last (the "CLEARED 85%" line)
bool TryBackupCore();
float SiphonScale();            // the absorb box of the ship (1 = plain): SIPHON, the resonance of absorb modules, the talent DUAL
int EnergyMax();                // what the piercing beam needs
int MaxHp();                    // the maximum HP of this run (the challenge level lowers it)
int OverflowStored();           // OVERFLOW: how many absorbed bullets wait for the next beam (the HUD shows them)
int OverflowStoredMax();
float ChainRate();              // 0..1: how much of its energy an absorbed bullet gives right now (the HUD shows it)
float DamageMul();              // what a shot of the ship does (1 = plain; AFTERGLOW raises it)
int PlayerShotDamage(float armorMul); // HP points of an enemy that one shot takes away (Balance::HP_UNIT per base shot)
float OnShotHit(int target);    // FOCUS: a shot hits a target (an enemy slot, 1000 + a part of the boss): what it does (1 = plain)
bool FocusLocked();
bool BackwaterActive();         // BACKWATER is on (under half of the HP): the screen edges glow (PostFx danger)             // synergy WEAKPOINT LOCK: the armour and the resistance of the target are ignored
float SpecialDamageMul();       // what a tick of the beam that is on does (TWIN, OVERFLOW; 1 = plain)
int SpecialTickDamage();        // ... one tick of the piercing beam
bool BeginBeam(int& energy);    // the beam key: a full beam, or a half beam with TWIN. Takes the energy; false = nothing to fire
float BeamDurationScale();      // the length of the beam that BeginBeam started (TWIN, OVERFLOW, the talent LONG PIERCE)
int ScaleDamageTaken(int amount, const char* source = ""); // source: "ram" / "boss_body_..." are bodies (PULSE), "white_..." / "boss_beam_..." what AEGIS stops
int InvincibleFrames();                                    // the invincibility after a hit (the talent BUFFER and BACKWATER make it longer)
float ShotDurationScale();                                 // the talent LONG PIERCE: how much longer the piercing beam lasts (1 = plain)
void OnPhaseStart(bool bossPhase);                         // a wave or a phase of the boss begins: the talent CHARGE fills the bar a little
float DeflectChance();                                     // the chance that a white bullet that grazes the ship is absorbed (0 without DEFLECT)
float DeflectEnergyMul();                                  // ... and what such a bullet gives (times a plain bullet; more at level 2)
bool TryDeflect();                                         // DEFLECT: a white bullet grazes the ship: is it absorbed? (a roll; false without the module)
void FlushBossAbsorbBonus();                               // gives the bonus for the homing bolts of the boss that were absorbed since the last call
void OnAbsorbBonus(float baseEnergy);                      // extra energy for a special bullet (a homing boss bolt): through the chain and the modules, but it is not another absorb
float MoveSpeedScale();                                    // the tactic CHARGE slows the ship while it charges (1 = plain)
float BreachBonus();            // BREACH level 2: what a hit on an elite / a rival / the boss does more (1 = plain)
float ShieldFactor();           // what a plain shot does to an enemy that still has its shield (Balance::ELITE_SHIELD; BREACH: all of it)
float BossResistFactor();       // what the boss lets through of a shot in phase 1 and 2 (BREACH: more)
void HealPlayer(int amount);      // repairs the ship (up to the maximum) with a ring of light and a sound
int RerollsLeft();              // unused rerolls of this run (R on the pick screen)
float AegisCharge();            // -1 = no shield module, 0..1 = how far the shield has recharged (1 = the shield is up)
void UpdateZones();
void OnSpecialActivated();
void UpdateSpecial();
void UpdateWaveEvents();
void LogEvent(const char* event, const char* note);
void CaptureDeath();
void DrawRunResult();
void DrawGameOverSummary(float reveal); // reveal 0..1: slides in from the left
void DrawTitleHint();
bool DrawTitleOverlay();
bool UpdateTitleOverlay();
void OpenHangarFromTitle();            // the HANGAR row of the title menu
// Right end of the HANGAR row: shard balance and the NEW tag. (rowLeft, centerY, rowWidth in virtual 1280x720 pixels)
void DrawHangarRowInfo(float rowLeft, float centerY, float rowWidth, bool lit, float alpha);
// The mouse is over the mode selector of START GAME (or the gap between it and the row): the row keeps its highlight then.
bool IsMouseOnModeSelector();

// Game over screen: a short lock after dying so that a mashed key does not skip the result.
bool ConsumeGameOverLock();
}


