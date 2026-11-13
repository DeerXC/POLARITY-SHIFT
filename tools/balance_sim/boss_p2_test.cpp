// boss_p2_test: the drones of phase 2 and 3, and the shell of the core (the real GameBoss).
//   1. phase 2: two drones, 60 HP each. Drone 0 fires red and blue bullets (one colour per fan, it alternates), drone 1 white needles and a laser now and then
//   2. a drone that is shot down comes back: after 15 s in phase 2; in phase 3 a drone that is down waits for the other one, and both come back together 7 s after the second one went down
//   3. the hull of phase 2 has its own bar; shots on it do their damage whatever the drones are doing; its empty bar starts phase 3, and the drones come along
//   4. phase 3: the shell of the core is closed while a drone is up; it opens when both are down, when the boss charges an overload, and for good under 25 % of the life
//   5. the drones do not fire while the boss charges an overload
//   boss_p2_test returns 1 when a check fails
#include "boss/GameBoss.h"
#include "game/GameTypes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

static Player MakePlayer()
{
    Player p{};
    p.x = -0.85f;
    p.y = 0.85f;
    p.halfWidth = 0.06f;
    p.halfHeight = 0.08f;
    p.speed = 0.018f;
    p.polarity = Polarity::Blue;
    return p;
}

static void Settle(Player& player, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        player.x = -0.85f;
        player.y = 0.85f;
        GameBoss::Update(player);
    }
}

// phase 1 -> 2 and 2 -> 3 are one blow each (the drones do not stand in the way of the bar of the body)
static bool ToPhase(int phase, Player& player)
{
    GameBoss::Initialize();
    GameBoss::Start();
    for (int i = 0; i < 3000 && !(GameBoss::IsBattleStarted() && !GameBoss::IsTransitioning()); ++i) GameBoss::Update(player);
    for (int k = 0; k < 4 && GameBoss::GetPhase() < phase; ++k)
    {
        if (GameBoss::GetPhase() == 2)
        {
            // phase 2 -> 3: the hull (a module's damage in phase 2 goes to the hull)
            GameBoss::TakeDamage(9999);
        }
        else
        {
            GameBoss::TakeDamage(9999);
        }
        for (int i = 0; i < 3000 && GameBoss::IsTransitioning(); ++i) GameBoss::Update(player);
    }
    return GameBoss::GetPhase() == phase;
}

// a plain shot from the left at (x, y)
static bool ShootAt(float x, float y, Polarity pol = Polarity::Red)
{
    Bullet b{};
    b.active = true;
    b.x = x - 0.30f;
    b.y = y;
    b.halfWidth = 0.040f;
    b.halfHeight = 0.012f;
    b.speed = 0.030f;
    b.polarity = pol;
    for (int i = 0; i < 40 && b.active; ++i)
    {
        b.x += b.speed;
        GameBoss::CheckPlayerBullets(&b, 1);
    }
    return !b.active;
}

static void KillDrone(int i)
{
    for (int n = 0; n < 200; ++n)
    {
        GameBoss::SimPod pods[2];
        GameBoss::GetPodsForSim(pods);
        if (!pods[i].alive) return;
        ShootAt(pods[i].x - 0.01f, pods[i].y);
    }
}

static void TestPhase2Drones()
{
    std::printf("phase 2: the drones\n");
    std::srand(11);
    Player player = MakePlayer();
    if (!ToPhase(2, player)) { Check(false, "reached phase 2"); return; }
    Settle(player, 200);
    GameBoss::SimPod pods[2];
    GameBoss::GetPodsForSim(pods);
    Check(pods[0].alive && pods[1].alive, "two drones");
    BossHudInfo info = GameBoss::GetHudInfo();
    Check(info.shipAlive[0] && info.shipAlive[1] && info.shipRate[0] > 0.99f, "the HUD shows both, at full life");
    // what they fire: watch new bolts that are born at a drone
    int red = 0, blue = 0, white = 0, laser = 0;
    GameBoss::SimBolt prev[400];
    int prevN = 0;
    for (int f = 0; f < 60 * 20; ++f)
    {
        player.x = -0.85f;
        player.y = 0.85f;
        GameBoss::Update(player);
        if (GameBoss::GetLaserStateForSim() == 2) ++laser;
        GameBoss::GetPodsForSim(pods);
        GameBoss::SimBolt bolts[400];
        const int n = GameBoss::GetBoltsForSim(bolts, 400);
        for (int k = 0; k < n; ++k)
        {
            bool old = false;
            for (int q = 0; q < prevN && !old; ++q)
                old = std::fabs(prev[q].x + prev[q].vx - bolts[k].x) < 0.002f && std::fabs(prev[q].y + prev[q].vy - bolts[k].y) < 0.002f;
            if (old) continue;
            for (int s = 0; s < 2; ++s)
            {
                if (!pods[s].alive) continue;
                if (std::fabs(bolts[k].x - (pods[s].x - 0.07f)) < 0.04f && std::fabs(bolts[k].y - pods[s].y) < 0.04f)
                {
                    if (s == 0 && !bolts[k].white) (bolts[k].red ? red : blue)++;
                    if (s == 1 && bolts[k].white) ++white;
                }
            }
        }
        for (int k = 0; k < n; ++k) prev[k] = bolts[k];
        prevN = n;
    }
    std::printf("  (in 20 s: red %d, blue %d from drone 0; white %d from drone 1; laser on %d frames)\n", red, blue, white, laser);
    Check(red > 0 && blue > 0, "drone 0 fires red and blue bullets");
    Check(white > 0, "drone 1 fires white needles");
    Check(laser > 0, "drone 1 fires a laser now and then");
    // shot down: back after 15 s
    KillDrone(0);
    GameBoss::GetPodsForSim(pods);
    Check(!pods[0].alive, "drone 0 can be shot down");
    const int respawn = GameBoss::GetDroneRespawnForSim(0);
    std::printf("  (drone 0 is back in %d frames)\n", respawn);
    Check(respawn > 880 && respawn <= 900, "it is back in 15 s");
    Settle(player, respawn + 5);
    GameBoss::GetPodsForSim(pods);
    Check(pods[0].alive, "... and it is back");
    Check(GameBoss::GetPhase() == 2, "shooting the drones does not end the phase");
}

static void TestHull()
{
    std::printf("phase 2: the hull\n");
    std::srand(12);
    Player player = MakePlayer();
    if (!ToPhase(2, player)) { Check(false, "reached phase 2"); return; }
    Settle(player, 200);
    float hx = 0.0f, hy = 0.0f;
    const float before = GameBoss::GetHudInfo().segmentRate[1];
    for (int n = 0; n < 40; ++n)
    {
        GameBoss::GetHullForSim(hx, hy);
        ShootAt(hx - 0.10f, hy);
    }
    const float drop = before - GameBoss::GetHudInfo().segmentRate[1];
    std::printf("  (40 shots on the hull took %.3f of its bar)\n", drop);
    Check(drop > 0.05f, "shots on the hull take its bar down while the drones are up");
    GameBoss::TakeDamage(9999);
    for (int i = 0; i < 3000 && GameBoss::IsTransitioning(); ++i) GameBoss::Update(player);
    Check(GameBoss::GetPhase() == 3, "the empty bar of the hull starts phase 3");
    Settle(player, 60);
    GameBoss::SimPod pods[2];
    GameBoss::GetPodsForSim(pods);
    Check(pods[0].alive && pods[1].alive, "the drones come along into phase 3");
}

static void TestShell()
{
    std::printf("phase 3: the shell of the core\n");
    std::srand(13);
    Player player = MakePlayer();
    if (!ToPhase(3, player)) { Check(false, "reached phase 3"); return; }
    // wait for a moment that is not an overload
    for (int i = 0; i < 600; ++i)
    {
        int left = 0;
        if (!GameBoss::GetInterruptWindowForSim(left) && GameBoss::GetAttackForSim() != 0) break;
        Settle(player, 1);
    }
    Check(!GameBoss::IsCoreExposedForSim(), "the shell is closed while the drones are up");
    Check(GameBoss::GetHudInfo().shellClosed, "... and the HUD says so");
    float cx = 0.0f, cy = 0.0f;
    GameBoss::GetHullForSim(cx, cy);
    const float before = GameBoss::GetHudInfo().segmentRate[2];
    for (int n = 0; n < 30; ++n) ShootAt(cx - 0.05f, cy);
    Check(std::fabs(GameBoss::GetHudInfo().segmentRate[2] - before) < 0.0001f, "a shot on the closed shell does nothing");
    KillDrone(0);
    Check(!GameBoss::IsCoreExposedForSim() || GameBoss::GetHudInfo().overload, "one drone down: still closed");
    // the drone that is down waits for the other one: 20 s later it is still down (it does not come back and take turns with the other one)
    for (int i = 0; i < 60 * 20; ++i)
    {
        player.x = -0.85f;
        player.y = 0.85f;
        GameBoss::Update(player);
    }
    {
        GameBoss::SimPod pods[2];
        GameBoss::GetPodsForSim(pods);
        Check(!pods[0].alive && pods[1].alive, "a drone that is down waits for the other one (it is still down 20 s later)");
    }
    KillDrone(1);
    Check(GameBoss::IsCoreExposedForSim(), "both drones down: the shell opens");
    const float open0 = GameBoss::GetHudInfo().segmentRate[2];
    GameBoss::GetHullForSim(cx, cy);
    for (int n = 0; n < 30; ++n) ShootAt(cx - 0.05f, cy);
    Check(GameBoss::GetHudInfo().segmentRate[2] < open0 - 0.02f, "... and the core can be hurt");
    const int respawn0 = GameBoss::GetDroneRespawnForSim(0), respawn1 = GameBoss::GetDroneRespawnForSim(1);
    std::printf("  (the drones are back in %d / %d frames)\n", respawn0, respawn1);
    Check(respawn0 == respawn1 && respawn1 > 390 && respawn1 <= 420, "both drones down: they come back together, after 7 s");
    // the overload opens it
    std::srand(14);
    if (!ToPhase(3, player)) return;
    bool sawOverload = false, openInOverload = true, droneFiredInOverload = false;
    for (int i = 0; i < 60 * 60 && !sawOverload; ++i)
    {
        Settle(player, 1);
        int left = 0;
        if (GameBoss::GetInterruptWindowForSim(left))
        {
            sawOverload = true;
            const int v0 = GameBoss::GetDroneVolleysForSim(0) + GameBoss::GetDroneVolleysForSim(1);
            for (int f = 0; f < 80 && GameBoss::GetInterruptWindowForSim(left); ++f)
            {
                openInOverload &= GameBoss::IsCoreExposedForSim();
                Settle(player, 1);
            }
            droneFiredInOverload = GameBoss::GetDroneVolleysForSim(0) + GameBoss::GetDroneVolleysForSim(1) != v0;
        }
    }
    Check(sawOverload, "the core charges an overload");
    Check(openInOverload, "the shell is open while it charges");
    Check(!droneFiredInOverload, "the drones are silent while it charges");
    // dying: under 25 % the shell is gone for good
    std::srand(15);
    if (!ToPhase(3, player)) return;
    KillDrone(0);
    KillDrone(1);
    GameBoss::GetHullForSim(cx, cy);
    for (int n = 0; n < 400 && GameBoss::GetHudInfo().segmentRate[2] > 0.22f; ++n) ShootAt(cx - 0.05f, cy);
    Settle(player, 5);
    GameBoss::SimPod pods[2];
    GameBoss::GetPodsForSim(pods);
    Check(GameBoss::GetPhase() == 3 && GameBoss::GetHudInfo().segmentRate[2] < 0.25f, "the core is under a quarter of its life");
    Check(pods[0].alive && pods[1].alive, "the drones are back at once");
    Check(GameBoss::IsCoreExposedForSim(), "... and the shell stays open");
}

int main()
{
    TestPhase2Drones();
    TestHull();
    TestShell();
    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d check(s) FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
