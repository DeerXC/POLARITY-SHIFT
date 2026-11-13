#pragma once

#include "game/GameTypes.h"

namespace GameEffect
{
void Initialize();

void SpawnExplosion(float x, float y, float size);

void SpawnAbsorb(float x, float y, float size, Polarity polarity);

void SpawnHit(float x, float y, bool heavy);

// CRYSTALS: the one kind of debris of the game (key art of Persona 3 Reload: translucent shards, warm on red, cold on blue). A flat sliver, a fill at
// <= 0.5 opacity and one brighter edge; it flies out, spins, drags to a stop and fades. Drawn with the other effects, i.e. UNDER the enemy bullets.
// Hard limits, so that a battle never gets busy: at most CRYSTAL_MAX alive (a new one takes the place of the one that is nearest to its end),
// never more than 0.5 opaque, 12 - 40 frames of life, no screen effect of their own.
enum class CrystalTint
{
    Red,   // warm
    Blue,  // cold
    White, // pale (white bullets, glass)
    Mixed  // red and blue alternately (the boss has both colours)
};
inline CrystalTint TintOf(Polarity polarity)
{
    return polarity == Polarity::Red ? CrystalTint::Red : CrystalTint::Blue;
}
constexpr int CRYSTAL_MAX = 48;
// count crystals of about `size` (x units, jittered 0.6 - 1.4) from (x, y), thrown towards baseAngle +- spread at speedLow..speedHigh (x units per frame),
// living lifeLow..lifeHigh frames
void SpawnCrystals(float x, float y, int count, float size, CrystalTint tint, float baseAngle, float spread, float speedLow, float speedHigh, int lifeLow = 12,
                   int lifeHigh = 20);

// SIPHON: a bullet caught by the wide net is pulled into the ship along a thread of light
void SpawnSiphon(float x, float y, Polarity polarity);

// the shield of an elite broken by the beam: its six sides fly apart (radius in x units)
void SpawnShieldBreak(float x, float y, float radius);

// DEFLECT: a white bullet turned into energy
void SpawnDeflect(float x, float y);

void Update();

void Draw();
}




