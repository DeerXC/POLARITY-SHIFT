#pragma once
#include "ui/UiKit.h"
#include <string>

// DemoKit: the small shapes that the looping demos are drawn from - the demos of the hangar (every module, tactic and synergy) and the animated cards of the
// HOW TO PLAY page. A demo is a DIAGRAM of what something does, not a recording: a ship, bullets, an enemy and the one thing that changes. Everything is flat
// shapes (no texture), and every line is clipped to the strip of the demo, so a ring that is larger than the strip is cut by its edges.
//
//   DemoKit::Begin(x, y, w, h, frame, loop)   draws the ground of the strip, sets the clip and returns the Scene (its clock t runs 0 .. loop)
//   the shapes (Line, Disc, Ring, Ship, Enemy, Bullet ...) take positions in virtual 1280 x 720 px
//
// The text in a demo is only numbers and signs ("x2", "+40%"), so a demo never needs a translation.
namespace DemoKit
{
using UiKit::Color;
using UiKit::Rgba;

inline const Color RED = Rgba(1.00f, 0.30f, 0.24f, 1.0f);
inline const Color BLUE = Rgba(0.22f, 0.66f, 1.00f, 1.0f);
inline const Color WHITE = Rgba(0.97f, 0.98f, 1.00f, 1.0f);
inline const Color GOLD = Rgba(1.00f, 0.80f, 0.30f, 1.0f);
inline const Color GREEN = Rgba(0.45f, 1.00f, 0.66f, 1.0f);
inline const Color FIRE = Rgba(1.00f, 0.55f, 0.18f, 1.0f);
inline const Color DIM = Rgba(0.62f, 0.70f, 0.85f, 1.0f);

struct Scene
{
    float x, y, w, h; // the strip on the screen
    float t;          // the clock, 0 .. loop
    float a;          // opacity of everything (the loop fades in and out at its seam)
    float cy() const { return y + h * 0.5f; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
};

float Clamp01(float v);
float Ease(float t);                           // fast start, soft landing
float Seg01(float t, float a, float b);        // 0 before a, 1 after b, linear between
float Pulse(float t, float a, float b);        // 0 outside a .. b, 1 in the middle, soft at both ends (a flash)

Color Fade(Color c, float a);                  // the colour with its alpha multiplied by a
Color Mix(Color a, Color b, float t);

// the ground of a strip: a dark panel with a hairline frame and a faint grid; sets the clip rectangle of every shape below. `loop` frames long; `fadeSeam` hides the jump of the loop.
Scene Begin(float x, float y, float w, float h, int frame, int loop, bool fadeSeam = true, bool grid = true);

// ---- shapes -------------------------------------------------------------------------------------------------------------------------------------------
void Line(float x0, float y0, float x1, float y1, float th, Color c);
void Disc(float cx, float cy, float r, Color c);
void Ring(float cx, float cy, float r, float th, Color c);
void DashedRing(float cx, float cy, float r, float th, Color c, int dashes = 12);
void Dia(float cx, float cy, float r, Color c);                       // a diamond
void DiaOutline(float cx, float cy, float r, float th, Color c);
void Box(float x, float y, float w, float h, Color c);                // a filled rectangle (clipped)
void Frame(float x, float y, float w, float h, float th, Color c);    // a rectangle outline
void Bolt(float x0, float y0, float x1, float y1, int seed, float th, Color c);
void Brackets(float cx, float cy, float r, float th, Color c);
void Hex(float cx, float cy, float r, float th, Color c);             // a hexagon outline (a shield)
void Arc(float cx, float cy, float r, float th, float from, float to, Color c); // part of a circle; angles in turns, 0 = up, clockwise
void Cross(float cx, float cy, float r, float th, Color c);           // an X

// the ship: a chevron pointing right; `tint` is the colour of its core (its polarity)
void Ship(float cx, float cy, Color tint, float a, float scale = 1.0f);
// an enemy: a dark hexagon with a coloured rim; flash 0..1 lights it up
void Enemy(float cx, float cy, Color rim, float flash, float a, float scale = 1.0f);
// a bullet: a diamond in its colour (white ones get a ring)
void Bullet(float cx, float cy, Color c, bool white, float scale, float a);
// a short shot of the ship: a thin bar pointing right
void Shot(float cx, float cy, Color c, float a);

// ---- gauges ---------------------------------------------------------------------------------------------------------------------------------------------
void Bar(float x, float y, float w, float h, float fill, Color c, float a);                   // a bar with its ground; fill 0..1
void Pips(float x, float cy, int count, float lit, float gap, float r, Color c, float a);       // a row of diamonds; `lit` of them are on (fractions grow the next one)
void Beam(float x0, float cy, float len, float half, Color core, Color glow, float a);         // the white beam: a core and a glow, `half` = half its height
void Num(const std::string& text, float x, float y, float px, Color c, float a, bool centre = true);

// ---- motion ---------------------------------------------------------------------------------------------------------------------------------------------
// a bullet that flies from the right edge of the strip towards the ship at (shipX, y) and, when `absorbed`, is pulled into the ship at hitX and becomes energy there; otherwise
// it flies on. t0 = the frame it appears. Returns true on the frame in which it is absorbed (the demos light their counters with it).
struct Flight
{
    float t0;
    float y;       // the lane (absolute px)
    Color color;
    bool white;
    bool absorbed;
    float hitX;    // where it is taken
    float startX = 0.0f; // where it appears (0 = the right edge of the strip)
};
bool Fly(const Scene& s, const Flight& f, float shipX, float shipY, float speed = 2.4f);
float FlightHitTime(const Scene& s, const Flight& f, float speed = 2.4f); // the frame at which the bullet reaches hitX

// an energy diamond that flies from (x0, y0) to (x1, y1) between the frames t0 and t1
void EnergyMote(const Scene& s, float t0, float t1, float x0, float y0, float x1, float y1, Color c = GREEN);
}
