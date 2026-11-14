#include "rogue/Balance.h"
#include "rogue/ModulesInternal.h"

#include <algorithm>

// What the modules did in a run. A roguelite is learned from its runs: "why was this one strong" needs a number next to every module, so the game over page and the clear page
// show one (Mod::DrawModuleReport), and the simulator prints the same numbers (tools/balance_sim/sim_main.cpp: SIM_STATS_FILE) to find the cards that do nothing.
//
// THE RULE OF THE BOOKS. A module is credited with what would NOT have happened without it:
//   - what it does itself: the damage of an arc of ECHO, of the fire of EMBER, of the blast of BACKUP CORE; the energy that PULSE, SIPHON or DEFLECT put into the bar; the HP that MEND
//     repairs, that AEGIS keeps, that the revive gives;
//   - what it makes bigger: AFTERGLOW, FOCUS, MOMENTUM, BREACH, TWIN, OVERFLOW multiply the damage of the gun and of the beam; each is credited with the part of every hit that
//     would be missing without it (a hit of D with a bonus of +40 % is D - D / 1.4 = 29 % of D). Credits of different modules can overlap: they are not a split of one cake.
namespace Mod
{
ModuleStat g_Stat[COUNT];
float g_StatTotalDamage = 0.0f;
float g_StatTotalEnergy = 0.0f;
int g_DamageSource = -1;
float g_LastFocusMul = 1.0f;
float g_BeamBoost = 0.0f;

void StatsReset()
{
    for (ModuleStat& stat : g_Stat)
        stat = ModuleStat{};
    g_StatTotalDamage = 0.0f;
    g_StatTotalEnergy = 0.0f;
    g_DamageSource = -1;
    g_LastFocusMul = 1.0f;
    g_BeamBoost = 0.0f;
}

void CreditDamage(int module, float shots)
{
    if (module >= 0 && module < COUNT && shots > 0.0f) g_Stat[module].damage += shots;
}

void CreditEnergy(int module, float units)
{
    if (module >= 0 && module < COUNT && units > 0.0f) g_Stat[module].energy += units;
}

void CreditHp(int module, float points)
{
    if (module >= 0 && module < COUNT && points > 0.0f) g_Stat[module].hp += points;
}

void CreditCount(int module, int n)
{
    if (module >= 0 && module < COUNT) g_Stat[module].count += n;
}

void NoteDealt(float shots, float reduction, bool bonusTarget, bool beam)
{
    if (shots <= 0.0f) return;
    g_StatTotalDamage += shots;
    const float focus = g_LastFocusMul;
    g_LastFocusMul = 1.0f; // (it belongs to one shot: a beam, a module or a shell that stops the shot must not inherit it)
    if (g_DamageSource >= 0)
    {
        g_Stat[g_DamageSource].damage += shots; // a module deals this damage itself
        return;
    }
    if (g_DamageSource == DAMAGE_OTHER) return;

    // the fire rate bonuses make more shots, and so more damage; the shots only (a beam is one shot)
    const float rate = beam ? 0.0f : FireRateBonus();
    if (Has(Id::Afterglow) && g_AfterglowTimer > 0)
    {
        float factor = 1.0f + Balance::AFTERGLOW_BONUS; // every shot hits harder ...
        if (!beam && rate >= Balance::AFTERGLOW_BONUS) factor *= (1.0f + rate) / (1.0f + rate - Balance::AFTERGLOW_BONUS); // ... and the gun fires faster
        CreditDamage(static_cast<int>(Id::Afterglow), shots * (1.0f - 1.0f / factor));
    }
    if (!beam && Has(Id::Momentum) && g_Momentum > 0.0f)
    {
        const float part = Balance::MOMENTUM_STEP * g_Momentum * Gain(Id::Momentum);
        CreditDamage(static_cast<int>(Id::Momentum), shots * part / (1.0f + rate));
    }
    if (!beam && Has(Id::Focus) && focus > 1.0f) CreditDamage(static_cast<int>(Id::Focus), shots * (1.0f - 1.0f / focus));
    if (Has(Id::Breach))
    {
        float credit = reduction > 0.0f ? reduction : 0.0f; // what the shield or the resistance would have taken
        if (bonusTarget && Level(Id::Breach) >= 2) credit += 1.0f - 1.0f / BreachBonus();
        CreditDamage(static_cast<int>(Id::Breach), shots * credit);
    }
    if (beam && g_BeamIsHalf && Has(Id::Twin)) CreditDamage(static_cast<int>(Id::Twin), shots); // a half beam would not have been fired at all
    if (beam && g_BeamBoost > 0.0f && Has(Id::Overflow)) CreditDamage(static_cast<int>(Id::Overflow), shots * g_BeamBoost / (1.0f + g_BeamBoost));
}

// ---------------------------------------------------------------- the report
int ModuleReportCount()
{
    return g_OrderCount;
}

ModuleReportRow ModuleReportAt(int index)
{
    ModuleReportRow row{};
    if (index < 0 || index >= g_OrderCount) return row;
    const int module = g_Order[index];
    const ModuleStat& stat = g_Stat[module];
    row.module = module;
    row.level = g_Level[module] > 0 ? g_Level[module] : 1;
    row.damage = stat.damage;
    row.energy = stat.energy;
    row.hp = stat.hp;
    row.count = stat.count;
    return row;
}

// what the count of a module counts (Loc keys, assets/lang/*.txt)
const char* ModuleReportCountLabel(int module)
{
    switch (static_cast<Id>(module))
    {
    case Id::Siphon: return "EXTRA ABSORBS";
    case Id::Echo: return "ARCS";
    case Id::Overflow: return "BULLETS STORED";
    case Id::Momentum: return "STACKS";
    case Id::Deflect: return "GRAZED AND TAKEN";
    case Id::Ember: return "BULLETS BURNT";
    case Id::Breach: return "HITS THROUGH SHIELDS";
    case Id::Twin: return "HALF BEAMS";
    case Id::Afterglow: return "AFTERGLOWS";
    case Id::Focus: return "FULL FOCUS";
    case Id::BackupCore: return "REVIVES";
    case Id::Pulse: return "BULLETS TURNED";
    case Id::Aegis: return "HITS BLOCKED";
    case Id::Mend: return "REPAIRS";
    case Id::Backwater: return "TIMES UNDER HALF HP";
    default: return "";
    }
}

float ReportTotalDamage()
{
    return g_StatTotalDamage;
}

float ReportTotalEnergy()
{
    return g_StatTotalEnergy;
}
}
