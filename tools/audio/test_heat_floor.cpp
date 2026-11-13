// Test of core/AudioHeat.h (the floor under the heat of the music). Needs no Windows and no audio:
//   g++ -std=c++20 -Wall -Wextra -I src tools/audio/test_heat_floor.cpp -o test_heat_floor && ./test_heat_floor
#include "core/AudioHeat.h"
#include <cstdio>

using namespace AudioLogic;

static int g_Failed = 0;
#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::printf("FAILED line %d: %s\n", __LINE__, #condition); \
            ++g_Failed; \
        } \
    } while (0)

int main()
{
    // no fight, no floor
    CHECK(HeatFloorFor(false, true, false, 0, false) == 0.0f);
    CHECK(HeatFloorFor(false, true, true, 2, true) == 0.0f);   // the card screen / a menu during a boss fight
    CHECK(HeatFloorFor(true, false, true, 2, true) == 0.0f);   // the ship is dead
    // a wave: level 1 (above the line where the Director goes up, 0.30, and the cooling of one step)
    CHECK(HeatFloorFor(true, true, false, 0, false) == HEAT_FLOOR_WAVE);
    CHECK(HEAT_FLOOR_WAVE * 0.9965f > 0.30f);
    // the boss: higher with every phase; phase 2 and 3 are over the line of level 2 (0.65) even after the cooling
    CHECK(HeatFloorFor(true, true, true, 0, false) == HEAT_FLOOR_BOSS[0]);
    CHECK(HeatFloorFor(true, true, true, 1, false) == HEAT_FLOOR_BOSS[1]);
    CHECK(HeatFloorFor(true, true, true, 2, false) == HEAT_FLOOR_BOSS[2]);
    CHECK(HEAT_FLOOR_BOSS[0] > 0.30f && HEAT_FLOOR_BOSS[0] * 0.9965f < 0.65f); // phase 1 stays at level 1 unless the fight itself heats it
    CHECK(HEAT_FLOOR_BOSS[1] * 0.9965f > 0.65f && HEAT_FLOOR_BOSS[2] * 0.9965f > 0.65f);
    CHECK(HEAT_FLOOR_BOSS[0] < HEAT_FLOOR_BOSS[1] && HEAT_FLOOR_BOSS[1] < HEAT_FLOOR_BOSS[2]);
    // a stage number outside 0..2 is clamped
    CHECK(HeatFloorFor(true, true, true, -1, false) == HEAT_FLOOR_BOSS[0]);
    CHECK(HeatFloorFor(true, true, true, 7, false) == HEAT_FLOOR_BOSS[2]);
    // an overload that charges: the peak, in a wave and in the boss alike; never lowers a higher floor
    CHECK(HeatFloorFor(true, true, false, 0, true) == HEAT_FLOOR_OVERLOAD);
    CHECK(HeatFloorFor(true, true, true, 0, true) == HEAT_FLOOR_OVERLOAD);
    CHECK(HeatFloorFor(true, true, true, 2, true) >= HEAT_FLOOR_BOSS[2]);
    // the floor stays under the cap of the heat (1.5)
    CHECK(HEAT_FLOOR_OVERLOAD < 1.5f);
    if (g_Failed == 0) std::printf("heat floor: all checks passed\n");
    return g_Failed == 0 ? 0 : 1;
}
