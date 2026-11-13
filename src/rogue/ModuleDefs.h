#pragma once

// The roguelite modules: ids, types and the static table. Texts in the table are Loc keys.
namespace Mod
{
// The number of a module is its cell in the icon atlas (textures/ui/module_icons.png) and its place in every table of ModulesData.cpp. (modules=0,4,8 in config.ini)
// Every module is free: the pick screen deals them from the start. The shards buy the TACTICS and the TALENTS (below).
enum class Id
{
    Siphon,     //  0 absorb : a much bigger absorb range
    Echo,       //  1 absorb : every 4th absorb fires an arc that jumps to two more enemies
    Overflow,   //  2 absorb : with a full bar, absorbed bullets are stored: the next piercing beam is longer and harder
    Momentum,   //  3 absorb : every absorbed bullet raises the fire rate for a while
    Deflect,    //  4 absorb : a white bullet that grazes the ship is absorbed with a chance, for three times the energy
    Ember,      //  5 break  : the piercing beam leaves a band of fire that burns enemies and turns bullets into energy
    Breach,     //  6 break  : shots of the colour of an armoured enemy are not halved any more, the boss resists less
    Twin,       //  7 break  : a half bar fires a half beam (it costs half a bar)
    Afterglow,  //  8 break  : after the piercing beam the gun fires faster and harder for a while
    Focus,      //  9 break  : hits on the same target in a row do more and more damage
    BackupCore, // 10 survive: one revive per run with half of the HP, and a blast over the whole screen
    Pulse,      // 11 survive: a shockwave every few seconds turns the bullets around the ship into energy; bodies hit softer
    Aegis,      // 12 survive: a shield against white bullets and beams, recharged by absorbing
    Mend,       // 13 survive: repairs the ship after five seconds without a hit
    Backwater,  // 14 survive: under half of the HP the ship is harder to kill and charges faster
    Count
};

enum class Verb
{
    Absorb,
    Break,
    Survive
};

constexpr int COUNT = static_cast<int>(Id::Count);

struct Info
{
    const char* name;  // Loc key (English text)
    const char* gain;  // "+" line
    const char* gain2; // second "+" line or nullptr
    const char* cost;  // "-" line or nullptr
    const char* code;  // used in run_log.csv and meta.sav
    Verb verb;
    int price; // hangar price in shards, 0 = available from the start
};

extern const Info INFO[COUNT];

// ---- tactics: bought with shards in the hangar; the ship takes ONE of them into a run (the hangar marks it). The TACTIC key (SHIFT, mouse side button 1, the left trigger) uses it.
constexpr int TACTIC_COUNT = 3;
enum class Tactic
{
    Decoy,  // drops a decoy that pulls and absorbs the bullets of the colour the ship has
    Dash,   // a short dash in the direction of the move, invincible while it lasts
    Charge  // hold to charge a beam, release to fire it; a full charge breaks an overload
};
struct TacticInfo
{
    const char* name; // Loc key
    const char* gain; // "+" lines
    const char* gain2;
    const char* code; // meta.sav
    int price;
    int icon; // cell of the icon atlas
};
extern const TacticInfo TACTICS[TACTIC_COUNT];

// ---- talents: bought with shards in the hangar, they stay for every run. Every talent has three levels; every level is a small step.
constexpr int TALENT_COUNT = 5;
constexpr int TALENT_LEVELS = 3;
constexpr int TALENT_PRICE[TALENT_LEVELS] = {80, 120, 180}; // what level 1 / 2 / 3 costs
enum class Talent
{
    Buffer,  // the invincibility after a hit lasts longer
    Pierce,  // the piercing beam (and the safe time that comes with it) lasts longer
    Charge,  // every wave and every phase of the boss starts with some energy
    Dual,    // red: the gun fires faster, blue: the absorb range is bigger
    Veteran  // one more reroll on the pick screen per level
};
struct TalentInfo
{
    const char* name; // Loc key
    const char* gain; // "+" line: the three levels ("+8% / +16% / +25%")
    const char* gain2;
    const char* code; // meta.sav
    int icon; // cell of the icon atlas
};
extern const TalentInfo TALENTS[TALENT_COUNT];
float TalentValue(Talent talent, int level);
// the ids of the hangar (and of Meta::IsUnlocked / Price / TryUnlock): the modules, then the tactics, then the talents
constexpr int TACTIC_ID0 = COUNT;
constexpr int TALENT_ID0 = COUNT + TACTIC_COUNT;
constexpr int HANGAR_ID_COUNT = TALENT_ID0 + TALENT_COUNT; // what a talent gives at a level (0 = not bought): a fraction (0.08 = 8 %) or a count (VETERAN: rerolls)
extern const char* const VERB_KEY[3]; // Loc keys of the three types

// ---- levels. A module that the ship owns can be upgraded (the pick screen, the forge, the wager) once: level 2.
// GainAt / CostAt are what the effects multiply their good numbers / costs by. A module takes only a share of the gain of a level (LEVEL_K in ModulesData.cpp).
float GainAt(Id id, int level);
float CostAt(Id id, int level);
int EchoTargetsAt(int level); // ECHO: how many enemies an arc hits at that level (the first one and the jumps)
// The one number that the upgrade card shows before and after ("ABSORB RANGE +45% -> +58%"). HEADLINE[id].label is a Loc key.
enum class StatFmt
{
    PlusPercent, // +45%
    Seconds,     // 2.0 S
    Count,       // 5
    Hp,          // 20 HP
    Decimal1,    // 0.6
    Decimal2,    // 0.25
    Percent      // 60%
};
struct Headline
{
    const char* label;
    StatFmt fmt;
    bool lowerIsBetter; // the shield recharge: fewer seconds is the gain
};
extern const Headline HEADLINE[COUNT];
float HeadlineValue(Id id, int level);
}

