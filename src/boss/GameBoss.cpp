// ARBITER - the three-phase boss of the last stage.
//   The boss is a twin-hull DREADNOUGHT seen from above (assets/textures/boss/hull*.png). Everything that is not a weak point is ARMOUR: a player bullet that
//   hits it stops with sparks and does no damage (the silhouette is a bitmap, see BossMask.h). Phase 1 and 2 resist a part of every shot (Balance::BOSS_RESIST).
//   The fight is built from COMBOS and the OVERLOAD. Attacks come in pairs (the first one pushes the ship to a place or a colour, the second one punishes it);
//   after a round of four attacks the boss charges the OVERLOAD, a long lit-up charge. A piercing beam on the weak point while it charges BREAKS it: the beam is
//   cancelled, the bullets in the air become energy and the boss is staggered (it resists nothing for a while). Left alone, it fires a white beam that takes
//   30 % of the HP of the ship. Every phase asks one question:
//   Phase 1  AWAKENING : the colour - colour lanes, fans, the comb and the ram. Weak point: the command citadel.
//   Phase 2  SPLIT     : the place - the two turrets undock as DRONES (a red / blue one and a white one with a laser); they come back after they are shot down.
//                        The hull glides and rams; its own bar ends the phase.
//   Phase 3  CORE      : the timing - the reactor is exposed but SHELLED while a drone is up; downing both drones (or an overload) opens it. Under 25 % of its
//                        life the shell breaks for good and the drones go wild.
#include "boss/GameBoss.h"
#include "boss/BossInternal.h"
#include "core/GameSettings.h"
#include "boss/BossMask.h"
#include "game/BeamFx.h"
#include "rogue/Balance.h"
#include "rogue/Modules.h"
#include "rogue/Meta.h"
#include "game/GameState.h"
#include "core/AssetManager.h"
#include "core/Collision.h"
#include "core/Renderer2D.h"
#include "game/GameEffect.h"
#include "core/GameAudio.h"
#include "game/GameVfx.h"
#include "game/GamePlayer.h"
#include "core/GameTimer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>


namespace BossInternal
{

constexpr int WARNING_FRAMES = 120;
constexpr float START_X = 1.45f;
constexpr float BATTLE_X = 0.64f;
constexpr float CORE_BASE_X = 0.64f;
constexpr float ENTER_SPEED = 0.0065f;
constexpr float P2_BODY_HP = 250.0f; // (the ship does not have to get through the drones to the hull any more: a longer bar)
constexpr float P3_HP = 200.0f;

// ---- the drones. The two turrets of the hull leave it at the start of phase 2 and stay to the end of the fight; they are the same machines in phase 2 and 3:
//   drone 0 (red / blue) fires fans of bullets of one colour, the next fan of the other colour: they can be absorbed
//   drone 1 (white)      fires white needles and, every few volleys, a slanted white laser: both can only be dodged
// A drone that is shot down comes back after DRONE_RESPAWN frames. They do not fire while the boss charges an overload (the overload is the one thing to read then).
// In phase 3 the drones hold the SHELL of the core: the core can be hurt only while both drones are down, while it charges an overload (the shell opens for the
// beam to come out), while it is staggered by a broken overload, and once it is DYING (under DYING_RATE of its life: the shell is gone, the drones go wild).
constexpr float DRONE_HP_P[2] = {60.0f, 45.0f};  // phase 2 / phase 3: the life of a drone ...
constexpr float CORE_OPEN_MUL = 1.5f;        // ... and the core takes this much more while both drones are down
constexpr int COLOUR_PERIOD[2] = {144, 96};  // drone 0: frames between two fans ...
constexpr int COLOUR_COUNT[2] = {5, 7};      // ... the bullets of a fan
constexpr int WHITE_PERIOD[2] = {150, 110};  // drone 1: frames between two volleys of needles ...
constexpr int LASER_EVERY[2] = {4, 3};       // ... every this many volleys a laser instead ...
constexpr int WHITE_DEFER_MAX = 90;
constexpr int WHITE_HOLD_MAX = 45;
constexpr int BEAM_QUIET = 40;
constexpr float DYING_RATE = 0.25f;          // phase 3: under this much of its life the shell breaks for good ...
constexpr int FRENZY_FRAMES = 480;           // ... and the drones fire twice as often for this long
constexpr float TOTAL_HP = P1_HP + P2_BODY_HP + P3_HP;

// anchors on the hull (u, v = texture position 0..1): turret rings, muzzles of the mounted turrets, engine nozzles
// (art_source/tools/boss3.py - ARBITER v6) phase 1: a gun pokes out of the leading edge of each blade (TURRET = its base, MUZZLE = its mouth).
// Phase 2 / 3: the barrels are gone and the RELAY in the gun port (= TURRET) lights while its drone lives (DrawHullExtras).
constexpr float TURRET_U = 232.0f / 602.0f;
constexpr float TURRET_V[2] = {205.0f / 684.0f, 479.0f / 684.0f};
constexpr float MUZZLE_U = 104.0f / 602.0f;
constexpr float MUZZLE_V[2] = {205.0f / 684.0f, 479.0f / 684.0f};
// the rail gun (assets/textures/boss/rail.png, 236 x 64 hull px, its emitter at the left edge) lives in the central pod: it slides out of the
// slot in the pod's front (u 196/602) for the beam attacks and back in afterwards. Every beam of the boss comes out of its emitter.
constexpr float EMITTER_U = 26.0f / 602.0f;  // the emitter when the gun is out
constexpr float RAIL_IN_U = 236.0f / 602.0f; // ... and when it is in (hidden in the pod)
constexpr float RAIL_W_PX = 236.0f, RAIL_H_PX = 64.0f;

// weak points, both on the centre line: the command citadel (phase 1) and the exposed reactor (phase 3)
constexpr float WEAK_DX = 0.0414f; // from the hull centre (u 0.593 on the texture)
constexpr float SHIP_HALF_W = 92.0f * POD_KX; // hit box of a gunboat: its body
constexpr float SHIP_HALF_H = 41.0f * POD_KY;
constexpr float SHIP_HIT_DX = 20.0f * POD_KX;
constexpr float POD_MUZZLE_DX = -73.0f * POD_KX;
constexpr float POD_Y = 0.44f; // the gunboats hover above and below the middle
constexpr float POD_X = 0.50f;
constexpr float HULL_P2_X = 0.80f; // the empty hull drifts to the right edge


// damage model
constexpr float SPECIAL_SCALE = 3.0f; // the special beam hits harder than a normal shot
constexpr int VENT_FRAMES = 110; // length of the break window
// OVERLOAD / BREAK: after a combo of two attacks the boss charges a finisher (the overload). The charge is long and lit up: a piercing beam that hits
// the weak point meanwhile BREAKS it. The finisher is cancelled, the boss is staggered (VENT_FRAMES: it does not resist a shot) and the bullets it had out become energy.
constexpr float BREAK_MIN_HALF_H = 0.05f; // only the full piercing beam breaks an overload (it is 0.10 high, 0.06 with LANCE); the thin beam of PRISM (0.045) does not
// The OVERLOAD is the boss's own move (no other enemy has one): it comes after a ROUND of attacks - two combos - every time, never at random
// (about once in 15 seconds of the fight).
constexpr int OVERLOAD_EVERY = 4;     // attacks of a round, between two overloads (phase 2 with both gunboats: two turns of each gunboat)
constexpr int RAM_HOLD = 22;
constexpr int RAM_HOLD_CORE = 40;
constexpr int RAM_BACK = 40;
constexpr float RAM_CHASE = 0.012f;      // the lane follows the ship at this speed (the ship is faster: 0.018)
constexpr float RAM_WIND_BACK = 0.07f;
constexpr float RAM_END_HULL = -0.77f;   // g_X of the hull / core at the end of the charge: its nose is at the left edge
constexpr float RAM_LANE_HULL = 0.30f;
constexpr float RAM_SLANT_MIN = 0.10f; // a ram leans (its lane is a slanted line through the ship): the steepest slope is chosen between these two ...
constexpr float RAM_SLANT_MAX = 0.24f; // ... and it always leans towards the middle of the screen, so the body does not start far outside it
constexpr int SWEEP_RUN = 88;          // phase 2: frames of the glide of the hull
constexpr int SWEEP_BACK = 30;
constexpr float SWEEP_Y = 0.46f;       // ... between +SWEEP_Y and -SWEEP_Y
constexpr float RAM_LANE_LIMIT = 0.45f; // the lane of the hull / core never goes further from the middle than this (the hull is 0.3 high on each side of it)
constexpr float BEAM_MIN_SEP = 0.30f;
// The colours of one attack never meet in the same place at the same time: a colour holds long enough to switch to it (the switch locks for 18 frames)
// and to move, and a change of colour is announced by the muzzle first.
// Beams: after a beam attack (the rail shot or the overload) the next beam waits for at least this many other attacks (phase 3 asks for more
// than the others), and phase 3 takes a breath of BEAM_BREATH frames after each one.
constexpr int BEAM_SPACING = 1;
constexpr int BEAM_SPACING_P3 = 2;
constexpr int BEAM_BREATH = 26;
constexpr int HEAVY_BREATH = 14; // phase 3: the rest after a heavy attack (the rings of NOVA, the COLLAPSE)
constexpr int TRANS_1_TO_2 = 110;
constexpr int TRANS_2_TO_3 = 130;

// ------------------------------------------------------------------ state
State g_State = State::Inactive;
int g_StateTimer = 0;
int g_Phase = 1;
int g_Tick = 0;
float g_X = START_X;
float g_Y = 0.0f;
float g_PrevX = START_X;
float g_PrevY = 0.0f;
float g_MoveT = 0.0f;
float g_P1Hp = P1_HP;
float g_P2Hp = P2_BODY_HP;
float g_P3Hp = P3_HP;
bool g_Berserk = false;
int g_Vent = 0;
int g_BodyFlash = 0;
int g_CoreFlash = 0;

Ship g_Ship[2]{};
DroneLaser g_Laser{};
int g_WhiteWarn = 0;     // drone 1: frames that the warning of its next volley of needles has run (0 = no warning)
float g_WhiteAim = 0.0f; // ... the direction of the volley (it follows the ship, then it is locked)
int g_WhiteHold = 0;     // ... frames that a finished warning has waited for the end of an attack
int g_WhiteDefer = 0;    // frames that a warning has waited to begin
int g_BeamQuiet = 0;     // frames since a laser or a beam of the boss was last on (BeamAllowed / the laser of the drone wait for it to run out)
int g_Frenzy = 0;              // phase 3, dying: frames left of the frenzy of the drones
bool g_Dying = false;          // phase 3: under DYING_RATE of its life
bool g_ShellWasOpen = false;   // (the run log notes when the shell opens and closes)
int g_ShellFlash = 0;          // a shot on the closed shell

// transition / defeat
int g_TransT = 0;
int g_TransTo = 2;
float g_CoreScale = 0.0f;
float g_WreckFade = 1.0f;
float g_FlashAlpha = 0.0f;
int g_BannerT = 0;
int g_BannerPhase = 0;
int g_DefeatTimer = 0;
int g_DefeatBlastIndex = 0;
bool g_FinalBlast = false;

// attack scheduler
AtkState g_AState = AtkState::Idle;
AtkId g_Id = AtkId::None;
AtkId g_LastId = AtkId::None;
int g_ATimer = 0;
int g_AT = 0;
int g_AStep = 0;
int g_StatOverloads = 0; // overloads charged / broken in this fight (tools/balance_sim reads them)
int g_StatBreaks = 0;
float g_PodFromX[2] = {0, 0}; // where the gunboats left the hull
float g_PodFromY[2] = {0, 0};
Polarity g_APol = Polarity::Red;
float g_AFa = 0.0f;
float g_AFb = 0.0f;
float g_RailXl = 0.0f, g_RailXl2 = 0.0f; // the columns of the two lanes of the rail strike
float g_RamM = 0.0f;                      // the ram: the steepest slope that it may have (0 = always straight)
float g_RamSlope = 0.0f;                  // ... the slope it has now (the lane leans towards the middle of the screen, see TrackRamLane)
float g_RamXL = -0.85f;                   // ... the column of the ship that the lane passes through
int g_AttackCount = 0;
int g_SinceBeam = 99; // attacks since the last beam attack (the rail shot or the overload)
AtkId g_Bag[8]{};
int g_BagSize = 0;
int g_BagPos = 0;
int g_BagPhase = 0;

Beam g_Beam[2]{};
float g_BeamHalf = BEAM_HALF_H; // half height of the beam that is on (the rail strike is thinner than the overload beam)
int g_BeamLen = 62;             // frames the beam stays on, and how many frames it has been on
int g_BeamT = 0;
float g_FlipAlpha = 0.0f;

Bolt g_Bolts[BOLT_MAX]{};
Debris g_Debris[DEBRIS_MAX]{};
Mote g_Motes[MOTE_MAX]{};
Shock g_Shocks[SHOCK_MAX]{};

float g_PX = -0.75f;
float g_PY = 0.0f;

// one-shot request queues polled by PlayScene
int g_HitStopRequest = 0;
int g_AbsorbBonus = 0; // homing bolts that were absorbed (they give more energy): ConsumeAbsorbBonus
int g_ShakeCount = 0;
int g_ShakeDuration[4]{};
float g_ShakeStrength[4]{};
int g_EnergyReward = 0;
int g_HitSoundCooldown = 0;
int g_FlipSoundCooldown = 0;
int g_DrawFrame = 0;
char g_LastHitSource[40] = "boss";
int g_LastHitDamage = Balance::DMG_BOSS_BOLT; // what the last hit that CheckPlayerCollision reported takes away

// ------------------------------------------------------------------ helpers
float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
float Clamp01(float v)
{
    return Clamp(v, 0.0f, 1.0f);
}
float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
float EaseOut(float t)
{
    const float i = 1.0f - Clamp01(t);
    return 1.0f - i * i * i;
}
float Rnd01()
{
    return static_cast<float>(std::rand() % 10000) / 9999.0f;
}
float RndRange(float a, float b)
{
    return a + (b - a) * Rnd01();
}
Polarity Opp(Polarity p)
{
    return p == Polarity::Red ? Polarity::Blue : Polarity::Red;
}
bool IsRed(Polarity p)
{
    return p == Polarity::Red;
}

float NormalizeAngle(float a)
{
    while (a > PI)
        a -= TWO_PI;
    while (a < -PI)
        a += TWO_PI;
    return a;
}

void Colour(Polarity p, float& r, float& g, float& b)
{
    GameVfx::GetPolarityColor(IsRed(p), r, g, b);
}

// Directions are defined in pixel space so rings are circles on screen.
void Velocity(float heading, float speed, float& vx, float& vy)
{
    vx = std::cos(heading) * speed;
    vy = std::sin(heading) * speed / ASPECT;
}

float AimAngle(float sx, float sy, float tx, float ty)
{
    return std::atan2((ty - sy) * ASPECT, tx - sx);
}

void PhaseAccent(int phase, float& r, float& g, float& b)
{
    // Only red and blue carry colour. Everything that is not a polarity is white.
    if (phase >= 3)
    {
        r = 1.00f;
        g = 1.00f;
        b = 1.00f;
    }
    else if (phase == 2)
    {
        r = 1.00f;
        g = 0.94f;
        b = 0.94f;
    }
    else
    {
        r = 0.90f;
        g = 0.93f;
        b = 1.00f;
    }
}

void PushShake(int duration, float strength)
{
    if (g_ShakeCount >= 4) return;
    g_ShakeDuration[g_ShakeCount] = duration;
    g_ShakeStrength[g_ShakeCount] = strength;
    ++g_ShakeCount;
}

// ------------------------------------------------------------------ pools
void ClearBolts()
{
    for (auto& b : g_Bolts)
        b = {};
}

void ClearEffects()
{
    for (auto& d : g_Debris)
        d = {};
    for (auto& m : g_Motes)
        m = {};
    for (auto& s : g_Shocks)
        s = {};
}

// Bullets that are wiped by a phase break become motes that fly to the player.
void ConvertBoltsToMotes()
{
    int converted = 0;
    for (auto& b : g_Bolts)
    {
        if (!b.active) continue;
        if (b.white)
        {
            b.active = false;
            continue;
        }
        b.active = false;
        if (converted >= 24) continue;
        for (auto& m : g_Motes)
        {
            if (m.active) continue;
            m = {};
            m.active = true;
            m.x = b.x;
            m.y = b.y;
            const float a = RndRange(0.0f, TWO_PI);
            m.vx = std::cos(a) * 0.006f;
            m.vy = std::sin(a) * 0.006f / ASPECT;
            m.delay = 10 + std::rand() % 10;
            m.polarity = b.polarity;
            ++converted;
            break;
        }
    }
}

Bolt* NewBolt(float x, float y, float heading, float speed, Polarity pol)
{
    for (auto& b : g_Bolts)
    {
        if (b.active) continue;
        b = {};
        b.active = true;
        b.x = x;
        b.y = y;
        b.halfWidth = 0.025f;
        b.halfHeight = 0.025f;
        b.heading = heading;
        b.speed = g_Phase >= 3 ? speed * Balance::BOSS_P3_SPEED_MUL : speed; // phase 3: faster bullets
        b.polarity = pol;
        b.prevX = x;
        b.prevY = y;
        b.atk = static_cast<int>(g_Id);
        return &b;
    }
    return nullptr;
}

void Shoot(float x, float y, float heading, float speed, Polarity pol)
{
    NewBolt(x, y, heading, speed, pol);
}

void FanShot(float x, float y, float centre, float spread, int count, float speed, Polarity pol)
{
    if (count <= 0) return;
    if (count == 1)
    {
        Shoot(x, y, centre, speed, pol);
        return;
    }
    const float start = centre - spread * 0.5f;
    const float step = spread / static_cast<float>(count - 1);
    for (int i = 0; i < count; ++i)
    {
        Shoot(x, y, start + step * static_cast<float>(i), speed, pol);
    }
}

void SpawnDebris(float x, float y, int kind, float vx, float vy, float size, float life, float r, float g, float b, float spin)
{
    for (auto& d : g_Debris)
    {
        if (d.active) continue;
        d = {};
        d.active = true;
        d.kind = kind;
        d.x = x;
        d.y = y;
        d.vx = vx;
        d.vy = vy;
        d.size = size;
        d.life = life;
        d.maxLife = life;
        d.r = r;
        d.g = g;
        d.b = b;
        d.spin = spin;
        d.rotation = RndRange(0.0f, TWO_PI);
        return;
    }
}

// The shards of the hull are CRYSTALS now (GameEffect::SpawnCrystals): two thirds of the old number (at most 16), red and blue alternately (the boss has both
// colours), 0.33 - 0.6 s of life. The pool of the crystals is shared with the enemies: it never holds more than GameEffect::CRYSTAL_MAX.
void BurstShards(float x, float y, int count, float speed, float size, GameEffect::CrystalTint tint = GameEffect::CrystalTint::Mixed)
{
    const int n = std::max(4, std::min(16, count * 2 / 3));
    GameEffect::SpawnCrystals(x, y, n, size * 0.80f, tint, 0.0f, 3.1415926f, speed * 0.5f, speed * 1.1f, 20, 36);
}

void BurstSparks(float x, float y, int count, float speed, float r, float g, float b)
{
    for (int i = 0; i < count; ++i)
    {
        const float a = RndRange(0.0f, TWO_PI);
        const float s = speed * RndRange(0.4f, 1.0f);
        SpawnDebris(x, y, 1, std::cos(a) * s, std::sin(a) * s / ASPECT, RndRange(0.04f, 0.09f), RndRange(12.0f, 24.0f), r, g, b,
                    0.0f);
    }
}

void MuzzleFlash(float x, float y, Polarity pol)
{
    float r, g, b;
    Colour(pol, r, g, b);
    SpawnDebris(x, y, 2, 0.0f, 0.0f, 0.09f, 8.0f, r, g, b, 0.0f);
}

void SpawnShock(float x, float y, float r, float g, float b, float size, int duration)
{
    for (auto& s : g_Shocks)
    {
        if (s.active) continue;
        s.active = true;
        s.x = x;
        s.y = y;
        s.r = r;
        s.g = g;
        s.b = b;
        s.size = size;
        s.timer = duration;
        s.duration = duration;
        return;
    }
}

void UpdateEffects()
{
    for (auto& d : g_Debris)
    {
        if (!d.active) continue;
        d.x += d.vx;
        d.y += d.vy;
        d.vx *= 0.985f;
        d.vy *= 0.985f;
        if (d.kind == 0) d.vy -= 0.0004f;
        d.rotation += d.spin;
        if (--d.life <= 0.0f) d.active = false;
    }
    for (auto& m : g_Motes)
    {
        if (!m.active) continue;
        if (m.delay > 0)
        {
            --m.delay;
            m.x += m.vx;
            m.y += m.vy;
            m.vx *= 0.92f;
            m.vy *= 0.92f;
            continue;
        }
        m.speed = std::min(m.speed + 0.0016f, 0.032f);
        const float dx = g_PX - m.x;
        const float dy = (g_PY - m.y) * ASPECT;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.055f)
        {
            m.active = false;
            ++g_EnergyReward;
            continue;
        }
        m.x += dx / len * m.speed;
        m.y += dy / len * m.speed / ASPECT;
    }
    for (auto& s : g_Shocks)
    {
        if (s.active && --s.timer <= 0) s.active = false;
    }
    if (g_FlashAlpha > 0.0f) g_FlashAlpha = std::max(0.0f, g_FlashAlpha - 0.035f);
    if (g_BannerT > 0) --g_BannerT;
    if (g_HitSoundCooldown > 0) --g_HitSoundCooldown;
    if (g_FlipSoundCooldown > 0) --g_FlipSoundCooldown;
    if (g_BodyFlash > 0) --g_BodyFlash;
    if (g_CoreFlash > 0) --g_CoreFlash;
    for (auto& s : g_Ship)
    {
        if (s.flash > 0) --s.flash;
        if (s.fireFlash > 0) --s.fireFlash;
    }
}

// ------------------------------------------------------------------ geometry
// position on the hull texture (u, v in 0..1) -> world
void HullPoint(float u, float v, float& x, float& y)
{
    x = g_X + (u - 0.5f) * VIS_W;
    y = g_Y + (0.5f - v) * VIS_H;
}

// the centre of the turret ring (top turret = 0 = red, bottom turret = 1 = blue)
void TurretPoint(int i, float& x, float& y)
{
    HullPoint(TURRET_U, TURRET_V[i], x, y);
}

// where the mounted turret's bullets come out (a little in front of the barrels)
void ReactorPoint(int i, float& x, float& y)
{
    HullPoint(MUZZLE_U, MUZZLE_V[i], x, y);
    x -= 0.02f;
}

// the nose of the hull
void BodyTip(float& x, float& y)
{
    HullPoint(0.060f, 0.5f, x, y); // (the tips of the fork)
}

float WeakX()
{
    return g_X + WEAK_DX;
}
float BodyX()
{
    return WeakX();
}

// phase 1 / entering: the two ship slots sit in the turret rings (they only carry flash / charge values then)
// where a drone stands: phase 2 at the left of the hull (above and below), phase 3 around the core
void DroneHome(int i, float& x, float& y)
{
    const float t = static_cast<float>(g_Tick);
    if (g_Phase >= 3)
    {
        // they hover in front of the core (left of the hull, where the ship can reach them), one above it and one below it, and weave a little
        const float a = t * 0.025f + static_cast<float>(i) * PI;
        x = CORE_BASE_X - 0.30f + std::cos(a) * 0.05f; // (not on the hull: they do not ride along when the core rams)
        y = Clamp(g_Y + (i == 0 ? 0.40f : -0.40f) + std::sin(a) * 0.07f, -0.76f, 0.76f);
        return;
    }
    x = POD_X + 0.025f * std::sin(t * 0.021f + static_cast<float>(i) * 2.0f);
    y = (i == 0 ? POD_Y : -POD_Y) + 0.07f * std::sin(t * (0.020f - 0.003f * static_cast<float>(i)) + static_cast<float>(i) * 1.9f);
}

void SyncShipsToBody()
{
    for (int i = 0; i < 2; ++i)
    {
        TurretPoint(i, g_Ship[i].x, g_Ship[i].y);
    }
}

float ShipHitX(int i)
{
    return g_Ship[i].x + SHIP_HIT_DX;
}

int ShipsAlive()
{
    return (g_Ship[0].alive ? 1 : 0) + (g_Ship[1].alive ? 1 : 0);
}

// phase 3: 0 at its start, 1 when the core is nearly dead. It warms up (the pauses get shorter, the second beam comes in
// the second half) so that it does not begin with a step in difficulty.
float P3Heat()
{
    return g_Phase >= 3 ? Clamp01(1.0f - g_P3Hp / P3_HP) : 0.0f;
}

// the second beam of the overload (the two beams mirror each other and leave a corridor): from the middle of phase 3 on
bool TwinBeams()
{
    return g_Phase >= 3 && P3Heat() >= 0.45f;
}

// A beam is a line from the weak point of the boss through its lane (the point at the column of the ship that the telegraph showed): it has an ANGLE, the beam of the
// boss is aimed, and the ship can leave it sideways as well as up and down.
float g_RailExt = 0.0f; // 0 = the rail gun is in its pod, 1 = it is out

float BeamOriginX()
{
    const float u = RAIL_IN_U + (EMITTER_U - RAIL_IN_U) * g_RailExt;
    return g_X + (u - 0.5f) * VIS_W; // the emitter of the rail gun
}

// the rail gun comes out for the attacks that fire a beam and goes back in when they are over
void UpdateRail()
{
    const bool beamAttack = (g_Id == AtkId::RailShot || g_Id == AtkId::OverloadBeam) && g_AState != AtkState::Idle;
    const bool want = g_State == State::Battle && (beamAttack || g_Beam[0].active || g_Beam[1].active);
    if (want) g_RailExt = std::min(1.0f, g_RailExt + 1.0f / 14.0f); // out in 14 frames (well inside a telegraph)
    else g_RailExt = std::max(0.0f, g_RailExt - 1.0f / 24.0f);
}

void DrawRail()
{
    const int tex = AssetManager::Get(AssetManager::Tex::BossRail);
    if (tex < 0 || g_RailExt <= 0.0f) return;
    const float e = g_RailExt * g_RailExt * (3.0f - 2.0f * g_RailExt); // eased
    const float tipU = RAIL_IN_U + (EMITTER_U - RAIL_IN_U) * e;
    const float w = RAIL_W_PX * PX_W, h = RAIL_H_PX * PX_H;
    const float x = g_X + (tipU - 0.5f) * VIS_W + w * 0.5f;
    Renderer2D_DrawTextureEx(tex, x, g_Y, w, h, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
}

float BeamLineY(float lane, float laneX, float x)
{
    const float ox = BeamOriginX();
    const float dx = laneX - ox;
    if (std::fabs(dx) < 0.05f) return lane;
    return g_Y + (lane - g_Y) * (x - ox) / dx;
}

// 1 / cos of the angle on the screen: the vertical half height of a slanted band
float BeamSlantScale(float lane, float laneX)
{
    const float dx = laneX - BeamOriginX();
    if (std::fabs(dx) < 0.05f) return 1.0f;
    const float k = (lane - g_Y) / dx * ASPECT;
    return std::sqrt(1.0f + k * k);
}

float CoreHalfH()
{
    return CORE_HALF_H;
}
float CoreHalfW()
{
    return CORE_HALF_W;
}

// ---- silhouette (armour) --------------------------------------------------------
const char* const* ActiveMask()
{
    if (g_Phase == 1) return BossMask::HULL;
    if (g_Phase == 2) return BossMask::BARE;
    return BossMask::BERSERK;
}

bool MaskSolid(const char* const* rows, float u, float v)
{
    if (u < 0.0f || v < 0.0f) return false;
    const int cx = static_cast<int>(u * 602.0f / BossMask::CELL);
    const int cy = static_cast<int>(v * 684.0f / BossMask::CELL);
    if (cx >= BossMask::WIDTH || cy >= BossMask::HEIGHT) return false;
    return rows[cy][cx] == '#';
}

// is there hull at this world position? (the hull of the current phase, at its current position)
bool HullSolidAt(float x, float y)
{
    const float u = (x - g_X) / VIS_W + 0.5f;
    const float v = 0.5f - (y - g_Y) / VIS_H;
    return MaskSolid(ActiveMask(), u, v);
}

// does the band [x0, x1] x [yc - hh, yc + hh] cross the hull of the current phase? (hitX: the left end of the first hull cell in it)
bool HullInBand(float x0, float x1, float yc, float hh, float& hitX)
{
    const char* const* rows = ActiveMask();
    const float cw = VIS_W * BossMask::CELL / 602.0f, ch = VIS_H * BossMask::CELL / 684.0f;
    const float left = g_X - VIS_W * 0.5f, top = g_Y + VIS_H * 0.5f;
    bool found = false;
    hitX = 0.0f;
    for (int cy = 0; cy < BossMask::HEIGHT; ++cy)
    {
        const float y1 = top - static_cast<float>(cy) * ch, y0 = y1 - ch;
        if (y1 < yc - hh || y0 > yc + hh) continue;
        for (int cx = 0; cx < BossMask::WIDTH; ++cx)
        {
            if (rows[cy][cx] != '#') continue;
            const float c0 = left + static_cast<float>(cx) * cw;
            if (c0 + cw < x0 || c0 > x1) continue;
            if (!found || c0 < hitX) hitX = c0;
            found = true;
            break; // (the first cell of the row from the left)
        }
    }
    return found;
}

// the muzzle of ship i as it is now: the mounted turret in phase 1, the gunboat in phase 2
void MuzzlePoint(int i, float& x, float& y)
{
    if (g_Phase == 2)
    {
        x = g_Ship[i].x + POD_MUZZLE_DX;
        y = g_Ship[i].y;
        return;
    }
    ReactorPoint(i, x, y);
}

// ------------------------------------------------------------------ attack tables
// An overload that goes off takes this share of the maximum HP of the ship (a bit less than a third of it): AEGIS stops the beam, BACKUP CORE and the healing are what
// carry the ship through a failure. (The piercing beam does not make the ship invulnerable any more: the only way to be safe from it is not to be there.)
int OverloadDamage(float rate)
{
    return std::max(1, static_cast<int>(std::lround(static_cast<float>(Mod::MaxHp()) * rate)));
}

// what a hit takes away: phase 3 hits harder
int ScaledHit(int damage, float phase3Mul)
{
    return g_Phase >= 3 ? static_cast<int>(std::lround(static_cast<float>(damage) * phase3Mul)) : damage;
}

const char* AtkName(int id)
{
    static const char* const NAMES[] = {"none",          "twin_burst",     "polarity_fan", "color_lanes", "cross_wall", "comb_sweep", "double_spiral", "prism_ring",
                                        "rail_shot",     "overload_beam",  "polarity_storm", "nova_rings", "ram",        "sweep",      "collapse"};
    return (id >= 0 && id < static_cast<int>(sizeof(NAMES) / sizeof(NAMES[0]))) ? NAMES[id] : "boss";
}

int TelegraphFrames(AtkId id)
{
    switch (id)
    {
    case AtkId::TwinBurst:
        return 26;
    case AtkId::PolarityFan:
        return 30;
    case AtkId::ColorHelix:
        return 40;
    case AtkId::SplitBloom:
        return 36;
    case AtkId::Ram:
        return 60;
    case AtkId::Sweep:
        return 40;
    case AtkId::DoubleSpiral:
        return 30;
    case AtkId::PrismRing:
        return 38;
    case AtkId::RailShot:
        return 34;
    case AtkId::OverloadBeam:
        return 90;
    case AtkId::PolarityStorm:
    case AtkId::CombSweep:
        return 34;
    case AtkId::NovaRings:
        return 42;
    case AtkId::Collapse:
        return 46;
    default:
        return 30;
    }
}

int IdleFrames()
{
    // The pauses between two attacks are short on purpose (the log of the player said the fight was too calm): one attack is hardly over when the next one is lit.
    float base = 13.0f;
    if (g_Phase == 1)
    {
        base = (13.0f - 4.0f * (1.0f - g_P1Hp / P1_HP)) * 0.75f;
    }
    else if (g_Phase == 2)
    {
        base = 11.0f * 0.75f;
    }
    else
    {
        // phase 3 breathes a little more than it did (it was a quarter shorter than the others: hardly a pause between two patterns): the rhythm of the phase is
        // pressure, rest, pressure. The heavy attacks add their own rest (FinishAttack).
        base = 13.0f - 5.0f * P3Heat();
        return static_cast<int>((base + static_cast<float>(std::rand() % 6)) * 0.85f);
    }
    return static_cast<int>(base) + std::rand() % 6;
}

// the attacks that fill the screen or ask for the most (phase 3 never runs two of them one after the other: it takes a longer breath after each, see FinishAttack)
bool IsHeavyAttack(AtkId id)
{
    return id == AtkId::Collapse || id == AtkId::NovaRings || id == AtkId::OverloadBeam;
}

// The bag holds COMBOS: two attacks that belong together (the first one pushes the ship to a place or a colour, the second one punishes it). The order of the
// three combos of a phase is shuffled, and the combo that ended the last bag does not open the next one.
void FillBag()
{
    struct Combo
    {
        AtkId a, b;
    };
    static const Combo p1[4] = {{AtkId::ColorHelix, AtkId::SplitBloom}, {AtkId::PolarityFan, AtkId::CombSweep}, {AtkId::TwinBurst, AtkId::Ram}, {AtkId::PolarityStorm, AtkId::PolarityFan}};
    static const Combo p2[4] = {{AtkId::Ram, AtkId::PolarityStorm}, {AtkId::Sweep, AtkId::DoubleSpiral}, {AtkId::Ram, AtkId::NovaRings}, {AtkId::Sweep, AtkId::PolarityStorm}};
    static const Combo p3[3] = {{AtkId::DoubleSpiral, AtkId::RailShot}, {AtkId::PrismRing, AtkId::Collapse}, {AtkId::NovaRings, AtkId::Ram}};
    static int lastCombo = -1;
    const Combo* src = g_Phase == 1 ? p1 : (g_Phase == 2 ? p2 : p3);
    const int count = g_Phase == 3 ? 3 : 4;
    int order[4] = {0, 1, 2, 3};
    // shuffle until the order is fit: the combo that ended the last bag does not open this one, the first attack is not the one that has just run, and
    // no attack runs twice in a row where two combos meet (the end of one is the start of the next)
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        for (int i = count - 1; i > 0; --i)
            std::swap(order[i], order[std::rand() % (i + 1)]);
        bool fit = order[0] != lastCombo && src[order[0]].a != g_LastId;
        if (g_Phase >= 3 && IsHeavyAttack(g_LastId) && IsHeavyAttack(src[order[0]].a)) fit = false;
        for (int i = 0; i + 1 < count && fit; ++i)
        {
            fit = src[order[i]].b != src[order[i + 1]].a;
            if (g_Phase >= 3 && IsHeavyAttack(src[order[i]].b) && IsHeavyAttack(src[order[i + 1]].a)) fit = false; // (a heavy attack never meets another one where two combos join)
        }
        if (fit) break;
    }
    lastCombo = order[count - 1];
    g_BagSize = count * 2;
    for (int i = 0, slot = 0; i < count; ++i, slot += 2)
    {
        g_Bag[slot] = src[order[i]].a;
        g_Bag[slot + 1] = src[order[i]].b;
    }
    g_BagPos = 0;
    g_BagPhase = g_Phase;
}

bool IsBeamAttack(AtkId id)
{
    return id == AtkId::RailShot || id == AtkId::OverloadBeam;
}

// may a beam attack (the rail shot, the overload) start now? Not before BEAM_SPACING (phase 3: BEAM_SPACING_P3) other attacks have passed since the last one
bool BodyBeamBusy()
{
    return ((g_Id == AtkId::RailShot || g_Id == AtkId::OverloadBeam) && g_AState != AtkState::Idle) || g_Beam[0].active || g_Beam[1].active;
}

// the white drone is about to start its laser (it is its turn, and the timer is nearly out): the hull keeps its beams for after it
bool DroneLaserSoon()
{
    if (g_Phase < 2 || !g_Ship[1].alive) return false;
    const int k = g_Phase >= 3 ? 1 : 0;
    const Ship& s = g_Ship[1];
    return s.volley % LASER_EVERY[k] == LASER_EVERY[k] - 1 && s.fireTimer <= 45;
}

// the rail shot (it is in the bag of attacks): not while a laser is up or about to start or has just been, nor while a volley of needles is announced
bool BeamAllowed()
{
    return g_SinceBeam >= (g_Phase >= 3 ? BEAM_SPACING_P3 : BEAM_SPACING) && g_Laser.state == 0 && g_BeamQuiet <= 0 && g_WhiteWarn == 0 && !DroneLaserSoon();
}

// the overload (it comes on a count, every OVERLOAD_EVERY attacks: it is what the player breaks, so it must not be starved): only not while a laser is up. A laser that is
// about to start, or a volley that is announced, give way to it (the drones are silent while it charges: the announcement is called off, the laser waits).
bool OverloadAllowed()
{
    return g_SinceBeam >= (g_Phase >= 3 ? BEAM_SPACING_P3 : BEAM_SPACING) && g_Laser.state == 0;
}

AtkId NextFromBag()
{
    if (g_BagPhase != g_Phase || g_BagPos >= g_BagSize) FillBag();
    if (!BeamAllowed() && IsBeamAttack(g_Bag[g_BagPos]))
    {
        // a beam is next but it is too soon after the last one: the next attack of the bag that is not a beam (and is not the one that has just run) goes first ...
        for (int j = g_BagPos + 1; j < g_BagSize; ++j)
        {
            if (!IsBeamAttack(g_Bag[j]) && g_Bag[j] != g_LastId)
            {
                std::swap(g_Bag[g_BagPos], g_Bag[j]);
                return g_Bag[g_BagPos++];
            }
        }
        // ... and when the bag has none left, a filler takes the place (not the last attack, not the one that follows); the beam stays in the bag
        static const AtkId FILLERS[4] = {AtkId::NovaRings, AtkId::PrismRing, AtkId::Ram, AtkId::DoubleSpiral};
        for (AtkId filler : FILLERS)
        {
            if (filler != g_LastId && filler != g_Bag[g_BagPos]) return filler;
        }
        return AtkId::NovaRings;
    }
    return g_Bag[g_BagPos++];
}

void ResetScheduler(int idleFrames)
{
    g_AState = AtkState::Idle;
    g_Id = AtkId::None;
    g_ATimer = idleFrames;
    g_AT = 0;
    g_AStep = 0;
    g_AttackCount = 0;
    g_SinceBeam = 0; // (a new phase, or a new start: the first beam comes after a few other attacks)
    g_BagPos = g_BagSize = 0;
    g_Beam[0].active = g_Beam[1].active = false;
}

void PlayHitSound(bool armored)
{
    if (g_HitSoundCooldown > 0) return;
    g_HitSoundCooldown = 4;
    GameAudio::Play(armored ? GameSound::BossArmor : GameSound::BossHit);
}

float DroneHp()
{
    return DRONE_HP_P[g_Phase >= 3 ? 1 : 0];
}

// is the core of phase 3 open to shots? (both drones down, an overload charging, staggered by a broken one, or dying)
bool CoreExposed()
{
    if (g_Phase != 3) return true;
    return g_Dying || g_Vent > 0 || ShipsAlive() == 0 || (g_AState == AtkState::Telegraph && g_Id == AtkId::OverloadBeam);
}

// what a hit does: a drone takes it in full; the body of phase 1 and 2 resists a part of it (not while it is staggered by a broken overload)
float DamageMultiplier(int ship, Polarity bulletPolarity, bool special)
{
    (void)bulletPolarity;
    (void)special;
    float m = 1.0f;
    if (ship < 0 && g_Phase < 3 && g_Vent <= 0) m *= Mod::BossResistFactor();
    if (ship < 0 && g_Phase == 3 && !g_Dying && ShipsAlive() == 0) m *= CORE_OPEN_MUL; // the reward for taking both drones down
    return m;
}

void BeginTransition(int toPhase);
void BeginDefeat();

// a drone is shot down: it falls apart and comes back after DRONE_RESPAWN frames (the drones of phase 3 come back faster)
void KillShip(int i)
{
    Ship& s = g_Ship[i];
    s.alive = false;
    s.hp = 0.0f;
    s.respawn = DRONE_RESPAWN[g_Phase >= 3 ? 1 : 0];
    GameEffect::SpawnExplosion(s.x, s.y, 0.20f);
    BurstShards(s.x, s.y, 16, 0.022f, 0.10f, i == 0 ? GameEffect::TintOf(s.polarity) : GameEffect::CrystalTint::White); // (the colour drone breaks in its colour, the white one in white)
    float r, g, b;
    if (i == 0)
        Colour(s.polarity, r, g, b);
    else
        r = g = b = 1.0f;
    SpawnShock(s.x, s.y, r, g, b, 0.80f, 46);
    GameAudio::Play(GameSound::BossCannonBreak);
    PushShake(10, 0.010f);
    g_HitStopRequest = std::max(g_HitStopRequest, 4);
    if (i == 1 && g_Laser.state != 0) g_Laser.state = 0; // (a laser that was being shown dies with its drone)
    if (i == 1) g_WhiteWarn = g_WhiteHold = g_WhiteDefer = 0; // (... and so does a volley that was announced)
    // Phase 3: a drone that is down waits for the other one (its timer stands still while the other one is up), so the two can never take turns and keep the
    // shell closed for good. When the second one goes down, both come back TOGETHER after CORE_WINDOW: the core is open for that long.
    Ship& other = g_Ship[1 - i];
    if (g_Phase >= 3 && !other.alive)
    {
        s.respawn = CORE_WINDOW;
        other.respawn = CORE_WINDOW;
    }
    else if (!other.alive)
    {
        other.respawn = std::max(other.respawn, CORE_WINDOW);
    }
    Mod::LogEvent("drone_down", i == 0 ? "colour" : "white");
}

void ReviveShip(int i)
{
    Ship& s = g_Ship[i];
    s.alive = true;
    s.hp = s.maxHp = DroneHp();
    s.respawn = 0;
    s.fireTimer = 60 + 20 * i;
    s.flash = 0;
    float r, g, b;
    if (i == 0)
        Colour(s.polarity, r, g, b);
    else
        r = g = b = 1.0f;
    SpawnShock(s.x, s.y, r, g, b, 0.50f, 30);
    GameAudio::Play(GameSound::BossSwap);
}

// ship = -1 for body / core. Returns true when this hit kills the boss.
bool HitBoss(int ship, float amount, Polarity bulletPolarity, bool special, float hx, float hy)
{
    if (g_State != State::Battle) return false;
    if (g_Phase == 3 && ship < 0 && !CoreExposed())
    {
        // the shell of the core: the shot stops on it (sparks, a cold ring)
        PlayHitSound(true);
        BurstSparks(hx, hy, 2, 0.012f, 0.80f, 0.90f, 1.00f);
        g_ShellFlash = 6;
        return false;
    }
    const bool armored = false;
    const float dmg = amount * DamageMultiplier(ship, bulletPolarity, special) * Mod::BreachBonus();
    Mod::NoteDamage(dmg); // (the streak counts the damage on the boss too)
    // the run report: what the shot did (a beam is `special`), and what the resistance of the body in phase 1 and 2 would have taken of it (BREACH goes through)
    Mod::NoteDealt(dmg, (ship < 0 && g_Phase < 3 && g_Vent <= 0) ? Balance::BOSS_RESIST : 0.0f, true, special);
    PlayHitSound(armored);
    if (armored)
    {
        BurstSparks(hx, hy, 2, 0.012f, 0.80f, 0.90f, 1.00f);
    }
    else
    {
        GameEffect::SpawnHit(hx, hy, true);
    }

    if (g_Phase == 1)
    {
        g_P1Hp -= dmg;
        g_BodyFlash = 4;
        if (g_P1Hp <= 0.0f)
        {
            g_P1Hp = 0.0f;
            BeginTransition(2);
        }
        return false;
    }
    if (ship >= 0)
    {
        // a drone (phase 2 and 3)
        if (!g_Ship[ship].alive) return false;
        g_Ship[ship].hp -= dmg;
        g_Ship[ship].flash = 4;
        if (g_Ship[ship].hp <= 0.0f) KillShip(ship);
        return false;
    }
    if (g_Phase == 2)
    {
        {
            // the hull: its own bar ends the phase, whatever is left of the gunboats
            g_P2Hp -= dmg;
            g_BodyFlash = 4;
            if (g_P2Hp <= 0.0f)
            {
                g_P2Hp = 0.0f;
                BeginTransition(3);
            }
            return false;
        }
    }
    g_P3Hp -= dmg;
    g_CoreFlash = 4;
    if (g_P3Hp <= 0.0f)
    {
        g_P3Hp = 0.0f;
        BeginDefeat();
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ transitions
void BeginTransition(int toPhase)
{
    Mod::LogEvent("boss_phase", toPhase == 2 ? "2" : "3");
    Mod::OnPhaseStart(true);
    g_State = State::Transition;
    g_TransTo = toPhase;
    g_TransT = 0;
    g_Phase = toPhase;
    g_FlashAlpha = 1.0f;
    g_HitStopRequest = std::max(g_HitStopRequest, 7);
    // the change of phase: a batch of big crystals out of the hull, with the stop-frame (the player is held / nothing is being dodged in a transition)
    GameEffect::SpawnCrystals(g_X, g_Y, 16, 0.085f, GameEffect::CrystalTint::Mixed, 0.0f, 3.1415926f, 0.012f, 0.030f, 28, 40);
    ConvertBoltsToMotes();
    ResetScheduler(40);
    g_Vent = 0;
    g_Berserk = false;
    g_BannerT = 175;
    g_BannerPhase = toPhase;
    GameAudio::SetBossPhase(toPhase); // the music of the new phase starts now, on the flash (before the sound, so the sound does not count the phase again)
    GameAudio::Play(GameSound::BossPhase);
Mod::HealPlayer(Balance::BOSS_PHASE_HEAL); // a breather between the phases
    if (toPhase == 2)
    {
        g_P2Hp = P2_BODY_HP;
        for (int i = 0; i < 2; ++i)
        {
            g_Ship[i].hp = g_Ship[i].maxHp = DroneHp();
            g_Ship[i].alive = true;
            g_Ship[i].respawn = 0;
            g_Ship[i].fireTimer = 90 + 30 * i;
            g_Ship[i].volley = 0;
        }
        g_Ship[0].polarity = Polarity::Red;
        g_Ship[1].polarity = Polarity::Blue; // (drone 1 is white; its texture is the blue one, drawn pale)
        // (the sparks and shards come at the moment the turrets let go, see UpdateTransition)
    }
    else
    {
        g_WreckFade = 1.0f;
        g_CoreScale = 0.0f;
        g_MoveT = 0.0f;
        g_Laser.state = 0;
        g_WhiteWarn = g_WhiteHold = g_WhiteDefer = g_BeamQuiet = 0;
        for (int i = 0; i < 2; ++i) // the drones come along into phase 3, rebuilt
        {
            if (!g_Ship[i].alive) ReviveShip(i);
            g_Ship[i].hp = g_Ship[i].maxHp = DroneHp();
            g_Ship[i].fireTimer = 120 + 30 * i;
            g_Ship[i].volley = 0;
        }
    }
}

void UpdateTransition()
{
    ++g_TransT;
    const int t = g_TransT;

    if (g_TransTo == 2)
    {
        // 1. the clamps let go: the hull trembles, sparks jump at the turret rings
        if (t < UNDOCK_AT)
        {
            SyncShipsToBody();
            if (t % 6 == 0)
            {
                for (int i = 0; i < 2; ++i)
                {
                    float x, y;
                    TurretPoint(i, x, y);
                    BurstSparks(x, y, 3, 0.012f, 1.0f, 0.90f, 0.70f);
                }
            }
        }
        // 2. the guns go in, the relays light up and the drones are called in: they fly in from beyond the right edge, one above, one below
        if (t == UNDOCK_AT)
        {
            for (int i = 0; i < 2; ++i)
            {
                float x, y;
                TurretPoint(i, x, y);
                g_PodFromX[i] = 1.35f;
                g_PodFromY[i] = i == 0 ? 0.95f : -0.95f;
                g_Ship[i].x = g_PodFromX[i];
                g_Ship[i].y = g_PodFromY[i];
                float r, g, b;
                Colour(g_Ship[i].polarity, r, g, b);
                SpawnShock(x, y, r, g, b, 0.30f, 30); // (the relay switches on)
                BurstSparks(x, y, 6, 0.016f, r, g, b);
            }
            float pr, pg, pb;
            PhaseAccent(2, pr, pg, pb);
            SpawnShock(g_X, g_Y, pr, pg, pb, 0.90f, 52);
            GameAudio::Play(GameSound::BossCannonBreak);
            PushShake(10, 0.010f);
            g_FlashAlpha = 0.35f;
        }
        if (t > UNDOCK_AT)
        {
            const float k = EaseOut(static_cast<float>(t - UNDOCK_AT) / 56.0f);
            for (int i = 0; i < 2; ++i)
            {
                g_Ship[i].x = Lerp(g_PodFromX[i], POD_X, k);
                g_Ship[i].y = Lerp(g_PodFromY[i], (i == 0 ? POD_Y : -POD_Y), k);
            }
            g_X = Lerp(g_X, HULL_P2_X, 0.05f); // the empty hull falls back and drifts to the right edge
            g_Y = Lerp(g_Y, 0.0f, 0.08f);
        }
        if (t >= TRANS_1_TO_2)
        {
            g_State = State::Battle;
            ResetScheduler(48);
        }
        return;
    }

    // 2 -> 3: the empty hull comes back, the wrecks burn out, the armour blows off and the reactor lights up (berserk)
    g_X = Lerp(g_X, CORE_BASE_X, 0.05f);
    g_Y = Lerp(g_Y, 0.0f, 0.06f);
    g_WreckFade = 1.0f - Clamp01(static_cast<float>(t - 10) / 40.0f);
    for (int i = 0; i < 2; ++i) // the drones take their places around the core
    {
        float tx, ty;
        DroneHome(i, tx, ty);
        g_Ship[i].x += (tx - g_Ship[i].x) * 0.06f;
        g_Ship[i].y += (ty - g_Ship[i].y) * 0.06f;
    }
    if (t % 9 == 0 && t < 60)
    {
        float x, y;
        HullPoint(RndRange(0.30f, 0.90f), RndRange(0.10f, 0.90f), x, y);
        BurstSparks(x, y, 3, 0.02f, 1.0f, 0.65f, 0.25f);
    }
    if (t == 40) // the wing plates go first
    {
        float x, y;
        HullPoint(240.0f / 602.0f, 140.0f / 684.0f, x, y); // (the tear in the upper blade)
        SpawnShock(x, y, 1.0f, 0.70f, 0.30f, 0.50f, 34);
        BurstShards(x, y, 14, 0.030f, 0.11f);
        HullPoint(240.0f / 602.0f, 544.0f / 684.0f, x, y);
        SpawnShock(x, y, 1.0f, 0.70f, 0.30f, 0.50f, 34);
        BurstShards(x, y, 14, 0.030f, 0.11f);
        PushShake(12, 0.012f);
        GameAudio::Play(GameSound::BossCannonBreak);
    }
    if (t == 62) // the reactor ignites (the berserk hull is drawn from here on)
    {
        g_FlashAlpha = 1.0f;
        SpawnShock(WeakX(), g_Y, 1.0f, 1.0f, 1.0f, 1.0f, 52);
        BurstShards(WeakX(), g_Y, 20, 0.030f, 0.11f);
        GameAudio::Play(GameSound::BossPhase);
        g_HitStopRequest = std::max(g_HitStopRequest, 5);
    }
    if (t == 92)
    {
        float x, y;
        TurretPoint(0, x, y);
        SpawnShock(x, y, 1.0f, 0.34f, 0.22f, 0.60f, 40); // the empty sockets spark: red above ...
    }
    if (t == 112)
    {
        float x, y;
        TurretPoint(1, x, y);
        SpawnShock(x, y, 0.25f, 0.65f, 1.00f, 0.60f, 40); // ... blue below
    }
    g_CoreScale = EaseOut(static_cast<float>(t - 62) / 62.0f);
    if (t >= TRANS_2_TO_3)
    {
        g_State = State::Battle;
        g_CoreScale = 1.0f;
        g_WreckFade = 0.0f;
        g_Berserk = true; // phase 3 IS the berserk phase
        GameAudio::Play(GameSound::BossBerserk);
        SpawnShock(WeakX(), g_Y, 1.0f, 0.55f, 0.20f, 0.90f, 50);
        PushShake(16, 0.016f);
        ResetScheduler(44);
    }
}

void BeginDefeat()
{
    g_State = State::Defeated;
    g_DefeatTimer = DEFEAT_FRAMES;
    g_DefeatBlastIndex = 0;
    g_FinalBlast = false;
    g_Beam[0].active = g_Beam[1].active = false;
    g_AState = AtkState::Idle;
    g_Id = AtkId::None;
    g_HitStopRequest = std::max(g_HitStopRequest, 12);
    for (auto& b : g_Bolts)
    {
        if (!b.active) continue;
        BurstSparks(b.x, b.y, 2, 0.01f, 1.0f, 0.9f, 0.6f);
        b.active = false;
    }
    GameAudio::Play(GameSound::BossExplosion);
}

void UpdateDefeat()
{
    if (g_DefeatTimer > 0) --g_DefeatTimer;
    const int t = DEFEAT_FRAMES - g_DefeatTimer;
    if (t > 4 && t < 120 && t % 8 == 0)
    {
        float x, y;
        HullPoint(RndRange(0.14f, 0.96f), RndRange(0.08f, 0.92f), x, y); // the whole hull comes apart
        GameEffect::SpawnExplosion(x, y, RndRange(0.08f, 0.15f));
        ++g_DefeatBlastIndex;
    }
    if (t > 4 && t < 120 && t % 22 == 0)
    {
        float r, g, b;
        Colour((t / 22) % 2 == 0 ? Polarity::Red : Polarity::Blue, r, g, b);
        SpawnShock(WeakX(), g_Y, r, g, b, 0.50f, 34);
    }
    if (t == 128 && !g_FinalBlast)
    {
        g_FinalBlast = true;
        g_FlashAlpha = 1.0f;
        GameEffect::SpawnExplosion(WeakX(), g_Y, 0.30f);
        SpawnShock(WeakX(), g_Y, 1.0f, 1.0f, 1.0f, 1.40f, 64);
        SpawnShock(WeakX(), g_Y, 1.0f, 0.62f, 0.22f, 1.00f, 50);
        BurstShards(WeakX(), g_Y, 30, 0.035f, 0.12f);
        GameAudio::Play(GameSound::BossFinalExplosion);
    }
}

// ------------------------------------------------------------------ attacks
// The lanes of the overload beam. They follow the ship while the beam charges (what the telegraph shows is where the beam will be) and are almost
// locked once it fires. In the berserk phase two beams mirror each other; the corridor between them is never narrower than BEAM_MIN_SEP.
void TrackBeamLane(float speed, bool followColumn = false)
{
    if (followColumn) g_Beam[0].xl = g_Beam[1].xl = g_PX;
    g_Beam[0].y += Clamp(g_PY - g_Beam[0].y, -speed, speed);
    g_Beam[0].y = Clamp(g_Beam[0].y, -0.62f, 0.62f);
    if (TwinBeams() && std::fabs(g_Beam[0].y) < BEAM_MIN_SEP) g_Beam[0].y = g_Beam[0].y < 0.0f ? -BEAM_MIN_SEP : BEAM_MIN_SEP;
    g_Beam[1].y = Clamp(-g_Beam[0].y, -0.62f, 0.62f);
}

void BeginAttack(AtkId id)
{
    g_Id = id;
    if (id == AtkId::OverloadBeam)
    {
        ++g_StatOverloads;
        g_HitStopRequest = std::max(g_HitStopRequest, 6); // a beat of silence: the world stops for a moment and the boss does not fire again until the charge is over
        PushShake(6, 0.004f);
    }
    g_AState = AtkState::Telegraph;
    g_ATimer = TelegraphFrames(id);
    g_AT = 0;
    g_AStep = 0;
    g_AFa = g_AFb = 0.0f;
    g_APol = (std::rand() % 2 == 0) ? Polarity::Red : Polarity::Blue;

    switch (id)
    {
    case AtkId::RailShot:
        g_AFa = Clamp(g_PY, -0.62f, 0.62f);
        g_RailXl = g_PX;
        GameAudio::Play(GameSound::BossDashWarning);
        break;
    case AtkId::ColorHelix:
    case AtkId::SplitBloom:
        GameAudio::Play(GameSound::BossCannonCharge);
        break;
    case AtkId::OverloadBeam:
        g_Beam[0].y = Clamp(g_PY, -0.62f, 0.62f);
        TrackBeamLane(0.0f, true);
        GameAudio::Play(GameSound::OverloadAlarm); // (the alarm: what the player has to break; not the charge of a laser)
        break;
    case AtkId::PrismRing:
    case AtkId::NovaRings:
        GameAudio::Play(GameSound::BossLaserCharge);
        break;
    case AtkId::CombSweep:
        g_AFa = RndRange(0.0f, TWO_PI); // where in its swing the comb starts
        GameAudio::Play(GameSound::BossCannonCharge);
        break;
    case AtkId::Ram:
        g_AFa = Clamp(g_Y, -RAM_LANE_LIMIT, RAM_LANE_LIMIT); // the lane starts where the body is and then follows the ship
        g_RamM = (std::rand() % 100 < 55) ? RndRange(RAM_SLANT_MIN, RAM_SLANT_MAX) : 0.0f;
        g_RamXL = g_PX;
        g_RamSlope = 0.0f;
        GameAudio::Play(GameSound::BossDashWarning);
        break;
    case AtkId::Collapse:
        GameAudio::Play(GameSound::BossLaserCharge);
        break;
    case AtkId::Sweep:
        g_AFa = (std::rand() % 2 == 0) ? 1.0f : -1.0f; // +1: from the top to the bottom
        GameAudio::Play(GameSound::BossDashWarning);
        break;
    default:
        GameAudio::Play(GameSound::BossCannonCharge);
        break;
    }
}

void StartVent()
{
    g_AState = AtkState::Vent;
    g_ATimer = VENT_FRAMES;
    g_Vent = VENT_FRAMES;
    g_AttackCount = 0;
    g_Id = AtkId::None;
    g_SinceBeam = 0;
    g_Beam[0].active = g_Beam[1].active = false;
    GameAudio::Play(GameSound::BossVent);
}

void FinishAttack()
{
    const AtkId finished = g_Id;
    g_LastId = finished;
    g_Id = AtkId::None;
    g_Beam[0].active = g_Beam[1].active = false;
    const bool finisher = (finished == AtkId::OverloadBeam);
    if (finisher)
    {
        Mod::LogEvent("overload", "boss_fired");
        g_AttackCount = 0; // the overload went off: the next combo starts, the finisher is not one of the attacks of a combo
    }
    else
    {
        ++g_AttackCount;
    }
    if (IsBeamAttack(finished)) g_SinceBeam = 0;
    else if (g_SinceBeam < 99) ++g_SinceBeam;
    g_AState = AtkState::Idle;
    // after a beam the boss takes a breath (phase 3 a longer one): the ship gets the time to come back, to switch and to see the next question
    g_ATimer = IdleFrames() + (finisher ? 26 : 0) + (IsBeamAttack(finished) && g_Phase >= 3 ? BEAM_BREATH : 0);
    if (g_Phase >= 3 && !finisher && IsHeavyAttack(finished)) g_ATimer += HEAVY_BREATH; // (the overload has its own, longer rest)
}

// Is the overload charging, so that a piercing beam on the weak point would break it?
bool Interruptible()
{
    return g_State == State::Battle && g_AState == AtkState::Telegraph && g_Id == AtkId::OverloadBeam;
}

// A beam hit the weak point while the overload charged: the finisher is cancelled, the boss is staggered, and what it had fired becomes energy.
void BreakOverload(float hx, float hy)
{
    ++g_StatBreaks;
    Meta::NoteOverloadBreak();
    Mod::LogEvent("overload", "boss_break");
    g_LastId = g_Id;
    StartVent(); // the window: VENT_FRAMES without resistance (it also clears the finisher and the beams)
    ConvertBoltsToMotes();
    SpawnShock(hx, hy, 1.0f, 1.0f, 1.0f, 0.95f, 40);
    SpawnShock(hx, hy, 1.0f, 0.82f, 0.30f, 0.55f, 30);
    BurstShards(hx, hy, 18, 0.026f, 0.10f);
    PushShake(14, 0.012f);
    g_HitStopRequest = std::max(g_HitStopRequest, 8);
    GameAudio::Play(GameSound::OverloadBreak);
}

void ChooseNextAttack()
{
    if (g_Phase == 1)
    {
        if (g_AttackCount >= OVERLOAD_EVERY && OverloadAllowed())
        {
            BeginAttack(AtkId::OverloadBeam);
            return;
        }
        BeginAttack(NextFromBag());
        return;
    }
    if (g_Dying) // the end: no more overloads, the core rams, collapses and fires rings without a pause
    {
        // (the two heavy attacks are never next to each other: the ram is between them)
        static const AtkId FINALE[4] = {AtkId::Ram, AtkId::Collapse, AtkId::Ram, AtkId::NovaRings};
        BeginAttack(FINALE[g_AttackCount % 4]);
        return;
    }
    if (g_AttackCount >= OVERLOAD_EVERY && OverloadAllowed() && !IsHeavyAttack(g_LastId)) // (too soon after a beam, or right after a heavy attack: the overload is due, it waits for its turn)
    {
        BeginAttack(AtkId::OverloadBeam);
        return;
    }
    BeginAttack(NextFromBag());
}

// ---- phase 1 ------------------------------------------------------------------
void FireTwinBurst()
{
    // A series is one colour: three rounds from one turret, a pause that is long enough to switch, then three rounds from the other turret.

    constexpr int INTERVAL = 13;
    constexpr int SERIES_GAP = 30;
    constexpr int ROUNDS = 3;
    const int t = g_AT - 1;
    const int first = IsRed(g_APol) ? 0 : 1; // which turret opens
    int round = -1;
    if (t >= 0)
    {
        if (t % INTERVAL == 0 && t / INTERVAL < ROUNDS)
        {
            round = t / INTERVAL;
        }
        else
        {
            const int t2 = t - (ROUNDS * INTERVAL + SERIES_GAP);
            if (t2 >= 0 && t2 % INTERVAL == 0 && t2 / INTERVAL < ROUNDS) round = ROUNDS + t2 / INTERVAL;
        }
    }
    if (round >= 0)
    {
        const int side = round < ROUNDS ? first : 1 - first; // 0 upper (red), 1 lower (blue)
        float mx, my;
        ReactorPoint(side, mx, my);
        const float aim = AimAngle(mx, my, g_PX, g_PY);
        FanShot(mx, my, aim, 0.40f, 4, 0.0116f, side == 0 ? Polarity::Red : Polarity::Blue);
        g_Ship[side].fireFlash = 8;
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= INTERVAL * ROUNDS * 2 + SERIES_GAP + 14) FinishAttack();
}

void FirePolarityFan()
{
    // two fans, 43 frames apart: the second one has the other colour and is answered in time
    float mx, my;
    BodyTip(mx, my);
    if (g_AT == 1)
    {
        FanShot(mx, my, PI + 0.08f, 1.45f, 11, 0.0105f, g_APol);
        g_Ship[0].fireFlash = g_Ship[1].fireFlash = 8;
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT == 44)
    {
        FanShot(mx, my, PI - 0.08f, 1.45f, 11, 0.0105f, Opp(g_APol));
        g_Ship[0].fireFlash = g_Ship[1].fireFlash = 8;
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= 80) FinishAttack();
}

// The first question of the fight is "which colour is this?". Two attacks ask it in a way that cannot be answered by reading the position only.
//
// HELIX: the two guns fire arms that sweep towards each other and braid across the middle. Each arm has a colour, and on a beat the arms SWAP their
// colours. The beat is announced: the muzzles flash in their new colour for 8 frames first. A step is 4 frames, the colours swap every 8 steps.
constexpr int HELIX_STEP = 4;
constexpr int HELIX_STEPS = 26;
constexpr int HELIX_SWAP_STEPS = 8;
constexpr int HELIX_WARN = 10; // frames before a swap

Polarity HelixPol(int arm, int step)
{
    return ((arm + step / HELIX_SWAP_STEPS) % 2 == 0) ? g_APol : Opp(g_APol);
}

void FireColorHelix()
{
    const float speed = 0.0098f;
    if (g_AT >= 1 && g_AT % HELIX_STEP == 1)
    {
        const int step = g_AT / HELIX_STEP;
        if (step < HELIX_STEPS)
        {
            const float sweep = 0.80f * std::sin(static_cast<float>(step) * 0.26f);
            for (int arm = 0; arm < 2; ++arm)
            {
                float mx, my;
                MuzzlePoint(arm, mx, my);
                Shoot(mx - 0.02f, my, PI + (arm == 0 ? sweep : -sweep), speed, HelixPol(arm, step));
            }
            g_Ship[0].fireFlash = g_Ship[1].fireFlash = 3;
            if (step % 6 == 0) GameAudio::Play(GameSound::BossCannonFire);
        }
    }
    // the announcement of a swap: the muzzles light in the colours they are about to fire
    for (int swapStep = HELIX_SWAP_STEPS; swapStep < HELIX_STEPS; swapStep += HELIX_SWAP_STEPS)
    {
        const int at = swapStep * HELIX_STEP;
        if (g_AT == at - HELIX_WARN || g_AT == at - HELIX_WARN / 2)
        {
            for (int arm = 0; arm < 2; ++arm)
            {
                float mx, my;
                MuzzlePoint(arm, mx, my);
                MuzzleFlash(mx, my, HelixPol(arm, swapStep));
            }
        }
    }
    if (g_AT >= HELIX_STEPS * HELIX_STEP + 24) FinishAttack();
}

// SPLIT BLOOM: three half rings leave the nose, 28 frames apart. A ring is cut into sectors of two colours; the next ring is cut the other way round,
// so the colour in front of the ship changes from ring to ring. The speed is low on purpose: the whole ring can be read before it arrives.
void FireSplitBloom()
{
    constexpr int RINGS = 3;
    constexpr int GAP = 28;
    constexpr int COUNT = 15; // bullets of a ring: five sectors of three
    for (int ring = 0; ring < RINGS; ++ring)
    {
        if (g_AT != 1 + ring * GAP) continue;
        float mx, my;
        BodyTip(mx, my);
        for (int i = 0; i < COUNT; ++i)
        {
            const float heading = PI + (static_cast<float>(i) - static_cast<float>(COUNT - 1) * 0.5f) * 0.235f;
            const Polarity pol = ((i / 3 + ring) % 2 == 0) ? g_APol : Opp(g_APol);
            Shoot(mx, my, heading, 0.0090f, pol);
        }
        MuzzleFlash(mx, my, ring % 2 == 0 ? g_APol : Opp(g_APol));
        g_Ship[0].fireFlash = g_Ship[1].fireFlash = 6;
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= (RINGS - 1) * GAP + 40) FinishAttack();
}

// The comb: five streams of one colour that sweep across the screen together. The way through is between the teeth, and it moves.
void FireCombSweep()
{
    float mx, my;
    BodyTip(mx, my);
    constexpr int DURATION = 76;
    constexpr int EVERY = 6;
    if (g_AT >= 1 && g_AT <= DURATION && g_AT % EVERY == 1)
    {
        const float sweep = 0.52f * std::sin(static_cast<float>(g_AT) * 0.085f + g_AFa);
        for (int k = -2; k <= 2; ++k)
        {
            Shoot(mx, my, PI + sweep + static_cast<float>(k) * 0.24f, 0.0098f, g_APol);
        }
        g_Ship[0].fireFlash = g_Ship[1].fireFlash = 4;
        if (g_AT % 18 == 1) GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= DURATION + 20) FinishAttack();
}

// ---- phase 3 ------------------------------------------------------------------
void FireDoubleSpiral()
{
    // The two empty turret sockets fire against each other, but NOT at the same time: first the red one on its own, then the blue one, and only in the
    // last part both together (the braid). The player has seen both colours alone before they meet.
    constexpr int STEPS = 34;
    const float speed = 0.0090f + 0.0008f * P3Heat();
    const int step = g_AT / 3;
    if (g_AT % 3 == 1 && step < STEPS)
    {
        const float sweep = 0.95f * std::sin(static_cast<float>(step) * 0.30f);
        const bool red = step < 10 || step >= 24;
        const bool blue = (step >= 12 && step < 22) || step >= 24;
        float rx, ry, bx, by;
        TurretPoint(0, rx, ry);
        TurretPoint(1, bx, by);
        if (red) Shoot(rx - 0.05f, ry, PI + sweep, speed, Polarity::Red);

        if (blue) Shoot(bx - 0.05f, by, PI - sweep, speed, Polarity::Blue);
        if (g_AT % 12 == 1) GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= STEPS * 3 + 8) FinishAttack();
}

void FirePrismRing()
{
    // three fans that flip their colour at the line: the first two have the same colour, the third the other one, 37 frames apart

    if (g_AT == 1 || g_AT == 38 || g_AT == 75)
    {
        const int n = (g_AT - 1) / 37;
        const Polarity pol = (n == 2) ? Opp(g_APol) : g_APol;
        const float a0 = PI * 0.5f + 0.12f;
        const float a1 = PI * 1.5f - 0.12f;
        const int count = 13;
        for (int i = 0; i < count; ++i)
        {
            const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(count - 1);
            Bolt* b = NewBolt(WeakX() - 0.07f, g_Y, a, 0.0085f, pol);
            if (b) b->flips = true;
        }
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= 98) FinishAttack();
}

void FireRailShot()
{
    // Two rail strikes. The first lane was shown during the telegraph, the second lane is shown for 22 frames after the
    // first strike and is taken from where the player is THEN. White = cannot be absorbed.
    if (g_AT == 1)
    {
        g_BeamHalf = RAIL_HALF_H;
        g_BeamLen = 16;
        g_BeamT = 0;
        g_Beam[0].active = true;
        g_Beam[0].y = g_AFa;
        g_Beam[0].xl = g_RailXl;
        g_Beam[1].active = false;
        GameAudio::Play(GameSound::BossLaserFire);
        PushShake(8, 0.006f);
    }
    if (g_AT == 17)
    {
        g_Beam[0].active = false;
        g_AFb = Clamp(g_PY, -0.62f, 0.62f);
        g_RailXl2 = g_PX;
        GameAudio::Play(GameSound::BossDashWarning);
    }
    if (g_AT == 48) // (the second lane is shown for 31 frames)
    {
        g_BeamHalf = RAIL_HALF_H;
        g_BeamLen = 16;
        g_BeamT = 0;
        g_Beam[0].active = true;
        g_Beam[0].y = g_AFb;
        g_Beam[0].xl = g_RailXl2;

        GameAudio::Play(GameSound::BossLaserFire);
        PushShake(8, 0.006f);
    }
    if (g_AT == 65) g_Beam[0].active = false;
    if (g_AT >= 70)
    {
        FanShot(WeakX() - 0.07f, g_Y, AimAngle(WeakX(), g_Y, g_PX, g_PY), 0.55f, 5, 0.0098f,
                g_APol); // a short fan after the strikes
        GameAudio::Play(GameSound::BossCannonFire);
        FinishAttack();
    }
}

void FireOverloadBeam()
{
    if (g_AT == 1)
    {
        g_BeamHalf = BEAM_HALF_H;
        g_BeamLen = 50;
        g_BeamT = 0;

        g_Beam[0].active = true;
        g_Beam[1].active = TwinBeams();
        GameAudio::Play(GameSound::BossLaserFire);
        PushShake(10, 0.006f);
    }

    TrackBeamLane(0.0008f); // nearly locked: it does not chase the ship for the whole beam
    if (g_AT >= 50) FinishAttack();
}

void FirePolarityStorm()
{

    // The storm comes in BANDS of one colour with a clear gap between them: 26 steps of red rain, a pause, 26 steps of blue ...

    const int block = g_AT / 32;
    const bool inBand = (g_AT % 32) < 26;
    const Polarity pol = (block % 2 == 0) ? g_APol : Opp(g_APol);
    if (g_AT % 4 == 0 && g_AT <= 96 && inBand)
    {
        const bool top = (g_AT / 4) % 2 == 0;
        const float x = RndRange(-0.15f, 0.95f);
        const float heading = (top ? PI * 1.25f : PI * 0.75f) + RndRange(-0.15f, 0.15f);
        Shoot(x, top ? 0.98f : -0.98f, heading, 0.0062f, pol);
    }
    if (g_AT % 16 == 8 && g_AT <= 96 && inBand)
    {
        Shoot(WeakX() - 0.08f, g_Y, AimAngle(WeakX(), g_Y, g_PX, g_PY), 0.0090f, pol);
    }
    if (g_AT % 12 == 6 && g_AT <= 96 && inBand) // ... and from the right edge of the screen: the storm comes from all sides but the ship's
    {
        Shoot(1.02f, RndRange(-0.80f, 0.80f), PI + RndRange(-0.14f, 0.14f), 0.0068f, pol);
    }
    if (g_AT == 1) GameAudio::Play(GameSound::BossCannonFire);
    if (g_AT >= 112) FinishAttack();
}

// Three rings out of the core, the colours alternate 38 frames apart. Every ring has a way through, but it is NOT where the ship stands: it is aimed
// to one side of it (the side changes from ring to ring), so the ship has to move to the gap.

void FireNovaRings()
{
    constexpr int COUNT = 30;
    if (g_AT == 1 || g_AT == 40 || g_AT == 79)
    {
        const int n = (g_AT - 1) / 39;
        const Polarity pol = (n % 2 == 0) ? g_APol : Opp(g_APol);
        const float cx = WeakX() - 0.07f;
        const float cy = g_Y;
        // a wide arc that is centred on the ship, with a gap that is next to the ship (not on it): the way through
        const float aim = AimAngle(cx, cy, g_PX, g_PY);
        constexpr float ARC = TWO_PI * 0.55f;
        const float step = ARC / static_cast<float>(COUNT - 1);
        const int gapAt = (COUNT - 1) / 2 + ((n % 2 == 0) ? 3 : -3);
        for (int i = 0; i < COUNT; ++i)
        {
            if (std::abs(i - gapAt) <= 3) continue; // seven bullets are missing
            Shoot(cx, cy, aim - ARC * 0.5f + step * static_cast<float>(i), 0.0082f, pol);
        }
        SpawnShock(cx, cy, 1.0f, 1.0f, 1.0f, 0.55f, 26);
        PushShake(6, 0.006f);
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= 104) FinishAttack();
}

// ---- collapse (phase 3) -----------------------------------------------------------
// The core pulls every bolt on the screen in (they slide towards it and are gone in it), then throws two rings out, one of each colour, a moment apart. Each ring has a gap of
// COLLAPSE_GAP bullets; the gap of the first ring is next to the ship on one side, the gap of the second ring on the other side: the ship has to move through both.
// The colour does not help much (half of each ring hurts whatever the ship is): this one is read by place.
constexpr int COLLAPSE_PULL = 34;  // frames of the pull
constexpr int COLLAPSE_COUNT = 30; // bullets of a ring
constexpr int COLLAPSE_GAP = 3;    // ... of which this many are missing (the way through)

void FireCollapse()
{
    const float cx = WeakX() - 0.05f;
    const float cy = g_Y;
    if (g_AT <= COLLAPSE_PULL)
    {
        for (auto& b : g_Bolts)
        {
            if (!b.active) continue;
            const float dx = cx - b.x;
            const float dy = (cy - b.y) * ASPECT;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < 0.06f)
            {
                b.active = false;
                continue;
            }
            b.heading = std::atan2(dy, dx);
            b.speed = std::max(b.speed, 0.016f);
            b.homing = false;
            b.flips = false;
        }
        if (g_AT % 10 == 1) SpawnShock(cx, cy, 1.0f, 1.0f, 1.0f, 0.45f - 0.30f * static_cast<float>(g_AT) / static_cast<float>(COLLAPSE_PULL), 14); // the pull: rings that close on the core
        if (g_AT == 1) GameAudio::Play(GameSound::BossLaserCharge);
    }
    for (int n = 0; n < 2; ++n)
    {
        if (g_AT != COLLAPSE_PULL + 1 + n * 30) continue;
        const Polarity pol = (n == 0) ? g_APol : Opp(g_APol);
        const float aim = AimAngle(cx, cy, g_PX, g_PY);
        const float step = TWO_PI / static_cast<float>(COLLAPSE_COUNT);
        const int gapAt = (n == 0) ? 2 : COLLAPSE_COUNT - 2 - COLLAPSE_GAP; // a little above the line to the ship, then a little below it
        for (int i = 0; i < COLLAPSE_COUNT; ++i)
        {
            if (i >= gapAt && i < gapAt + COLLAPSE_GAP) continue;
            Shoot(cx, cy, aim + step * (static_cast<float>(i) + 0.5f), 0.0080f, pol);
        }
        SpawnShock(cx, cy, 1.0f, 1.0f, 1.0f, 0.70f, 30);
        PushShake(10, 0.010f);
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= COLLAPSE_PULL + 30 + 40) FinishAttack();
}

// ---- ram ------------------------------------------------------------------------
bool RamActive()
{
    return g_Id == AtkId::Ram && (g_AState == AtkState::Telegraph || g_AState == AtkState::Fire);
}

float RamHomeX()
{
    return g_Phase == 1 ? BATTLE_X : (g_Phase == 3 ? CORE_BASE_X : HULL_P2_X);
}

// where the lane of the ram is at world x: a line through (g_RamXL, g_AFa)
float RamLaneY(float x)
{
    return g_AFa + g_RamSlope * (x - g_RamXL);
}

int RamHold()
{
    return g_Phase == 3 ? RAM_HOLD_CORE : RAM_HOLD;
}

float RamLaneHalf()
{
    return RAM_LANE_HULL;
}

// the lane follows the ship until RAM_LOCK frames before the charge: what is on the screen then is where the body will go
void TrackRamLane()
{
    if (g_ATimer <= RAM_LOCK) return;
    const float limit = RAM_LANE_LIMIT; // the lane never goes to the very edge: the edge of the screen is the way out
    g_AFa += Clamp(Clamp(g_PY, -limit, limit) - g_AFa, -RAM_CHASE, RAM_CHASE);
    g_RamXL = g_PX;
    g_RamSlope = -g_RamM * Clamp(g_AFa / 0.25f, -1.0f, 1.0f); // it leans towards the middle: a lane at the top comes down from the middle of the screen, a lane at the bottom up from it
}

// where the body is during the telegraph (it slides into the lane) and the charge (wind-up, dash, hold, return)
void UpdateRamMotion()
{
    float& x = g_X;
    float& y = g_Y;
    const float home = RamHomeX();
    if (g_AState == AtkState::Telegraph)
    {
        y += (RamLaneY(home) - y) * 0.14f;
        x += (home - x) * 0.10f;
        return;
    }
    const float end = RAM_END_HULL;
    const float windX = home + RAM_WIND_BACK;
    const int hold = RamHold();
    const int t = g_AT;
    if (t <= RAM_WIND)
    {
        x = Lerp(home, windX, EaseOut(static_cast<float>(t) / static_cast<float>(RAM_WIND)));
    }
    else if (t <= RAM_WIND + RAM_DASH)
    {
        const float u = static_cast<float>(t - RAM_WIND) / static_cast<float>(RAM_DASH);
        x = Lerp(windX, end, u * u); // it accelerates all the way
    }
    else if (t <= RAM_WIND + RAM_DASH + hold)
    {
        x = end + 0.006f * std::sin(static_cast<float>(t) * 1.7f); // the hull rattles against the edge
    }
    else
    {
        const float u = Clamp01(static_cast<float>(t - RAM_WIND - RAM_DASH - hold) / static_cast<float>(RAM_BACK));
        x = Lerp(end, home, u * u * (3.0f - 2.0f * u));
    }
    y = RamLaneY(x);
}

void FireRam()
{
    const int hold = RamHold();
    const int tDash = RAM_WIND + 1;
    const int tImpact = RAM_WIND + RAM_DASH + 1;
    const float noseX = g_X - VIS_W * 0.42f;
    const float noseY = g_Y;
    if (g_AT == tDash)
    {
        GameAudio::Play(GameSound::BossLaserFire);
        PushShake(6, 0.005f);
    }
    if (g_AT == tImpact)
    {
        SpawnShock(noseX, noseY, 1.0f, 1.0f, 1.0f, 0.60f, 26);
        BurstSparks(noseX, noseY, 10, 0.020f, 1.0f, 0.85f, 0.45f);
        PushShake(14, 0.012f);
        GameAudio::Play(GameSound::BossCannonBreak);
        FanShot(noseX, noseY, AimAngle(noseX, noseY, g_PX, g_PY), 0.90f, 7, 0.0085f, g_APol); // the net: wherever the ship dodged to
    }
    if (g_Phase == 3 && g_AT == tImpact + 34) // the core answers with the other colour
    {
        FanShot(noseX, noseY, AimAngle(noseX, noseY, g_PX, g_PY), 0.90f, 7, 0.0085f, Opp(g_APol));
        GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= RAM_WIND + RAM_DASH + hold + RAM_BACK) FinishAttack();
}

// ---- sweep (phase 2): the hull glides down (or up) the right side of the screen and fires aimed fans from its nose --------------------------------------------
bool SweepActive()
{
    return g_Id == AtkId::Sweep && (g_AState == AtkState::Telegraph || g_AState == AtkState::Fire);
}

void UpdateSweepMotion()
{
    const float side = g_AFa;
    if (g_AState == AtkState::Telegraph)
    {
        g_Y += (side * SWEEP_Y - g_Y) * 0.10f;
        g_X += (HULL_P2_X - 0.12f - g_X) * 0.08f;
        return;
    }
    if (g_AT <= SWEEP_RUN)
    {
        const float u = Clamp01(static_cast<float>(g_AT) / static_cast<float>(SWEEP_RUN));
        const float s = u * u * (3.0f - 2.0f * u);
        g_Y = side * SWEEP_Y * (1.0f - 2.0f * s);
        g_X = Lerp(g_X, SWEEP_X + 0.06f * std::sin(u * PI), 0.12f);
    }
    else
    {
        g_Y = Lerp(g_Y, 0.0f, 0.12f);
        g_X = Lerp(g_X, HULL_P2_X, 0.10f);
    }
}

void FireSweep()
{
    if (g_AT == 1)
    {
        GameAudio::Play(GameSound::BossLaserFire);
        PushShake(6, 0.005f);
    }
    // one colour for the first half, a gap long enough to switch, the other colour for the second half
    if (g_AT <= SWEEP_RUN && g_AT % 10 == 2 && !(g_AT > 40 && g_AT < 58))
    {
        float mx, my;
        BodyTip(mx, my);
        const Polarity pol = (g_AT < SWEEP_RUN / 2) ? g_APol : Opp(g_APol);
        FanShot(mx, my, AimAngle(mx, my, g_PX, g_PY), 0.34f, 3, 0.0105f, pol);
        MuzzleFlash(mx, my, pol);
        if (g_AT % 20 == 2) GameAudio::Play(GameSound::BossCannonFire);
    }
    if (g_AT >= SWEEP_RUN + SWEEP_BACK) FinishAttack();
}

void FireTick()
{
    switch (g_Id)
    {
    case AtkId::Ram:
        FireRam();
        break;
    case AtkId::Sweep:
        FireSweep();
        break;
    case AtkId::TwinBurst:
        FireTwinBurst();
        break;
    case AtkId::PolarityFan:
        FirePolarityFan();
        break;
    case AtkId::ColorHelix:
        FireColorHelix();
        break;
    case AtkId::SplitBloom:
        FireSplitBloom();
        break;
    case AtkId::DoubleSpiral:
        FireDoubleSpiral();
        break;
    case AtkId::PrismRing:
        FirePrismRing();
        break;
    case AtkId::RailShot:
        FireRailShot();
        break;
    case AtkId::OverloadBeam:
        FireOverloadBeam();
        break;
    case AtkId::PolarityStorm:
        FirePolarityStorm();
        break;
    case AtkId::CombSweep:
        FireCombSweep();
        break;
    case AtkId::NovaRings:
        FireNovaRings();
        break;
    case AtkId::Collapse:
        FireCollapse();
        break;
    default:
        FinishAttack();
        break;
    }
}

void TickScheduler()
{
    switch (g_AState)
    {
    case AtkState::Idle:
        if (--g_ATimer <= 0) ChooseNextAttack();
        break;
    case AtkState::Telegraph:
    {
        const float progress = 1.0f - static_cast<float>(g_ATimer) / static_cast<float>(TelegraphFrames(g_Id));
        if (g_Id == AtkId::OverloadBeam) TrackBeamLane(0.0045f, true); // the lane follows the ship while the beam charges
        if (g_Id == AtkId::Ram) TrackRamLane();
        (void)progress;
        if (--g_ATimer <= 0)
        {
            g_AState = AtkState::Fire;
            g_AT = 0;
            g_AStep = 0;
        }
        break;
    }
    case AtkState::Fire:
        ++g_AT;
        FireTick();
        break;
    case AtkState::Vent:
        if (g_Phase == 1 && g_ATimer % 6 == 0)
        {
            SpawnDebris(WeakX(), g_Y + 0.05f, 2, RndRange(-0.002f, 0.001f), 0.004f, 0.06f, 28.0f, 1.0f, 0.85f, 0.35f, 0.0f);
            SpawnDebris(WeakX(), g_Y - 0.05f, 2, RndRange(-0.002f, 0.001f), -0.004f, 0.06f, 28.0f, 1.0f, 0.85f, 0.35f, 0.0f);
        }
        if (g_Phase == 3 && g_ATimer % 6 == 0)
        {
            SpawnDebris(WeakX(), g_Y, 2, RndRange(-0.003f, 0.003f), RndRange(-0.006f, 0.006f), 0.07f, 28.0f, 1.0f, 0.85f, 0.35f,
                        0.0f);
        }
        if (--g_ATimer <= 0)
        {
            g_AState = AtkState::Idle;
            g_ATimer = IdleFrames();
        }
        break;
    }
}

// ------------------------------------------------------------------ motion
float MotionScale()
{
    if (g_AState == AtkState::Vent) return 0.0f;
    if (g_AState == AtkState::Telegraph && g_Id == AtkId::OverloadBeam) return 0.60f; // it does not hang in the air while it charges: the weak point drifts, the beam has to follow it
    if (g_AState == AtkState::Telegraph || g_AState == AtkState::Fire) return 0.45f;
    return 1.0f;
}

void UpdateBolts()
{
    bool anyFlip = false;
    for (auto& b : g_Bolts)
    {
        if (!b.active) continue;
        ++b.age;
        if (b.homing && b.homingTimer > 0)
        {
            const float target = AimAngle(b.x, b.y, g_PX, g_PY);
            float d = NormalizeAngle(target - b.heading);
            d = Clamp(d, -b.turnRate, b.turnRate);
            b.heading = NormalizeAngle(b.heading + d);
            if (--b.homingTimer <= 0) b.homing = false;
        }
        float vx, vy;
        Velocity(b.heading, b.speed, vx, vy);
        const float prevX = b.x;
        b.x += vx;
        b.y += vy;
        if (b.flips)
        {
            anyFlip = true;
            if (!b.flipped && prevX > FLIP_X && b.x <= FLIP_X)
            {
                b.polarity = Opp(b.polarity);
                b.flipped = true;
                float r, g, bl;
                Colour(b.polarity, r, g, bl);
                SpawnShock(b.x, b.y, r, g, bl, 0.14f, 14);
                if (g_FlipSoundCooldown <= 0)
                {
                    GameAudio::Play(GameSound::BossFlip);
                    g_FlipSoundCooldown = 6;
                }
            }
        }
        if (b.x < -1.25f || b.x > 1.45f || b.y < -1.25f || b.y > 1.25f) b.active = false;
    }
    const float target = (anyFlip || g_Id == AtkId::PrismRing) ? 1.0f : 0.0f;
    g_FlipAlpha += (target - g_FlipAlpha) * 0.12f;
}

// the white laser of drone 1: a line from the drone, in the direction that the warning showed
float LaserDistance(float px, float py)
{
    const float dx = px - g_Laser.x0;
    const float dy = (py - g_Laser.y0) * ASPECT;
    const float c = std::cos(g_Laser.heading), s = std::sin(g_Laser.heading);
    if (dx * c + dy * s < 0.0f) return 9.0f; // (behind the drone)
    return std::fabs(-dx * s + dy * c);
}

void UpdateDroneLaser()
{
    if (g_Laser.state == 0) return;
    ++g_Laser.t;
    const int k = g_Phase >= 3 ? 1 : 0;
    Ship& s = g_Ship[1];
    if (g_Laser.state == 1)
    {
        g_Laser.x0 = s.x - 0.05f;
        g_Laser.y0 = s.y;
        if (g_Laser.t < LASER_WARN[k] - 12) // it follows the ship, then it is locked for the last 12 frames
        {
            const float target = AimAngle(g_Laser.x0, g_Laser.y0, g_PX, g_PY);
            g_Laser.heading += Clamp(NormalizeAngle(target - g_Laser.heading), -0.04f, 0.04f);
        }
        if (g_Laser.t >= LASER_WARN[k])
        {
            g_Laser.state = 2;
            g_Laser.t = 0;
            GameAudio::Play(GameSound::BossLaserFire);
            PushShake(5, 0.004f);
        }
        return;
    }
    if (g_Laser.t >= LASER_FIRE) g_Laser.state = 0;
}

// The drones on their own: the scheduler of the attacks does not know them. They move to their places, fire on their own timers, and come back after they are shot down.
void UpdateDrones()
{
    if (g_Phase < 2) return;
    const int k = g_Phase >= 3 ? 1 : 0;
    const bool hold = g_State != State::Battle || (g_AState == AtkState::Telegraph && g_Id == AtkId::OverloadBeam); // (they are silent while the overload charges)
    const bool frenzy = g_Frenzy > 0;
    if (g_Frenzy > 0) --g_Frenzy;
    if (g_ShellFlash > 0) --g_ShellFlash;
    if (BodyBeamBusy() || g_Laser.state != 0) g_BeamQuiet = BEAM_QUIET; // (the beams and the laser keep their distance from each other)
    else if (g_BeamQuiet > 0) --g_BeamQuiet;
    UpdateRail();
    for (int i = 0; i < 2; ++i)
    {
        Ship& s = g_Ship[i];
        float tx, ty;
        DroneHome(i, tx, ty);
        if (!s.alive)
        {
            s.y += (i == 0 ? 1.0f : -1.0f) * 0.0006f; // the wreck drifts away ...
            s.x += 0.0008f;
            const bool waits = g_Phase >= 3 && !g_Dying && g_Ship[1 - i].alive; // (phase 3: a drone that is down waits until the other one is down too)
            if (g_State == State::Battle && !waits && --s.respawn <= 0) // ... and a new drone flies in from its place
            {
                s.x = 1.30f; // (a new drone is called in from beyond the right edge, like the first ones)
                s.y = ty;
                ReviveShip(i);
            }
            continue;
        }
        s.x += (tx - s.x) * 0.10f;
        s.y += (ty - s.y) * 0.10f;
        s.y = Clamp(s.y, -0.78f, 0.78f);
        if (hold)
        {
            if (i == 1 && g_WhiteWarn > 0) // (the overload charges: a volley that was announced is called off, the next one comes later)
            {
                g_WhiteWarn = 0;
                g_WhiteHold = 0;
                s.fireTimer = 40;
            }
            continue;
        }
        if (i == 1 && g_WhiteWarn > 0)
        {
            ++g_WhiteWarn;
            if (s.fireTimer > 12 && g_WhiteHold == 0) // the lines follow the ship, and are locked for the last 12 frames
            {
                const float target = AimAngle(s.x - 0.07f, s.y, g_PX, g_PY);
                g_WhiteAim += Clamp(NormalizeAngle(target - g_WhiteAim), -0.05f, 0.05f);
            }
        }
        if (--s.fireTimer > 0) continue;
        const float mx = s.x - 0.07f;
        const float my = s.y;
        const float aim = AimAngle(mx, my, g_PX, g_PY);
        if (i == 0)
        {
            // a fan of one colour, the next one of the other colour
            const Polarity pol = (s.volley % 2 == 0) ? Polarity::Red : Polarity::Blue;
            s.polarity = Opp(pol); // (the glow of the drone shows the colour of the NEXT fan)
            FanShot(mx, my, aim, 0.55f + 0.05f * static_cast<float>(k), COLOUR_COUNT[k], 0.0090f + 0.0010f * static_cast<float>(k), pol);
            MuzzleFlash(mx, my, pol);
            s.fireTimer = COLOUR_PERIOD[k] / (frenzy ? 2 : 1);
        }
        else
        {
            const bool laserDue = s.volley % LASER_EVERY[k] == LASER_EVERY[k] - 1;
            if (laserDue)
            {
                // ONE laser at a time: not while the hull charges or fires a beam (or rams), not while a laser is up, not just after one
                const bool ramming = g_Id == AtkId::Ram && g_AState != AtkState::Idle;
                if (BodyBeamBusy() || ramming || g_Laser.state != 0 || g_BeamQuiet > 0)
                {
                    s.fireTimer = 10; // (it waits: the same volley is tried again)
                    continue;
                }
                g_Laser.state = 1;
                g_Laser.t = 0;
                g_Laser.x0 = mx;
                g_Laser.y0 = my;
                g_Laser.heading = aim + ((s.volley / LASER_EVERY[k]) % 2 == 0 ? 0.30f : -0.30f) * (k == 1 ? 1.3f : 1.0f); // it starts off to one side and sweeps in
                GameAudio::Play(GameSound::BossDashWarning);
                s.fireTimer = WHITE_PERIOD[k] / (frenzy ? 2 : 1);
            }
            else
            {
                if (g_WhiteWarn == 0)
                {
                    // the volley is announced first. The warning does not begin in the firing part of an attack (the player is reading a pattern then)
                    const bool busy = g_AState == AtkState::Fire || BodyBeamBusy() || g_Laser.state != 0;
                    if (busy && g_WhiteDefer < WHITE_DEFER_MAX)
                    {
                        g_WhiteDefer += 6;
                        s.fireTimer = 6;
                        continue;
                    }
                    g_WhiteDefer = 0;
                    g_WhiteHold = 0;
                    g_WhiteWarn = 1;
                    g_WhiteAim = aim;
                    GameAudio::Play(GameSound::WhiteWarn); // (the lock-on of the announcement: a sound of its own, not the sniper's)
                    s.fireTimer = WHITE_WARN[k];
                    continue;
                }
                // the warning has run out: a volley is not fired into the firing part of an attack either (it waits a little, locked)
                if ((g_AState == AtkState::Fire || BodyBeamBusy() || g_Laser.state != 0) && g_WhiteHold < WHITE_HOLD_MAX)
                {
                    ++g_WhiteHold;
                    s.fireTimer = 1;
                    continue;
                }
                for (int n = 0; n < WHITE_COUNT[k]; ++n)
                {
                    const float spread = (static_cast<float>(n) - static_cast<float>(WHITE_COUNT[k] - 1) * 0.5f) * 0.14f;
                    if (Bolt* b = NewBolt(mx, my, g_WhiteAim + spread, 0.0140f, Polarity::Blue))
                    {
                        b->white = true;
                        b->halfWidth = 0.020f;
                        b->halfHeight = 0.020f;
                    }
                }
                GameAudio::Play(GameSound::SniperFire);
                g_WhiteWarn = 0;
                g_WhiteHold = 0;
                s.fireTimer = std::max(24, WHITE_PERIOD[k] / (frenzy ? 2 : 1) - WHITE_WARN[k]); // (the warning is part of the period)
            }
        }
        s.fireFlash = 8;
        ++s.volley;
    }
    UpdateDroneLaser();
    // the run log: the shell of the core opens / closes
    if (g_Phase == 3 && g_State == State::Battle)
    {
        const bool open = CoreExposed();
        if (open != g_ShellWasOpen) Mod::LogEvent("boss_shell", open ? "open" : "closed");
        g_ShellWasOpen = open;
    }
}

void UpdatePhaseOne()
{
    if (RamActive())
    {
        UpdateRamMotion();
        SyncShipsToBody();
        return;
    }
    g_MoveT += 0.0105f * MotionScale();
    g_Y = Lerp(g_Y, 0.38f * std::sin(g_MoveT), 0.12f); // (it eases back after a ram instead of jumping)
    g_X = Lerp(g_X, BATTLE_X + 0.015f * std::sin(static_cast<float>(g_Tick) * 0.02f), 0.1f);
    SyncShipsToBody();
}

void UpdatePhaseTwo()
{
    const float t = static_cast<float>(g_Tick);
    if (RamActive() && g_Id == AtkId::Ram)
    {
        UpdateRamMotion(); // the hull rams
    }
    else if (SweepActive())
    {
        UpdateSweepMotion(); // the hull glides
    }
    else
    {
        // the empty hull idles at the right edge
        g_X = Lerp(g_X, HULL_P2_X + 0.010f * std::sin(t * 0.017f), 0.05f);
        g_Y = Lerp(g_Y, 0.04f * std::sin(t * 0.011f), 0.05f);
    }
}

void UpdatePhaseThree()
{
    // DYING: under DYING_RATE of its life the shell breaks for good and the drones go wild for a while
    if (!g_Dying && g_P3Hp <= P3_HP * DYING_RATE)
    {
        g_Dying = true;
        g_Frenzy = FRENZY_FRAMES;
        for (int i = 0; i < 2; ++i)
            if (!g_Ship[i].alive) ReviveShip(i);
        g_AttackCount = 0;
        SpawnShock(WeakX(), g_Y, 1.0f, 0.55f, 0.20f, 1.0f, 50);
        BurstShards(WeakX(), g_Y, 24, 0.030f, 0.11f);
        PushShake(18, 0.016f);
        g_HitStopRequest = std::max(g_HitStopRequest, 8);
        GameAudio::Play(GameSound::BossBerserk);
        Mod::LogEvent("boss_dying", "");
    }
    if (RamActive())
    {
        UpdateRamMotion();
        return;
    }
    g_MoveT += 0.0155f * MotionScale() * (1.0f + 0.2f * P3Heat());
    g_Y = Lerp(g_Y, 0.40f * std::sin(g_MoveT), 0.12f);
    g_X = Lerp(g_X, CORE_BASE_X + 0.03f * std::sin(static_cast<float>(g_Tick) * 0.03f), 0.12f);
    if (g_Tick % 12 == 0) // embers rise from the two holes in the wings
    {
        float x, y;
        HullPoint(240.0f / 602.0f, 140.0f / 684.0f, x, y); // (the tear in the upper blade)
        SpawnDebris(x, y, 2, RndRange(-0.001f, 0.001f), 0.002f, 0.05f, 26.0f, 1.0f, 0.70f, 0.30f, 0.0f);
        HullPoint(240.0f / 602.0f, 544.0f / 684.0f, x, y);
        SpawnDebris(x, y, 2, RndRange(-0.001f, 0.001f), -0.002f, 0.05f, 26.0f, 1.0f, 0.70f, 0.30f, 0.0f);
    }
}

void UpdateBattle()
{
    if (g_Vent > 0) --g_Vent;
    if (g_Beam[0].active || g_Beam[1].active) ++g_BeamT;
    if (g_Phase == 1)
        UpdatePhaseOne();
    else if (g_Phase == 2)
        UpdatePhaseTwo();
    else
        UpdatePhaseThree();
    UpdateDrones();
    TickScheduler();
}

void ResetAll()
{
    g_State = State::Inactive;
    g_StateTimer = 0;
    g_Phase = 1;
    g_Tick = 0;
    g_X = START_X;
    g_Y = 0.0f;
    g_PrevX = START_X;
    g_PrevY = 0.0f;
    g_MoveT = 0.0f;
    g_P1Hp = P1_HP;
    g_P2Hp = P2_BODY_HP;
    g_P3Hp = P3_HP;
    g_Berserk = false;
    g_Vent = 0;
    g_StatOverloads = g_StatBreaks = 0;
    g_AbsorbBonus = 0;
    g_BodyFlash = g_CoreFlash = 0;
    for (int i = 0; i < 2; ++i)
    {
        g_Ship[i] = {};
        g_Ship[i].hp = g_Ship[i].maxHp = P1_HP;
        g_Ship[i].alive = true;
        g_Ship[i].polarity = (i == 0) ? Polarity::Red : Polarity::Blue;
    }
    g_Laser = DroneLaser{};
    g_WhiteWarn = g_WhiteHold = g_WhiteDefer = g_BeamQuiet = 0;
    g_Frenzy = 0;
    g_Dying = false;
    g_ShellWasOpen = true;
    g_ShellFlash = 0;
    g_RailExt = 0.0f;
    g_TransT = 0;
    g_TransTo = 2;
    g_CoreScale = 0.0f;
    g_WreckFade = 1.0f;
    g_FlashAlpha = 0.0f;
    g_BeamHalf = BEAM_HALF_H;
    g_BeamLen = 62;
    g_BeamT = 0;
    g_BannerT = 0;
    g_BannerPhase = 0;
    g_DefeatTimer = 0;
    g_FinalBlast = false;
    g_LastId = AtkId::None;
    g_FlipAlpha = 0.0f;
    g_HitStopRequest = 0;
    g_ShakeCount = 0;
    g_EnergyReward = 0;
    g_HitSoundCooldown = 0;
    g_FlipSoundCooldown = 0;
    ResetScheduler(60);
    ClearBolts();
    ClearEffects();
    SyncShipsToBody();
}
}

// =====================================================================================
//  public API : logic
// =====================================================================================
using namespace BossInternal;

void GameBoss::Initialize()
{
    ResetAll();
}

void GameBoss::Start()
{
    if (g_State != State::Inactive) return;
    ResetAll();
    g_State = State::Warning;
    g_StateTimer = WARNING_FRAMES;
    GameAudio::Play(GameSound::BossWarning);
}

void GameBoss::Update(const Player& player)
{
    g_PX = player.x;
    g_PY = player.y;
    g_PrevX = g_X;
    g_PrevY = g_Y;
    for (auto& s : g_Ship)
    {
        s.prevX = s.x; // (render interpolation)
        s.prevY = s.y;
    }
    for (auto& b : g_Bolts)
    {
        b.prevX = b.x;
        b.prevY = b.y;
    }
    ++g_Tick;
    UpdateEffects();

    switch (g_State)
    {
    case State::Inactive:
        break;
    case State::Warning:
        if (--g_StateTimer <= 0) g_State = State::Entering;
        break;
    case State::Entering:
        g_X -= ENTER_SPEED;
        SyncShipsToBody();
        if (g_X <= BATTLE_X)
        {
            g_X = BATTLE_X;
            g_State = State::Battle;
            g_BannerT = 175;
            g_BannerPhase = 1;
            ResetScheduler(70);
        }
        break;
    case State::Battle:
        UpdateBattle();
        UpdateBolts();
        break;
    case State::Transition:
        UpdateTransition();
        UpdateBolts();
        break;
    case State::Defeated:
        UpdateDefeat();
        break;
    }
}

bool GameBoss::CheckPlayerBullets(Bullet bullets[], int bulletCount)
{
    if (g_State != State::Battle) return false;
    for (int i = 0; i < bulletCount; ++i)
    {
        Bullet& bullet = bullets[i];
        if (!bullet.active) continue;
        int hitShip = -2;
        const float hx = bullet.x + bullet.halfWidth;
        const float hy = bullet.y;
        // 1. the drones (phase 2 and 3) are in front of the hull, then the weak points (the citadel, the reactor)
        if (g_Phase >= 2)
        {
            for (int s = 0; s < 2; ++s)
            {
                if (!g_Ship[s].alive) continue;
                if (Collision::CheckAABB(bullet.x, bullet.y, bullet.halfWidth, bullet.halfHeight, ShipHitX(s), g_Ship[s].y,
                                         SHIP_HALF_W, SHIP_HALF_H))
                {
                    hitShip = s;
                    break;
                }
            }
        }
        if (hitShip == -2 && g_Phase == 1)
        {
            if (Collision::CheckAABB(bullet.x, bullet.y, bullet.halfWidth, bullet.halfHeight, BodyX(), g_Y, BODY_HALF_W,
                                     BODY_HALF_H))
            {
                hitShip = -1;
            }
        }
        else if (hitShip == -2 && g_Phase == 3
                 && Collision::CheckAABB(bullet.x, bullet.y, bullet.halfWidth, bullet.halfHeight, WeakX(), g_Y, CoreHalfW(), CoreHalfH()))
        {
            hitShip = -1;
        }
        if (hitShip == -2)
        {
            // 2. the whole hull can be hurt (in phase 3 only while the core is open: HitBoss stops the shot on the shell otherwise)
            if (!(HullSolidAt(hx, hy) || HullSolidAt(bullet.x, hy))) continue;
            hitShip = -1;
        }
        bullet.active = false;
        const float dealt = Mod::DamageMul() * Mod::OnShotHit(1001 + hitShip); // AFTERGLOW, FOCUS (the core / the hull: 1000, a drone: 1001 / 1002)
        if (HitBoss(hitShip, dealt, bullet.polarity, false, hx, hy)) return true;
        if (g_State != State::Battle) return false;
    }
    return false;
}

bool GameBoss::CheckSpecialShotHit(float shotStartX, float shotEndX, float shotCenterY, float shotHalfHeight, int damage, float scale)
{
    if (g_State != State::Battle || damage <= 0) return false;
    const float shotTop = shotCenterY + shotHalfHeight;
    const float shotBottom = shotCenterY - shotHalfHeight;
    const float amount = static_cast<float>(damage) * SPECIAL_SCALE * Mod::DamageMul() * scale;
    auto overlaps = [&](float cx, float cy, float hw, float hh)
    { return shotEndX >= cx - hw && shotStartX <= cx + hw && shotTop >= cy - hh && shotBottom <= cy + hh; };
    const bool breaks = shotHalfHeight >= BREAK_MIN_HALF_H; // (together with Interruptible(): an overload is charging)
    // the beam hurts wherever it crosses the hull (phase 3: only while the core is open, see HitBoss); it breaks an overload when it reaches the weak point
    float hitX = 0.0f;
    const bool onHull = HullInBand(shotStartX, shotEndX, shotCenterY, shotHalfHeight, hitX);
    if (g_Phase >= 2)
    {
        for (int s = 0; s < 2 && g_State == State::Battle; ++s)
        {
            if (!g_Ship[s].alive) continue;
            if (overlaps(ShipHitX(s), g_Ship[s].y, SHIP_HALF_W, SHIP_HALF_H))
                HitBoss(s, amount, Polarity::Red, true, ShipHitX(s) - SHIP_HALF_W, Clamp(shotCenterY, g_Ship[s].y - SHIP_HALF_H, g_Ship[s].y + SHIP_HALF_H));
        }
    }
    if (g_State != State::Battle) return false;
    const float weakW = g_Phase == 3 ? CoreHalfW() : BODY_HALF_W;
    const float weakH = g_Phase == 3 ? CoreHalfH() : BODY_HALF_H;
    const bool weak = overlaps(WeakX(), g_Y, weakW, weakH);
    if (!weak && !onHull) return false;
    if (weak && breaks && Interruptible()) BreakOverload(WeakX() - weakW, g_Y);
    const float fxX = weak ? WeakX() - weakW : hitX;
    const bool killed = HitBoss(-1, amount, Polarity::Red, true, fxX, Clamp(shotCenterY, g_Y - weakH, g_Y + weakH));
    return g_Phase == 2 ? false : killed;
}

// BACKUP CORE: the blast of the revive takes this much of the life of the phase the boss is in (no resistance, no break)
bool GameBoss::DamagePhaseShare(float share)
{
    if (g_State != State::Battle || share <= 0.0f) return false;
    const float life = g_Phase == 1 ? P1_HP : (g_Phase == 2 ? P2_BODY_HP : P3_HP);
    const float raw = life * share;
    const float m = DamageMultiplier(-1, Polarity::Red, true);
    return HitBoss(-1, m > 0.0f ? raw / m : raw, Polarity::Red, true, WeakX(), g_Y);
}

bool GameBoss::TakeDamage(int damage)
{
    if (g_State != State::Battle || damage <= 0) return false;
    int ship = -1;
    if (g_Phase == 3 && !CoreExposed()) // the damage of a module goes to a drone while the shell of the core is closed
        ship = g_Ship[0].alive ? 0 : 1;
    return HitBoss(ship, static_cast<float>(damage) * Mod::DamageMul(), Polarity::Red, true, g_X, g_Y);
}

BossBulletHitResult GameBoss::CheckPlayerCollision(const Player& player, bool playerIsInvincible)
{
    if (g_State != State::Battle) return BossBulletHitResult::None;

    // beams
    for (const Beam& beam : g_Beam)
    {
        if (!beam.active) continue;
        const float hurtHalfHeight = GamePlayer::GetHurtHalfHeight(player);
        const float lineY = BeamLineY(beam.y, beam.xl, player.x);
        const float half = g_BeamHalf * BeamSlantScale(beam.y, beam.xl);
        const bool overlap = (player.y + hurtHalfHeight >= lineY - half) && (player.y - hurtHalfHeight <= lineY + half) && player.x < BeamOriginX();
        if (overlap && !playerIsInvincible)
        {
            g_LastHitDamage = (g_Id == AtkId::OverloadBeam) ? OverloadDamage(Balance::OVERLOAD_BEAM_HP_RATE) : ScaledHit(Balance::DMG_BOSS_BEAM, Balance::BOSS_P3_HIT_MUL);
            std::snprintf(g_LastHitSource, sizeof g_LastHitSource, "boss_beam_%s", AtkName(static_cast<int>(g_Id)));
            return BossBulletHitResult::Damaged;
        }
    }

    // the laser of drone 1
    if (g_Laser.state == 2 && !playerIsInvincible && LaserDistance(player.x, player.y) < LASER_HALF + GamePlayer::GetHurtHalfHeight(player) * ASPECT)
    {
        g_LastHitDamage = ScaledHit(Balance::DMG_BOSS_BEAM, Balance::BOSS_P3_HIT_MUL);
        std::snprintf(g_LastHitSource, sizeof g_LastHitSource, "boss_beam_drone_laser");
        return BossBulletHitResult::Damaged;
    }

    // bullets
    for (auto& b : g_Bolts)
    {
        if (!b.active) continue;
        // same colour: absorbed by the whole ship box. Other colour (and a white needle): only the small hurt box hurts.
        const bool same = !b.white && (player.polarity == b.polarity);
        const float boxHalfWidth = same ? player.halfWidth : GamePlayer::GetHurtHalfWidth(player);

        const float boxHalfHeight = same ? player.halfHeight : GamePlayer::GetHurtHalfHeight(player);
        if (!Collision::CheckAABB(player.x, player.y, boxHalfWidth, boxHalfHeight, b.x, b.y, b.halfWidth, b.halfHeight)) continue;
        b.active = false;
        if (same)
        {
            if (b.homing) ++g_AbsorbBonus;
            return BossBulletHitResult::Absorbed;
        }
        if (playerIsInvincible) return BossBulletHitResult::None;
        g_LastHitDamage = b.damage > 0 ? b.damage : ScaledHit(Balance::DMG_BOSS_BOLT, Balance::BOSS_P3_BOLT_MUL);
        if (b.white)
            std::snprintf(g_LastHitSource, sizeof g_LastHitSource, "white_boss_drone");
        else
            std::snprintf(g_LastHitSource, sizeof g_LastHitSource, "boss_%s", AtkName(b.atk));
        return BossBulletHitResult::Damaged;
    }

    // body contact: the weak point, a drone, or any hull (flying into the ship hurts)
    if (!playerIsInvincible)
    {
        const float hw = GamePlayer::GetHurtHalfWidth(player);
        const float hh = GamePlayer::GetHurtHalfHeight(player);
        bool touch = false;
        if (g_Phase == 1)
        {
            touch = Collision::CheckAABB(player.x, player.y, hw, hh, BodyX(), g_Y, BODY_HALF_W, BODY_HALF_H);
        }
        else if (g_Phase == 3)
        {
            touch = Collision::CheckAABB(player.x, player.y, hw, hh, WeakX(), g_Y, CoreHalfW(), CoreHalfH());
        }
        for (int s = 0; s < 2 && g_Phase >= 2; ++s)
        {
            if (g_Ship[s].alive && Collision::CheckAABB(player.x, player.y, hw, hh, ShipHitX(s), g_Ship[s].y, SHIP_HALF_W, SHIP_HALF_H))
                touch = true;
        }
        if (!touch)
        {
            // five points of the hurt box against the silhouette of the hull

            const float sx = hw * 0.6f;
            const float sy = hh * 0.6f;
            touch = HullSolidAt(player.x, player.y) || HullSolidAt(player.x - sx, player.y - sy)
                    || HullSolidAt(player.x + sx, player.y - sy) || HullSolidAt(player.x - sx, player.y + sy)
                    || HullSolidAt(player.x + sx, player.y + sy);
        }
        if (touch)
        {
            g_LastHitDamage = ScaledHit(Balance::DMG_BOSS_BODY, Balance::BOSS_P3_HIT_MUL);
            std::snprintf(g_LastHitSource, sizeof g_LastHitSource, "boss_body_%s", AtkName(static_cast<int>(g_Id)));
            return BossBulletHitResult::Damaged;
        }
    }
    return BossBulletHitResult::None;
}

int GameBoss::ConsumeAbsorbBonus()
{
    const int v = g_AbsorbBonus;
    g_AbsorbBonus = 0;
    return v;
}

int GameBoss::GetLastHitDamage()
{
    return g_LastHitDamage;
}

const char* GameBoss::GetLastHitSource()
{
    return g_LastHitSource;
}

bool GameBoss::IsActive()
{
    return g_State != State::Inactive;
}
bool GameBoss::IsWarning()
{
    return g_State == State::Warning;
}
bool GameBoss::IsArriving()
{
    return g_State == State::Warning || g_State == State::Entering;
}
bool GameBoss::IsBattleStarted()
{
    return g_State == State::Battle || g_State == State::Transition;
}
bool GameBoss::IsDefeated()
{
    return g_State == State::Defeated;
}
bool GameBoss::IsDefeatAnimationFinished()
{
    return g_State == State::Defeated && g_DefeatTimer <= 0;
}
bool GameBoss::IsBerserk()
{
    return g_Berserk && g_State != State::Defeated;
}
bool GameBoss::IsTransitioning()
{
    return g_State == State::Transition;
}
int GameBoss::GetPhase()
{
    return g_Phase;
}
float GameBoss::GetProgress()
{
    if (g_State == State::Defeated) return 1.0f;
    // the phases are one after the other: what is gone of the bar of the phase that runs, plus the whole life of the phases before it
    float done = 0.0f;
    if (g_Phase == 1)
        done = P1_HP - g_P1Hp;
    else if (g_Phase == 2)
        done = P1_HP + (P2_BODY_HP - g_P2Hp);
    else
        done = P1_HP + P2_BODY_HP + (P3_HP - g_P3Hp);
    return Clamp01(done / TOTAL_HP);
}

BossHudInfo GameBoss::GetHudInfo()
{
    BossHudInfo info;
    info.visible = (g_State == State::Battle || g_State == State::Transition);
    info.phase = g_Phase;
    info.segmentWeight[0] = P1_HP / TOTAL_HP;
    info.segmentWeight[1] = P2_BODY_HP / TOTAL_HP;
    info.segmentWeight[2] = P3_HP / TOTAL_HP;
    info.segmentRate[0] = (g_Phase == 1) ? Clamp01(g_P1Hp / P1_HP) : 0.0f;
    float p2 = 1.0f;
    if (g_Phase == 2)
    {
        p2 = Clamp01(g_P2Hp / P2_BODY_HP);
    }
    else if (g_Phase > 2)
    {
        p2 = 0.0f;
    }
    info.segmentRate[1] = p2;
    info.segmentRate[2] = (g_Phase >= 3) ? Clamp01(g_P3Hp / P3_HP) : 1.0f;
    for (int i = 0; i < 2; ++i)
    {
        info.shipAlive[i] = (g_Phase >= 2) && g_Ship[i].alive;
        info.shipRate[i] = info.shipAlive[i] ? Clamp01(g_Ship[i].hp / DroneHp()) : 0.0f;
        info.shipRespawn[i] = (g_Phase >= 2 && !g_Ship[i].alive) ? Clamp01(1.0f - static_cast<float>(g_Ship[i].respawn) / static_cast<float>(DRONE_RESPAWN[g_Phase >= 3 ? 1 : 0])) : 0.0f;
    }
    info.shellClosed = g_Phase == 3 && g_State == State::Battle && !CoreExposed();
    info.overheated = g_Vent > 0;
    info.overload = Interruptible();
    info.overloadHint = Meta::OverloadBreaks() < 3;
    info.overloadRate = info.overload ? 1.0f - static_cast<float>(g_ATimer) / static_cast<float>(TelegraphFrames(g_Id)) : 0.0f;
    info.berserk = g_Berserk;
    info.transition = (g_State == State::Transition);
    info.breaks = g_StatBreaks;
    if (g_BannerT > 0)
    {
        info.bannerPhase = g_BannerPhase;
        const float fadeIn = Clamp01(static_cast<float>(175 - g_BannerT) / 14.0f);
        const float fadeOut = Clamp01(static_cast<float>(g_BannerT) / 30.0f);
        info.bannerAlpha = std::min(fadeIn, fadeOut);
    }
    return info;
}

int GameBoss::ConsumeHitStop()
{
    const int v = g_HitStopRequest;
    g_HitStopRequest = 0;
    return v;
}

bool GameBoss::ConsumeShake(int& duration, float& strength)
{
    if (g_ShakeCount <= 0) return false;
    --g_ShakeCount;
    duration = g_ShakeDuration[g_ShakeCount];
    strength = g_ShakeStrength[g_ShakeCount];
    return true;
}

void GameBoss::FreezeForResult()
{
    g_Beam[0].active = g_Beam[1].active = false;
    g_AState = AtkState::Idle;
    g_ATimer = 1000000;
    g_Id = AtkId::None;
    g_FlipAlpha = 0.0f;
}

int GameBoss::ConsumeEnergyReward()
{
    const int v = std::min(g_EnergyReward, 20);
    g_EnergyReward -= v;
    return v;
}

// =====================================================================================
//  drawing
// =====================================================================================

// ------------------------------------------------------------------ support for the roguelite modules (rogue/)
int GameBoss::AbsorbSamePolarityInZone(float centerX, float centerY, float halfWidth, float halfHeight, Polarity polarity)
{
    if (g_State != State::Battle && g_State != State::Transition) return 0;

    int absorbed = 0;
    for (auto& b : g_Bolts)
    {
        if (!b.active || b.white || b.polarity != polarity) continue;
        if (!Collision::CheckAABB(centerX, centerY, halfWidth, halfHeight, b.x, b.y, b.halfWidth, b.halfHeight)) continue;
        b.active = false;
        if (b.homing) ++g_AbsorbBonus;
        ++absorbed;
    }
    return absorbed;
}

void GameBoss::ConvertBulletsToEnergy()
{
    ConvertBoltsToMotes(); // the motes fly to the player and come back through ConsumeEnergyReward()
}

void GameBoss::ClearBullets()
{
    ClearBolts();
}

int GameBoss::GetBoltsForSim(SimBolt* out, int capacity)
{
    int n = 0;
    for (const Bolt& b : g_Bolts)
    {
        if (!b.active || n >= capacity) continue;
        float vx, vy;
        Velocity(b.heading, b.speed, vx, vy);
        out[n++] = SimBolt{b.x, b.y, vx, vy, b.halfWidth, b.halfHeight, IsRed(b.polarity), b.white};
    }
    return n;
}

void GameBoss::GetPodsForSim(SimPod out[2])
{
    for (int i = 0; i < 2; ++i)
    {
        out[i] = SimPod{g_Ship[i].x, g_Ship[i].y, IsRed(g_Ship[i].polarity), g_Ship[i].alive};
    }
}

bool GameBoss::IsCoreExposedForSim()
{
    return CoreExposed();
}

int GameBoss::GetDroneRespawnForSim(int i)
{
    return (i >= 0 && i < 2 && !g_Ship[i].alive) ? g_Ship[i].respawn : 0;
}

int GameBoss::GetDroneVolleysForSim(int i)
{
    return (i >= 0 && i < 2) ? g_Ship[i].volley : 0;
}

int GameBoss::GetLaserStateForSim()
{
    return g_Laser.state;
}

bool GameBoss::GetHullForSim(float& x, float& y)
{
    x = WeakX();
    y = g_Y;
    return true;
}

int GameBoss::GetAttackForSim()
{
    return static_cast<int>(g_Id);
}

float GameBoss::GetLaneSlopeForSim()
{
    if (g_Id == AtkId::Ram) return g_RamSlope;
    const float dx = g_Beam[0].xl - BeamOriginX();
    if ((g_Beam[0].active || g_Id == AtkId::OverloadBeam || g_Id == AtkId::RailShot) && std::fabs(dx) > 0.05f) return (g_Beam[0].y - g_Y) / dx;
    return 0.0f;
}

bool GameBoss::GetInterruptWindowForSim(int& framesLeft)
{
    framesLeft = Interruptible() ? g_ATimer : 0;
    return Interruptible();
}

void GameBoss::GetBreakStatsForSim(int& overloads, int& breaks)
{
    overloads = g_StatOverloads;
    breaks = g_StatBreaks;
}

int GameBoss::GetLanesForSim(SimLane* out, int capacity)
{
    int n = 0;
    auto add = [&](float y, float half, int frames, float chase = 0.0f, bool mirror = false, float minAbs = 0.0f)
    {
        if (n < capacity) out[n++] = SimLane{y, half, frames, chase, mirror, minAbs};
    };
    if (g_State != State::Battle) return 0;
    for (int k = 0; k < 2; ++k)
    {
        if (g_Beam[k].active) add(BeamLineY(g_Beam[k].y, g_Beam[k].xl, g_PX), g_BeamHalf * BeamSlantScale(g_Beam[k].y, g_Beam[k].xl), 0);
    }
    if (g_AState == AtkState::Telegraph && g_Id == AtkId::OverloadBeam)
    {
        add(g_Beam[0].y, BEAM_HALF_H, g_ATimer, 0.0045f, false, TwinBeams() ? BEAM_MIN_SEP : 0.0f);
        if (TwinBeams()) add(g_Beam[1].y, BEAM_HALF_H, g_ATimer, 0.0045f, true, BEAM_MIN_SEP);
    }
    if (g_Id == AtkId::Ram && (g_AState == AtkState::Telegraph || g_AState == AtkState::Fire))
    {
        const float half = RamLaneHalf();
        const int hold = RamHold();
        if (g_AState == AtkState::Telegraph)
        {
            // the lane follows the ship until it is locked; it hurts when the body reaches the ship's column (about RAM_WIND + 60 % of the dash)
            if (n < capacity) out[n++] = SimLane{RamLaneY(g_PX), half, (std::max)(0, g_ATimer - RAM_LOCK), RAM_CHASE, false, 0.0f, RAM_LOCK + RAM_WIND + RAM_DASH * 6 / 10};
        }
        else if (g_AT < RAM_WIND + RAM_DASH + hold)
        {
            add(RamLaneY(g_PX), half, 0);
        }
    }
    if (g_Laser.state != 0) // the laser of drone 1, where it crosses the column of the ship
    {
        const float c = std::cos(g_Laser.heading), sn = std::sin(g_Laser.heading);
        if (c < -0.05f)
        {
            const float y = g_Laser.y0 + (g_PX - g_Laser.x0) * (sn / c) / ASPECT;
            const float half = LASER_HALF / ASPECT / std::fabs(c);
            add(y, half, g_Laser.state == 1 ? std::max(0, LASER_WARN[g_Phase >= 3 ? 1 : 0] - g_Laser.t) : 0);
        }
    }
    if (g_Id == AtkId::RailShot)
    {
        if (g_AState == AtkState::Telegraph) add(BeamLineY(g_AFa, g_RailXl, g_PX), RAIL_HALF_H * BeamSlantScale(g_AFa, g_RailXl), g_ATimer);
        else if (g_AState == AtkState::Fire && g_AT >= 17 && g_AT < 48) add(BeamLineY(g_AFb, g_RailXl2, g_PX), RAIL_HALF_H * BeamSlantScale(g_AFb, g_RailXl2), 48 - g_AT);
    }
    return n;
}

bool GameBoss::GetTargetPosition(float& x, float& y)
{
    if (g_State != State::Battle) return false;

    if (g_Phase == 3 && !CoreExposed())
    {
        // the shell is closed: the drone that is nearer to the ship is the target
        int best = -1;
        float bestD = 9.0f;
        for (int s = 0; s < 2; ++s)
        {
            if (!g_Ship[s].alive) continue;
            const float d = std::fabs(g_Ship[s].y - g_PY);
            if (d < bestD)
            {
                bestD = d;
                best = s;
            }
        }
        if (best >= 0)
        {
            x = ShipHitX(best);
            y = g_Ship[best].y;
            return true;
        }
    }
    x = WeakX(); // the citadel (phase 1), the hull (phase 2), the reactor (phase 3)
    y = g_Y;
    return true;
}












