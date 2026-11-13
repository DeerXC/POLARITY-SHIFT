#include "core/GameTimer.h"
#include "core/GameSettings.h"
#include <Windows.h>
#include <timeapi.h>
#include <algorithm>
#include <chrono>
#include <thread>

#pragma comment(lib, "winmm.lib")

namespace
{
using Clock = std::chrono::steady_clock;

constexpr double FIXED_DELTA_TIME = 1.0 / 60.0;
constexpr double MAX_FRAME_TIME = 0.25;
constexpr double STATS_WINDOW = 0.5; // seconds

Clock::time_point g_PreviousTime{};
double g_Accumulator = 0.0;
bool g_WorldAdvanced = false;
bool g_BackgroundAdvanced = false;

// statistics
Clock::time_point g_LastFrameBegin{};
Clock::time_point g_FrameBegin{};
bool g_HasLastFrame = false;
int g_WindowFrames = 0;
int g_WindowUpdates = 0;
double g_WindowTime = 0.0;
double g_WindowMax = 0.0;
double g_WindowCpu = 0.0;
GameTimer::FrameStats g_Stats;

double MillisecondsSince(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}
}

void GameTimer::Initialize()
{
    g_PreviousTime = Clock::now();
    g_Accumulator = 0.0;
    g_HasLastFrame = false;
    g_WindowFrames = 0;
    g_WindowUpdates = 0;
    g_WindowTime = 0.0;
    g_WindowMax = 0.0;
    g_WindowCpu = 0.0;
    g_Stats = FrameStats{};
}

void GameTimer::BeginFrame()
{
    const Clock::time_point currentTime = Clock::now();
    double frameTime = std::chrono::duration<double>(currentTime - g_PreviousTime).count();
    g_PreviousTime = currentTime;

    frameTime = std::clamp(frameTime, 0.0, MAX_FRAME_TIME);
    g_Accumulator += frameTime;
}

bool GameTimer::ShouldUpdate()
{
    return g_Accumulator >= FIXED_DELTA_TIME;
}

void GameTimer::ConsumeUpdate()
{
    g_Accumulator -= FIXED_DELTA_TIME;
    if (g_Accumulator < 0.0)
    {
        g_Accumulator = 0.0;
    }
    ++g_WindowUpdates;
}

float GameTimer::RenderAlpha()
{
    const double alpha = g_Accumulator / FIXED_DELTA_TIME;
    return static_cast<float>(alpha < 0.0 ? 0.0 : (alpha > 1.0 ? 1.0 : alpha));
}

void GameTimer::SetWorldAdvanced(bool advanced)
{
    g_WorldAdvanced = advanced;
}

void GameTimer::SetBackgroundAdvanced(bool advanced)
{
    g_BackgroundAdvanced = advanced;
}

float GameTimer::Blend(float previous, float current)
{
    const float alpha = g_WorldAdvanced ? RenderAlpha() : 1.0f;
    return previous + (current - previous) * alpha;
}

float GameTimer::BackgroundAlpha()
{
    return g_BackgroundAdvanced ? RenderAlpha() : 1.0f;
}

void GameTimer::LimitFrameRate()
{
    static Clock::time_point s_Next{};
    static bool s_Timer = false;
    if (!s_Timer)
    {
        timeBeginPeriod(1); // (1 ms sleeps: the default is about 15 ms)
        s_Timer = true;
    }
    const int cap = GameSettings::Get().fpsCap;
    if (cap < 20)
    {
        s_Next = Clock::now(); // no cap: the next capped frame starts from here
        return;
    }
    const auto step = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / static_cast<double>(cap)));
    Clock::time_point now = Clock::now();
    if (s_Next == Clock::time_point{} || now - s_Next > step * 4) s_Next = now; // (a long stall, or the first frame: do not try to catch up)
    s_Next += step;
    for (;;)
    {
        now = Clock::now();
        if (now >= s_Next) break;
        const auto left = s_Next - now;
        if (left > std::chrono::milliseconds(2)) std::this_thread::sleep_for(left - std::chrono::milliseconds(1) - std::chrono::microseconds(500));
        else std::this_thread::yield();
    }
}

void GameTimer::StatsFrameBegin()
{
    const Clock::time_point now = Clock::now();
    if (g_HasLastFrame)
    {
        const double frameMs = MillisecondsSince(g_LastFrameBegin, now);
        ++g_WindowFrames;
        g_WindowTime += frameMs;
        g_WindowMax = (std::max)(g_WindowMax, frameMs);

        if (g_WindowTime >= STATS_WINDOW * 1000.0 && g_WindowFrames > 0)
        {
            g_Stats.fps = static_cast<float>(g_WindowFrames * 1000.0 / g_WindowTime);
            g_Stats.avgMs = static_cast<float>(g_WindowTime / g_WindowFrames);
            g_Stats.maxMs = static_cast<float>(g_WindowMax);
            g_Stats.cpuMs = static_cast<float>(g_WindowCpu / g_WindowFrames);
            g_Stats.updatesPerFrame = static_cast<float>(g_WindowUpdates) / static_cast<float>(g_WindowFrames);
            g_WindowFrames = 0;
            g_WindowUpdates = 0;
            g_WindowTime = 0.0;
            g_WindowMax = 0.0;
            g_WindowCpu = 0.0;
        }
    }
    g_HasLastFrame = true;
    g_LastFrameBegin = now;
    g_FrameBegin = now;
}

void GameTimer::StatsBeforePresent()
{
    g_WindowCpu += MillisecondsSince(g_FrameBegin, Clock::now());
}

void GameTimer::StatsFrameEnd()
{
    // nothing to do: the time between two StatsFrameBegin() calls is the length of a frame (it includes the wait for the display)
}

GameTimer::FrameStats GameTimer::GetStats()
{
    return g_Stats;
}

