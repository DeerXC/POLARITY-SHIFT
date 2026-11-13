#include "rogue/ModulesDrawInternal.h"
#include "ui/DemoKit.h"
#include "ui/UiKit.h"

#include <algorithm>
#include <cmath>
#include <string>

// The demo of a module or a tactic: a short looping animation in the detail panel of the hangar (a strip of about 380 x 84 px), drawn from the flat shapes of DemoKit. One scene for
// each of the 15 modules and the 3 tactics. They are DIAGRAMS of what the card says, with the numbers of the game in them (Balance.h): the ship, the bullets, an enemy and the
// one thing that changes. The text in them is numbers and signs only.
namespace Mod
{
namespace
{
namespace D = DemoKit;
using D::Clamp01;
using D::Ease;
using D::Seg01;

D::Color A(D::Color c, const D::Scene& s, float k = 1.0f)
{
    return D::Fade(c, s.a * k);
}

std::string Pct(float v)
{
    return std::to_string(static_cast<int>(std::lround(v)));
}

// where a ring of radius r is crossed by a horizontal lane dy away from its centre (the bullet is taken there)
float Reach(float cx, float r, float dy)
{
    const float q = r * r - dy * dy;
    return cx + (q > 0.0f ? std::sqrt(q) : 0.0f);
}

// ---------------------------------------------------------------- absorb ---------------------------------------------------------------------------------------------------------------
// SIPHON: ABSORB RANGE +90%. The dashed ring is the normal reach; after 80 frames the ring grows to 1.9 times and takes the bullet that used to pass.
void SceneSiphon(const D::Scene& s)
{
    const float sx = s.x + 64.0f, cy = s.cy(), t = s.t;
    const float r0 = 15.0f, r1 = r0 * 1.9f;
    const float R = r0 + (r1 - r0) * Ease(Seg01(t, 80.0f, 98.0f));
    const bool wide = t >= 80.0f;
    D::DashedRing(sx, cy, r0, 1.2f, A(D::WHITE, s, 0.5f), 10);
    D::Ring(sx, cy, wide ? R : r0, wide ? 2.2f : 1.6f, A(D::BLUE, s, wide ? 0.95f : 0.55f));
    D::Ship(sx, cy, D::BLUE, s.a);
    const float from = sx + 130.0f;
    // the near bullet is taken, the far one passes - in the second half both are taken
    D::Fly(s, D::Flight{4.0f, cy - 4.0f, D::BLUE, false, true, Reach(sx, r0, 4.0f), from}, sx, cy, 3.4f);
    D::Fly(s, D::Flight{18.0f, cy + 24.0f, D::BLUE, false, false, 0.0f, from}, sx, cy, 3.4f);
    D::Fly(s, D::Flight{104.0f, cy - 4.0f, D::BLUE, false, true, Reach(sx, r1, 4.0f), from}, sx, cy, 3.4f);
    D::Fly(s, D::Flight{114.0f, cy + 24.0f, D::BLUE, false, true, Reach(sx, r1, 24.0f), from}, sx, cy, 3.4f);
    D::Fly(s, D::Flight{124.0f, cy - 22.0f, D::BLUE, false, true, Reach(sx, r1, 22.0f), from}, sx, cy, 3.4f);
    D::Num(wide ? "+90%" : "+0%", s.right() - 46.0f, s.y + 18.0f, 14.0f, wide ? D::GOLD : D::DIM, s.a);
}

// ECHO: EVERY 4 ABSORBS AN ARC THAT JUMPS TO 2 MORE ENEMIES. Four bullets are taken, the fourth pip fires the arc: 2, then 1.4, then 1 shots of damage.
void SceneEcho(const D::Scene& s)
{
    const float sx = s.x + 36.0f, sy = s.y + 58.0f, t = s.t;
    const float ex[3] = {s.x + 150.0f, s.x + 236.0f, s.x + 316.0f};
    const float ey[3] = {s.y + 22.0f, s.y + 30.0f, s.y + 20.0f};
    const float arcT[3] = {94.0f, 102.0f, 110.0f};
    int taken = 0;
    for (int i = 0; i < 4; ++i)
    {
        const D::Flight f{4.0f + 18.0f * static_cast<float>(i), sy, D::BLUE, false, true, sx + 20.0f, sx + 120.0f};
        D::Fly(s, f, sx, sy, 3.2f);
        if (t >= D::FlightHitTime(s, f, 3.2f)) ++taken;
    }
    D::Pips(s.x + 16.0f, s.y + 12.0f, 4, t >= 94.0f ? 0.0f : static_cast<float>(taken), 12.0f, 3.6f, D::BLUE, s.a);
    D::Ship(sx, sy, D::BLUE, s.a);
    const float bx[4] = {sx + 12.0f, ex[0], ex[1], ex[2]};
    const float by[4] = {sy, ey[0], ey[1], ey[2]};
    const char* dmg[3] = {"2", "1.4", "1"};
    for (int k = 0; k < 3; ++k)
    {
        const float flash = D::Pulse(t, arcT[k], arcT[k] + 12.0f);
        D::Enemy(ex[k], ey[k], D::BLUE, flash, s.a);
        const float seg = Seg01(t, arcT[k] - 8.0f, arcT[k]);
        const float fade = 1.0f - Seg01(t, 112.0f, 130.0f);
        if (seg > 0.0f && fade > 0.0f)
        {
            const float qx = bx[k] + (ex[k] - bx[k]) * seg, qy = by[k] + (ey[k] - by[k]) * seg;
            D::Bolt(bx[k], by[k], qx, qy, static_cast<int>(t * 0.5f) + k, 2.2f - 0.5f * static_cast<float>(k), A(D::WHITE, s, fade));
        }
        if (t >= arcT[k] && t < arcT[k] + 26.0f) D::Num(dmg[k], ex[k], ey[k] - 19.0f, 12.0f, D::GOLD, s.a * (1.0f - Seg01(t, arcT[k] + 12.0f, arcT[k] + 26.0f)));
    }
}

// OVERFLOW: FULL BAR, ABSORBED BULLETS ARE STORED; THE NEXT BEAM IS LONGER AND HARDER. The bar is full: six bullets go into the store, the beam is 24% longer than the plain one.
void SceneOverflow(const D::Scene& s)
{
    const float sx = s.x + 40.0f, sy = s.cy() + 8.0f, t = s.t;
    D::Bar(s.x + 12.0f, s.y + 10.0f, 84.0f, 6.0f, t < 112.0f ? 1.0f : 0.0f, D::GOLD, s.a);
    int stored = 0;
    for (int i = 0; i < 6; ++i)
    {
        const D::Flight f{4.0f + 14.0f * static_cast<float>(i), sy, D::BLUE, false, true, sx + 18.0f, sx + 110.0f};
        D::Fly(s, f, sx, sy, 3.2f);
        const float hit = D::FlightHitTime(s, f, 3.2f);
        D::EnergyMote(s, hit + 4.0f, hit + 14.0f, sx, sy, s.x + 14.0f + 12.0f * static_cast<float>(i), s.y + 26.0f, D::GOLD);
        if (t >= hit + 14.0f) ++stored;
    }
    D::Pips(s.x + 14.0f, s.y + 26.0f, 6, t < 112.0f ? static_cast<float>(stored) : 0.0f, 12.0f, 3.4f, D::GOLD, s.a);
    D::Ship(sx, sy, D::BLUE, s.a);
    // the plain beam is 150 px long (the tick); with six stored bullets it is 150 * 1.24
    const float plain = 150.0f, len = plain * 1.24f;
    const float k = Ease(Seg01(t, 112.0f, 124.0f)) * (1.0f - Seg01(t, 152.0f, 170.0f));
    if (t >= 112.0f)
    {
        D::Line(sx + 12.0f + plain, sy - 11.0f, sx + 12.0f + plain, sy + 11.0f, 1.2f, A(D::WHITE, s, 0.55f));
        D::Beam(sx + 12.0f, sy, len * Ease(Seg01(t, 112.0f, 124.0f)), 4.5f, D::WHITE, D::GOLD, s.a * (1.0f - Seg01(t, 152.0f, 170.0f)));
        D::Num("+24%", sx + 12.0f + len - 18.0f, sy - 19.0f, 13.0f, D::GOLD, s.a * k);
    }
}

// MOMENTUM: EACH ABSORB FIRE RATE +8%, MAX +64%. The pips fill with every absorbed bullet and the shots come closer together; after 4 s without an absorb the stacks fade.
void SceneMomentum(const D::Scene& s)
{
    const float sx = s.x + 36.0f, cy = s.cy() + 4.0f, t = s.t;
    float stacks = 0.0f;
    for (int i = 0; i < 8; ++i)
    {
        const D::Flight f{2.0f + 10.0f * static_cast<float>(i), cy - 17.0f, D::RED, false, true, sx + 16.0f, sx + 100.0f};
        D::Fly(s, f, sx, cy, 3.2f);
        if (t >= D::FlightHitTime(s, f, 3.2f)) stacks += 1.0f;
    }
    if (t >= 124.0f) stacks = std::max(0.0f, 8.0f - (t - 124.0f) / 7.0f);
    D::Ship(sx, cy, D::RED, s.a);
    // the shots: the spacing between them shrinks with the stacks (15 frames between two shots, 8% faster per stack)
    const float spacing = 3.0f * 15.0f / (1.0f + 0.08f * stacks);
    const float phase = std::fmod(t * 3.0f, spacing);
    for (int k = 0; k < 9; ++k)
    {
        const float x = sx + 16.0f + phase + spacing * static_cast<float>(k);
        if (x < s.right() - 24.0f) D::Shot(x, cy + 6.0f, D::RED, s.a);
    }
    D::Pips(s.x + 14.0f, s.bottom() - 11.0f, 8, stacks, 11.0f, 3.2f, D::GOLD, s.a);
    D::Num("+" + Pct(stacks * 8.0f) + "%", s.x + 124.0f, s.bottom() - 11.0f, 13.0f, stacks >= 7.5f ? D::GOLD : D::DIM, s.a, false);
}

// DEFLECT: A WHITE BULLET THAT GRAZES IS ABSORBED (85%) FOR TRIPLE ENERGY. The first white bullet grazes the ship and is taken for three bits of energy; the second passes far away.
void SceneDeflect(const D::Scene& s)
{
    const float sx = s.x + 74.0f, cy = s.cy(), t = s.t;
    D::Ship(sx, cy, D::BLUE, s.a);
    const D::Flight a{6.0f, cy - 4.0f, D::WHITE, true, true, sx + 17.0f, sx + 160.0f};
    const D::Flight b{58.0f, cy - 31.0f, D::WHITE, true, false, 0.0f, sx + 160.0f};
    const D::Flight c{96.0f, cy + 4.0f, D::WHITE, true, true, sx + 17.0f, sx + 160.0f};
    D::Fly(s, a, sx, cy, 3.4f);
    D::Fly(s, b, sx, cy, 3.4f);
    D::Fly(s, c, sx, cy, 3.4f);
    const float hits[2] = {D::FlightHitTime(s, a, 3.4f), D::FlightHitTime(s, c, 3.4f)};
    for (float hit : hits)
    {
        for (int k = 0; k < 2; ++k) // two more bits besides the one that every absorb makes: three in all
            D::EnergyMote(s, hit + 2.0f + 3.0f * static_cast<float>(k), hit + 12.0f + 3.0f * static_cast<float>(k), sx + 17.0f, cy - 4.0f + 8.0f * static_cast<float>(k) - 4.0f, sx, cy);
        if (t >= hit && t < hit + 30.0f)
        {
            D::Ring(sx + 17.0f, cy, 6.0f + (t - hit) * 0.9f, 1.6f, A(D::WHITE, s, 1.0f - (t - hit) / 30.0f));
            D::Num("x3", sx, cy - 24.0f, 14.0f, D::GOLD, s.a * (1.0f - Seg01(t, hit + 14.0f, hit + 30.0f)));
        }
    }
    D::Num("85%", s.x + 14.0f, s.y + 14.0f, 12.0f, D::DIM, s.a, false);
}

// ---------------------------------------------------------------- break ----------------------------------------------------------------------------------------------------------------
// EMBER: A FIRE BAND AFTER THE SPECIAL; THE FIRE BURNS BULLETS INTO ENERGY. The beam leaves a band of fire; the bullets that fly into it become energy and the enemy in it burns.
void SceneEmber(const D::Scene& s)
{
    const float sx = s.x + 34.0f, cy = s.cy() + 2.0f, t = s.t;
    const float beamLen = 330.0f * Ease(Seg01(t, 6.0f, 20.0f));
    if (t < 34.0f) D::Beam(sx + 12.0f, cy, beamLen, 4.0f, D::WHITE, D::BLUE, s.a * (1.0f - Seg01(t, 24.0f, 34.0f)));
    D::Ship(sx, cy, D::BLUE, s.a);
    const float life = Seg01(t, 24.0f, 30.0f) * (1.0f - Seg01(t, 150.0f, 172.0f));
    const float bx0 = sx + 16.0f, bx1 = s.right() - 6.0f;
    if (life > 0.0f)
    {
        D::Box(bx0, cy - 10.0f, bx1 - bx0, 20.0f, A(D::FIRE, s, 0.20f * life));
        D::Line(bx0, cy - 10.0f, bx1, cy - 10.0f, 1.2f, A(D::FIRE, s, 0.8f * life));
        D::Line(bx0, cy + 10.0f, bx1, cy + 10.0f, 1.2f, A(D::FIRE, s, 0.8f * life));
        for (int i = 0; i < 16; ++i)
        {
            const float rise = std::fmod(t * 0.55f + static_cast<float>(i) * 3.7f, 16.0f);
            D::Dia(bx0 + 12.0f + static_cast<float>(i) * 21.0f, cy + 8.0f - rise, 2.6f * (1.0f - rise / 18.0f), A(D::FIRE, s, (0.9f - rise / 20.0f) * life));
        }
    }
    const float ex = s.x + 338.0f;
    D::Enemy(ex, cy, D::RED, D::Pulse(std::fmod(t, 30.0f), 0.0f, 8.0f) * (t > 36.0f && t < 160.0f ? 1.0f : 0.0f), s.a);
    // bullets that fly into the fire burn into energy, which flies to the bar
    for (int i = 0; i < 3; ++i)
    {
        const float by = cy - 2.0f + 4.0f * static_cast<float>(i % 2), hitX = s.x + 262.0f - 40.0f * static_cast<float>(i);
        const D::Flight f{40.0f + 22.0f * static_cast<float>(i), by, i % 2 ? D::BLUE : D::RED, false, true, hitX, ex - 16.0f};
        D::Fly(s, f, hitX, by, 3.6f); // (taken where it is: it burns, it does not fly to the ship)
        const float hit = D::FlightHitTime(s, f, 3.6f);
        D::EnergyMote(s, hit + 6.0f, hit + 30.0f, hitX, by, s.x + 54.0f, s.y + 14.0f);
    }
    D::Bar(s.x + 12.0f, s.y + 10.0f, 84.0f, 6.0f, 0.15f + 0.22f * Seg01(t, 56.0f, 130.0f), D::GREEN, s.a);
}

// BREACH: IGNORES ALL DAMAGE REDUCTION. A shielded elite takes 75% of a shot (the spark is stopped by the shield); with BREACH the shot goes through at 100%.
void SceneBreach(const D::Scene& s)
{
    const float sx = s.x + 34.0f, cy = s.cy(), t = s.t, ex = s.x + 262.0f;
    D::Ship(sx, cy, D::BLUE, s.a);
    const bool breach = t >= 92.0f;
    const float flash = breach ? D::Pulse(std::fmod(t, 14.0f), 0.0f, 6.0f) : 0.0f;
    D::Enemy(ex, cy, D::GOLD, flash, s.a, 1.25f);
    if (!breach)
        D::Hex(ex, cy, 20.0f, 2.0f, A(D::GOLD, s, 0.9f));
    else
    {
        D::Hex(ex, cy, 20.0f, 1.2f, A(D::GOLD, s, 0.35f));
        D::Line(ex - 18.0f, cy - 18.0f, ex + 18.0f, cy + 18.0f, 1.4f, A(D::RED, s, 0.8f)); // the shield is cut
    }
    for (int k = 0; k < 6; ++k)
    {
        const float x = sx + 16.0f + std::fmod(t * 3.6f + static_cast<float>(k) * 40.0f, 240.0f);
        if (x < ex - (breach ? 12.0f : 21.0f))
            D::Shot(x, cy, D::BLUE, s.a);
        else if (!breach && x < ex - 14.0f)
            D::Dia(ex - 22.0f, cy + ((k % 2) ? 5.0f : -5.0f), 2.4f, A(D::WHITE, s, 0.8f)); // the spark on the shield
    }
    D::Num(breach ? "100%" : "75%", ex, s.y + 12.0f, 14.0f, breach ? D::GOLD : D::DIM, s.a);
}

// TWIN: A HALF BAR FIRES A HALF BEAM (COSTS HALF A BAR, 75% DAMAGE). The bar is at half; the half beam takes it to zero and is half as long, three quarters as hard.
void SceneTwin(const D::Scene& s)
{
    const float sx = s.x + 34.0f, cy = s.cy() + 8.0f, t = s.t;
    const float fill = 0.5f * (1.0f - Seg01(t, 26.0f, 38.0f));
    D::Bar(s.x + 12.0f, s.y + 12.0f, 90.0f, 6.0f, fill, D::GOLD, s.a);
    D::Line(s.x + 57.0f, s.y + 8.0f, s.x + 57.0f, s.y + 22.0f, 1.2f, A(D::WHITE, s, 0.65f));
    D::Num("1/2", s.x + 118.0f, s.y + 15.0f, 13.0f, D::GOLD, s.a);
    D::Ship(sx, cy, D::BLUE, s.a);
    // the full beam as a dashed outline for comparison
    for (int i = 0; i < 12; ++i)
    {
        const float x = sx + 12.0f + 22.0f * static_cast<float>(i);
        D::Line(x, cy - 7.0f, x + 12.0f, cy - 7.0f, 1.0f, A(D::WHITE, s, 0.28f));
        D::Line(x, cy + 7.0f, x + 12.0f, cy + 7.0f, 1.0f, A(D::WHITE, s, 0.28f));
    }
    if (t >= 26.0f)
    {
        D::Beam(sx + 12.0f, cy, 120.0f * Ease(Seg01(t, 26.0f, 36.0f)), 3.0f, D::WHITE, D::GOLD, s.a * (1.0f - Seg01(t, 96.0f, 116.0f)));
        D::Num("75%", sx + 12.0f + 120.0f + 22.0f, cy, 13.0f, D::GOLD, s.a * Seg01(t, 40.0f, 50.0f) * (1.0f - Seg01(t, 96.0f, 116.0f)));
    }
}

// AFTERGLOW: AFTER THE BEAM, FIRE RATE +40% AND SHOT DAMAGE +40% FOR 4 S. After the beam the shots turn gold and come closer together while the 4 s timer runs down.
void SceneAfterglow(const D::Scene& s)
{
    const float sx = s.x + 34.0f, cy = s.cy() - 2.0f, t = s.t;
    const bool glow = t >= 48.0f && t < 160.0f;
    if (t >= 30.0f && t < 48.0f) D::Beam(sx + 12.0f, cy, 330.0f * Ease(Seg01(t, 30.0f, 38.0f)), 4.0f, D::WHITE, D::BLUE, s.a * (1.0f - Seg01(t, 40.0f, 48.0f)));
    D::Ship(sx, cy, glow ? D::GOLD : D::BLUE, s.a);
    if (t < 30.0f || t >= 48.0f)
    {
        const float spacing = glow ? 3.0f * 15.0f / 1.4f : 45.0f;
        const float phase = std::fmod(t * 3.0f, spacing);
        for (int k = 0; k < 9; ++k)
        {
            const float x = sx + 16.0f + phase + spacing * static_cast<float>(k);
            if (x >= s.right() - 40.0f) continue;
            D::Shot(x, cy, glow ? D::GOLD : D::BLUE, s.a);
            if (glow) D::Line(x - 6.0f, cy - 3.0f, x + 6.0f, cy - 3.0f, 1.0f, A(D::GOLD, s, 0.5f));
        }
    }
    D::Enemy(s.x + 352.0f, cy, D::RED, glow ? D::Pulse(std::fmod(t, 8.0f), 0.0f, 5.0f) : 0.0f, s.a);
    if (t >= 48.0f)
    {
        D::Bar(s.x + 14.0f, s.bottom() - 11.0f, 130.0f, 5.0f, 1.0f - Seg01(t, 48.0f, 160.0f), D::GOLD, s.a);
        D::Num("4 S", s.x + 160.0f, s.bottom() - 11.0f, 12.0f, D::DIM, s.a, false);
        D::Num("+40%", sx + 30.0f, s.y + 14.0f, 13.0f, D::GOLD, s.a * (1.0f - Seg01(t, 140.0f, 160.0f)));
    }
}

// FOCUS: HITS ON ONE TARGET +4% EACH, UP TO +40%, RESET ON A NEW TARGET. The brackets close in on one enemy while the stacks rise; when the ship turns to the other target they start again.
void SceneFocus(const D::Scene& s)
{
    const float cy = s.cy(), t = s.t, ex = s.x + 270.0f;
    const bool second = t >= 140.0f;
    const float tyA = cy - 16.0f, tyB = cy + 16.0f;
    const float move = Ease(Seg01(t, 136.0f, 150.0f));
    const float sy = tyA + (tyB - tyA) * move;
    const float sx = s.x + 34.0f;
    D::Ship(sx, sy, D::GOLD, s.a);
    const float hitFlash = D::Pulse(std::fmod(t, 9.0f), 0.0f, 5.0f);
    D::Enemy(ex, tyA, D::RED, !second ? hitFlash : 0.0f, s.a);
    D::Enemy(ex, tyB, D::RED, second ? hitFlash : 0.0f, s.a);
    float stacks = !second ? std::floor(Seg01(t, 8.0f, 128.0f) * 10.0f) : std::floor(Seg01(t, 156.0f, 200.0f) * 4.0f);
    if (t > 134.0f && t < 156.0f) stacks = 0.0f;
    const float ty = second ? tyB : tyA;
    const float close = !second ? Seg01(t, 8.0f, 128.0f) : Seg01(t, 156.0f, 200.0f) * 0.4f;
    D::Brackets(ex, ty, 24.0f - 10.0f * close, 1.6f, A(D::WHITE, s, 0.95f));
    for (int k = 0; k < 7; ++k)
    {
        const float x = sx + 16.0f + std::fmod(t * 3.6f + static_cast<float>(k) * 36.0f, ex - sx - 34.0f);
        D::Shot(x, sy, D::GOLD, s.a);
    }
    D::Pips(s.x + 14.0f, s.bottom() - 11.0f, 10, stacks, 11.0f, 3.2f, D::GOLD, s.a);
    D::Num("+" + Pct(stacks * 4.0f) + "%", s.x + 134.0f, s.bottom() - 11.0f, 13.0f, stacks >= 10.0f ? D::GOLD : D::DIM, s.a, false);
}

// ---------------------------------------------------------------- survive --------------------------------------------------------------------------------------------------------------
// BACKUP CORE: REVIVE ONCE WITH HALF THE HP AND A BLAST OVER THE WHOLE SCREEN. The ship is lost, comes back with half its HP and a blast that hits every enemy and turns the bullets into energy.
void SceneBackupCore(const D::Scene& s)
{
    const float cx = s.x + 80.0f, cy = s.cy(), t = s.t;
    const float dead = Seg01(t, 26.0f, 30.0f) * (1.0f - Seg01(t, 50.0f, 54.0f));
    const float hp = t < 26.0f ? 1.0f : (t < 56.0f ? 0.0f : 0.5f);
    D::Bar(s.right() - 104.0f, s.y + 12.0f, 88.0f, 6.0f, hp, hp > 0.0f ? D::GREEN : D::RED, s.a);
    D::Num(t >= 56.0f ? "50%" : "", s.right() - 60.0f, s.y + 28.0f, 12.0f, D::GREEN, s.a);
    const float blast = Seg01(t, 56.0f, 120.0f);
    const float R = blast * 150.0f;
    const float fade = 1.0f - Seg01(t, 120.0f, 140.0f);
    const float ex[2] = {s.x + 200.0f, s.x + 290.0f};
    for (int i = 0; i < 2; ++i) D::Enemy(ex[i], cy + (i ? 12.0f : -12.0f), D::RED, D::Pulse(t, 56.0f + (ex[i] - cx) / 150.0f * 64.0f, 70.0f + (ex[i] - cx) / 150.0f * 64.0f), s.a * (t > 130.0f ? 0.5f : 1.0f));
    const float ang[8] = {0.4f, -0.5f, 2.6f, -2.4f, 0.0f, 3.1f, -1.2f, 1.4f};
    const float dist[8] = {30.0f, 36.0f, 44.0f, 52.0f, 70.0f, 78.0f, 64.0f, 58.0f};
    for (int i = 0; i < 8; ++i)
    {
        const float px = cx + std::cos(ang[i]) * dist[i] * 1.6f, py = cy + std::sin(ang[i]) * dist[i] * 0.4f;
        const float dd = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        const float taken = Clamp01((R - dd) / 9.0f) * (blast > 0.0f ? 1.0f : 0.0f);
        const float qx = px + (cx - px) * Ease(taken), qy = py + (cy - py) * Ease(taken);
        if (taken >= 1.0f) continue;
        D::Bullet(qx, qy, i % 2 ? D::RED : D::BLUE, false, 1.0f - 0.85f * taken, s.a * (1.0f - 0.7f * taken));
        if (taken > 0.05f) D::Dia(qx, qy, 2.0f, A(D::GREEN, s));
    }
    if (blast > 0.0f && fade > 0.0f) D::Ring(cx, cy, R, 2.6f, A(D::GOLD, s, fade));
    if (t < 26.0f || t >= 56.0f) D::Ship(cx, cy, D::GOLD, s.a * (t >= 56.0f ? Seg01(t, 56.0f, 62.0f) : 1.0f));
    if (dead > 0.0f) D::Cross(cx, cy, 8.0f, 2.4f, A(D::RED, s, dead));
}

// PULSE: EVERY 6 S A SHOCKWAVE: BULLETS TO ENERGY. A ring of the 6 s timer closes around the ship, then the shockwave runs out and turns the bullets around the ship into energy.
void ScenePulse(const D::Scene& s)
{
    const float cx = s.x + 84.0f, cy = s.cy(), t = s.t;
    const float timer = t < 100.0f ? Seg01(t, 0.0f, 100.0f) : Seg01(t, 150.0f, 200.0f) * 0.5f;
    D::Arc(cx, cy, 25.0f, 2.2f, 0.0f, std::max(0.001f, timer), A(D::GOLD, s, 0.9f));
    D::Num("6 S", s.x + 150.0f, s.y + 16.0f, 13.0f, D::DIM, s.a);
    const float wave = Seg01(t, 100.0f, 150.0f);
    const float R = 24.0f + wave * 70.0f;
    if (wave > 0.0f && wave < 1.0f) D::Ring(cx, cy, R, 2.4f, A(D::WHITE, s, 1.0f - wave));
    const float ang[8] = {0.3f, 1.1f, 2.0f, 2.9f, 3.8f, 4.7f, 5.5f, 6.1f};
    const float dist[8] = {48.0f, 62.0f, 40.0f, 70.0f, 52.0f, 44.0f, 66.0f, 56.0f};
    for (int i = 0; i < 8; ++i)
    {
        const float drift = std::min(t, 100.0f) * 0.10f; // the bullets creep in
        const float d = dist[i] - drift;
        const float px = cx + std::cos(ang[i]) * d * 1.4f, py = cy + std::sin(ang[i]) * d * 0.46f;
        const float taken = Clamp01((R - dist[i] + drift) / 8.0f) * (wave > 0.0f ? 1.0f : 0.0f);
        const float qx = px + (cx - px) * Ease(taken), qy = py + (cy - py) * Ease(taken);
        if (taken >= 1.0f) continue;
        D::Bullet(qx, qy, i % 2 ? D::RED : D::BLUE, false, 1.0f - 0.8f * taken, s.a * (1.0f - 0.6f * taken));
        if (taken > 0.05f) D::Dia(qx, qy, 2.0f, A(D::GREEN, s));
    }
    D::Ship(cx, cy, D::BLUE, s.a);
}

// AEGIS: BLOCKS ONE WHITE BULLET OR BEAM; RECHARGES EVERY 40 ABSORBS. The shield takes the white bullet and breaks; absorbed bullets charge it again (the five pips stand for the 40).
void SceneAegis(const D::Scene& s)
{
    const float sx = s.x + 72.0f, cy = s.cy(), t = s.t;
    const bool up = t < 46.0f || t >= 152.0f;
    const float back = Seg01(t, 152.0f, 168.0f);
    if (up) D::Hex(sx, cy, 20.0f, 2.2f, A(D::BLUE, s, t < 46.0f ? 0.95f : back));
    D::Ship(sx, cy, D::BLUE, s.a);
    const D::Flight w{8.0f, cy - 2.0f, D::WHITE, true, true, sx + 21.0f, sx + 110.0f};
    D::Fly(s, w, sx, cy, 3.0f);
    const float hit = D::FlightHitTime(s, w, 3.0f);
    if (t >= hit && t < hit + 20.0f)
    {
        const float k = (t - hit) / 20.0f;
        for (int i = 0; i < 6; ++i)
        {
            const float a = 1.0472f * static_cast<float>(i) + 0.3f;
            D::Line(sx + std::cos(a) * (20.0f + 16.0f * k), cy + std::sin(a) * (18.0f + 14.0f * k), sx + std::cos(a) * (26.0f + 18.0f * k), cy + std::sin(a) * (23.0f + 16.0f * k), 1.8f, A(D::BLUE, s, 1.0f - k));
        }
    }
    int charged = 0;
    for (int i = 0; i < 5; ++i)
    {
        const D::Flight f{72.0f + 15.0f * static_cast<float>(i), cy + 2.0f, i % 2 ? D::RED : D::BLUE, false, true, sx + 17.0f, sx + 90.0f};
        D::Fly(s, f, sx, cy, 3.2f);
        if (t >= D::FlightHitTime(s, f, 3.2f)) ++charged;
    }
    if (t < 152.0f && t >= 46.0f) D::Pips(s.x + 14.0f, s.bottom() - 11.0f, 5, static_cast<float>(charged), 12.0f, 3.4f, D::BLUE, s.a);
    D::Num("40", s.x + 82.0f, s.bottom() - 11.0f, 12.0f, D::DIM, s.a, false);
}

// MEND: 5 S WITHOUT A HIT: 1 HP EVERY 1.5 S. A hit takes HP away; the 5 s ring closes without another hit, then the bar repairs itself a point at a time.
void SceneMend(const D::Scene& s)
{
    const float cy = s.cy(), t = s.t;
    const float bx = s.x + 28.0f, bw = 252.0f;
    float hp = 0.62f;
    if (t >= 16.0f) hp = 0.42f;
    if (t >= 110.0f) hp = 0.42f + 0.04f * std::floor((t - 110.0f) / 14.0f);
    D::Box(bx, cy - 5.0f, bw, 10.0f, D::Rgba(1.0f, 1.0f, 1.0f, 0.12f * s.a));
    D::Box(bx, cy - 5.0f, bw * hp, 10.0f, A(t >= 16.0f && t < 24.0f ? D::RED : D::GREEN, s, 0.92f));
    for (int i = 1; i < 10; ++i) D::Line(bx + bw * static_cast<float>(i) / 10.0f, cy - 5.0f, bx + bw * static_cast<float>(i) / 10.0f, cy + 5.0f, 1.0f, A(D::Rgba(0.0f, 0.0f, 0.0f, 1.0f), s, 0.35f));
    if (t >= 16.0f && t < 40.0f) D::Num("-", bx + bw * hp + 12.0f, cy, 16.0f, D::RED, s.a * (1.0f - Seg01(t, 28.0f, 40.0f)));
    const float cx = s.right() - 46.0f;
    D::Ring(cx, cy, 16.0f, 1.0f, A(D::WHITE, s, 0.25f));
    if (t >= 20.0f) D::Arc(cx, cy, 16.0f, 2.4f, 0.0f, std::max(0.001f, Seg01(t, 20.0f, 110.0f)), A(D::GOLD, s, 0.95f));
    D::Num("5 S", cx, cy, 11.0f, D::DIM, s.a);
    if (t >= 110.0f)
    {
        const float k = std::fmod(t - 110.0f, 14.0f) / 14.0f;
        D::Num("+", bx + bw * hp + 6.0f, cy - 12.0f - 8.0f * k, 14.0f, D::GREEN, s.a * (1.0f - k));
    }
}

// BACKWATER: UNDER HALF HP ABSORB ENERGY +20%, THE BEAM NEEDS 15 LESS, LONGER INVINCIBILITY. HP under the half mark: the screen edge glows, the energy bar fills 20% faster and the beam is ready at 85.
void SceneBackwater(const D::Scene& s)
{
    const float t = s.t;
    const float glow = 0.14f + 0.12f * std::sin(t * 0.14f);
    D::Box(s.x + 1.0f, s.y + 1.0f, s.w - 2.0f, 3.0f, A(D::RED, s, glow * 2.0f));
    D::Box(s.x + 1.0f, s.bottom() - 4.0f, s.w - 2.0f, 3.0f, A(D::RED, s, glow * 2.0f));
    D::Bar(s.x + 16.0f, s.y + 14.0f, 170.0f, 8.0f, 0.38f, D::RED, s.a);
    D::Line(s.x + 16.0f + 85.0f, s.y + 10.0f, s.x + 16.0f + 85.0f, s.y + 26.0f, 1.4f, A(D::WHITE, s, 0.7f));
    const float rows[2] = {s.y + 46.0f, s.y + 64.0f};
    const float plain = Seg01(t, 10.0f, 130.0f), fast = Seg01(t, 10.0f, 130.0f / 1.2f);
    D::Bar(s.x + 16.0f, rows[0] - 3.0f, 250.0f, 6.0f, plain, D::BLUE, s.a);
    D::Bar(s.x + 16.0f, rows[1] - 3.0f, 250.0f, 6.0f, fast, D::GOLD, s.a);
    D::Line(s.x + 16.0f + 250.0f * 0.85f, rows[1] - 7.0f, s.x + 16.0f + 250.0f * 0.85f, rows[1] + 7.0f, 1.4f, A(D::WHITE, s, 0.7f));
    D::Line(s.x + 16.0f + 250.0f, rows[0] - 7.0f, s.x + 16.0f + 250.0f, rows[0] + 7.0f, 1.4f, A(D::WHITE, s, 0.7f));
    D::Num("85", s.x + 16.0f + 250.0f * 0.85f, rows[1] - 12.0f, 11.0f, fast >= 0.85f ? D::GOLD : D::DIM, s.a);
    D::Num("100", s.x + 16.0f + 250.0f, rows[0] - 12.0f, 11.0f, D::DIM, s.a);
    D::Num("+20%", s.x + 300.0f, rows[1], 13.0f, D::GOLD, s.a);
    D::Num("-15", s.x + 300.0f, rows[0], 13.0f, D::DIM, s.a);
}

// ---------------------------------------------------------------- tactics --------------------------------------------------------------------------------------------------------------
// DECOY: DROP A DECOY THAT PULLS AND ABSORBS BULLETS OF YOUR COLOR. The decoy stays 3 s; the bullets of the ship's colour bend towards it and are taken for half their energy.
void SceneDecoy(const D::Scene& s)
{
    const float cy = s.cy(), t = s.t;
    const float dx = s.x + 92.0f, dy = cy + 10.0f;
    const float move = Ease(Seg01(t, 14.0f, 32.0f));
    const float sx = dx + 56.0f * move, sy = dy - 26.0f * move;
    const bool active = t >= 14.0f && t < 196.0f;
    if (active)
    {
        const float fade = 1.0f - Seg01(t, 186.0f, 196.0f);
        D::DashedRing(dx, dy, 46.0f, 1.0f, A(D::BLUE, s, 0.35f * fade), 14);
        D::Ship(dx, dy, D::BLUE, s.a * 0.42f * fade, 1.2f);
        D::Ring(dx, dy, 20.0f, 1.4f, A(D::BLUE, s, 0.7f * fade));
        D::Arc(dx, dy, 25.0f, 2.2f, 0.0f, std::max(0.001f, 1.0f - Seg01(t, 14.0f, 196.0f)), A(D::GOLD, s, 0.9f * fade));
    }
    D::Ship(t < 14.0f ? dx : sx, t < 14.0f ? dy : sy, D::BLUE, s.a);
    // the bullets of the colour of the ship bend to the decoy
    const float lanes[5] = {-12.0f, 14.0f, 4.0f, -4.0f, 24.0f};
    for (int i = 0; i < 5; ++i)
    {
        const float t0 = 36.0f + 24.0f * static_cast<float>(i);
        if (t < t0) continue;
        const float x = s.right() - 10.0f - 3.2f * (t - t0);
        const float pullStart = dx + 70.0f;
        const float k = Seg01(pullStart - x, 0.0f, 62.0f); // 0 .. 1 as it flies in
        const float y = (cy + lanes[i]) + (dy - (cy + lanes[i])) * Ease(k);
        if (k >= 1.0f)
        {
            D::EnergyMote(s, t0 + (s.right() - 10.0f - pullStart) / 3.2f + 19.0f, t0 + (s.right() - 10.0f - pullStart) / 3.2f + 31.0f, dx, dy, sx, sy);
            continue;
        }
        D::Bullet(x, y, D::BLUE, false, 1.0f - 0.4f * k, s.a);
    }
    D::Num("50%", dx, s.y + 14.0f, 12.0f, D::GOLD, s.a * (active ? 1.0f : 0.0f));
}

// DASH: DASH WHERE YOU MOVE, INVINCIBLE WHILE DASHING, 2.5 S COOLDOWN. A wall of bullets comes in; the ship dashes through it and is not hurt; the cooldown bar fills.
void SceneDash(const D::Scene& s)
{
    const float cy = s.cy(), t = s.t;
    const float baseX = s.x + 120.0f;
    const float dash = Ease(Seg01(t, 62.0f, 72.0f));
    const float back = Ease(Seg01(t, 96.0f, 128.0f));
    const float sx = baseX + 70.0f * dash - 70.0f * back;
    const bool dashing = t >= 62.0f && t < 76.0f;
    // the wall: bullets in a column that flies in
    const float wallX = s.x + 340.0f - 2.8f * t;
    for (int i = -3; i <= 3; ++i)
    {
        if (wallX > s.x - 8.0f) D::Bullet(wallX, cy + static_cast<float>(i) * 11.0f, ((i + 3) % 2) ? D::RED : D::BLUE, false, 1.0f, s.a);
    }
    // the ghosts of the dash
    if (t >= 62.0f && t < 90.0f)
    {
        for (int g = 1; g <= 3; ++g)
        {
            const float gt = std::max(62.0f, t - 2.0f * static_cast<float>(g));
            const float gx = baseX + 70.0f * Ease(Seg01(gt, 62.0f, 72.0f));
            D::Ship(gx, cy, D::BLUE, s.a * (0.5f - 0.13f * static_cast<float>(g)) * (1.0f - Seg01(t, 76.0f, 90.0f)));
        }
    }
    D::Ship(sx, cy, D::BLUE, s.a * (dashing ? 0.7f : 1.0f));
    if (dashing) D::Ring(sx, cy, 16.0f, 1.6f, A(D::WHITE, s, 0.7f));
    D::Bar(s.x + 14.0f, s.bottom() - 11.0f, 100.0f, 5.0f, t < 62.0f ? 1.0f : (t < 72.0f ? 1.0f - Seg01(t, 62.0f, 72.0f) : Seg01(t, 72.0f, 222.0f)), D::GOLD, s.a);
    D::Num("2.5 S", s.x + 128.0f, s.bottom() - 11.0f, 12.0f, D::DIM, s.a, false);
}

// CHARGE SHOT: HOLD THE TACTIC KEY: CHARGE A BEAM; A FULL CHARGE BREAKS AN OVERLOAD. A short charge fires a thin beam; a full charge (2 s) fires a thick one that breaks the shield of the overload.
void SceneChargeShot(const D::Scene& s)
{
    const float sx = s.x + 34.0f, cy = s.cy() + 2.0f, t = s.t;
    const float ex = s.x + 336.0f;
    float charge = 0.0f;
    if (t >= 8.0f && t < 26.0f) charge = (t - 8.0f) / 120.0f;
    if (t >= 54.0f && t < 174.0f) charge = (t - 54.0f) / 120.0f;
    D::Ship(sx, cy, D::BLUE, s.a);
    if (charge > 0.0f)
    {
        D::Ring(sx + 14.0f, cy, 4.0f + 24.0f * charge, 1.6f, A(D::BLUE, s, 0.9f));
        D::Disc(sx + 14.0f, cy, 2.0f + 10.0f * charge, A(D::WHITE, s, 0.4f + 0.5f * charge));
    }
    const bool broken = t >= 176.0f;
    D::Enemy(ex, cy, D::GOLD, broken ? D::Pulse(t, 176.0f, 190.0f) : 0.0f, s.a, 1.2f);
    if (!broken)
        D::Hex(ex, cy, 19.0f, 2.0f, A(D::GOLD, s, 0.9f));
    else if (t < 192.0f)
    {
        const float k = (t - 176.0f) / 16.0f;
        for (int i = 0; i < 6; ++i)
        {
            const float a = 1.0472f * static_cast<float>(i);
            D::Line(ex + std::cos(a) * (19.0f + 14.0f * k), cy + std::sin(a) * (17.0f + 12.0f * k), ex + std::cos(a) * (25.0f + 16.0f * k), cy + std::sin(a) * (22.0f + 14.0f * k), 1.8f, A(D::GOLD, s, 1.0f - k));
        }
    }
    if (t >= 26.0f && t < 42.0f) D::Beam(sx + 12.0f, cy, 330.0f * Ease(Seg01(t, 26.0f, 32.0f)), 2.5f, D::WHITE, D::BLUE, s.a * (1.0f - Seg01(t, 36.0f, 42.0f)));
    if (t >= 174.0f && t < 196.0f) D::Beam(sx + 12.0f, cy, 330.0f * Ease(Seg01(t, 174.0f, 180.0f)), 7.0f, D::WHITE, D::BLUE, s.a * (1.0f - Seg01(t, 188.0f, 196.0f)));
    D::Bar(s.x + 14.0f, s.bottom() - 11.0f, 120.0f, 5.0f, charge, charge >= 0.99f ? D::GOLD : D::BLUE, s.a);
    D::Num("2 S", s.x + 148.0f, s.bottom() - 11.0f, 12.0f, D::DIM, s.a, false);
}
}

// the demo of a hangar id: a module (0 .. COUNT - 1) or a tactic (TACTIC_ID0 ..); anything else draws nothing
void DrawModuleDemo(int hangarId, float x, float y, float w, float h, int frame)
{
    const bool tactic = hangarId >= TACTIC_ID0 && hangarId < TALENT_ID0;
    if (!tactic && (hangarId < 0 || hangarId >= COUNT)) return;
    int loop = 200;
    if (tactic)
        loop = hangarId == TACTIC_ID0 + 1 ? 230 : (hangarId == TACTIC_ID0 + 2 ? 210 : 220);
    else if (hangarId == static_cast<int>(Id::Mend) || hangarId == static_cast<int>(Id::Backwater) || hangarId == static_cast<int>(Id::Focus) || hangarId == static_cast<int>(Id::Aegis))
        loop = 220;
    const D::Scene s = D::Begin(x, y, w, h, frame, loop);
    if (tactic)
    {
        switch (hangarId - TACTIC_ID0)
        {
        case 0: SceneDecoy(s); break;
        case 1: SceneDash(s); break;
        default: SceneChargeShot(s); break;
        }
        return;
    }
    switch (static_cast<Id>(hangarId))
    {
    case Id::Siphon: SceneSiphon(s); break;
    case Id::Echo: SceneEcho(s); break;
    case Id::Overflow: SceneOverflow(s); break;
    case Id::Momentum: SceneMomentum(s); break;
    case Id::Deflect: SceneDeflect(s); break;
    case Id::Ember: SceneEmber(s); break;
    case Id::Breach: SceneBreach(s); break;
    case Id::Twin: SceneTwin(s); break;
    case Id::Afterglow: SceneAfterglow(s); break;
    case Id::Focus: SceneFocus(s); break;
    case Id::BackupCore: SceneBackupCore(s); break;
    case Id::Pulse: ScenePulse(s); break;
    case Id::Aegis: SceneAegis(s); break;
    case Id::Mend: SceneMend(s); break;
    case Id::Backwater: SceneBackwater(s); break;
    default: break;
    }
}
}
