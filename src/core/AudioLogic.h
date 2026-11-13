#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The parts of the audio system that do not need the operating system: curves, fades, ducking, the pitch of a chain, the "director" that
// decides which layers of the music play, a small random generator. GameAudio.cpp uses them; tools/audio/test_logic.cpp tests them alone.
namespace AudioLogic
{
constexpr float PI = 3.14159265f;

inline float Clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Slider 0..1 -> amplitude. A square law is much closer to how loudness is felt than a straight line: the middle of the slider is about
// 12 dB down (not 6), so the lower half of the slider is usable too. 0 is silence.
inline float VolumeCurve(float slider)
{
    const float s = Clamp01(slider);
    return s * s;
}

inline float DbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

inline float SemitonesToRatio(float semitones)
{
    return std::pow(2.0f, semitones / 12.0f);
}

// pan -1 (left) .. +1 (right). Constant power: a sound that moves across the screen keeps its loudness. The centre is 0.85 on both sides.
inline void PanGains(float pan, float& left, float& right)
{
    pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    const float angle = (pan + 1.0f) * PI * 0.25f;
    left = std::cos(angle) * 1.2f;
    right = std::sin(angle) * 1.2f;
}

// A value that walks to its target in a given number of steps (one step = one update, 1/60 s). It arrives exactly on the last step.
struct Fader
{
    float value = 0.0f;
    float target = 0.0f;
    float delta = 0.0f;
    int left = 0;

    void Jump(float v)
    {
        value = v;
        target = v;
        delta = 0.0f;
        left = 0;
    }

    void FadeTo(float t, int frames)
    {
        target = t;
        if (frames <= 0 || value == t)
        {
            value = t;
            delta = 0.0f;
            left = 0;
            return;
        }
        left = frames;
        delta = (t - value) / static_cast<float>(frames);
    }

    // returns true while it is still moving
    bool Update()
    {
        if (left <= 0) return false;
        --left;
        value = left == 0 ? target : value + delta;
        return left > 0;
    }
};

// Lowers the music for a moment when a big sound plays: it goes down quickly, stays there for `hold` steps, and comes back slowly.
struct Ducker
{
    static constexpr int RELEASE_FRAMES = 40;
    static constexpr float ATTACK_STEP = 0.25f;

    float gain = 1.0f;
    float target = 1.0f;
    int hold = 0;

    void Trigger(float wantedGain, int holdFrames)
    {
        if (hold <= 0 || wantedGain < target) target = wantedGain;
        if (holdFrames > hold) hold = holdFrames;
    }

    void Update()
    {
        if (hold > 0)
        {
            --hold;
            gain = (std::max)(target, gain - ATTACK_STEP);
        }
        else
        {
            target = 1.0f;
            gain = (std::min)(1.0f, gain + 1.0f / static_cast<float>(RELEASE_FRAMES));
        }
    }
};

// A sound that repeats quickly (the absorb) climbs a pentatonic scale; it starts again from the bottom after a pause.
struct Chain
{
    int count = 0;
    long long last = -1000000;

    int Hit(long long frame, int window, int maxSteps)
    {
        if (frame - last <= window)
            count = (std::min)(count + 1, maxSteps);
        else
            count = 0;
        last = frame;
        return count;
    }
};

// The chain of absorbs climbs D E G A C D (semitones 0 2 5 7 10 12 above the D5 that the absorb sounds are made at): the five notes that EVERY key of the music has in
// common (D dorian, G dorian, A dorian, D aeolian, E aeolian), so a chain is never a wrong note against the score. (It was the major pentatonic, 0 2 4 7 9 12: its F sharp
// is a clash with every minor chord of the music.)
inline float ChainSemitones(int step)
{
    static const float SCALE[6] = {0.0f, 2.0f, 5.0f, 7.0f, 10.0f, 12.0f};
    return SCALE[step < 0 ? 0 : (step > 5 ? 5 : step)];
}

// xorshift: the audio has its own random numbers, so std::rand() of the game (waves, attack choices) is not disturbed by it
struct Rng
{
    uint32_t state = 2463534242u;

    uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float Unit()
    {
        return static_cast<float>(Next() & 0xFFFFFFu) / 16777216.0f;
    }

    float Signed()
    {
        return Unit() * 2.0f - 1.0f;
    }

    // 0 .. count-1, never `avoid` (when there is more than one choice): a track or a variant is not played twice in a row
    int Pick(int count, int avoid)
    {
        if (count <= 1) return 0;
        int v = static_cast<int>(Next() % static_cast<uint32_t>(count));
        if (v == avoid) v = (v + 1) % count;
        return v;
    }
};

// ---- the director: which layers of the music play ---------------------------------------------------------------------------------------
// "Heat" is how much is happening. Every sound the game plays adds a little (a kill, a hit taken, a special shot ...) and it cools down with
// a half life of about 3.3 seconds. The level (0 calm, 1 drive, 2 peak) follows the heat with hysteresis: it needs a clear rise to go up, a
// clear fall to go down, and it stays where it is for at least 6 seconds, so the music does not flicker.
struct Director
{
    static constexpr float COOLING = 0.9965f; // per update step
    static constexpr int DWELL_FRAMES = 360;
    static constexpr float UP_1 = 0.30f, UP_2 = 0.65f, DOWN_2 = 0.40f, DOWN_1 = 0.12f;

    float heat = 0.0f;
    int level = 0;
    int dwell = 0;

    void Add(float weight)
    {
        heat = (std::min)(1.5f, heat + weight);
    }

    void Update(int minLevel)
    {
        heat *= COOLING;
        if (dwell > 0) --dwell;
        int target = level;
        if (level < 1 && heat >= UP_1) target = 1;
        if (level < 2 && heat >= UP_2) target = 2;
        if (target > level)
        {
            level = target;
            dwell = DWELL_FRAMES;
        }
        else if (dwell == 0)
        {
            if (level == 2 && heat < DOWN_2)
            {
                level = 1;
                dwell = DWELL_FRAMES;
            }
            else if (level == 1 && heat < DOWN_1)
            {
                level = 0;
                dwell = DWELL_FRAMES;
            }
        }
        if (level < minLevel) level = minLevel;
    }
};

// The phase of the boss fight. The game does not have to tell it: the sound "boss phase" is played at every phase change, and the first few
// seconds of a fight are ignored (the first phase may announce itself with the same sound). SetHint() is the exact way: after it was called
// once, the counting of sounds is switched off.
struct BossStage
{
    static constexpr int IGNORE_FRAMES = 720;

    int stage = 0; // 0 = phase 1, 1 = phase 2, 2 = phase 3
    int frames = 0;
    bool hinted = false;

    void Start()
    {
        stage = 0;
        frames = 0;
        hinted = false;
    }

    void Update()
    {
        if (frames < 1000000) ++frames;
    }

    void OnPhaseSound()
    {
        if (hinted || frames < IGNORE_FRAMES) return;
        stage = (std::min)(2, stage + 1);
    }

    void SetHint(int phase)
    {
        hinted = true;
        stage = phase < 1 ? 0 : (phase > 3 ? 2 : phase - 1);
    }
};

// Where each layer of a track should be (0..1). Layer 0 is the foundation and always plays. `boss`: layers are phases (see music.py).
inline void LayerTargets(int layers, bool boss, int bossStage, int level, bool pick, float* out)
{
    for (int i = 0; i < layers; ++i)
        out[i] = 0.0f;
    if (layers <= 0) return;
    out[0] = 1.0f;
    if (pick) return; // the card screen: only the foundation, the rest of the music holds its breath
    if (boss)
    {
        const int open = (std::min)(layers - 1, 1 + bossStage); // phase 1: layers 0, 1; phase 2: + 2; phase 3: + 3
        for (int i = 1; i <= open; ++i)
            out[i] = 1.0f;
        if (open + 1 < layers && level >= 2) out[open + 1] = 0.4f; // a taste of the next phase when it gets hot
        return;
    }
    for (int i = 1; i < layers; ++i)
        out[i] = i <= level ? 1.0f : 0.0f;
}

// ---- the two faces of the music (version 4) -----------------------------------------------------------------------------------------------
// A battle track has a BLUE and a RED face: two layers (stems 1 and 2) that play the same notes in the voices of the two colours (glass / bells / electric piano against a
// driven saw / brass / clap). The engine cross-fades them with the colour of the ship. The colour is not followed at once: the mix is a one-pole low-pass of the colour (time
// constant TAU_STEPS), so a player who switches colour quickly hears both faces in about equal measure instead of a music that jumps, and a player who stays in one colour
// hears that face take over in about two seconds. The face that is not the colour of the ship never goes under DEPTH: a switch is a change of face, not a change of music.
struct PolarityMix
{
    static constexpr float TAU_STEPS = 72.0f; // 1.2 s
    static constexpr float DEPTH = 0.35f;     // the weight of the other face, at the least

    float mix = 0.0f; // 0 = blue, 1 = red

    void Jump(bool red)
    {
        mix = red ? 1.0f : 0.0f;
    }

    void Update(bool red)
    {
        const float target = red ? 1.0f : 0.0f;
        mix += (target - mix) * (1.0f - std::exp(-1.0f / TAU_STEPS));
        mix = Clamp01(mix);
    }

    static float FaceGain(float weight)
    {
        return DEPTH + (1.0f - DEPTH) * std::sqrt(Clamp01(weight)); // equal power: both faces at 0.81 in the middle of a cross-fade
    }

    float BlueGain() const
    {
        return FaceGain(1.0f - mix);
    }

    float RedGain() const
    {
        return FaceGain(mix);
    }
};

// Where each layer of a track with two faces should be (0..1). Layer 0 is the foundation and always plays. Layers 1 and 2 are the faces (the engine weights them by the
// colour afterwards, PolarityMix); they play from level 1 on, layer 3 (the peak) from level 2 on. `alwaysOpen` (the title): everything plays all the time. The card screen:
// only the foundation, as with every track.
inline void PolarLayerTargets(int layers, int level, bool pick, bool alwaysOpen, float* out)
{
    for (int i = 0; i < layers; ++i)
        out[i] = 0.0f;
    if (layers <= 0) return;
    out[0] = 1.0f;
    if (pick) return;
    for (int i = 1; i < layers; ++i)
    {
        const int needed = i <= 2 ? 1 : 2;
        out[i] = (alwaysOpen || level >= needed) ? 1.0f : 0.0f;
    }
}

// ======================================================================================================================================
// Version 2 of the audio system: priorities and voice budget, the clock of the music (bars and beats), quantized actions, the heartbeat, the
// density of the mix and a look-ahead limiter. Everything here is plain C++ (no operating system): tools/audio/test_audio_logic.cpp tests it.
// ======================================================================================================================================

// ---- priorities: what may be dropped when too much happens at once ---------------------------------------------------------------------
// Critical: the player's hurt, the boss's phase, the alarms - always plays. High: the cues that tell the player what to do. Normal: the body of the
// fight. Low: texture (the player's shots, the hits on a scout): dropped first.
enum class Priority
{
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3
};

// How many sounds may START in one update step (a step is 1/60 s): a burst of 14 absorbs is not 14 sounds. A sound of a lower priority needs more room.
struct StepBudget
{
    int used[4] = {0, 0, 0, 0};

    void Reset()
    {
        used[0] = used[1] = used[2] = used[3] = 0;
    }

    int Total() const
    {
        return used[0] + used[1] + used[2] + used[3];
    }

    bool Allow(Priority p) const
    {
        const int total = Total();
        switch (p)
        {
        case Priority::Critical: return true;
        case Priority::High: return total < 14;
        case Priority::Normal: return total < 10 && used[2] < 7;
        default: return total < 7 && used[3] < 4;
        }
    }

    void Note(Priority p)
    {
        ++used[static_cast<int>(p)];
    }
};

// When many voices play at once, the body of the fight (not the cues) steps back a little, so that the sum does not get loud and muddy. 1 = untouched.
inline float DensityGain(int activeVoices)
{
    if (activeVoices <= 10) return 1.0f;
    const float g = 1.0f / (1.0f + 0.035f * static_cast<float>(activeVoices - 10));
    return g < 0.60f ? 0.60f : g;
}

// ---- the clock of the music ---------------------------------------------------------------------------------------------------------------
// A layer of a track is a file of `length` frames, played from the start once and then looped from `loopBegin`. `played` is what XAudio2 reports
// (the frames that went through the voice since it started). Where in the FILE are we? The bars and the beats are where they are in the file.
inline long long PositionInFile(unsigned long long played, unsigned int loopBegin, unsigned int length)
{
    if (length == 0) return static_cast<long long>(played);
    if (played < loopBegin) return static_cast<long long>(played);
    if (loopBegin >= length) return static_cast<long long>(played % length);
    const unsigned long long loopLength = length - loopBegin;
    return static_cast<long long>(loopBegin + (played - loopBegin) % loopLength);
}

inline double SamplesPerBeat(int sampleRate, float bpm)
{
    return bpm > 1.0f ? static_cast<double>(sampleRate) * 60.0 / static_cast<double>(bpm) : 0.0;
}

// frames until the next line of a grid that has `samplesPerGrid` frames between its lines (0 when we stand on a line)
inline double SamplesToNextGrid(long long position, double samplesPerGrid)
{
    if (samplesPerGrid <= 0.0) return 0.0;
    const double r = std::fmod(static_cast<double>(position), samplesPerGrid);
    return r < 0.5 ? 0.0 : samplesPerGrid - r;
}

// How many update steps to wait so that an action lands on a line of the grid. `gridBeats`: 4 = the next bar, 1 = the next beat, 0.5 = the next half
// beat. When the preferred grid is too far (more than maxSteps), the next finer one is tried (a bar -> two beats -> a beat -> a half beat ...); when even
// the finest is too far, or the track has no tempo, the answer is 0: do it now. The result is rounded to the nearest step.
inline int QuantizedWaitSteps(long long position, double samplesPerBeat, int sampleRate, double gridBeats, int maxSteps)
{
    if (samplesPerBeat <= 0.0 || sampleRate <= 0 || gridBeats <= 0.0) return 0;
    const double stepSamples = static_cast<double>(sampleRate) / 60.0;
    for (double g = gridBeats; g >= 0.249; g *= 0.5)
    {
        const double toLine = SamplesToNextGrid(position, samplesPerBeat * g);
        const int steps = static_cast<int>(std::floor(toLine / stepSamples + 0.5));
        if (steps <= maxSteps) return steps < 0 ? 0 : steps;
    }
    return 0;
}

// ---- the heartbeat: the ship is nearly lost ---------------------------------------------------------------------------------------------------
// It starts when the HP falls under 34 % and ends above 42 % (hysteresis: it does not flicker at the edge). The nearer to zero, the faster and the louder:
// 68 steps between two beats at the threshold (about 53 per minute), 34 at the end (106). Update() is called once per step; it returns true on the step in
// which a beat has to be played, `level` (0..1) is how loud.
struct Heartbeat
{
    static constexpr float ON_BELOW = 0.34f;
    static constexpr float OFF_ABOVE = 0.42f;

    bool on = false;
    int timer = 0;
    float level = 0.0f;

    bool Update(float hpRate, bool allowed)
    {
        if (!allowed)
        {
            on = false;
            timer = 0;
            level = 0.0f;
            return false;
        }
        if (!on && hpRate < ON_BELOW)
        {
            on = true;
            timer = 0;
        }
        else if (on && hpRate > OFF_ABOVE)
        {
            on = false;
        }
        if (!on)
        {
            level = 0.0f;
            return false;
        }
        const float d = Clamp01((ON_BELOW - hpRate) / ON_BELOW);
        level = 0.35f + 0.65f * d;
        if (timer > 0)
        {
            --timer;
            return false;
        }
        timer = static_cast<int>(std::floor(68.0f - 34.0f * d + 0.5f));
        return true;
    }
};

// how much the music is muffled and lowered by the danger (0..1) of a low HP: nothing above 35 %, the most at 0
inline float DangerAmount(float hpRate, bool alive)
{
    if (!alive) return 0.0f;
    return Clamp01((0.35f - hpRate) / 0.35f);
}

// ---- the limiter -----------------------------------------------------------------------------------------------------------------------------------
// A look-ahead peak limiter for the output of the game: the sum of many voices must never go over the ceiling (a clipped sum sounds harsh, and the
// louder the fight the harsher). The signal is delayed by `lookahead` frames; in that time the gain falls (a ramp as long as the look-ahead) so that it
// is exactly low enough when the peak arrives, and it comes back slowly (the release). Under the ceiling the signal passes untouched (gain 1).
//   in the engine: an XAPO on the mastering voice (GameAudio.cpp, GAME_AUDIO_LIMITER); the test: tools/audio/test_audio_logic.cpp
class LookaheadLimiter
{
public:
    static constexpr int MAX_CHANNELS = 8;
    static constexpr int MAX_LOOKAHEAD = 256;

    void Configure(int channels, int sampleRate, float ceiling = 0.97f, float lookaheadMs = 1.5f, float releaseMs = 90.0f)
    {
        channels_ = channels < 1 ? 1 : (channels > MAX_CHANNELS ? MAX_CHANNELS : channels);
        ceiling_ = ceiling;
        look_ = static_cast<int>(std::floor(lookaheadMs * 0.001f * static_cast<float>(sampleRate) + 0.5f));
        if (look_ < 8) look_ = 8;
        if (look_ > MAX_LOOKAHEAD) look_ = MAX_LOOKAHEAD;
        release_ = 1.0f - std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(sampleRate)));
        Reset();
    }

    void Reset()
    {
        for (int i = 0; i <= MAX_LOOKAHEAD; ++i)
        {
            req_[i] = 1.0f;
            min_[i] = 1.0f;
            for (int c = 0; c < MAX_CHANNELS; ++c)
                delay_[i][c] = 0.0f;
        }
        index_ = 0;
        dIndex_ = 0;
        sum_ = static_cast<double>(look_ + 1);
        gain_ = 1.0f;
        lowest_ = 1.0f;
    }

    int Latency() const
    {
        return look_;
    }

    // the lowest gain since the last call to TakeLowestGain() (1 = the limiter did nothing): for the debug line / the tests
    float TakeLowestGain()
    {
        const float g = lowest_;
        lowest_ = 1.0f;
        return g;
    }

    // `samples`: interleaved, `frames` frames of `channels` channels, processed in place
    void Process(float* samples, int frames)
    {
        const int ch = channels_;
        const int ring = look_ + 1;
        for (int n = 0; n < frames; ++n)
        {
            float* frame = samples + static_cast<size_t>(n) * static_cast<size_t>(ch);
            float peak = 0.0f;
            for (int c = 0; c < ch; ++c)
            {
                const float a = std::fabs(frame[c]);
                if (a > peak) peak = a;
            }
            const float req = peak > ceiling_ ? ceiling_ / peak : 1.0f;
            req_[index_] = req;
            float m = 1.0f; // the lowest requirement of the last look+1 frames (this one included)
            for (int k = 0; k < ring; ++k)
                if (req_[k] < m) m = req_[k];
            sum_ += static_cast<double>(m) - static_cast<double>(min_[index_]);
            min_[index_] = m;
            const float average = static_cast<float>(sum_ / static_cast<double>(ring));
            if (average < gain_)
                gain_ = average;
            else
                gain_ += (average - gain_) * release_;
            if (gain_ < lowest_) lowest_ = gain_;
            // the output is the frame of exactly `look_` frames ago: its requirement is inside the window that the gain was averaged over
            for (int c = 0; c < ch; ++c)
            {
                float out = delay_[dIndex_][c] * gain_;
                if (out > ceiling_) out = ceiling_;
                if (out < -ceiling_) out = -ceiling_;
                delay_[dIndex_][c] = frame[c];
                frame[c] = out;
            }
            if (++index_ >= ring) index_ = 0;
            if (++dIndex_ >= look_) dIndex_ = 0;
        }
    }

private:
    int channels_ = 2;
    int look_ = 66;
    float ceiling_ = 0.97f;
    float release_ = 0.0005f;
    float gain_ = 1.0f;
    float lowest_ = 1.0f;
    int index_ = 0;  // in the rings of the requirement (look + 1 frames)
    int dIndex_ = 0; // in the delay of the signal (look frames)
    double sum_ = 0.0;
    float req_[MAX_LOOKAHEAD + 1];
    float min_[MAX_LOOKAHEAD + 1];
    float delay_[MAX_LOOKAHEAD + 1][MAX_CHANNELS];
};
}


