// display_test: does the 16:9 game fit every screen, and is the scale-down of a supersample right? (src/core/DisplayMath.h: the arithmetic that the swap chain, the mouse and the text share)
//   1. GameRect: for every shape of window or screen that exists (and some that should not) the game is a 16:9 rectangle inside it, centred, as large as it can be
//   2. full screen: for every monitor, the sizes that the settings page offers (DisplaySystem: from the native size up to twice as wide) - each one is a 16:9 picture that is not
//      smaller than the picture on the screen, and the scale-down to it needs a bounded number of taps
//   3. the mouse: the corners of the game rectangle are the corners of the game (-1 .. 1), whatever the shape of the window
//   4. the filter (ResampleAxis, the twin of PSResampleH / PSResampleV): it keeps a flat colour flat, keeps the position of a smooth signal, keeps the energy of a thin line better than
//      the plain bilinear filter that the desktop used to scale the back buffer, and never goes below 0. (The numbers of the plain filter are printed next to it.)
//   display_test returns 1 when a check fails
#include "core/DisplayMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

namespace
{
// the sizes that the settings page offers in full screen (DisplaySystem.cpp: RESOLUTIONS)
const int WIDTHS[5] = {1280, 1600, 1920, 2560, 3840};
const int HEIGHTS[5] = {720, 900, 1080, 1440, 2160};

struct Monitor
{
    const char* name;
    int w, h;
};

const Monitor MONITORS[] = {
    {"1280x720 (16:9)", 1280, 720},       {"1366x768 (laptop)", 1366, 768},  {"1440x900 (16:10)", 1440, 900},   {"1600x900 (16:9)", 1600, 900},
    {"1680x1050 (16:10)", 1680, 1050},    {"1920x1080 (16:9)", 1920, 1080},  {"1920x1200 (16:10)", 1920, 1200}, {"2560x1080 (21:9)", 2560, 1080},
    {"2560x1440 (16:9, 2K)", 2560, 1440}, {"2560x1600 (16:10)", 2560, 1600}, {"3440x1440 (21:9)", 3440, 1440},  {"3840x1600 (24:10)", 3840, 1600},
    {"3840x2160 (16:9, 4K)", 3840, 2160}, {"5120x1440 (32:9)", 5120, 1440},   {"1280x1024 (5:4)", 1280, 1024},   {"1024x768 (4:3)", 1024, 768},
    {"2880x1800 (16:10 laptop)", 2880, 1800}, {"3000x2000 (3:2 laptop)", 3000, 2000}, {"1080x1920 (portrait)", 1080, 1920},
};
} // namespace

int main()
{
    std::printf("1. GameRect: the largest 16:9 rectangle, centred\n");
    {
        int bad = 0, count = 0, worstAspect = 0;
        for (const Monitor& m : MONITORS)
        {
            for (int dw = -3; dw <= 3; ++dw) // (a window is not always exactly the size that was asked for)
            {
                const int bw = m.w + dw, bh = m.h;
                const DisplayMath::Rect r = DisplayMath::GameRect(bw, bh);
                ++count;
                const bool inside = r.x >= 0 && r.y >= 0 && r.x + r.w <= bw && r.y + r.h <= bh;
                const bool centred = std::abs((bw - r.w) - 2 * r.x) <= 1 && std::abs((bh - r.h) - 2 * r.y) <= 1;
                const bool maximal = r.w == bw || r.h == bh;
                // 16:9 to the pixel, or the whole buffer when it is within 2 px of 16:9
                const bool exact = std::abs(r.w * 9 - r.h * 16) <= 16 || (r.w == bw && r.h == bh && std::abs(bw * 9 - bh * 16) <= 2 * 16 + 18);
                if (!(inside && centred && maximal && exact)) ++bad;
                worstAspect = std::max(worstAspect, std::abs(r.w * 9 - r.h * 16));
            }
        }
        std::printf("     (%d shapes, the worst departure from 16:9 is %d / 16 of a pixel)\n", count, worstAspect);
        Check(bad == 0, "inside the buffer, centred, as large as it can be, 16:9 to the pixel");
        const DisplayMath::Rect a = DisplayMath::GameRect(1920, 1080), b = DisplayMath::GameRect(1920, 1200), c = DisplayMath::GameRect(3440, 1440), d = DisplayMath::GameRect(1366, 768);
        Check(a.x == 0 && a.y == 0 && a.w == 1920 && a.h == 1080, "1920 x 1080: the whole screen");
        Check(b.x == 0 && b.y == 60 && b.w == 1920 && b.h == 1080, "1920 x 1200 (16:10): 1920 x 1080 with 60 px above and below");
        Check(c.x == 440 && c.y == 0 && c.w == 2560 && c.h == 1440, "3440 x 1440 (21:9): 2560 x 1440 with 440 px at each side");
        Check(d.x == 0 && d.y == 0 && d.w == 1366 && d.h == 768, "1366 x 768 (a laptop that is 16:9 within a pixel): the whole screen, no sliver");
    }

    std::printf("2. full screen: what every monitor is offered\n");
    {
        int offers = 0, badNative = 0, badAspect = 0, badRatio = 0, badTaps = 0, bad1to1 = 0;
        float worstRatio = 1.0f;
        int worstTaps = 0;
        for (const Monitor& m : MONITORS)
        {
            int fw = 0, fh = 0;
            if (!DisplayMath::FullscreenFit(m.w, m.h, fw, fh)) continue;
            const int first = DisplayMath::NativeIndex(WIDTHS, 5, fw);
            const int last = DisplayMath::LastIndex(WIDTHS, 5, fw);
            if (WIDTHS[first] < fw && first != 4) ++badNative; // (a monitor wider than 3840 is offered 3840 and nothing else)
            if (last < first) ++badNative;
            const DisplayMath::Rect game = DisplayMath::GameRect(m.w, m.h);
            for (int i = first; i <= last; ++i)
            {
                ++offers;
                int rw = 0, rh = 0;
                DisplayMath::EffectiveRender(WIDTHS[i], HEIGHTS[i], game, rw, rh);
                if (rw * 9 != rh * 16) ++badAspect;
                const bool resample = DisplayMath::NeedsResample(rw, rh, game);
                if (rw < game.w || rh < game.h) ++badRatio;
                const float rx = static_cast<float>(rw) / static_cast<float>(game.w), ry = static_cast<float>(rh) / static_cast<float>(game.h);
                worstRatio = std::max(worstRatio, std::max(rx, ry));
                if (rx > 2.01f || ry > 2.01f) ++badRatio;
                const int taps = std::max(DisplayMath::MaxTaps(rx), DisplayMath::MaxTaps(ry));
                worstTaps = std::max(worstTaps, taps);
                if (taps > 9) ++badTaps;
                if (i == first && m.w * 9 == m.h * 16 && resample) ++bad1to1; // the native size of a 16:9 screen is drawn 1 : 1
            }
        }
        std::printf("     (%d offers on %d monitors; the biggest scale-down is %.2f, at most %d pixels are read for one)\n", offers, static_cast<int>(sizeof(MONITORS) / sizeof(MONITORS[0])), worstRatio, worstTaps);
        Check(badNative == 0, "the first size offered is at least as wide as the picture on the screen (a smaller one would be a blurry enlargement)");
        Check(badAspect == 0, "every picture that is drawn is 16:9 to the pixel");
        Check(badRatio == 0, "no picture is smaller than the game rectangle, and none needs more than a 2 : 1 scale-down");
        Check(badTaps <= 0, "the filter reads at most 9 pixels per output pixel (a bounded loop in the shader)");
        Check(bad1to1 == 0, "the native size of a 16:9 screen needs no scale-down at all (it is drawn 1 : 1, as before)");

        // the case of the report: 4K on a 1440p screen
        const DisplayMath::Rect g2k = DisplayMath::GameRect(2560, 1440);
        int rw = 0, rh = 0;
        DisplayMath::EffectiveRender(3840, 2160, g2k, rw, rh);
        Check(DisplayMath::NeedsResample(rw, rh, g2k) && rw == 3840 && rh == 2160 && std::fabs(rw / static_cast<float>(g2k.w) - 1.5f) < 1e-6f,
              "4K on a 1440p screen: a 3840 x 2160 picture scaled by 1.5 to the 2560 x 1440 back buffer");
        int nfw = 0, nfh = 0;
        DisplayMath::FullscreenFit(2560, 1440, nfw, nfh);
        Check(DisplayMath::NativeIndex(WIDTHS, 5, nfw) == 3, "a 1440p screen is offered 2560 and 3840 (native and a supersample)");
        int lw = 0, lh = 0;
        DisplayMath::FullscreenFit(1366, 768, lw, lh);
        Check(DisplayMath::NativeIndex(WIDTHS, 5, lw) == 1 && DisplayMath::LastIndex(WIDTHS, 5, lw) == 3, "a 1366 x 768 laptop is offered 1600, 1920 and 2560 (not 4K: more than twice its picture)");
        int lw2 = 0, lh2 = 0;
        DisplayMath::FullscreenFit(1920, 1080, lw2, lh2);
        Check(DisplayMath::LastIndex(WIDTHS, 5, lw2) == 4, "a 1080p screen is offered 1920, 2560 and 3840 (4K is exactly twice)");
        int u2k = 0, u2h = 0;
        DisplayMath::FullscreenFit(3440, 1440, u2k, u2h);
        Check(u2k == 2560 && u2h == 1440 && DisplayMath::NativeIndex(WIDTHS, 5, u2k) == 3, "a 21:9 screen of 1440 lines is offered the same sizes as a 16:9 one (the picture is the middle of it)");
    }

    std::printf("3. the mouse: the game rectangle is the game\n");
    {
        int bad = 0;
        for (const Monitor& m : MONITORS)
        {
            const DisplayMath::Rect r = DisplayMath::GameRect(m.w, m.h);
            // the mouse of GameInput: local = position - corner; x = local / width * 2 - 1; y = 1 - local / height * 2
            auto mouseX = [&](int px) { return static_cast<float>(px - r.x) / static_cast<float>(r.w) * 2.0f - 1.0f; };
            auto mouseY = [&](int py) { return 1.0f - static_cast<float>(py - r.y) / static_cast<float>(r.h) * 2.0f; };
            if (std::fabs(mouseX(r.x) + 1.0f) > 1e-6f || std::fabs(mouseX(r.x + r.w) - 1.0f) > 1e-6f) ++bad;
            if (std::fabs(mouseY(r.y) - 1.0f) > 1e-6f || std::fabs(mouseY(r.y + r.h) + 1.0f) > 1e-6f) ++bad;
            if (std::fabs(mouseX(r.x + r.w / 2)) > 2.0f / static_cast<float>(r.w) || std::fabs(mouseY(r.y + r.h / 2)) > 2.0f / static_cast<float>(r.h)) ++bad;
        }
        Check(bad == 0, "the corners of the game rectangle are -1 .. 1 and its middle is 0, on every monitor");
    }

    std::printf("4. the filter of the scale-down\n");
    {
        // a flat colour stays flat, for every ratio and position (the weights are normalised)
        std::vector<float> flat(4000, 0.37f);
        float worst = 0.0f;
        for (float ratio : {1.0f, 1.1667f, 1.3333f, 1.5f, 2.0f, 3.0f})
            for (int dst = 0; dst < static_cast<int>(flat.size() / ratio) - 1; dst += 7)
                worst = std::max(worst, std::fabs(DisplayMath::ResampleAxis(flat.data(), static_cast<int>(flat.size()), ratio, dst) - 0.37f));
        std::printf("     (flat colour: worst error %.2g)\n", worst);
        Check(worst < 1e-5f, "a flat colour stays flat (ratios 1 to 3, edges included)");

        // a smooth signal keeps its place: the output pixel j is the signal at (j + 0.5) * ratio
        const int n = 3840;
        std::vector<float> wave(n);
        for (int i = 0; i < n; ++i) wave[i] = 0.5f + 0.4f * std::sin(2.0f * 3.14159265f * (static_cast<float>(i) + 0.5f) / 480.0f);
        float place = 0.0f;
        for (float ratio : {1.5f, 2.0f, 1.2f})
            for (int dst = 40; dst < static_cast<int>(n / ratio) - 40; dst += 5)
            {
                const float want = 0.5f + 0.4f * std::sin(2.0f * 3.14159265f * (static_cast<float>(dst) + 0.5f) * ratio / 480.0f);
                place = std::max(place, std::fabs(DisplayMath::ResampleAxis(wave.data(), n, ratio, dst) - want));
            }
        std::printf("     (a wave of 480 pixels, scaled by 1.5 / 2 / 1.2: worst error %.4f)\n", place);
        Check(place < 0.02f, "a smooth signal keeps its place (no shift of half a pixel: the centres of the pixels are respected)");

        // a thin bright line on black: how much of its energy survives the scale-down, at every phase of the pixel grid. (Energy = the sum of the output times the ratio.) The plain
        // bilinear filter of the desktop is measured next to it.
        auto bilinearAt = [](const std::vector<float>& src, float ratio, int dst) {
            const float c = (static_cast<float>(dst) + 0.5f) * ratio - 0.5f;
            const int i0 = static_cast<int>(std::floor(c));
            const float f = c - static_cast<float>(i0);
            const int count = static_cast<int>(src.size());
            return src[std::min(std::max(i0, 0), count - 1)] * (1.0f - f) + src[std::min(std::max(i0 + 1, 0), count - 1)] * f;
        };
        float lanczosWorst = 0.0f, bilinearWorst = 0.0f;
        for (int phase = 0; phase < 12; ++phase)
        {
            std::vector<float> one(1200, 0.0f);
            one[600 + phase] = 1.0f; // a line ONE pixel wide, the thinnest line of the game (the hairlines of the HUD and of the seam)
            float sumL = 0.0f, sumB = 0.0f;
            const float ratio = 1.5f;
            for (int dst = 0; dst < 800; ++dst)
            {
                sumL += DisplayMath::ResampleAxis(one.data(), 1200, ratio, dst);
                sumB += bilinearAt(one, ratio, dst);
            }
            lanczosWorst = std::max(lanczosWorst, std::fabs(sumL * ratio - 1.0f));
            bilinearWorst = std::max(bilinearWorst, std::fabs(sumB * ratio - 1.0f));
        }
        std::printf("     (a line one pixel wide scaled by 1.5, 12 phases: the energy is off by at most %.0f %% with this filter, %.0f %% with the plain bilinear one)\n", lanczosWorst * 100.0f, bilinearWorst * 100.0f);
        Check(lanczosWorst < 0.16f && lanczosWorst < bilinearWorst, "a thin line keeps its energy within 16 %, and better than with the plain filter");

        // the edge: a step from 0 to 1 scaled by 1.5, at every phase of the grid. The width of the transition (10 % to 90 %), in output pixels, should be small and the SAME for every phase.
        auto edgeWidth = [&](bool lanczos, int phase) {
            std::vector<float> step(600, 0.0f);
            for (int i = 300 + phase; i < 600; ++i) step[i] = 1.0f;
            const float ratio = 1.5f;
            std::vector<float> out;
            for (int dst = 100; dst < 350; ++dst)
            {
                float v;
                if (lanczos)
                {
                    v = DisplayMath::ResampleAxis(step.data(), 600, ratio, dst);
                }
                else // the filter of the desktop: bilinear at the centre of the output pixel
                {
                    const float c = (static_cast<float>(dst) + 0.5f) * ratio - 0.5f;
                    const int i0 = static_cast<int>(std::floor(c));
                    const float f = c - static_cast<float>(i0);
                    v = step[std::min(std::max(i0, 0), 599)] * (1.0f - f) + step[std::min(std::max(i0 + 1, 0), 599)] * f;
                }
                out.push_back(v);
            }
            // position where the curve crosses 0.1 and 0.9 (linear interpolation between pixels)
            auto crossing = [&](float level) {
                for (size_t k = 1; k < out.size(); ++k)
                    if (out[k - 1] < level && out[k] >= level) return static_cast<float>(k - 1) + (level - out[k - 1]) / (out[k] - out[k - 1]);
                return 0.0f;
            };
            return crossing(0.9f) - crossing(0.1f);
        };
        float lMin = 99.0f, lMax = 0.0f, bMin = 99.0f, bMax = 0.0f;
        for (int phase = 0; phase < 3; ++phase) // (the grid repeats every 3 source pixels = 2 output pixels at 1.5)
        {
            lMin = std::min(lMin, edgeWidth(true, phase));
            lMax = std::max(lMax, edgeWidth(true, phase));
            bMin = std::min(bMin, edgeWidth(false, phase));
            bMax = std::max(bMax, edgeWidth(false, phase));
        }
        std::printf("     (an edge scaled by 1.5, 10-90 %% width over the phases of the grid: this filter %.2f .. %.2f px, the plain bilinear filter %.2f .. %.2f px)\n", lMin, lMax, bMin, bMax);
        Check(lMax < 1.6f && lMin > 0.7f, "an edge stays within 0.7 .. 1.6 output pixels wide at every phase (it is not smeared over a second pixel)");

        // never below 0 (a dark sky next to a bright bullet must not get a negative lobe that wraps to something else)
        std::vector<float> spike(400, 0.0f);
        spike[200] = 1.0f;
        float lowest = 1.0f;
        for (int dst = 0; dst < 260; ++dst) lowest = std::min(lowest, DisplayMath::ResampleAxis(spike.data(), 400, 1.5f, dst));
        Check(lowest >= 0.0f, "the output is never negative");

        int taps = 0;
        DisplayMath::ResampleAxis(flat.data(), 4000, 1.5f, 1000, &taps);
        int taps2 = 0;
        DisplayMath::ResampleAxis(flat.data(), 4000, 2.0f, 1000, &taps2);
        std::printf("     (taps: %d at 1.5, %d at 2.0; the bound is %d / %d)\n", taps, taps2, DisplayMath::MaxTaps(1.5f), DisplayMath::MaxTaps(2.0f));
        Check(taps <= DisplayMath::MaxTaps(1.5f) && taps2 <= DisplayMath::MaxTaps(2.0f), "the number of pixels read stays within MaxTaps");
    }

    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d CHECKS FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
