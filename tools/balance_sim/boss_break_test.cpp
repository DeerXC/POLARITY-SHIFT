// boss_break_test: the overload / break system of the boss (the real GameBoss).
//   1. an overload is charged after a combo in every phase, and a piercing beam on the weak point while it charges BREAKS it: the finisher is cancelled,
//      the boss is staggered (the HUD says so), and the shots that the boss takes then are not resisted any more
//   2. a beam that misses the weak point, or hits it while no overload charges, breaks nothing
//   3. an overload that is not broken fires its finisher: in phase 1 and 3 a white beam
//   4. the ram: the body charges along a lane that was shown; a ship in the lane is hit, a ship out of the lane is not
//   5. the overload comes after a round of four attacks, every time; it spills no energy; the boss does not hang in the air while it charges; an overload that goes off takes
//      30 % of the HP of the ship, in every phase (the overload of phase 2 is the beam of the hull)
//   boss_break_test returns 1 when a check fails
#include "boss/GameBoss.h"
#include "game/GameTypes.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// the numbers of the attacks (GameBoss.cpp AtkId; GameBoss::GetAttackForSim)
constexpr int SPIRAL = 6;
constexpr int OVERLOAD = 9;
constexpr int RAM = 12;
constexpr int SWEEP = 13;

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
    p.y = 0.0f;
    p.halfWidth = 0.06f;
    p.halfHeight = 0.08f;
    p.speed = 0.018f;
    p.polarity = Polarity::Blue;
    return p;
}

static bool ToPhase(int phase, Player& player)
{
    GameBoss::Initialize();
    GameBoss::Start();
    for (int i = 0; i < 3000 && !(GameBoss::IsBattleStarted() && !GameBoss::IsTransitioning()); ++i) GameBoss::Update(player);
    // phase 1 -> 2: one blow; phase 2 -> 3: one blow on the hull (the drones do not stand in the way of its bar)
    for (int k = 0; k < 6 && GameBoss::GetPhase() < phase; ++k)
    {
        GameBoss::TakeDamage(9999);
        for (int i = 0; i < 3000 && GameBoss::IsTransitioning(); ++i) GameBoss::Update(player);
    }
    return GameBoss::GetPhase() == phase;
}

// runs the fight (the player stays out of the way: top of the screen, invincible by not being asked) until an overload charges; false when none does in time
static bool WaitForOverload(Player& player, int maxFrames)
{
    for (int i = 0; i < maxFrames; ++i)
    {
        player.y = 0.85f; // far from every lane
        GameBoss::Update(player);
        int left = 0;
        if (GameBoss::GetInterruptWindowForSim(left) && left > 30) return true;
    }
    return false;
}

// a piercing beam across the whole screen at height y: returns whether the boss took it as a hit on a weak point
static void Beam(float y, float halfHeight)
{
    GameBoss::CheckSpecialShotHit(-1.0f, 1.2f, y, halfHeight, 10);
}

static float WeakY(float& x)
{
    float y = 0.0f;
    GameBoss::GetTargetPosition(x, y);
    return y;
}

static void TestPhase(int phase, Player& player)
{
    std::printf("phase %d\n", phase);
    if (!ToPhase(phase, player))
    {
        Check(false, "reached the phase");
        return;
    }
    // 1. the overload comes, and a beam on the weak point breaks it
    Check(WaitForOverload(player, 60 * 40), "an overload is charged after a combo");
    float wx = 0.0f;
    const float wy = WeakY(wx);
    // 2. a beam that misses it breaks nothing (it goes 0.5 above the weak point, its height is small)
    Beam(wy + 0.55f, 0.01f);
    int left = 0;
    Check(GameBoss::GetInterruptWindowForSim(left), "a beam that misses the weak point does not break the overload");
    Check(!GameBoss::GetHudInfo().overheated, "... and the boss is not staggered");
    Check(GameBoss::GetHudInfo().overload, "the HUD says that the overload charges");
    int overloads = 0, breaks = 0;
    GameBoss::GetBreakStatsForSim(overloads, breaks);
    const int breaksBefore = breaks;
    // the thin beam of PRISM (0.045 high) is no piercing beam: it breaks nothing
    GameBoss::GetBreakStatsForSim(overloads, breaks);
    const int breaksThin = breaks;
    Beam(wy, 0.045f);
    GameBoss::GetBreakStatsForSim(overloads, breaks);
    Check(breaks == breaksThin && GameBoss::GetInterruptWindowForSim(left), "the thin beam of PRISM does not break the overload");
    // 1. the beam on the weak point
    Beam(wy, 0.10f);
    GameBoss::GetBreakStatsForSim(overloads, breaks);
    Check(breaks == breaksBefore + 1, "a beam on the weak point breaks the overload");
    Check(!GameBoss::GetInterruptWindowForSim(left), "the charge is gone");
    Check(GameBoss::GetHudInfo().overheated, "the boss is staggered (the HUD shows the window)");
    Check(GameBoss::GetAttackForSim() == 0, "no finisher is running");
    // the window lasts, and it does not end at once
    for (int i = 0; i < 60; ++i)
    {
        player.y = 0.85f;
        GameBoss::Update(player);
    }
    Check(GameBoss::GetHudInfo().overheated, "... and it is still open a second later");
    // 2. a second beam while it is staggered is not a break (nothing charges)
    GameBoss::GetBreakStatsForSim(overloads, breaks);
    const int breaksMid = breaks;
    Beam(wy, 0.10f);
    GameBoss::GetBreakStatsForSim(overloads, breaks);
    Check(breaks == breaksMid, "a beam while the boss is staggered is not a second break");
    // the window ends and the fight goes on
    for (int i = 0; i < 200; ++i)
    {
        player.y = 0.85f;
        GameBoss::Update(player);
    }
    Check(!GameBoss::GetHudInfo().overheated, "the window ends");
    // 3. an overload that is left alone fires its finisher
    Check(WaitForOverload(player, 60 * 40), "the next overload comes");
    bool fired = false;
    for (int i = 0; i < 400 && !fired; ++i)
    {
        player.y = 0.85f;
        GameBoss::Update(player);
        GameBoss::SimLane lanes[8];
        const int n = GameBoss::GetLanesForSim(lanes, 8);
        for (int k = 0; k < n; ++k)
            if (lanes[k].framesUntil == 0 && lanes[k].halfHeight > 0.05f) fired = true;
    }
    Check(fired, "an overload that is not broken fires its finisher");
}

// ---- the ram: it comes along a lane that was shown; the lane may lean (it leans towards the middle of the screen), the body follows the lane
static bool RamStep(Player& player, int& frames)
{
    for (int i = 0; i < 60 * 120; ++i)
    {
        player.y = 0.85f;
        GameBoss::Update(player);
        ++frames;
        if (GameBoss::GetAttackForSim() == RAM) return true;
    }
    return false;
}

static void TestRam(int phase)
{
    std::printf("ram, phase %d\n", phase);
    int seenRams = 0, leanInHit = 0, leanIn = 0, straightInHit = 0, straightIn = 0, leanOut = 0, outHits = 0;
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        const bool stayIn = (attempt % 2 == 1);
        std::srand(100 + attempt);
        Player player = MakePlayer();
        if (!ToPhase(phase, player))
        {
            Check(false, "reached the phase");
            return;
        }
        int frames = 0;
        if (!RamStep(player, frames)) continue;
        ++seenRams;
        GameBoss::SimLane lanes[8];
        for (int i = 0; i < 300 && GameBoss::GetAttackForSim() == RAM; ++i) // wait until the lane is locked (it follows the ship until then)
        {
            player.x = -0.80f;
            player.y = 0.85f;
            GameBoss::Update(player);
            const int n = GameBoss::GetLanesForSim(lanes, 8);
            if (n >= 1 && lanes[0].framesUntil == 0) break;
        }
        const float slope = GameBoss::GetLaneSlopeForSim();
        const bool slanted = std::fabs(slope) > 0.05f;
        int hits = 0;
        for (int i = 0; i < 300 && GameBoss::GetAttackForSim() == RAM; ++i)
        {
            const int m = GameBoss::GetLanesForSim(lanes, 8);
            player.x = -0.80f;
            if (stayIn)
                player.y = m >= 1 ? lanes[0].y : 0.0f;
            else
                player.y = (m >= 1 && lanes[0].y > 0.0f) ? -0.85f : 0.85f;
            GameBoss::Update(player);
            if (GameBoss::CheckPlayerCollision(player, false) == BossBulletHitResult::Damaged && std::strncmp(GameBoss::GetLastHitSource(), "boss_body", 9) == 0) ++hits; // (the body, not the fan after the impact)
        }
        if (stayIn)
        {
            if (slanted) { ++leanIn; if (hits > 0) ++leanInHit; }
            else { ++straightIn; if (hits > 0) ++straightInHit; }
        }
        else
        {
            if (slanted) ++leanOut;
            outHits += hits;
        }
    }
    std::printf("  (%d rams: leaning, ship in the lane %d / hit %d; straight, ship in the lane %d / hit %d; leaning, ship out of the lane %d, hits there %d)\n", seenRams, leanIn, leanInHit, straightIn, straightInHit, leanOut, outHits);
    Check(seenRams > 6, "a ram is part of the phase");
    Check(leanIn > 0 && leanOut > 0, "a ram leans (its lane has a slope) in some of the cases");
    Check(outHits == 0, "a ship that stays out of the lane is never hit by the ram");
    Check(leanIn > 0 && leanInHit == leanIn, "a ship that stays in a leaning lane is hit (every time)");
    Check(straightIn > 0 && straightInHit == straightIn, "a ship that stays in a straight lane is hit (every time)");
}

// ---- the beam is a line from the boss through the lane at the column of the ship: it has an angle, and a ship that leaves it sideways is out of it
static void TestBeamAngle()
{
    std::printf("beam angle\n");
    bool sideways = false, onLine = false, offLine = true;
    for (int attempt = 0; attempt < 10 && !sideways; ++attempt)
    {
        std::srand(300 + attempt);
        Player player = MakePlayer();
        if (!ToPhase(1, player)) { Check(false, "reached phase 1"); return; }
        player.x = -0.85f;
        bool fired = false;
        for (int i = 0; i < 60 * 60 && !fired; ++i)
        {
            player.y = (attempt % 2 == 0) ? -0.55f : 0.55f;
            GameBoss::Update(player);
            int left = 0;
            if (GameBoss::GetAttackForSim() == OVERLOAD && !GameBoss::GetInterruptWindowForSim(left)) fired = true;
        }
        if (!fired) continue;
        GameBoss::SimLane lanes[8];
        int n = 0;
        for (int k = 0; k < 6 && !(n >= 1 && lanes[0].framesUntil == 0); ++k) // (the beam is on one step after the charge is over)
        {
            player.y = (attempt % 2 == 0) ? -0.55f : 0.55f;
            GameBoss::Update(player);
            n = GameBoss::GetLanesForSim(lanes, 8);
        }
        if (n < 1 || lanes[0].framesUntil != 0) continue;
        const float slope = GameBoss::GetLaneSlopeForSim();
        const float lane = lanes[0].y;
        player.x = -0.85f;
        player.y = lane;
        if (GameBoss::CheckPlayerCollision(player, false) == BossBulletHitResult::Damaged) onLine = true;
        player.y = lane + 0.45f;
        if (GameBoss::CheckPlayerCollision(player, false) == BossBulletHitResult::Damaged) offLine = false;
        player.y = lane - 0.45f;
        if (GameBoss::CheckPlayerCollision(player, false) == BossBulletHitResult::Damaged) offLine = false;
        if (std::fabs(slope) * 0.9f > 0.25f) // the same height, a long way along the beam towards the boss: the beam is somewhere else there
        {
            player.x = 0.05f;
            player.y = lane;
            if (GameBoss::CheckPlayerCollision(player, false) != BossBulletHitResult::Damaged) sideways = true;
        }
    }
    Check(onLine, "a ship on the line of the beam is hit");
    Check(offLine, "a ship beside the line is not");
    Check(sideways, "a leaning beam can be left sideways: the same height, a long way along it, is out of the beam");
}

// ---- phase 2: the hull alone fights on with a ram, a sweep and an overload beam of its own, and the break works on it
static void TestHullAlone()
{
    std::printf("the hull of phase 2 moves and fights\n");
    std::srand(77);
    Player player = MakePlayer();
    if (!ToPhase(2, player)) { Check(false, "reached phase 2"); return; }
    bool ram = false, sweep = false, overload = false, spiral = false, broke = false;
    float minY = 9.0f, maxY = -9.0f;
    for (int i = 0; i < 60 * 150; ++i)
    {
        player.y = 0.85f;
        GameBoss::Update(player);
        const int a = GameBoss::GetAttackForSim();
        ram |= a == RAM;
        sweep |= a == SWEEP;
        spiral |= a == SPIRAL;
        float hx = 0.0f, hy = 0.0f;
        GameBoss::GetHullForSim(hx, hy);
        minY = std::fmin(minY, hy);
        maxY = std::fmax(maxY, hy);
        int left = 0;
        if (GameBoss::GetInterruptWindowForSim(left) && left > 30 && !broke)
        {
            overload = true;
            float wx = 0.0f;
            const float wy = WeakY(wx);
            Beam(wy, 0.10f);
            if (GameBoss::GetHudInfo().overheated) broke = true;
        }
    }
    Check(ram, "the hull alone rams");
    Check(sweep, "the hull alone glides (the sweep)");
    Check(spiral, "the hull alone fires its spirals");
    Check(overload, "the hull alone charges an overload");
    Check(broke, "the piercing beam breaks the overload of the hull");
    Check(maxY - minY > 0.6f, "the hull moves over most of the height of the screen (it is not standing still)");
}

// ---- the overload: a round of four attacks, no energy cores, no hanging in the air, a heavy hit when it goes off
static void TestOverloadRules(int phase)
{
    std::printf("overload rules, phase %d\n", phase);
    std::srand(500 + phase);
    Player player = MakePlayer();
    if (!ToPhase(phase, player)) { Check(false, "reached the phase"); return; }
    int lastId = 0, attacks = 0, cycles = 0, badCycles = 0, rewards = 0;
    float maxMove = 0.0f, startY = 0.0f, bestMove = 0.0f;
    bool inOverload = false;
    int dmgBeam = 0;
    char srcBeam[40] = "";
    for (int i = 0; i < 60 * 150 && cycles < 4; ++i)
    {
        player.x = -0.85f;
        player.y = 0.85f;
        GameBoss::Update(player);
        const int id = GameBoss::GetAttackForSim();
        float hx = 0.0f, hy = 0.0f;
        GameBoss::GetHullForSim(hx, hy);
        if (id != lastId && id != 0)
        {
            if (id == OVERLOAD)
            {
                // an overload begins: the attacks since the last one were a round (the first overload of the test has no round before it: it is not counted)
                if (cycles > 0 && attacks != 4) ++badCycles;
                if (cycles > 0 || attacks > 0) ++cycles;
                attacks = 0;
                bestMove = std::fmax(bestMove, maxMove);
                inOverload = true;
                startY = hy;
                maxMove = 0.0f;
            }
            else
            {
                ++attacks;
                inOverload = false;
            }
        }
        int left = 0;
        if (inOverload && GameBoss::GetInterruptWindowForSim(left))
        {
            maxMove = std::fmax(maxMove, std::fabs(hy - startY));
            rewards += GameBoss::ConsumeEnergyReward();
        }
        lastId = id;
    }
    bestMove = std::fmax(bestMove, maxMove);
    std::printf("  (%d overloads; rounds that were not four attacks: %d; energy from the boss during a charge: %d; the hull moved up to %.3f during a charge)\n", cycles, badCycles, rewards, bestMove);
    Check(cycles >= 3, "overloads come");
    Check(badCycles == 0, "every overload comes after a round of exactly four attacks");
    Check(rewards == 0, "the boss spills no energy when it charges an overload");
    if (phase == 1) Check(bestMove > 0.02f, "the boss does not hang in the air while it charges (the weak point moves)");

    // an overload that goes off: the beam takes 30 % of the HP of the ship, in every phase
    {
        for (int attempt = 0; attempt < 6 && dmgBeam == 0; ++attempt)
        {
            std::srand(600 + attempt);
            Player p = MakePlayer();
            if (!ToPhase(phase, p)) return;
            bool fired = false;
            for (int i = 0; i < 60 * 80 && !fired; ++i)
            {
                p.x = -0.85f;
                p.y = 0.50f;
                GameBoss::Update(p);
                GameBoss::SimLane lanes[8];
                const int n = GameBoss::GetLanesForSim(lanes, 8);
                if (GameBoss::GetAttackForSim() == OVERLOAD && n >= 1 && lanes[0].framesUntil == 0)
                {
                    p.y = lanes[0].y;
                    if (GameBoss::CheckPlayerCollision(p, false) == BossBulletHitResult::Damaged)
                    {
                        dmgBeam = GameBoss::GetLastHitDamage();
                        std::snprintf(srcBeam, sizeof srcBeam, "%s", GameBoss::GetLastHitSource());
                        fired = true;
                    }
                }
            }
        }
        std::printf("  (the overload beam took %d HP of %d; source %s)\n", dmgBeam, Mod::MaxHp(), srcBeam);
        Check(dmgBeam == static_cast<int>(std::lround(0.30f * static_cast<float>(Mod::MaxHp()))), "the overload beam that goes off takes 30 % of the maximum HP (no more in phase 3)");
        Check(std::strcmp(srcBeam, "boss_beam_overload_beam") == 0, "... and the run log calls it by its name (AEGIS stops this kind)");
    }

}

int main()
{
    for (int phase = 1; phase <= 3; ++phase)
    {
        Player player = MakePlayer();
        TestPhase(phase, player);
    }
    TestRam(1);
    TestRam(2);
    TestRam(3);
    TestBeamAngle();
    TestHullAlone();
    TestOverloadRules(1);
    TestOverloadRules(2);
    TestOverloadRules(3);
    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d check(s) FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
