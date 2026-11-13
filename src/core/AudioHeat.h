#pragma once
// The "heat" of the music (AudioLogic::Director) is fed by the SOUNDS that play: a kill, a hit, a big weapon. A long dodge has none of them: nothing is added, the
// heat halves every 3.3 s and the music falls back to the foundation in the middle of the most tense part of a fight.
// A FLOOR under the heat fixes that: while a fight runs, the heat never falls under a value that depends on what is going on in the game (not on what was heard
// lately). A wave never goes under level 1 (the groove with the motif), the boss goes up with its phases, a charging overload is the peak. Events still push the
// heat above the floor (level 2 in the busy parts of a wave). With no fight (the card screen, a menu, a dead ship) there is no floor: the heat cools down as before.
// The Director goes up at 0.30 (level 1) and 0.65 (level 2) and down at 0.40 / 0.12; the floor is applied before the cooling of each step (x 0.9965), so the values are
// kept a little above those lines.
namespace AudioLogic
{
constexpr float HEAT_FLOOR_WAVE = 0.40f;                       // a wave: level 1 all through
constexpr float HEAT_FLOOR_BOSS[3] = {0.52f, 0.70f, 0.84f};    // boss phase 1, 2, 3 (phase 2 and 3: level 2)
constexpr float HEAT_FLOOR_OVERLOAD = 0.84f;                   // the charge of an overload: the peak

inline float HeatFloorFor(bool fightActive, bool alive, bool bossFight, int bossStage, bool overloadCharging)
{
    if (!fightActive || !alive) return 0.0f;
    float minimum = HEAT_FLOOR_WAVE;
    if (bossFight)
    {
        const int stage = bossStage < 0 ? 0 : (bossStage > 2 ? 2 : bossStage);
        minimum = HEAT_FLOOR_BOSS[stage];
    }
    if (overloadCharging && minimum < HEAT_FLOOR_OVERLOAD) minimum = HEAT_FLOOR_OVERLOAD;
    return minimum;
}
} // namespace AudioLogic
