#include "rogue/Meta.h"
#include "rogue/ModulesDrawInternal.h"
#include "ui/DemoKit.h"
#include "ui/UiKit.h"

#include <algorithm>
#include <cmath>
#include <string>

// The demo of a synergy: a short looping animation in the detail panel of the hangar (380 x 84 px), drawn from the flat shapes of DemoKit - no texture, no text that needs
// translating (only numbers and signs). One scene for each synergy. They are DIAGRAMS of what the synergy does (with the numbers of Balance.h), not recordings of the game.
namespace Mod
{
namespace
{
namespace D = DemoKit;
using D::Clamp01;
using D::Ease;
using D::Seg01;

constexpr int LOOP = 210;

D::Color A(D::Color c, const D::Scene& s, float k = 1.0f)
{
    return D::Fade(c, s.a * k);
}

// GRAVITY WELL (SIPHON + PULSE): the shockwave reaches twice as far, and what it gives may be half as much again (22 -> 33 energy).
void GravityWell(const D::Scene& s)
{
    const float sx = s.x + 40.0f, cy = s.cy(), t = s.t;
    const float reach = 22.0f;
    const float ringT = Seg01(t, 10.0f, 104.0f);
    const float R = ringT * reach * 2.4f;
    const float fade = 1.0f - Seg01(t, 112.0f, 130.0f);
    D::DashedRing(sx, cy, reach, 1.2f, A(D::WHITE, s, 0.4f), 12);
    if (ringT > 0.0f && fade > 0.0f) D::Ring(sx, cy, R, 2.2f, A(D::GOLD, s, 0.95f * fade));
    const float ang[12] = {0.2f, 0.9f, 1.7f, 2.4f, 3.0f, 3.7f, 4.3f, 5.0f, 5.7f, 6.1f, 1.3f, 4.7f};
    const float dist[12] = {20.0f, 30.0f, 38.0f, 48.0f, 56.0f, 62.0f, 44.0f, 52.0f, 34.0f, 60.0f, 54.0f, 26.0f};
    float got = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float px = sx + std::cos(ang[i]) * dist[i] * 1.3f, py = cy + std::sin(ang[i]) * dist[i] * 0.55f;
        const float dd = std::sqrt((px - sx) * (px - sx) + (py - cy) * (py - cy));
        const float taken = Clamp01((R - dd) / 9.0f) * (ringT > 0.0f ? 1.0f : 0.0f);
        if (taken >= 1.0f)
        {
            got += 1.0f;
            continue;
        }
        const float qx = px + (sx - px) * Ease(taken), qy = py + (cy - py) * Ease(taken);
        D::Bullet(qx, qy, (i % 2) ? D::RED : D::BLUE, false, 1.0f - 0.8f * taken, s.a * (1.0f - 0.6f * taken));
        if (taken > 0.05f) D::Dia(qx, qy, 2.0f, A(D::GREEN, s));
    }
    D::Ship(sx, cy, D::GOLD, s.a);
    // the energy it gave: the plain shockwave stops at 22, this one goes on to 33
    const float bx = s.x + 200.0f;
    D::Bar(bx, s.y + 14.0f, 160.0f, 7.0f, std::min(0.22f, got * 0.06f), D::GREEN, s.a);
    D::Bar(bx, s.y + 36.0f, 160.0f, 7.0f, std::min(0.33f, got * 0.06f), D::GOLD, s.a);
    D::Line(bx + 160.0f * 0.22f, s.y + 10.0f, bx + 160.0f * 0.22f, s.y + 40.0f, 1.2f, A(D::WHITE, s, 0.6f));
    D::Num("22", bx + 160.0f * 0.22f, s.y + 56.0f, 11.0f, D::DIM, s.a);
    D::Num("33", bx + 160.0f * 0.33f + 14.0f, s.y + 56.0f, 12.0f, D::GOLD, s.a * Seg01(got, 5.0f, 6.0f));
}

// THUNDERSTORM (ECHO + EMBER): the fire calls lightning - an arc from the band every half second; an enemy that burns takes one more hit.
void Thunderstorm(const D::Scene& s)
{
    const float sx = s.x + 34.0f, cy = s.cy() + 8.0f, t = s.t;
    const float bx0 = sx + 16.0f, bx1 = s.right() - 8.0f, mid = (bx0 + bx1) * 0.5f;
    const float life = Seg01(t, 4.0f, 10.0f) * (1.0f - Seg01(t, 180.0f, 200.0f));
    D::Ship(sx, cy, D::BLUE, s.a);
    if (life > 0.0f)
    {
        D::Box(bx0, cy - 9.0f, bx1 - bx0, 18.0f, A(D::FIRE, s, 0.18f * life));
        D::Line(bx0, cy - 9.0f, bx1, cy - 9.0f, 1.1f, A(D::FIRE, s, 0.7f * life));
        D::Line(bx0, cy + 9.0f, bx1, cy + 9.0f, 1.1f, A(D::FIRE, s, 0.7f * life));
        for (int i = 0; i < 14; ++i)
        {
            const float rise = std::fmod(t * 0.55f + static_cast<float>(i) * 3.7f, 14.0f);
            D::Dia(bx0 + 10.0f + static_cast<float>(i) * 24.0f, cy + 7.0f - rise, 2.4f * (1.0f - rise / 17.0f), A(D::FIRE, s, (0.9f - rise / 20.0f) * life));
        }
    }
    const float ex[3] = {s.x + 150.0f, s.x + 236.0f, s.x + 320.0f};
    const float ey[3] = {s.y + 22.0f, cy, s.y + 24.0f}; // the middle one stands in the fire
    // an arc every 30 frames from the middle of the band to the next enemy
    const int shot = static_cast<int>(std::floor((t - 30.0f) / 30.0f));
    for (int k = 0; k < 3; ++k)
    {
        float flash = 0.0f;
        for (int n = 0; n < 6; ++n)
        {
            const float at = 30.0f + 30.0f * static_cast<float>(n);
            if (n % 3 == k) flash = std::max(flash, D::Pulse(t, at, at + 10.0f));
        }
        D::Enemy(ex[k], ey[k], k == 1 ? D::FIRE : D::BLUE, flash, s.a);
    }
    if (t >= 30.0f && shot >= 0 && shot < 6)
    {
        const float at = 30.0f + 30.0f * static_cast<float>(shot);
        const int k = shot % 3;
        const float fade = 1.0f - Seg01(t, at + 4.0f, at + 14.0f);
        D::Bolt(mid, cy - 8.0f, ex[k], ey[k] + 8.0f, shot * 3 + static_cast<int>(t * 0.5f), 2.2f, A(D::WHITE, s, fade));
        if (k == 1 && t < at + 28.0f) D::Num("x2", ex[k], ey[k] - 22.0f, 13.0f, D::GOLD, s.a * (1.0f - Seg01(t, at + 14.0f, at + 28.0f)));
    }
    D::Num("0.5 S", s.x + 34.0f, s.y + 14.0f, 12.0f, D::DIM, s.a, false);
}

// POISED (OVERFLOW + TWIN): the half beam uses the stored bullets too, and spends only half of them. Six stored: the half beam is longer, and three are left for the next one.
void Poised(const D::Scene& s)
{
    const float sx = s.x + 40.0f, cy = s.cy() + 8.0f, t = s.t;
    D::Bar(s.x + 14.0f, s.y + 10.0f, 90.0f, 6.0f, 0.5f * (1.0f - Seg01(t, 40.0f, 52.0f)), D::GOLD, s.a);
    D::Line(s.x + 59.0f, s.y + 6.0f, s.x + 59.0f, s.y + 20.0f, 1.2f, A(D::WHITE, s, 0.65f));
    const float stored = t < 46.0f ? 6.0f : std::max(3.0f, 6.0f - std::floor((t - 46.0f) / 4.0f));
    D::Pips(s.x + 14.0f, s.y + 30.0f, 6, stored, 13.0f, 3.6f, D::GOLD, s.a);
    for (int i = 0; i < 12; ++i) // the plain half beam, dashed
    {
        const float x = sx + 12.0f + 12.0f * static_cast<float>(i);
        if (x < sx + 12.0f + 120.0f) D::Line(x, cy - 7.0f, x + 6.0f, cy - 7.0f, 1.0f, A(D::WHITE, s, 0.28f));
    }
    D::Ship(sx, cy, D::BLUE, s.a);
    if (t >= 40.0f)
    {
        const float len = 120.0f * 1.24f * Ease(Seg01(t, 40.0f, 52.0f));
        const float fade = 1.0f - Seg01(t, 130.0f, 160.0f);
        D::Beam(sx + 12.0f, cy, len, 4.0f, D::WHITE, D::GOLD, s.a * fade);
        D::Num("+24%", sx + 12.0f + len + 22.0f, cy, 13.0f, D::GOLD, s.a * fade * Seg01(t, 52.0f, 60.0f));
        D::Num("3", s.x + 14.0f + 13.0f * 6.0f + 6.0f, s.y + 30.0f, 12.0f, D::GOLD, s.a * Seg01(t, 58.0f, 66.0f), false);
    }
}

// COMBO (MOMENTUM + AFTERGLOW): the end of the beam fills the stacks to the top, and they hold during the afterglow; plain MOMENTUM would let them fade.
void ComboScene(const D::Scene& s)
{
    const float t = s.t;
    const float x0 = s.x + 60.0f;
    const float rows[2] = {s.y + 24.0f, s.y + 58.0f};
    const float beam = D::Pulse(t, 10.0f, 34.0f);
    if (beam > 0.0f)
    {
        D::Beam(s.x + 24.0f, s.cy(), 310.0f * Ease(Seg01(t, 10.0f, 20.0f)), 3.5f, D::WHITE, D::BLUE, s.a * beam);
    }
    for (int r = 0; r < 2; ++r)
    {
        // the first row: plain momentum - the stacks it had fade after 4 s; the second: the beam fills it to 8 and it holds for the afterglow (4 s) and then fades
        const float before = 3.0f;
        float stacks = before;
        if (t >= 34.0f) stacks = r == 1 ? 8.0f : before;
        if (r == 1 && t >= 150.0f) stacks = std::max(0.0f, 8.0f - (t - 150.0f) / 6.0f);
        if (r == 0 && t >= 90.0f) stacks = std::max(0.0f, before - (t - 90.0f) / 10.0f);
        D::Pips(x0, rows[r], 8, stacks, 22.0f, 5.0f, r == 0 ? D::BLUE : D::GOLD, s.a);
        D::Dia(s.x + 26.0f, rows[r], 4.0f, A(r == 0 ? D::BLUE : D::GOLD, s));
    }
    D::Bar(x0, s.bottom() - 9.0f, 174.0f, 4.0f, t >= 34.0f ? 1.0f - Seg01(t, 34.0f, 150.0f) : 0.0f, D::GOLD, s.a);
    D::Num("4 S", x0 + 190.0f, s.bottom() - 9.0f, 11.0f, D::DIM, s.a, false);
    D::Num("+64%", s.right() - 40.0f, rows[1], 13.0f, D::GOLD, s.a * (t >= 34.0f && t < 150.0f ? 1.0f : 0.0f));
}

// WEAKPOINT LOCK (FOCUS + BREACH): at full focus +25% damage; a new target does not start from zero, it keeps half of the stacks (5).
void WeakpointLock(const D::Scene& s)
{
    const float cy = s.cy() - 4.0f, t = s.t, ex = s.x + 270.0f;
    const float tyA = cy - 14.0f, tyB = cy + 14.0f;
    const bool second = t >= 120.0f;
    const float sx = s.x + 34.0f;
    const float sy = tyA + (tyB - tyA) * Ease(Seg01(t, 116.0f, 130.0f));
    D::Ship(sx, sy, D::GOLD, s.a);
    D::Enemy(ex, tyA, D::RED, !second ? D::Pulse(std::fmod(t, 9.0f), 0.0f, 5.0f) : 0.0f, s.a);
    D::Enemy(ex, tyB, D::RED, second ? D::Pulse(std::fmod(t, 9.0f), 0.0f, 5.0f) : 0.0f, s.a);
    float stacks = Seg01(t, 6.0f, 80.0f) * 10.0f;
    if (t >= 120.0f) stacks = 5.0f + (t >= 150.0f ? std::min(5.0f, (t - 150.0f) / 8.0f) : 0.0f);
    stacks = std::floor(stacks);
    const bool locked = stacks >= 10.0f;
    D::Brackets(ex, second ? tyB : tyA, 22.0f - 8.0f * stacks / 10.0f, 1.6f, A(locked ? D::GOLD : D::WHITE, s, 0.95f));
    for (int k = 0; k < 7; ++k)
    {
        const float x = sx + 16.0f + std::fmod(t * 3.6f + static_cast<float>(k) * 36.0f, ex - sx - 34.0f);
        D::Shot(x, sy, D::GOLD, s.a);
    }
    D::Pips(s.x + 14.0f, s.bottom() - 10.0f, 10, stacks, 13.0f, 3.6f, locked ? D::GOLD : D::WHITE, s.a);
    D::Num("+" + std::to_string(static_cast<int>(stacks * 4.0f) + (locked ? 25 : 0)) + "%", s.x + 160.0f, s.bottom() - 10.0f, 13.0f, locked ? D::GOLD : D::DIM, s.a, false);
    if (t >= 120.0f && t < 160.0f) D::Num("5", ex + 34.0f, tyB, 14.0f, D::GOLD, s.a * (1.0f - Seg01(t, 140.0f, 160.0f)));
}

// UNYIELDING (BACKWATER + MEND): under half of the HP MEND needs no wait and repairs 50 % faster. Top: MEND alone (waits 5 s, 1 HP per 1.5 s); bottom: with the synergy.
void Unyielding(const D::Scene& s)
{
    const float t = s.t;
    const float x0 = s.x + 60.0f, w = 270.0f;
    const float rows[2] = {s.y + 26.0f, s.y + 60.0f};
    for (int r = 0; r < 2; ++r)
    {
        const float y = rows[r];
        const D::Color c = r == 0 ? D::BLUE : D::GOLD;
        const float start = r == 0 ? 110.0f : 14.0f;
        const float speed = r == 0 ? 1.0f : 1.5f;
        const float hp = 0.28f + 0.002f * speed * std::max(0.0f, t - start);
        D::Box(x0, y - 4.0f, w, 8.0f, D::Rgba(1.0f, 1.0f, 1.0f, 0.12f * s.a));
        D::Box(x0, y - 4.0f, w * std::min(0.7f, hp), 8.0f, A(c, s, 0.92f));
        D::Line(x0 + w * 0.5f, y - 8.0f, x0 + w * 0.5f, y + 8.0f, 1.4f, A(D::WHITE, s, 0.7f));
        D::Dia(s.x + 26.0f, y, 4.0f, A(c, s));
        if (t < start) D::Box(x0, y + 7.0f, w * Seg01(t, 0.0f, start), 1.5f, A(D::WHITE, s, 0.35f)); // the wait
        if (t >= start && hp < 0.7f)
        {
            const float k = std::fmod(t * 0.07f, 1.0f);
            D::Num("+", x0 + w + 14.0f, y - 4.0f * k, 14.0f, D::GREEN, s.a * (1.0f - k));
        }
    }
    D::Num("+50%", s.right() - 30.0f, rows[1], 13.0f, D::GOLD, s.a);
    D::Num("5 S", s.right() - 30.0f, rows[0], 11.0f, D::DIM, s.a);
}

// REVENGE (BACKUP CORE + DEFLECT): the revive keeps half a bar of energy, and its blast turns the white bullets into energy as well.
void Revenge(const D::Scene& s)
{
    const float cx = s.x + 70.0f, cy = s.cy(), t = s.t;
    const float dead = Seg01(t, 14.0f, 20.0f) * (1.0f - Seg01(t, 36.0f, 40.0f));
    const float blast = Seg01(t, 40.0f, 112.0f);
    const float R = blast * 100.0f;
    const float fade = 1.0f - Seg01(t, 116.0f, 134.0f);
    const float dist0[8] = {26.0f, 34.0f, 46.0f, 58.0f, 66.0f, 74.0f, 82.0f, 90.0f};
    const float ang0[8] = {0.6f, -2.2f, 2.5f, -0.5f, 0.1f, -1.4f, 1.3f, -0.2f};
    for (int i = 0; i < 8; ++i)
    {
        const bool white = i == 3 || i == 5 || i == 7;
        const float px = cx + std::cos(ang0[i]) * dist0[i] * 1.5f, py = cy + std::sin(ang0[i]) * dist0[i] * 0.42f;
        const float dd = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        const float taken = Clamp01((R - dd) / 9.0f) * (blast > 0.0f ? 1.0f : 0.0f);
        if (taken >= 1.0f) continue;
        const float qx = px + (cx - px) * Ease(taken), qy = py + (cy - py) * Ease(taken);
        D::Bullet(qx, qy, white ? D::WHITE : (i % 2 ? D::RED : D::BLUE), white, 1.0f - 0.85f * taken, s.a * (1.0f - 0.7f * taken));
        if (taken > 0.05f) D::Dia(qx, qy, 2.0f, A(D::GREEN, s));
    }
    if (blast > 0.0f && fade > 0.0f) D::Ring(cx, cy, R, 2.6f, A(D::GOLD, s, fade));
    if (t < 14.0f || t >= 40.0f) D::Ship(cx, cy, D::GOLD, s.a * (t >= 40.0f ? Seg01(t, 40.0f, 46.0f) : 1.0f));
    if (dead > 0.0f) D::Cross(cx, cy, 8.0f, 2.4f, A(D::RED, s, dead));
    // the two bars of the ship: the HP (half) and the energy (half: the synergy)
    const float bx = s.right() - 150.0f;
    const float on = Seg01(t, 44.0f, 56.0f);
    D::Bar(bx, s.y + 16.0f, 130.0f, 6.0f, t < 40.0f ? (t < 14.0f ? 1.0f : 0.0f) : 0.5f * on, D::GREEN, s.a);
    D::Bar(bx, s.y + 40.0f, 130.0f, 6.0f, t < 40.0f ? 1.0f : 0.5f * on + 0.5f * (1.0f - on), D::GOLD, s.a);
    D::Num("HP", bx - 14.0f, s.y + 19.0f, 11.0f, D::DIM, s.a);
    D::Num("50%", bx + 130.0f + 18.0f, s.y + 43.0f, 12.0f, D::GOLD, s.a * on);
}
}

void DrawSynergyDemo(int combo, float x, float y, float w, float h, int frame)
{
    if (combo < 0 || combo >= Meta::COMBO_COUNT) return;
    const D::Scene s = D::Begin(x, y, w, h, frame, LOOP);
    switch (combo)
    {
    case 0: GravityWell(s); break;
    case 1: Thunderstorm(s); break;
    case 2: Poised(s); break;
    case 3: ComboScene(s); break;
    case 4: WeakpointLock(s); break;
    case 5: Unyielding(s); break;
    case 6: Revenge(s); break;
    default: break;
    }
}
}
