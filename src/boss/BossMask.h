#pragma once

// Silhouette of the boss hull as a bitmap, used for the armour test: a player bullet that hits a '#' cell stops with sparks,
// a '.' cell is empty space.
// One character = 8 x 8 px of the 602 x 684 px hull textures (assets/textures/boss/hull*.png). This table is edited data:
// when a hull texture changes, the matching table below has to be updated by hand.
namespace BossMask
{
constexpr int WIDTH = 76;
constexpr int HEIGHT = 86;
constexpr float CELL = 8.0f;

extern const char* const HULL[HEIGHT];     // turrets mounted (phase 1)
extern const char* const BARE[HEIGHT];     // empty turret sockets (phase 2)
extern const char* const BERSERK[HEIGHT];  // armour blown off, reactor exposed (phase 3)
}
