#include "ui/GameMenuUi.h"
#include "core/GameAudio.h"
#include "core/GameInput.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "ui/SceneFx.h"
#include "ui/UiKit.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

using UiAtlas::Id;
using UiKit::Color;
using UiKit::Rgba;

namespace
{
constexpr float PI = 3.1415926535f;
constexpr float SCREEN_W = 1280.0f;
constexpr float SCREEN_H = 720.0f;

float ToX(float normalized) { return (normalized + 1.0f) * UiKit::VIRTUAL_W * 0.5f; }
float ToY(float normalized) { return (1.0f - normalized) * UiKit::VIRTUAL_H * 0.5f; }
float ToW(float normalized) { return normalized * UiKit::VIRTUAL_W * 0.5f; }
float ToH(float normalized) { return normalized * UiKit::VIRTUAL_H * 0.5f; }

Color WithAlpha(Color c, float a)
{
    c.a *= a;
    return c;
}

float Hash(int seed)
{
    unsigned int x = static_cast<unsigned int>(seed) * 374761393u + 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u;
    x ^= x >> 16;
    return static_cast<float>(x & 0xffffu) / 65535.0f;
}

// ---- time ------------------------------------------------------------------------------------------------------------
using Clock = std::chrono::steady_clock;
Clock::time_point g_LastFrameTime{};
bool g_HasLastFrame = false;
float g_FixedStep = 0.0f;
float g_Dt = 1.0f;     // length of the last rendered frame in 60 Hz frames
long long g_Frame = 0; // rendered frames
float g_EntranceTime = 1.0e5f; // frames since the current screen started to build up

// ---- row animation -------------------------------------------------------------------------------------------------
struct RowAnim
{
    long long key;
    float t;
    long long lastFrame;
};

std::vector<RowAnim> g_Rows;

long long RowKey(float left, float top)
{
    return static_cast<long long>(std::lround(top * 2000.0f)) * 100003LL + static_cast<long long>(std::lround(left * 2000.0f));
}

RowAnim* FindRow(long long key)
{
    for (RowAnim& row : g_Rows)
    {
        if (row.key == key) return &row;
    }
    return nullptr;
}

RowAnim& TouchRow(long long key)
{
    RowAnim* row = FindRow(key);
    if (row == nullptr)
    {
        if (g_Rows.size() > 96) g_Rows.erase(g_Rows.begin());
        g_Rows.push_back(RowAnim{key, 0.0f, g_Frame});
        row = &g_Rows.back();
    }
    if (g_Frame - row->lastFrame > 2) row->t = 0.0f;
    row->lastFrame = g_Frame;
    return *row;
}

bool g_ConfirmConsumed = false;
float g_RowExitDx = 0.0f; // rows that are leaving the screen (SetRowExit): moved to the left by this many px ...
float g_RowExitA = 1.0f;  // ... and faded by this factor

// ---- backdrop state --------------------------------------------------------------------------------------------------
float g_BackdropAmount = 0.0f; // 0 = blue wins, 1 = red wins
// ---- the cut of the title turns with the polarity of the ship (see WithTitleTilt, UpdateBackdrop) --------------------------------------------
float g_BackdropSpring = 0.0f;   // 0 = blue ship, 1 = red ship, but driven by a spring: it overshoots and swings back
float g_BackdropVelocity = 0.0f;
float g_TitleTilt = 0.0f;        // how much of the turn is applied now (it fades away when the title is left)
constexpr float TITLE_PIVOT_X = 1000.0f; // the ship of the title (TitleScene.cpp: SHIP_X / SHIP_Y): the cut turns around this point
constexpr float TITLE_PIVOT_Y = 330.0f;
constexpr float TITLE_TILT = 0.06f;      // radians each side of the neutral angle (3.5 degrees): a lean, not a swing
constexpr float SPRING_K = 0.10f;        // critically damped (C = 2 sqrt K): it arrives fast and does not overshoot - no toy bounce
constexpr float SPRING_C = 0.632f;
// How far a colour switch pushes the red / blue cut (px along its normal). The title pushes it much further: the shape of the ship's colour is what you see
constexpr float CUT_SHIFT_PX = 70.0f;
constexpr float TITLE_CUT_SHIFT_PX = 110.0f;
float g_CutShiftPx = CUT_SHIFT_PX;
bool g_BackdropRed = false;
int g_BackdropFlash = 100; // update steps since the last switch
int g_BackdropClock = 0;
GameMenuUi::Stage g_Stage = GameMenuUi::Stage::Title;

constexpr float TILT_CUT = 0.60f; // angle of the red / blue cut (34 degrees): the one angle of the whole game

const Color RED_FIELD = Rgba(0.30f, 0.035f, 0.050f, 0.92f);
const Color BLUE_FIELD = Rgba(0.022f, 0.095f, 0.270f, 0.92f);
const Color BLACK_FIELD = Rgba(0.004f, 0.006f, 0.012f, 0.90f);

void HairRect(float x, float y, float w, float h, float t, Color c)
{
    UiKit::Fill(x, y, w, t, c);
    UiKit::Fill(x, y + h - t, w, t, c);
    UiKit::Fill(x, y + t, t, h - 2.0f * t, c);
    UiKit::Fill(x + w - t, y + t, t, h - 2.0f * t, c);
}

void Surface(float x, float y, float w, float h, float, Color fill, Color line)
{
    if (fill.a > 0.0f) UiKit::Fill(x, y, w, h, fill);
    if (line.a > 0.0f) HairRect(x, y, w, h, 1.5f, line);
}

// ---- geometry --------------------------------------------------------------------------------------------------------
struct Pt
{
    float x, y;
};
using Poly = std::vector<Pt>;
struct Plane // keeps the points with a * x + b * y + c >= 0
{
    float a, b, c;
};

Poly ScreenPoly()
{
    return Poly{{0.0f, 0.0f}, {SCREEN_W, 0.0f}, {SCREEN_W, SCREEN_H}, {0.0f, SCREEN_H}};
}

Poly ClipPoly(const Poly& in, const Plane& plane)
{
    Poly out;
    const size_t n = in.size();
    for (size_t i = 0; i < n; ++i)
    {
        const Pt& p = in[i];
        const Pt& q = in[(i + 1) % n];
        const float dp = plane.a * p.x + plane.b * p.y + plane.c;
        const float dq = plane.a * q.x + plane.b * q.y + plane.c;
        if (dp >= 0.0f) out.push_back(p);
        if ((dp >= 0.0f) != (dq >= 0.0f))
        {
            const float t = dp / (dp - dq);
            out.push_back(Pt{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t});
        }
    }
    return out;
}

// part of the line p0 + t * d that lies inside every plane; false when nothing is left
bool ClipLine(Pt p0, Pt d, const Plane* planes, int count, float& t0, float& t1)
{
    t0 = -4000.0f;
    t1 = 4000.0f;
    for (int i = 0; i < count; ++i)
    {
        const float denom = planes[i].a * d.x + planes[i].b * d.y;
        const float num = planes[i].a * p0.x + planes[i].b * p0.y + planes[i].c;
        if (std::fabs(denom) < 1.0e-6f)
        {
            if (num < 0.0f) return false;
            continue;
        }
        const float t = -num / denom;
        if (denom > 0.0f)
            t0 = (std::max)(t0, t);
        else
            t1 = (std::min)(t1, t);
    }
    return t0 < t1;
}

template <class F> void FillPoly(const Poly& poly, F colorAt)
{
    if (poly.size() < 3) return;
    std::vector<Renderer2DVertex> vertices;
    vertices.reserve(poly.size());
    for (const Pt& p : poly)
    {
        const Color c = colorAt(p.x, p.y);
        vertices.push_back(Renderer2DVertex{UiKit::NX(p.x), UiKit::NY(p.y), c.r, c.g, c.b, c.a});
    }
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    Renderer2D_DrawPolygon(vertices.data(), static_cast<int>(vertices.size()));
    Renderer2D_SetBlendMode(previous);
}

void DrawGrid(float alpha); // (below)

// ---- layouts ------------------------------------------------------------------------------------------------------------
// The backdrop of every screen is two convex shapes, RED and BLUE. A shape is the intersection of three half-planes, and a half-plane is
// two numbers: the kept side is  n . (p - c) >= d  with n = (cos theta, sin theta) and c = the centre of the screen. A change of screen
// moves the numbers from one layout to the next: the edges slide and turn, the shapes grow and shrink, and because only angles and
// offsets are mixed, a shape is convex at every moment of the change (no flicker, no empty frame).
//   plane 0 = the steep edge of a panel (78 degrees), plane 1 = the cut (34 degrees), plane 2 = spare
//   d = -850 means "not used" (the edge is off the screen), the shape ignores that plane.
struct HalfPlane
{
    float theta;
    float d;
    float line;   // 0..1: an ivory line with coloured accents is drawn along this edge
    float follow; // +1 / -1: the edge moves with the polarity of the ship (the cut of the title and of the pages)
};

struct Layout
{
    HalfPlane shape[2][3]; // 0 = red, 1 = blue
    float field;           // alpha of the coloured fields
    float line;            // alpha of the lines
    float base;            // alpha of the dark plate under the fields (1 = the stars do not shine through)
    float hatch;           // alpha of the fine lines along the cut
    float bright;          // colour multiplier (the quiet field of the game is a faint tint of a bright colour)
    float ground;          // alpha of a black ground with a blueprint grid (hangar)
    float tint = 0.0f;     // 0 = red / blue fields, 1 = black fields (module pick)
    float flat = 0.0f;     // 0 = the dark fields with their gradient, 1 = flat saturated paint, no gradient (the key art of the title)
};

enum LayoutId
{
    L_TITLE,
    L_HOWTO,
    L_RECORDS,
    L_SETTINGS,
    L_GAMEOVER,
    L_GAMECLEAR,
    L_PLAY,
    L_HANGAR,
    L_NONE,  // nothing: an overlay (pause, pick) builds itself up from here
    L_PAUSE, // a red and a blue corner over the game
    L_PICK,  // two black corners over the dimmed game
    L_COUNT
};

// How to read a row: {theta, d, line, follow}. theta (radians) says which way the edge faces and which side is kept:
//    0.2108 = the steep panel edge, panel to its right   3.3524 = the same edge, panel to its left
//   -2.1708 = the cut, the red side is kept              0.9708 = the cut, the blue side is kept
// d is how far the edge is from the centre of the screen (px, along the way it faces), -850 = not used. To move the cut of a screen
// by 100 px towards the red corner, add 100 to d of both cut planes of that layout (red: d + 100, blue: d - 100).
// The idea of each layout:
//   Title      panel on the right, split by the cut
//   HowTo      the panel grows to the left and the cut climbs: blue takes over, red keeps a wedge at the top right
//   Records    the panel flips to the left side and grows: red takes over, blue keeps a wedge at the bottom left
//   Settings   no panel: a red and a blue corner, the middle belongs to the page
//   GameOver / GameClear  the whole screen, the cut sits low (red wins) / high (blue wins)
//   Play       the same split as a faint tint behind the game
//   Hangar     black ground with a grid and two corners
const Layout LAYOUTS[L_COUNT] = {
    // Title
    {{{{0.2108f, 78.2292f, 1.0000f, 0.0000f}, {-2.1708f, -248.0101f, 1.0000f, 1.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{0.2108f, 78.2292f, 1.0000f, 0.0000f}, {0.9708f, 248.0101f, 0.0000f, -1.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     1.0000f, 1.0000f, 1.0000f, 0.0500f, 1.0000f, 0.0000f, 0.0000f, 1.0000f},
    // HowTo
    {{{{0.2108f, -381.0000f, 1.0000f, 0.0000f}, {-2.1708f, 400.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{0.2108f, -381.0000f, 1.0000f, 0.0000f}, {0.9708f, -400.0000f, 0.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     0.9200f, 1.0000f, 1.0000f, 0.0450f, 1.0000f, 0.0000f},
    // Records
    {{{{3.3524f, -340.0000f, 1.0000f, 0.0000f}, {-2.1708f, -330.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{3.3524f, -340.0000f, 1.0000f, 0.0000f}, {0.9708f, 330.0000f, 0.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     0.9200f, 1.0000f, 1.0000f, 0.0450f, 1.0000f, 0.0000f},
    // Settings
    {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, 420.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, 420.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     0.9500f, 1.0000f, 1.0000f, 0.0400f, 1.0000f, 0.0000f},
    // GameOver
    {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, -212.8339f, 1.0000f, 1.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, 212.8339f, 0.0000f, -1.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     0.3600f, 0.5000f, 0.0000f, 0.0350f, 1.1500f, 0.0000f},
    // GameClear
    {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, 173.7415f, 1.0000f, 1.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, -173.7415f, 0.0000f, -1.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     0.3600f, 0.5000f, 0.0000f, 0.0350f, 1.1500f, 0.0000f},
    // Play
    {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, -203.2713f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, 203.2713f, 0.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     0.0480f, 0.0850f, 0.0000f, 0.0000f, 2.6000f, 0.0000f, 0.0000f, 1.0000f},
    // Hangar
    {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, 560.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
      {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, 600.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
     1.0000f, 1.0000f, 0.0000f, 0.0000f, 1.0000f, 0.9700f},
      // None (the starting point of the overlays: both shapes are off the screen)
      {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, 900.0000f, 0.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
        {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, 900.0000f, 0.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
       0.4600f, 0.5500f, 0.0000f, 0.0000f, 1.0000f, 0.0000f},
      // Pause
      {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, 430.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
        {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, 480.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
       0.4600f, 0.5500f, 0.0000f, 0.0000f, 1.0000f, 0.0000f},
      // Pick
      {{{{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {-2.1708f, 380.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}},
        {{0.2108f, -850.0000f, 0.0000f, 0.0000f}, {0.9708f, 420.0000f, 1.0000f, 0.0000f}, {-2.1708f, -850.0000f, 0.0000f, 0.0000f}}},
       0.9000f, 0.8500f, 0.0000f, 0.0000f, 1.0000f, 0.0000f, 1.0000f}
};

// The field behind the game: flat colour, as quiet as the player wants it (settings: BACKGROUND). Everything else of a layout is a constant.
float g_PlayFieldAlpha = 0.048f; // (40 %, the default of the setting)
Layout TargetLayout(int id)
{
    Layout l = LAYOUTS[id];
    if (id == L_PLAY) l.field = g_PlayFieldAlpha;
    return l;
}

constexpr float MORPH_DURATION = 22.0f; // frames one plane needs to reach its place
constexpr float MORPH_DELAY[2][3] = {{0.0f, 2.0f, 4.0f}, {1.0f, 3.0f, 5.0f}}; // the planes do not start together: the change is not rigid
constexpr float MORPH_DONE = MORPH_DURATION + 8.0f;
constexpr float FIELD_REACH = 380.0f; // the colour of a field fades over this distance from its cut
constexpr float CX = 640.0f;
constexpr float CY = 360.0f;

float Clamp01Local(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float SmoothLocal(float t)
{
    t = Clamp01Local(t);
    return t * t * (3.0f - 2.0f * t);
}

Layout MixLayout(const Layout& a, const Layout& b, float time)
{
    Layout out = b;
    for (int sh = 0; sh < 2; ++sh)
    {
        for (int k = 0; k < 3; ++k)
        {
            const float x = (time - MORPH_DELAY[sh][k]) / MORPH_DURATION;
            const float p = 1.0f - (1.0f - Clamp01Local(x)) * (1.0f - Clamp01Local(x)) * (1.0f - Clamp01Local(x)); // (ease out: no overshoot)
            const float q = SmoothLocal(x);
            const HalfPlane& from = a.shape[sh][k];
            const HalfPlane& to = b.shape[sh][k];
            out.shape[sh][k].theta = from.theta + (to.theta - from.theta) * p;
            out.shape[sh][k].d = from.d + (to.d - from.d) * p;
            out.shape[sh][k].line = from.line + (to.line - from.line) * q;
            out.shape[sh][k].follow = from.follow + (to.follow - from.follow) * q;
        }
    }
    const float q = SmoothLocal(time / (MORPH_DURATION + 6.0f));
    out.field = a.field + (b.field - a.field) * q;
    out.line = a.line + (b.line - a.line) * q;
    out.base = a.base + (b.base - a.base) * q;
    out.hatch = a.hatch + (b.hatch - a.hatch) * q;
    out.bright = a.bright + (b.bright - a.bright) * q;
    out.ground = a.ground + (b.ground - a.ground) * q;
    out.tint = a.tint + (b.tint - a.tint) * q;
    out.flat = a.flat + (b.flat - a.flat) * q;
    return out;
}

// The state of one morphing backdrop. There are two: the screens (title, pages, results, hangar, the game) and the overlays that are drawn on
// top of a screen (pause, module pick), so an overlay can build itself up while the screen below it keeps its own layout.
struct Morph
{
    Layout cur;           // what is on the screen now
    Layout from;          // where the running change started
    int toId = -1;
    float time = 1.0e5f;
    bool init = false;
    long long frame = -1;
    long long lastDraw = -1000;
};

Morph g_World;
Morph g_Overlay;

// Called when a screen draws its backdrop. A new layout starts a change from whatever is on the screen right now, so a change that is
// interrupted by the next one never jumps. An overlay that was not drawn in the last frames starts from `openFrom`.
void StepMorph(Morph& m, int id, int openFrom = -1)
{
    if (openFrom >= 0 && g_Frame - m.lastDraw > 2)
    {
        m.cur = m.from = TargetLayout(openFrom);
        m.toId = -1;
        m.init = true;
    }
    m.lastDraw = g_Frame;
    if (!m.init)
    {
        m.cur = m.from = TargetLayout(id);
        m.toId = id;
        m.time = 1.0e5f;
        m.init = true;
        m.frame = g_Frame;
        return;
    }
    if (id != m.toId)
    {
        m.from = m.cur;
        m.toId = id;
        m.time = 0.0f;
        m.frame = g_Frame;
        return; // the first frame of a change is still the old picture
    }
    if (m.frame == g_Frame) return; // once per rendered frame
    m.frame = g_Frame;
    if (m.time >= MORPH_DONE) return;
    m.time += g_Dt;
    m.cur = m.time >= MORPH_DONE ? TargetLayout(id) : MixLayout(m.from, TargetLayout(id), m.time);
}

void DrawClipped(Pt p0, Pt d, const Plane* planes, int count, float thickness, Color color)
{
    float t0, t1;
    if (color.a <= 0.002f || !ClipLine(p0, d, planes, count, t0, t1)) return;
    GameMenuUi::Segment(p0.x + d.x * t0, p0.y + d.y * t0, p0.x + d.x * t1, p0.y + d.y * t1, thickness, color);
}

struct BuiltShape
{
    Plane p[3];
    Poly poly;
};

Plane g_LastCut{0.0f, 0.0f, 0.0f}; // the red plane of the cut, as the last composition was drawn

void DrawComposition(const Layout& source, float fade, bool effects)
{
    Layout L = source;
    L.field *= fade;
    L.line *= fade;
    L.base *= fade;
    L.hatch *= fade;
    L.ground *= fade;
    const Plane screenPlanes[4] = {{1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, SCREEN_W}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, SCREEN_H}};

    if (L.ground > 0.003f)
    {
        UiKit::Fill(0.0f, 0.0f, SCREEN_W, SCREEN_H, Rgba(0.006f, 0.008f, 0.014f, L.ground));
        DrawGrid(L.ground / 0.97f);
    }

    // The cut follows the polarity of the ship, and the colour of the ship WINS: a red ship pushes the cut towards the blue corner (the red shape grows), a
    // blue ship the other way.
    const float s = g_BackdropAmount * g_BackdropAmount * (3.0f - 2.0f * g_BackdropAmount);
    const float shift = -(s - 0.5f) * 2.0f * g_CutShiftPx;

    BuiltShape built[2];
    for (int sh = 0; sh < 2; ++sh)
    {
        BuiltShape& b = built[sh];
        b.poly = ScreenPoly();
        for (int k = 0; k < 3; ++k)
        {
            const HalfPlane& h = L.shape[sh][k];
            const float ca = std::cos(h.theta);
            const float sa = std::sin(h.theta);
            const float d = h.d + h.follow * shift;
            b.p[k] = Plane{ca, sa, -(ca * CX + sa * CY) - d};
            if (b.poly.size() >= 3) b.poly = ClipPoly(b.poly, b.p[k]);
        }
        if (b.poly.size() < 3) b.poly.clear();
    }

    g_LastCut = built[0].p[1]; // (for CutSide: the red plane of the cut as it is drawn now)

    // 1. plate and fields (brighter towards the cut: a gradient that is exact, because the polygon is split where it ends)
    for (int sh = 0; sh < 2; ++sh)
    {
        const BuiltShape& b = built[sh];
        if (b.poly.empty()) continue;
        const bool blue = sh == 1;
        if (L.base > 0.01f)
        {
            const float a = L.base;
            FillPoly(b.poly, [a](float, float) { return Rgba(0.024f, 0.028f, 0.042f, a); });
        }
        const Plane ref = b.p[1];
        const float br = L.bright;
        const float fa = L.field;
        const float tintK = L.tint;
        const float flatK = L.flat;
        auto colour = [=](float x, float y)
        {
            const float dist = ref.a * x + ref.b * y + ref.c;
            const float t = (std::max)(0.0f, 1.0f - dist / FIELD_REACH);
            Color c = blue ? Rgba((std::min)(1.0f, (0.014f + 0.020f * t) * br), (std::min)(1.0f, (0.040f + 0.070f * t) * br),
                                  (std::min)(1.0f, (0.115f + 0.170f * t) * br), fa)
                           : Rgba((std::min)(1.0f, (0.10f + 0.20f * t) * br), (std::min)(1.0f, (0.018f + 0.030f * t) * br),
                                  (std::min)(1.0f, (0.030f + 0.040f * t) * br), fa);
            if (flatK > 0.001f) // the key art: one flat saturated colour per field (the covers of Persona 3 Reload: #E8141C and #10B0F0), no gradient
            {
                const float fr = blue ? 0.063f : 0.909f, fg = blue ? 0.690f : 0.078f, fb = blue ? 0.941f : 0.110f;
                c.r += (fr - c.r) * flatK;
                c.g += (fg - c.g) * flatK;
                c.b += (fb - c.b) * flatK;
            }
            if (tintK > 0.001f) // black fields: the colour of the shape fades to ink
            {
                c.r += (0.004f - c.r) * tintK;
                c.g += (0.006f - c.g) * tintK;
                c.b += (0.012f - c.b) * tintK;
            }
            return c;
        };
        FillPoly(ClipPoly(b.poly, Plane{-ref.a, -ref.b, -ref.c + FIELD_REACH}), colour);
        FillPoly(ClipPoly(b.poly, Plane{ref.a, ref.b, ref.c - FIELD_REACH}), colour);
    }

    // 2. the surfaces of THE SEAM: the red shape is printed (P5 halftone dots, big at the cut, fading away from it), the blue one is glass
    //    (P3: a fine grid along and across the cut, and one glass slab parallel to it). Black fields (tint) have neither.
    if (L.tint < 0.5f)
    {
        const float surfaceA = (std::max)(L.hatch, 0.35f * L.line) * (1.0f - L.tint * 2.0f);
        for (int sh = 0; sh < 2; ++sh)
        {
            const BuiltShape& b = built[sh];
            if (b.poly.empty() || surfaceA <= 0.003f) continue;
            const Plane ref = b.p[1]; // the cut: positive inside the shape
            const Pt nrmIn{ref.a, ref.b};
            const Pt along{-ref.b, ref.a};
            const float onCut = ref.a * CX + ref.b * CY + ref.c;
            const Pt origin{CX - ref.a * onCut, CY - ref.b * onCut}; // (a point on the cut)
            auto inside = [&](float x, float y)
            {
                if (x < -10.0f || x > SCREEN_W + 10.0f || y < -10.0f || y > SCREEN_H + 10.0f) return false;
                for (int k = 0; k < 3; ++k)
                    if (b.p[k].a * x + b.p[k].b * y + b.p[k].c <= 2.0f) return false;
                return true;
            };
            // the GPU draws the surface per pixel (Renderer2D_DrawPolygonPattern, shaders/shader_pattern_2d.hlsl): the outline of the shape is
            // the only geometry. Without the pattern shader the same picture is built from polygons below.
            if (b.poly.size() <= 32)
            {
                Renderer2DVertex outline[32];
                const float patternA = (std::min)(1.0f, surfaceA * (sh == 1 ? 1.0f + 1.5f * L.flat : 1.0f)); // (the glass lines need more on the bright blue)
                for (size_t k = 0; k < b.poly.size(); ++k)
                    outline[k] = Renderer2DVertex{UiKit::NX(b.poly[k].x), UiKit::NY(b.poly[k].y), 1.0f, 1.0f, 1.0f, patternA};
                Renderer2DPattern pattern;
                pattern.kind = sh == 0 ? Renderer2DPatternKind::Halftone : Renderer2DPatternKind::Glass;
                pattern.originX = origin.x;
                pattern.originY = origin.y;
                pattern.alongX = along.x;
                pattern.alongY = along.y;
                pattern.normalX = nrmIn.x;
                pattern.normalY = nrmIn.y;
                if (sh == 0)
                {
                    // the print on the flat red: dark dots (the halftone of the covers); on the dark field the dots are light
                    pattern.r = 1.0f + (0.30f - 1.0f) * L.flat, pattern.g = 0.30f + (0.0f - 0.30f) * L.flat, pattern.b = 0.24f + (0.03f - 0.24f) * L.flat;
                    pattern.a = 0.45f * (1.0f - 0.15f * L.flat);
                }
                else
                {
                    pattern.r = 0.55f + 0.40f * L.flat, pattern.g = 0.80f + 0.18f * L.flat, pattern.b = 1.0f, pattern.a = 1.0f;
                    const Color ivory = GameMenuUi::Ivory(1.0f);
                    pattern.r2 = ivory.r, pattern.g2 = ivory.g, pattern.b2 = ivory.b;
                }
                if (Renderer2D_DrawPolygonPattern(outline, static_cast<int>(b.poly.size()), pattern)) continue;
            }
            if (sh == 0)
            {
                constexpr float STEP = 17.0f, REACH = 300.0f;
                const Color dot = Rgba(1.0f, 0.30f, 0.24f, 0.45f * surfaceA);
                for (int r = 0; static_cast<float>(r) * STEP < REACH; ++r)
                {
                    const float out = 10.0f + static_cast<float>(r) * STEP;
                    const float k = 1.0f - out / REACH;
                    const float rad = 4.4f * k * k;
                    if (rad < 0.7f) break;
                    const float shiftAlong = (r & 1) ? STEP * 0.5f : 0.0f;
                    for (int c = -60; c <= 60; ++c)
                    {
                        const float t = static_cast<float>(c) * STEP + shiftAlong;
                        const float x = origin.x + along.x * t + nrmIn.x * out;
                        const float y = origin.y + along.y * t + nrmIn.y * out;
                        if (!inside(x, y)) continue;
                        GameMenuUi::DiamondFill(x, y, rad, dot);
                    }
                }
            }
            else
            {
                const Plane set[7] = {b.p[0], b.p[1], b.p[2], screenPlanes[0], screenPlanes[1], screenPlanes[2], screenPlanes[3]};
                for (int i = 1; i <= 26; ++i) // lines along the cut
                {
                    const float out = static_cast<float>(i) * 26.0f;
                    DrawClipped(Pt{origin.x + nrmIn.x * out, origin.y + nrmIn.y * out}, along, set, 7, 1.0f,
                                Rgba(0.55f, 0.80f, 1.0f, (i % 4 == 0 ? 0.10f : 0.05f) * surfaceA));
                }
                for (int i = -40; i <= 40; ++i) // and across it, wider apart
                {
                    const float t = static_cast<float>(i) * 52.0f;
                    DrawClipped(Pt{origin.x + along.x * t, origin.y + along.y * t}, nrmIn, set, 7, 1.0f, Rgba(0.55f, 0.80f, 1.0f, 0.035f * surfaceA));
                }
                // the glass slab: a pale band parallel to the cut, with hairline edges
                const float s0 = 120.0f, s1 = 196.0f;
                const Plane a0{ref.a, ref.b, ref.c - s0};
                const Plane a1{-ref.a, -ref.b, -ref.c + s1};
                FillPoly(ClipPoly(ClipPoly(b.poly, a0), a1), [surfaceA](float, float) { return Rgba(0.60f, 0.85f, 1.0f, 0.045f * surfaceA); });
                DrawClipped(Pt{origin.x + nrmIn.x * s0, origin.y + nrmIn.y * s0}, along, set, 7, 1.0f, GameMenuUi::Ivory(0.22f * surfaceA));
                DrawClipped(Pt{origin.x + nrmIn.x * s1, origin.y + nrmIn.y * s1}, along, set, 7, 1.0f, Rgba(0.55f, 0.80f, 1.0f, 0.30f * surfaceA));
            }
        }
    }

    // 3. the edges: ivory line, coloured accents. Plane 0 is a panel edge (red outside, a ghost line inside), plane 1 and 2 are cuts
    //    (the colour of the shape on its own side, the other colour on the far side)
    for (int sh = 0; sh < 2; ++sh)
    {
        const BuiltShape& b = built[sh];
        if (b.poly.empty()) continue;
        for (int k = 0; k < 3; ++k)
        {
            const HalfPlane& h = L.shape[sh][k];
            const float w = h.line * L.line;
            if (w < 0.02f) continue;
            Plane set[6];
            int n = 0;
            for (int j = 0; j < 3; ++j)
                if (j != k) set[n++] = b.p[j];
            for (int j = 0; j < 4; ++j)
                set[n++] = screenPlanes[j];
            const float ca = b.p[k].a;
            const float sa = b.p[k].b;
            const float d = h.d + h.follow * shift;
            const Pt p0{CX + ca * d, CY + sa * d};
            const Pt dir{-sa, ca};
            auto at = [&](float o) { return Pt{p0.x + ca * o, p0.y + sa * o}; };
            if (k == 0)
            {
                DrawClipped(p0, dir, set, n, 3.0f, GameMenuUi::Ivory(w));
                DrawClipped(at(-7.0f), dir, set, n, 2.0f, GameMenuUi::Signal(w));
                DrawClipped(at(16.0f), dir, set, n, 1.0f, GameMenuUi::Ivory(0.22f * w));
            }
            else
            {
                const Color red = Rgba(1.0f, 0.28f, 0.22f, 0.90f * w);
                const Color blue = Rgba(0.25f, 0.62f, 1.0f, 0.90f * w);
                DrawClipped(p0, dir, set, n, 4.0f, GameMenuUi::Ivory(0.95f * w));
                DrawClipped(at(8.0f), dir, set, n, 2.0f, sh == 1 ? blue : red);
                DrawClipped(at(-8.0f), dir, set, n, 2.0f, sh == 1 ? red : blue);
            }
        }
    }

    // 4. glass shards drift along the cut of the red shape, and a burst when the polarity switches (only on the coloured shapes)
    if (!effects || L.line < 0.2f) return;
    const HalfPlane& rc = L.shape[0][1];
    const float rca = std::cos(rc.theta);
    const float rsa = std::sin(rc.theta);
    const float rd = rc.d + rc.follow * shift;
    const Pt cutP{CX + rca * rd, CY + rsa * rd};
    const Pt dirv{-rsa, rca};     // along the cut
    const Pt nrm{rca, rsa};       // towards the red side
    auto onShape = [&](float x, float y)
    {
        for (int sh = 0; sh < 2; ++sh)
        {
            if (built[sh].poly.empty()) continue;
            bool inside = true;
            for (int k = 0; k < 3; ++k)
                if (built[sh].p[k].a * x + built[sh].p[k].b * y + built[sh].p[k].c <= 4.0f) inside = false;
            if (inside) return true;
        }
        return false;
    };
    const float clock = static_cast<float>(g_BackdropClock);
    // 4a. glass: a wide soft sheen sweeps along each coloured shape every few seconds (a gradient band, exact: split at its middle)
    {
        const float pos = std::fmod(clock * 3.2f, 3400.0f) - 1500.0f; // (most of the time it is off the screen: a pause between sweeps)
        const float base = -(dirv.x * cutP.x + dirv.y * cutP.y);
        constexpr float HALF = 150.0f;
        for (int sh = 0; sh < 2; ++sh)
        {
            const BuiltShape& b = built[sh];
            if (b.poly.empty()) continue;
            const float peak = 0.075f * L.line;
            const Plane lo{dirv.x, dirv.y, base - (pos - HALF)};  // along >= pos - HALF
            const Plane mid{-dirv.x, -dirv.y, -(base - pos)};    // along <= pos
            const Plane mid2{dirv.x, dirv.y, base - pos};         // along >= pos
            const Plane hi{-dirv.x, -dirv.y, -(base - (pos + HALF))};
            auto sheen = [&](float x, float y)
            {
                const float along = dirv.x * x + dirv.y * y + base;
                const float k = (std::max)(0.0f, 1.0f - std::fabs(along - pos) / HALF);
                return Rgba(1.0f, 1.0f, 1.0f, peak * k);
            };
            FillPoly(ClipPoly(ClipPoly(b.poly, lo), mid), sheen);
            FillPoly(ClipPoly(ClipPoly(b.poly, mid2), hi), sheen);
        }
    }
    // 4b. a switch: two waves leave the cut, one into each colour (lines parallel to it), the ivory cut itself flares
    if (g_BackdropFlash < 30)
    {
        const float age = static_cast<float>(g_BackdropFlash);
        const float life = 1.0f - age / 30.0f;
        for (int sh = 0; sh < 2; ++sh)
        {
            const BuiltShape& b = built[sh];
            if (b.poly.empty()) continue;
            const float side = sh == 0 ? 1.0f : -1.0f; // (nrm points into the red shape)
            const Plane set[7] = {b.p[0], b.p[1], b.p[2], screenPlanes[0], screenPlanes[1], screenPlanes[2], screenPlanes[3]};
            for (int w = 0; w < 2; ++w)
            {
                const float d = side * (10.0f + age * (22.0f - 8.0f * static_cast<float>(w)));
                const Pt o{cutP.x + nrm.x * d, cutP.y + nrm.y * d};
                const Color c = w == 0 ? (sh == 0 ? Rgba(1.0f, 0.36f, 0.30f, 0.65f * life * L.line) : Rgba(0.35f, 0.70f, 1.0f, 0.65f * life * L.line))
                                       : GameMenuUi::Ivory(0.35f * life * L.line);
                DrawClipped(o, dirv, set, 7, (w == 0 ? 2.0f : 1.0f), c);
            }
        }
    }
    // (4c. the seam diamonds that used to travel along the cut are gone: a small thing that was always moving, and meant nothing)
    // 4d. a switch is a blade: a hard white slash runs the length of the cut in 7 frames, a thin trail stays a moment behind it
    if (g_BackdropFlash < 22)
    {
        const BuiltShape& b = built[0];
        if (!b.poly.empty())
        {
            const float age = static_cast<float>(g_BackdropFlash);
            const float head = -1100.0f + 2200.0f * Clamp01Local(age / 7.0f);
            const float tail = -1100.0f + 2200.0f * Clamp01Local((age - 3.0f) / 9.0f);
            const float life = 1.0f - Clamp01Local((age - 8.0f) / 14.0f);
            const Plane set[6] = {b.p[0], b.p[2], screenPlanes[0], screenPlanes[1], screenPlanes[2], screenPlanes[3]};
            const Pt p0{cutP.x + dirv.x * tail, cutP.y + dirv.y * tail};
            const Pt d{dirv.x * (head - tail), dirv.y * (head - tail)};
            float t0, t1;
            if (head > tail && ClipLine(p0, d, set, 6, t0, t1))
            {
                GameMenuUi::Segment(p0.x + d.x * t0, p0.y + d.y * t0, p0.x + d.x * t1, p0.y + d.y * t1, 3.0f, Rgba(1.0f, 1.0f, 1.0f, 0.95f * life * L.line));
                // the edge of the blade: the new colour on its own side, hard
                const Color edge = g_BackdropRed ? Rgba(1.0f, 0.30f, 0.24f, 0.9f * life * L.line) : Rgba(0.30f, 0.66f, 1.0f, 0.9f * life * L.line);
                const float side = g_BackdropRed ? 6.0f : -6.0f;
                GameMenuUi::Segment(p0.x + d.x * t0 + nrm.x * side, p0.y + d.y * t0 + nrm.y * side, p0.x + d.x * t1 + nrm.x * side, p0.y + d.y * t1 + nrm.y * side,
                                    2.0f, edge);
            }
            // a few thin splinters thrown off the cut at the head of the blade (hard, short-lived)
            if (age < 10.0f)
            {
                const float k = 1.0f - age / 10.0f;
                for (int i = 0; i < 5; ++i)
                {
                    const float h1 = Hash(i * 5 + 201), h2 = Hash(i * 5 + 202);
                    const float at = head - h1 * 160.0f;
                    const float out = (i % 2 == 0 ? 1.0f : -1.0f) * (8.0f + age * (3.0f + 5.0f * h2));
                    const float px = cutP.x + dirv.x * at + nrm.x * out, py = cutP.y + dirv.y * at + nrm.y * out;
                    if (!onShape(px, py)) continue;
                    GameMenuUi::Segment(px - dirv.x * 9.0f, py - dirv.y * 9.0f, px + dirv.x * 9.0f, py + dirv.y * 9.0f, 1.5f, GameMenuUi::Ivory(0.8f * k * L.line));
                }
            }
        }
    }
}

void DrawGrid(float alpha)
{
    for (int i = 0; i <= 32; ++i)
    {
        const float x = static_cast<float>(i) * 40.0f;
        UiKit::Fill(x, 0.0f, 1.0f, 720.0f, GameMenuUi::Ivory((i % 4 == 0 ? 0.060f : 0.028f) * alpha));
    }
    for (int j = 0; j <= 18; ++j)
    {
        const float y = static_cast<float>(j) * 40.0f;
        UiKit::Fill(0.0f, y, 1280.0f, 1.0f, GameMenuUi::Ivory((j % 4 == 0 ? 0.060f : 0.028f) * alpha));
    }
}
}

// ---------------------------------------------------------------------------------------------------------------------
float GameMenuUi::EaseOut(float t)
{
    const float i = 1.0f - Clamp01(t);
    return 1.0f - i * i * i;
}

float GameMenuUi::Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

Color GameMenuUi::Ivory(float alpha)
{
    return Rgba(0.93f, 0.92f, 0.88f, alpha);
}

Color GameMenuUi::Signal(float alpha)
{
    return Rgba(0.88f, 0.17f, 0.15f, alpha);
}

Color GameMenuUi::Ink(float alpha)
{
    return Rgba(0.05f, 0.06f, 0.09f, alpha);
}

// ---- THE SEAM: the language of this game -----------------------------------------------------------------------------------------------
// P5R gives the red half (hard plates that slam in, heavy type, hard offset shadows), P3R the blue half (thin glass lines, smooth glides).
// Every piece of UI carries both, joined at the seam: the polarity of the moment is the plate / the offset, the other colour is the glass
// line. When the polarity changes (the backdrop turns), they trade places smoothly (g_BackdropAmount), like the red / blue cut behind.
namespace
{
const Color SEAM_RED{1.00f, 0.20f, 0.17f, 1.0f};
const Color SEAM_BLUE{0.16f, 0.62f, 1.00f, 1.0f};
Color Lerp(Color a, Color b, float t)
{
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
float Smooth01(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}
}

float GameMenuUi::CutSide(float x, float y)
{
    return g_LastCut.a * x + g_LastCut.b * y + g_LastCut.c;
}

Color GameMenuUi::Accent(float alpha)
{
    return WithAlpha(Lerp(SEAM_BLUE, SEAM_RED, Smooth01(g_BackdropAmount)), alpha);
}

Color GameMenuUi::Counter(float alpha)
{
    return WithAlpha(Lerp(SEAM_RED, SEAM_BLUE, Smooth01(g_BackdropAmount)), alpha);
}

// the seam diamond: the left half red, the right half blue, a white core (the cursor, the node of a rule)
void GameMenuUi::SeamDiamond(float cx, float cy, float r, float alpha)
{
    if (alpha <= 0.0f) return;
    const Color red = WithAlpha(SEAM_RED, alpha), blue = WithAlpha(SEAM_BLUE, alpha);
    const Renderer2DVertex left[3] = {{UiKit::NX(cx), UiKit::NY(cy - r), red.r, red.g, red.b, red.a},
                                      {UiKit::NX(cx - r), UiKit::NY(cy), red.r, red.g, red.b, red.a},
                                      {UiKit::NX(cx), UiKit::NY(cy + r), red.r, red.g, red.b, red.a}};
    const Renderer2DVertex right[3] = {{UiKit::NX(cx), UiKit::NY(cy - r), blue.r, blue.g, blue.b, blue.a},
                                       {UiKit::NX(cx), UiKit::NY(cy + r), blue.r, blue.g, blue.b, blue.a},
                                       {UiKit::NX(cx + r), UiKit::NY(cy), blue.r, blue.g, blue.b, blue.a}};
    Renderer2D_DrawPolygon(left, 3);
    Renderer2D_DrawPolygon(right, 3);
    DiamondFill(cx, cy, r * 0.38f, Ivory(alpha));
}

// the seam rule (the rule under the logo): red | diamond | blue; the side of the polarity is the bright one, it is drawn from the left
void GameMenuUi::RowMark(float cx, float cy, float lit, float alpha, bool danger)
{
    if (alpha <= 0.002f) return;
    const float k = EaseOut(Clamp01(lit));
    const float r = 3.2f + 3.8f * k;                 // 3.2 at rest, 7 when the row is selected
    const float a = alpha * (0.42f + 0.58f * k);     // dim at rest
    if (!danger)
    {
        SeamDiamond(cx, cy, r, a);
        return;
    }
    // danger: both halves red (the left one a little darker), the white core stays
    const Color hi = WithAlpha(Rgba(1.0f, 0.36f, 0.30f, 1.0f), a), lo = WithAlpha(Rgba(0.78f, 0.16f, 0.14f, 1.0f), a);
    const Renderer2DVertex left[3] = {{UiKit::NX(cx), UiKit::NY(cy - r), lo.r, lo.g, lo.b, lo.a},
                                      {UiKit::NX(cx - r), UiKit::NY(cy), lo.r, lo.g, lo.b, lo.a},
                                      {UiKit::NX(cx), UiKit::NY(cy + r), lo.r, lo.g, lo.b, lo.a}};
    const Renderer2DVertex right[3] = {{UiKit::NX(cx), UiKit::NY(cy - r), hi.r, hi.g, hi.b, hi.a},
                                       {UiKit::NX(cx), UiKit::NY(cy + r), hi.r, hi.g, hi.b, hi.a},
                                       {UiKit::NX(cx + r), UiKit::NY(cy), hi.r, hi.g, hi.b, hi.a}};
    Renderer2D_DrawPolygon(left, 3);
    Renderer2D_DrawPolygon(right, 3);
    DiamondFill(cx, cy, r * 0.38f, Ivory(a));
}

void GameMenuUi::SeamRule(float x, float y, float w, float progress, float alpha)
{
    if (alpha <= 0.0f || progress <= 0.0f) return;
    const float a = Smooth01(g_BackdropAmount);
    const float split = x + w * 0.5f;
    const float drawn = w * Clamp01(progress);
    UiKit::Fill(x, y, (std::min)(drawn, w * 0.5f), 2.0f, WithAlpha(SEAM_RED, (0.35f + 0.60f * a) * alpha));
    if (drawn > w * 0.5f) UiKit::Fill(split, y, drawn - w * 0.5f, 2.0f, WithAlpha(SEAM_BLUE, (0.95f - 0.60f * a) * alpha));
    if (drawn > w * 0.5f) SeamDiamond(split, y + 1.0f, 5.0f, alpha);
}

void GameMenuUi::BeginFrame()
{
    const Clock::time_point now = Clock::now();
    float dt = 1.0f;
    if (g_FixedStep > 0.0f)
    {
        dt = g_FixedStep;
    }
    else if (g_HasLastFrame)
    {
        dt = std::chrono::duration<float>(now - g_LastFrameTime).count() * 60.0f;
        dt = (std::max)(0.0f, (std::min)(dt, 4.0f));
    }
    g_LastFrameTime = now;
    g_HasLastFrame = true;
    g_Dt = dt;
    ++g_Frame;
    if (g_EntranceTime < 1.0e5f) g_EntranceTime += dt;
}

void GameMenuUi::StartEntrance()
{
    g_EntranceTime = 0.0f;
}

float GameMenuUi::Entrance(float delayFrames, float durationFrames)
{
    return EaseOut((g_EntranceTime - delayFrames) / durationFrames);
}

// the delay of a row grows with its height on the screen: 2 frames at the top, 16 frames at the bottom (12 frames to slide in)
static float RowEntrance(float topNormalized)
{
    return GameMenuUi::Entrance(2.0f + ToY(topNormalized) * 0.020f, 12.0f);
}

void GameMenuUi::SetRowExit(float leftPx, float alpha)
{
    g_RowExitDx = leftPx;
    g_RowExitA = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}

float GameMenuUi::RowShift(float topNormalized)
{
    return -(1.0f - RowEntrance(topNormalized)) * 90.0f / (UiKit::VIRTUAL_W * 0.5f);
}

float GameMenuUi::RowAlpha(float topNormalized)
{
    return RowEntrance(topNormalized);
}

// ---- selection -------------------------------------------------------------------------------------------------------
int GameMenuUi::UpdateMenu(int& selected, int count, int hovered, bool horizontal, GameSound moveSound, bool holdSelection)
{
    g_ConfirmConsumed = false;
    if (count <= 0)
    {
        selected = -1;
        return -1;
    }
    if (selected >= count) selected = count - 1;
    const int before = selected;
    const bool previousKey = horizontal ? (GameInput::IsTriggered(VK_LEFT) || GameInput::IsTriggered('A'))
                                        : (GameInput::IsTriggered(VK_UP) || GameInput::IsTriggered('W'));
    const bool nextKey = horizontal ? (GameInput::IsTriggered(VK_RIGHT) || GameInput::IsTriggered('D'))
                                    : (GameInput::IsTriggered(VK_DOWN) || GameInput::IsTriggered('S'));
    if (nextKey)
    {
        selected = selected < 0 ? 0 : (selected + 1) % count;
    }
    else if (previousKey)
    {
        selected = selected < 0 ? count - 1 : (selected + count - 1) % count;
    }
    else if (GameInput::MouseMoved())
    {
        if (hovered >= 0)
            selected = hovered;
        else if (!holdSelection)
            selected = -1; // the mouse left the rows: nothing is highlighted
    }
    if (selected != before && selected >= 0)
    {
        GameAudio::Play(moveSound);
    }
    int activated = -1;
    if (GameInput::IsTriggered(VK_RETURN) || GameInput::IsTriggered(VK_SPACE))
    {
        if (selected >= 0)
        {
            activated = selected;
        }
        else
        {
            selected = 0;
            g_ConfirmConsumed = true;
            GameAudio::Play(moveSound);
        }
    }
    else if (GameInput::IsTriggered(VK_LBUTTON) && hovered >= 0)
    {
        selected = hovered;
        activated = hovered;
    }
    return activated;
}

// ---- primitives ------------------------------------------------------------------------------------------------------
void GameMenuUi::Slab(float cx, float cy, float w, float h, float rotation, Color c)
{
    UiKit::ImageRotated(Id::Stripe, cx, cy, w, h, rotation, c);
}

void GameMenuUi::Segment(float x0, float y0, float x1, float y1, float thickness, Color c)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.5f) return;
    Slab((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, length, thickness, std::atan2(-dy, dx), c);
}

// ---- vector plates ------------------------------------------------------------------------------------------------------------------------
// The slanted plates of the menus are polygons now, not the 64 x 32 px cell of the atlas: that texture was magnified 2 to 2.5 times at 1440p
// and 4K, which made the selection plate and its edges soft. The edges of a polygon are exact at any resolution; a feather of 0.65 px (a ring
// of triangles whose outer vertices are transparent) anti-aliases them.
namespace
{
struct PlateV2
{
    float x, y;
};

struct PlateBlendScope
{
    Renderer2DBlendMode previous;
    PlateBlendScope() : previous(Renderer2D_GetBlendMode())
    {
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    }
    ~PlateBlendScope()
    {
        Renderer2D_SetBlendMode(previous);
    }
};

constexpr float PLATE_FEATHER = 0.65f;

// the four corners, clockwise on the screen: top left, top right, bottom right, bottom left. The edges lean to the right like a slash.
void PlateCorners(float x, float y, float w, float h, PlateV2 out[4])
{
    float slant = h * 0.22f;
    if (slant < 3.0f) slant = 3.0f;
    if (slant > 14.0f) slant = 14.0f;
    if (slant > w * 0.25f) slant = w * 0.25f;
    out[0] = {x + slant, y};
    out[1] = {x + w, y};
    out[2] = {x + w - slant, y + h};
    out[3] = {x, y + h};
}

// the polygon moved outwards by `offset` (negative = inwards); the corners are mitred
void PlateOffset(const PlateV2 in[4], float offset, PlateV2 out[4])
{
    PlateV2 normal[4]; // outward normal of the edge i -> i + 1
    for (int i = 0; i < 4; ++i)
    {
        const PlateV2& a = in[i];
        const PlateV2& b = in[(i + 1) % 4];
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0.0001f)
        {
            dx /= length;
            dy /= length;
        }
        normal[i] = {dy, -dx};
    }
    for (int i = 0; i < 4; ++i)
    {
        const PlateV2& n1 = normal[(i + 3) % 4];
        const PlateV2& n2 = normal[i];
        const float k = offset / (1.0f + n1.x * n2.x + n1.y * n2.y);
        out[i] = {in[i].x + (n1.x + n2.x) * k, in[i].y + (n1.y + n2.y) * k};
    }
}

void PlateQuad(const PlateV2& a, float alphaA, const PlateV2& b, float alphaB, const PlateV2& c, float alphaC, const PlateV2& d, float alphaD,
               Color col)
{
    const Renderer2DVertex v[4] = {{UiKit::NX(a.x), UiKit::NY(a.y), col.r, col.g, col.b, col.a * alphaA},
                                   {UiKit::NX(b.x), UiKit::NY(b.y), col.r, col.g, col.b, col.a * alphaB},
                                   {UiKit::NX(c.x), UiKit::NY(c.y), col.r, col.g, col.b, col.a * alphaC},
                                   {UiKit::NX(d.x), UiKit::NY(d.y), col.r, col.g, col.b, col.a * alphaD}};
    Renderer2D_DrawPolygon(v, 4);
}

// a ring between two offsets of the polygon, alpha from `alphaInner` (at the inner offset) to `alphaOuter`
void PlateRing(const PlateV2 base[4], float innerOffset, float alphaInner, float outerOffset, float alphaOuter, Color col)
{
    PlateV2 inner[4], outer[4];
    PlateOffset(base, innerOffset, inner);
    PlateOffset(base, outerOffset, outer);
    for (int i = 0; i < 4; ++i)
    {
        const int j = (i + 1) % 4;
        PlateQuad(inner[i], alphaInner, inner[j], alphaInner, outer[j], alphaOuter, outer[i], alphaOuter, col);
    }
}
}

void GameMenuUi::Plate(float x, float y, float w, float h, Color c)
{
    if (c.a <= 0.001f || w < 2.0f || h < 2.0f) return;
    PlateBlendScope blend;
    PlateV2 p[4];
    PlateCorners(x, y, w, h, p);
    // the solid part is the plate inset by half the feather; the feather ring (alpha 1 -> 0) reaches half a feather beyond the real edge, so the
    // visible edge is where the geometry says it is
    PlateV2 core[4];
    PlateOffset(p, -PLATE_FEATHER * 0.5f, core);
    const Renderer2DVertex v[4] = {{UiKit::NX(core[0].x), UiKit::NY(core[0].y), c.r, c.g, c.b, c.a},
                                   {UiKit::NX(core[1].x), UiKit::NY(core[1].y), c.r, c.g, c.b, c.a},
                                   {UiKit::NX(core[2].x), UiKit::NY(core[2].y), c.r, c.g, c.b, c.a},
                                   {UiKit::NX(core[3].x), UiKit::NY(core[3].y), c.r, c.g, c.b, c.a}};
    Renderer2D_DrawPolygon(v, 4);
    PlateRing(p, -PLATE_FEATHER * 0.5f, 1.0f, PLATE_FEATHER * 0.5f, 0.0f, c);
}

void GameMenuUi::PlateLine(float x, float y, float w, float h, Color c)
{
    if (c.a <= 0.001f || w < 4.0f || h < 4.0f) return;
    PlateBlendScope blend;
    PlateV2 p[4];
    PlateCorners(x, y, w, h, p);
    constexpr float THICKNESS = 1.25f; // the line is centred on the edge of the plate
    const float core = THICKNESS * 0.5f - PLATE_FEATHER * 0.5f;
    PlateRing(p, -core, 1.0f, core, 1.0f, c);
    PlateRing(p, core, 1.0f, core + PLATE_FEATHER, 0.0f, c);
    PlateRing(p, -core, 1.0f, -core - PLATE_FEATHER, 0.0f, c);
}

void GameMenuUi::Panel(float x, float y, float w, float h, float corner, Color fill, Color line)
{
    Surface(x, y, w, h, corner, fill, line);
}

void GameMenuUi::Hairline(float x, float y, float w, float h, float thickness, Color c)
{
    HairRect(x, y, w, h, thickness, c);
}

void GameMenuUi::DiamondFill(float cx, float cy, float radius, Color c)
{
    const float side = radius * 1.41421356f;
    Slab(cx, cy, side, side, PI * 0.25f, c);
}

void GameMenuUi::DiamondOutline(float cx, float cy, float radius, float thickness, Color c)
{
    const float length = radius * 1.41421356f + thickness;
    const float half = radius * 0.5f;
    Slab(cx + half, cy - half, length, thickness, -PI * 0.25f, c);
    Slab(cx + half, cy + half, length, thickness, PI * 0.25f, c);
    Slab(cx - half, cy + half, length, thickness, -PI * 0.25f, c);
    Slab(cx - half, cy - half, length, thickness, PI * 0.25f, c);
}

void GameMenuUi::DrawHeader(const std::string& title, const std::string& sub, float progress)
{
    // THE SEAM header: a black slab slams in (P5), the title on it in heavy letters with a hard offset in the colour of the polarity,
    // a glass line of the other colour glides along its top (P3), and the seam rule runs under it
    const float p = EaseOut(progress);
    if (p <= 0.0f) return;
    const float dx = -(1.0f - p) * 800.0f;
    const float glide = EaseOut(Clamp01(progress * 1.4f - 0.4f)); // the glass line comes a beat later and smoother
    Plate(-40.0f + dx + 10.0f, 66.0f, 800.0f, 60.0f, Accent(0.92f * p));
    Plate(-40.0f + dx, 60.0f, 800.0f, 60.0f, Rgba(0.0f, 0.0f, 0.0f, 0.90f * p));
    UiKit::Fill(-40.0f + 760.0f * (1.0f - glide), 56.0f, 800.0f * glide, 1.5f, Counter(0.85f * glide));
    const float px = UiKit::FitFontPx(title, 42.0f, 560.0f, 16.0f);
    DrawHeavyText(title, 84.0f + dx + 3.0f, 92.0f + 2.5f, px, Accent(p), TextAlignment::Left, 640.0f);
    DrawHeavyText(title, 84.0f + dx, 92.0f, px, Rgba(0.98f, 0.97f, 0.94f, p), TextAlignment::Left, 640.0f);
    SeamRule(84.0f, 138.0f, 300.0f, glide, p);
    if (sub.empty()) return;
    UiKit::Text(sub, 84.0f + dx, 164.0f, UiKit::FitFontPx(sub, 20.0f, 600.0f, 12.0f), Rgba(0.97f, 0.98f, 1.0f, p),
                TextAlignment::Left, 660.0f, false);
}

// ---- rows ------------------------------------------------------------------------------------------------------------
float GameMenuUi::SelectionAmount(float left, float top)
{
    const RowAnim* row = FindRow(RowKey(left, top));
    return row != nullptr ? EaseOut(row->t) : 0.0f;
}

void GameMenuUi::DrawRow(float left, float top, float right, float bottom, RowStyle style, bool flat)
{
    const float y = ToY(top);
    const float w = ToW(right - left);
    const float h = ToH(top - bottom);
    const float p = RowAlpha(top) * g_RowExitA; // entrance: the rows come in one after the other, from the left
    const float x = ToX(left) + RowShift(top) * (UiKit::VIRTUAL_W * 0.5f) - g_RowExitDx;
    const bool selectedStyle = style == RowStyle::Selected || style == RowStyle::DangerSelected;
    const bool danger = style == RowStyle::Danger || style == RowStyle::DangerSelected;
    RowAnim& anim = TouchRow(RowKey(left, top));
    const float target = selectedStyle ? 1.0f : 0.0f;
    if (target > anim.t)
        anim.t = (std::min)(target, anim.t + g_Dt / 6.0f);
    else
        anim.t = (std::max)(target, anim.t - g_Dt / 8.0f);
    if (p <= 0.001f) return;
    const float t = anim.t;
    const float e = EaseOut(t);
    // resting look: a hairline plate and a small dash in front (NieR)
    const float restA = (1.0f - e) * p;
    if (restA > 0.01f)
    {
        if (flat)
        {
            // Persona style: no frame behind the text, only a hairline under the row (the text sits on the background);
            // the highlight is the plate that slides in below
            const Color line = danger ? Rgba(1.0f, 0.36f, 0.30f, 0.45f)
                                      : (style == RowStyle::Important ? Rgba(1.0f, 0.80f, 0.30f, 0.55f) : Rgba(1.0f, 1.0f, 1.0f, 0.24f));
            UiKit::Fill(x + h * 0.5f, y + h - 1.0f, w - h * 0.5f, 1.0f, WithAlpha(line, restA));
        }
        else
        switch (style)
        {
        case RowStyle::Quiet:
            UiKit::Fill(x + h * 0.5f, y + h - 1.0f, w - h, 1.0f, Rgba(1.0f, 1.0f, 1.0f, 0.10f * restA));
            break;
        case RowStyle::Normal:
        case RowStyle::Selected:
            PlateLine(x, y, w, h, Rgba(1.0f, 1.0f, 1.0f, 0.22f * restA));
            break;
        case RowStyle::Danger:
        case RowStyle::DangerSelected:
            PlateLine(x, y, w, h, Rgba(1.0f, 0.36f, 0.30f, 0.55f * restA));
            break;
        case RowStyle::Important:
            PlateLine(x, y, w, h, Rgba(1.0f, 0.80f, 0.30f, 0.55f * restA));
            break;
        }
    }
    // THE MARK: the seam diamond is the CURSOR - it exists only in front of the row that is selected, and slides out to the plate as it arrives. A row at
    // rest has no mark at all (a column of small diamonds in front of every row was noise: it said "here" six times and meant nothing).
    if (e <= 0.01f) return;
    if (style != RowStyle::Quiet) RowMark(x - 16.0f * (0.35f + 0.65f * e), y + h * 0.5f, 1.0f, p * e, danger);
    // highlighted look: the ivory plate slides in from the left, the red plate follows one beat later
    const float minW = (std::min)(w, h * 1.3f);
    const float plateW = minW + (w - minW) * e;
    const float eRed = EaseOut(Clamp01((t - 0.25f) / 0.75f));
    const float redW = minW + (w - minW) * eRed;
    // P5: the ivory plate slams in on a hard offset plate in the colour of the polarity (danger: red plate on ivory)
    const Color top1 = danger ? Rgba(1.0f, 0.30f, 0.25f, 0.98f) : Ivory(0.98f);
    const Color back = danger ? Ivory(1.0f) : Accent(1.0f);
    Plate(x + 8.0f * e, y + 5.0f * e, plateW, h, Rgba(0.0f, 0.0f, 0.0f, 0.55f * e * p));
    Plate(x + 5.0f * eRed, y + 4.0f * eRed, redW, h, WithAlpha(back, eRed * p));
    Plate(x, y, plateW, h, WithAlpha(top1, e * p));
    // P3: a glass line of the other colour glides along over the top edge, a beat later and smoother
    const float glide = EaseOut(Clamp01((t - 0.35f) / 0.65f));
    if (!danger && glide > 0.0f)
    {
        const float k = h * 0.30f; // the plates lean: follow the top edge
        UiKit::Fill(x + k + 6.0f, y - 5.0f, (plateW - 12.0f) * glide, 1.5f, Counter(0.90f * glide * p));
    }
}

// ---- backdrop --------------------------------------------------------------------------------------------------------
void GameMenuUi::SetPlayFieldBrightness(int percent)
{
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    // 2 % ... 9 % of the flat colour over the black plate. (It was 5 % ... 22 %, and the red field was too bright to read the bullets on: the old DEFAULT, 11.8 %, is now
    // hardly above the middle of the range, and the brightest setting is only a little brighter than it.)
    g_PlayFieldAlpha = 0.02f + 0.07f * static_cast<float>(percent) / 100.0f;
}

void GameMenuUi::SetBackdropPolarity(bool red, bool instant)
{
    if (red != g_BackdropRed)
    {
        g_BackdropRed = red;
        g_BackdropFlash = 0;
    }
    if (instant)
    {
        g_BackdropAmount = red ? 1.0f : 0.0f;
        g_BackdropSpring = g_BackdropAmount;
        g_BackdropVelocity = 0.0f;
        g_BackdropFlash = 100;
    }
}

void GameMenuUi::UpdateBackdrop()
{
    const float target = g_BackdropRed ? 1.0f : 0.0f;
    const float step = 1.0f / 12.0f;
    if (g_BackdropAmount < target)
        g_BackdropAmount = (std::min)(target, g_BackdropAmount + step);
    else if (g_BackdropAmount > target)
        g_BackdropAmount = (std::max)(target, g_BackdropAmount - step);
    // the spring that turns the cut of the title (see WithTitleTilt)
    g_BackdropVelocity += (target - g_BackdropSpring) * SPRING_K - g_BackdropVelocity * SPRING_C;
    g_BackdropSpring += g_BackdropVelocity;
    if (std::fabs(target - g_BackdropSpring) < 0.0005f && std::fabs(g_BackdropVelocity) < 0.0005f)
    {
        g_BackdropSpring = target;
        g_BackdropVelocity = 0.0f;
    }
    if (g_BackdropFlash < 100) ++g_BackdropFlash;
    ++g_BackdropClock;
}

void GameMenuUi::SetStage(Stage stage)
{
    g_Stage = stage;
}

void GameMenuUi::Impact()
{
    g_BackdropFlash = 0; // the flash on the cut and the burst of shards (see DrawComposition)
}

void GameMenuUi::DrawBackdrop(float strength)
{
    DrawStage(g_Stage, strength);
}

// ---- the cut of the title turns around the ship ------------------------------------------------------------------------------------------
// When the ship of the title changes colour, the red / blue cut does not just slide: it TURNS around the ship, about 24 degrees each way, driven
// by the spring of UpdateBackdrop. Every other screen keeps its own fixed angle.
// the layout with its cut turned; `weight` fades the turn in and out when the title is entered or left
namespace
{
Layout WithTitleTilt(const Layout& source, float weight)
{
    Layout L = source;
    if (weight <= 0.001f) return L;
    const float delta = (g_BackdropSpring - 0.5f) * 2.0f * TITLE_TILT * weight;
    for (int sh = 0; sh < 2; ++sh)
    {
        for (int k = 0; k < 3; ++k)
        {
            HalfPlane& h = L.shape[sh][k];
            if (h.follow == 0.0f) continue; // only the cut follows the polarity
            // how far the ship is from the line (along its normal): the line keeps this distance while it turns around the ship
            const float dist = std::cos(h.theta) * (TITLE_PIVOT_X - CX) + std::sin(h.theta) * (TITLE_PIVOT_Y - CY) - h.d;
            h.theta += delta;
            h.d = std::cos(h.theta) * (TITLE_PIVOT_X - CX) + std::sin(h.theta) * (TITLE_PIVOT_Y - CY) - dist;
        }
    }
    return L;
}
}

int LayoutOf(GameMenuUi::Stage stage)
{
    switch (stage)
    {
    case GameMenuUi::Stage::Title: return L_TITLE;
    case GameMenuUi::Stage::HowTo: return L_HOWTO;
    case GameMenuUi::Stage::Records: return L_RECORDS;
    case GameMenuUi::Stage::Settings: return L_SETTINGS;
    case GameMenuUi::Stage::GameOver: return L_GAMEOVER;
    case GameMenuUi::Stage::GameClear: return L_GAMECLEAR;
    case GameMenuUi::Stage::Hangar: return L_HANGAR;
    default: return L_PLAY;
    }
}

void GameMenuUi::DrawStage(Stage stage, float strength)
{
    if (strength <= 0.01f) return;
    switch (stage)
    {
    case Stage::Pause:
    case Stage::Pick:
        // an overlay: the two corners grow out of the screen edges when it opens (L_NONE -> L_PAUSE / L_PICK)
        StepMorph(g_Overlay, stage == Stage::Pause ? L_PAUSE : L_PICK, L_NONE);
        DrawComposition(g_Overlay.cur, 1.0f, false);
        break;
    default:
    {
        // every other screen has its own layout; the strength of the caller is not used (it is part of the layout, so it can change
        // smoothly from one screen to the next). The title also turns its cut with the polarity of the ship.
        StepMorph(g_World, LayoutOf(stage));
        const float tiltWanted = g_World.toId == L_TITLE ? 1.0f : 0.0f;
        g_TitleTilt += (tiltWanted - g_TitleTilt) * (1.0f - std::exp(-0.22f * g_Dt));
        g_CutShiftPx = CUT_SHIFT_PX + (TITLE_CUT_SHIFT_PX - CUT_SHIFT_PX) * g_TitleTilt;
        DrawComposition(WithTitleTilt(g_World.cur, g_TitleTilt), 1.0f, true);
        break;
    }
    }
}

void GameMenuUi::DrawCard(float left, float top, float right, float bottom, float red, float green, float blue)
{
    const float p = RowAlpha(top);
    if (p <= 0.001f) return;
    const float x = ToX(left) + RowShift(top) * (UiKit::VIRTUAL_W * 0.5f);
    const float y = ToY(top);
    const float w = ToW(right - left);
    const float h = ToH(top - bottom);
    // no frame behind the text: the coloured tick at the left and a hairline under the card (the text sits on the background)
    UiKit::Fill(x + 22.0f, y + h - 1.0f, w - 22.0f, 1.0f, Rgba(1.0f, 1.0f, 1.0f, 0.22f * p));
    UiKit::Fill(x + 22.0f, y + h - 3.0f, (w - 22.0f) * 0.35f, 1.0f, Counter(0.35f * p)); // (the glass line of the seam)
    UiKit::Image(Id::Tick, x + 10.0f, y + 15.0f, 4.0f, h - 30.0f, Rgba(red, green, blue, 0.90f * p));
}

void GameMenuUi::DrawArrow(float centerX, float centerY, bool left, float alpha)
{
    // THE SEAM: the left arrow is the red half of the seam diamond, the right arrow the blue half (hard triangles)
    const float x = ToX(centerX), y = ToY(centerY), r = 9.0f;
    const float a = (std::min)(1.0f, alpha);
    const Color c = WithAlpha(left ? SEAM_RED : SEAM_BLUE, a);
    const float tip = left ? x - r : x + r, back = left ? x + r * 0.5f : x - r * 0.5f;
    const Renderer2DVertex v[3] = {{UiKit::NX(tip), UiKit::NY(y), c.r, c.g, c.b, c.a},
                                   {UiKit::NX(back), UiKit::NY(y - r), c.r, c.g, c.b, c.a},
                                   {UiKit::NX(back), UiKit::NY(y + r), c.r, c.g, c.b, c.a}};
    Renderer2D_DrawPolygon(v, 3);
}

void GameMenuUi::Band(float x0, float x1, float cx, float cy, float h, float slope, Color c)
{
    if (c.a <= 0.0f || h <= 0.0f) return;
    const float y0 = cy + slope * (x0 - cx), y1 = cy + slope * (x1 - cx);
    const Renderer2DVertex v[4] = {{UiKit::NX(x0), UiKit::NY(y0 - h * 0.5f), c.r, c.g, c.b, c.a}, {UiKit::NX(x1), UiKit::NY(y1 - h * 0.5f), c.r, c.g, c.b, c.a},
                                   {UiKit::NX(x1), UiKit::NY(y1 + h * 0.5f), c.r, c.g, c.b, c.a}, {UiKit::NX(x0), UiKit::NY(y0 + h * 0.5f), c.r, c.g, c.b, c.a}};
    Renderer2D_DrawPolygon(v, 4);
}

void GameMenuUi::Halftone(float x0, float x1, float cy, float slope, float step, float maxR, Color c)
{
    if (c.a <= 0.0f) return;
    const int n = static_cast<int>((x1 - x0) / step);
    for (int i = 0; i <= n; ++i)
    {
        const float x = x1 - step * static_cast<float>(i);
        const float k = 1.0f - static_cast<float>(i) / static_cast<float>((std::max)(1, n));
        for (int j = -2; j <= 2; ++j)
        {
            const float y = cy + slope * (x - x1) + static_cast<float>(j) * step + ((i & 1) ? step * 0.5f : 0.0f);
            const float r = maxR * k * (1.0f - std::fabs(static_cast<float>(j)) / 3.0f);
            if (r < 0.8f) continue;
            DiamondFill(x, y, r, c);
        }
    }
}

void GameMenuUi::Notice(const std::string& text, float cx, float cy, int timer, int total, bool refusal)
{
    if (timer <= 0 || total <= 0 || text.empty()) return;
    const float t = static_cast<float>(total - timer);
    const float in = EaseOut(Clamp01(t / 8.0f));
    const float out = Clamp01(static_cast<float>(timer) / 10.0f); // the last 10 frames: it folds (the height goes)
    const float px = 15.0f;
    const float w = UiKit::TextWidth(text, px) * 1.06f + 58.0f;
    const float h = 30.0f * (0.25f + 0.75f * out);
    const float x = cx - w * 0.5f + (1.0f - in) * 60.0f;
    const float y = cy - h * 0.5f;
    const float a = in;
    Plate(x + 6.0f, y + 4.0f, w, h, refusal ? Rgba(1.0f, 0.22f, 0.18f, a) : Accent(a));
    Plate(x, y, w, h, Ivory(0.98f * a));
    if (!refusal) UiKit::Fill(x + 10.0f, y - 5.0f, (w - 14.0f) * in, 1.5f, Counter(0.85f * a));
    SeamDiamond(x + 20.0f, cy, 6.5f * out + 1.0f, a);
    if (out > 0.4f) DrawHeavyText(text, x + 38.0f, cy, px, Ink(a * (out - 0.4f) / 0.6f), TextAlignment::Left, w);
}

void GameMenuUi::SelectTab(float x, float y, float w, float h, float alpha, float offset)
{
    if (alpha <= 0.0f) return;
    Plate(x + offset, y + offset * 0.8f, w, h, Accent(alpha));
    Plate(x, y, w, h, Ivory(0.98f * alpha));
    UiKit::Fill(x + offset + 3.0f, y - offset, w - offset - 6.0f, 1.5f, Counter(0.9f * alpha));
}

void GameMenuUi::SelectTile(float x, float y, float w, float h, float alpha, bool mark)
{
    if (alpha <= 0.0f) return;
    const Color none = Rgba(0.0f, 0.0f, 0.0f, 0.0f);
    Panel(x + 8.0f, y + 6.0f, w, h, 0.0f, Rgba(0.0f, 0.0f, 0.0f, 0.60f * alpha), none);
    Panel(x + 5.0f, y + 4.0f, w, h, 0.0f, Accent(alpha), none);
    Panel(x, y, w, h, 0.0f, Ivory(0.98f * alpha), none);
    UiKit::Fill(x + 4.0f, y - 4.0f, w - 8.0f, 1.5f, Counter(0.9f * alpha));
    if (mark) SeamDiamond(x + 8.0f, y + 8.0f, 4.5f, alpha); // (inside the corner: the diamond used to sit between two tiles and cover the one next to it)
}

void GameMenuUi::SectionTitle(const std::string& text, float x, float y, float width, float alpha)
{
    if (alpha <= 0.0f) return;
    SeamDiamond(x + 6.0f, y, 5.5f, alpha);
    DrawHeavyText(text, x + 20.0f, y, 14.0f, Ivory(0.95f * alpha), TextAlignment::Left, width);
    const float tx = x + 20.0f + UiKit::TextWidth(text, 14.0f) * 1.06f + 14.0f;
    if (tx < x + width) UiKit::Fill(tx, y, x + width - tx, 1.0f, Counter(0.55f * alpha));
}

void GameMenuUi::SeamGauge(float x, float cy, float width, float rate, float alpha, bool lit)
{
    if (alpha <= 0.0f) return;
    constexpr int CELLS = 20;
    const float h = lit ? 12.0f : 10.0f, gap = 3.0f;
    const float cw = (width - gap * static_cast<float>(CELLS - 1)) / static_cast<float>(CELLS);
    const float y = cy - h * 0.5f;
    rate = Clamp01(rate);
    for (int i = 0; i < CELLS; ++i)
    {
        const float f0 = static_cast<float>(i) / CELLS;
        const float cx = x + (cw + gap) * static_cast<float>(i);
        const bool on = rate > f0 + 0.5f / CELLS;
        Plate(cx, y, cw, h, on ? (lit ? Ink(0.92f * alpha) : Accent(0.95f * alpha)) : Rgba(1.0f, 1.0f, 1.0f, (lit ? 0.22f : 0.12f) * alpha));
    }
    const float kx = x + width * rate;
    SeamDiamond(kx, cy, lit ? 9.0f : 7.0f, alpha);
}

void GameMenuUi::ValueTag(const std::string& text, float cx, float cy, float fontPx, float alpha, bool lit)
{
    if (alpha <= 0.0f || text.empty()) return;
    DrawHeavyText(text, cx, cy, fontPx, lit ? Ink(alpha) : Ivory(0.95f * alpha), TextAlignment::Center, 300.0f);
    if (!lit)
    {
        const float w = UiKit::TextWidth(text, fontPx) * 1.06f;
        UiKit::Fill(cx - w * 0.5f, cy + fontPx * 0.75f, w, 1.5f, Accent(0.80f * alpha));
    }
}

// Title of a page: the red band at the top left, the same as the hangar and the module pick (no panel behind the options)
void GameMenuUi::DrawPageHeader(const std::string& title)
{
    DrawHeader(title, std::string(), Clamp01(g_EntranceTime / 12.0f));
}

void GameMenuUi::DrawHeavyText(const std::string& text, float x, float y, float fontPx, Color c, TextAlignment align, float boxWidth)
{
    if (text.empty() || c.a <= 0.0f) return;
    TextStyle style{};
    style.size = fontPx / 2160.0f;
    style.red = c.r;
    style.green = c.g;
    style.blue = c.b;
    style.alpha = c.a;
    style.bold = true;
    style.layer = 1; // heavy text IS display text, whatever its size: one typeface for the headings, the rows and the numbers of every page
    style.shadow = false;
    style.alignment = align;
    style.layoutWidth = boxWidth / (UiKit::VIRTUAL_W * 0.5f);
    float left = x;
    if (align == TextAlignment::Center) left -= boxWidth * 0.5f;
    else if (align == TextAlignment::Right) left -= boxWidth;
    const float top = y - fontPx * 0.62f;
    const float ny = 1.0f - 2.0f * (top + 0.18f * fontPx + UiKit::g_OriginY) / UiKit::VIRTUAL_H;
    TextRenderer::QueueText(text, UiKit::NX(left), ny, style);
}

void GameMenuUi::DrawCutText(const std::string& text, float x, float y, float fontPx, Color offsetColor, float offsetPx, float alpha,
                             TextAlignment align, float boxWidth)
{
    DrawHeavyText(text, x + offsetPx, y + offsetPx * 0.8f, fontPx, WithAlpha(offsetColor, alpha), align, boxWidth);
    DrawHeavyText(text, x, y, fontPx, Ivory(alpha), align, boxWidth);
}

// ---- result screens --------------------------------------------------------------------------------------------------
void GameMenuUi::DrawResultMenuItem(const std::string& text, float centerY, bool isSelected, bool danger)
{
    const float top = centerY + RESULT_MENU_HEIGHT * 0.5f;
    const float bottom = centerY - RESULT_MENU_HEIGHT * 0.5f;
    RowStyle rowStyle = RowStyle::Normal;
    if (isSelected)
        rowStyle = danger ? RowStyle::DangerSelected : RowStyle::Selected;
    else if (danger)
        rowStyle = RowStyle::Danger;
    DrawRow(RESULT_MENU_LEFT, top, RESULT_MENU_RIGHT, bottom, rowStyle);
    const bool lit = SelectionAmount(RESULT_MENU_LEFT, top) > 0.5f;
    TextStyle textStyle = TextTheme::GetStyle(lit ? TextRole::ButtonSelected : TextRole::Button);
    textStyle.size = 0.0120f;
    textStyle.alignment = TextAlignment::Center;
    textStyle.layoutWidth = RESULT_MENU_RIGHT - RESULT_MENU_LEFT;
    textStyle.shadow = false;
    if (danger && !lit)
    {
        textStyle.red = 1.00f;
        textStyle.green = 0.55f;
        textStyle.blue = 0.50f;
    }
    if (lit)
    {
        textStyle.red = 0.05f;
        textStyle.green = 0.06f;
        textStyle.blue = 0.09f;
    }
    textStyle.alpha *= RowAlpha(top);
    TextRenderer::QueueText(text, RESULT_MENU_LEFT + RowShift(top), centerY + 0.018f, textStyle);
}

bool GameMenuUi::IsMouseInsideRect(float left, float top, float right, float bottom)
{
    if (!GameInput::IsMouseInsideWindow()) return false;
    const float mouseX = GameInput::GetMouseX();
    const float mouseY = GameInput::GetMouseY();
    return mouseX >= left && mouseX <= right && mouseY <= top && mouseY >= bottom;
}











