#include "rogue/Balance.h"
#include "rogue/ModuleDefs.h"
#include "rogue/ModulesInternal.h"
#include <algorithm>

namespace Mod
{
// name, "+" line, second "+" line, "-" line, code, type, hangar price (0: every module is free). Every text is a Loc key (assets/lang/*.txt).
const Info INFO[COUNT] = {
    {"SIPHON", "ABSORB RANGE +90%", nullptr, nullptr, "siphon", Verb::Absorb, 0},
    {"ECHO", "EVERY 4 ABSORBS: AN ARC", "THE ARC JUMPS TO 2 MORE ENEMIES", nullptr, "echo", Verb::Absorb, 0},
    {"OVERFLOW", "FULL BAR: ABSORBED BULLETS ARE STORED", "EACH: NEXT BEAM +4% DAMAGE AND LENGTH", nullptr, "overflow", Verb::Absorb, 0},
    {"MOMENTUM", "EACH ABSORB: FIRE RATE +8%, MAX +64%", "STACKS FADE AFTER 4 SECONDS", "ABSORB ENERGY -15%", "momentum", Verb::Absorb, 0},
    {"DEFLECT", "WHITE BULLETS THAT GRAZE: 85% ABSORBED", "AND THEY GIVE TRIPLE ENERGY", nullptr, "deflect", Verb::Absorb, 0},
    {"EMBER", "FIRE BAND AFTER SPECIAL", "THE FIRE BURNS BULLETS INTO ENERGY", nullptr, "ember", Verb::Break, 0},
    {"BREACH", "IGNORES ALL DAMAGE REDUCTION", "ELITE SHIELDS, BOSS RESISTANCE", nullptr, "breach", Verb::Break, 0},
    {"TWIN", "HALF BAR: A HALF BEAM", "IT COSTS HALF A BAR, 75% DAMAGE", nullptr, "twin", Verb::Break, 0},
    {"AFTERGLOW", "AFTER THE BEAM: FIRE RATE +40%", "AND SHOT DAMAGE +40% FOR 4 S", nullptr, "afterglow", Verb::Break, 0},
    {"FOCUS", "HITS ON ONE TARGET: +4% EACH", "UP TO +40%, RESET ON A NEW TARGET", nullptr, "focus", Verb::Break, 0},
    {"BACKUP CORE", "REVIVE ONCE WITH HALF THE HP", "AND A BLAST OVER THE WHOLE SCREEN", nullptr, "backup_core", Verb::Survive, 0},
    {"PULSE", "EVERY 6 S A SHOCKWAVE: BULLETS TO ENERGY", "BODY HITS -25%, A HIT BRINGS IT SOONER", nullptr, "pulse", Verb::Survive, 0},
    {"AEGIS", "BLOCKS ONE WHITE BULLET OR BEAM", "RECHARGES EVERY 40 ABSORBS", "MAX HP -10%", "aegis", Verb::Survive, 0},
    {"MEND", "5 S WITHOUT A HIT: 1 HP EVERY 1.5 S", nullptr, nullptr, "mend", Verb::Survive, 0},
    {"BACKWATER", "UNDER HALF HP: ABSORB ENERGY +20%", "BEAM NEEDS 15 LESS, LONGER INVINCIBILITY", nullptr, "backwater", Verb::Survive, 0}};

const char* const VERB_KEY[3] = {"ABSORB", "BREAK", "SURVIVE"};

// name, "+" line, second "+" line, code, price, icon cell
const TacticInfo TACTICS[TACTIC_COUNT] = {
    {"DECOY", "TACTIC KEY: DROP A DECOY", "IT PULLS AND ABSORBS BULLETS OF YOUR COLOR", "decoy", 150, 21},
    {"DASH", "TACTIC KEY: DASH WHERE YOU MOVE", "INVINCIBLE WHILE DASHING, 2.5 S COOLDOWN", "dash", 150, 22},
    {"CHARGE SHOT", "HOLD THE TACTIC KEY: CHARGE A BEAM", "A FULL CHARGE BREAKS AN OVERLOAD", "charge_beam", 150, 23}};

// name, "+" line (the three levels), second "+" line, code, icon cell
const TalentInfo TALENTS[TALENT_COUNT] = {
    {"BUFFER", "INVINCIBLE AFTER A HIT +8% / +16% / +25%", "MORE TIME TO GET AWAY AFTER A HIT", "buffer", 24},
    {"LONG PIERCE", "PIERCING BEAM LASTS +8% / +16% / +25%", "A LONGER BEAM DEALS MORE DAMAGE", "long_pierce", 25},
    {"CHARGE", "WAVES AND BOSS PHASES START WITH", "8% / 16% / 24% ENERGY", "charge", 26},
    {"DUAL", "RED: FIRE RATE +6% / +12% / +18%", "BLUE: ABSORB RANGE +6% / +12% / +18%", "dual", 27},
    {"VETERAN", "+1 / +2 / +3 REROLLS ON THE PICK SCREEN", nullptr, "veteran", 28}};

float TalentValue(Talent talent, int level)
{
    if (level <= 0) return 0.0f;
    const int k = (level > TALENT_LEVELS ? TALENT_LEVELS : level) - 1;
    static const float BUFFER[TALENT_LEVELS] = {0.08f, 0.16f, 0.25f};
    static const float CHARGE[TALENT_LEVELS] = {0.08f, 0.16f, 0.24f};
    static const float DUAL[TALENT_LEVELS] = {0.06f, 0.12f, 0.18f};
    switch (talent)
    {
    case Talent::Buffer:
    case Talent::Pierce:
        return BUFFER[k];
    case Talent::Charge:
        return CHARGE[k];
    case Talent::Dual:
        return DUAL[k];
    case Talent::Veteran:
        return static_cast<float>(level);
    default:
        return 0.0f;
    }
}

// ---- levels
namespace
{
int ClampLevel(int level)
{
    return level < 1 ? 1 : (level > 2 ? 2 : level);
}

// How much of the gain of a level (Balance::LEVEL_GAIN) a module takes: 1 = all of it. The big levers take less: a bigger absorb net or a faster gun is worth far more per point than a
// longer afterimage or a bigger heal.
constexpr float LEVEL_K[COUNT] = {
    0.5f, // Siphon      the absorb range (the area grows with its square)
    1.0f, // Echo        (level 2: one more jump)
    1.0f, // Overflow    what a stored bullet adds to the beam
    0.6f, // Momentum    fire rate at the most stacks
    0.5f, // Deflect     the energy of a grazed bullet
    0.6f, // Ember       how long and how hard the flames burn
    0.6f, // Breach      the resistance of the boss that it takes away
    0.5f, // Twin        the damage of the half beam
    0.6f, // Afterglow   how long it lasts
    0.7f, // Focus       the damage at the most stacks
    0.6f, // BackupCore  the HP of the revive
    0.6f, // Pulse       how often the shockwave comes
    1.0f, // Aegis       how fast the shield comes back
    1.0f, // Mend        healing
    1.0f  // Backwater   the energy bonus
};
}

float GainAt(Id id, int level)
{
    return 1.0f + (Balance::LEVEL_GAIN[ClampLevel(level)] - 1.0f) * LEVEL_K[static_cast<int>(id)];
}

float CostAt(Id id, int level)
{
    (void)id;
    return Balance::LEVEL_COST[ClampLevel(level)];
}

int EchoTargetsAt(int level)
{
    return 3 + (ClampLevel(level) - 1); // the first enemy and two jumps; level 2: three jumps
}

const Headline HEADLINE[COUNT] = {
    {"ABSORB RANGE", StatFmt::PlusPercent, false},          // Siphon
    {"ARC TARGETS", StatFmt::Count, false},                 // Echo
    {"BONUS PER STORED BULLET", StatFmt::PlusPercent, false}, // Overflow
    {"FIRE RATE AT FULL STACKS", StatFmt::PlusPercent, false}, // Momentum
    {"ENERGY OF A GRAZED BULLET", StatFmt::Decimal1, false}, // Deflect (times a plain bullet)
    {"FLAMES LAST", StatFmt::Seconds, false},               // Ember
    {"DAMAGE TO ELITES AND BOSS", StatFmt::Percent, false}, // Breach
    {"HALF BEAM DAMAGE", StatFmt::Percent, false},          // Twin
    {"AFTERGLOW LASTS", StatFmt::Seconds, false},           // Afterglow
    {"DAMAGE AT FULL STACKS", StatFmt::PlusPercent, false}, // Focus
    {"REVIVE WITH", StatFmt::Percent, false},               // BackupCore
    {"SHOCKWAVE EVERY", StatFmt::Seconds, true},            // Pulse
    {"ABSORBS TO RECHARGE", StatFmt::Count, true},          // Aegis
    {"HP PER SECOND", StatFmt::Decimal1, false},            // Mend
    {"ENERGY UNDER HALF HP", StatFmt::PlusPercent, false} // Backwater
};

// The headline number at a level. Every line is the formula that the effect itself uses (tools/balance_sim/upgrade_test.cpp compares the ones that can be asked of the real code).
float HeadlineValue(Id id, int level)
{
    const float g = GainAt(id, level);
    switch (id)
    {
    case Id::Siphon:
        return (Balance::SIPHON_RANGE - 1.0f) * 100.0f * g;
    case Id::Echo:
        return static_cast<float>(EchoTargetsAt(level));
    case Id::Overflow:
        return Balance::OVERFLOW_BOOST_PER_BULLET * 100.0f * g;
    case Id::Momentum:
        return Balance::MOMENTUM_STEP * static_cast<float>(Balance::MOMENTUM_MAX_STACKS) * 100.0f * g;
    case Id::Deflect:
        return Balance::DEFLECT_ENERGY_MUL * g;
    case Id::Ember:
        return static_cast<float>(EMBER_FRAMES) / 60.0f * g;
    case Id::Breach:
        return level >= 2 ? 100.0f * (1.0f + Balance::BREACH_LEVEL2_BONUS) : 100.0f;
    case Id::Twin:
        return (std::min)(100.0f, Balance::TWIN_DAMAGE * 100.0f * g);
    case Id::Afterglow:
        return static_cast<float>(Balance::AFTERGLOW_FRAMES) / 60.0f * g;
    case Id::Focus:
        return Balance::FOCUS_STEP * static_cast<float>(Balance::FOCUS_MAX_STACKS) * 100.0f * g;
    case Id::BackupCore:
        return (std::min)(100.0f, Balance::BACKUP_REVIVE_RATE * 100.0f * g);
    case Id::Pulse:
        return static_cast<float>(Balance::PULSE_PERIOD) / 60.0f / g;
    case Id::Aegis:
        return static_cast<float>(Balance::AEGIS_ABSORBS) / g;
    case Id::Mend:
        return Balance::MEND_PER_SECOND * g;
    case Id::Backwater:
        return Balance::BACKWATER_ABSORB_BONUS * 100.0f * g;
    default:
        return 0.0f;
    }
}
}

