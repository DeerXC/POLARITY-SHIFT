#pragma once
// POLARITY SHIFT: the numbers that decide how hard the game is. ONE file, used by the game (src/) and by the headless simulator
// (tools/balance_sim), so a number that is tuned with the simulator is the number the player gets. No includes, no game types: the enemy
// types are indices (0 scout, 1 sine, 2 heavy, 3 charger, 4 turret, 5 sniper, 6 rival, the order of EnemyType).
//
// Reading guide
//   HP            the ship has Balance::PLAYER_MAX_HP points, not lives. Every source of damage has its own number (below).
//   HP_UNIT       the hit points of an enemy are counted in tenths of a shot: one base shot of the ship takes HP_UNIT points away.
//                 That is what lets a module say "damage -35%" or "armour: 40%" without fractions.
//   energy        absorbing gives ABSORB_ENERGY * ChainScale(heat): the more you absorb in a short time, the less each one gives (the
//                 heat of the chain cools down with CHAIN_DECAY per step). Wide nets and bullet fields do not fill the bar in two seconds.
//   shield        an ELITE (and the rival) has a shield: a plain shot does ELITE_SHIELD of its damage. The piercing beam breaks the shield for good.
//   module level  level 1 = the module as written, level 2 raises its good numbers by LEVEL_GAIN (times the share that the module takes, LEVEL_K) and multiply its costs by LEVEL_COST.
namespace Balance
{
// ---- the ship
constexpr int WAVE_COUNT = 5; // waves before the boss: 1 absorb, 2 switch, 3 control, 4 pressure, 5 the rival
constexpr int BOSS_WAVE = WAVE_COUNT + 1; // the number that the run log and the records use for the boss
constexpr int PLAYER_MAX_HP = 120;
constexpr int CHALLENGE_MAX_HP_PENALTY = 20; // the top challenge level: max HP -20
constexpr int INVINCIBLE_FRAMES = 56;        // after a hit (0.93 s)
constexpr int HP_UNIT = 10;

// ---- the harder mode of the standard run (CHALLENGE 1 .. 3: faster bullets, more life, more elites, more shards) is switched off for now: the title screen offers the
// standard run and the boss practice. (The code and the saves stay as they are: true brings it back.)
constexpr bool CHALLENGES_ENABLED = false;

// ---- boss practice scores like a run: the boss pays by its bar (Mod::UpdateBossScore), a practice run that ends is on the records page with its score, and its score counts
// for the high score and the best combo. (false = practice never touches them)
constexpr bool PRACTICE_SCORES = true;

// ---- boss practice: false = it is on the title screen from the first start (START GAME, LEFT / RIGHT); true = it waits until the player has met the boss once.
// (It waited, and a save that was lost - a deleted build folder, a new machine - took the entry away with it. The practice is the quickest way to show the boss.)
constexpr bool PRACTICE_NEEDS_BOSS = false;

// ---- score of the boss: it is paid while the boss bar goes down, so a fight that is lost is not worth 0
constexpr int BOSS_SCORE_DAMAGE = 4000; // spread over the whole bar of the boss (phase 1 + 2 + 3, by their life): every share that is shot away pays at once
constexpr int BOSS_SCORE_CLEAR = 1000;  // the last blow (4000 + 1000 = the 5000 that the kill alone used to give: the ranks of the clear screen stay where they were)
constexpr int BOSS_SCORE_STEP = 10;     // paid in steps of this many points (the score on the HUD does not twitch with every shot)

// ---- how far the mission has come (the defeat screen says it as a percentage)
constexpr float MISSION_WAVES_SHARE = 0.60f; // the five waves are this much of the mission, the boss is the rest
// wave 1..WAVE_COUNT and segment 0..3 of GameWave (3 = everything of the wave has come); bossReached: the waves are done; boss 0..1 = how much of the boss bar is gone
inline float MissionProgress(int wave, int segment, bool bossReached, float boss)
{
    const int w = wave < 1 ? 1 : (wave > WAVE_COUNT ? WAVE_COUNT : wave);
    const int s = segment < 0 ? 0 : (segment > 3 ? 3 : segment);
    float waves = static_cast<float>(3 * (w - 1) + s) / static_cast<float>(3 * WAVE_COUNT);
    if (bossReached) waves = 1.0f;
    const float b = boss < 0.0f ? 0.0f : (boss > 1.0f ? 1.0f : boss);
    return MISSION_WAVES_SHARE * waves + (1.0f - MISSION_WAVES_SHARE) * b;
}

// ---- damage that the ship takes (HP points)
constexpr int DMG_BULLET[7] = {4, 5, 8, 7, 8, 13, 9}; // a bullet of: scout, sine, heavy, charger, turret, sniper, rival
constexpr int DMG_RAM = 16;       // an enemy flies into the ship
constexpr int DMG_BOSS_BOLT = 8;
constexpr int DMG_BOSS_BEAM = 18; // the white beams of the boss
constexpr int DMG_BOSS_BODY = 16; // touching the hull
constexpr float BOSS_P3_BOLT_MUL = 1.25f; // phase 3 of the boss: its bullets hurt this much more ...
constexpr float BOSS_P3_HIT_MUL = 1.20f;  // ... its beams and its hull this much more
constexpr float BOSS_P3_SPEED_MUL = 1.08f;
constexpr float OVERLOAD_BEAM_HP_RATE = 0.30f; // an overload beam of the boss that goes off takes this share of the maximum HP ...

// ---- hit points of the enemies (in shots; multiplied by HP_UNIT in the game): scout, sine, heavy, charger, turret, sniper, rival
constexpr int ENEMY_HP[7] = {1, 2, 16, 2, 22, 14, 80};
constexpr float ELITE_SHIELD = 0.75f; // a plain shot on the shield of an elite / a rival does this much (the piercing beam breaks the shield)
// ---- the elite templates (turret, sniper, heavy, charger): their life in shots (instead of ENEMY_HP), and how much faster they attack / move
constexpr int ELITE_HP_TURRET = 50;
constexpr int ELITE_HP_SNIPER = 28;
constexpr int ELITE_HP_HEAVY = 44;
constexpr int ELITE_HP_CHARGER = 12;
constexpr float ELITE_SIZE = 1.20f;   // an elite is this much bigger
constexpr int DMG_ELITE_LASER = 18;   // the rail of the elite sniper

// ---- energy
constexpr int ENERGY_MAX = 100;
constexpr float ABSORB_ENERGY = 6.0f;      // per absorbed bullet, before the chain and the modules (a full bar = 17 bullets)
constexpr float ABSORB_ENERGY_BOSS = 3.5f; // per absorbed boss bullet (the boss throws more of them)
constexpr float CHAIN_ADD = 1.0f;          // heat of the chain per absorbed bullet
constexpr float CHAIN_DECAY = 0.975f;      // per step: half of the heat is gone after 0.46 s (it cools faster than it did: 0.985)
constexpr float CHAIN_SLOPE = 0.040f;
constexpr float CHAIN_FLOOR = 0.55f;       // a net that never closes still gives this much per bullet
constexpr float ABSORB_MUL_MINE = 3.0f;    // a mine of the heavy gives this many times the energy of a plain bullet ...
constexpr float ABSORB_MUL_HOMING = 2.0f;  // ... a bullet that follows the ship this many times (rings and fans: the plain value)

inline float ChainScale(float heat)
{
    const float s = 1.0f - CHAIN_SLOPE * heat;
    return s < CHAIN_FLOOR ? CHAIN_FLOOR : s;
}

// ---- the modules (level 1; an upgrade multiplies the good number by GainAt, see ModulesData.cpp)
// absorb
constexpr float SIPHON_RANGE = 1.90f; // (was 1.60 + a pull: the pull is gone, the net is bigger)             // SIPHON: the absorb box (the area of the net grows with the SQUARE of this)
constexpr int ECHO_EVERY = 4;                     // ECHO: an arc every this many absorbs ...
constexpr int ECHO_DAMAGE_FIRST = 2;              // ... that hits the nearest enemy for this many base shots ...
constexpr float ECHO_JUMP_FALLOFF[3] = {0.70f, 0.50f, 0.35f}; // ... and jumps to the next ones for this much of it
constexpr float ECHO_JUMP_RANGE = 0.70f;          // ... when they are this close to the last one
constexpr int OVERFLOW_STORE_MAX = 12;            // OVERFLOW: a full bar stores this many absorbed bullets at most ...
constexpr float OVERFLOW_BOOST_PER_BULLET = 0.04f; // ... each makes the next full beam this much longer and harder
constexpr float MOMENTUM_STEP = 0.08f;            // MOMENTUM: fire rate per stack ...
constexpr int MOMENTUM_MAX_STACKS = 8;            // ... up to this many stacks ...
constexpr int MOMENTUM_HOLD_FRAMES = 240;         // ... they start to fade after this long without an absorb
constexpr float MOMENTUM_ENERGY_PENALTY = 0.15f;
constexpr float DEFLECT_CHANCE = 0.85f;           // DEFLECT: a white bullet that grazes the ship is absorbed with this chance ...
constexpr float DEFLECT_ENERGY_MUL = 3.0f;        // ... and a bullet that was absorbed that way gives this many times the energy
// break
constexpr float BOSS_RESIST = 0.25f;              // the boss takes this much less in phase 1 and 2 (not while it is staggered, never in phase 3) ...
constexpr float BREACH_LEVEL2_BONUS = 0.20f;      // BREACH ignores every damage reduction (the shields of the elites, the resistance of the boss); level 2: +20 % on elites and the boss
constexpr float TWIN_DAMAGE = 0.75f;              // TWIN: a half beam lasts half as long and hits this hard (a tick of it) ...
constexpr int TWIN_COST_PERCENT = 50;             // ... and costs this much of the bar
constexpr int AFTERGLOW_FRAMES = 240;             // AFTERGLOW: after the beam, for this long ...
constexpr float AFTERGLOW_BONUS = 0.40f;          // ... the gun fires this much faster and each shot does this much more
constexpr float FOCUS_STEP = 0.04f;               // FOCUS: every hit on the same target in a row adds this much damage ...
constexpr int FOCUS_MAX_STACKS = 10;              // ... up to this many times ...
constexpr int FOCUS_IDLE_FRAMES = 60;             // ... and it is gone after this long without a hit
// survive
constexpr float BACKUP_REVIVE_RATE = 0.50f;       // BACKUP CORE: the revive gives this much of the maximum HP ...
constexpr float BACKUP_BLAST_HITS = 7.2f;         // ... and the blast hits every enemy for this many base shots (60 % of a full beam) ...
constexpr float BACKUP_BOSS_RATE = 0.06f;         // ... and the boss for this much of the life of the phase it is in
constexpr int PULSE_PERIOD = 360;                 // PULSE: a shockwave every this many steps ...
constexpr int PULSE_HIT_CUT = 180;                // ... a hit brings the next one this much sooner ...
constexpr float PULSE_BODY_CUT = 0.25f;           // ... and a body that flies into the ship does this much less
constexpr int AEGIS_ABSORBS = 40;                 // AEGIS: the shield takes one white bullet or beam and comes back after this many absorbed bullets
constexpr int AEGIS_MAXHP_PERCENT = 10;           // the price: maximum HP
constexpr int MEND_DELAY = 300;                   // MEND: after this long without a hit ...
constexpr float MEND_PER_SECOND = 0.6667f; // (1 HP every 1.5 s)
//           // ... it repairs this much every second
constexpr float BACKWATER_INVINCIBLE = 0.25f;     // BACKWATER: under half of the HP the invincibility after a hit is this much longer ...
constexpr int BACKWATER_ENERGY_CUT = 15;          // ... the beam needs this much less energy ...
constexpr float BACKWATER_ABSORB_BONUS = 0.20f;   // ... and an absorbed bullet gives this much more
// resonance: two / three modules of one type
constexpr float RES_ABSORB_RANGE = 0.10f;         // absorb 2: the absorb range
constexpr int RES_ABSORB_ARC_EVERY = 20;          // absorb 3: every this many absorbs an arc (ECHO's arc)
constexpr int RES_BREAK_ENERGY_CUT = 10;          // break 2: the beam needs this much less energy
constexpr float RES_BREAK_BLAST_HITS = 3.0f;      // break 3: when the beam ends it bursts along its line for this many base shots
constexpr int RES_SURVIVE_HP = 10;                // survive 2: the maximum HP
constexpr int RES_SURVIVE_PHASE_HEAL = 10;        // survive 3: HP at the start of every phase of the boss
// ---- tactics (the TACTIC key)
constexpr int DECOY_FRAMES = 180;                 // DECOY: it stays this long ...
constexpr int DECOY_COOLDOWN = 540;               // ... and can be dropped again after this long
constexpr float DECOY_PULL_RADIUS = 0.45f;        // ... bullets of its colour this close are pulled to it
constexpr float DECOY_ENERGY = 0.50f;             // ... and give this much of their energy when it absorbs them
constexpr int DASH_FRAMES = 10;                   // DASH: the dash takes this long ...
constexpr float DASH_DISTANCE = 0.40f;            // ... and goes this far (horizontal units; up / down in the same screen distance)
constexpr int DASH_INVINCIBLE = 14;               // ... the ship cannot be hurt for this long
constexpr int DASH_COOLDOWN = 150;
constexpr int CHARGE_FULL = 120;                  // CHARGE: frames to a full charge ...
constexpr int CHARGE_MIN = 20;                    // ... a shorter one fires nothing
constexpr int CHARGE_COOLDOWN = 60;
constexpr float CHARGE_MOVE = 0.70f;              // ... the ship moves this fast while it charges
constexpr float CHARGE_HITS_MIN = 2.0f;           // ... the beam hits for this many base shots (a short charge) ...
constexpr float CHARGE_HITS_MAX = 8.0f;           // ... up to this many (a full one)
constexpr int REROLLS_PER_RUN = 1;                // the pick screen can be dealt again this many times per run (R); the talent VETERAN adds to it

// ---- module levels (index = level, 1..3)
// An upgrade has to be worth a card. Pass 16 gave +30% of the good numbers and -10% of the costs: next to a NEW module (a whole new effect, resonance with the modules of its type, a chance
// at a synergy) that was a poor pick. The first step is the big one now (the way the first levels of a Hades boon are: +60 % of its base, then less and less); a module takes only
// LEVEL_K of it (ModulesData.cpp: a big lever such as the absorb range moves less than a plain timer), and the card shows the real numbers before and after.
constexpr float LEVEL_GAIN[3] = {1.0f, 1.0f, 1.90f};
constexpr float LEVEL_COST[3] = {1.0f, 1.0f, 0.0f};  // level 2 has no drawback at all
constexpr int LEVEL_CAP = 2;          // a module can be upgraded once

// ---- the route nodes
constexpr int BOSS_PHASE_HEAL = 20;    // the boss changes phase: the ship is repaired this much (a breather before the next phase)
constexpr float MERCY_MAX = 0.25f;     // mercy: with no HP left a hit takes 25% less, with full HP nothing less (see Mod::ScaleDamageTaken)
// every cleared wave repairs the ship by how much of it was shot down (the ships that were killed / the ships that came)
constexpr int CLEAR_HEAL_FULL = 20; // all of them
constexpr int CLEAR_HEAL_MOST = 14; // 80 % or more
constexpr int CLEAR_HEAL_HALF = 6;  // 50 % or more (less: nothing)
// ---- a kill drops energy (motes that fly to the ship) ...
// (half of the first numbers 4 / 6 / 10 / 25: those made the kills give as much energy as absorbing, the core of the game)
constexpr int KILL_ENERGY_SMALL = 2; // scout
constexpr int KILL_ENERGY_MID = 3;   // sine, charger
constexpr int KILL_ENERGY_BIG = 5;   // heavy, turret, sniper
constexpr int KILL_ENERGY_ELITE = 12; // an elite, a rival
// ---- ... more with the STREAK: the damage the ship has dealt since it was last hurt (in shots). A hit drops it to the start of the tier below.
constexpr int STREAK_TIER1 = 40;
constexpr int STREAK_TIER2 = 120;
constexpr float STREAK_ENERGY[3] = {1.0f, 1.25f, 1.5f}; // what a kill drops, per tier
constexpr float STREAK_DAMAGE_TIER2 = 0.10f;           // tier 2: plain shots hit this much harder
constexpr int HEAL_SUPPLY = 30;       // supply run (after wave 2): HP after wave 3
constexpr int WAGER_COST_PERCENT = 30; // wager (after wave 3): this much of the maximum HP for a module AND an upgrade

// ---- hangar (shards)
}




