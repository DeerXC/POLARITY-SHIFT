#include "ui/SceneFx.h"
#include "core/DirectX.h"
#include "core/Renderer2D.h"
#include "ui/GameMenuUi.h"
#include "ui/UiKit.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr float SCREEN_W = 1280.0f;
constexpr float SCREEN_H = 720.0f;
constexpr float TILT = 0.60f;          // the angle of the red / blue cut (radians)
constexpr float TOTAL_FRAMES = 30.0f;  // the whole change (the shapes of GameMenuUi need 29 frames to settle)
constexpr float GHOST_FRAMES = 9.0f;   // the old picture lives this long
constexpr float GHOST_SLIDE = 90.0f;   // ... and slides this far (px)
constexpr float IMPACT_AT = 11.0f;     // frame at which the cut flashes
constexpr float TEXT_FROM = 8.0f;      // the text of the new screen fades in between these two frames
constexpr float TEXT_TO = 19.0f;

Clock::time_point g_Start{};
bool g_Running = false;
bool g_Forward = true;
bool g_UsesSnapshot = false;
bool g_ImpactDone = false;

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float Smooth(float t)
{
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

// frames (60 per second) since Begin(); a huge value when nothing is running
float Elapsed()
{
    if (!g_Running) return 1.0e6f;
    const float frames = std::chrono::duration<float>(Clock::now() - g_Start).count() * 60.0f;
    if (frames > TOTAL_FRAMES)
    {
        g_Running = false;
        DirectXSystem::SetSnapshotFrozen(false);
        return 1.0e6f;
    }
    return frames;
}
}

void SceneFx::Begin(bool forward)
{
    g_UsesSnapshot = DirectXSystem::HasSnapshot();
    if (g_UsesSnapshot && !g_Running)
    {
        DirectXSystem::SetSnapshotFrozen(true); // keep the picture of the old screen until it has left
    }
    g_Start = Clock::now();
    g_Running = true;
    g_Forward = forward;
    g_ImpactDone = false;
    GameMenuUi::StartEntrance(); // the rows, cards and the title come in one after the other
}

float SceneFx::TextAlpha()
{
    const float t = Elapsed();
    if (t >= TOTAL_FRAMES) return 1.0f;
    return Smooth((t - TEXT_FROM) / (TEXT_TO - TEXT_FROM));
}

float SceneFx::Enter()
{
    return Clamp01(Elapsed() / TOTAL_FRAMES);
}

void SceneFx::Draw()
{
    const float t = Elapsed();
    if (t >= TOTAL_FRAMES) return;

    if (!g_ImpactDone && t >= IMPACT_AT)
    {
        g_ImpactDone = true;
        GameMenuUi::Impact();
    }

    // the old picture: slides away towards the red side of the cut (deeper) or the blue side (back) while it fades
    if (t >= GHOST_FRAMES || !g_UsesSnapshot || DirectXSystem::GetSnapshotSRV() == nullptr) return;
    const float k = t / GHOST_FRAMES;
    const float fade = 1.0f - Smooth(k);
    const float slide = GHOST_SLIDE * (1.0f - (1.0f - k) * (1.0f - k)) * (g_Forward ? 1.0f : -1.0f);
    const float moveX = -std::sin(TILT) * slide;
    const float moveY = -std::cos(TILT) * slide;

    Renderer2D_BindExternalTexture(DirectXSystem::GetSnapshotSRV());
    const float xs[4] = {0.0f, SCREEN_W, SCREEN_W, 0.0f};
    const float ys[4] = {0.0f, 0.0f, SCREEN_H, SCREEN_H};
    Renderer2DVertex vertices[4];
    for (int i = 0; i < 4; ++i)
    {
        // (the snapshot is the whole back buffer: only its 16:9 game rectangle is the picture)
        int gx, gy, gw, gh;
        DirectXSystem::GetGameRect(gx, gy, gw, gh);
        const float bw = static_cast<float>(DirectXSystem::GetBackBufferWidth()), bh = static_cast<float>(DirectXSystem::GetBackBufferHeight());
        const float u = (static_cast<float>(gx) + xs[i] / SCREEN_W * static_cast<float>(gw)) / bw;
        const float v = (static_cast<float>(gy) + ys[i] / SCREEN_H * static_cast<float>(gh)) / bh;
        vertices[i] = Renderer2DVertex{UiKit::NX(xs[i] + moveX), UiKit::NY(ys[i] + moveY), 1.0f, 1.0f, 1.0f, fade, u, v};
    }
    const Renderer2DBlendMode previous = Renderer2D_GetBlendMode();
    Renderer2D_SetBlendMode(Renderer2DBlendMode::Alpha);
    Renderer2D_DrawPolygonTextured(RENDERER2D_EXTERNAL_TEXTURE_ID, vertices, 4);
    Renderer2D_SetBlendMode(previous);
}

