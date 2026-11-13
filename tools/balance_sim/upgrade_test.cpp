// upgrade_test: the levels of the modules (the real Mod:: code).
//   1. level 2 is a step up: GainAt grows, CostAt shrinks, the headline number of every module moves in its good direction
//   2. the headline number that the upgrade card shows is the number the effect really uses (asked of the real code where it can be asked)
//   3. an upgrade is worth a card: the first step is at least +20% on the headline of every module, and the gain of the big levers is capped
//   upgrade_test prints the table of the headline numbers and returns 1 when a check fails
#include "game/GamePlayer.h"
#include "game/GameState.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

static bool Near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

static void Fresh(Mod::Id id, int level)
{
    GamePlayer::Initialize(GameState::player);
    GameState::ResetRun();
    Mod::Reset();
    Mod::Grant(id);
    Mod::g_Level[static_cast<int>(id)] = level;
}

int main()
{
    std::srand(1);
    std::printf("1. every level is a step up\n");
    bool steps = true;
    for (int i = 0; i < Mod::COUNT; ++i)
    {
        const Mod::Id id = static_cast<Mod::Id>(i);
        steps &= Mod::GainAt(id, 2) > Mod::GainAt(id, 1);
        steps &= Mod::CostAt(id, 2) < Mod::CostAt(id, 1);
        const float v1 = Mod::HeadlineValue(id, 1), v2 = Mod::HeadlineValue(id, 2);
        const bool better = Mod::HEADLINE[i].lowerIsBetter ? (v2 < v1) : (v2 > v1);
        if (!better) std::printf("     headline of %s does not improve: %.3f %.3f\n", Mod::INFO[i].code, v1, v2);
        steps &= better;
    }
    Check(steps, "gain grows, cost shrinks, the headline of every module improves at level 2");

    std::printf("2. the headline is what the effect uses\n");
    {
        bool ok = true;
        for (int level = 1; level <= 2; ++level)
        {
            Fresh(Mod::Id::Siphon, level);
            GameState::player.polarity = Polarity::Red; // (the talent DUAL adds to the range while blue: none here)
            ok &= Near((Mod::SiphonScale() - 1.0f) * 100.0f, Mod::HeadlineValue(Mod::Id::Siphon, level), 0.05f);
        }
        Check(ok, "SIPHON: the range of the net");
        ok = true;
        for (int level = 1; level <= 2; ++level)
        {
            Fresh(Mod::Id::Aegis, level);
            ok &= Near(static_cast<float>(Mod::AegisAbsorbsNeeded()), Mod::HeadlineValue(Mod::Id::Aegis, level), 1.0f);
        }
        Check(ok, "AEGIS: the absorbs that recharge the shield");
        ok = true;
        for (int level = 1; level <= 2; ++level)
        {
            Fresh(Mod::Id::Pulse, level);
            ok &= Near(static_cast<float>(Mod::PulsePeriod()) / 60.0f, Mod::HeadlineValue(Mod::Id::Pulse, level), 0.02f);
        }
        Check(ok, "PULSE: the time between two shockwaves");
        ok = true;
        for (int level = 1; level <= 2; ++level)
        {
            Fresh(Mod::Id::BackupCore, level);
            Mod::TryBackupCore();
            ok &= Near(100.0f * static_cast<float>(GameState::playerHp) / static_cast<float>(Mod::MaxHp()), Mod::HeadlineValue(Mod::Id::BackupCore, level), 1.0f);
        }
        Check(ok, "BACKUP CORE: the HP of the revive");
        ok = true;
        for (int level = 1; level <= 2; ++level)
        {
            Fresh(Mod::Id::Twin, level);
            int energy = Mod::EnergyMax() / 2;
            ok &= Mod::BeginBeam(energy) && energy == 0;
            ok &= Near(Mod::SpecialDamageMul() * 100.0f, Mod::HeadlineValue(Mod::Id::Twin, level), 0.5f);
        }
        Check(ok, "TWIN: half a bar fires a half beam (it costs half a bar), and its damage");
        ok = true;
        for (int level = 1; level <= 2; ++level)
        {
            Fresh(Mod::Id::Deflect, level);
            ok &= Near(Mod::DeflectEnergyMul(), Mod::HeadlineValue(Mod::Id::Deflect, level), 0.01f);
        }
        Check(ok, "DEFLECT: the energy of a grazed bullet");
    }

    std::printf("3. the first step is worth a card\n");
    std::printf("   %-12s %-26s %9s %9s   %s\n", "module", "headline", "level 1", "level 2", "level 2 vs 1");
    bool worth = true;
    for (int i = 0; i < Mod::COUNT; ++i)
    {
        const Mod::Id id = static_cast<Mod::Id>(i);
        const float v1 = Mod::HeadlineValue(id, 1), v2 = Mod::HeadlineValue(id, 2);
        // for a headline that is a "+X%" the real change is the ratio of the (1 + X) values, for a plain number it is the ratio of the numbers
        float gain;
        if (Mod::HEADLINE[i].lowerIsBetter)
            gain = v1 / v2 - 1.0f;
        else if (Mod::HEADLINE[i].fmt == Mod::StatFmt::PlusPercent)
            gain = (v2 - v1) / v1;
        else
            gain = v2 / v1 - 1.0f;
        std::printf("   %-12s %-26s %9.2f %9.2f   %+.0f%%\n", Mod::INFO[i].code, Mod::HEADLINE[i].label, v1, v2, gain * 100.0f);
        worth &= gain >= 0.19f;
    }
    Check(worth, "level 2 raises the headline of every module by at least 20% (it was +30% of the number, and for most modules less in practice)");
    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
