#include "ui/CutIn.h"
#include "core/AssetManager.h"
#include "core/GameAudio.h"
#include "core/Loc.h"
#include "core/PostFx.h"
#include "core/Renderer2D.h"
#include "core/TextRenderer.h"
#include "ui/UiKit.h"
#include "ui/GameMenuUi.h"
#include "ui/Wordmark.h"

#include <algorithm>
#include <cmath>

// All in the virtual 1280 x 720 space of UiKit (y down).
namespace
{
struct Rgb
{
    float r, g, b;
};

const Rgb ACCENT[5] = {{1.00f, 0.18f, 0.16f}, {0.22f, 0.66f, 1.00f}, {0.92f, 0.95f, 1.00f}, {1.00f, 0.80f, 0.25f}, {1.00f, 0.46f, 0.10f}};

CutIn::Kind g_Kind = CutIn::Kind::Wave;
std::string g_Title;
std::string g_Sub;
int g_Accent = 0;
int g_T = 0;        // steps since the trigger
int g_Length = 0;   // 0 = nothing is showing
bool g_Held = false;
bool g_Docked = false; // the WAVE CLEAR card has docked as the header of the module pick (the pick shows its header at once)
constexpr float DOCK_FRAMES = 20.0f;
// the next card, waiting for the one on screen to leave
bool g_HasNext = false;
CutIn::Kind g_NextKind = CutIn::Kind::Wave;
std::string g_NextTitle, g_NextSub;
int g_NextAccent = 0;

int LengthOf(CutIn::Kind kind)
{
    switch (kind)
    {
    case CutIn::Kind::Wave: return 96;
    case CutIn::Kind::Clear: return 60; // (a small tag that is gone before the module pick opens at GameWave clear 90 - CHOICE_OPEN_AT_TIMER 14 = 76 steps: the pick brings its own header)
    case CutIn::Kind::Warning: return 150;
    case CutIn::Kind::Phase: return 112;
    case CutIn::Kind::Break: return 40; // (a short tag, the fight goes on)
    }
    return 96;
}
int OutFrames(CutIn::Kind kind)
{
    return kind == CutIn::Kind::Break ? 10 : 14;
}

void Start(CutIn::Kind kind, const std::string& title, const std::string& subtitle, int accent)
{
    g_Docked = false;
    g_Kind = kind;
    g_Title = title;
    g_Sub = subtitle;
    g_Accent = accent < 0 || accent > 4 ? 0 : accent;
    g_T = 0;
    g_Length = LengthOf(kind);
    if (kind != CutIn::Kind::Clear) PostFx::Kick(0.008f); // (WAVE CLEAR is quiet: no colour fringe, nothing hits the screen)
    if (kind == CutIn::Kind::Wave) GameAudio::Play(GameSound::CutInWave); // (the blue card of a new wave: a sweep of air, a glass note; the other cards have their sounds already)
}

float SKEW = -0.10f; // the slant of the bands: y moves this much per px of x (rising to the right); the WAVE CLEAR card flattens it as it docks
constexpr float CX = 640.0f;

float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
float Hash01(int seed)
{
    unsigned int x = static_cast<unsigned int>(seed) * 374761393u + 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u;
    x ^= x >> 16;
    return static_cast<float>(x & 0xffffu) / 65535.0f;
}

float EaseOut(float t)
{
    t = Clamp01(t);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}
float EaseInOut(float t)
{
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}
float EaseIn(float t)
{
    t = Clamp01(t);
    return t * t * t;
}

Renderer2DVertex V(float x, float y, const Rgb& c, float a)
{
    return Renderer2DVertex{UiKit::NX(x), UiKit::NY(y), c.r, c.g, c.b, a};
}

void Quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, const Rgb& c, float a)
{
    if (a <= 0.0f) return;
    const Renderer2DVertex v[4] = {V(x0, y0, c, a), V(x1, y1, c, a), V(x2, y2, c, a), V(x3, y3, c, a)};
    Renderer2D_DrawPolygon(v, 4);
}

void Tri(float x0, float y0, float x1, float y1, float x2, float y2, const Rgb& c, float a)
{
    if (a <= 0.0f) return;
    const Renderer2DVertex v[3] = {V(x0, y0, c, a), V(x1, y1, c, a), V(x2, y2, c, a)};
    Renderer2D_DrawPolygon(v, 3);
}

// a slanted band between xl and xr: its middle line is at `cy` at the screen centre, `h` tall
void Band(float xl, float xr, float cy, float h, const Rgb& c, float a)
{
    const float yl = cy + SKEW * (xl - CX), yr = cy + SKEW * (xr - CX);
    Quad(xl, yl - h * 0.5f, xr, yr - h * 0.5f, xr, yr + h * 0.5f, xl, yl + h * 0.5f, c, a);
}

// heavy text with a hard offset shadow in the accent colour (P5)
void Title(const std::string& text, float x, float y, float px, const Rgb& face, const Rgb& shadow, float alpha, float shadowDx)
{
    auto queue = [&](float dx, float dy, const Rgb& c, float a)
    {
        TextStyle style{};
        style.size = px / 2160.0f;
        style.red = c.r;
        style.green = c.g;
        style.blue = c.b;
        style.alpha = a;
        style.bold = true;
        style.shadow = false;
        style.alignment = TextAlignment::Center;
        const float box = 1200.0f;
        style.layoutWidth = box / (UiKit::VIRTUAL_W * 0.5f);
        const float top = (y + dy) - px * 0.62f;
        const float ny = 1.0f - 2.0f * (top + 0.18f * px) / UiKit::VIRTUAL_H;
        TextRenderer::QueueText(text, UiKit::NX(x + dx - box * 0.5f), ny, style);
    };
    if (shadowDx != 0.0f) queue(shadowDx, shadowDx * 0.8f, shadow, alpha);
    queue(0.0f, 0.0f, face, alpha);
}

// P5: halftone dots in a slanted strip, big on the right, small on the left
void Halftone(float cy, const Rgb& c, float alpha, float reveal)
{
    const float step = 18.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float x = 1290.0f - step * static_cast<float>(i);
        if (x < 1290.0f - 720.0f * reveal) break;
        const float k = 1.0f - static_cast<float>(i) / 40.0f; // size falls off to the left
        for (int j = -2; j <= 2; ++j)
        {
            const float y = cy + SKEW * (x - CX) + static_cast<float>(j) * step + ((i & 1) ? step * 0.5f : 0.0f);
            const float r = 6.0f * k * (1.0f - std::fabs(static_cast<float>(j)) / 3.0f);
            if (r < 0.8f) continue;
            Quad(x, y - r, x + r, y, x, y + r, x - r, y, c, alpha); // a diamond dot
        }
    }
}

// P5: the lower edge of the accent band as a row of teeth
void Teeth(float xl, float xr, float cy, float h, const Rgb& c, float a, int seed)
{
    const float w = 34.0f;
    int n = 0;
    for (float x = xl; x < xr; x += w, ++n)
    {
        const float depth = 10.0f + 12.0f * static_cast<float>((n * 7 + seed) % 3);
        const float y0 = cy + SKEW * (x - CX) + h * 0.5f;
        const float y1 = cy + SKEW * (x + w - CX) + h * 0.5f;
        Tri(x, y0, x + w, y1, x + w * 0.70f, (y0 + y1) * 0.5f + depth, c, a);
    }
}

}

void CutIn::Trigger(Kind kind, const std::string& title, const std::string& subtitle, int accent)
{
    if (g_Length <= 0 || kind == Kind::Break)
    {
        g_HasNext = false;
        Start(kind, title, subtitle, accent);
        return;
    }
    // a card is showing: it starts to leave now (if it is not leaving already), the new one waits for it
    const int exitAt = g_Length - OutFrames(g_Kind);
    if (g_T < exitAt) g_T = exitAt;
    g_HasNext = true;
    g_NextKind = kind;
    g_NextTitle = title;
    g_NextSub = subtitle;
    g_NextAccent = accent;
}

void CutIn::Hold(bool hold)
{
    if (hold && !g_Held && g_Length > 0 && g_Kind == Kind::Clear) g_Length = 0; // the pick has opened: the tag is not needed any more (the pick slides its own header in)
    if (g_Held && !hold) g_Docked = false;
    if (g_Held && !hold && g_Length > 0)
    {
        // the menu has closed: the card that was showing slams in again (it would otherwise pop up mid-way); a card that was leaving is dropped
        if (g_T >= g_Length - OutFrames(g_Kind)) g_Length = 0;
        else g_T = 0;
    }
    g_Held = hold;
}

void CutIn::Update()
{
    if (g_Held) return;
    if (g_Length > 0 && ++g_T >= g_Length)
    {
        g_Length = 0;
    }
    if (g_Length <= 0 && g_HasNext)
    {
        g_HasNext = false;
        Start(g_NextKind, g_NextTitle, g_NextSub, g_NextAccent);
    }
}

void CutIn::Reset()
{
    g_Length = 0;
    g_T = 0;
    g_HasNext = false;
    g_Held = false;
    g_Docked = false;
}

bool CutIn::Docked()
{
    return g_Docked;
}

bool CutIn::Active()
{
    return g_Length > 0;
}

// ---- the BOSS INTRO (the WARNING card) ----------------------------------------------------------------------------------------------------
// The arrival of the boss is the one place where the key-art logic of the Persona 3 Reload covers is used at full strength - the player is held for
// the whole arrival (GamePlayer::SetHold), nothing is being dodged:
//   one flat saturated band (#E8141C) cuts across the screen at a slant, with a hard black offset and a print of dark dots;
//   the BLACK silhouette of the boss - a flat ink shape - slides in over it, bigger than the band, tilted with it (where it sticks out of the band it
//   disappears into the dark of the world: the black is negative space);
//   the name in heavy white letters on a solid white block (the logo logic), a white tag WARNING over it, a white plate under it;
//   a few translucent crystals, warm on the red.
// It leaves the way every card leaves: the band snaps shut to the left. The real boss comes in from the right behind it.
void DrawBossIntro(float t, float len)
{
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    SKEW = -0.10f;
    const Rgb red{0.909f, 0.078f, 0.110f};   // #E8141C
    const Rgb ink{0.012f, 0.012f, 0.020f};
    const Rgb white{0.97f, 0.97f, 1.0f};
    const float inA = EaseOut(t / 12.0f);
    const float outA = EaseIn((t - (len - 16.0f)) / 16.0f);
    const float alive = inA * (1.0f - outA);
    if (alive <= 0.0f && outA >= 1.0f) return;
    float cy = 330.0f;
    if (inA >= 1.0f && outA <= 0.0f) cy += std::sin(t * 1.7f) * 2.0f; // the warning shakes
    const float h = 380.0f * (1.0f - outA);
    const float xr = 1400.0f - 1500.0f * outA;   // leaves to the left
    const float xl = 1400.0f - 1520.0f * inA;    // comes in from the right
    auto bandY = [&](float x) { return cy + SKEW * (x - CX); };
    auto slab = [&](float x0, float x1, float ym, float hh, const Rgb& c, float a)   // a slanted plate whose middle is at height ym
    { Band(x0, x1, ym - SKEW * ((x0 + x1) * 0.5f - CX), hh, c, a); };

    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    UiKit::Fill(0.0f, 0.0f, 1280.0f, 720.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.45f * alive)); // the world dims
    if (h > 2.0f)
    {
        Band(xl + 14.0f, xr + 14.0f, cy + 16.0f, h, ink, 0.80f);   // the hard offset
        Band(xl, xr, cy, h, red, 1.0f);                            // the flat band
        // the print: dark dots, big at the right end, gone at the left
        for (int i = 0; i < 14; ++i)
        {
            const float x = 1250.0f - 40.0f * static_cast<float>(i);
            if (x < xl + 30.0f || x > xr - 30.0f) continue;
            const float k = 1.0f - static_cast<float>(i) / 14.0f;
            for (int j = -4; j <= 4; ++j)
            {
                const float xx = x + ((j & 1) ? 20.0f : 0.0f);
                const float y = bandY(xx) + static_cast<float>(j) * 40.0f;
                const float r = 8.0f * k * (1.0f - std::fabs(static_cast<float>(j)) / 5.5f);
                if (r < 1.2f || std::fabs(static_cast<float>(j)) * 40.0f > h * 0.5f - 14.0f) continue;
                Quad(xx, y - r, xx + r, y, xx, y + r, xx - r, y, Rgb{0.50f, 0.02f, 0.06f}, 0.55f * alive);
            }
        }
        Band(xl, xr, cy - h * 0.5f + 1.5f, 3.0f, white, 0.9f);     // the cuts: a hairline on both edges
        Band(xl, xr, cy + h * 0.5f - 1.5f, 3.0f, white, 0.9f);
    }
    // the silhouette: bigger than the band, tilted with it, a dark-red offset behind it
    const int hull = AssetManager::Get(AssetManager::Tex::BossHull);
    if (hull >= 0 && alive > 0.0f)
    {
        const float slide = 1.0f - EaseOut((t - 3.0f) / 18.0f);
        const float sx = 930.0f + 430.0f * slide - 26.0f * Clamp01(t / len) + 380.0f * outA;
        const float sy = bandY(sx) - 4.0f;
        const float hp = 660.0f, wp = hp * 602.0f / 684.0f;
        const float rot = -std::atan(SKEW);
        Renderer2D_DrawTextureEx(hull, UiKit::NX(sx + 12.0f), UiKit::NY(sy + 10.0f), wp / 640.0f, hp / 360.0f, rot, 0.40f, 0.01f, 0.04f, 0.95f * alive, 0.0f, 0.0f, 1.0f, 1.0f);
        Renderer2D_DrawTextureEx(hull, UiKit::NX(sx), UiKit::NY(sy), wp / 640.0f, hp / 360.0f, rot, ink.r, ink.g, ink.b, alive, 0.0f, 0.0f, 1.0f, 1.0f);
    }
    // crystals: translucent, warm, a few, drifting (flat triangles)
    if (alive > 0.0f)
    {
        for (int i = 0; i < 16; ++i)
        {
            const float h0 = Hash01(i * 7 + 1), h1 = Hash01(i * 7 + 2), h2 = Hash01(i * 7 + 3), h3 = Hash01(i * 7 + 4);
            const float x = 60.0f + h0 * 1160.0f + std::sin(t * (0.03f + 0.03f * h3) + h2 * 6.0f) * 8.0f;
            const float y = bandY(x) + (h1 - 0.5f) * 560.0f + std::cos(t * 0.04f + h0 * 6.0f) * 6.0f;
            if (y < 20.0f || y > 700.0f) continue;
            const float r = 7.0f + 22.0f * h2 * h2;
            const float a0 = h3 * 6.2831853f + t * (h1 - 0.5f) * 0.05f, c = std::cos(a0), s = std::sin(a0);
            const float px[3] = {r, -r * 0.55f, -r * 0.25f}, py[3] = {0.0f, r * 0.42f, -r * 0.5f};
            Tri(x + px[0] * c - py[0] * s, y + px[0] * s + py[0] * c, x + px[1] * c - py[1] * s, y + px[1] * s + py[1] * c, x + px[2] * c - py[2] * s,
                y + px[2] * s + py[2] * c, Rgb{1.0f, 0.74f, 0.58f}, (0.18f + 0.30f * h1) * alive);
        }
    }
    Renderer2D_SetBlendMode(previous);

    // the wordmark, small, in the lower right corner: the card belongs to the game
    Wordmark::DrawBadge(1240.0f, 692.0f, 22.0f, 0.85f * alive);
    // the texts
    const float textA = Clamp01((t - 8.0f) / 4.0f) * (1.0f - outA);
    if (textA <= 0.0f) return;
    const float slideX = -90.0f * (1.0f - EaseOut((t - 8.0f) / 14.0f)) - 220.0f * outA;
    const float px = UiKit::FitFontPx(g_Title, 118.0f, 500.0f, 44.0f);
    const float nameW = UiKit::TextWidth(g_Title, px);
    const float left = 118.0f + slideX;
    const float tx = left + nameW * 0.5f;
    const float ty = bandY(tx) + 12.0f;
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    slab(left - 44.0f, left - 22.0f, ty - 6.0f, 150.0f, white, textA);                       // the solid white block of the logo
    {
        const std::string tag = Loc::T("WARNING");
        const float tagW = UiKit::TextWidth(tag, 22.0f) + 40.0f;
        slab(left - 4.0f, left - 4.0f + tagW, ty - 92.0f, 36.0f, white, 0.96f * textA);      // the tag over the name
        UiKit::Text(tag, left - 4.0f + tagW * 0.5f, ty - 92.0f, 22.0f, UiKit::Rgba(0.02f, 0.02f, 0.03f, textA), TextAlignment::Center, tagW + 40.0f, false);
    }
    Renderer2D_SetBlendMode(previous);
    Title(g_Title, tx, ty, px, white, ink, textA, 7.0f);
    if (!g_Sub.empty())
    {
        const float subW = UiKit::TextWidth(g_Sub, 17.0f) + 40.0f;
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
        slab(left - 4.0f, left - 4.0f + subW, ty + 80.0f, 32.0f, ink, 0.92f * textA);        // the plate under the name (black on the red)
        Renderer2D_SetBlendMode(previous);
        UiKit::Text(g_Sub, left - 4.0f + subW * 0.5f, ty + 80.0f, 17.0f, UiKit::Rgba(0.97f, 0.97f, 1.0f, textA), TextAlignment::Center, subW + 40.0f, false);
    }
}

namespace
{
// WAVE CLEAR: a small slanted tag under the wave counter. It opens from its middle, stays, and folds back into it; no band across the screen, no veil, nothing that
// covers the field (it was a full-width band that slammed in at the middle of the screen, and the pick that followed hid it again a moment later).
void DrawClearTag(float t, float len, const Rgb& acc, const Rgb& counter, Renderer2DBlendMode previous)
{
    constexpr float IN = 14.0f, OUT = 12.0f;
    const Rgb black{0.015f, 0.015f, 0.022f};
    const Rgb white{0.97f, 0.97f, 1.0f};
    const float in = EaseOut(t / IN);
    const float out = EaseIn((t - (len - OUT)) / OUT);
    const float open = in * (1.0f - out);
    const float cx = CX, cy = 132.0f, h = 40.0f;
    const float halfW = 168.0f * open;
    if (halfW < 3.0f) return;
    const float x0 = cx - halfW, x1 = cx + halfW, top = cy - h * 0.5f, bottom = cy + h * 0.5f;
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    auto tag = [&](float dx, float dy, const Rgb& c, float a) { Quad(x0 + dx + 10.0f, top + dy, x1 + dx + 10.0f, top + dy, x1 + dx, bottom + dy, x0 + dx, bottom + dy, c, a); };
    tag(5.0f, 4.0f, acc, 0.85f);
    tag(0.0f, 0.0f, black, 0.93f);
    Quad(x0 + 10.0f, top, x1 + 10.0f, top, x1 + 10.0f, top + 2.0f, x0 + 10.0f, top + 2.0f, white, 0.85f); // the white hairline on top (the cut)
    Quad(x0 + 26.0f, top - 6.0f, x0 + 26.0f + (x1 - x0) * 0.45f, top - 6.0f, x0 + 26.0f + (x1 - x0) * 0.45f, top - 4.5f, x0 + 26.0f, top - 4.5f, counter, 0.85f); // (the glass line)
    Renderer2D_SetBlendMode(previous);
    const float textA = Clamp01((t - 6.0f) / 6.0f) * (1.0f - out);
    if (textA <= 0.0f) return;
    const float px = UiKit::FitFontPx(g_Title, 26.0f, 300.0f, 14.0f);
    Title(g_Title, cx + 5.0f, cy + 1.0f, px, white, acc, textA, 3.0f);
    if (!g_Sub.empty())
        UiKit::Text(g_Sub, cx + 5.0f, bottom + 20.0f, 15.0f, UiKit::Rgba(0.84f, 0.90f, 1.0f, 0.95f * textA), TextAlignment::Center, 700.0f, false);
}
}

void CutIn::Draw()
{
    if (g_Length <= 0 || g_Held) return;
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    const float t = static_cast<float>(g_T);
    const float len = static_cast<float>(g_Length);
    const Rgb acc = ACCENT[g_Accent];
    const Rgb black{0.015f, 0.015f, 0.022f};
    const Rgb white{0.97f, 0.97f, 1.0f};
    // THE SEAM: every card has both halves. The hard band (P5) is the accent of the card; the glass lines (P3) are the other polarity colour.
    const bool hard = g_Accent == AccentRed || g_Accent == AccentOrange || g_Accent == AccentGold;  // red side: teeth on the hard band
    const Rgb counter = (g_Accent == AccentBlue || g_Accent == AccentWhite) ? ACCENT[AccentRed] : ACCENT[AccentBlue];
    const bool brk = g_Kind == Kind::Break;
    if (g_Kind == Kind::Warning)
    {
        DrawBossIntro(t, len);
        return;
    }
    if (g_Kind == Kind::Clear)
    {
        DrawClearTag(t, len, acc, counter, previous);
        return;
    }

    // timing: in (slam), hold, out (the band snaps shut and leaves to the left)
    const float inT = brk ? 6.0f : 10.0f;
    const float outT = brk ? 10.0f : 14.0f;
    const bool clear = g_Kind == Kind::Clear;
    const float in = EaseOut(t / inT);
    // WAVE CLEAR does not leave: it docks - the band flattens, narrows and rises to the top left, where it becomes the header of the
    // module pick (GameMenuUi::DrawHeader: a black slab at y 60..120, x -40..760, the title at 84 / 92)
    const float dock = clear ? EaseInOut((t - (len - DOCK_FRAMES)) / DOCK_FRAMES) : 0.0f;
    const float out = clear ? 0.0f : EaseIn((t - (len - outT)) / outT);
    SKEW = -0.10f * (1.0f - dock);

    float cy = brk ? 360.0f : 300.0f;
    float h = (brk ? 84.0f : 112.0f) * (1.0f - out);
    float xr = 1400.0f - 1500.0f * out;      // leaves to the left
    float xl = 1400.0f - 1520.0f * in;       // comes in from the right
    if (clear)
    {
        cy = cy + (90.0f - cy) * dock;
        h = h + (60.0f - h) * dock;
        xl = xl + (-40.0f - xl) * dock;
        xr = xr + (760.0f - xr) * dock;
    }
    if (brk)
    {
        // BREAK!: the answer to a hit that the player has just made, in the middle of a fight - so it is small and short. No veil, no flash,
        // no band across the screen: a short gold slash through the weak point, and a compact tag in the upper right (over the boss's side
        // of the screen, away from the ship and the bullets) that slides in, stays a moment and folds away.
        const float s = EaseOut(t / 4.0f);
        const float fade = 1.0f - Clamp01((t - 6.0f) / 10.0f);
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
        if (fade > 0.0f)
        {
            const float x0 = 940.0f, y0 = 430.0f, x1 = x0 + 220.0f * s, y1 = y0 - 150.0f * s;
            Quad(x0 - 3.0f, y0, x1 - 3.0f, y1, x1 + 3.0f, y1, x0 + 3.0f, y0, acc, 0.90f * fade);
        }
        const float tagY = 112.0f, tagH = 40.0f, right = 1236.0f;
        const float width = 250.0f * in;
        const float x1 = right - (right - (right - width)) * out; // (leaving: the tag folds towards its right edge)
        const float x0 = (right - width) + (x1 - (right - width)) * out;
        if (x1 - x0 > 4.0f)
        {
            // a slanted tag (the top edge leans right by 10 px), the accent offset under it, a white hairline on top
            auto tag = [&](float dx, float dy, const Rgb& c, float al)
            {
                Quad(x0 + dx + 10.0f, tagY - tagH * 0.5f + dy, x1 + dx + 10.0f, tagY - tagH * 0.5f + dy, x1 + dx, tagY + tagH * 0.5f + dy, x0 + dx, tagY + tagH * 0.5f + dy, c, al);
            };
            tag(6.0f, 5.0f, acc, 0.92f);
            tag(0.0f, 0.0f, black, 0.95f);
            Quad(x0 + 10.0f, tagY - tagH * 0.5f, x1 + 10.0f, tagY - tagH * 0.5f, x1 + 10.0f, tagY - tagH * 0.5f + 2.0f, x0 + 10.0f, tagY - tagH * 0.5f + 2.0f, white, 0.85f);
        }
        const float ta = Clamp01((t - 3.0f) / 3.0f) * (1.0f - out);
        if (ta > 0.0f) Title(g_Title, (x0 + x1) * 0.5f, tagY + 1.0f, 26.0f, acc, black, ta, 2.0f);
        Renderer2D_SetBlendMode(previous);
        return;
    }
    const float deco = 1.0f - dock; // the halftone, the glass lines and the subtitle fade while it docks
    // ... and its accent turns into the colour of the polarity, the accent of the header it becomes
    Rgb accD = acc;
    if (dock > 0.0f)
    {
        const UiKit::Color pa = GameMenuUi::Accent(1.0f);
        accD = Rgb{acc.r + (pa.r - acc.r) * dock, acc.g + (pa.g - acc.g) * dock, acc.b + (pa.b - acc.b) * dock};
    }

    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    // a dim veil over the world while the card is up (only the loud ones)
    if (brk) UiKit::Fill(0.0f, 0.0f, 1280.0f, 720.0f, UiKit::Rgba(0.0f, 0.0f, 0.0f, 0.28f * in * (1.0f - out)));

    if (brk)
    {
        // the slash: a gold blade across the screen, then the white flash
        const float s = EaseOut(t / 5.0f);
        const float x0 = -80.0f, y0 = 760.0f, x1 = -80.0f + 1460.0f * s, y1 = 760.0f - 860.0f * s;
        const float w = 10.0f * (1.0f - out);
        Quad(x0 - w, y0, x1 - w, y1, x1 + w, y1, x0 + w, y0, acc, 0.95f * (1.0f - out));
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Additive);
        UiKit::Fill(0.0f, 0.0f, 1280.0f, 720.0f, UiKit::Rgba(1.0f, 0.95f, 0.85f, 0.30f * Clamp01(1.0f - t / 6.0f)));
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    }

    // the accent band behind, offset down-right (P5), and the glass lines of the other colour gliding in a beat later (P3)
    if (h > 1.0f)
    {
        Halftone(cy - h * 0.5f - 46.0f, acc, 0.55f * (1.0f - out) * deco, in);
        {
            const float ox = 36.0f + (10.0f - 36.0f) * dock, oy = 10.0f + (6.0f - 10.0f) * dock;
            Band(xl + ox, xr + ox, cy + oy, h + 14.0f * deco, accD, 0.92f);
        }
        if (hard) Teeth(xl + 36.0f, xr + 36.0f, cy + 10.0f, h + 14.0f, acc, 0.92f, g_T / 4);
        const float glide = EaseOut((t - inT * 0.6f) / 14.0f) * (1.0f - out) * deco;
        if (glide > 0.0f)
        {
            const float gl = xr - (xr - xl) * glide;
            Band(gl, xr, cy - h * 0.5f - 12.0f, 2.0f, counter, 0.90f);
            Band(gl + 60.0f, xr, cy - h * 0.5f - 20.0f, 1.0f, counter, 0.55f);
        }
        // the black band
        Band(xl, xr, cy, h, black, 0.96f);
        // a white hairline on its top edge (the cut)
        Band(xl, xr, cy - h * 0.5f + 1.5f, 3.0f, white, 0.85f);
    }

    Renderer2D_SetBlendMode(previous);

    // the texts slide in from the left a little, and leave with the band
    const float textA = Clamp01((t - inT * 0.5f) / 4.0f) * (1.0f - out);
    if (textA <= 0.0f) return;
    float tx = CX - 70.0f * (1.0f - EaseOut((t - inT * 0.5f) / 12.0f)) - 260.0f * out;
    float ty = cy + 2.0f;
    float px = brk ? 66.0f : 58.0f;
    if (clear && dock > 0.0f) // the title glides to where the pick's header puts it
    {
        const float headPx = UiKit::FitFontPx(g_Title, 42.0f, 560.0f, 16.0f);
        const float headCx = 84.0f + UiKit::TextWidth(g_Title, headPx) * 0.5f;
        px = px + (headPx - px) * dock;
        tx = tx + (headCx - tx) * dock;
        ty = cy;
    }
    const Rgb face = brk ? acc : white;
    const Rgb shadow = accD;
    Title(g_Title, tx, ty, px, face, shadow, textA, 5.0f - 2.0f * dock);
    if (deco > 0.0f)
    {
        // the seam diamond (half red / half blue) at the left of the title
        const float dx = tx - UiKit::TextWidth(g_Title, px) * 0.55f - 34.0f, dy = ty, r = 11.0f;
        const Rgb red = ACCENT[AccentRed], blue = ACCENT[AccentBlue];
        Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
        Tri(dx, dy - r, dx - r, dy, dx, dy + r, red, textA * deco);
        Tri(dx, dy - r, dx, dy + r, dx + r, dy, blue, textA * deco);
        Quad(dx, dy - r * 0.38f, dx + r * 0.38f, dy, dx, dy + r * 0.38f, dx - r * 0.38f, dy, white, textA * deco);
        Renderer2D_SetBlendMode(previous);
    }
    if (!g_Sub.empty() && deco > 0.0f)
    {
        const float sy = cy + h * 0.5f + (hard ? 50.0f : 38.0f); // (below the teeth)
            // the subtitle on a white tag (both halves use it now), black letters, slanted with the band
            const float tw = UiKit::TextWidth(g_Sub, 20.0f) + 44.0f;
            const float x0 = tx - tw * 0.5f, x1 = tx + tw * 0.5f;
            Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
            Quad(x0 + 8.0f, sy - 16.0f + SKEW * (x0 - tx), x1 + 8.0f, sy - 16.0f + SKEW * (x1 - tx), x1 - 8.0f, sy + 16.0f + SKEW * (x1 - tx),
                 x0 - 8.0f, sy + 16.0f + SKEW * (x0 - tx), white, 0.95f * textA * deco);
            Renderer2D_SetBlendMode(previous);
            UiKit::Text(g_Sub, tx, sy, 20.0f, UiKit::Rgba(0.02f, 0.02f, 0.03f, textA * deco), TextAlignment::Center, 900.0f, false);
            }
}






