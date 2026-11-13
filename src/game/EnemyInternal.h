#pragma once

// What GameEnemy.cpp (the logic of the enemies) and EnemyDraw.cpp (how they are drawn) share: the numbers and types, the few pieces of state that the drawing reads,
// and the helpers that both need. It is not for the rest of the game: that is GameEnemy.h.

#include "game/GameTypes.h"
#include "game/GameVfx.h"

namespace EnemyInternal
{
inline constexpr float ASPECT = GameVfx::ASPECT;
inline constexpr float PI = 3.1415926535f;
inline constexpr int MUZZLE_VISUAL_MAX = 64;
inline constexpr int RAIL_WARN = 48;
inline constexpr int RAIL_FIRE = 16;
inline constexpr float RAIL_HALF = 0.024f;
inline constexpr int MINE_FUSE = 130;
inline constexpr int NEEDLE_LOCK_FRAMES = 12;
inline constexpr float RIVAL_ORBIT_X = 0.10f; // the orbit of the half ring move: radii in normalized units
inline constexpr float RIVAL_ORBIT_Y = 0.24f;


struct MuzzleVisual
{
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f;
    Polarity polarity = Polarity::Blue;
    int timer = 0;
};


// the rail of the elite sniper: shown (it does not move), then on for RAIL_FIRE steps. Kept here, not as a bullet: Combat asks GameEnemy::RailHits.
struct Rail
{
    bool active;
    int state; // 1 warning, 2 firing
    int t;
    float x0, y0, angle;
};

extern MuzzleVisual g_MuzzleVisuals[MUZZLE_VISUAL_MAX];
extern Rail g_Rails[4];
extern int g_VisualFrame;

// ---- helpers (defined in GameEnemy.cpp)
float ClampFloat(float value, float minimum, float maximum);
void VisualColor(Polarity p, float& r, float& g, float& b);
bool IsRivalPhaseTwo(const Enemy& enemy);
int RivalTier(const Enemy& enemy);
int RivalWarningFrames(EnemyAttackType type, int tier);
}  // namespace EnemyInternal
