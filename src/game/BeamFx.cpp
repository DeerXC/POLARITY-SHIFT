#include "game/BeamFx.h"

#include "core/Renderer2D.h"
#include "game/GameVfx.h"

#include <algorithm>
#include <cmath>

namespace BeamFx
{
namespace
{
constexpr float ASPECT = GameVfx::ASPECT;
constexpr float TAU = 6.2831853f;
constexpr float PI = 3.1415927f;
constexpr int MAX_SAMPLES = 112;

float Saturate(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float SmoothStep(float edge0, float edge1, float x)
{
    const float t = Saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float Hash(int seed)
{
    return GameVfx::Hash(seed);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
// the frame of a line: a point in "equal-scale" space (x, y * ASPECT), a direction, its normal, a length
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Frame
{
    float ox, oy;   // the origin (x, y * ASPECT)
    float dx, dy;   // the unit direction
    float nx, ny;   // the unit normal (to the left of the direction)
    float length;
};

Frame MakeFrame(float x0, float y0, float x1, float y1)
{
    Frame f{};
    f.ox = x0;
    f.oy = y0 * ASPECT;
    const float ex = x1 - x0, ey = (y1 - y0) * ASPECT;
    f.length = std::sqrt(ex * ex + ey * ey);
    const float inv = f.length > 1e-5f ? 1.0f / f.length : 0.0f;
    f.dx = ex * inv;
    f.dy = ey * inv;
    f.nx = -f.dy;
    f.ny = f.dx;
    return f;
}

// the point at distance s along the line and off across it, back in screen coordinates
void Place(const Frame& f, float s, float off, float& x, float& y)
{
    x = f.ox + f.dx * s + f.nx * off;
    y = (f.oy + f.dy * s + f.ny * off) / ASPECT;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------
// a ribbon: a strip along a line whose width, offset and brightness change from one sample to the next. The middle is `alpha`, the edges are alpha * edgeFactor: a soft
// profile made of vertex colours alone, no texture (so it is as sharp as the screen is, at any resolution)
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Sample
{
    float s;     // distance along the line
    float off;   // offset of the middle of the ribbon across the line
    float half;  // half width
    float alpha; // brightness of the middle
};

void Ribbon(const Frame& f, const Sample* samples, int count, const Rgb& colour, float edgeFactor)
{
    for (int i = 0; i + 1 < count; ++i)
    {
        const Sample& a = samples[i];
        const Sample& b = samples[i + 1];
        if (a.alpha <= 0.002f && b.alpha <= 0.002f) continue;
        float ax, ay, aux, auy, adx, ady, bx, by, bux, buy, bdx, bdy;
        Place(f, a.s, a.off, ax, ay);
        Place(f, a.s, a.off + a.half, aux, auy);
        Place(f, a.s, a.off - a.half, adx, ady);
        Place(f, b.s, b.off, bx, by);
        Place(f, b.s, b.off + b.half, bux, buy);
        Place(f, b.s, b.off - b.half, bdx, bdy);
        const float ea = a.alpha * edgeFactor, eb = b.alpha * edgeFactor;
        const Renderer2DVertex upper[4] = {{ax, ay, colour.r, colour.g, colour.b, a.alpha},
                                           {bx, by, colour.r, colour.g, colour.b, b.alpha},
                                           {bux, buy, colour.r, colour.g, colour.b, eb},
                                           {aux, auy, colour.r, colour.g, colour.b, ea}};
        const Renderer2DVertex lower[4] = {{ax, ay, colour.r, colour.g, colour.b, a.alpha},
                                           {bx, by, colour.r, colour.g, colour.b, b.alpha},
                                           {bdx, bdy, colour.r, colour.g, colour.b, eb},
                                           {adx, ady, colour.r, colour.g, colour.b, ea}};
        Renderer2D_DrawPolygon(upper, 4);
        Renderer2D_DrawPolygon(lower, 4);
    }
}

// the plasma moves: three waves of different length, running at different speeds in different directions (their sum is never the same twice, and never a sine)
float Wave(float s, float t, int seed)
{
    const float ph = Hash(seed) * TAU;
    return 0.55f * std::sin(s * 13.0f - t * 0.31f + ph) + 0.30f * std::sin(s * 29.0f + t * 0.47f + 2.1f * ph) + 0.15f * std::sin(s * 61.0f - t * 0.83f + 3.7f * ph);
}

// a jagged bolt between two points of the frame: `parts` segments, the middle ones thrown off the line by up to `jag`
void Bolt(const Frame& f, float s0, float off0, float s1, float off1, float jag, int parts, int seed, float thickness, const Rgb& colour, float alpha)
{
    float px, py;
    Place(f, s0, off0, px, py);
    for (int k = 1; k <= parts; ++k)
    {
        const float u = static_cast<float>(k) / static_cast<float>(parts);
        const float kick = k == parts ? 0.0f : (Hash(seed + k * 3) - 0.5f) * 2.0f * jag * std::sin(PI * u);
        float qx, qy;
        Place(f, s0 + (s1 - s0) * u + kick * 0.5f, off0 + (off1 - off0) * u + kick, qx, qy);
        GameVfx::DrawBar(px, py, qx, qy, thickness * (1.0f - 0.45f * u), colour.r, colour.g, colour.b, alpha * (1.0f - 0.35f * u));
        px = qx;
        py = qy;
    }
    GameVfx::DrawRound(GameVfx::Sprite::Glow, px, py, 0.022f, colour.r, colour.g, colour.b, 0.45f * alpha);
}

Rgb Mix(const Rgb& a, const Rgb& b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

constexpr Rgb WHITE{1.0f, 1.0f, 1.0f};
} // namespace

// ================================================================================================================================================================
// palettes
// ================================================================================================================================================================
Palette PiercingPalette(bool shipIsRed)
{
    // white, with a trace of the colour of the ship in the body, and the strands in the two colours of the game (the colour of the ship, and its mirror)
    const Rgb ship = shipIsRed ? Rgb{1.00f, 0.30f, 0.26f} : Rgb{0.30f, 0.62f, 1.00f};
    const Rgb mirror = shipIsRed ? Rgb{0.30f, 0.62f, 1.00f} : Rgb{1.00f, 0.30f, 0.26f};
    return {WHITE, Mix(WHITE, ship, 0.22f), Mix(WHITE, ship, 0.45f), ship, mirror};
}

Palette BossPalette()
{
    return {WHITE, {0.94f, 0.96f, 1.00f}, {0.86f, 0.90f, 1.00f}, {1.00f, 0.26f, 0.20f}, {0.22f, 0.58f, 1.00f}};
}

Palette WhitePalette()
{
    return {WHITE, {0.95f, 0.97f, 1.00f}, {0.80f, 0.88f, 1.00f}, {0.86f, 0.92f, 1.00f}, {0.86f, 0.92f, 1.00f}};
}

Palette VioletPalette()
{
    return {WHITE, {0.90f, 0.82f, 1.00f}, {0.80f, 0.62f, 1.00f}, {0.78f, 0.55f, 1.00f}, {0.95f, 0.90f, 1.00f}};
}

Palette FirePalette()
{
    return {{1.00f, 0.90f, 0.62f}, {1.00f, 0.55f, 0.14f}, {1.00f, 0.38f, 0.08f}, {1.00f, 0.62f, 0.20f}, {0.95f, 0.25f, 0.06f}};
}

// ================================================================================================================================================================
// the beam
// ================================================================================================================================================================
void DrawBeam(const BeamParams& p, const Palette& pal)
{
    if (p.envelope <= 0.01f || p.grown <= 0.001f) return;
    const Frame f = MakeFrame(p.x0, p.y0, p.x1, p.y1);
    const float fullLength = f.length;
    const float length = fullLength * Saturate(p.grown);
    if (length < 0.02f) return;

    const Renderer2DBlendMode previousBlend = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);

    const float t = p.time;
    const float env = Saturate(p.envelope);
    const float hw = p.halfWidth * (0.35f + 0.65f * env); // the zone thins out as the beam goes
    const float amp = p.turbulence * hw * 0.28f;          // how far the sheath may leave the core
    const int n = std::max(8, std::min(MAX_SAMPLES - 1, static_cast<int>(length / 0.026f) + 2));

    // brightness along the beam: it starts softly at the muzzle (the flare is there) and ends in a round tip while it is growing
    auto along = [&](float s)
    {
        const float start = SmoothStep(0.0f, 0.07f, s / std::max(fullLength, 0.2f));
        const float tip = p.grown >= 0.999f ? 1.0f : SmoothStep(0.0f, 0.10f, length - s);
        return start * tip;
    };

    Sample aura[MAX_SAMPLES], sheath[MAX_SAMPLES], sheath2[MAX_SAMPLES], core[MAX_SAMPLES], hair[MAX_SAMPLES];
    for (int i = 0; i < n; ++i)
    {
        const float s = length * static_cast<float>(i) / static_cast<float>(n - 1);
        const float k = along(s) * env;
        aura[i] = {s, 0.0f, hw * 2.4f * (1.0f + 0.06f * std::sin(s * 7.0f - t * 0.20f)), 0.20f * k};
        sheath[i] = {s, amp * Wave(s, t, p.seed), hw * 0.95f * (1.0f + 0.16f * std::sin(s * 17.0f - t * 0.40f)), 0.72f * k};
        sheath2[i] = {s, -amp * 0.8f * Wave(s + 3.3f, t * 1.2f, p.seed + 1), hw * 0.72f * (1.0f + 0.20f * std::sin(s * 23.0f + t * 0.55f)), 0.42f * k};
        core[i] = {s, 0.0f, hw * 0.20f * (1.0f + 0.10f * std::sin(t * 1.1f + s * 3.0f)), 0.95f * k};
        hair[i] = {s, 0.0f, 0.0055f, 1.0f * k};
    }
    Ribbon(f, aura, n, pal.aura, 0.0f);
    Ribbon(f, sheath, n, pal.body, 0.24f);
    Ribbon(f, sheath2, n, pal.aura, 0.0f);

    // the braid: two strands wind round the core, half a turn apart, one in each colour. Where a strand is in front it is brighter and fatter; where it is behind, dim and thin
    if (p.braid)
    {
        const float turn = 20.0f;
        const float phase = Hash(p.seed + 5) * TAU;
        for (int strand = 0; strand < 2; ++strand)
        {
            Sample st[MAX_SAMPLES];
            for (int i = 0; i < n; ++i)
            {
                const float s = length * static_cast<float>(i) / static_cast<float>(n - 1);
                const float angle = s * turn - t * 0.28f + phase + static_cast<float>(strand) * PI;
                const float depth = 0.5f + 0.5f * std::cos(angle);
                st[i] = {s, hw * 0.78f * std::sin(angle), 0.0085f + 0.0045f * depth, (0.30f + 0.65f * depth) * along(s) * env};
            }
            Ribbon(f, st, n, strand == 0 ? pal.strandA : pal.strandB, 0.0f);
        }
    }

    // the core: straight, the hot line that does not move - this is where it hurts
    Ribbon(f, core, n, pal.core, 0.25f);
    Ribbon(f, hair, n, WHITE, 0.0f);

    // the rails: the edge of the danger, thin, straight and hard (the seam of the game: one colour above, the other below)
    if (p.rails)
    {
        const float railPulse = 0.82f + 0.18f * std::sin(t * 0.6f);
        for (int side = -1; side <= 1; side += 2)
        {
            Sample rail[MAX_SAMPLES];
            for (int i = 0; i < n; ++i)
            {
                const float s = length * static_cast<float>(i) / static_cast<float>(n - 1);
                rail[i] = {s, static_cast<float>(side) * hw, side > 0 ? 0.0030f : 0.0022f, 0.80f * railPulse * along(s) * env};
            }
            Ribbon(f, rail, n, side > 0 ? pal.strandA : pal.strandB, 0.0f);
        }
    }

    // pulses: bright beads that run along the core (from the muzzle to the end when pulseSpeed > 0): the beam is a flow, it is not a rod
    {
        constexpr int PULSES = 6;
        const float perStep = std::fabs(p.pulseSpeed) / 60.0f * 0.5f;
        for (int j = 0; j < PULSES; ++j)
        {
            float u = std::fmod(static_cast<float>(j) / PULSES + t * perStep + Hash(p.seed + 11) , 1.0f);
            if (p.pulseSpeed < 0.0f) u = 1.0f - u;
            const float s = u * length;
            const float k = along(s) * env * (0.55f + 0.45f * Hash(p.seed + 20 + j));
            const float len = 0.06f + 0.07f * Hash(p.seed + 30 + j);
            float ax, ay, bx, by;
            Place(f, std::max(0.0f, s - len), 0.0f, ax, ay);
            Place(f, s, 0.0f, bx, by);
            GameVfx::DrawBar(ax, ay, bx, by, hw * 0.30f, pal.core.r, pal.core.g, pal.core.b, 0.55f * k);
            GameVfx::DrawDiamond(bx, by, hw * 0.34f, 0.0f, 1.0f, 1.0f, 1.0f, 0.75f * k);
        }
    }

    // arcs: lightning that jumps off the plasma for a moment. They are re-rolled every three steps, so they crackle, and each one fades in its three steps
    if (p.arcs > 0)
    {
        const int slot = static_cast<int>(t / 3.0f);
        const float life = 1.0f - std::fmod(t, 3.0f) / 3.0f;
        for (int a = 0; a < p.arcs; ++a)
        {
            const int seed = p.seed * 131 + slot * 17 + a * 7;
            if (Hash(seed) < 0.30f + 0.25f * (1.0f - p.turbulence)) continue;
            const float s0 = length * (0.08f + 0.88f * Hash(seed + 1));
            const float side = Hash(seed + 2) < 0.5f ? -1.0f : 1.0f;
            const float reach = hw * (0.9f + 1.3f * Hash(seed + 3));
            const float drift = (Hash(seed + 4) - 0.5f) * 0.16f;
            const float a0 = along(s0) * env * life;
            if (a0 <= 0.01f) continue;
            const Rgb tint = Mix(WHITE, side > 0 ? pal.strandA : pal.strandB, 0.55f);
            Bolt(f, s0, side * hw * 0.25f, s0 + drift, side * (hw * 0.45f + reach), hw * 0.9f, 6, seed + 10, 0.0042f, tint, 1.0f * a0);
            if (Hash(seed + 5) > 0.55f) // a fork
                Bolt(f, s0 + drift * 0.5f, side * (hw * 0.45f + reach * 0.5f), s0 + drift * 1.8f, side * (hw * 0.8f + reach * 1.2f), hw * 0.6f, 3, seed + 40, 0.0030f, tint, 0.75f * a0);
        }
    }

    // sparks: small bright chips that leave the plasma and are gone in sixteen steps
    {
        constexpr int SPARKS = 10;
        constexpr float LIFE = 16.0f;
        for (int i = 0; i < SPARKS; ++i)
        {
            const float born = std::fmod(t + Hash(p.seed + 60 + i) * LIFE, LIFE) / LIFE;
            const float s = length * (0.06f + 0.9f * Hash(p.seed + 80 + i + static_cast<int>((t + Hash(p.seed + 60 + i) * LIFE) / LIFE) * 13));
            const float side = (i & 1) ? 1.0f : -1.0f;
            const float off = side * (hw * 0.7f + born * (0.05f + 0.07f * Hash(p.seed + 100 + i)));
            const float sp = s - born * 0.10f; // they are swept back: the beam goes one way, the chips are left behind
            float ax, ay, bx, by;
            Place(f, sp, off, ax, ay);
            Place(f, sp + 0.03f, off - side * 0.02f, bx, by);
            GameVfx::DrawBar(ax, ay, bx, by, 0.0034f * (1.0f - born), pal.body.r, pal.body.g, pal.body.b, 0.80f * (1.0f - born) * along(s) * env);
        }
    }

    Renderer2D_SetBlendMode(previousBlend);
}

void DrawMuzzle(float x, float y, float size, float time, float envelope, const Palette& pal)
{
    const float env = Saturate(envelope);
    if (env <= 0.01f) return;
    const Renderer2DBlendMode previousBlend = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    const float pulse = 0.90f + 0.10f * std::sin(time * 0.9f);
    GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, size * 1.5f * pulse, pal.aura.r, pal.aura.g, pal.aura.b, 0.55f * env);
    GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, size * 0.55f, 1.0f, 1.0f, 1.0f, 0.95f * env);
    // four spikes that turn slowly, and four shorter ones between them that turn the other way: a star whose points taper to nothing
    for (int i = 0; i < 8; ++i)
    {
        const bool major = (i & 1) == 0;
        const float angle = (major ? time * 0.035f : -time * 0.05f + PI * 0.25f) + static_cast<float>(i >> 1) * (PI * 0.5f); // (each of the four points has two spikes: i >> 1 is the whole number)
        const float reach = size * (major ? 1.15f : 0.62f) * pulse;
        const float root = size * (major ? 0.16f : 0.11f);
        const float ca = std::cos(angle), sa = std::sin(angle);
        const float tx = x + ca * reach, ty = y + sa * reach / ASPECT;
        const float lx = x - sa * root, ly = y + ca * root / ASPECT;
        const float rx = x + sa * root, ry = y - ca * root / ASPECT;
        const float alpha = (major ? 0.85f : 0.55f) * env;
        const Renderer2DVertex spike[3] = {{lx, ly, 1.0f, 1.0f, 1.0f, alpha}, {rx, ry, 1.0f, 1.0f, 1.0f, alpha}, {tx, ty, pal.aura.r, pal.aura.g, pal.aura.b, 0.0f}};
        Renderer2D_DrawPolygon(spike, 3);
    }
    // a ring that opens and is gone, again and again
    const float ring = std::fmod(time, 12.0f) / 12.0f;
    GameVfx::DrawRound(GameVfx::Sprite::Ring, x, y, size * (0.5f + 1.4f * (1.0f - (1.0f - ring) * (1.0f - ring))), pal.strandA.r, pal.strandA.g, pal.strandA.b, (1.0f - ring) * 0.55f * env);
    Renderer2D_SetBlendMode(previousBlend);
}

void DrawChargeUp(float x, float y, float radius, float progress, float time, const Palette& pal, int seed)
{
    const float p = Saturate(progress);
    if (p <= 0.01f) return;
    const Renderer2DBlendMode previousBlend = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    // motes fly in from the ring, over and over, faster as the charge fills: the light is being drawn into the muzzle
    constexpr int MOTES = 18;
    for (int i = 0; i < MOTES; ++i)
    {
        const float angle = Hash(seed + i * 3) * TAU;
        const float rate = 0.030f + 0.030f * p;
        const float m = std::fmod(time * rate + Hash(seed + i * 3 + 1), 1.0f);
        const float r = radius * std::pow(1.0f - m, 1.6f);
        const float k = std::sin(PI * m) * (0.35f + 0.65f * p);
        const float tail = 0.012f + 0.050f * m;
        const float px = x + std::cos(angle + m * 0.8f) * r, py = y + std::sin(angle + m * 0.8f) * r / ASPECT;
        const float qx = x + std::cos(angle + (m - 0.05f) * 0.8f) * (r + tail), qy = y + std::sin(angle + (m - 0.05f) * 0.8f) * (r + tail) / ASPECT;
        const Rgb c = Mix(WHITE, (i & 1) ? pal.strandA : pal.strandB, 0.5f);
        GameVfx::DrawBar(px, py, qx, qy, 0.0034f, c.r, c.g, c.b, 0.85f * k);
        GameVfx::DrawDiamond(px, py, 0.0050f, angle, 1.0f, 1.0f, 1.0f, 0.70f * k);
    }
    // a ring that closes, and a core that swells
    GameVfx::DrawRound(GameVfx::Sprite::Ring, x, y, radius * 2.0f * (1.0f - 0.75f * p), pal.aura.r, pal.aura.g, pal.aura.b, 0.18f + 0.45f * p);
    GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.05f + radius * 0.55f * p, pal.body.r, pal.body.g, pal.body.b, 0.20f + 0.55f * p * p);
    GameVfx::DrawRound(GameVfx::Sprite::Glow, x, y, 0.03f + radius * 0.20f * p, 1.0f, 1.0f, 1.0f, 0.25f + 0.70f * p * p);
    Renderer2D_SetBlendMode(previousBlend);
}

// ================================================================================================================================================================
// the band of fire
// ================================================================================================================================================================
// The zone is a rectangle (x0 .. x1, y +- halfY): the bullets that fly into it burn into energy. The fire is not: it streams. Wind blows from the right, so every flame
// leans to the left like the tail of a comet and the embers drift that way; a lick of fire runs along the band; a few flames are taller. A hot bed of coals glows along the
// middle line, and two faint dashed lines say where the zone ends (the flames never leave it by much). It is lit by the beam (white-hot for twelve steps), burns, and
// burns out from the end that is near the ship.
void DrawFlameBand(const FlameParams& band)
{
    const float length = band.x1 - band.x0;
    if (length < 0.05f || band.halfY <= 0.0f || band.lifetime <= 0) return;
    const float t = static_cast<float>(band.age);
    const float life = Saturate(static_cast<float>(band.lifetime - band.age) / static_cast<float>(band.lifetime));
    const float fade = Saturate(life * 5.0f);                       // the last fifth of its life: everything dims
    const float lit = Saturate(1.0f - t / 12.0f);                   // the first twelve steps: lit by the beam
    const float flicker = 0.80f + 0.20f * std::sin(t * 0.62f) * std::sin(t * 0.23f + 1.0f);
    const float halfE = band.halfY * ASPECT;                        // half height in equal-scale units
    auto burn = [&](float u) { return Saturate(life * 1.7f - (1.0f - u) * 0.7f); }; // u: 0 at the ship's end, 1 at the far end

    const Renderer2DBlendMode previousBlend = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);

    const Frame f = MakeFrame(band.x0, band.y, band.x1, band.y);
    const int n = std::max(8, std::min(MAX_SAMPLES - 1, static_cast<int>(length / 0.03f) + 2));

    // the heat: a soft ribbon over the whole zone whose edges breathe, and the bed of coals along the middle
    Sample heat[MAX_SAMPLES], coals[MAX_SAMPLES], white[MAX_SAMPLES];
    for (int i = 0; i < n; ++i)
    {
        const float s = length * static_cast<float>(i) / static_cast<float>(n - 1);
        const float u = s / length;
        const float b = burn(u) * fade;
        const float travel = 0.5f + 0.5f * std::sin(s * 38.0f + t * 0.55f);         // light that runs to the left along the coals
        heat[i] = {s, -0.30f * halfE, halfE * (1.25f + 0.12f * std::sin(s * 18.0f - t * 0.25f)), (0.20f * flicker + 0.10f * lit) * b};
        coals[i] = {s, 0.0f, 0.0042f + 0.0022f * travel, (0.40f + 0.30f * travel) * b};
        white[i] = {s, 0.0f, 0.0026f, 0.90f * lit * b};
    }
    Ribbon(f, heat, n, {1.00f, 0.42f, 0.08f}, 0.05f);
    Ribbon(f, coals, n, {1.00f, 0.80f, 0.42f}, 0.0f);
    Ribbon(f, white, n, {1.00f, 0.96f, 0.85f}, 0.0f);

    // the zone: two dashed lines that run to the left
    {
        constexpr float DASH = 0.07f;
        const float run = std::fmod(t * 0.0030f, DASH);
        for (int side = -1; side <= 1; side += 2)
            for (float d = -run; d < length; d += DASH)
            {
                const float a = std::max(0.0f, d), b = std::min(length, d + DASH * 0.55f);
                if (b <= a) continue;
                const float b01 = burn(0.5f * (a + b) / length) * fade;
                float ax, ay, bx, by;
                Place(f, a, static_cast<float>(side) * halfE, ax, ay);
                Place(f, b, static_cast<float>(side) * halfE, bx, by);
                GameVfx::DrawBar(ax, ay, bx, by, 0.0018f, 1.0f, 0.55f, 0.16f, 0.45f * b01);
            }
    }

    // the flames: each one is a ribbon along a curved centre line that is wide and hot at the root and thins to nothing at the tip: soft, with a colour that cools along it
    // (yellow, orange, dark red). Two layers, the hot heart inside the body. Two rows: the main fire rises from the lower edge, a shorter and sparser fringe hangs from the upper.
    const int tongues = std::min(48, std::max(5, static_cast<int>(length / 0.050f)));
    const float spacing = length / static_cast<float>(tongues);
    constexpr int FLAME_SAMPLES = 9;
    for (int i = 0; i < tongues; ++i)
    {
        const float h0 = Hash(9300 + i * 5), h1 = Hash(9301 + i * 5), h2 = Hash(9302 + i * 5), h3 = Hash(9303 + i * 5), h4 = Hash(9304 + i * 5);
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(tongues);
        const float b = burn(u) * fade;
        if (b <= 0.0f) continue;
        const float fx = band.x0 + spacing * (static_cast<float>(i) + 0.5f + (h0 - 0.5f) * 0.9f);
        const float lick = 0.5f + 0.5f * std::sin(u * 17.0f - t * 0.10f + h1 * 6.0f); // a lick of fire that runs along the band
        for (int row = 0; row < 2; ++row)
        {
            const bool mainRow = row == 0;
            if (!mainRow && h4 < 0.45f) continue; // (the fringe is sparse)
            const float phase = t * (mainRow ? 0.17f : 0.25f) + h1 * 40.0f + static_cast<float>(row) * 2.1f;
            const float sway = (0.45f + 0.55f * lick) * (0.72f + 0.28f * std::fabs(std::sin(phase))) * (0.85f + 0.15f * std::sin(t * 0.41f + h2 * 30.0f));
            const float tall = h3 > 0.80f ? 1.35f : (h3 < 0.25f ? 0.70f : 1.0f);
            const float height = (mainRow ? 2.50f : 1.00f) * halfE * sway * tall * b;       // equal-scale units
            const float width = (0.020f + 0.020f * h2) * (0.85f + 0.30f * sway);
            const float dirY = mainRow ? 1.0f : -1.0f;
            const Frame ff = MakeFrame(fx, band.y + (mainRow ? -band.halfY : band.halfY), fx, band.y + (mainRow ? 1.0f : -1.0f) * 10.0f); // (straight up / down)
            const float a = (mainRow ? 1.0f : 0.65f) * b;
            for (int layer = 0; layer < 2; ++layer)
            {
                const float k = layer == 0 ? 1.0f : 0.55f;       // the second, smaller one is the hot heart of the flame
                Sample sm[FLAME_SAMPLES];
                for (int j = 0; j < FLAME_SAMPLES; ++j)
                {
                    const float v = static_cast<float>(j) / static_cast<float>(FLAME_SAMPLES - 1); // 0 root .. 1 tip
                    // the centre line leans into the wind (to the left) and curls: an S that sways with the flame
                    const float curl = std::sin(phase * 0.9f + v * 3.4f + h0 * 6.0f) * 0.30f * v;
                    const float offset = ((0.30f + 0.20f * sway) * v * v + curl * 0.55f) * height * k * dirY;
                    sm[j] = {height * k * v, offset, width * k * std::pow(1.0f - v, 0.85f) + 0.0012f, (0.62f * a) * std::pow(1.0f - v, 1.15f)};
                }
                Ribbon(ff, sm, FLAME_SAMPLES, layer == 0 ? Rgb{1.0f, 0.46f, 0.10f} : Rgb{1.0f, 0.88f, 0.50f}, 0.0f);
            }
        }
    }

    // embers: chips of fire that lift off the band, drift to the left and go out
    const int embers = std::min(40, std::max(6, static_cast<int>(length / 0.03f)));
    for (int e = 0; e < embers; ++e)
    {
        const float h0 = Hash(9100 + e * 3), h1 = Hash(9101 + e * 3), h2 = Hash(9102 + e * 3);
        const float cycle = 42.0f + 46.0f * h1;
        const float ph = std::fmod(t + h2 * cycle, cycle) / cycle; // 0 born in the band ... 1 gone
        const float b = burn(h0) * fade;
        const float ex = band.x0 + length * h0 + 0.012f * std::sin(t * 0.09f + h2 * 20.0f) - 0.10f * ph;
        const float ey = band.y - band.halfY * 0.5f + ph * (2.0f * band.halfY + 0.12f);
        const float al = std::pow(1.0f - ph, 1.3f) * b;
        GameVfx::DrawDiamond(ex, ey, 0.0055f * (1.0f - 0.55f * ph) + 0.0014f, 0.7854f + ph * 3.0f, 1.0f, 0.66f - 0.34f * ph, 0.20f - 0.10f * ph, 0.85f * al);
    }

    // where the beam began: a bloom for twelve steps
    if (lit > 0.0f) GameVfx::DrawRound(GameVfx::Sprite::Glow, band.x0, band.y, 0.30f, 1.0f, 0.62f, 0.22f, 0.45f * lit * fade);

    Renderer2D_SetBlendMode(previousBlend);
}
} // namespace BeamFx
