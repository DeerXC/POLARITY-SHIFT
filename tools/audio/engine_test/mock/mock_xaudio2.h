// Control and inspection of the mock engine (mock_xaudio2.cpp)
#pragma once
#include <xaudio2.h>
#include <xapobase.h>
#include <string>
#include <vector>

namespace MockXAudio2
{
enum class Kind { Source, Submix, Master };

struct Send
{
    VoiceImpl* dest = nullptr;
    std::vector<float> matrix; // [srcCh * dstCh]
    bool custom = false;
};

struct SubmitLog
{
    long long step = 0;
    int voice = 0;
    unsigned bytes = 0;
    bool loop = false;
    int destVoice = -1; // the voice of the last SetOutputMatrix
    float volumeAtStart = 0.0f;
};

struct VoiceImpl
{
    int id = 0;
    Kind kind = Kind::Source;
    IXAudio2Voice* api = nullptr;
    unsigned channels = 1;
    unsigned rate = 44100;
    unsigned stage = 0;
    float volume = 1.0f;
    float ratio = 1.0f;
    bool started = false;
    bool hasBuffer = false;
    bool finished = false;
    const unsigned char* data = nullptr;
    unsigned bytes = 0;
    unsigned frames = 0;
    unsigned loopBegin = 0, loopLength = 0, loopCount = 0;
    double position = 0.0;   // in frames of the file
    double played = 0.0;     // what GetState reports
    std::vector<Send> sends;
    float filterFrequency = 1.0f;
    IXAPO* limiter = nullptr;
    std::vector<float> in;   // the sum of what is sent to this voice in the step
    float peakIn = 0.0f;
    float lastReverbSend = -1.0f;
    int lastDest = -1;
    bool alive = true;
    bool isReverb = false;
};

constexpr int STEP_FRAMES = 800; // 48000 / 60

extern std::vector<VoiceImpl*> g_Voices;
extern std::vector<float> g_Output;       // the final stereo mix, 48000 Hz, interleaved
extern std::vector<SubmitLog> g_Submits;
extern long long g_Step;
extern float g_LimiterLowest;             // the lowest gain of the limiter since the last read

void Reset();
void Advance(int steps);                  // 1/60 s each: mixes and appends to g_Output
VoiceImpl* Impl(IXAudio2Voice* v);
}

