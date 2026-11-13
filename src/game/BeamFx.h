#pragma once

// BeamFx: the look of everything in this game that is a LINE OF LIGHT - the piercing beam of the ship, the beams and the laser of the boss, the band of fire that EMBER
// leaves - in one place. A straight textured bar was what all of them were; this draws them as what a beam is in the games that this one learns from (Zelda's
// sword beams, Final Fantasy's limit breaks): a plasma that moves.
//
// THE RULE THAT KEEPS IT HONEST. The player has to read where a beam hurts. So every layer is split in two kinds:
//   the ZONE    a straight, hard-edged core and two thin straight rails at the edge of the danger: they never move, they ARE the hit box
//   the PLASMA  everything that lives (a sheath that writhes, two strands that braid round the core, pulses that run along it, arcs that jump off it, sparks that leave it):
//               it moves, but it stays INSIDE the rails (an arc may leave them for a moment, it is thin and dim)
// The plasma is all additive, and all of it is dimmer than a bullet: a bullet inside a beam is still the brightest thing on the screen.
//
// COORDINATES. Positions are the normalised screen coordinates of the rest of the game (x, y in -1 .. 1). Widths are in "equal-scale" units: 1 = half the width of the
// screen, on both axes (an offset of w across a beam is w / ASPECT in y).
namespace BeamFx
{
struct Rgb
{
    float r, g, b;
};

struct Palette
{
    Rgb core;  // the hot line in the middle (white)
    Rgb body;  // the sheath round it
    Rgb aura;  // the soft glow outside the sheath
    Rgb strandA; // the two strands that braid round the core, and the two rails: one colour each (the seam of the game: red above, blue below)
    Rgb strandB;
};

// the piercing beam of the ship: white, with a trace of the colour of the ship, and its opposite on the other strand
Palette PiercingPalette(bool shipIsRed);
// the beam of the boss: it fires both colours
Palette BossPalette();
// the laser of the white drone: white, cold
Palette WhitePalette();
// the band of fire
Palette FirePalette();
// the beam of the tactic CHARGE: violet, with a white core
Palette VioletPalette();

struct BeamParams
{
    float x0 = 0.0f, y0 = 0.0f; // the muzzle
    float x1 = 1.0f, y1 = 0.0f; // the far end (the beam is drawn from the muzzle to here, or to the part of it that has grown)
    float halfWidth = 0.05f;    // half of the danger zone across the beam (equal-scale units)
    float time = 0.0f;          // steps since the beam started: everything that moves is a function of it
    float envelope = 1.0f;      // 0 .. 1: the fade in and out (scales alpha and width)
    float grown = 1.0f;         // 0 .. 1: how much of the length has been drawn (a beam that is fired grows out of the muzzle in a few steps)
    float turbulence = 0.7f;    // 0 .. 1: how much the plasma writhes (0 = a calm lane, 1 = a beam that tears itself)
    float pulseSpeed = 0.9f;    // lengths per second that the pulses run (the sign is the direction: + from the muzzle to the end)
    int seed = 0;               // a beam is never identical to another one
    bool rails = true;          // the two thin straight rails at the edge of the zone
    bool braid = true;          // the two strands
    int arcs = 4;               // lightning arcs that jump off the plasma (0 = none)
};

// the whole beam: aura, sheath, braid, core, rails, pulses, arcs, sparks
void DrawBeam(const BeamParams& beam, const Palette& palette);

// what a beam does at its MUZZLE: a flare of four spikes that turns, a ring that opens, a bloom (size in equal-scale units)
void DrawMuzzle(float x, float y, float size, float time, float envelope, const Palette& palette);

// what a beam does while it CHARGES: bright motes fly in from a ring and are drawn into the muzzle, a ring closes on it, the core swells (progress 0 .. 1)
void DrawChargeUp(float x, float y, float radius, float progress, float time, const Palette& palette, int seed = 0);

// the band of fire that EMBER leaves (the zone: x0 .. x1 across, y +- halfY high, in the y units of the rest of the game)
struct FlameParams
{
    float x0 = 0.0f, x1 = 1.0f, y = 0.0f, halfY = 0.08f;
    int age = 0;        // steps since the beam left it
    int lifetime = 120; // steps in all
};
void DrawFlameBand(const FlameParams& band);
} // namespace BeamFx
