#pragma once

// DisplayMath: the arithmetic of "how does a 16:9 game fit a screen of any shape and size", with no Windows and no Direct3D in it, so that it is one piece of code (the swap chain,
// the mouse and the text all ask the same functions) and so that it can be tested on every monitor shape (tools/balance_sim/display_test.cpp).
//
// THREE SIZES (version 10; before, the second and the third were one):
//   OUTPUT   the back buffer: the pixels of the window / of the screen. Never scaled by Windows (a back buffer that is not the size of the window is stretched by the desktop with a
//            plain bilinear filter, which is what made 4K on a 1440p screen soft). The text is drawn at this size, so it is always sharp.
//   GAME     the largest 16:9 rectangle in the output, in its middle (GameRect). The rest of a 16:9-less screen (16:10, 21:9) is filled by PostFx.
//   RENDER   the size of the picture of the world (the HDR scene). Equal to the game rectangle, or larger: then the picture is a SUPERSAMPLE (4K on a 1440p screen) and PostFx scales
//            it down with a Lanczos filter in linear light (ResampleAxis is the reference of that filter, the shader postfx_resample is its twin).
#include <cmath>

namespace DisplayMath
{
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
};

// The largest 16:9 rectangle that fits a buffer of bw x bh, centred. A buffer that is within 2 px of 16:9 on both axes is used whole (1366 x 768 is 16:9 to the pixel for every
// practical purpose; the sliver would only be a seam).
inline Rect GameRect(int bw, int bh)
{
    bw = bw > 0 ? bw : 1;
    bh = bh > 0 ? bh : 1;
    Rect r;
    r.w = bw;
    r.h = bw * 9 / 16;
    if (r.h > bh)
    {
        r.h = bh;
        r.w = bh * 16 / 9;
    }
    if (bw - r.w <= 2 && bh - r.h <= 2)
    {
        r.w = bw;
        r.h = bh;
    }
    r.x = (bw - r.w) / 2;
    r.y = (bh - r.h) / 2;
    return r;
}

// The picture in full screen: the largest 16:9 rectangle of the monitor (its size, not its place).
inline bool FullscreenFit(int monitorW, int monitorH, int& w, int& h)
{
    if (monitorW <= 0 || monitorH <= 0) return false;
    w = monitorW;
    h = monitorW * 9 / 16;
    if (h > monitorH)
    {
        h = monitorH;
        w = monitorH * 16 / 9;
    }
    return w > 0 && h > 0;
}

// The first of the sizes (widths in ascending order) that is at least as wide as the picture on the screen ("native"); a smaller one would only be a blurry enlargement.
inline int NativeIndex(const int* widths, int count, int fitWidth)
{
    for (int i = 0; i < count; ++i)
    {
        if (widths[i] >= fitWidth) return i;
    }
    return count > 0 ? count - 1 : 0;
}

// The last of the sizes that is offered in full screen: the largest whose width is at most `maxRatio` times the picture on the screen (a supersample of more than 2 : 1 costs the
// graphics chip a great deal and shows nothing more; a 1366 x 768 laptop is not offered 4K), and never smaller than the native size.
inline int LastIndex(const int* widths, int count, int fitWidth, float maxRatio = 2.0f)
{
    const int first = NativeIndex(widths, count, fitWidth);
    int last = first;
    for (int i = first; i < count; ++i)
    {
        if (static_cast<float>(widths[i]) <= static_cast<float>(fitWidth) * maxRatio + 0.5f) last = i;
    }
    return last;
}

// The size that the world is drawn at, for a chosen render size and the game rectangle of the output: never smaller than the game rectangle (an enlargement would be a blur).
inline void EffectiveRender(int renderW, int renderH, const Rect& game, int& outW, int& outH)
{
    outW = renderW > game.w ? renderW : game.w;
    outH = renderH > game.h ? renderH : game.h;
}

// Does the picture have to be scaled down to the game rectangle (more than one pixel larger on an axis)?
inline bool NeedsResample(int renderW, int renderH, const Rect& game)
{
    return renderW > game.w + 1 || renderH > game.h + 1;
}

// ---- the filter -----------------------------------------------------------------------------------------------------------------------------------------------------------------
// Lanczos with two lobes: sinc(x) * sinc(x / 2) for |x| < 2, in destination pixels.
inline float Lanczos2(float x)
{
    x = std::fabs(x);
    if (x < 1.0e-4f) return 1.0f;
    if (x >= 2.0f) return 0.0f;
    const float px = 3.14159265f * x;
    return 2.0f * std::sin(px) * std::sin(px * 0.5f) / (px * px);
}

// How many source pixels one destination pixel looks at, at most, along an axis (the loop of the shader is bounded by it).
inline int MaxTaps(float ratio)
{
    const float r = ratio > 1.0f ? ratio : 1.0f;
    return static_cast<int>(std::ceil(4.0f * r)) + 1;
}

// One destination pixel of an axis, from `count` source pixels. `ratio` is source pixels per destination pixel; `dst` is the index of the destination pixel. The twin of the loops of
// shaders/postfx.hlsli (PSResampleH / PSResampleV): the same centre, the same first and last pixel, the same weights, the same normalisation, the same clamp at the edges.
// Returns the value; `taps` (optional) receives the number of pixels that were read.
inline float ResampleAxis(const float* source, int count, float ratio, int dst, int* taps = nullptr)
{
    const float r = ratio > 1.0f ? ratio : 1.0f;
    const float support = 2.0f * r;
    const float centre = (static_cast<float>(dst) + 0.5f) * ratio;
    const int first = static_cast<int>(std::floor(centre - support + 0.5f));
    const int last = static_cast<int>(std::floor(centre + support - 0.5f));
    float sum = 0.0f, weights = 0.0f;
    int n = 0;
    for (int k = first; k <= last; ++k)
    {
        const float w = Lanczos2((static_cast<float>(k) + 0.5f - centre) / r);
        const int index = k < 0 ? 0 : (k > count - 1 ? count - 1 : k);
        sum += source[index] * w;
        weights += w;
        ++n;
    }
    if (taps != nullptr) *taps = n;
    const float v = weights > 1.0e-6f ? sum / weights : 0.0f;
    return v < 0.0f ? 0.0f : v;
}
} // namespace DisplayMath
