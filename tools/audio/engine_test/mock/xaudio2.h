// A stand-in for <xaudio2.h>: the same names and signatures as far as GameAudio.cpp uses them (as I know them from the documentation of XAudio2.9), and a small
// engine behind them (mock_xaudio2.cpp) that really mixes, so that the game's audio code can be run and measured offline.
#pragma once
#include <Windows.h>
#include <cstddef>
#define XAUDIO2_DEFAULT_PROCESSOR 0xffffffff
#define XAUDIO2_END_OF_STREAM 0x0040
#define XAUDIO2_LOOP_INFINITE 255
#define XAUDIO2_VOICE_NOSAMPLESPLAYED 0x0100
#define XAUDIO2_VOICE_USEFILTER 0x0080
#define XAUDIO2_COMMIT_NOW 0
#define XAUDIO2_DEFAULT_FREQ_RATIO 2.0f
enum XAUDIO2_FILTER_TYPE { LowPassFilter = 0 };
struct XAUDIO2_FILTER_PARAMETERS { XAUDIO2_FILTER_TYPE Type; float Frequency; float OneOverQ; };
struct XAUDIO2_VOICE_DETAILS { UINT32 CreationFlags; UINT32 ActiveFlags; UINT32 InputChannels; UINT32 InputSampleRate; };
struct XAUDIO2_VOICE_STATE { void* pCurrentBufferContext; UINT32 BuffersQueued; UINT64 SamplesPlayed; };
struct XAUDIO2_BUFFER { UINT32 Flags; UINT32 AudioBytes; const BYTE* pAudioData; UINT32 PlayBegin; UINT32 PlayLength; UINT32 LoopBegin; UINT32 LoopLength; UINT32 LoopCount; void* pContext; };
class IXAudio2Voice;
struct XAUDIO2_SEND_DESCRIPTOR { UINT32 Flags; IXAudio2Voice* pOutputVoice; };
struct XAUDIO2_VOICE_SENDS { UINT32 SendCount; XAUDIO2_SEND_DESCRIPTOR* pSends; };
struct XAUDIO2_EFFECT_DESCRIPTOR { IUnknown* pEffect; BOOL InitialState; UINT32 OutputChannels; };
struct XAUDIO2_EFFECT_CHAIN { UINT32 EffectCount; XAUDIO2_EFFECT_DESCRIPTOR* pEffectDescriptors; };
class IXAudio2VoiceCallback;
class IXAudio2EngineCallback { public: virtual void OnProcessingPassStart() = 0; virtual void OnProcessingPassEnd() = 0; virtual void OnCriticalError(HRESULT) = 0; };
namespace MockXAudio2 { struct VoiceImpl; }
class IXAudio2Voice {
public:
    MockXAudio2::VoiceImpl* impl = nullptr;
    void GetVoiceDetails(XAUDIO2_VOICE_DETAILS* d);
    void DestroyVoice();
    HRESULT SetOutputMatrix(IXAudio2Voice* dest, UINT32 srcCh, UINT32 dstCh, const float* levels, UINT32 op = XAUDIO2_COMMIT_NOW);
    void SetVolume(float v, UINT32 op = XAUDIO2_COMMIT_NOW);
    HRESULT SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* p, UINT32 op = XAUDIO2_COMMIT_NOW);
    HRESULT SetEffectChain(const XAUDIO2_EFFECT_CHAIN* chain);
    HRESULT SetEffectParameters(UINT32 index, const void* params, UINT32 bytes, UINT32 op = XAUDIO2_COMMIT_NOW);
};
class IXAudio2SourceVoice : public IXAudio2Voice {
public:
    HRESULT Start(UINT32 flags = 0, UINT32 op = XAUDIO2_COMMIT_NOW);
    HRESULT Stop(UINT32 flags = 0, UINT32 op = XAUDIO2_COMMIT_NOW);
    HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER* b, const void* wma = nullptr);
    HRESULT FlushSourceBuffers();
    HRESULT SetFrequencyRatio(float r, UINT32 op = XAUDIO2_COMMIT_NOW);
    void GetState(XAUDIO2_VOICE_STATE* s, UINT32 flags = 0);
};
class IXAudio2SubmixVoice : public IXAudio2Voice {};
class IXAudio2MasteringVoice : public IXAudio2Voice {};
class IXAudio2 {
public:
    HRESULT CreateSourceVoice(IXAudio2SourceVoice** v, const WAVEFORMATEX* f, UINT32 flags = 0, float maxRatio = XAUDIO2_DEFAULT_FREQ_RATIO, IXAudio2VoiceCallback* cb = nullptr, const XAUDIO2_VOICE_SENDS* sends = nullptr, const XAUDIO2_EFFECT_CHAIN* chain = nullptr);
    HRESULT CreateSubmixVoice(IXAudio2SubmixVoice** v, UINT32 ch, UINT32 rate, UINT32 flags = 0, UINT32 stage = 0, const XAUDIO2_VOICE_SENDS* sends = nullptr, const XAUDIO2_EFFECT_CHAIN* chain = nullptr);
    HRESULT CreateMasteringVoice(IXAudio2MasteringVoice** v, UINT32 ch = 0, UINT32 rate = 0, UINT32 flags = 0, const wchar_t* dev = nullptr, const XAUDIO2_EFFECT_CHAIN* chain = nullptr);
    HRESULT StartEngine();
    HRESULT RegisterForCallbacks(IXAudio2EngineCallback*);
    void UnregisterForCallbacks(IXAudio2EngineCallback*);
    void StopEngine();
    HRESULT CommitChanges(UINT32 op);
    ULONG Release();
};
HRESULT XAudio2Create(IXAudio2** pp, UINT32 flags = 0, UINT32 processor = XAUDIO2_DEFAULT_PROCESSOR);

