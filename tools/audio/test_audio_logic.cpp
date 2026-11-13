// Tests of the plain-C++ part of the audio system (AudioLogic.h, AudioMap.h): no operating system, no sound card.
//   g++ -std=c++17 -O2 -I../../src test_audio_logic.cpp -o test_audio_logic && ./test_audio_logic
#include "core/AudioLogic.h"
#include "core/AudioMap.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace AudioLogic;

static int g_Failed = 0;
#define CHECK(cond)                                                                                                      \
    do                                                                                                                   \
    {                                                                                                                    \
        if (!(cond))                                                                                                     \
        {                                                                                                                \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);                                                         \
            ++g_Failed;                                                                                                  \
        }                                                                                                                \
    } while (0)

static void TestClock()
{
    // a track of 1,436,400 frames with a loop that starts at 0
    CHECK(PositionInFile(0, 0, 1436400) == 0);
    CHECK(PositionInFile(1436400, 0, 1436400) == 0);
    CHECK(PositionInFile(1436400 + 7, 0, 1436400) == 7);
    // an intro of 1000 frames that plays once, then the loop of 9000
    CHECK(PositionInFile(500, 1000, 10000) == 500);
    CHECK(PositionInFile(10000, 1000, 10000) == 1000);
    CHECK(PositionInFile(10000 + 9000 + 5, 1000, 10000) == 1005);
    // 140 BPM at 44.1 kHz: a beat is 18900 frames, a bar 75600
    const double spb = SamplesPerBeat(44100, 140.0f);
    CHECK(std::fabs(spb - 18900.0) < 1e-6);
    CHECK(SamplesToNextGrid(0, spb * 4) == 0.0);
    CHECK(std::fabs(SamplesToNextGrid(1, spb * 4) - (75600.0 - 1.0)) < 1e-6);
    CHECK(std::fabs(SamplesToNextGrid(75600 - 100, spb * 4) - 100.0) < 1e-6);
    // 1436400 frames are exactly 19 bars of 140 BPM
    CHECK(std::fabs(1436400.0 / (spb * 4) - 19.0) < 1e-9);
}

static void TestQuantize()
{
    const double spb = SamplesPerBeat(44100, 140.0f); // 18900
    const double stepSamples = 44100.0 / 60.0;        // 735
    // on a line: no wait
    CHECK(QuantizedWaitSteps(0, spb, 44100, 4, 130) == 0);
    // 1000 frames after a bar line, a bar is too far (more than 130 steps = 2.17 s?): 74600 / 735 = 101 steps: fits in 130
    CHECK(QuantizedWaitSteps(1000, spb, 44100, 4, 130) == static_cast<int>(std::floor((75600.0 - 1000.0) / stepSamples + 0.5)));
    // a short limit falls back to a finer grid: the next beat at most 18900 frames = 26 steps
    const int w = QuantizedWaitSteps(1000, spb, 44100, 4, 30);
    CHECK(w > 0 && w <= 30);
    // never later than the limit, whatever the position
    for (long long pos = 0; pos < 300000; pos += 977)
    {
        const int s = QuantizedWaitSteps(pos, spb, 44100, 1, 20);
        CHECK(s >= 0 && s <= 20);
        // the action lands on a line of the beat or half beat grid (within a step)
        const double landed = static_cast<double>(pos) + s * stepSamples;
        const double toBeat = std::fmod(landed, spb * 0.25);
        const double err = std::fmin(toBeat, spb * 0.25 - toBeat);
        if (s > 0) CHECK(err <= stepSamples * 0.51 + 1.0);
    }
    // no tempo: at once
    CHECK(QuantizedWaitSteps(12345, 0.0, 44100, 4, 100) == 0);
}

static void TestHeartbeat()
{
    Heartbeat h;
    int beats = 0;
    for (int i = 0; i < 600; ++i) beats += h.Update(0.8f, true) ? 1 : 0;
    CHECK(beats == 0 && h.level == 0.0f);
    // under 34 %: it starts at once, and the beats are 68 steps apart at the threshold
    std::vector<int> at;
    for (int i = 0; i < 400; ++i)
        if (h.Update(0.33f, true)) at.push_back(i);
    CHECK(at.size() >= 5);
    CHECK(at[0] == 0);
    for (size_t i = 1; i < at.size(); ++i) CHECK(at[i] - at[i - 1] >= 66 && at[i] - at[i - 1] <= 68);
    // nearly dead: faster and louder
    Heartbeat d;
    std::vector<int> at2;
    for (int i = 0; i < 200; ++i)
        if (d.Update(0.02f, true)) at2.push_back(i);
    CHECK(at2.size() >= 5);
    CHECK(at2[2] - at2[1] <= 38 && at2[2] - at2[1] >= 34);
    CHECK(d.level > 0.9f);
    // hysteresis: at 38 % it goes on, at 45 % it stops
    Heartbeat e;
    e.Update(0.30f, true);
    e.Update(0.38f, true);
    CHECK(e.on);
    e.Update(0.45f, true);
    CHECK(!e.on);
    // not allowed (paused, dead): silent
    Heartbeat f;
    CHECK(!f.Update(0.1f, false) && !f.on);
    CHECK(DangerAmount(0.5f, true) == 0.0f);
    CHECK(DangerAmount(0.0f, true) == 1.0f);
    CHECK(DangerAmount(0.0f, false) == 0.0f);
}

static void TestBudget()
{
    StepBudget b;
    for (int i = 0; i < 50; ++i)
    {
        if (b.Allow(Priority::Low)) b.Note(Priority::Low);
    }
    CHECK(b.used[3] == 4); // the texture stops at 4 in a step
    for (int i = 0; i < 50; ++i)
    {
        if (b.Allow(Priority::Normal)) b.Note(Priority::Normal);
    }
    CHECK(b.used[2] >= 1 && b.Total() <= 10);
    // a warning always plays, however full the step is
    for (int i = 0; i < 30; ++i) b.Note(Priority::High);
    CHECK(!b.Allow(Priority::High));
    CHECK(b.Allow(Priority::Critical));
    CHECK(DensityGain(5) == 1.0f && DensityGain(10) == 1.0f);
    CHECK(DensityGain(20) < 0.8f && DensityGain(20) > 0.7f);
    CHECK(DensityGain(500) == 0.60f);
}

static void TestMap()
{
    // every sound has a bus and a priority, the warnings are alerts, a cue that ducks is never on a bus that it ducks
    for (int i = 0; i < static_cast<int>(GameSound::Count); ++i)
    {
        const GameSound s = static_cast<GameSound>(i);
        const int bus = static_cast<int>(AudioMap::BusOf(s));
        CHECK(bus >= 0 && bus < static_cast<int>(AudioMap::Bus::Count));
        const int pr = static_cast<int>(AudioMap::PriorityOf(s));
        CHECK(pr >= 0 && pr <= 3);
        if (AudioMap::CueDuckPlayerDb(s) < 0.0f || AudioMap::CueDuckWorldDb(s) < 0.0f) CHECK(AudioMap::BusOf(s) == AudioMap::Bus::Alert);
    }
    CHECK(AudioMap::BusOf(GameSound::WhiteWarn) == AudioMap::Bus::Alert);
    CHECK(AudioMap::PriorityOf(GameSound::WhiteWarn) == AudioLogic::Priority::Critical);
    CHECK(AudioMap::PriorityOf(GameSound::PlayerShotBlue) == AudioLogic::Priority::Low);
    CHECK(AudioMap::PriorityOf(GameSound::PlayerShotRed) == AudioLogic::Priority::Low);
    CHECK(AudioMap::PriorityOf(GameSound::SniperLock) == AudioLogic::Priority::High);
    CHECK(AudioMap::BusOf(GameSound::SniperLock) == AudioMap::Bus::Alert);
    CHECK(AudioMap::BusOf(GameSound::ChargerWarning) == AudioMap::Bus::Alert);
    CHECK(AudioMap::BusOf(GameSound::MenuCursor) == AudioMap::Bus::Ui);
    CHECK(AudioMap::BusOf(GameSound::EnemyHitBlue) == AudioMap::Bus::World);
    CHECK(AudioMap::BusOf(GameSound::EnemyHitRed) == AudioMap::Bus::World);
    CHECK(AudioMap::BusOf(GameSound::CutInWave) == AudioMap::Bus::Ui);
}

// ---- the two faces of the music -----------------------------------------------------------------------------------------------------------------------
static void TestPolarity()
{
    PolarityMix m;
    CHECK(m.mix == 0.0f);
    CHECK(std::fabs(m.BlueGain() - 1.0f) < 1e-5f);                       // blue: the blue face at full, the red one at its depth
    CHECK(std::fabs(m.RedGain() - PolarityMix::DEPTH) < 1e-5f);
    // the colour changes: the red face takes over in about two seconds and is not there at once
    for (int i = 0; i < 12; ++i) m.Update(true);
    CHECK(m.mix > 0.10f && m.mix < 0.30f);
    for (int i = 0; i < 120; ++i) m.Update(true);
    CHECK(m.mix > 0.80f && m.RedGain() > m.BlueGain());
    for (int i = 0; i < 600; ++i) m.Update(true);
    CHECK(m.mix > 0.999f && std::fabs(m.RedGain() - 1.0f) < 1e-3f);
    // equal power in the middle: both faces at 0.81, and the sum of the powers never goes more than 1.3 dB over the one face
    for (int k = 0; k <= 20; ++k)
    {
        m.mix = k / 20.0f;
        const float b = m.BlueGain(), r = m.RedGain();
        CHECK(b >= PolarityMix::DEPTH - 1e-5f && b <= 1.0f + 1e-5f && r >= PolarityMix::DEPTH - 1e-5f && r <= 1.0f + 1e-5f);
        CHECK(b * b + r * r <= 1.40f && b * b + r * r >= 0.99f);
    }
    m.mix = 0.5f;
    CHECK(std::fabs(m.BlueGain() - m.RedGain()) < 1e-5f && std::fabs(m.BlueGain() - 0.8115f) < 0.002f);
    // a player who switches all the time (every 1/3 s) hears both faces in about equal measure
    m.Jump(false);
    float lo = 1.0f, hi = 0.0f;
    bool red = false;
    for (int i = 0; i < 900; ++i)
    {
        if (i % 20 == 0) red = !red;
        m.Update(red);
        if (i > 300)
        {
            lo = std::fmin(lo, m.mix);
            hi = std::fmax(hi, m.mix);
        }
    }
    CHECK(lo > 0.30f && hi < 0.70f);
    CHECK(std::fabs(m.BlueGain() - m.RedGain()) < 0.25f);
    // the layers of a track with two faces
    float t[4];
    PolarLayerTargets(4, 0, false, false, t);
    CHECK(t[0] == 1.0f && t[1] == 0.0f && t[2] == 0.0f && t[3] == 0.0f);   // calm: the foundation
    PolarLayerTargets(4, 1, false, false, t);
    CHECK(t[0] == 1.0f && t[1] == 1.0f && t[2] == 1.0f && t[3] == 0.0f);   // drive: both faces (the engine weights them)
    PolarLayerTargets(4, 2, false, false, t);
    CHECK(t[0] == 1.0f && t[1] == 1.0f && t[2] == 1.0f && t[3] == 1.0f);   // peak
    PolarLayerTargets(4, 2, true, false, t);
    CHECK(t[0] == 1.0f && t[1] == 0.0f && t[2] == 0.0f && t[3] == 0.0f);   // the card screen: the foundation only
    PolarLayerTargets(3, 0, false, true, t);
    CHECK(t[0] == 1.0f && t[1] == 1.0f && t[2] == 1.0f);                   // the title: everything, all the time
    PolarLayerTargets(3, 0, true, true, t);
    CHECK(t[0] == 1.0f && t[1] == 0.0f && t[2] == 0.0f);
}

// ---- the limiter ------------------------------------------------------------------------------------------------------------------------------------
static float Peak(const std::vector<float>& v)
{
    float p = 0.0f;
    for (float x : v) p = std::fmax(p, std::fabs(x));
    return p;
}

static void TestLimiter()
{
    const int rate = 48000;
    LookaheadLimiter lim;
    lim.Configure(2, rate);
    const int L = lim.Latency();
    // 1. under the ceiling: untouched (only delayed)
    {
        std::vector<float> in(rate), out;
        for (int i = 0; i < rate / 2; ++i)
        {
            in[static_cast<size_t>(i) * 2] = 0.5f * std::sin(6.2831853f * 440.0f * static_cast<float>(i) / rate);
            in[static_cast<size_t>(i) * 2 + 1] = 0.4f * std::sin(6.2831853f * 660.0f * static_cast<float>(i) / rate);
        }
        out = in;
        lim.Process(out.data(), rate / 2);
        double err = 0.0;
        for (int i = L; i < rate / 2; ++i)
        {
            err = std::fmax(err, std::fabs(out[static_cast<size_t>(i) * 2] - in[static_cast<size_t>(i - L) * 2]));
            err = std::fmax(err, std::fabs(out[static_cast<size_t>(i) * 2 + 1] - in[static_cast<size_t>(i - L) * 2 + 1]));
        }
        CHECK(err == 0.0);
        CHECK(lim.TakeLowestGain() == 1.0f);
    }
    // 2. far over the ceiling (a sum of 8 voices): the output never goes over
    {
        lim.Configure(2, rate);
        std::vector<float> in(static_cast<size_t>(rate) * 2);
        unsigned s = 1;
        for (int i = 0; i < rate; ++i)
        {
            s = s * 1664525u + 1013904223u;
            const float noise = (static_cast<float>(s >> 8) / 16777216.0f - 0.5f);
            const float burst = (i > 12000 && i < 14000) ? 3.0f : 1.0f;
            in[static_cast<size_t>(i) * 2] = burst * (1.6f * std::sin(6.2831853f * 220.0f * static_cast<float>(i) / rate) + noise);
            in[static_cast<size_t>(i) * 2 + 1] = burst * 1.3f * std::sin(6.2831853f * 330.0f * static_cast<float>(i) / rate);
        }
        std::vector<float> out = in;
        // in blocks of 480 frames, as the engine does
        for (int pos = 0; pos < rate; pos += 480) lim.Process(out.data() + static_cast<size_t>(pos) * 2, 480);
        CHECK(Peak(out) <= 0.97f + 1e-6f);
        CHECK(lim.TakeLowestGain() < 0.5f);
        // it is not a hard clipper: the gain is a smooth curve (it changes by at most 1.5 % per frame, where a clipper jumps), so the wave keeps its shape
        double worst = 0.0, previous = -1.0;
        for (int i = L + 100; i < rate - 100; ++i)
        {
            const double x = in[static_cast<size_t>(i - L) * 2 + 1], y = out[static_cast<size_t>(i) * 2 + 1];
            if (std::fabs(x) < 0.4) continue;
            const double g = y / x;
            if (previous >= 0.0 && i > 0) worst = std::fmax(worst, std::fabs(g - previous));
            previous = g;
        }
        std::printf("largest step of the gain between two samples that are compared: %.4f\n", worst);
        CHECK(worst < 0.06); // (samples are not adjacent when |x| is small: allow for a few frames between two compared ones)
    }
    // 3. the gain comes back after a peak (the release): a quiet tone after a loud click is not left lowered
    {
        lim.Configure(2, rate);
        std::vector<float> in(static_cast<size_t>(rate) * 2, 0.0f);
        for (int i = 0; i < rate; ++i)
        {
            const float tone = 0.3f * std::sin(6.2831853f * 500.0f * static_cast<float>(i) / rate);
            in[static_cast<size_t>(i) * 2] = tone;
            in[static_cast<size_t>(i) * 2 + 1] = tone;
        }
        in[20000 * 2] = 6.0f; // one loud sample
        std::vector<float> out = in;
        lim.Process(out.data(), rate);
        double ratio = 0.0;
        int n = 0;
        for (int i = rate - 4000; i < rate; ++i)
            if (std::fabs(in[static_cast<size_t>(i - L) * 2]) > 0.05f)
            {
                ratio += out[static_cast<size_t>(i) * 2] / in[static_cast<size_t>(i - L) * 2];
                ++n;
            }
        CHECK(n > 0 && std::fabs(ratio / n - 1.0) < 5e-3);
    }
    // 4. more channels
    {
        LookaheadLimiter l6;
        l6.Configure(6, rate);
        std::vector<float> in(static_cast<size_t>(6000) * 6);
        for (size_t i = 0; i < in.size(); ++i) in[i] = (i % 7 == 0 ? 2.5f : -1.2f) * std::sin(static_cast<float>(i) * 0.1f);
        l6.Process(in.data(), 6000);
        CHECK(Peak(in) <= 0.97f + 1e-6f);
    }
}

// The chain of absorbs climbs the five notes that every key of the music has (D E G A C: 0 2 5 7 10 above the D of the absorb sounds), then the octave.
static void TestChainScale()
{
    const float expect[6] = {0.0f, 2.0f, 5.0f, 7.0f, 10.0f, 12.0f};
    for (int i = 0; i < 6; ++i) CHECK(ChainSemitones(i) == expect[i]);
    CHECK(ChainSemitones(-3) == 0.0f && ChainSemitones(99) == 12.0f);
    for (int i = 0; i < 6; ++i)
    {
        const int pc = (static_cast<int>(ChainSemitones(i)) + 2) % 12; // pitch class of the note above D (D = 2)
        CHECK(pc == 2 || pc == 4 || pc == 7 || pc == 9 || pc == 0);      // D E G A C
    }
}

int main()
{
    TestChainScale();
    TestClock();
    TestQuantize();
    TestHeartbeat();
    TestBudget();
    TestMap();
    TestPolarity();
    TestLimiter();
    if (g_Failed == 0) std::printf("audio logic: all tests passed\n");
    return g_Failed == 0 ? 0 : 1;
}


