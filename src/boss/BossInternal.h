#pragma once

// What GameBoss.cpp (the logic of the boss: its phases, its attacks, its bullets) and BossDraw.cpp (how it is drawn) share: the types and the numbers, the state that the drawing
// reads, and the helpers that both need. It is not for the rest of the game: that is GameBoss.h.

#include "game/GameTypes.h"
#include "game/GameVfx.h"

namespace BossInternal
{
inline constexpr float ASPECT = GameVfx::ASPECT;
inline constexpr float PI = 3.1415926535f;
inline constexpr float TWO_PI = 6.2831853f;
inline constexpr float P1_HP = 210.0f; // (phase 1 and 2 resist a quarter of every shot: Balance::BOSS_RESIST; phase 3 resists nothing)
inline constexpr int DRONE_RESPAWN[2] = {900, 600}; // and when it is back (15 s / 10 s)
inline constexpr int CORE_WINDOW = 420;             // phase 3: when the second drone goes down, both come back together after this long (6 s of open shell)
inline constexpr int WHITE_COUNT[2] = {2, 3};       // the needles of a volley
inline constexpr int LASER_WARN[2] = {51, 42};      // that is shown this long before it fires (0.85 s / 0.7 s)
inline constexpr int LASER_FIRE = 16;
inline constexpr int WHITE_WARN[2] = {36, 40};
inline constexpr float LASER_HALF = 0.030f;         // half height of the laser (screen units, measured across it)
inline constexpr float TEX_ASPECT = 301.0f / 342.0f;
inline constexpr float VIS_H = 0.90f; // (the carrier hull: a little smaller than the old dreadnought, its drones bigger)
inline constexpr float VIS_W = VIS_H * TEX_ASPECT * ASPECT;
inline constexpr float PX_W = VIS_W / 602.0f; // world width of one texture pixel
inline constexpr float PX_H = VIS_H / 684.0f; // world height of one texture pixel
inline constexpr float NOZZLE_U = 590.0f / 602.0f;
inline constexpr float NOZZLE_V[2] = {237.0f / 684.0f, 447.0f / 684.0f};
inline constexpr float BODY_HALF_W = 0.099f;
inline constexpr float BODY_HALF_H = 0.135f;
inline constexpr float CORE_HALF_W = 0.0585f;
inline constexpr float CORE_HALF_H = 0.081f;
inline constexpr float POD_SCALE = 1.66f; // (the drone is drawn on the hull at this scale too)
inline constexpr float POD_KX = PX_W * POD_SCALE;
inline constexpr float POD_KY = PX_H * POD_SCALE;
inline constexpr float POD_W = 260.0f * POD_KX;
inline constexpr float POD_H = 150.0f * POD_KY;
inline constexpr float POD_CX = 6.0f * POD_KX;       // sprite centre minus the centre of the turret ring
inline constexpr float POD_NOZZLE_DX = 126.0f * POD_KX;
inline constexpr float POD_NOZZLE_DY = 21.0f * POD_KY;
inline constexpr int UNDOCK_AT = 24;      // frame of the transition at which the turrets let go
inline constexpr int BOLT_MAX = 360;
inline constexpr int DEBRIS_MAX = 160;
inline constexpr int MOTE_MAX = 64;
inline constexpr int SHOCK_MAX = 14;
inline constexpr int RAM_WIND = 12;
inline constexpr int RAM_DASH = 22;
inline constexpr int RAM_LOCK = 24; // (the lane is locked this long before the dash: the time to step out of it)
inline constexpr float SWEEP_X = 0.36f;       // the hull glides at this distance from the middle
inline constexpr float FLIP_X = 0.10f;
inline constexpr float BEAM_HALF_H = 0.095f;
inline constexpr float RAIL_HALF_H = 0.065f; // the rail strike is thinner than the overload beam
inline constexpr int DEFEAT_FRAMES = 150;


// ------------------------------------------------------------------ types
enum class State
{
    Inactive,
    Warning,
    Entering,
    Battle,
    Transition,
    Defeated
};
enum class AtkState
{
    Idle,
    Telegraph,
    Fire,
    Vent
};
enum class AtkId
{
    None,
    TwinBurst,
    PolarityFan,
    ColorHelix, // phase 1: two arms that braid, and swap colours on a beat
    SplitBloom, // phase 1: half rings whose colour changes by sector, and again from ring to ring
    CombSweep, // phase 1
    DoubleSpiral,
    PrismRing,
    RailShot,
    OverloadBeam,
    PolarityStorm,
    NovaRings, // phase 3
    Ram,       // the hull / the core charges across the screen (all three phases)
    Sweep,     // phase 2: the hull glides down (or up) the right side of the screen and fires aimed fans from its nose
    Collapse   // phase 3: the core pulls every bolt in, then throws two rings out; the gap of each ring is next to the ship
};

struct Bolt
{
    float x, y, halfWidth, halfHeight;
    float speed, heading, turnRate;
    int homingTimer;
    int age;
    Polarity polarity;
    bool homing, flips, flipped, active;
    float prevX, prevY; // position at the start of the last step (render interpolation)
    int atk;            // AtkId of the attack that fired it (the run log says what hurt the ship)
    int damage;         // HP the ship loses when it is hit by this bolt (0 = Balance::DMG_BOSS_BOLT, more in phase 3)
    bool white;         // a needle of drone 1: it cannot be absorbed
    bool pulled;        // COLLAPSE: it is being pulled into the core
};

struct Ship
{
    float x, y;
    float hp, maxHp;
    bool alive;
    Polarity polarity;
    int flash;
    int fireFlash;
    float charge; // 0..1 while this ship is telegraphing
    float prevX, prevY; // position at the start of the last step (render interpolation)
    int respawn;   // a drone that is down: frames until it is back
    int fireTimer; // frames to its next volley
    int volley;    // volleys fired (drone 0: the colour of the next fan; drone 1: when the laser comes)
};

// the laser of drone 1: shown (it follows the ship, then it is locked), then on for LASER_FIRE frames
struct DroneLaser
{
    int state; // 0 off, 1 warning, 2 firing
    int t;
    float x0, y0, heading; // from the drone, in this direction (screen space, see AimAngle)
};

struct Debris
{
    float x, y, vx, vy, rotation, spin, size, life, maxLife;
    float r, g, b;
    int kind; // 0 steel shard, 1 spark, 2 soft glow
    bool active;
};

struct Mote
{
    float x, y, vx, vy, speed;
    int delay;
    Polarity polarity;
    bool active;
};

struct Shock
{
    float x, y, r, g, b, size;
    int timer, duration;
    bool active;
};

struct Beam
{
    bool active;
    float y;  // the lane: where the beam is at the column of the ship
    float xl; // which is this column (the ship at the moment the lane was set)
};

extern State g_State;
extern int g_Phase;
extern float g_X;
extern float g_Y;
extern float g_PrevX;
extern float g_PrevY;
extern float g_P1Hp;
extern bool g_Berserk;
extern int g_Vent;
extern int g_BodyFlash;
extern int g_CoreFlash;
extern Ship g_Ship[2];
extern DroneLaser g_Laser;
extern int g_WhiteWarn;
extern float g_WhiteAim;
extern int g_WhiteHold;
extern int g_Frenzy;
extern bool g_Dying;
extern int g_ShellFlash;
extern int g_TransT;
extern int g_TransTo;
extern float g_CoreScale;
extern float g_FlashAlpha;
extern int g_DefeatTimer;
extern AtkState g_AState;
extern AtkId g_Id;
extern int g_ATimer;
extern int g_AT;
extern Polarity g_APol;
extern float g_AFa;
extern float g_AFb;
extern float g_RailXl, g_RailXl2;
extern Beam g_Beam[2];
extern float g_BeamHalf;
extern int g_BeamLen;
extern int g_BeamT;
extern float g_FlipAlpha;
extern Bolt g_Bolts[BOLT_MAX];
extern Debris g_Debris[DEBRIS_MAX];
extern Mote g_Motes[MOTE_MAX];
extern Shock g_Shocks[SHOCK_MAX];
extern float g_PX;
extern float g_PY;
extern int g_DrawFrame;

// ---- helpers (defined in GameBoss.cpp)
float Clamp(float v, float lo, float hi);
float Clamp01(float v);
float EaseOut(float t);
bool IsRed(Polarity p);
void Colour(Polarity p, float& r, float& g, float& b);
void Velocity(float heading, float speed, float& vx, float& vy);
void PhaseAccent(int phase, float& r, float& g, float& b);
void HullPoint(float u, float v, float& x, float& y);
void TurretPoint(int i, float& x, float& y);
void ReactorPoint(int i, float& x, float& y);
void BodyTip(float& x, float& y);
float WeakX();
float BodyX();
void DroneHome(int i, float& x, float& y);
int ShipsAlive();
bool TwinBeams();
float BeamOriginX();
void DrawRail();
float BeamLineY(float lane, float laneX, float x);
void MuzzlePoint(int i, float& x, float& y);
int TelegraphFrames(AtkId id);
bool CoreExposed();
bool Interruptible();
Polarity HelixPol(int arm, int step);
float RamLaneY(float x);
float RamLaneHalf();
}  // namespace BossInternal
