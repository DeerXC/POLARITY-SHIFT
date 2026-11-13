// boss_hit_test: where does a plain shot of the ship hurt the boss? It runs the REAL GameBoss, freezes it in each phase and fires one shot after the other along
// lanes at different heights (the same Bullet a player shot is), and reports for each height whether the shot did damage, stopped on armour (sparks, no damage),
// or flew past. A plain shot at the middle of the boss must hurt it in every phase (it did not in phase 1 and 3: the weak point was behind armour).
//   boss_hit_test        prints the table and returns 1 when a phase cannot be hurt by plain shots at all
#include "boss/GameBoss.h"
#include "game/GameTypes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static Player MakePlayer()
{
    Player p{};
    p.x = -0.75f;
    p.y = 0.0f;
    p.halfWidth = 0.06f;
    p.halfHeight = 0.08f;
    p.speed = 0.018f;
    p.polarity = Polarity::Blue;
    return p;
}

static float Total(const BossHudInfo& h)
{
    // remaining life of the boss in the units of the bar: the weights are the shares of the phases
    return h.segmentRate[0] * h.segmentWeight[0] + h.segmentRate[1] * h.segmentWeight[1] + h.segmentRate[2] * h.segmentWeight[2];
}

// 0 = flew past, 1 = stopped without damage (armour), 2 = damage
static int Shoot(float y, Polarity pol)
{
    Bullet b{};
    b.active = true;
    b.x = -0.75f;
    b.y = y;
    b.halfWidth = 0.040f;
    b.halfHeight = 0.012f;
    b.speed = 0.030f;
    b.polarity = pol;
    const float before = Total(GameBoss::GetHudInfo());
    for (int i = 0; i < 120 && b.active; ++i)
    {
        b.x += b.speed;
        GameBoss::CheckPlayerBullets(&b, 1);
    }
    const float after = Total(GameBoss::GetHudInfo());
    if (b.active) return 0;
    return after < before - 1.0e-6f ? 2 : 1;
}

static void Freeze(Player& player)
{
    // a few updates so that the boss has come to its place
    for (int i = 0; i < 4; ++i) GameBoss::Update(player);
}

static bool Row(int phase, const char* what)
{
    float tx = 0.0f, ty = 0.0f;
    GameBoss::GetTargetPosition(tx, ty);
    std::printf("phase %d  %s  weak point at (%.3f, %.3f)\n  offset from the weak point: ", phase, what, tx, ty);
    bool anyDamage = false;
    for (int k = -12; k <= 12; ++k)
    {
        const float dy = 0.045f * static_cast<float>(k);
        const int r = Shoot(ty + dy, k % 2 == 0 ? Polarity::Red : Polarity::Blue);
        anyDamage |= r == 2;
        std::printf("%s", r == 2 ? "#" : (r == 1 ? "a" : "."));
    }
    std::printf("   (# damage, a armour, . flew past; one character per 0.045 of height, the middle is the weak point)\n");
    return anyDamage;
}

int main()
{
    Player player = MakePlayer();
    GameBoss::Initialize();
    GameBoss::Start();
    for (int i = 0; i < 2000 && !(GameBoss::IsBattleStarted() && !GameBoss::IsTransitioning()); ++i) GameBoss::Update(player);
    Freeze(player);
    bool ok = true;
    ok &= Row(1, "AWAKENING");
    GameBoss::TakeDamage(9999);
    for (int i = 0; i < 2000 && !(GameBoss::GetPhase() == 2 && !GameBoss::IsTransitioning()); ++i) GameBoss::Update(player);
    Freeze(player);
    ok &= Row(2, "SPLIT (the hull and its drones)");
    GameBoss::TakeDamage(9999); // the hull: its bar is what ends the phase
    for (int i = 0; i < 2000 && !(GameBoss::GetPhase() == 3 && !GameBoss::IsTransitioning()); ++i) GameBoss::Update(player);
    Freeze(player);
    GameBoss::TakeDamage(9999); // phase 3: the shell of the core opens when both drones are down (the damage of a module goes to a drone while it is closed)
    GameBoss::TakeDamage(9999);
    Freeze(player);
    ok &= Row(3, "CORE (the shell open: both drones down)");
    std::printf(ok ? "every phase can be hurt by plain shots\n" : "THERE IS A PHASE THAT PLAIN SHOTS CANNOT HURT\n");
    return ok ? 0 : 1;
}
