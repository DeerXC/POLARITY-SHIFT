#include "game/ScreenShake.h"
#include "core/GameInput.h"
#include "core/GameSettings.h"
#include "core/Renderer2D.h"

namespace
{
int g_ScreenShakeTimer = 0;
int g_ScreenShakeDuration = 0;
float g_ScreenShakeStrength = 0.0f;
}

namespace ScreenShake
{
void Reset()
{
    g_ScreenShakeTimer = 0;
    g_ScreenShakeDuration = 0;
    g_ScreenShakeStrength = 0.0f;
}

void Start(int duration, float strength)
{
    if (duration <= 0)
    {
        return;
    }

    // the pad shakes with the screen (settings: VIBRATION; its own strength, not the one of SCREEN SHAKE): 0.004 is a tick, 0.028 the hardest blow
    GameInput::Rumble(0.05f + strength * 30.0f, duration + 4);

    strength *= static_cast<float>(GameSettings::Get().shakePercent) / 100.0f; // (settings: SCREEN SHAKE)
    if (strength <= 0.0f)
    {
        return;
    }

    if (g_ScreenShakeTimer > 0 && strength < g_ScreenShakeStrength)
    {
        return;
    }

    g_ScreenShakeTimer = duration;

    g_ScreenShakeDuration = duration;

    g_ScreenShakeStrength = strength;
}

void Update()
{
    if (g_ScreenShakeTimer <= 0)
    {
        g_ScreenShakeTimer = 0;
        g_ScreenShakeDuration = 0;
        g_ScreenShakeStrength = 0.0f;

        return;
    }

    g_ScreenShakeTimer--;
}

void Apply()
{
    if (g_ScreenShakeTimer <= 0 || g_ScreenShakeDuration <= 0)
    {
        Renderer2D_ClearCameraOffset();
        return;
    }

    const float lifeRate = static_cast<float>(g_ScreenShakeTimer) / static_cast<float>(g_ScreenShakeDuration);

    const float currentStrength = g_ScreenShakeStrength * lifeRate;

    const int patternIndex = g_ScreenShakeTimer % 8;

    constexpr float SHAKE_PATTERN_X[8] = {1.00f, -0.75f, 0.55f, -1.00f, 0.72f, -0.42f, 0.25f, -0.15f};

    constexpr float SHAKE_PATTERN_Y[8] = {-0.45f, 0.82f, -1.00f, 0.35f, 0.68f, -0.72f, 0.40f, -0.20f};

    Renderer2D_SetCameraOffset(SHAKE_PATTERN_X[patternIndex] * currentStrength, SHAKE_PATTERN_Y[patternIndex] * currentStrength);
}
}


