#include "mock_xaudio2.h"
#include <algorithm>
#include <cmath>

namespace MockXAudio2
{
std::vector<VoiceImpl*> g_Voices;
std::vector<float> g_Output;
std::vector<SubmitLog> g_Submits;
long long g_Step = 0;
float g_LimiterLowest = 1.0f;

VoiceImpl* Impl(IXAudio2Voice* v) { return v ? v->impl : nullptr; }

void Reset()
{
    for (VoiceImpl* v : g_Voices) delete v;
    g_Voices.clear();
    g_Output.clear();
    g_Submits.clear();
    g_Step = 0;
}

static VoiceImpl* NewVoice(Kind kind, unsigned channels, unsigned rate, unsigned stage, const XAUDIO2_VOICE_SENDS* sends)
{
    VoiceImpl* v = new VoiceImpl();
    v->id = static_cast<int>(g_Voices.size());
    v->kind = kind;
    v->channels = channels;
    v->rate = rate;
    v->stage = stage;
    g_Voices.push_back(v);
    if (sends != nullptr)
    {
        for (UINT32 i = 0; i < sends->SendCount; ++i)
        {
            Send s;
            s.dest = Impl(sends->pSends[i].pOutputVoice);
            v->sends.push_back(s);
        }
    }
    return v;
}

// ---- the mixing ----------------------------------------------------------------------------------------------------------------------------
static void DefaultMatrix(unsigned src, unsigned dst, std::vector<float>& m)
{
    m.assign(static_cast<size_t>(src) * dst, 0.0f);
    if (src == dst)
        for (unsigned i = 0; i < src; ++i) m[i * dst + i] = 1.0f;
    else if (src == 1)
        for (unsigned d = 0; d < dst; ++d) m[d] = 1.0f;
}

static void SendTo(VoiceImpl* from, const float* frames, unsigned fromCh, int n)
{
    for (Send& s : from->sends)
    {
        if (s.dest == nullptr) continue;
        if (s.matrix.empty()) DefaultMatrix(fromCh, 2, s.matrix);
        float* dst = s.dest->in.data();
        for (int i = 0; i < n; ++i)
        {
            for (unsigned d = 0; d < 2; ++d)
            {
                float acc = 0.0f;
                for (unsigned c = 0; c < fromCh; ++c) acc += frames[static_cast<size_t>(i) * fromCh + c] * s.matrix[fromCh * d + c];
                dst[static_cast<size_t>(i) * 2 + d] += acc;
            }
        }
    }
}

static float Sample(const VoiceImpl* v, double pos, unsigned ch)
{
    const long long i0 = static_cast<long long>(pos);
    const double frac = pos - static_cast<double>(i0);
    const long long i1 = i0 + 1 < static_cast<long long>(v->frames) ? i0 + 1 : i0;
    if (i0 < 0 || i0 >= static_cast<long long>(v->frames)) return 0.0f;
    const short* pcm = reinterpret_cast<const short*>(v->data);
    const float a = pcm[static_cast<size_t>(i0) * v->channels + ch] / 32768.0f;
    const float b = pcm[static_cast<size_t>(i1) * v->channels + ch] / 32768.0f;
    return static_cast<float>(a + (b - a) * frac);
}

static void MixSource(VoiceImpl* v)
{
    if (!v->started || !v->hasBuffer || v->finished) return;
    const double stepFrames = static_cast<double>(v->rate) * v->ratio / 48000.0; // frames of the file per frame of the bus
    std::vector<float> out(static_cast<size_t>(STEP_FRAMES) * v->channels, 0.0f);
    int produced = 0;
    for (int n = 0; n < STEP_FRAMES; ++n)
    {
        const bool looping = v->loopCount > 0;
        if (!looping && v->position >= v->frames)
        {
            v->finished = true;
            break;
        }
        for (unsigned c = 0; c < v->channels; ++c) out[static_cast<size_t>(n) * v->channels + c] = Sample(v, v->position, c) * v->volume;
        v->position += stepFrames;
        v->played += stepFrames;
        if (looping && v->position >= v->loopBegin + v->loopLength) v->position = v->loopBegin + std::fmod(v->position - v->loopBegin, static_cast<double>(v->loopLength));
        ++produced;
    }
    if (produced > 0) SendTo(v, out.data(), v->channels, STEP_FRAMES);
}

void Advance(int steps)
{
    for (int step = 0; step < steps; ++step)
    {
        ++g_Step;
        for (VoiceImpl* v : g_Voices)
        {
            v->in.assign(static_cast<size_t>(STEP_FRAMES) * 2, 0.0f);
            v->peakIn = 0.0f;
        }
        for (VoiceImpl* v : g_Voices)
            if (v->alive && v->kind == Kind::Source) MixSource(v);
        std::vector<VoiceImpl*> submixes;
        VoiceImpl* master = nullptr;
        for (VoiceImpl* v : g_Voices)
        {
            if (!v->alive) continue;
            if (v->kind == Kind::Submix) submixes.push_back(v);
            if (v->kind == Kind::Master) master = v;
        }
        std::stable_sort(submixes.begin(), submixes.end(), [](const VoiceImpl* a, const VoiceImpl* b) { return a->stage < b->stage; });
        for (VoiceImpl* v : submixes)
        {
            for (float x : v->in) v->peakIn = std::max(v->peakIn, std::fabs(x));
            std::vector<float> out = v->in;
            for (float& x : out) x *= v->volume;
            if (v->isReverb) continue; // (the reverb itself is not simulated: only what is sent to it is measured)
            if (v->sends.empty() && master != nullptr)
            {
                Send s;
                s.dest = master;
                v->sends.push_back(s);
            }
            SendTo(v, out.data(), 2, STEP_FRAMES);
        }
        if (master != nullptr)
        {
            std::vector<float> out = master->in;
            for (float& x : out) x *= master->volume;
            if (master->limiter != nullptr)
            {
                XAPO_PROCESS_BUFFER_PARAMETERS in{out.data(), XAPO_BUFFER_VALID, static_cast<UINT32>(STEP_FRAMES)};
                XAPO_PROCESS_BUFFER_PARAMETERS o{out.data(), XAPO_BUFFER_VALID, static_cast<UINT32>(STEP_FRAMES)};
                master->limiter->Process(1, &in, 1, &o, TRUE);
            }
            g_Output.insert(g_Output.end(), out.begin(), out.end());
        }
        // sources that music / effects started this step: nothing else to do
    }
}
}

// ---- the API ---------------------------------------------------------------------------------------------------------------------------------
using namespace MockXAudio2;

void IXAudio2Voice::GetVoiceDetails(XAUDIO2_VOICE_DETAILS* d)
{
    d->InputChannels = impl->channels;
    d->InputSampleRate = impl->rate;
}
void IXAudio2Voice::DestroyVoice() { if (impl) impl->alive = false; }
HRESULT IXAudio2Voice::SetOutputMatrix(IXAudio2Voice* dest, UINT32 src, UINT32 dst, const float* levels, UINT32)
{
    VoiceImpl* d = Impl(dest);
    for (Send& s : impl->sends)
    {
        if (s.dest == d)
        {
            s.matrix.assign(levels, levels + static_cast<size_t>(src) * dst);
            s.custom = true;
            impl->lastDest = d ? d->id : -1;
            if (d && d->isReverb) impl->lastReverbSend = levels[0];
            return S_OK;
        }
    }
    return E_FAIL; // (a send to a voice that is not in the send list: XAudio2 refuses it, too)
}
void IXAudio2Voice::SetVolume(float v, UINT32) { impl->volume = v; }
HRESULT IXAudio2Voice::SetFilterParameters(const XAUDIO2_FILTER_PARAMETERS* p, UINT32) { impl->filterFrequency = p->Frequency; return S_OK; }
HRESULT IXAudio2Voice::SetEffectChain(const XAUDIO2_EFFECT_CHAIN* chain)
{
    if (impl->kind == Kind::Master && chain && chain->EffectCount > 0)
    {
        impl->limiter = static_cast<IXAPO*>(chain->pEffectDescriptors[0].pEffect);
        impl->limiter->AddRef();
        // the engine holds a reference: ours is released by the caller; keep the object alive in the mock by never deleting it
        WAVEFORMATEX f{};
        f.wFormatTag = 3;
        f.nChannels = static_cast<WORD>(impl->channels);
        f.nSamplesPerSec = impl->rate;
        f.wBitsPerSample = 32;
        XAPO_LOCKFORPROCESS_PARAMETERS lock{&f, static_cast<UINT32>(STEP_FRAMES)};
        impl->limiter->LockForProcess(1, &lock, 1, &lock);
    }
    return S_OK;
}
HRESULT IXAudio2Voice::SetEffectParameters(UINT32, const void*, UINT32, UINT32) { return S_OK; }

HRESULT IXAudio2SourceVoice::Start(UINT32, UINT32) { impl->started = true; return S_OK; }
HRESULT IXAudio2SourceVoice::Stop(UINT32, UINT32) { impl->started = false; return S_OK; }
HRESULT IXAudio2SourceVoice::SubmitSourceBuffer(const XAUDIO2_BUFFER* b, const void*)
{
    impl->data = b->pAudioData;
    impl->bytes = b->AudioBytes;
    impl->frames = b->AudioBytes / (2 * impl->channels);
    impl->loopBegin = b->LoopBegin;
    impl->loopLength = b->LoopLength > 0 ? b->LoopLength : impl->frames;
    impl->loopCount = b->LoopCount;
    impl->hasBuffer = true;
    impl->finished = false;
    impl->position = 0.0;
    impl->played = 0.0;
    SubmitLog log;
    log.step = g_Step;
    log.voice = impl->id;
    log.bytes = b->AudioBytes;
    log.loop = b->LoopCount > 0;
    log.destVoice = impl->lastDest;
    log.volumeAtStart = impl->volume;
    g_Submits.push_back(log);
    return S_OK;
}
HRESULT IXAudio2SourceVoice::FlushSourceBuffers()
{
    impl->hasBuffer = false;
    impl->finished = false;
    impl->position = 0.0;
    impl->played = 0.0;
    return S_OK;
}
HRESULT IXAudio2SourceVoice::SetFrequencyRatio(float r, UINT32) { impl->ratio = r; return S_OK; }
void IXAudio2SourceVoice::GetState(XAUDIO2_VOICE_STATE* s, UINT32)
{
    s->pCurrentBufferContext = nullptr;
    s->BuffersQueued = (impl->hasBuffer && !impl->finished) ? 1 : 0;
    s->SamplesPlayed = static_cast<UINT64>(impl->played);
}

HRESULT IXAudio2::CreateSourceVoice(IXAudio2SourceVoice** v, const WAVEFORMATEX* f, UINT32, float, IXAudio2VoiceCallback*, const XAUDIO2_VOICE_SENDS* sends, const XAUDIO2_EFFECT_CHAIN*)
{
    VoiceImpl* impl = NewVoice(Kind::Source, f->nChannels, f->nSamplesPerSec, 0, sends);
    auto* api = new IXAudio2SourceVoice();
    api->impl = impl;
    impl->api = api;
    *v = api;
    return S_OK;
}
HRESULT IXAudio2::CreateSubmixVoice(IXAudio2SubmixVoice** v, UINT32 ch, UINT32 rate, UINT32, UINT32 stage, const XAUDIO2_VOICE_SENDS* sends, const XAUDIO2_EFFECT_CHAIN* chain)
{
    // XAudio2 refuses a send to a voice of a lower (or the same) stage
    if (sends != nullptr)
        for (UINT32 i = 0; i < sends->SendCount; ++i)
        {
            VoiceImpl* d = Impl(sends->pSends[i].pOutputVoice);
            if (d && d->kind == Kind::Submix && d->stage <= stage) return E_FAIL;
        }
    VoiceImpl* impl = NewVoice(Kind::Submix, ch, rate, stage, sends);
    impl->isReverb = chain != nullptr && chain->EffectCount > 0;
    auto* api = new IXAudio2SubmixVoice();
    api->impl = impl;
    impl->api = api;
    *v = api;
    return S_OK;
}
HRESULT IXAudio2::CreateMasteringVoice(IXAudio2MasteringVoice** v, UINT32, UINT32, UINT32, const wchar_t*, const XAUDIO2_EFFECT_CHAIN*)
{
    VoiceImpl* impl = NewVoice(Kind::Master, 2, 48000, 1000, nullptr);
    auto* api = new IXAudio2MasteringVoice();
    api->impl = impl;
    impl->api = api;
    *v = api;
    return S_OK;
}
HRESULT IXAudio2::StartEngine() { return S_OK; }
HRESULT IXAudio2::RegisterForCallbacks(IXAudio2EngineCallback*) { return S_OK; }
void IXAudio2::UnregisterForCallbacks(IXAudio2EngineCallback*) {}
void IXAudio2::StopEngine() {}
HRESULT IXAudio2::CommitChanges(UINT32) { return S_OK; }
ULONG IXAudio2::Release() { return 0; }
HRESULT XAudio2Create(IXAudio2** pp, UINT32, UINT32) { *pp = new IXAudio2(); return S_OK; }

