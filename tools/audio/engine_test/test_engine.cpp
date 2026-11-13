// Runs the REAL GameAudio.cpp against a mock of XAudio2 that mixes (see mock/): the control logic of the engine is tested - the routing of the buses, the ducking, the
// priorities, the heartbeat, the music that waits for a bar, the stinger that waits for a beat, the limiter - and a loud scene is rendered to look at its peak.
#include "core/GameAudio.h"
#include "core/AudioLogic.h"
#include "core/AudioMap.h"
#include "core/GameSettings.h"
#include "mock_xaudio2.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifndef GAME_AUDIO_LIMITER
#define GAME_AUDIO_LIMITER 1 // (the same switch as in GameAudio.cpp)
#endif
using namespace MockXAudio2;
static int g_Failed = 0, g_Passed = 0;
#define CHECK(cond)                                                                                                       \
    do                                                                                                                    \
    {                                                                                                                     \
        if (!(cond)) { std::printf("  FAIL line %d: %s\n", __LINE__, #cond); ++g_Failed; }                              \
        else ++g_Passed;                                                                                                  \
    } while (0)

static void Step(int n = 1)
{
    for (int i = 0; i < n; ++i)
    {
        GameAudio::Update();
        Advance(1);
    }
}

static std::multimap<unsigned, std::string> g_SizeToName; // AudioBytes of a file of the game -> its name (two files can have the same length: a name matches if it is one of them)
static void IndexAssets()
{
    const char* dirs[] = {"assets/audio/se/"};
    const char* names[] = {"white_warn", "overload_alarm", "overload_break", "wave_clear", "heal", "synergy_found", "unlock", "tactic_decoy", "tactic_dash", "aegis_block",
                           "backup_revive", "echo_arc", "low_hp_pulse", "player_shot_blue", "player_shot_red", "enemy_hit_blue", "enemy_hit_red", "enemy_explosion", "elite_explosion", "boss_cannon_fire", "absorb_blue", "absorb_red", "sniper_lock", "charger_warning",
                           "boss_laser_fire", "boss_explosion", "player_damage", "enemy_hit_armor"};
    for (const char* n : names)
    {
        std::ifstream f(std::string(dirs[0]) + n + ".wav", std::ios::binary | std::ios::ate);
        if (f) g_SizeToName.emplace(static_cast<unsigned>(static_cast<long long>(f.tellg()) - 44), n);
    }
}
static bool IsName(unsigned bytes, const std::string& name)
{
    auto range = g_SizeToName.equal_range(bytes);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second == name) return true;
    return false;
}

static std::vector<VoiceImpl*> Buses() // UI, PLAYER, WORLD, ALERT
{
    std::vector<VoiceImpl*> v;
    for (VoiceImpl* x : g_Voices)
        if (x->alive && x->kind == Kind::Submix && x->stage == 1) v.push_back(x);
    return v;
}
static VoiceImpl* FindStage(unsigned stage)
{
    for (VoiceImpl* x : g_Voices)
        if (x->alive && x->kind == Kind::Submix && x->stage == stage && !x->isReverb && stage != 2) return x;
    for (VoiceImpl* x : g_Voices)
        if (x->alive && x->kind == Kind::Submix && x->stage == stage) return x;
    return nullptr;
}
static VoiceImpl* Master() { for (VoiceImpl* x : g_Voices) if (x->kind == Kind::Master) return x; return nullptr; }
static int CountSubmits(size_t from, const std::string& name)
{
    int n = 0;
    for (size_t i = from; i < g_Submits.size(); ++i)
        if (!g_Submits[i].loop && IsName(g_Submits[i].bytes, name)) ++n;
    return n;
}

static void FreshEngine()
{
    GameAudio::StopAll();
    GameAudio::Finalize();
    Reset();
    GameSettings::Edit() = GameSettings::Values();
    GameSettings::ResetKeys();
    GameAudio::Initialize();
}

// ----------------------------------------------------------------------------------------------------------------------------------------------------
static void TestRouting()
{
    std::printf("routing of the buses\n");
    FreshEngine();
    auto buses = Buses();
    CHECK(buses.size() == 4);
    VoiceImpl* sfx = FindStage(3);
    VoiceImpl* reverb = nullptr;
    for (VoiceImpl* x : g_Voices) if (x->isReverb) reverb = x;
    CHECK(sfx != nullptr);
    CHECK(reverb != nullptr);
    if (buses.size() == 4)
    {
        CHECK(buses[0]->sends.size() == 1 && buses[0]->sends[0].dest == sfx);
        CHECK(buses[2]->sends.size() == 2); // the world has the reverb send
        CHECK(buses[3]->sends.size() == 1);
        // the voices of a sound play into its bus
        int ui = 0, player = 0, world = 0, alert = 0;
        for (VoiceImpl* x : g_Voices)
        {
            if (x->kind != Kind::Source || x->sends.empty()) continue;
            VoiceImpl* d = x->sends[0].dest;
            if (d == buses[0]) ++ui; else if (d == buses[1]) ++player; else if (d == buses[2]) ++world; else if (d == buses[3]) ++alert;
        }
        std::printf("  voices per bus: ui %d player %d world %d alert %d\n", ui, player, world, alert);
        CHECK(ui > 0 && player > 0 && world > 0 && alert > 0);
    }
    CHECK(Master() != nullptr);
#if GAME_AUDIO_LIMITER
    CHECK(Master()->limiter != nullptr); // the limiter is on the mastering voice
#endif
}

static void TestDuckingAndPriority()
{
    std::printf("cues duck the player and the world; priorities\n");
    FreshEngine();
    auto buses = Buses();
    Step(5);
    CHECK(std::fabs(buses[1]->volume - 1.0f) < 1e-4 && std::fabs(buses[2]->volume - 1.0f) < 1e-4);
    const float cue = std::pow(10.0f, 2.0f / 20.0f); // CUE EMPHASIS 50 % = +2 dB
    CHECK(std::fabs(buses[3]->volume - cue) < 1e-3);
    GameAudio::Play(GameSound::WhiteWarn);
    Step(4);
    std::printf("  while the white warning plays: player %.3f  world %.3f  alert %.3f\n", buses[1]->volume, buses[2]->volume, buses[3]->volume);
    CHECK(buses[1]->volume < 0.85f && buses[2]->volume < buses[1]->volume);
    CHECK(buses[3]->volume > 1.0f);
    CHECK(buses[0]->volume > 0.99f); // the interface is never ducked
    Step(150);
    CHECK(buses[1]->volume > 0.99f && buses[2]->volume > 0.99f); // it comes back
    // the emphasis setting: 100 % ducks harder, 0 % softer
    GameSettings::Edit().cuePercent = 100;
    GameAudio::Play(GameSound::WhiteWarn);
    Step(4);
    const float hard = buses[2]->volume;
    Step(150);
    GameSettings::Edit().cuePercent = 0;
    GameAudio::Play(GameSound::WhiteWarn);
    Step(4);
    const float soft = buses[2]->volume;
    std::printf("  world bus under a warning: emphasis 0 %% -> %.3f, 100 %% -> %.3f\n", soft, hard);
    CHECK(hard < soft);
    CHECK(std::fabs(buses[3]->volume - 1.0f) < 1e-3); // 0 % = the alert is not lifted
    GameSettings::Edit().cuePercent = 50;
    Step(200);

    // priorities: in ONE step a burst of texture and a warning: the warning always plays, the texture is limited
    FreshEngine();
    size_t from = g_Submits.size();
    for (int i = 0; i < 40; ++i) GameAudio::Play(GameSound::PlayerShotBlue);
    for (int i = 0; i < 30; ++i) GameAudio::Play(GameSound::EnemyHitBlue);
    for (int i = 0; i < 12; ++i) GameAudio::Play(GameSound::EnemyExplosion);
    GameAudio::Play(GameSound::WhiteWarn);
    GameAudio::Play(GameSound::OverloadAlarm);
    GameAudio::Play(GameSound::PlayerDamage);
    int shots = CountSubmits(from, "player_shot_blue"), hits = CountSubmits(from, "enemy_hit_blue"), expl = CountSubmits(from, "enemy_explosion");
    int warn = CountSubmits(from, "white_warn"), alarm = CountSubmits(from, "overload_alarm"), dmg = CountSubmits(from, "player_damage");
    std::printf("  one step, 83 requests: shots %d, hits %d, explosions %d, white warning %d, alarm %d, damage %d\n", shots, hits, expl, warn, alarm, dmg);
    CHECK(warn == 1 && alarm == 1 && dmg == 1);
    CHECK(shots + hits <= 6);
    // (in this one step the budget was used up by the first requests: the texture that came later was dropped, the cues were not)
    Step(1);
    from = g_Submits.size();
    for (int i = 0; i < 40; ++i) GameAudio::Play(GameSound::PlayerShotBlue);
    CHECK(CountSubmits(from, "player_shot_blue") <= 4);
}

static void TestHeartbeat()
{
    std::printf("heartbeat and the muffled music\n");
    FreshEngine();
    GameAudio::PlayLoop(GameSound::StageBgm);
    Step(10);
    VoiceImpl* music = nullptr;
    for (VoiceImpl* x : g_Voices) if (x->kind == Kind::Submix && x->stage == 0) music = x;
    GameAudio::FightState f;
    f.active = true; f.alive = true; f.hpRate = 0.8f;
    GameAudio::SetFightState(f);
    size_t from = g_Submits.size();
    Step(300);
    CHECK(CountSubmits(from, "low_hp_pulse") == 0);
    CHECK(music->filterFrequency >= 0.99f);
    f.hpRate = 0.30f;
    GameAudio::SetFightState(f);
    from = g_Submits.size();
    std::vector<long long> at;
    for (int i = 0; i < 400; ++i)
    {
        const size_t before = g_Submits.size();
        Step(1);
        for (size_t k = before; k < g_Submits.size(); ++k)
            if (!g_Submits[k].loop && IsName(g_Submits[k].bytes, "low_hp_pulse")) at.push_back(g_Step);
    }
    std::printf("  at 30 %% HP: %zu beats in 400 steps", at.size());
    if (at.size() > 2) std::printf(", %lld steps apart", at[2] - at[1]);
    std::printf("; music filter %.3f\n", music->filterFrequency);
    CHECK(at.size() >= 5 && at.size() <= 8);
    CHECK(music->filterFrequency < 0.95f); // muffled a little
    f.hpRate = 0.04f;
    GameAudio::SetFightState(f);
    at.clear();
    Step(200);
    from = g_Submits.size();
    Step(240);
    const int fast = CountSubmits(from, "low_hp_pulse");
    std::printf("  at 4 %% HP: %d beats in 240 steps\n", fast);
    CHECK(fast >= 6);
    // paused / dead / setting off: silent
    f.active = false;
    GameAudio::SetFightState(f);
    Step(10);
    from = g_Submits.size();
    Step(200);
    CHECK(CountSubmits(from, "low_hp_pulse") == 0);
    f.active = true; f.alive = false;
    GameAudio::SetFightState(f);
    Step(10);
    from = g_Submits.size();
    Step(200);
    CHECK(CountSubmits(from, "low_hp_pulse") == 0);
    f.alive = true;
    GameSettings::Edit().heartbeat = false;
    GameAudio::SetFightState(f);
    Step(10);
    from = g_Submits.size();
    Step(200);
    CHECK(CountSubmits(from, "low_hp_pulse") == 0);
    GameSettings::Edit().heartbeat = true;
    // recovers: the HP is full, the filter opens
    f.hpRate = 1.0f;
    GameAudio::SetFightState(f);
    Step(400);
    std::printf("  HP full again: music filter %.3f\n", music->filterFrequency);
    CHECK(music->filterFrequency >= 0.99f);
    // the overload: the music holds its breath (the bus is lowered and muffled), and lets go
    f.overload = true; f.bossFight = true; f.overloadRate = 0.8f;
    GameAudio::SetFightState(f);
    Step(120);
    std::printf("  while an overload charges: music filter %.3f, bus gain %.3f\n", music->filterFrequency, music->volume);
    CHECK(music->filterFrequency < 0.8f);
    f.overload = false;
    GameAudio::SetFightState(f);
    Step(400);
    CHECK(music->filterFrequency >= 0.99f);
}

// the position of the music in its file at this moment
struct MusicProbe
{
    std::vector<VoiceImpl*> stems;
    float bpm = 0.0f;
    unsigned frames = 0;
};
static MusicProbe ProbeMusic(size_t submitFrom)
{
    MusicProbe p;
    for (size_t i = submitFrom; i < g_Submits.size(); ++i)
        if (g_Submits[i].loop) p.stems.push_back(g_Voices[static_cast<size_t>(g_Submits[i].voice)]);
    if (!p.stems.empty())
    {
        p.frames = p.stems[0]->frames;
        // the tempo from the length of the file (the table of the engine, version 4: stage_a 2,419,200 frames = 140 BPM, stage_b 2,646,000 = 128, stage_c 2,822,400 = 120)
        p.bpm = p.frames == 2419200 ? 140.0f : (p.frames == 2646000 ? 128.0f : (p.frames == 2822400 ? 120.0f : 0.0f));
    }
    return p;
}
static long long PositionOf(const VoiceImpl* v) { return AudioLogic::PositionInFile(static_cast<unsigned long long>(v->played), 0, v->frames); }

static void TestQuantizedLayers()
{
    std::printf("layers come in on the next bar\n");
    int tested = 0;
    double worstBar = 0, worstWait = 0, sumWait = 0;
    for (int offset = 0; offset < 160; offset += 9)
    {
        FreshEngine();
        const size_t from = g_Submits.size();
        GameAudio::PlayLoop(GameSound::StageBgm);
        Step(2);
        MusicProbe p = ProbeMusic(from);
        if (p.stems.size() < 2 || p.bpm == 0.0f) continue;
        Step(offset);
        // heat: the music goes from calm to level 1 (a second layer)
        const float volBefore = p.stems[1]->volume;
        CHECK(volBefore < 0.01f);
        for (int i = 0; i < 8; ++i) GameAudio::Play(GameSound::EliteExplosion);
        const long long step0 = g_Step;
        long long risen = -1;
        for (int i = 0; i < 260 && risen < 0; ++i)
        {
            Step(1);
            if (p.stems[1]->volume > 0.002f) risen = g_Step;
        }
        CHECK(risen >= 0);
        if (risen < 0) continue;
        const double spb = AudioLogic::SamplesPerBeat(44100, p.bpm);
        // the fade started one step before the first volume we see: the position then is what counts
        const long long pos = PositionOf(p.stems[1]) - static_cast<long long>(44100.0 / 60.0 * (g_Step - risen + 1));
        const double toBar = AudioLogic::SamplesToNextGrid(pos < 0 ? pos + p.frames : pos, spb * 4);
        const double fromBar = std::fmin(toBar, spb * 4 - toBar);
        const double toBeat = std::fmin(std::fmod((double)pos, spb), spb - std::fmod((double)pos, spb));
        const double wait = static_cast<double>(risen - step0);
        // lands on a line of the bar, two beats or the beat (the grid falls back when a bar is too far)
        const double toHalf = std::fmin(std::fmod((double)pos, spb * 2), spb * 2 - std::fmod((double)pos, spb * 2));
        const double best = std::fmin(fromBar, std::fmin(toHalf, toBeat));
        worstBar = std::fmax(worstBar, best / (44100.0 / 60.0));
        worstWait = std::fmax(worstWait, wait);
        sumWait += wait;
        ++tested;
    }
    std::printf("  %d starts at different places of the bar: the layer began on a bar / beat line within %.2f steps at worst (a step is 1/60 s); it waited %.1f steps on average, %.0f at most\n",
                tested, worstBar, tested ? sumWait / tested : 0.0, worstWait);
    CHECK(tested >= 10);
    CHECK(worstBar <= 1.6);
    CHECK(worstWait <= 101);
}

// ---- version 4: the two faces of the music -------------------------------------------------------------------------------------------------------------
static float PolarityMixDepth() { return AudioLogic::PolarityMix::DEPTH; }

static void TestPolarFaces()
{
    std::printf("the two faces of the music follow the colour\n");
    // a stage track: s0 the foundation, s1 the blue face, s2 the red face, s3 the peak
    FreshEngine();
    const size_t from = g_Submits.size();
    GameAudio::PlayLoop(GameSound::StageBgm);
    Step(2);
    MusicProbe p = ProbeMusic(from);
    CHECK(p.stems.size() == 4);
    if (p.stems.size() != 4) return;
    GameAudio::SetMusicPolarity(false);
    Step(240);
    std::printf("  calm: s0 %.2f s1 %.2f s2 %.2f s3 %.2f\n", p.stems[0]->volume, p.stems[1]->volume, p.stems[2]->volume, p.stems[3]->volume);
    CHECK(p.stems[0]->volume > 0.5f && p.stems[1]->volume < 0.01f && p.stems[2]->volume < 0.01f && p.stems[3]->volume < 0.01f);
    // a fight: the heat floor opens level 1 (both faces, not the peak); the blue ship hears the blue face
    GameAudio::FightState f;
    f.active = true;
    f.alive = true;
    f.hpRate = 1.0f;
    GameAudio::SetFightState(f);
    Step(500);
    std::printf("  a fight, blue ship: s1 (blue) %.2f  s2 (red) %.2f  s3 %.2f\n", p.stems[1]->volume, p.stems[2]->volume, p.stems[3]->volume);
    CHECK(p.stems[1]->volume > 0.8f && p.stems[2]->volume > 0.2f && p.stems[1]->volume > 2.0f * p.stems[2]->volume);
    CHECK(p.stems[3]->volume < 0.01f);
    // the ship turns red: the red face takes over slowly (not within a fifth of a second), and then it has
    GameAudio::SetMusicPolarity(true);
    Step(12);
    CHECK(p.stems[1]->volume > p.stems[2]->volume);
    Step(300);
    std::printf("  red ship: s1 %.2f  s2 %.2f\n", p.stems[1]->volume, p.stems[2]->volume);
    CHECK(p.stems[2]->volume > 0.8f && p.stems[2]->volume > 2.0f * p.stems[1]->volume);
    // a player who switches all the time: both faces are about equally loud, none of them is ever gone
    bool red = false;
    float worstDiff = 0.0f, lowest = 1.0f;
    for (int i = 0; i < 900; ++i)
    {
        if (i % 20 == 0)
        {
            red = !red;
            GameAudio::SetMusicPolarity(red);
        }
        Step(1);
        if (i > 300)
        {
            worstDiff = std::fmax(worstDiff, std::fabs(p.stems[1]->volume - p.stems[2]->volume));
            lowest = std::fmin(lowest, std::fmin(p.stems[1]->volume, p.stems[2]->volume));
        }
    }
    std::printf("  switching every 1/3 s: the two faces differ by %.2f at most, the lowest is %.2f\n", worstDiff, lowest);
    CHECK(worstDiff < 0.30f && lowest > 0.30f);
    // the peak: the heat goes up, s3 comes in
    for (int i = 0; i < 8; ++i) GameAudio::Play(GameSound::EliteExplosion);
    float peakS3 = 0.0f;
    for (int i = 0; i < 300; ++i)
    {
        Step(1);
        peakS3 = std::fmax(peakS3, p.stems[3]->volume);
    }
    std::printf("  hot: s3 reaches %.2f\n", peakS3);
    CHECK(peakS3 > 0.8f);
    Step(400);
    // the card screen: only the foundation
    GameAudio::SetPickScreen(true);
    Step(60);
    CHECK(p.stems[0]->volume > 0.5f && p.stems[1]->volume < 0.01f && p.stems[2]->volume < 0.01f && p.stems[3]->volume < 0.01f);
    GameAudio::SetPickScreen(false);
    // the title: three layers, all of them all the time, the face follows the colour of the title ship
    FreshEngine();
    const size_t from2 = g_Submits.size();
    GameAudio::SetMusicPolarity(false);
    GameAudio::PlayLoop(GameSound::TitleBgm);
    Step(200);
    MusicProbe t = ProbeMusic(from2);
    CHECK(t.stems.size() == 3);
    if (t.stems.size() == 3)
    {
        std::printf("  title, blue ship: %.2f %.2f %.2f\n", t.stems[0]->volume, t.stems[1]->volume, t.stems[2]->volume);
        CHECK(t.stems[0]->volume > 0.5f && t.stems[1]->volume > 0.5f && t.stems[2]->volume > 0.15f && t.stems[1]->volume > t.stems[2]->volume);
        GameAudio::SetMusicPolarity(true);
        Step(300);
        CHECK(t.stems[2]->volume > t.stems[1]->volume);
    }
    // the boss: one theme in three tempi, four layers each; the boss theme never falls below level 1 (both faces on)
    FreshEngine();
    const size_t from3 = g_Submits.size();
    GameAudio::PlayLoop(GameSound::BossBgm);
    Step(150);
    MusicProbe b = ProbeMusic(from3);
    CHECK(b.stems.size() == 4);
    if (b.stems.size() == 4)
    {
        std::printf("  boss, calm: %.2f %.2f %.2f %.2f (the colour is still red from the title)\n", b.stems[0]->volume, b.stems[1]->volume, b.stems[2]->volume, b.stems[3]->volume);
        CHECK(b.stems[0]->volume > 0.5f && b.stems[1]->volume >= PolarityMixDepth() - 0.02f && b.stems[2]->volume > 0.5f && b.stems[3]->volume < 0.01f);
    }
}

static void TestStinger()
{
    std::printf("the wave clear stinger lands on a beat\n");
    int tested = 0;
    double worstErr = 0, worstWait = 0, sumWait = 0;
    for (int offset = 0; offset < 140; offset += 5)
    {
        FreshEngine();
        const size_t from = g_Submits.size();
        GameAudio::PlayLoop(GameSound::StageBgm);
        Step(2);
        MusicProbe p = ProbeMusic(from);
        if (p.stems.empty() || p.bpm == 0.0f) continue;
        Step(offset);
        const size_t before = g_Submits.size();
        const long long step0 = g_Step;
        GameAudio::Play(GameSound::WaveClear);
        long long played = -1;
        VoiceImpl* v = p.stems[0];
        long long posAt = 0;
        for (int i = 0; i < 40 && played < 0; ++i)
        {
            const bool had = CountSubmits(before, "wave_clear") > 0;
            if (had) { played = g_Step; break; }
            Step(1);
            if (CountSubmits(before, "wave_clear") > 0) { played = g_Step; posAt = PositionOf(v); }
        }
        if (played < 0) { CHECK(false); continue; }
        if (posAt == 0) posAt = PositionOf(v);
        const double spb = AudioLogic::SamplesPerBeat(44100, p.bpm);
        // it was started in the step that ended at `played`: its sound is heard in the NEXT step of the engine (the position of the music at the START of that step is posAt - 735)
        const long long startPos = posAt;   // (the mock mixes after Update: the voice starts at the beginning of the next mixing step, where the music is `posAt` frames in)
        const double r = std::fmod(static_cast<double>(startPos), spb * 0.25);
        const double err = std::fmin(r, spb * 0.25 - r) / (44100.0 / 60.0); // distance to the nearest line of a 16th grid, in steps
        const double r1 = std::fmod(static_cast<double>(startPos), spb * 0.5);
        const double err8 = std::fmin(r1, spb * 0.5 - r1) / (44100.0 / 60.0);
        worstErr = std::fmax(worstErr, std::fmin(err, err8));
        worstWait = std::fmax(worstWait, static_cast<double>(played - step0));
        sumWait += static_cast<double>(played - step0);
        ++tested;
    }
    std::printf("  %d requests at different places of the beat: it started %.2f steps from a line of the grid at worst; it waited %.1f steps on average, %.0f at most\n", tested, worstErr,
                tested ? sumWait / tested : 0.0, worstWait);
    CHECK(tested >= 20);
    CHECK(worstWait <= 21);
    CHECK(worstErr <= 1.6);
    // without music that has a tempo it plays at once
    FreshEngine();
    const size_t from = g_Submits.size();
    GameAudio::Play(GameSound::WaveClear);
    CHECK(CountSubmits(from, "wave_clear") == 1);
}

static void TestSettings()
{
    std::printf("master volume, the background\n");
    FreshEngine();
    Step(2);
    VoiceImpl* master = Master();
    CHECK(std::fabs(master->volume - 1.0f) < 1e-4);
    GameSettings::Edit().masterPercent = 50;
    Step(2);
    CHECK(std::fabs(master->volume - 0.25f) < 1e-4); // the square law of the sliders
    GameSettings::Edit().masterPercent = 0;
    Step(2);
    CHECK(master->volume == 0.0f);
    GameSettings::Edit().masterPercent = 100;
    Step(2);
    // pause
    VoiceImpl* music = nullptr;
    for (VoiceImpl* x : g_Voices) if (x->kind == Kind::Submix && x->stage == 0) music = x;
    GameAudio::PlayLoop(GameSound::StageBgm);
    GameAudio::SetPaused(true);
    Step(30);
    std::printf("  paused: music bus %.2f, filter %.3f\n", music->volume, music->filterFrequency);
    CHECK(music->volume < 0.6f && music->filterFrequency < 0.35f);
    GameAudio::SetPaused(false);
    Step(30);
    CHECK(music->volume > 0.45f && music->filterFrequency >= 0.99f);
}

// a loud scene: the fight at its worst
static float PeakOf(size_t from, size_t to)
{
    float p = 0;
    for (size_t i = from; i < to && i < g_Output.size(); ++i) p = std::fmax(p, std::fabs(g_Output[i]));
    return p;
}
static void WriteWav(const char* path, const std::vector<float>& s)
{
    std::ofstream f(path, std::ios::binary);
    const unsigned n = static_cast<unsigned>(s.size());
    unsigned data = n * 2, riff = 36 + data, fmt = 16, rate = 48000, avg = 48000 * 4;
    unsigned short tag = 1, ch = 2, align = 4, bits = 16;
    f.write("RIFF", 4); f.write((char*)&riff, 4); f.write("WAVEfmt ", 8); f.write((char*)&fmt, 4); f.write((char*)&tag, 2); f.write((char*)&ch, 2); f.write((char*)&rate, 4);
    f.write((char*)&avg, 4); f.write((char*)&align, 2); f.write((char*)&bits, 2); f.write("data", 4); f.write((char*)&data, 4);
    for (float x : s) { short v = (short)std::lround(std::fmax(-1.0f, std::fmin(1.0f, x)) * 32767.0f); f.write((char*)&v, 2); }
}

static void TestLoudScene(const char* label, const char* wavPath)
{
    std::printf("a loud scene (%s)\n", label);
    FreshEngine();
    GameAudio::SetSeVolume(1.0f);   // the sliders at the top: the worst case
    GameAudio::SetBgmVolume(1.0f);
    GameAudio::PlayLoop(GameSound::BossBgm);
    Step(60);
    const size_t start = g_Output.size();
    // 6 seconds of the worst case: explosions, hits, shots, the boss cannons and a laser, at the rate of a bullet hell
    for (int s = 0; s < 360; ++s)
    {
        if (s % 3 == 0) GameAudio::Play(GameSound::PlayerShotBlue);
        if (s % 2 == 0) GameAudio::Play(GameSound::EnemyHitBlue);
        if (s % 7 == 0) GameAudio::Play(GameSound::EnemyExplosion);
        if (s % 11 == 0) GameAudio::Play(GameSound::EliteExplosion);
        if (s % 5 == 0) GameAudio::Play(GameSound::BossCannonFire);
        if (s % 4 == 0) GameAudio::PlayAbsorb(false);
        if (s % 90 == 20) GameAudio::Play(GameSound::BossLaserFire);
        if (s % 120 == 60) GameAudio::Play(GameSound::BossExplosion);
        if (s % 150 == 100) GameAudio::Play(GameSound::PlayerDamage);
        if (s % 100 == 10) GameAudio::Play(GameSound::WhiteWarn);
        Step(1);
    }
    const float peak = PeakOf(start, g_Output.size());
    float rms = 0; size_t n = 0;
    for (size_t i = start; i < g_Output.size(); ++i) { rms += g_Output[i] * g_Output[i]; ++n; }
    rms = std::sqrt(rms / std::max<size_t>(1, n));
    std::printf("  peak of the final mix %.3f (%.1f dBFS), rms %.1f dBFS\n", peak, 20 * std::log10(peak + 1e-9f), 20 * std::log10(rms + 1e-9f));
    WriteWav(wavPath, std::vector<float>(g_Output.begin() + (long)start, g_Output.end()));
    if (std::strcmp(label, "with the limiter") == 0) CHECK(peak <= 0.97f + 1e-4f);
}

int main(int argc, char** argv)
{
    IndexAssets();
    const bool onlyLoud = argc > 1 && std::strcmp(argv[1], "loud") == 0;
    if (!onlyLoud)
    {
        TestRouting();
        TestDuckingAndPriority();
        TestHeartbeat();
        TestQuantizedLayers();
        TestPolarFaces();
        TestStinger();
        TestSettings();
    }
#if GAME_AUDIO_LIMITER
    TestLoudScene("with the limiter", "loud_with_limiter.wav");
#else
    TestLoudScene("without the limiter", "loud_without_limiter.wav");
#endif
    std::printf("\n%d checks passed, %d failed\n", g_Passed, g_Failed);
    GameAudio::Finalize();
    return g_Failed == 0 ? 0 : 1;
}


