#include "game/GameBackground.h"
#include "boss/GameBoss.h"
#include "core/AssetManager.h"
#include "core/GameTimer.h"
#include "game/GalaxyStars.h"
#include "game/GameSave.h"
#include "core/Renderer2D.h"
#include "game/GameState.h"
#include "game/GameVfx.h"
#include "ui/GameMenuUi.h"
#include "ui/UiKit.h"
#include <algorithm>
#include <cmath>

using GameVfx::Sprite;

namespace
{
int g_StarFrame = 0;      // never reset: the stars do not jump when a run starts
float g_TitleLook = 0.0f; // 0..1: the in-game layers (clouds, light shafts, streaks) while the title hands over to the game

constexpr float PI = 3.1415926535f;
constexpr float CUT_ANGLE = 0.60f; // the angle of the red / blue cut of the menus: the light shafts of the game run along it

float Wrap(float value, float minimum, float range)
{
    value = std::fmod(value - minimum, range);
    if (value < 0.0f)
    {
        value += range;
    }
    return value + minimum;
}

// A rectangle with one colour at the top and one at the bottom (per-vertex colours: smooth, no steps).
void VerticalGradient(float left, float top, float right, float bottom, const float c0[4], const float c1[4])
{
    const Renderer2DVertex v[4] = {{left, top, c0[0], c0[1], c0[2], c0[3]},
                                   {right, top, c0[0], c0[1], c0[2], c0[3]},
                                   {right, bottom, c1[0], c1[1], c1[2], c1[3]},
                                   {left, bottom, c1[0], c1[1], c1[2], c1[3]}};
    Renderer2D_DrawPolygon(v, 4);
}

// A black band that fades out: the edge of the screen falls into the dark. `edge` is the side of the band that is dark.
void EdgeFade(float left, float top, float right, float bottom, int edge, float alpha)
{
    // edge: 0 top, 1 bottom, 2 left, 3 right
    float aTL = 0.0f, aTR = 0.0f, aBR = 0.0f, aBL = 0.0f;
    switch (edge)
    {
    case 0:
        aTL = aTR = alpha;
        break;
    case 1:
        aBL = aBR = alpha;
        break;
    case 2:
        aTL = aBL = alpha;
        break;
    default:
        aTR = aBR = alpha;
        break;
    }
    const Renderer2DVertex v[4] = {{left, top, 0.0f, 0.0f, 0.0f, aTL},
                                   {right, top, 0.0f, 0.0f, 0.0f, aTR},
                                   {right, bottom, 0.0f, 0.0f, 0.0f, aBR},
                                   {left, bottom, 0.0f, 0.0f, 0.0f, aBL}};
    Renderer2D_DrawPolygon(v, 4);
}

// Stars: crisp - a hard diamond, no soft halo (the halos were what made the sky look milky); the near layer gets a four-point cross that glints now and
// then. The colour temperature varies from star to star. `drift`: 1 in the game, less on the title, where the sky should hardly move.
void DrawStars(float frame, int seedBase, int count, float speed, float size, float brightness, bool flare, float drift)
{
    const float time = frame;
    for (int i = 0; i < count; i++)
    {
        const float hx = GameVfx::Hash(seedBase + i * 3);
        const float hy = GameVfx::Hash(seedBase + i * 3 + 1);
        const float hs = GameVfx::Hash(seedBase + i * 3 + 2);
        const float x = Wrap(hx * 2.4f - 1.2f - time * speed * drift, -1.2f, 2.4f);
        const float y = hy * 2.0f - 1.0f;
        const float twinkle = 0.72f + 0.28f * std::sin(time * (0.03f + hs * 0.05f) + hs * 40.0f);
        const float starSize = size * (0.6f + hs * 0.8f);
        const float warm = hs; // 0 = pale blue, 1 = pale warm
        const float r = 0.78f + 0.22f * warm;
        const float g = 0.84f + 0.08f * warm;
        const float b = 1.00f - 0.16f * warm;
        if (flare && hs > 0.62f)
        {
            // a bright star: a hard four-point star (a cross of thin bars and a small diamond), its glint flares now and then
            const float glint = std::pow((std::max)(0.0f, std::sin(time * (0.011f + hs * 0.01f) + hs * 31.0f)), 24.0f);
            const float arm = starSize * (0.8f + 0.6f * hs) * (1.0f + 1.6f * glint);
            const float a = brightness * (0.40f + 0.50f * glint) * twinkle;
            GameVfx::DrawBar(x - arm, y, x + arm, y, 0.0012f, r, g, b, a);
            GameVfx::DrawBar(x, y - arm / GameVfx::ASPECT, x, y + arm / GameVfx::ASPECT, 0.0012f, r, g, b, a);
            GameVfx::DrawDiamond(x, y, starSize * 0.22f, 0.0f, 1.0f, 1.0f, 1.0f, brightness * twinkle);
        }
        else
        {
            GameVfx::DrawDiamond(x, y, starSize * 0.17f, 0.7854f, 0.5f + 0.5f * r, 0.5f + 0.5f * g, 0.5f + 0.5f * b, brightness * 0.95f * twinkle);
        }
    }
}

// THE MILKY WAY. Not painted: tools/make_galaxy.py SIMULATES a spiral galaxy (disk, four arms, bulge, dust, nebulae), ray-marches what an observer inside it sees (an
// all-round panorama: 360 degrees of longitude, +-40 of latitude, the left and the right edge are the same place) and then draws it in the style of this game: THREE FLAT
// TONES with hard edges, the dark lanes of the dust cut out of them like blades, and a hairline along the outer edge (the ink of the HUD). The stars are not in the picture:
// the brightest 2600 of the simulation are in GalaxyStars.h and drawn here as hard diamonds (the big ones with a cross), so they are sharp at any resolution and sit
// exactly in the structure of the band. The farthest layer of the sky, tilted across the red / blue cut, drifting very slowly (slower than any star), far below a bullet.
// The picture is a full circle, so two tiles side by side never show a seam, and the drift can go on for ever.
constexpr float GALAXY_ANGLE = -0.42f;          // radians: the band falls to the right, ACROSS the red / blue cut of the menus (the axis of the picture)
constexpr float GALAXY_DEG_PER_SCREEN = 140.0f; // how many degrees of the panorama the 1280 px of the screen show
constexpr float GALAXY_PPD = 20.0f;             // pixels per degree of the texture
constexpr float GALAXY_W = 7200.0f, GALAXY_H = 1600.0f;
constexpr float GALAXY_LAT = 40.0f;             // degrees of latitude from the middle of the picture to its edge
constexpr float GALAXY_DRIFT = 0.020f;          // virtual px per step along the band (a star of the far field moves about 4 px per second, this one 1.2)
// How loud the Milky Way may be. The order of what the player has to read is: enemy bullet > the hit point of the ship > the ship > an enemy > the background, and a
// bullet is a saturated, bright thing. So the sky stays far under it: the three tones of the band add 5, 11 and 17 of 255 (the hairline 13), the stars are a third of the
// brightness of a bullet at most, and only a part of the 2600 stars of the simulation is drawn (every 4th of the faint ones, every 2nd of the middle ones, all the big ones).
constexpr float GALAXY_TEXTURE_GAIN = 0.13f;    // the tones of the picture are baked 7.7 times as bright as they are drawn (8 bit): this is the factor back (the tones: 40, 84, 132 of 255)
constexpr float GALAXY_STRENGTH = 1.0f;         // the tones: the light the band adds to the sky (the polarity field and the clouds come on top)
constexpr float GALAXY_STAR_STRENGTH = 0.55f;   // the stars of the band
constexpr int GALAXY_STAR_STEP[3] = {4, 2, 1};  // of the stars of a tier (small, middle, big) every n-th is drawn
constexpr float GALAXY_START_DEG = 18.0f;       // at the start of a run the bulge and the great rift are just left of the middle

void DrawGalaxy(float frame, float strength)
{
    // the BACKGROUND setting (settings > GAME) is how bright the sky is: it scales the Milky Way too (40 %, the default, is 1; 0 % is a fifth)
    const float userScale = (std::min)(1.5f, 0.20f + 0.80f * static_cast<float>(GameSave::GetRecordData().backgroundPercent) / 40.0f);
    strength *= userScale;
    if (strength <= 0.01f) return;
    const int texture = AssetManager::Get(AssetManager::Tex::Galaxy);
    if (texture < 0) return;
    const float pxPerTexel = 1280.0f / (GALAXY_DEG_PER_SCREEN * GALAXY_PPD); // virtual px of the screen per pixel of the texture
    const float tileLength = GALAXY_W * pxPerTexel;                          // one full circle, along the band
    const float tileHeight = GALAXY_H * pxPerTexel;
    const float scroll = GALAXY_START_DEG * GALAXY_PPD * pxPerTexel + frame * GALAXY_DRIFT;
    const float ca = std::cos(GALAXY_ANGLE), sa = std::sin(GALAXY_ANGLE);
    const float across = 20.0f; // virtual px: the band passes a little above the middle of the screen (y up)
    const float k0 = std::floor(scroll / tileLength);
    for (int k = -1; k <= 2; ++k)
    {
        const float along = (k0 + static_cast<float>(k)) * tileLength - scroll; // the centre of tile k along the band, from the middle of the screen
        if (std::fabs(along) > tileLength * 0.5f + 760.0f) continue;           // (760 = the half diagonal of the screen: this tile is not on the screen)
        const float cx = along * ca - across * sa;
        const float cy = along * sa + across * ca;
        Renderer2D_DrawTextureEx(texture, cx / 640.0f, cy / 360.0f, tileLength / 640.0f, tileHeight / 360.0f, GALAXY_ANGLE, 1.0f, 1.0f, 1.0f,
                                 strength * GALAXY_STRENGTH * GALAXY_TEXTURE_GAIN, 0.0f, 0.0f, 1.0f, 1.0f);

        // the stars of this tile: the same transform as the picture (the middle of the tile, rotated by the angle of the band)
        const float starAlpha = strength * GALAXY_STAR_STRENGTH;
        for (int si = 0; si < GalaxyStars::COUNT; ++si)
        {
            const GalaxyStars::Star& star = GalaxyStars::STARS[si];
            if (si % GALAXY_STAR_STEP[star.tier < 3 ? star.tier : 0] != 0) continue;
            const float lx = ((star.lon + 180.0f) * GALAXY_PPD - GALAXY_W * 0.5f) * pxPerTexel;
            const float ly = -((GALAXY_LAT - star.lat) * GALAXY_PPD - GALAXY_H * 0.5f) * pxPerTexel; // (the texture runs down, the screen up)
            const float X = cx + lx * ca - ly * sa;
            const float Y = cy + lx * sa + ly * ca;
            if (std::fabs(X) > 660.0f || std::fabs(Y) > 380.0f) continue;
            static const float TINT[3][3] = {{0.92f, 0.93f, 0.96f}, {0.62f, 0.78f, 1.00f}, {1.00f, 0.66f, 0.58f}}; // ivory, cool (young stars), the red of the game (old giants)
            const float* c = TINT[star.tint < 3 ? star.tint : 0];
            const float nx = X / 640.0f, ny = Y / 360.0f;
            if (star.tier == 0)
            {
                GameVfx::DrawDiamond(nx, ny, 0.8f / 640.0f, 0.7854f, c[0], c[1], c[2], 0.50f * starAlpha);
            }
            else if (star.tier == 1)
            {
                GameVfx::DrawDiamond(nx, ny, 1.2f / 640.0f, 0.7854f, c[0], c[1], c[2], 0.70f * starAlpha);
            }
            else
            {
                const float glint = 0.80f + 0.20f * std::sin(frame * 0.02f + star.lon * 3.1f);
                const float arm = 8.0f / 640.0f;
                GameVfx::DrawBar(nx - arm, ny, nx + arm, ny, 0.0012f, c[0], c[1], c[2], 0.22f * starAlpha * glint);
                GameVfx::DrawBar(nx, ny - arm / GameVfx::ASPECT, nx, ny + arm / GameVfx::ASPECT, 0.0012f, c[0], c[1], c[2], 0.22f * starAlpha * glint);
                GameVfx::DrawDiamond(nx, ny, 1.7f / 640.0f, 0.7854f, 1.0f, 1.0f, 1.0f, 0.80f * starAlpha * glint);
            }
        }
    }
}

void DrawDust(float frame)
{
    const float time = frame;
    for (int i = 0; i < 6; i++)
    {
        const float hx = GameVfx::Hash(900 + i * 2);
        const float hy = GameVfx::Hash(901 + i * 2);
        const float x = Wrap(hx * 2.8f - 1.4f - time * 0.020f, -1.4f, 2.8f);
        const float y = hy * 1.8f - 0.9f;
        GameVfx::Draw(Sprite::Spark, x, y, 0.22f, 0.010f, 3.1415926f, 0.90f, 0.92f, 1.00f, 0.07f);
    }
}

// Far gas clouds: very large, very faint and slow. Neutral slate colours (red and blue belong to the polarity).
struct Cloud
{
    float x, y, size, speed;
    float r, g, b, a;
};

constexpr Cloud CLOUDS[5] = {{-0.35f, 0.42f, 1.50f, 0.000060f, 0.34f, 0.42f, 0.62f, 0.055f},
                             {0.60f, -0.38f, 1.70f, 0.000040f, 0.42f, 0.36f, 0.54f, 0.042f},
                             {0.05f, -0.02f, 1.20f, 0.000090f, 0.30f, 0.38f, 0.50f, 0.034f},
                             {-0.80f, -0.55f, 1.30f, 0.000050f, 0.36f, 0.40f, 0.56f, 0.036f},
                             {0.90f, 0.55f, 1.10f, 0.000070f, 0.32f, 0.36f, 0.50f, 0.030f}};

void DrawClouds(float frame, float look)
{
    const float time = frame;
    for (const Cloud& c : CLOUDS)
    {
        const float x = Wrap(c.x - time * c.speed, -1.8f, 3.6f);
        const float y = c.y + 0.03f * std::sin(time * 0.002f + c.x * 5.0f);
        GameVfx::DrawRound(Sprite::Glow, x, y, c.size, c.r, c.g, c.b, c.a * look);
    }
}

// Long soft bands of light along the 34 degrees of the cut, drifting very slowly: the same angle as the menus, but only a texture here.
void DrawShaft(float offsetPx, float halfWidthPx, float alpha, float time, float speed)
{
    const float nx = -std::sin(CUT_ANGLE);
    const float ny = -std::cos(CUT_ANGLE);
    const float dx = std::cos(CUT_ANGLE);
    const float dy = -std::sin(CUT_ANGLE);
    const float off = Wrap(offsetPx + time * speed, -1000.0f, 2000.0f);
    const float length = 1900.0f;
    float px[3][2][2];
    const float o[3] = {off - halfWidthPx, off, off + halfWidthPx};
    for (int k = 0; k < 3; ++k)
    {
        const float cx = 640.0f + nx * o[k];
        const float cy = 360.0f + ny * o[k];
        px[k][0][0] = cx - dx * length;
        px[k][0][1] = cy - dy * length;
        px[k][1][0] = cx + dx * length;
        px[k][1][1] = cy + dy * length;
    }
    const float a[3] = {0.0f, alpha, 0.0f};
    for (int k = 0; k < 2; ++k)
    {
        const Renderer2DVertex v[4] = {{UiKit::NX(px[k][0][0]), UiKit::NY(px[k][0][1]), 0.62f, 0.72f, 0.95f, a[k]},
                                       {UiKit::NX(px[k][1][0]), UiKit::NY(px[k][1][1]), 0.62f, 0.72f, 0.95f, a[k]},
                                       {UiKit::NX(px[k + 1][1][0]), UiKit::NY(px[k + 1][1][1]), 0.62f, 0.72f, 0.95f, a[k + 1]},
                                       {UiKit::NX(px[k + 1][0][0]), UiKit::NY(px[k + 1][0][1]), 0.62f, 0.72f, 0.95f, a[k + 1]}};
        Renderer2D_DrawPolygon(v, 4);
    }
}

// THE SEAM in the far sky: a red hairline and a blue hairline, a few px apart, along the angle of the menus' cut, drifting very slowly;
// diamond nodes (half red / half blue) travel along it. Far and faint: it is the identity of the game, not something to look at.
void DrawFarSeam(float time, float look)
{
    const float dx = std::cos(CUT_ANGLE), dy = -std::sin(CUT_ANGLE);
    const float nx = -std::sin(CUT_ANGLE), ny = -std::cos(CUT_ANGLE);
    const float off = Wrap(-300.0f + time * 0.035f, -900.0f, 1800.0f);
    const float cx = 640.0f + nx * off, cy = 360.0f + ny * off;
    const float L = 1500.0f;
    const float a = 0.10f * look;
    for (int k = 0; k < 2; ++k)
    {
        const float s = k == 0 ? -3.0f : 3.0f;
        const float r = k == 0 ? 1.0f : 0.20f, g = k == 0 ? 0.22f : 0.55f, b = k == 0 ? 0.18f : 1.0f;
        const float x0 = cx - dx * L + nx * s, y0 = cy - dy * L + ny * s, x1 = cx + dx * L + nx * s, y1 = cy + dy * L + ny * s;
        const float h = 0.8f;
        const Renderer2DVertex v[4] = {{UiKit::NX(x0 + nx * h), UiKit::NY(y0 + ny * h), r, g, b, a}, {UiKit::NX(x1 + nx * h), UiKit::NY(y1 + ny * h), r, g, b, a},
                                       {UiKit::NX(x1 - nx * h), UiKit::NY(y1 - ny * h), r, g, b, a}, {UiKit::NX(x0 - nx * h), UiKit::NY(y0 - ny * h), r, g, b, a}};
        Renderer2D_DrawPolygon(v, 4);
    }
    // (no travelling diamonds on the line any more)
}

// Far wreckage: a few dark hard-edged plates drifting slowly far behind (parallax), lit on one edge - the remains of the fleet
void DrawWreckage(float time, float look)
{
    for (int i = 0; i < 7; ++i)
    {
        const float h1 = GameVfx::Hash(2100 + i * 5), h2 = GameVfx::Hash(2101 + i * 5), h3 = GameVfx::Hash(2102 + i * 5);
        const float x = Wrap(h1 * 3.0f - 1.5f - time * (0.00045f + 0.0006f * h3), -1.5f, 3.0f);
        const float y = h2 * 1.6f - 0.8f;
        const float r = 0.025f + 0.045f * h3;
        const float rot = time * (0.0008f + 0.0012f * h1) * (i & 1 ? 1.0f : -1.0f) + h2 * 6.0f;
        const float x0 = x + std::cos(rot) * r, y0 = y + std::sin(rot) * r / GameVfx::ASPECT;
        const float x1 = x + std::cos(rot + 2.3f) * r * 0.7f, y1 = y + std::sin(rot + 2.3f) * r * 0.7f / GameVfx::ASPECT;
        const float x2 = x + std::cos(rot - 2.0f) * r * 0.8f, y2 = y + std::sin(rot - 2.0f) * r * 0.8f / GameVfx::ASPECT;
        GameVfx::DrawTri(x0, y0, x1, y1, x2, y2, 0.030f, 0.034f, 0.046f, 0.85f * look, false);
        GameVfx::DrawBar(x0, y0, x1, y1, 0.0016f, 0.55f, 0.62f, 0.78f, 0.22f * look);
    }
}

// Speed streaks: a bright head and a tail that fades out (per-vertex alpha), very faint.
void DrawStreaks(float time, float look)
{
    for (int i = 0; i < 10; i++)
    {
        const float h1 = GameVfx::Hash(1300 + i * 3);
        const float h2 = GameVfx::Hash(1301 + i * 3);
        const float h3 = GameVfx::Hash(1302 + i * 3);
        const float x = Wrap(h1 * 2.8f - 1.4f - time * (0.012f + 0.016f * h3), -1.4f, 2.8f);
        const float y = h2 * 1.7f - 0.85f;
        const float length = 0.22f + 0.34f * h3;
        const float a = (0.05f + 0.07f * h3) * look;
        const float t = 0.0014f; // half the thickness, normalized y
        const Renderer2DVertex v[4] = {{x, y + t, 0.80f, 0.88f, 1.0f, a},
                                       {x + length, y + t, 0.80f, 0.88f, 1.0f, 0.0f},
                                       {x + length, y - t, 0.80f, 0.88f, 1.0f, 0.0f},
                                       {x, y - t, 0.80f, 0.88f, 1.0f, a}};
        Renderer2D_DrawPolygon(v, 4);
    }
}
}

void GameBackground::Draw(float frame, bool bossWarning, bool bossBattle, int bossPhase)
{
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    const float time = frame;
    const bool inGame = GameState::scene == GameState::Scene::Game;
    const float look = inGame ? 1.0f : g_TitleLook;
    // The layers of the fight (clouds, light shafts, the far seam, streaks) were too bright to play on: they are dimmed as a whole. (The title hands over with the same factor,
    // so nothing jumps when a run starts.)
    constexpr float FIGHT_LAYER_DIM = 0.55f;
    const float layer = look * FIGHT_LAYER_DIM;

    // The world is graphite and white. Colour is reserved for polarity (red / blue) and for the boss warning.
    // A slightly lighter top and a darker bottom: the sky has a direction.
    float top[4] = {0.013f, 0.015f, 0.023f, 1.0f};
    float bottom[4] = {0.006f, 0.007f, 0.011f, 1.0f};
    // the menus' sky is a little lighter and bluer than the sky of a fight (the left of the title is the first thing the player sees): it fades back to the dark one
    // while the game starts (g_TitleLook goes 0 -> 1 then), so there is no jump
    const float lift = inGame ? 0.0f : 1.0f - g_TitleLook;
    if (lift > 0.0f)
    {
        top[0] += 0.007f * lift;
        top[1] += 0.012f * lift;
        top[2] += 0.030f * lift;
        bottom[0] += 0.004f * lift;
        bottom[1] += 0.007f * lift;
        bottom[2] += 0.016f * lift;
    }
    if (bossWarning)
    {
        const float pulse = 0.5f + 0.5f * std::sin(time * 0.25f);
        top[0] = bottom[0] = 0.024f + 0.026f * pulse;
        top[1] = bottom[1] = 0.004f;
        top[2] = bottom[2] = 0.007f;
        bottom[0] *= 0.6f;
    }
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    VerticalGradient(-1.0f, 1.0f, 1.0f, -1.0f, top, bottom);

    Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
    if (look > 0.01f)
    {
        DrawClouds(frame, layer);
        DrawShaft(-520.0f, 70.0f, 0.028f * layer, time, 0.10f);
        DrawShaft(-160.0f, 120.0f, 0.020f * layer, time, 0.07f);
        DrawShaft(210.0f, 55.0f, 0.034f * layer, time, 0.13f);
        DrawShaft(560.0f, 90.0f, 0.017f * layer, time, 0.05f);
    }
    // the Milky Way (the farthest layer), then three layers of stars (parallax): the nearest bright ones as hard four-point stars
    const float drift = inGame ? 1.0f : 0.30f; // (on the title the sky hardly moves)
    DrawGalaxy(frame, bossWarning ? 0.35f : 1.0f); // (the title and the other pages of the menu have it too, as quiet as in a fight: the sky is one sky)
    DrawStars(frame, 1100, 40, 0.00012f, 0.006f, 0.26f, false, drift); // the far field: a few tiny points (the galaxy has its own stars now)
    DrawStars(frame, 100, 70, 0.00030f, 0.010f, 0.34f, false, drift);
    DrawStars(frame, 400, 28, 0.00090f, 0.016f, 0.55f, false, drift);
    DrawStars(frame, 700, 10, 0.00260f, 0.024f, 0.80f, true, drift);
    if (inGame) DrawDust(frame); // (the streaks of dust that crossed the whole title in a second are only for the game)
    if (look > 0.01f)
    {
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
        DrawWreckage(time, look);
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
        if (!bossBattle) DrawFarSeam(time, layer);
    }

    // The boss: phase 1 the sky is cut by a faint seam line through the middle; phase 2 the two polarities divide the sky (red above,
    // blue below) with the seam between them; phase 3 the sky glows hot from below and embers rise.
    if (bossBattle && !bossWarning)
    {
        const float pulse = 0.5f + 0.5f * std::sin(time * 0.08f);
        if (bossPhase == 2)
        {
            const Renderer2DVertex up[4] = {{-1.0f, 1.0f, 1.0f, 0.16f, 0.12f, 0.010f}, {1.0f, 1.0f, 1.0f, 0.16f, 0.12f, 0.010f},
                                            {1.0f, 0.0f, 1.0f, 0.16f, 0.12f, 0.045f}, {-1.0f, 0.0f, 1.0f, 0.16f, 0.12f, 0.045f}};
            const Renderer2DVertex dn[4] = {{-1.0f, 0.0f, 0.12f, 0.45f, 1.0f, 0.045f}, {1.0f, 0.0f, 0.12f, 0.45f, 1.0f, 0.045f},
                                            {1.0f, -1.0f, 0.12f, 0.45f, 1.0f, 0.010f}, {-1.0f, -1.0f, 0.12f, 0.45f, 1.0f, 0.010f}};
            Renderer2D_DrawPolygon(up, 4);
            Renderer2D_DrawPolygon(dn, 4);
        }
        if (bossPhase <= 2) // the seam through the middle of the sky
        {
            Renderer2D_DrawQuad(-1.0f, 0.0060f, 1.0f, 0.0040f, 1.0f, 0.22f, 0.18f, 0.10f + 0.04f * pulse);
            Renderer2D_DrawQuad(-1.0f, -0.0040f, 1.0f, -0.0060f, 0.20f, 0.55f, 1.0f, 0.10f + 0.04f * pulse);
            for (int i = 0; i < 6; ++i)
            {
                const float x = Wrap(static_cast<float>(i) * 0.40f - time * 0.0016f, -1.1f, 2.2f);
                GameVfx::DrawDiamond(x, 0.0f, 0.007f, 0.0f, 0.85f, 0.85f, 0.90f, 0.22f);
            }
        }
        else // phase 3: heat from below, rising embers
        {
            const Renderer2DVertex heat[4] = {{-1.0f, -0.2f, 1.0f, 0.45f, 0.10f, 0.0f}, {1.0f, -0.2f, 1.0f, 0.45f, 0.10f, 0.0f},
                                              {1.0f, -1.0f, 1.0f, 0.45f, 0.10f, 0.05f + 0.03f * pulse}, {-1.0f, -1.0f, 1.0f, 0.45f, 0.10f, 0.05f + 0.03f * pulse}};
            Renderer2D_DrawPolygon(heat, 4);
            for (int i = 0; i < 16; ++i)
            {
                const float h1 = GameVfx::Hash(3100 + i * 3), h2 = GameVfx::Hash(3101 + i * 3);
                const float k = Wrap(time * (0.0025f + 0.0025f * h2) + h1, 0.0f, 1.0f);
                const float x = h1 * 2.2f - 1.1f + 0.03f * std::sin(time * 0.03f + h2 * 9.0f);
                const float y = -1.0f + 2.0f * k;
                GameVfx::DrawDiamond(x, y, 0.004f + 0.003f * h2, time * 0.05f, 1.0f, 0.55f, 0.18f, 0.30f * (1.0f - k));
            }
        }
    }

    // in the game: very faint speed streaks, and the red / blue diagonal of the menus as a quiet field. The field does NOT follow the
    // colour of the ship: nothing in the background changes when the player switches (it would pull the eyes away from the bullets).
    if (look > 0.01f)
    {
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
        DrawStreaks(time, layer);
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    }
    if (inGame)
    {
        GameMenuUi::DrawBackdrop(0.085f); // (while the title hands over, the title draws the same field itself, see TitleScene::Draw)
    }

    // vignette: the edges fall into the dark (smooth gradients)
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    EdgeFade(-1.0f, 1.0f, 1.0f, 0.78f, 0, 0.58f);
    EdgeFade(-1.0f, -0.78f, 1.0f, -1.0f, 1, 0.58f);
    EdgeFade(-1.0f, 1.0f, -0.80f, -1.0f, 2, 0.52f);
    EdgeFade(0.80f, 1.0f, 1.0f, -1.0f, 3, 0.52f);
    Renderer2D_SetBlendMode(previous);
}

namespace
{
int g_FrameCounter = 0;
}

void GameBackground::DrawCurrent()
{
    // the counter has already counted the last step: the picture is between (counter - 1) and (counter)
    Draw(static_cast<float>(g_StarFrame - 1) + GameTimer::BackgroundAlpha(), GameBoss::IsWarning(), GameBoss::IsBattleStarted(), GameBoss::GetPhase());
}

void GameBackground::Reset()
{
    g_FrameCounter = 0;
}

void GameBackground::SetTitleLook(float look)
{
    g_TitleLook = look < 0.0f ? 0.0f : (look > 1.0f ? 1.0f : look);
}

void GameBackground::Update()
{
    g_FrameCounter++;
    g_StarFrame++;
    GameTimer::SetBackgroundAdvanced(true);
    // (the backdrop of the game does not follow the colour of the ship any more, see Draw)
    GameMenuUi::UpdateBackdrop();
}

int GameBackground::GetFrame()
{
    return g_FrameCounter;
}








