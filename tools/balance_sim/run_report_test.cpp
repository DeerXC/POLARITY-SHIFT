// run_report_test: what the modules did (the books of ModulesStats.cpp, shown on the game over and clear pages) and the rules of the picks (the cards that LEAD to a synergy, the
// routes). The real Mod code, the real Combat.
//   1. the books of damage: what a module deals itself, what it only makes bigger (AFTERGLOW, FOCUS, BREACH, TWIN, OVERFLOW), the damage that belongs to nobody
//   2. the books of energy and HP: SIPHON / DEFLECT / PULSE / BACKWATER, AEGIS / MEND / BACKUP CORE
//   3. PULSE goes off when the ship SWITCHES COLOUR (and by itself when it does not)
//   4. the pick screen leads: a card that links to a module of the ship is on offer much more often than chance, and always after CALIBRATION
//   5. the routes: every reward is one pick as a base; the forge makes two upgrades, the wager a module and an upgrade, an elite that was shot down adds an upgrade
//   run_report_test returns 1 when a check fails
#include "game/Combat.h"
#include "game/GamePlayer.h"
#include "game/GameState.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include "sim_input.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}
static bool Near(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

static void Fresh()
{
    GamePlayer::Initialize(GameState::player);
    GameState::ResetRun();
    Mod::Reset();
    for (EnemyBullet& b : GameState::enemyBullets)
        b.active = false;
    for (Enemy& e : GameState::enemies)
        e.active = false;
}

static int ID(Mod::Id id)
{
    return static_cast<int>(id);
}

int main()
{
    std::srand(11);

    std::printf("1. the books of damage\n");
    {
        Fresh();
        Mod::Grant(Mod::Id::Echo);
        Enemy& e = GameState::enemies[0];
        e = Enemy{};
        e.active = true;
        e.x = GameState::player.x + 0.3f;
        e.y = GameState::player.y;
        e.maxHp = e.hp = 1000;
        Mod::FireEchoArc(GameState::player.x, GameState::player.y);
        const float echo = Mod::g_Stat[ID(Mod::Id::Echo)].damage;
        std::printf("     (an arc of ECHO: %.2f shots booked on it; the total is %.2f)\n", echo, Mod::ReportTotalDamage());
        Check(Near(echo, static_cast<float>(Balance::ECHO_DAMAGE_FIRST), 0.05f), "an arc of ECHO is booked on ECHO: ECHO_DAMAGE_FIRST shots");
        Check(Near(Mod::ReportTotalDamage(), echo, 0.001f) && Mod::g_Stat[ID(Mod::Id::Echo)].count == 1, "... it is in the total, and it is one arc");

        const float before = Mod::ReportTotalDamage();
        {
            const Mod::DamageSource other(Mod::DAMAGE_OTHER);
            Mod::DamageEnemy(e, 3.0f);
        }
        Check(Near(Mod::ReportTotalDamage() - before, 3.0f, 0.05f) && Near(Mod::g_Stat[ID(Mod::Id::Echo)].damage, echo, 0.001f), "damage that is nobody's (a tactic) is in the total and on no module");

        // the multipliers: what would be missing without the module
        Fresh();
        Mod::Grant(Mod::Id::Afterglow);
        Mod::g_AfterglowTimer = 100;
        Mod::NoteDealt(10.0f, 0.0f, false, true); // a tick of the beam: +40 % damage only
        Check(Near(Mod::g_Stat[ID(Mod::Id::Afterglow)].damage, 10.0f * (1.0f - 1.0f / 1.4f), 0.02f), "AFTERGLOW on a beam: 10 - 10 / 1.4 = 2.86 shots were its");
        Mod::g_Stat[ID(Mod::Id::Afterglow)] = Mod::ModuleStat{};
        Mod::NoteDealt(10.0f, 0.0f, false, false); // a shot: +40 % damage and +40 % fire rate
        Check(Near(Mod::g_Stat[ID(Mod::Id::Afterglow)].damage, 10.0f * (1.0f - 1.0f / 1.96f), 0.05f), "... on a shot it also made the gun fire faster: 10 - 10 / 1.96 = 4.9 shots");

        Fresh();
        Mod::Grant(Mod::Id::Focus);
        Mod::g_LastFocusMul = 1.2f; // OnShotHit sets it for the shot that is booked next
        Mod::NoteDealt(10.0f, 0.0f, false, false);
        Check(Near(Mod::g_Stat[ID(Mod::Id::Focus)].damage, 10.0f * (1.0f - 1.0f / 1.2f), 0.02f) && Mod::g_LastFocusMul == 1.0f, "FOCUS: a shot that was 20 % up books 1.67 of 10, and the bonus belongs to that shot only");

        Fresh();
        Mod::Grant(Mod::Id::Breach);
        Mod::NoteDealt(10.0f, 1.0f - Balance::ELITE_SHIELD, true, false);
        Check(Near(Mod::g_Stat[ID(Mod::Id::Breach)].damage, 2.5f, 0.02f), "BREACH on a shielded elite: the 25 % that the shield would have taken");
        Mod::g_Level[ID(Mod::Id::Breach)] = 2;
        Mod::g_Stat[ID(Mod::Id::Breach)] = Mod::ModuleStat{};
        Mod::NoteDealt(10.0f, 0.0f, true, false);
        Check(Near(Mod::g_Stat[ID(Mod::Id::Breach)].damage, 10.0f * (1.0f - 1.0f / (1.0f + Balance::BREACH_LEVEL2_BONUS)), 0.02f), "... level 2: and the 20 % more that it does on an elite, a rival or the boss");

        Fresh();
        Mod::Grant(Mod::Id::Twin);
        Mod::g_BeamIsHalf = true;
        Mod::NoteDealt(6.0f, 0.0f, false, true);
        Check(Near(Mod::g_Stat[ID(Mod::Id::Twin)].damage, 6.0f, 0.001f), "TWIN: the damage of a half beam is all its own (it would not have been fired)");

        Fresh();
        Mod::Grant(Mod::Id::Overflow);
        Mod::g_BeamBoost = 0.2f;
        Mod::NoteDealt(12.0f, 0.0f, false, true);
        Check(Near(Mod::g_Stat[ID(Mod::Id::Overflow)].damage, 2.0f, 0.001f), "OVERFLOW: a beam that is 20 % up books 12 * 0.2 / 1.2 = 2 shots");

        Fresh();
        Mod::Grant(Mod::Id::Momentum);
        Mod::g_Momentum = 5.0f;
        const float rate = Mod::FireRateBonus();
        Mod::NoteDealt(10.0f, 0.0f, false, false);
        Check(Near(Mod::g_Stat[ID(Mod::Id::Momentum)].damage, 10.0f * (Balance::MOMENTUM_STEP * 5.0f) / (1.0f + rate), 0.02f) && Mod::g_Stat[ID(Mod::Id::Momentum)].damage > 0.0f,
              "MOMENTUM: the part of the fire rate that is its own, on a shot (and none on a beam)");
        Mod::g_Stat[ID(Mod::Id::Momentum)] = Mod::ModuleStat{};
        Mod::NoteDealt(10.0f, 0.0f, false, true);
        Check(Mod::g_Stat[ID(Mod::Id::Momentum)].damage == 0.0f, "... a beam is one shot: no fire rate in it");
    }

    std::printf("2. the books of energy and HP\n");
    {
        Fresh();
        Mod::Grant(Mod::Id::Siphon);
        GameState::energy = 0;
        Mod::OnAbsorb(6.0f, ID(Mod::Id::Siphon));
        Check(Mod::g_Stat[ID(Mod::Id::Siphon)].energy > 4.0f && Mod::g_Stat[ID(Mod::Id::Siphon)].count == 1, "SIPHON: a bullet that only the wide net took books its energy and counts");
        GameState::energy = Mod::EnergyMax();
        const float before = Mod::g_Stat[ID(Mod::Id::Siphon)].energy;
        Mod::OnAbsorb(6.0f, ID(Mod::Id::Siphon));
        Check(Mod::g_Stat[ID(Mod::Id::Siphon)].energy == before, "... a full bar takes nothing, so nothing is booked");

        Fresh();
        Mod::Grant(Mod::Id::Backwater);
        GameState::playerHp = Mod::MaxHp() / 4; // under half
        GameState::energy = 0;
        Mod::OnAbsorb(6.0f);
        const float bonus = Balance::BACKWATER_ABSORB_BONUS;
        Check(Mod::g_Stat[ID(Mod::Id::Backwater)].energy > 0.5f && Mod::g_Stat[ID(Mod::Id::Backwater)].energy <= 6.0f * bonus + 0.01f, "BACKWATER: under half of the HP it books the 20 % of the energy that it added");

        Fresh();
        Mod::Grant(Mod::Id::Pulse);
        for (int i = 0; i < 5; ++i)
        {
            EnemyBullet& b = GameState::enemyBullets[i];
            b = EnemyBullet{};
            b.active = true;
            b.x = GameState::player.x + 0.05f * static_cast<float>(i);
            b.y = GameState::player.y;
        }
        GameState::energy = 0;
        Mod::FirePulse();
        Check(Mod::g_Stat[ID(Mod::Id::Pulse)].count == 5 && Mod::g_Stat[ID(Mod::Id::Pulse)].energy >= 9.0f && Mod::g_Stat[ID(Mod::Id::Pulse)].energy <= 10.5f, "PULSE: five bullets turned, 2 energy each");

        Fresh();
        Mod::Grant(Mod::Id::Aegis);
        GameState::playerHp = Mod::MaxHp();
        Check(Mod::ScaleDamageTaken(13, "sniper") == 0, "AEGIS blocks the first hit");
        Check(Mod::g_Stat[ID(Mod::Id::Aegis)].hp == 13.0f && Mod::g_Stat[ID(Mod::Id::Aegis)].count == 1, "... and books the 13 HP that it kept, once");

        Fresh();
        Mod::Grant(Mod::Id::Mend);
        GameState::playerHp = Mod::MaxHp() - 10;
        const int hp0 = GameState::playerHp;
        Mod::g_SinceHurt = 1000;
        for (int i = 0; i < 60 * 20; ++i)
            Mod::Tick();
        const int healed = GameState::playerHp - hp0;
        Check(healed > 0 && Near(Mod::g_Stat[ID(Mod::Id::Mend)].hp, static_cast<float>(healed), 0.5f), "MEND books exactly the HP that it repaired");

        Fresh();
        Mod::Grant(Mod::Id::BackupCore);
        GameState::playerHp = 0;
        Check(Mod::TryBackupCore(), "BACKUP CORE revives");
        Check(Near(Mod::g_Stat[ID(Mod::Id::BackupCore)].hp, static_cast<float>(GameState::playerHp), 0.5f) && Mod::g_Stat[ID(Mod::Id::BackupCore)].count == 1, "... and books the HP it gave, once");

        Fresh();
        Mod::Grant(Mod::Id::Siphon);
        Mod::Grant(Mod::Id::Echo);
        Mod::g_Level[ID(Mod::Id::Echo)] = 2;
        Check(Mod::ModuleReportCount() == 2, "the report has a row for every module of the run");
        const Mod::ModuleReportRow first = Mod::ModuleReportAt(0), second = Mod::ModuleReportAt(1);
        Check(first.module == ID(Mod::Id::Siphon) && second.module == ID(Mod::Id::Echo) && first.level == 1 && second.level == 2, "... in the order they were picked, with their levels");
    }

    std::printf("3. PULSE goes off when the ship switches colour\n");
    {
        Fresh();
        Mod::Grant(Mod::Id::Pulse);
        GameState::player.polarity = Polarity::Blue;
        Mod::g_PrevPolarity = Polarity::Blue;
        Mod::g_PulseCooldown = 5;
        for (int i = 0; i < 4; ++i)
            Mod::Tick();
        Check(Mod::g_PulseCooldown == 1, "it charges: 4 steps off a cooldown of 5");
        Mod::AfterPlayerUpdate();
        Mod::g_PulseTimer = 0;
        GameState::player.polarity = Polarity::Red;
        Mod::AfterPlayerUpdate();
        Check(Mod::g_PulseTimer == 0, "a switch while it is still charging does nothing");
        Mod::g_PrevPolarity = Polarity::Red;
        Mod::Tick();
        Mod::Tick();
        Check(Mod::g_PulseCooldown <= 0 && Mod::g_PulseTimer == 0, "it is READY and it waits: it does not go off by itself yet");
        Mod::AfterPlayerUpdate();
        Check(Mod::g_PulseTimer == 0, "... and no switch, no shockwave");
        GameState::player.polarity = Polarity::Blue;
        Mod::AfterPlayerUpdate();
        Check(Mod::g_PulseTimer > 0 && Mod::g_PulseCooldown == Mod::PulsePeriod(), "the switch of colour releases it, and the next one starts to charge");

        Fresh();
        Mod::Grant(Mod::Id::Pulse);
        Mod::g_PulseCooldown = 0;
        Mod::g_PulseTimer = 0;
        int steps = 0;
        while (Mod::g_PulseTimer == 0 && steps < Balance::PULSE_SWITCH_GRACE + 10)
        {
            Mod::Tick();
            ++steps;
        }
        Check(steps == Balance::PULSE_SWITCH_GRACE, "a ship that never switches gets the shockwave anyway after PULSE_SWITCH_GRACE steps (a card that waits for ever is a dead card)");
    }

    std::printf("4. the pick screen leads to a synergy\n");
    {
        Fresh();
        Mod::Grant(Mod::Id::Echo); // its partner is EMBER (THUNDERSTORM)
        int withPartner = 0, threeSame = 0;
        const int trials = 600;
        for (int n = 0; n < trials; ++n)
        {
            Mod::g_ForgeNext = false;
            Mod::g_Rerolling = false;
            Mod::RollChoices();
            bool has = false;
            for (int i = 0; i < Mod::g_ChoiceCount; ++i)
                has |= (Mod::g_Choice[i] == ID(Mod::Id::Ember) && !Mod::g_ChoiceUpgrade[i]);
            withPartner += has ? 1 : 0;
            if (Mod::g_ChoiceCount == 3 && !Mod::g_ChoiceUpgrade[0] && !Mod::g_ChoiceUpgrade[1] && !Mod::g_ChoiceUpgrade[2] && Mod::INFO[Mod::g_Choice[0]].verb == Mod::INFO[Mod::g_Choice[1]].verb && Mod::INFO[Mod::g_Choice[1]].verb == Mod::INFO[Mod::g_Choice[2]].verb) ++threeSame;
        }
        const float rate = static_cast<float>(withPartner) / static_cast<float>(trials);
        std::printf("     (EMBER is on offer in %.0f %% of the picks after ECHO; by chance it would be 3 of 14 = 21 %%)\n", rate * 100.0f);
        Check(rate > 0.50f && rate < 0.85f, "the partner of a module that the ship owns is on offer far more often than by chance (Balance::LINK_CHANCE_PERCENT)");
        Check(threeSame == 0, "... and three new modules are never one type");

        Mod::g_CalibrateOn = true;
        Mod::g_RouteStage = 2; // (a pick of the reward of wave 3)
        withPartner = 0;
        for (int n = 0; n < trials; ++n)
        {
            Mod::g_ForgeNext = false;
            Mod::RollChoices();
            bool has = false;
            for (int i = 0; i < Mod::g_ChoiceCount; ++i)
                has |= (Mod::g_Choice[i] == ID(Mod::Id::Ember) && !Mod::g_ChoiceUpgrade[i]);
            withPartner += has ? 1 : 0;
        }
        Check(withPartner == trials, "after CALIBRATION it is on offer every time");

        // an upgrade card never takes the place of the link
        Mod::g_Level[ID(Mod::Id::Echo)] = 1;
        int lost = 0;
        for (int n = 0; n < trials; ++n)
        {
            Mod::g_ForgeNext = false;
            Mod::RollChoices();
            bool has = false;
            for (int i = 0; i < Mod::g_ChoiceCount; ++i)
                has |= (Mod::g_Choice[i] == ID(Mod::Id::Ember) && !Mod::g_ChoiceUpgrade[i]);
            lost += has ? 0 : 1;
        }
        Check(lost == 0, "... even when the pick also offers an upgrade (the upgrade takes another card)");
    }

    std::printf("5. the routes\n");
    {
        // stage 1
        Fresh();
        Mod::OpenBranch(1);
        Check(Mod::g_RouteCount == 2 && Mod::g_RouteKind[0] == Mod::ROUTE_ELITE && Mod::g_RouteKind[1] == Mod::ROUTE_CALIBRATE, "after wave 2: ELITE FLEET or CALIBRATION");
        Check(!Mod::RouteLocked(Mod::ROUTE_CALIBRATE), "CALIBRATION is never locked (the supply run was, when the ship was whole)");
        const int rerolls = Mod::RerollsLeft();
        Mod::ConfirmBranch(1);
        Check(Mod::g_Branch == 2 && Mod::g_CalibrateOn && Mod::RerollsLeft() == rerolls + Balance::CALIBRATE_REROLLS, "CALIBRATION: one more reroll, and the picks lead from now on");
        Fresh();
        Mod::OpenBranch(1);
        Mod::ConfirmBranch(0);
        Check(Mod::g_Branch == 1 && !Mod::g_CalibrateOn, "ELITE FLEET: the elites, and no calibration");

        // an extra elite that is shot down is counted (and one of the ordinary waves is not)
        Fresh();
        Mod::g_Branch = 1;
        Enemy e{};
        e.groupId = 7031;
        Mod::OnEnemyKilled(e);
        e.groupId = 12;
        Mod::OnEnemyKilled(e);
        Check(Mod::g_DirectorElitesDown == 1, "only the extra elite of the fleet pays (group 7000 + wave * 10 + n)");
        Mod::g_Branch = 2;
        e.groupId = 7040;
        Mod::OnEnemyKilled(e);
        Check(Mod::g_DirectorElitesDown == 1, "... and only on the route of the fleet");

        // stage 2: every reward is one pick as a base
        auto owned = []()
        {
            Fresh();
            Mod::Grant(Mod::Id::Echo);
            Mod::Grant(Mod::Id::Siphon);
            Mod::Grant(Mod::Id::Mend);
            GameState::playerHp = Mod::MaxHp();
            Mod::g_ChoiceDone = false;
            Mod::OpenBranch(2);
        };
        // run the picks to the end; returns the kinds as a string: M = a module card was taken, U = an upgrade card
        auto play = []() -> std::string
        {
            std::string kinds;
            int guard = 0;
            while (Mod::g_ChoiceOpen && guard++ < 6)
            {
                kinds += Mod::g_ChoiceUpgrade[0] ? 'U' : 'M';
                Mod::ConfirmChoice(0);
            }
            return kinds;
        };
        owned();
        Check(Mod::g_RouteCount == 3 && Mod::g_RouteKind[0] == Mod::ROUTE_MODULE && Mod::g_RouteKind[1] == Mod::ROUTE_FORGE && Mod::g_RouteKind[2] == Mod::ROUTE_WAGER, "after wave 3: NEW MODULE, FORGE, WAGER");
        Mod::ConfirmBranch(0);
        const std::string module = play();
        Check(module == "M", "NEW MODULE: one pick, a module");

        owned();
        Mod::ConfirmBranch(1);
        const std::string forge = play();
        Check(forge == "UU" && Mod::g_Level[ID(Mod::Id::Echo)] + Mod::g_Level[ID(Mod::Id::Siphon)] + Mod::g_Level[ID(Mod::Id::Mend)] == 3 + 2, "FORGE: two upgrades (two of the three modules are level 2), no new module");

        owned();
        const int hp = GameState::playerHp;
        Mod::ConfirmBranch(2);
        Check(GameState::playerHp == hp - Mod::WagerCost(), "WAGER: it costs 30 % of the maximum HP, now");
        const std::string wager = play();
        Check(wager == "MU", "... and gives a module, then an upgrade");

        // the elite fleet: an upgrade for the elite that was shot down, on top of any reward
        owned();
        Mod::g_EliteBonus = true;
        Mod::ConfirmBranch(0);
        const std::string eliteModule = play();
        Check(eliteModule == "MU", "ELITE REWARD + NEW MODULE: a module, then the free upgrade");
        owned();
        Mod::g_EliteBonus = true;
        Mod::ConfirmBranch(1);
        const std::string eliteForge = play();
        Check(eliteForge.size() >= 2 && eliteForge.find('M') == std::string::npos, "ELITE REWARD + FORGE: upgrades only (the third one ends when nothing is left to upgrade)");

        owned();
        Mod::g_EliteBonus = false;
        const bool wagerLocked = Mod::RouteLocked(Mod::ROUTE_WAGER);
        GameState::playerHp = Mod::WagerCost() + 5;
        Check(!wagerLocked && Mod::RouteLocked(Mod::ROUTE_WAGER), "WAGER is locked when the ship cannot pay it");
        Fresh();
        Mod::Grant(Mod::Id::Echo);
        Mod::g_Level[ID(Mod::Id::Echo)] = 2;
        Check(Mod::RouteLocked(Mod::ROUTE_FORGE) && Mod::RouteLocked(Mod::ROUTE_WAGER), "FORGE and WAGER are locked when nothing can be upgraded");
    }

    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
