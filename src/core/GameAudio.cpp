#include "core/GameAudio.h"
#include "core/AudioLogic.h"
#include "core/AudioMap.h"
#include "core/GameSettings.h"
#include <Windows.h>
#include <xaudio2.h>
#include <array>
#include "core/AudioHeat.h"
#include "core/OggDecode.h"
#include <atomic>
#include <new>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "xaudio2.lib")

// Two parts of the engine use more of XAudio2 than the rest: the reverb of the world bus (xaudio2fx.h) and the limiter on the output (an XAPO of our own: it
// implements IXAPO from xapo.h directly - it does NOT use xapobase.lib, which clashes with the C runtime of a normal project: LNK2005 _setusermatherr / frexp).
// Each has a switch: if one of them does not build with your SDK, set it to 0 here (the game then sounds as it did, without that part).
#ifndef GAME_AUDIO_REVERB
#define GAME_AUDIO_REVERB 1
#endif
#ifndef GAME_AUDIO_LIMITER
#define GAME_AUDIO_LIMITER 1
#endif
#if GAME_AUDIO_REVERB
#include <xaudio2fx.h>
#endif
#if GAME_AUDIO_LIMITER
#include <objbase.h>
#include <xapo.h>
#include <cwchar>
#ifndef XAPO_E_FORMAT_UNSUPPORTED
#define XAPO_E_FORMAT_UNSUPPORTED static_cast<HRESULT>(0x88970001L) // (it is in xapo.h; the same value if your header does not have it)
#endif
#endif

#ifndef XAUDIO2_VOICE_USEFILTER
#define XAUDIO2_VOICE_USEFILTER 0x0080
#endif

using namespace AudioLogic;

namespace
{
constexpr int MAX_VOICES = 8;
constexpr int MAX_STEMS = 4;
constexpr int BUS_COUNT = static_cast<int>(AudioMap::Bus::Count);
constexpr int ACTIVE_VOICES_NORMAL = 28; // above this many voices that play at once, the sounds of the body of the fight (Normal, Low) are not started ...
constexpr int ACTIVE_VOICES_LOW = 20;    // ... and the texture (Low) already above this many
constexpr int MAX_PENDING = 8;           // sounds that wait for a beat of the music
// processing stages: a voice can only send to a voice of a higher stage (the voices of the effects are below all of them)
constexpr UINT32 STAGE_BUS = 1;
constexpr UINT32 STAGE_REVERB = 2;
constexpr UINT32 STAGE_SFX = 3;
constexpr UINT32 REVERB_RATE = 48000; // the reverb of XAudio2 works at 24 - 48 kHz: the sends are converted to it
constexpr float REVERB_SEND_BASE = 0.06f;  // how much of the world bus goes to the reverb ...
constexpr float REVERB_SEND_BOSS = 0.15f;  // ... in the fight with the boss
// The music bus has ONE low-pass filter (ApplyBusFilter: amount 0 = open, 1 = the pause menu, 1.5 kHz). The state variable filter of XAudio2 is only defined up to
// a sixth of the sample rate (8 kHz at 48 kHz): above it, the filter is wide open. So a low HP and an overload start at amount 0.30 (8 kHz, no change you can hear)
// and go on from there.
constexpr float DANGER_FILTER_BASE = 0.30f;
constexpr float DANGER_FILTER_SPAN = 0.30f; // a ship at zero HP: amount 0.60 = about 4 kHz
constexpr float TENSION_FILTER_BASE = 0.30f;
constexpr float TENSION_FILTER_SPAN = 0.30f; // the end of the charge of an overload: amount 0.60 = about 4 kHz
constexpr float TENSION_GAIN_DROP = 0.18f; // ... and how much lower
constexpr float MAX_PITCH_RATIO = 4.0f;
constexpr float PAUSE_MUSIC_GAIN = 0.55f;  // the music while the game is paused
constexpr float PAUSE_CUTOFF_HZ = 1500.0f; // ... and how muffled it is
constexpr float PAUSE_STEP = 0.12f;
constexpr float PICK_MUSIC_GAIN = 0.82f;   // the card screen: a little lower ...
constexpr float PICK_CUTOFF_AMOUNT = 0.75f; // ... and muffled, but not as much as in the pause menu
constexpr float PICK_STEP = 0.06f;
constexpr int LAYER_FADE_IN = 90;          // frames: a layer that comes in takes 1.5 s
constexpr int LAYER_FADE_OUT = 150;        // ... one that leaves 2.5 s
constexpr int LAYER_FADE_PICK = 24;        // the card screen cuts the layers quickly
constexpr long long NEVER = -1000000;

// ---------------------------------------------------------------------------------------------------------------------------------
// The mix of the effects. One line per sound, in the order of GameSound (the music lines only mark the place: see MUSIC below).
//   voices     how many copies can play at the same time (a new one takes the oldest when all are busy)
//   gainDb     level of the sound in the mix (0 = the level of the file; the files are made to a level of their own, see tools/audio)
//   cooldown   steps (1/60 s) that have to pass before the same sound can start again
//   pitchVar   random pitch spread in semitones (+-): a sound that repeats does not sound like a machine gun
//   duckDb     the music is lowered by this much while the sound plays (0 = no ducking), for duckFrames steps
//   variants   how many files the sound has (name.wav, name_2.wav ...): one is picked at random, never the same twice in a row
//   heat       how much this sound heats up the music (see AudioLogic::Director): kills, damage and big weapons raise the intensity
// ---------------------------------------------------------------------------------------------------------------------------------
struct SoundDef
{
    GameSound id;
    const char* file;
    bool music;
    int voices;
    float gainDb;
    int cooldown;
    float pitchVar;
    bool chain; // the pitch climbs while the sound repeats quickly (and a chime plays on the 3rd and 5th)
    float duckDb;
    int duckFrames;
    int variants;
    float heat;
};

#define BGM(id) {GameSound::id, nullptr, true, 1, 0.0f, 0, 0.0f, false, 0.0f, 0, 1, 0.0f}
#define SE(id, file, voices, gainDb, cooldown, pitchVar, variants, heat) \
    {GameSound::id, "assets/audio/se/" file, false, voices, gainDb, cooldown, pitchVar, false, 0.0f, 0, variants, heat}
#define SE_DUCK(id, file, voices, gainDb, cooldown, pitchVar, variants, heat, duckDb, duckFrames) \
    {GameSound::id, "assets/audio/se/" file, false, voices, gainDb, cooldown, pitchVar, false, duckDb, duckFrames, variants, heat}
#define SE_CHAIN(id, file, voices, gainDb, cooldown, pitchVar, variants, heat) \
    {GameSound::id, "assets/audio/se/" file, false, voices, gainDb, cooldown, pitchVar, true, 0.0f, 0, variants, heat}

const SoundDef SOUNDS[] = {
    // ---- music (see MUSIC)
    BGM(TitleBgm), BGM(StageBgm), BGM(BossBgm), BGM(BossBerserkBgm), BGM(ClearBgm), BGM(GameOverBgm), BGM(BossPhase2Bgm),
    // ---- Version 7 ("MIRROR": the sound world of the music, tools/audio/build_sfx.py and tools/audio/sfx/). Every tone of every effect is D, E, G, A or C (the notes that all the keys of the
    // music share), the big moments play the motto of the theme (A A D), blue is glass and red is ink. The LEVEL of every sound is set in the file (the class of the sound: LEVELS in
    // build_sfx.py: alerts -18, hits -27, shots -31 dB A-weighted ...), so gainDb is 0 everywhere: to make a sound louder or quieter, change its class there or its gainDb here.
    // The pitch spread of a sound that has four variants is small: the variants are four notes of the scale.
    // ---- interface (the two colours fused; quiet, short, never in the way)
    SE(MenuCursor, "cursor.wav", 2, 0.0f, 2, 0.0f, 1, 0.0f),
    SE(MenuDecide, "decide.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(MenuMove, "menu_move.wav", 3, 0.0f, 2, 0.0f, 1, 0.0f),
    SE(MenuAdjust, "menu_adjust.wav", 3, 0.0f, 2, 0.0f, 1, 0.0f),
    SE(MenuApply, "menu_apply.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(MenuError, "menu_error.wav", 2, 0.0f, 6, 0.0f, 1, 0.0f),
    SE(MenuOpen, "menu_open.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(UiHint, "ui_hint.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(Unlock, "unlock.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(SynergyFound, "synergy_found.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f),
    // ---- the ship: BLUE = glass, RED = ink
    SE(PlayerShotBlue, "player_shot_blue.wav", 5, 0.0f, 0, 0.12f, 4, 0.0f), // (the four variants are four notes of the scale: the spread is only to keep a sound from being a machine)
    SE(PlayerShotRed, "player_shot_red.wav", 5, 0.0f, 0, 0.12f, 4, 0.0f),
    SE(PolaritySwitchBlue, "polarity_switch_blue.wav", 2, 0.0f, 3, 0.0f, 1, 0.02f),
    SE(PolaritySwitchRed, "polarity_switch_red.wav", 2, 0.0f, 3, 0.0f, 1, 0.02f),
    SE_CHAIN(AbsorbBlue, "absorb_blue.wav", 6, 0.0f, 2, 0.0f, 3, 0.010f),
    SE_CHAIN(AbsorbRed, "absorb_red.wav", 6, 0.0f, 2, 0.0f, 3, 0.010f),
    SE(AbsorbChime, "absorb_chime.wav", 3, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(EnergyFull, "energy_full.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE_DUCK(PlayerDamage, "player_damage.wav", 2, 0.0f, 0, 0.0f, 1, 0.22f, -6.0f, 36),
    SE_DUCK(PlayerDeath, "player_death.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -12.0f, 70),
    SE_DUCK(SpecialShot, "special_shot.wav", 2, 0.0f, 0, 0.0f, 1, 0.25f, -6.0f, 66),
    SE(Heal, "heal.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(AegisBlock, "aegis_block.wav", 2, 0.0f, 0, 0.0f, 1, 0.02f),
    SE_DUCK(BackupRevive, "backup_revive.wav", 1, 0.0f, 0, 0.0f, 1, 0.20f, -8.0f, 60),
    SE(EchoArc, "echo_arc.wav", 3, 0.0f, 3, 0.35f, 1, 0.010f),
    SE(TacticDecoy, "tactic_decoy.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(TacticDash, "tactic_dash.wav", 2, 0.0f, 0, 0.15f, 1, 0.0f),
    SE(LowHpPulse, "low_hp_pulse.wav", 2, -2.0f, 0, 0.0f, 1, 0.0f), // (set by its peak, not by its loudness: it is felt more than heard)
    // ---- enemies
    SE(EnemyHitBlue, "enemy_hit_blue.wav", 6, 0.0f, 2, 0.15f, 4, 0.008f),
    SE(EnemyHitRed, "enemy_hit_red.wav", 6, 0.0f, 2, 0.15f, 4, 0.008f),
    SE(EliteHit, "elite_hit.wav", 6, 0.0f, 2, 0.35f, 3, 0.015f),
    SE(EnemyHitArmor, "enemy_hit_armor.wav", 6, 0.0f, 2, 0.30f, 3, 0.015f),
    SE(EnemyHitGlass, "enemy_hit_glass.wav", 6, 0.0f, 2, 0.30f, 3, 0.015f),
    SE(EnemyHitClash, "enemy_hit_clash.wav", 6, 0.0f, 2, 0.30f, 3, 0.015f),
    SE(EnemyExplosion, "enemy_explosion.wav", 8, 0.0f, 2, 0.30f, 3, 0.050f),
    SE_DUCK(EliteExplosion, "elite_explosion.wav", 4, 0.0f, 0, 0.20f, 1, 0.160f, -3.0f, 18),
    SE_DUCK(SniperLock, "sniper_lock.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -2.0f, 24),
    SE(SniperFire, "sniper_fire.wav", 3, 0.0f, 0, 0.0f, 1, 0.0f),
    SE_DUCK(ChargerWarning, "charger_warning.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -2.0f, 30),
    SE(ChargerDash, "charger_dash.wav", 3, 0.0f, 0, 0.0f, 1, 0.0f),
    SE_DUCK(RivalTwin, "rival_twin.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f, -2.5f, 40),
    // ---- the boss (the cues also lower the music for a moment: a cue must be heard through the music)
    SE_DUCK(BossWarning, "boss_warning.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f, -4.0f, 60),
    SE_DUCK(BossPhase, "boss_phase.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -3.0f, 24), // (the music must not cool down at a phase change)
    SE_DUCK(BossBerserk, "boss_berserk.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -3.0f, 30),
    SE_DUCK(BossCannonCharge, "boss_cannon_charge.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -2.0f, 24),
    SE(BossCannonFire, "boss_cannon_fire.wav", 4, 0.0f, 2, 0.25f, 2, 0.020f),
    SE_DUCK(BossCannonBreak, "boss_cannon_break.wav", 3, 0.0f, 0, 0.0f, 1, 0.0f, -5.0f, 40),
    SE_DUCK(BossDashWarning, "boss_dash_warning.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -2.5f, 24),
    SE_DUCK(BossLaserCharge, "boss_laser_charge.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -2.0f, 40),
    SE_DUCK(BossLaserFire, "boss_laser_fire.wav", 2, 0.0f, 0, 0.0f, 1, 0.100f, -4.0f, 50),
    SE(BossHit, "boss_hit.wav", 6, 0.0f, 3, 0.45f, 3, 0.008f),
    SE(BossArmor, "boss_armor.wav", 4, 0.0f, 3, 0.45f, 3, 0.004f),
    SE_DUCK(BossExplosion, "boss_explosion.wav", 4, 0.0f, 0, 0.0f, 1, 0.200f, -8.0f, 60),
    SE_DUCK(BossFinalExplosion, "boss_final_explosion.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -14.0f, 150),
    SE(BossVent, "boss_vent.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(BossSwap, "boss_swap.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f),
    SE(BossFlip, "boss_flip.wav", 4, 0.0f, 4, 0.15f, 1, 0.0f),
    SE_DUCK(WhiteWarn, "white_warn.wav", 2, 0.0f, 0, 0.0f, 1, 0.0f, -2.5f, 30),
    SE_DUCK(OverloadAlarm, "overload_alarm.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f, -2.0f, 40),
    SE_DUCK(OverloadBreak, "overload_break.wav", 2, 0.0f, 0, 0.0f, 1, 0.12f, -6.0f, 30),
    // ---- the flow of a run
    SE_DUCK(WaveClear, "wave_clear.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f, -2.5f, 40),
    SE(CutInWave, "cutin_wave.wav", 1, 0.0f, 0, 0.0f, 1, 0.0f),
};

constexpr int SOUND_COUNT = static_cast<int>(GameSound::Count);
static_assert(sizeof(SOUNDS) / sizeof(SOUNDS[0]) == SOUND_COUNT, "SOUNDS must have one line per GameSound");

// ---------------------------------------------------------------------------------------------------------------------------------
// The music. A track is `stems` files: name.wav (one layer) or name_s0.wav, name_s1.wav ... (layers, see tools/audio/music.py).
// A sound with several tracks plays another one each time it starts. fadeIn / fadeOut in steps (60 = 1 s): a Stop() followed by a
// PlayLoop() of another sound is a cross fade of these lengths. `boss`: the layers are the phases of the boss.
// ---------------------------------------------------------------------------------------------------------------------------------
struct TrackDef
{
    const char* name;
    int stems;
    float bpm = 0.0f; // the tempo (4 / 4): the layers come in on the next bar and a stinger lands on a beat. 0 = no clock (the music then behaves as before)
    // The layers of a track with two faces (tools/audio/bgm6): s0 the foundation, s1 the BLUE face, s2 the RED face, s3 the peak (the title has no s3).
    // 0 = the layers are the levels of the heat (as before), 1 = two faces, played from heat level 1 on, weighted by the colour (AudioLogic::PolarityMix), the peak from level 2,
    // 2 = two faces, everything plays all the time (the title).
    int polar = 0;
};

struct MusicDef
{
    GameSound id;
    int fadeIn;
    int fadeOut;
    float gainDb;
    bool boss;        // a boss theme: the music never falls below the second layer while it plays
    bool startsBoss;  // starting it also restarts the count of the boss phases (only the first phase does)
    bool resume;      // a PlayLoop() while it is still fading out brings it back from where it is, it does not restart (the title)
    int trackCount;
    TrackDef tracks[4];
};

// The boss has ONE theme asked in three ways: boss_awaken (phase 1, 112 BPM: the motto alone, very low), boss_split (phase 2, 144: the theme and its mirror a bar apart, a canon) and
// boss_core (phase 3, 168, a step higher: the theme twice as slow in the brass over fast arpeggios). SetBossPhase() switches between them: the new track comes in within 3 steps and starts
// with a hit, the old one is gone in 14, so the change lands on the flash of the transformation instead of a hole in the music.
const MusicDef MUSIC[] = {
    // Version 6 ("MIRROR"): every track is the same theme said another way (tools/audio/bgm6/score.py; the command is tools/audio/build_bgm.py). The title has three layers (the foundation and the
    // two faces, all the time), the stage and boss tracks four (s0, blue, red, peak). stage_a is 32 bars of 140 BPM (2,419,200 frames), stage_b 32 bars of 128 (2,646,000), stage_c 32 bars of 120
    // (2,822,400); the boss tracks have 24 bars (112 / 144 / 168 BPM); the title 16 bars of 76; clear (12 bars) and gameover (10 bars) are one layer; every file loops from its first frame
    // (loops.txt has no line for them).
    {GameSound::TitleBgm, 45, 40, 0.0f, false, false, true, 1, {{"title", 3, 76.0f, 2}}},
    {GameSound::StageBgm, 30, 40, 0.0f, false, false, false, 3, {{"stage_a", 4, 140.0f, 1}, {"stage_b", 4, 128.0f, 1}, {"stage_c", 4, 120.0f, 1}}},
    {GameSound::BossBgm, 15, 14, 0.0f, true, true, false, 1, {{"boss_awaken", 4, 112.0f, 1}}},
    {GameSound::BossBerserkBgm, 3, 30, 0.0f, true, false, false, 1, {{"boss_core", 4, 168.0f, 1}}},
    {GameSound::ClearBgm, 6, 40, 0.0f, false, false, false, 1, {{"clear", 1}}},
    {GameSound::GameOverBgm, 30, 40, 0.0f, false, false, false, 1, {{"gameover", 1}}},
    {GameSound::BossPhase2Bgm, 3, 14, 0.0f, true, false, false, 1, {{"boss_split", 4, 144.0f, 1}}},
};

enum class MusicState
{
    Idle,
    Playing,
    FadingOut
};

struct Pcm
{
    std::vector<std::uint8_t> data;
    std::uint32_t frames = 0;
};

struct SfxData
{
    WAVEFORMATEX format{};
    std::vector<Pcm> variants;
    std::array<IXAudio2SourceVoice*, MAX_VOICES> voices{};
    int voiceCount = 0;
    int nextVoice = 0;
    int lastVariant = -1;
    bool loaded = false;
    long long lastPlay = NEVER;
    Chain chain;
    long long voiceEnd[MAX_VOICES]{}; // the step in which the sound on that voice is over (for the count of the voices that play)
};

struct TrackData
{
    std::vector<Pcm> stems;
    std::uint32_t frames = 0;
    std::uint32_t loopBegin = 0; // the loop starts here (the part before it is an intro that plays once)
    bool ok = false;
};

struct MusicData
{
    WAVEFORMATEX stemFormat[MAX_STEMS]{};
    bool formatSet[MAX_STEMS]{};
    std::vector<TrackData> tracks;
    IXAudio2SourceVoice* voices[MAX_STEMS]{};
    int voiceCount = 0;
    bool loaded = false;
    MusicState state = MusicState::Idle;
    int track = -1;
    int lastTrack = -1;
    int layers = 0;
    Fader master;
    Fader stem[MAX_STEMS];
    float stemTarget[MAX_STEMS]{};
    int stemDelay[MAX_STEMS]{};         // a layer that is to come in waits for the next bar: steps left (-1 = nothing is waiting)
    float stemDelayTarget[MAX_STEMS]{};
};

struct Pending
{
    bool used = false;
    GameSound sound = GameSound::MenuCursor;
    float x = 0.0f;
    int steps = 0;
};

IXAudio2* g_XAudio2 = nullptr;
IXAudio2MasteringVoice* g_MasteringVoice = nullptr;
IXAudio2SubmixVoice* g_MusicBus = nullptr;
IXAudio2SubmixVoice* g_SfxBus = nullptr;                       // the sum of the effects (its volume is the slider)
IXAudio2SubmixVoice* g_Bus[BUS_COUNT]{};                       // UI, PLAYER, WORLD, ALERT (AudioMap.h): every effect voice plays into one of them
IXAudio2SubmixVoice* g_ReverbBus = nullptr;                    // the world bus sends a little to it
Ducker g_BusDuck[BUS_COUNT];
float g_LastBusGain[BUS_COUNT] = {-1.0f, -1.0f, -1.0f, -1.0f};
float g_ReverbSend = REVERB_SEND_BASE;
float g_LastReverbSend = -1.0f;
StepBudget g_Budget;
int g_ActiveVoices = 0;
float g_Density = 1.0f;
Pending g_Pending[MAX_PENDING];
GameAudio::FightState g_Fight;
Heartbeat g_Heart;
float g_Danger = 0.0f;  // 0..1: the music under a low HP
float g_Tension = 0.0f; // 0..1: the music while an overload charges
bool g_Foreground = true;
UINT32 g_BusRate = 48000;
std::array<SfxData, SOUND_COUNT> g_Sfx{};
std::array<MusicData, SOUND_COUNT> g_Music{};
long long g_Frame = 0;
bool g_BgmEnabled = true;
bool g_SeEnabled = true;
float g_BgmSlider = 0.70f;
float g_SeSlider = 0.80f;
Ducker g_Duck;
Director g_Director;
BossStage g_Boss;
PolarityMix g_Polar;            // the faces of the music: 0 blue .. 1 red
bool g_PolarityRed = false;     // the colour that the music follows (SetMusicPolarity)
float g_PauseAmount = 0.0f; // 0 = playing, 1 = paused
bool g_PauseTarget = false;
float g_PickAmount = 0.0f;  // 0 = no card screen, 1 = card screen
bool g_PickTarget = false;
float g_LastMusicBus = -1.0f;
float g_LastSfxBus = -1.0f;
float g_LastFilterAmount = -1.0f;
bool g_Active = true;        // the game is the window the player is looking at
bool g_EngineRunning = true;
Fader g_FocusGain;
float g_LastMasterGain = -1.0f;
std::uint32_t g_OperationSet = 0;
Rng g_Rng;

int ToIndex(GameSound sound)
{
    return static_cast<int>(sound);
}

bool IsValidSound(GameSound sound)
{
    const int index = ToIndex(sound);
    return index >= 0 && index < SOUND_COUNT;
}

const MusicDef* FindMusic(GameSound sound)
{
    for (const MusicDef& def : MUSIC)
    {
        if (def.id == sound) return &def;
    }
    return nullptr;
}

bool FourCC(const char value[4], char a, char b, char c, char d)
{
    return value[0] == a && value[1] == b && value[2] == c && value[3] == d;
}

// PCM wave file, 16 bit, one or two channels. Anything else is refused (the sound then stays silent).
// name.wav -> name.ogg: when the .ogg is next to the .wav (and OggDecode can read it: GAME_AUDIO_OGG, see core/OggDecode.h) it is used instead; the result is the same PCM in
// memory, frame for frame. The music of the game is shipped as .ogg (a tenth of the size); with no .ogg (or no decoder) nothing changes.
bool LoadOggInstead(const std::string& wavName, WAVEFORMATEX& format, Pcm& pcm)
{
    if (wavName.size() < 5 || wavName.compare(wavName.size() - 4, 4, ".wav") != 0) return false;
    std::ifstream file(wavName.substr(0, wavName.size() - 4) + ".ogg", std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const std::streamoff size = file.tellg();
    if (size <= 0 || size > 0x7FFFFFFF) return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return false;
    int channels = 0;
    int rate = 0;
    std::vector<std::int16_t> samples;
    if (!OggDecode::Decode(bytes, channels, rate, samples) || channels < 1 || channels > 2 || rate <= 0) return false;
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channels);
    if (frames == 0) return false;
    format = WAVEFORMATEX{};
    format.wFormatTag = 1; // PCM
    format.nChannels = static_cast<decltype(format.nChannels)>(channels);
    format.nSamplesPerSec = static_cast<decltype(format.nSamplesPerSec)>(rate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<decltype(format.nBlockAlign)>(channels * 2);
    format.nAvgBytesPerSec = static_cast<decltype(format.nAvgBytesPerSec)>(rate * channels * 2);
    pcm.data.resize(frames * static_cast<std::size_t>(channels) * 2);
    std::memcpy(pcm.data.data(), samples.data(), pcm.data.size());
    pcm.frames = static_cast<std::uint32_t>(frames);
    return true;
}

bool LoadWaveFile(const std::string& fileName, WAVEFORMATEX& format, Pcm& pcm)
{
    if (LoadOggInstead(fileName, format, pcm)) return true;
    std::ifstream file(fileName, std::ios::binary);
    if (!file.is_open()) return false;
    char riffId[4]{};
    std::uint32_t riffSize = 0;
    char waveId[4]{};
    file.read(riffId, 4);
    file.read(reinterpret_cast<char*>(&riffSize), sizeof(riffSize));
    file.read(waveId, 4);
    if (file.fail() || !FourCC(riffId, 'R', 'I', 'F', 'F') || !FourCC(waveId, 'W', 'A', 'V', 'E')) return false;
    bool formatFound = false;
    bool dataFound = false;
    while (file.good() && (!formatFound || !dataFound))
    {
        char chunkId[4]{};
        std::uint32_t chunkSize = 0;
        file.read(chunkId, 4);
        file.read(reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize));
        if (file.fail()) break;
        if (FourCC(chunkId, 'f', 'm', 't', ' '))
        {
            if (chunkSize < 16) return false;
            std::vector<std::uint8_t> buffer(chunkSize);
            file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
            if (file.fail()) return false;
            ZeroMemory(&format, sizeof(WAVEFORMATEX));
            CopyMemory(&format, buffer.data(), chunkSize < sizeof(WAVEFORMATEX) ? chunkSize : sizeof(WAVEFORMATEX));
            if (format.wFormatTag != WAVE_FORMAT_PCM || format.wBitsPerSample != 16) return false;
            if (format.nChannels < 1 || format.nChannels > 2 || format.nBlockAlign == 0) return false;
            format.cbSize = 0;
            formatFound = true;
        }
        else if (FourCC(chunkId, 'd', 'a', 't', 'a'))
        {
            pcm.data.resize(chunkSize);
            file.read(reinterpret_cast<char*>(pcm.data.data()), chunkSize);
            if (file.fail()) return false;
            dataFound = true;
        }
        else
        {
            file.seekg(chunkSize, std::ios::cur);
        }
        if (chunkSize % 2 != 0) file.seekg(1, std::ios::cur);
    }
    if (!formatFound || !dataFound || pcm.data.empty()) return false;
    pcm.frames = static_cast<std::uint32_t>(pcm.data.size() / format.nBlockAlign);
    return pcm.frames > 0;
}

bool SameFormat(const WAVEFORMATEX& a, const WAVEFORMATEX& b)
{
    return a.nChannels == b.nChannels && a.nSamplesPerSec == b.nSamplesPerSec && a.wBitsPerSample == b.wBitsPerSample;
}

// assets/audio/bgm/loops.txt: "title<TAB>frames". A track that has a line plays the part before it once (the intro) and loops from there.
std::uint32_t FindLoopBegin(const std::string& baseName)
{
    std::ifstream file("assets/audio/bgm/loops.txt");
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || line[0] == '#') continue;
        std::string key = line.substr(0, tab);
        if (key.size() > 4 && key.compare(key.size() - 4, 4, ".wav") == 0) key.resize(key.size() - 4); // the old format listed the file
        if (key == baseName) return static_cast<std::uint32_t>(std::strtoul(line.c_str() + tab + 1, nullptr, 10));
    }
    return 0;
}

void DestroyVoices(SfxData& data)
{
    for (IXAudio2SourceVoice*& voice : data.voices)
    {
        if (voice == nullptr) continue;
        voice->Stop();
        voice->FlushSourceBuffers();
        voice->DestroyVoice();
        voice = nullptr;
    }
    data.voiceCount = 0;
    data.nextVoice = 0;
}

void DestroyVoices(MusicData& m)
{
    for (IXAudio2SourceVoice*& voice : m.voices)
    {
        if (voice == nullptr) continue;
        voice->Stop();
        voice->FlushSourceBuffers();
        voice->DestroyVoice();
        voice = nullptr;
    }
    m.voiceCount = 0;
}

std::string VariantFile(const char* file, int variant)
{
    std::string name = file;
    if (variant <= 0) return name;
    const size_t dot = name.rfind(".wav");
    if (dot == std::string::npos) return name;
    return name.substr(0, dot) + "_" + std::to_string(variant + 1) + ".wav";
}

// where the voices of a sound play into: its bus (or, when the buses could not be made, the sum of the effects)
IXAudio2SubmixVoice* BusFor(GameSound sound)
{
    IXAudio2SubmixVoice* bus = g_Bus[static_cast<int>(AudioMap::BusOf(sound))];
    return bus != nullptr ? bus : g_SfxBus;
}

bool LoadSound(const SoundDef& def)
{
    if (g_XAudio2 == nullptr || def.music) return false;
    SfxData& data = g_Sfx[ToIndex(def.id)];
    DestroyVoices(data);
    data.variants.clear();
    data.loaded = false;
    data.lastPlay = NEVER; // (a second Initialize() starts the frame counter again: a stale time would block the sound for a while)
    data.lastVariant = -1;
    for (int v = 0; v < def.variants; ++v)
    {
        Pcm pcm;
        WAVEFORMATEX format{};
        if (!LoadWaveFile(VariantFile(def.file, v), format, pcm))
        {
            if (v == 0) return false;
            continue; // a missing variant is skipped, the sound still plays with the others
        }
        if (data.variants.empty())
            data.format = format;
        else if (!SameFormat(data.format, format))
            continue;
        data.variants.push_back(std::move(pcm));
    }
    if (data.variants.empty()) return false;
    IXAudio2SubmixVoice* target = BusFor(def.id);
    XAUDIO2_SEND_DESCRIPTOR send{0, target};
    XAUDIO2_VOICE_SENDS sends{1, &send};
    data.voiceCount = def.voices < 1 ? 1 : (def.voices > MAX_VOICES ? MAX_VOICES : def.voices);
    for (int i = 0; i < data.voiceCount; ++i)
    {
        data.voiceEnd[i] = NEVER;
        const HRESULT hr = g_XAudio2->CreateSourceVoice(&data.voices[i], &data.format, 0, MAX_PITCH_RATIO, nullptr, target != nullptr ? &sends : nullptr,
                                                         nullptr);
        if (FAILED(hr))
        {
            DestroyVoices(data);
            data.variants.clear();
            return false;
        }
    }
    data.loaded = true;
    return true;
}

bool LoadMusic(const MusicDef& def)
{
    if (g_XAudio2 == nullptr) return false;
    MusicData& m = g_Music[ToIndex(def.id)];
    DestroyVoices(m);
    m.tracks.clear();
    m.loaded = false;
    int maxStems = 0;
    for (int t = 0; t < def.trackCount; ++t)
    {
        TrackData track;
        bool good = true;
        const int stems = def.tracks[t].stems < 1 ? 1 : (def.tracks[t].stems > MAX_STEMS ? MAX_STEMS : def.tracks[t].stems);
        for (int s = 0; s < stems && good; ++s)
        {
            std::string path = std::string("assets/audio/bgm/") + def.tracks[t].name;
            path += def.tracks[t].stems == 1 ? std::string(".wav") : ("_s" + std::to_string(s) + ".wav");
            Pcm pcm;
            WAVEFORMATEX format{};
            if (!LoadWaveFile(path, format, pcm))
            {
                good = false;
                break;
            }
            if (s == 0)
                track.frames = pcm.frames;
            else if (pcm.frames != track.frames)
                good = false; // the layers of a track must be exactly as long as each other: they loop together
            if (!m.formatSet[s])
            {
                m.stemFormat[s] = format;
                m.formatSet[s] = true;
            }
            else if (!SameFormat(m.stemFormat[s], format))
            {
                good = false;
            }
            track.stems.push_back(std::move(pcm));
        }
        if (good)
        {
            track.loopBegin = FindLoopBegin(def.tracks[t].name);
            if (track.loopBegin >= track.frames) track.loopBegin = 0;
            track.ok = true;
            if (static_cast<int>(track.stems.size()) > maxStems) maxStems = static_cast<int>(track.stems.size());
        }
        else
        {
            char message[200];
            std::snprintf(message, sizeof message, "GameAudio: cannot load the music track %s\n", def.tracks[t].name);
            OutputDebugStringA(message);
        }
        m.tracks.push_back(std::move(track));
    }
    if (maxStems == 0) return false;
    XAUDIO2_SEND_DESCRIPTOR send{0, g_MusicBus};
    XAUDIO2_VOICE_SENDS sends{1, &send};
    for (int s = 0; s < maxStems; ++s)
    {
        const HRESULT hr = g_XAudio2->CreateSourceVoice(&m.voices[s], &m.stemFormat[s], 0, MAX_PITCH_RATIO, nullptr, g_MusicBus != nullptr ? &sends : nullptr,
                                                         nullptr);
        if (FAILED(hr))
        {
            DestroyVoices(m);
            return false;
        }
        if (m.stemFormat[s].nChannels == 1 && g_MusicBus != nullptr)
        {
            const float both[2] = {1.0f, 1.0f}; // a mono layer is the same on both sides
            m.voices[s]->SetOutputMatrix(g_MusicBus, 1, 2, both);
        }
    }
    m.voiceCount = maxStems;
    m.loaded = true;
    return true;
}

// ---- the world outside: is this the window the player is looking at? ---------------------------------------------------------------------
bool IsGameForeground(bool previous)
{
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) return previous; // a window is being switched: keep the state
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId(); // (the black window around a 16:9 picture belongs to the game, too)
}

// ---- buses ------------------------------------------------------------------------------------------------------------------------------
float MusicBusGain()
{
    const float pause = 1.0f - g_PauseAmount * (1.0f - PAUSE_MUSIC_GAIN);
    const float pick = 1.0f - g_PickAmount * (1.0f - PICK_MUSIC_GAIN);
    return VolumeCurve(g_BgmSlider) * g_Duck.gain * pause * pick * (1.0f - TENSION_GAIN_DROP * g_Tension);
}

// The setting CUE EMPHASIS (0 .. 100 %, 50 by default): the alerts are lifted by 0 .. 4 dB over the mix as it is made, and they push the rest down the harder the
// higher it is (half as hard at 0 %, one and a half times at 100 %).
float CueEmphasis()
{
    return static_cast<float>(GameSettings::Get().cuePercent) / 100.0f;
}

float CueBoostGain()
{
    return DbToGain(4.0f * CueEmphasis());
}

float CueDuckScale()
{
    return 0.5f + CueEmphasis();
}

void ApplyBusVolumes(bool force)
{
    const float music = g_BgmEnabled ? MusicBusGain() : 0.0f;
    const float sfx = g_SeEnabled ? VolumeCurve(g_SeSlider) : 0.0f;
    // the buses of the effects: ducked by the cues, the body of the fight steps back when many voices play, the alerts are lifted by the setting
    for (int b = 0; b < BUS_COUNT; ++b)
    {
        if (g_Bus[b] == nullptr) continue;
        float gain = g_BusDuck[b].gain;
        const AudioMap::Bus bus = static_cast<AudioMap::Bus>(b);
        if (bus == AudioMap::Bus::Player || bus == AudioMap::Bus::World) gain *= g_Density;
        if (bus == AudioMap::Bus::Alert) gain *= CueBoostGain();
        if (force || gain != g_LastBusGain[b])
        {
            g_Bus[b]->SetVolume(gain);
            g_LastBusGain[b] = gain;
        }
    }
    if (g_Bus[static_cast<int>(AudioMap::Bus::World)] != nullptr && g_ReverbBus != nullptr && (force || std::fabs(g_ReverbSend - g_LastReverbSend) > 0.004f))
    {
        const float level[4] = {g_ReverbSend, 0.0f, 0.0f, g_ReverbSend};
        g_Bus[static_cast<int>(AudioMap::Bus::World)]->SetOutputMatrix(g_ReverbBus, 2, 2, level);
        g_LastReverbSend = g_ReverbSend;
    }
    if (g_MusicBus != nullptr && (force || music != g_LastMusicBus))
    {
        g_MusicBus->SetVolume(music);
        g_LastMusicBus = music;
    }
    if (g_SfxBus != nullptr && (force || sfx != g_LastSfxBus))
    {
        g_SfxBus->SetVolume(sfx);
        g_LastSfxBus = sfx;
    }
}

// one low-pass filter for the whole music bus: open at amount 0, muffled at 1 (the pause menu); the card screen is part of the way
void ApplyBusFilter(float amount)
{
    if (g_MusicBus == nullptr) return;
    XAUDIO2_FILTER_PARAMETERS filter{};
    filter.Type = LowPassFilter;
    filter.OneOverQ = 1.0f;
    const float cutoff = 16000.0f * std::pow(PAUSE_CUTOFF_HZ / 16000.0f, amount);
    // (a state variable filter: frequency = 2 sin(pi fc / fs); 1.0 is wide open)
    filter.Frequency = amount <= 0.001f ? 1.0f : (std::min)(1.0f, 2.0f * std::sin(PI * cutoff / static_cast<float>(g_BusRate)));
    g_MusicBus->SetFilterParameters(&filter);
}

// ---- music --------------------------------------------------------------------------------------------------------------------------------
int MusicLevel(const MusicDef& def);

// The target of every layer of the track that plays: the levels of the heat, or the two faces (TrackDef::polar)
void TrackTargets(const MusicDef& def, const MusicData& m, float* out)
{
    const int polar = (m.track >= 0 && m.track < def.trackCount) ? def.tracks[m.track].polar : 0;
    if (polar > 0)
        PolarLayerTargets(m.layers, MusicLevel(def), g_PickTarget, polar == 2, out);
    else
        LayerTargets(m.layers, false, g_Boss.stage, MusicLevel(def), g_PickTarget, out); // (the boss themes are layered by heat, not by phase)
}

void SetLayerVolumes(const MusicDef& def, MusicData& m)
{
    const bool polar = m.track >= 0 && m.track < def.trackCount && def.tracks[m.track].polar > 0;
    for (int s = 0; s < m.layers && s < m.voiceCount; ++s)
    {
        if (m.voices[s] == nullptr) continue;
        float face = 1.0f; // the weight of the colour: layer 1 is the blue face, layer 2 the red one
        if (polar && s == 1) face = g_Polar.BlueGain();
        if (polar && s == 2) face = g_Polar.RedGain();
        m.voices[s]->SetVolume(m.stem[s].value * face * m.master.value * DbToGain(def.gainDb));
    }
}

void StopMusicNow(MusicData& m)
{
    for (int s = 0; s < m.voiceCount; ++s)
    {
        if (m.voices[s] == nullptr) continue;
        m.voices[s]->Stop();
        m.voices[s]->FlushSourceBuffers();
    }
    m.state = MusicState::Idle;
    m.master.Jump(0.0f);
}

int MusicLevel(const MusicDef& def)
{
    return def.boss ? (std::max)(1, g_Director.level) : g_Director.level;
}

void StartMusic(const MusicDef& def, MusicData& m, bool loop)
{
    if (!m.loaded || !g_BgmEnabled || m.voiceCount <= 0) return; // (also while the game is in the background: it plays when the player is back)
    // which track: another one than the last time, when there are several
    int usable = 0;
    for (const TrackData& t : m.tracks)
        usable += t.ok ? 1 : 0;
    if (usable == 0) return;
    int choice = m.track;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        choice = g_Rng.Pick(static_cast<int>(m.tracks.size()), usable > 1 ? m.lastTrack : -1);
        if (m.tracks[static_cast<size_t>(choice)].ok) break;
    }
    if (!m.tracks[static_cast<size_t>(choice)].ok)
    {
        for (size_t i = 0; i < m.tracks.size(); ++i)
        {
            if (m.tracks[i].ok)
            {
                choice = static_cast<int>(i);
                break;
            }
        }
    }
    m.track = choice;
    m.lastTrack = choice;
    TrackData& track = m.tracks[static_cast<size_t>(choice)];
    m.layers = static_cast<int>(track.stems.size());
    if (def.startsBoss) g_Boss.Start();
    TrackTargets(def, m, m.stemTarget);
    for (int s = 0; s < m.voiceCount; ++s)
    {
        if (m.voices[s] == nullptr) continue;
        m.voices[s]->Stop();
        m.voices[s]->FlushSourceBuffers();
    }
    m.master.Jump(0.0f);
    m.master.FadeTo(1.0f, def.fadeIn);
    for (int s = 0; s < MAX_STEMS; ++s)
        m.stemDelay[s] = -1;
    for (int s = 0; s < m.layers; ++s)
        m.stem[s].Jump(m.stemTarget[s]);
    SetLayerVolumes(def, m);
    // all layers start in the same operation set: they begin on the same sample and stay together for the whole loop
    const UINT32 operationSet = ++g_OperationSet;
    bool any = false;
    for (int s = 0; s < m.layers && s < m.voiceCount; ++s)
    {
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(track.stems[static_cast<size_t>(s)].data.size());
        buffer.pAudioData = track.stems[static_cast<size_t>(s)].data.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        if (loop)
        {
            buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
            buffer.LoopBegin = track.loopBegin;
            buffer.LoopLength = track.frames - track.loopBegin;
        }
        if (FAILED(m.voices[s]->SubmitSourceBuffer(&buffer))) continue;
        m.voices[s]->Start(0, operationSet);
        any = true;
    }
    if (any)
    {
        g_XAudio2->CommitChanges(operationSet);
        m.state = MusicState::Playing;
    }
}

// Where the music is now: the tempo, the rate of the files and the position in the file. False when the track has no tempo (or does not play).
bool ReadMusicClock(const MusicDef& def, MusicData& m, double& samplesPerBeat, int& rate, long long& position)
{
    if (m.state != MusicState::Playing || !m.loaded || m.track < 0 || m.track >= def.trackCount || m.voices[0] == nullptr) return false;
    const float bpm = def.tracks[m.track].bpm;
    if (bpm <= 1.0f) return false;
    const TrackData& track = m.tracks[static_cast<size_t>(m.track)];
    XAUDIO2_VOICE_STATE state{};
    m.voices[0]->GetState(&state, 0);
    if (state.BuffersQueued == 0) return false;
    rate = static_cast<int>(m.stemFormat[0].nSamplesPerSec);
    samplesPerBeat = SamplesPerBeat(rate, bpm);
    position = PositionInFile(state.SamplesPlayed, track.loopBegin, track.frames);
    return samplesPerBeat > 0.0;
}

// the clock of whatever music with a tempo is playing now (the stage, or the boss): used by the stingers
bool ReadAnyMusicClock(double& samplesPerBeat, int& rate, long long& position)
{
    for (const MusicDef& def : MUSIC)
    {
        if (ReadMusicClock(def, g_Music[ToIndex(def.id)], samplesPerBeat, rate, position)) return true;
    }
    return false;
}

void UpdateMusic(const MusicDef& def, MusicData& m)
{
    if (m.state == MusicState::Idle) return;
    m.master.Update();
    if (m.state == MusicState::Playing)
    {
        float target[MAX_STEMS]{};
        TrackTargets(def, m, target);
        for (int s = 0; s < m.layers; ++s)
        {
            if (target[s] == m.stemTarget[s]) continue;
            m.stemTarget[s] = target[s];
            const bool pick = g_PickTarget || g_PickAmount > 0.0f;
            const bool rising = target[s] > m.stem[s].value;
            // a layer that comes in waits for the next bar (at most 1.7 s; a bar is 1.4 - 2 s long: when the bar is too far the grid falls back to two beats, then one beat);
            // a layer that leaves, and everything on the card screen, does not wait
            m.stemDelay[s] = -1;
            if (rising && !pick)
            {
                double samplesPerBeat = 0.0;
                int rate = 0;
                long long position = 0;
                if (ReadMusicClock(def, m, samplesPerBeat, rate, position))
                {
                    const int wait = QuantizedWaitSteps(position, samplesPerBeat, rate, 4.0, 100);
                    if (wait > 0)
                    {
                        m.stemDelay[s] = wait;
                        m.stemDelayTarget[s] = target[s];
                        continue;
                    }
                }
            }
            m.stem[s].FadeTo(target[s], pick ? LAYER_FADE_PICK : (rising ? LAYER_FADE_IN : LAYER_FADE_OUT));
        }
        for (int s = 0; s < m.layers; ++s)
        {
            if (m.stemDelay[s] < 0) continue;
            if (--m.stemDelay[s] <= 0)
            {
                m.stemDelay[s] = -1;
                m.stem[s].FadeTo(m.stemDelayTarget[s], LAYER_FADE_IN);
            }
        }
    }
    for (int s = 0; s < m.layers; ++s)
        m.stem[s].Update();
    SetLayerVolumes(def, m);
    if (m.state == MusicState::FadingOut && m.master.value <= 0.0f) StopMusicNow(m);
}

// ---- effects ----------------------------------------------------------------------------------------------------------------------------
// A cue (an alert) lowers the player and the world for as long as it sounds, so that it is always heard (AudioMap.h: CueDuckPlayerDb / CueDuckWorldDb; the
// setting CUE EMPHASIS makes it softer or harder). The ALERT bus itself and the UI are never ducked.
void DuckForCue(GameSound sound, int steps)
{
    const float player = AudioMap::CueDuckPlayerDb(sound);
    const float world = AudioMap::CueDuckWorldDb(sound);
    if (player >= 0.0f && world >= 0.0f) return;
    const float scale = CueDuckScale();
    const int hold = steps < 12 ? 12 : (steps > 90 ? 90 : steps);
    if (player < 0.0f) g_BusDuck[static_cast<int>(AudioMap::Bus::Player)].Trigger(DbToGain(player * scale), hold);
    if (world < 0.0f) g_BusDuck[static_cast<int>(AudioMap::Bus::World)].Trigger(DbToGain(world * scale), hold);
}

void PlayEffect(const SoundDef& def, SfxData& data, float pan, float extraSemitones = 0.0f, float level = 1.0f)
{
    if (!data.loaded || !g_SeEnabled || !g_Active || data.voiceCount <= 0) return;
    if (g_Frame - data.lastPlay < def.cooldown) return;
    // what may be dropped: a burst of hits must not push out a warning (AudioLogic::StepBudget, and the count of the voices that play)
    const Priority priority = AudioMap::PriorityOf(def.id);
    const bool important = def.duckDb < 0.0f || priority == Priority::Critical;
    if (!important)
    {
        if (!g_Budget.Allow(priority)) return;
        if (priority == Priority::Low && g_ActiveVoices >= ACTIVE_VOICES_LOW) return;
        if (priority == Priority::Normal && g_ActiveVoices >= ACTIVE_VOICES_NORMAL) return;
    }
    // a free voice, else the oldest one
    IXAudio2SourceVoice* voice = nullptr;
    int voiceIndex = -1;
    for (int offset = 0; offset < data.voiceCount && voice == nullptr; ++offset)
    {
        const int index = (data.nextVoice + offset) % data.voiceCount;
        IXAudio2SourceVoice* candidate = data.voices[index];
        if (candidate == nullptr) continue;
        XAUDIO2_VOICE_STATE state{};
        candidate->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (state.BuffersQueued == 0)
        {
            data.nextVoice = (index + 1) % data.voiceCount;
            voice = candidate;
            voiceIndex = index;
        }
    }
    if (voice == nullptr)
    {
        voiceIndex = data.nextVoice;
        voice = data.voices[voiceIndex];
        data.nextVoice = (data.nextVoice + 1) % data.voiceCount;
    }
    if (voice == nullptr) return;
    data.lastPlay = g_Frame;
    g_Budget.Note(priority);
    voice->Stop();
    voice->FlushSourceBuffers();
    // which file: one of the variants, never the same twice in a row
    const int variant = g_Rng.Pick(static_cast<int>(data.variants.size()), data.lastVariant);
    data.lastVariant = variant;
    const Pcm& pcm = data.variants[static_cast<size_t>(variant)];
    // pitch: a small random spread, the climb of a chain, and what the caller asked for
    float semitones = (def.pitchVar > 0.0f ? g_Rng.Signed() * def.pitchVar : 0.0f) + extraSemitones;
    int chainStep = 0;
    if (def.chain)
    {
        chainStep = data.chain.Hit(g_Frame, 24, 5);
        semitones += ChainSemitones(chainStep);
    }
    const float ratio = SemitonesToRatio(semitones);
    voice->SetFrequencyRatio(ratio);
    voice->SetVolume(DbToGain(def.gainDb) * level);
    // where it sounds: mono effects are panned, stereo ones keep their picture (balance only)
    float left = 0.0f, right = 0.0f;
    PanGains(pan, left, right);
    IXAudio2SubmixVoice* bus = BusFor(def.id);
    if (bus != nullptr)
    {
        float matrix[4];
        const UINT32 channels = data.format.nChannels;
        if (channels == 1)
        {
            matrix[0] = left;
            matrix[1] = right;
        }
        else
        {
            matrix[0] = left;
            matrix[1] = 0.0f;
            matrix[2] = 0.0f;
            matrix[3] = right;
        }
        voice->SetOutputMatrix(bus, channels, 2, matrix);
    }
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(pcm.data.size());
    buffer.pAudioData = pcm.data.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    if (FAILED(voice->SubmitSourceBuffer(&buffer))) return;
    voice->Start();
    // how long the voice is busy (in steps), for the count of the voices that play
    const float seconds = static_cast<float>(pcm.frames) / static_cast<float>(data.format.nSamplesPerSec) / (ratio > 0.1f ? ratio : 0.1f);
    const int steps = static_cast<int>(seconds * 60.0f) + 2;
    if (voiceIndex >= 0 && voiceIndex < MAX_VOICES) data.voiceEnd[voiceIndex] = g_Frame + steps;
    if (def.duckDb < 0.0f) g_Duck.Trigger(DbToGain(def.duckDb), def.duckFrames);
    DuckForCue(def.id, steps);
    // the 3rd and 5th absorb of a chain are rewarded with a chime (a fifth higher the second time)
    if (def.chain && (chainStep == 3 || chainStep == 5))
    {
        const int chime = ToIndex(GameSound::AbsorbChime);
        PlayEffect(SOUNDS[chime], g_Sfx[chime], pan, chainStep == 5 ? 7.0f : 0.0f);
    }
}

// the voices that play now (every sound counts its voices' ends)
int CountActiveVoices()
{
    int count = 0;
    for (const SfxData& data : g_Sfx)
    {
        for (int i = 0; i < data.voiceCount; ++i)
            if (data.voiceEnd[i] > g_Frame) ++count;
    }
    return count;
}

// ---- sounds that wait for a beat ---------------------------------------------------------------------------------------------------------------
// A stinger (WaveClear) lands on the next beat of the music: its wait is worked out from the clock of the music (the beat, or a finer grid if the beat is too far).
// Without music that has a tempo it plays at once.
bool QueuePending(GameSound sound, float x, int steps)
{
    for (Pending& p : g_Pending)
    {
        if (p.used) continue;
        p.used = true;
        p.sound = sound;
        p.x = x;
        p.steps = steps;
        return true;
    }
    return false;
}

void UpdatePending()
{
    for (Pending& p : g_Pending)
    {
        if (!p.used) continue;
        if (--p.steps > 0) continue;
        p.used = false;
        const int index = ToIndex(p.sound);
        PlayEffect(SOUNDS[index], g_Sfx[index], p.x * 0.6f);
    }
}

void ClearPending()
{
    for (Pending& p : g_Pending)
        p.used = false;
}

// what the game just did, whether or not the sound can be played: it heats the music up, and tells the boss phase
void NoteEvent(GameSound sound, const SoundDef& def)
{
    if (def.heat > 0.0f) g_Director.Add(def.heat);
    if (sound == GameSound::BossPhase)
        g_Boss.OnPhaseSound();
    else if (sound == GameSound::BossWarning)
        g_Boss.Start();
}

GameSound HitSoundFor(int enemyType, bool elite, bool red)
{
    if (!elite) return red ? GameSound::EnemyHitRed : GameSound::EnemyHitBlue; // the light fighters answer the colour of the shot: glass or ink
    switch (enemyType)
    {
    case 2: // heavy
    case 3: // charger
    case 4: // turret
        return GameSound::EnemyHitArmor;
    case 5: // sniper
        return GameSound::EnemyHitGlass;
    case 6: // rival
        return GameSound::EnemyHitClash;
    default:
        return GameSound::EliteHit;
    }
}

void SetActive(bool active)
{
    g_Active = active;
    if (g_XAudio2 == nullptr) return;
    if (active)
    {
        if (!g_EngineRunning)
        {
            g_XAudio2->StartEngine();
            g_EngineRunning = true;
        }
        g_FocusGain.FadeTo(1.0f, 12);
    }
    else
    {
        g_FocusGain.FadeTo(0.0f, 6);
    }
}

// the first slider of the audio page (MASTER) and the fade of a window that loses the focus: one volume on the mastering voice
void ApplyMasterVolume()
{
    if (g_MasteringVoice == nullptr) return;
    const float master = g_FocusGain.value * VolumeCurve(static_cast<float>(GameSettings::Get().masterPercent) / 100.0f);
    if (master != g_LastMasterGain)
    {
        g_MasteringVoice->SetVolume(master);
        g_LastMasterGain = master;
    }
}

void UpdateFocus()
{
    // MUTE IN THE BACKGROUND (a setting, on by default): off = the sound goes on when the window is not the one in front
    g_Foreground = IsGameForeground(g_Foreground);
    const bool active = g_Foreground || !GameSettings::Get().muteInBackground;
    if (active != g_Active) SetActive(active);
    g_FocusGain.Update();
    ApplyMasterVolume();
    // faded out: the engine stops (everything is paused where it is: the music goes on from the same place when the player comes back)
    if (!g_Active && g_FocusGain.value <= 0.0f && g_EngineRunning)
    {
        g_XAudio2->StopEngine();
        g_EngineRunning = false;
    }
}

// ---- the limiter on the output: an XAPO of our own on the mastering voice -------------------------------------------------------------------
// The sum of the voices must never clip (a clipped sum sounds harsh, and the louder the fight the harsher). The work is AudioLogic::LookaheadLimiter (tested in
// tools/audio/test_audio_logic.cpp); this is only the shell that XAudio2 wants. It delays the sound by 1.5 ms and does nothing at all under the ceiling.
#if GAME_AUDIO_LIMITER
class __declspec(uuid("5C0A2F7E-6E44-4B8A-9C51-2D2B7B1F0A11")) OutputLimiterXapo : public IXAPO
{
public:
    OutputLimiterXapo() = default;

    // ---- IUnknown
    STDMETHOD(QueryInterface)(REFIID iid, void** object) override
    {
        if (object == nullptr) return E_POINTER;
        if (IsEqualIID(iid, __uuidof(IXAPO)) || IsEqualIID(iid, __uuidof(IUnknown)))
        {
            *object = static_cast<IXAPO*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&m_Ref));
    }
    STDMETHOD_(ULONG, Release)() override
    {
        const LONG n = InterlockedDecrement(&m_Ref);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }

    // ---- IXAPO
    STDMETHOD(GetRegistrationProperties)(XAPO_REGISTRATION_PROPERTIES** properties) override
    {
        if (properties == nullptr) return E_POINTER;
        // (XAudio2 frees it with CoTaskMemFree)
        *properties = static_cast<XAPO_REGISTRATION_PROPERTIES*>(CoTaskMemAlloc(sizeof(XAPO_REGISTRATION_PROPERTIES)));
        if (*properties == nullptr) return E_OUTOFMEMORY;
        std::memset(*properties, 0, sizeof(XAPO_REGISTRATION_PROPERTIES));
        (*properties)->clsid = __uuidof(OutputLimiterXapo);
        wcscpy_s((*properties)->FriendlyName, L"Output limiter");
        wcscpy_s((*properties)->CopyrightInfo, L"-");
        (*properties)->MajorVersion = 1;
        (*properties)->MinorVersion = 0;
        (*properties)->Flags = XAPO_FLAG_CHANNELS_MUST_MATCH | XAPO_FLAG_FRAMERATE_MUST_MATCH | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH |
                               XAPO_FLAG_INPLACE_SUPPORTED;
        (*properties)->MinInputBufferCount = 1;
        (*properties)->MaxInputBufferCount = 1;
        (*properties)->MinOutputBufferCount = 1;
        (*properties)->MaxOutputBufferCount = 1;
        return S_OK;
    }
    STDMETHOD(IsInputFormatSupported)(const WAVEFORMATEX*, const WAVEFORMATEX* requested, WAVEFORMATEX** supported) override
    {
        if (supported != nullptr) *supported = nullptr;
        return FormatOk(requested);
    }
    STDMETHOD(IsOutputFormatSupported)(const WAVEFORMATEX*, const WAVEFORMATEX* requested, WAVEFORMATEX** supported) override
    {
        if (supported != nullptr) *supported = nullptr;
        return FormatOk(requested);
    }
    STDMETHOD(Initialize)(const void*, UINT32) override
    {
        return S_OK;
    }
    STDMETHOD_(void, Reset)() override
    {
        m_Limiter.Reset();
    }
    STDMETHOD(LockForProcess)(UINT32 inputCount, const XAPO_LOCKFORPROCESS_PARAMETERS* inputParameters, UINT32 outputCount,
                              const XAPO_LOCKFORPROCESS_PARAMETERS* outputParameters) override
    {
        if (inputCount != 1 || outputCount != 1 || inputParameters == nullptr || outputParameters == nullptr || inputParameters[0].pFormat == nullptr) return E_INVALIDARG;
        const HRESULT hr = FormatOk(inputParameters[0].pFormat);
        if (FAILED(hr)) return hr;
        m_Limiter.Configure(static_cast<int>(inputParameters[0].pFormat->nChannels), static_cast<int>(inputParameters[0].pFormat->nSamplesPerSec));
        return S_OK;
    }
    STDMETHOD_(void, UnlockForProcess)() override {}
    STDMETHOD_(void, Process)(UINT32, const XAPO_PROCESS_BUFFER_PARAMETERS* inputParameters, UINT32, XAPO_PROCESS_BUFFER_PARAMETERS* outputParameters,
                              BOOL isEnabled) override
    {
        outputParameters[0].ValidFrameCount = inputParameters[0].ValidFrameCount;
        outputParameters[0].BufferFlags = inputParameters[0].BufferFlags;
        if (inputParameters[0].BufferFlags == XAPO_BUFFER_SILENT)
        {
            m_Limiter.Reset(); // (the buffer of a silent frame is not to be read)
            return;
        }
        if (outputParameters[0].pBuffer != inputParameters[0].pBuffer)
        {
            // (not in place: copy; the flag XAPO_FLAG_INPLACE_SUPPORTED lets XAudio2 give us one buffer, but it may give two)
            std::memcpy(outputParameters[0].pBuffer, inputParameters[0].pBuffer, static_cast<size_t>(inputParameters[0].ValidFrameCount) * m_FrameBytes);
        }
        if (!isEnabled) return;
        m_Limiter.Process(static_cast<float*>(outputParameters[0].pBuffer), static_cast<int>(inputParameters[0].ValidFrameCount));
    }
    STDMETHOD_(UINT32, CalcInputFrames)(UINT32 outputFrames) override
    {
        return outputFrames;
    }
    STDMETHOD_(UINT32, CalcOutputFrames)(UINT32 inputFrames) override
    {
        return inputFrames;
    }

private:
    // the mixer of XAudio2 works in 32 bit float: that is all that the limiter takes
    HRESULT FormatOk(const WAVEFORMATEX* format)
    {
        if (format == nullptr) return E_POINTER;
        const bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || format->wFormatTag == WAVE_FORMAT_EXTENSIBLE;
        if (!isFloat || format->wBitsPerSample != 32 || format->nChannels < 1 || format->nChannels > static_cast<WORD>(AudioLogic::LookaheadLimiter::MAX_CHANNELS))
            return XAPO_E_FORMAT_UNSUPPORTED;
        m_FrameBytes = static_cast<size_t>(format->nChannels) * sizeof(float);
        return S_OK;
    }

    volatile LONG m_Ref = 1;
    size_t m_FrameBytes = 8;
    AudioLogic::LookaheadLimiter m_Limiter;
};

bool AttachLimiter(UINT32 channels)
{
    if (g_MasteringVoice == nullptr) return false;
    OutputLimiterXapo* xapo = new (std::nothrow) OutputLimiterXapo();
    if (xapo == nullptr) return false;
    XAUDIO2_EFFECT_DESCRIPTOR descriptor{};
    descriptor.pEffect = static_cast<IXAPO*>(xapo);
    descriptor.InitialState = TRUE;
    descriptor.OutputChannels = channels;
    XAUDIO2_EFFECT_CHAIN chain{1, &descriptor};
    const HRESULT hr = g_MasteringVoice->SetEffectChain(&chain);
    xapo->Release(); // (the voice holds it now)
    return SUCCEEDED(hr);
}
#endif

// ---- the buses of the effects -------------------------------------------------------------------------------------------------------------------
// UI, PLAYER, WORLD and ALERT play into the sum of the effects (g_SfxBus, the slider). The world has a second send, to a reverb bus. If something cannot be made
// everything falls back to the old way: the voices play straight into the sum of the effects.
void DestroyEffectBuses()
{
    for (IXAudio2SubmixVoice*& bus : g_Bus)
    {
        if (bus == nullptr) continue;
        bus->DestroyVoice();
        bus = nullptr;
    }
    if (g_ReverbBus != nullptr)
    {
        g_ReverbBus->DestroyVoice();
        g_ReverbBus = nullptr;
    }
}

bool BuildEffectBuses()
{
    DestroyEffectBuses();
    for (int b = 0; b < BUS_COUNT; ++b)
        g_LastBusGain[b] = -1.0f;
    g_LastReverbSend = -1.0f;
    if (g_XAudio2 == nullptr || g_SfxBus == nullptr) return false;
#if GAME_AUDIO_REVERB
    {
        IUnknown* reverb = nullptr;
        if (SUCCEEDED(XAudio2CreateReverb(&reverb)) && reverb != nullptr)
        {
            XAUDIO2_EFFECT_DESCRIPTOR descriptor{};
            descriptor.pEffect = reverb;
            descriptor.InitialState = TRUE;
            descriptor.OutputChannels = 2;
            XAUDIO2_EFFECT_CHAIN chain{1, &descriptor};
            XAUDIO2_SEND_DESCRIPTOR send{0, g_SfxBus};
            XAUDIO2_VOICE_SENDS sends{1, &send};
            if (SUCCEEDED(g_XAudio2->CreateSubmixVoice(&g_ReverbBus, 2, REVERB_RATE, 0, STAGE_REVERB, &sends, &chain)))
            {
                // a small hall: short, dark, only the wet signal (the dry one goes to the sum of the effects directly)
                XAUDIO2FX_REVERB_I3DL2_PARAMETERS room{};
                room.WetDryMix = 100.0f;
                room.Room = -1000;
                room.RoomHF = -800;
                room.RoomRolloffFactor = 0.0f;
                room.DecayTime = 1.1f;
                room.DecayHFRatio = 0.55f;
                room.Reflections = -600;
                room.ReflectionsDelay = 0.012f;
                room.Reverb = -300;
                room.ReverbDelay = 0.022f;
                room.Diffusion = 70.0f;
                room.Density = 80.0f;
                room.HFReference = 5000.0f;
                XAUDIO2FX_REVERB_PARAMETERS native{};
                ReverbConvertI3DL2ToNative(&room, &native);
                g_ReverbBus->SetEffectParameters(0, &native, sizeof(native));
            }
            else
            {
                g_ReverbBus = nullptr;
            }
            reverb->Release();
        }
    }
#endif
    for (int b = 0; b < BUS_COUNT; ++b)
    {
        XAUDIO2_SEND_DESCRIPTOR sends[2]{};
        sends[0] = XAUDIO2_SEND_DESCRIPTOR{0, g_SfxBus};
        UINT32 count = 1;
        if (b == static_cast<int>(AudioMap::Bus::World) && g_ReverbBus != nullptr)
        {
            sends[1] = XAUDIO2_SEND_DESCRIPTOR{0, g_ReverbBus};
            count = 2;
        }
        XAUDIO2_VOICE_SENDS list{count, sends};
        if (FAILED(g_XAudio2->CreateSubmixVoice(&g_Bus[b], 2, g_BusRate, 0, STAGE_BUS, &list, nullptr)))
        {
            DestroyEffectBuses();
            return false;
        }
    }
    return true;
}

// ---- the boss: one theme, three tempos ------------------------------------------------------------------------------------------------
constexpr GameSound BOSS_THEME[3] = {GameSound::BossBgm, GameSound::BossPhase2Bgm, GameSound::BossBerserkBgm}; // phase 1, 2, 3

bool BossMusicPlaying()
{
    for (const GameSound sound : BOSS_THEME)
    {
        if (g_Music[ToIndex(sound)].state == MusicState::Playing) return true;
    }
    return false;
}

// The boss has changed phase (g_Boss.stage is the new one): if a boss theme is playing, the theme of the new phase takes over. The new track starts from
// its first beat (a hit), with every layer open: the music is at its hottest at the moment of the change and then follows the fight as usual.
void SwitchBossMusic()
{
    int playing = -1;
    for (int i = 0; i < 3; ++i)
    {
        if (g_Music[ToIndex(BOSS_THEME[i])].state == MusicState::Playing) playing = i;
    }
    const int wanted = g_Boss.stage;
    if (playing < 0 || playing == wanted) return;
    const MusicDef* next = FindMusic(BOSS_THEME[wanted]);
    MusicData& nextData = g_Music[ToIndex(BOSS_THEME[wanted])];
    if (next == nullptr || !nextData.loaded) return; // the files of that phase are missing: the old theme goes on
    for (int i = 0; i < 3; ++i)
    {
        if (i == wanted) continue;
        const MusicDef* def = FindMusic(BOSS_THEME[i]);
        MusicData& m = g_Music[ToIndex(BOSS_THEME[i])];
        if (def == nullptr || m.state != MusicState::Playing) continue;
        m.master.FadeTo(0.0f, def->fadeOut);
        m.state = MusicState::FadingOut;
    }
    g_Director.heat = (std::max)(g_Director.heat, 0.9f);
    g_Director.level = 2;
    g_Director.dwell = Director::DWELL_FRAMES;
    StartMusic(*next, nextData, true);
}
}

namespace
{
// ---- the audio device is lost  -------------------------------------------------------------------------------------------------
// Windows 10 / 11 move XAudio2 to the new default device by themselves (headphones plugged in or out). What is left is OnCriticalError: the device is gone for good
// (a driver reset, the only device pulled out). XAudio2 then needs a NEW engine. The callback runs on a thread of XAudio2 and only sets a flag; Update() - the game
// thread - does the work: it remembers which music plays, Finalize() + Initialize(), starts the same music again (from its start: the position is lost) and gives
// the settings back. With no device at all it tries again every two seconds.
#ifndef STDMETHOD_
#define STDMETHOD_(type, name) virtual type STDMETHODCALLTYPE name
#endif
std::atomic<bool> g_DeviceLost{false};
class EngineCallback : public IXAudio2EngineCallback
{
public:
    STDMETHOD_(void, OnProcessingPassStart)() override {}
    STDMETHOD_(void, OnProcessingPassEnd)() override {}
    STDMETHOD_(void, OnCriticalError)(HRESULT) override { g_DeviceLost.store(true); }
};
EngineCallback g_EngineCallback;
struct SavedAudio
{
    GameSound music[8]{};
    int count = 0;
    int bossStage = 0;
    bool bossHinted = false;
    bool bgm = true, se = true, paused = false, pick = false;
};
SavedAudio g_Saved;
bool g_Recovering = false;
int g_RecoverWait = 0; // steps until the next try (the first one is at once)
void RecoverDevice()
{
    if (!g_Recovering)
    {
        if (!g_DeviceLost.load()) return;
        g_Recovering = true;
        g_Saved = SavedAudio();
        for (const MusicDef& def : MUSIC)
        {
            if (g_Music[ToIndex(def.id)].state == MusicState::Playing && g_Saved.count < 8) g_Saved.music[g_Saved.count++] = def.id;
        }
        g_Saved.bossStage = g_Boss.stage;
        g_Saved.bossHinted = g_Boss.hinted;
        g_Saved.bgm = g_BgmEnabled;
        g_Saved.se = g_SeEnabled;
        g_Saved.paused = g_PauseTarget;
        g_Saved.pick = g_PickTarget;
        OutputDebugStringA("GameAudio: the audio device was lost; the engine is built again.\n");
        GameAudio::Finalize();
        g_RecoverWait = 0;
    }
    if (g_RecoverWait > 0)
    {
        --g_RecoverWait;
        return;
    }
    if (!GameAudio::Initialize())
    {
        g_RecoverWait = 120; // no device yet: again in two seconds
        return;
    }
    g_Recovering = false;
    g_DeviceLost.store(false);
    GameAudio::SetBgmEnabled(g_Saved.bgm);
    GameAudio::SetSeEnabled(g_Saved.se);
    GameAudio::SetPaused(g_Saved.paused);
    GameAudio::SetPickScreen(g_Saved.pick);
    if (g_Saved.bgm)
    {
        for (int i = 0; i < g_Saved.count; ++i)
            GameAudio::PlayLoop(g_Saved.music[i]);
    }
    if (BossMusicPlaying()) // (the phase of the boss: after the PlayLoop calls, which start the count again)
    {
        g_Boss.stage = g_Saved.bossStage;
        g_Boss.hinted = g_Saved.bossHinted;
        g_Boss.frames = BossStage::IGNORE_FRAMES;
    }
}
}
namespace
{
// ---- the fight keeps the music warm  (the floor: core/AudioHeat.h; it is applied at the start of every step, see Update())
void ApplyHeatFloor()
{
    const float minimum = HeatFloorFor(g_Fight.active, g_Fight.alive, g_Fight.bossFight, g_Boss.stage, g_Fight.overload);
    if (g_Director.heat < minimum) g_Director.heat = minimum;
}
}
bool GameAudio::Initialize()
{
    if (FAILED(XAudio2Create(&g_XAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
    {
        g_XAudio2 = nullptr;
        return false;
    }
    if (FAILED(g_XAudio2->CreateMasteringVoice(&g_MasteringVoice)))
    {
        g_XAudio2->Release();
        g_XAudio2 = nullptr;
        g_MasteringVoice = nullptr;
        return false;
    }
    g_DeviceLost.store(false); // the engine tells us when the device is lost (OnCriticalError)
    g_XAudio2->RegisterForCallbacks(&g_EngineCallback);
    XAUDIO2_VOICE_DETAILS details{};
    g_MasteringVoice->GetVoiceDetails(&details);
    g_BusRate = details.InputSampleRate > 0 ? details.InputSampleRate : 48000;
    // the two buses: music (with a filter) and effects. If they cannot be made the voices play straight to the speakers.
    if (FAILED(g_XAudio2->CreateSubmixVoice(&g_MusicBus, 2, g_BusRate, XAUDIO2_VOICE_USEFILTER, 0, nullptr, nullptr))) g_MusicBus = nullptr;
    if (FAILED(g_XAudio2->CreateSubmixVoice(&g_SfxBus, 2, g_BusRate, 0, STAGE_SFX))) g_SfxBus = nullptr;
    BuildEffectBuses(); // (the four buses of the effects and the reverb; false = the voices play into g_SfxBus as before)
#if GAME_AUDIO_LIMITER
    if (!AttachLimiter(details.InputChannels > 0 ? details.InputChannels : 2)) OutputDebugStringA("GameAudio: the limiter could not be attached.\n");
#endif
    g_Frame = 0;
    g_Budget.Reset();
    g_ActiveVoices = 0;
    g_Density = 1.0f;
    for (Ducker& d : g_BusDuck)
        d = Ducker();
    ClearPending();
    g_Fight = GameAudio::FightState();
    g_Heart = Heartbeat();
    g_Danger = 0.0f;
    g_Tension = 0.0f;
    g_ReverbSend = REVERB_SEND_BASE;
    g_Foreground = true;
    g_Duck = Ducker();
    g_Director = Director();
    g_Boss = BossStage();
    g_PauseAmount = 0.0f;
    g_PauseTarget = false;
    g_PickAmount = 0.0f;
    g_PickTarget = false;
    g_BgmEnabled = true;
    g_SeEnabled = true;
    g_Active = true;
    g_EngineRunning = true;
    g_FocusGain.Jump(1.0f);
    g_LastMasterGain = -1.0f;
    g_LastFilterAmount = -1.0f;
    for (int i = 0; i < SOUND_COUNT; ++i)
    {
        if (SOUNDS[i].id != static_cast<GameSound>(i))
        {
            OutputDebugStringA("GameAudio: the table SOUNDS is not in the order of GameSound.\n");
            return true;
        }
        if (SOUNDS[i].music) continue;
        if (!LoadSound(SOUNDS[i]))
        {
            char message[200];
            std::snprintf(message, sizeof message, "GameAudio: cannot load %s\n", SOUNDS[i].file);
            OutputDebugStringA(message);
        }
    }
    for (const MusicDef& def : MUSIC)
        LoadMusic(def);
    ApplyBusVolumes(true);
    return true;
}

void GameAudio::Finalize()
{
    if (g_XAudio2 != nullptr) g_XAudio2->UnregisterForCallbacks(&g_EngineCallback);
    StopAll();
    for (SfxData& data : g_Sfx)
    {
        DestroyVoices(data);
        data.variants.clear();
        data.loaded = false;
    }
    for (MusicData& m : g_Music)
    {
        DestroyVoices(m);
        m.tracks.clear();
        m.loaded = false;
    }
    if (g_MusicBus != nullptr)
    {
        g_MusicBus->DestroyVoice();
        g_MusicBus = nullptr;
    }
    DestroyEffectBuses();
    if (g_SfxBus != nullptr)
    {
        g_SfxBus->DestroyVoice();
        g_SfxBus = nullptr;
    }
    if (g_MasteringVoice != nullptr)
    {
        g_MasteringVoice->DestroyVoice();
        g_MasteringVoice = nullptr;
    }
    if (g_XAudio2 != nullptr)
    {
        g_XAudio2->Release();
        g_XAudio2 = nullptr;
    }
}

void GameAudio::Update()
{
    ApplyHeatFloor();
    RecoverDevice();
    if (g_XAudio2 == nullptr) return;
    UpdateFocus();
    if (!g_EngineRunning) return; // the game is in the background: nothing moves, nothing plays
    ++g_Frame;
    g_Budget.Reset();
    g_Duck.Update();
    for (Ducker& d : g_BusDuck)
        d.Update();
    UpdatePending();
    // how many voices play: the body of the fight (the player's and the world's buses) steps back when it is crowded; smoothed, so that it is not heard as a pumping
    g_ActiveVoices = CountActiveVoices();
    g_Density += (DensityGain(g_ActiveVoices) - g_Density) * (DensityGain(g_ActiveVoices) < g_Density ? 0.30f : 0.06f);
    // the fight: a low HP muffles the music a little and has a heartbeat, an overload makes the music hold its breath, the boss fight opens the reverb
    {
        const GameSettings::Values& settings = GameSettings::Get();
        const float dangerWanted = g_Fight.active ? DangerAmount(g_Fight.hpRate, g_Fight.alive) : 0.0f;
        g_Danger += (dangerWanted - g_Danger) * 0.06f;
        const float tensionWanted = (g_Fight.active && g_Fight.bossFight && g_Fight.overload) ? 0.45f + 0.55f * Clamp01(g_Fight.overloadRate) : 0.0f;
        g_Tension += (tensionWanted - g_Tension) * (tensionWanted > g_Tension ? 0.08f : 0.035f);
        if (dangerWanted <= 0.0f && g_Danger < 0.0015f) g_Danger = 0.0f; // (the fade is asymptotic: it ends)
        if (tensionWanted <= 0.0f && g_Tension < 0.0015f) g_Tension = 0.0f;
        const float sendWanted = g_Fight.active && g_Fight.bossFight ? REVERB_SEND_BOSS : REVERB_SEND_BASE;
        g_ReverbSend += (sendWanted - g_ReverbSend) * 0.02f;
        const bool beatAllowed = g_Fight.active && g_Fight.alive && settings.heartbeat && g_SeEnabled;
        if (g_Heart.Update(g_Fight.hpRate, beatAllowed))
        {
            const int index = ToIndex(GameSound::LowHpPulse);
            PlayEffect(SOUNDS[index], g_Sfx[index], 0.0f, 0.0f, g_Heart.level);
        }
    }
    // pause: the music is lowered and muffled over about 8 steps; the card screen does the same, less
    const float pauseGoal = g_PauseTarget ? 1.0f : 0.0f;
    if (g_PauseAmount < pauseGoal)
        g_PauseAmount = (std::min)(pauseGoal, g_PauseAmount + PAUSE_STEP);
    else if (g_PauseAmount > pauseGoal)
        g_PauseAmount = (std::max)(pauseGoal, g_PauseAmount - PAUSE_STEP);
    const float pickGoal = g_PickTarget ? 1.0f : 0.0f;
    if (g_PickAmount < pickGoal)
        g_PickAmount = (std::min)(pickGoal, g_PickAmount + PICK_STEP);
    else if (g_PickAmount > pickGoal)
        g_PickAmount = (std::max)(pickGoal, g_PickAmount - PICK_STEP);
    const float dangerAmount = g_Danger > 0.0f ? DANGER_FILTER_BASE + DANGER_FILTER_SPAN * g_Danger : 0.0f;
    const float tensionAmount = g_Tension > 0.0f ? TENSION_FILTER_BASE + TENSION_FILTER_SPAN * g_Tension : 0.0f;
    const float filterAmount = (std::max)((std::max)(g_PauseAmount, g_PickAmount * PICK_CUTOFF_AMOUNT), (std::max)(dangerAmount, tensionAmount));
    if (std::fabs(filterAmount - g_LastFilterAmount) > 0.002f || (filterAmount == 0.0f && g_LastFilterAmount != 0.0f))
    {
        ApplyBusFilter(filterAmount);
        g_LastFilterAmount = filterAmount;
    }
    // the director: how much is happening, and in which phase the boss is
    g_Director.Update(BossMusicPlaying() ? 1 : 0);
    g_Polar.Update(g_PolarityRed); // (the faces of the music follow the colour, slowly)
    g_Boss.Update();
    for (const MusicDef& def : MUSIC)
        UpdateMusic(def, g_Music[ToIndex(def.id)]);
    ApplyBusVolumes(false);
}

void GameAudio::Play(GameSound sound)
{
    PlayAt(sound, 0.0f);
}

void GameAudio::PlayAt(GameSound sound, float x)
{
    if (g_XAudio2 == nullptr || !IsValidSound(sound)) return;
    const SoundDef& def = SOUNDS[ToIndex(sound)];
    NoteEvent(sound, def);
    if (def.music)
    {
        const MusicDef* music = FindMusic(sound);
        if (music != nullptr) StartMusic(*music, g_Music[ToIndex(sound)], false);
    }
    else
    {
        // a stinger waits for the next beat of the music (when there is music with a tempo and the wait is short enough)
        const double grid = AudioMap::GridBeatsOf(sound);
        if (grid > 0.0)
        {
            double samplesPerBeat = 0.0;
            int rate = 0;
            long long position = 0;
            if (ReadAnyMusicClock(samplesPerBeat, rate, position))
            {
                const int wait = QuantizedWaitSteps(position, samplesPerBeat, rate, grid, AudioMap::GridMaxWaitSteps(sound));
                if (wait > 0 && QueuePending(sound, x, wait)) return;
            }
        }
        PlayEffect(def, g_Sfx[ToIndex(sound)], x * 0.6f); // (the picture is narrower than the screen: the sound of the far edge is not all in one ear)
    }
}

void GameAudio::PlayEnemyHit(int enemyType, bool elite, float x, bool red)
{
    PlayAt(HitSoundFor(enemyType, elite, red), x);
}

// The two colours sound different (version 3 / 4)
void GameAudio::PlayPolaritySwitch(bool toRed, float x)
{
    PlayAt(toRed ? GameSound::PolaritySwitchRed : GameSound::PolaritySwitchBlue, x);
}

void GameAudio::PlayAbsorb(bool red)
{
    Play(red ? GameSound::AbsorbRed : GameSound::AbsorbBlue); // the chain of the colour: the pitch climbs, the chime on the 3rd and 5th
}

void GameAudio::PlayPlayerShot(bool red)
{
    Play(red ? GameSound::PlayerShotRed : GameSound::PlayerShotBlue);
}

void GameAudio::SetMusicPolarity(bool red)
{
    g_PolarityRed = red;
}

void GameAudio::PlayLoop(GameSound sound)
{
    if (g_XAudio2 == nullptr || !IsValidSound(sound)) return;
    const MusicDef* music = FindMusic(sound);
    if (music == nullptr) return;
    MusicData& m = g_Music[ToIndex(sound)];
    if (m.state == MusicState::Playing) return; // already playing: it goes on (a new wave must not restart the music)
    if (m.state == MusicState::FadingOut && music->resume && m.loaded)
    {
        // A Stop() and a PlayLoop() of the same music in a row is not a restart: the music that was fading out comes back from where it is.
        // (The pages of the menu go back to the title with ReturnToTitle(), which stops all music and starts the title music again.)
        m.state = MusicState::Playing;
        m.master.FadeTo(1.0f, music->fadeIn);
        return;
    }
    StartMusic(*music, m, true);
}

void GameAudio::Stop(GameSound sound)
{
    if (!IsValidSound(sound)) return;
    if (sound == GameSound::BossBgm) Stop(GameSound::BossPhase2Bgm); // the theme of phase 2 belongs to BossBgm: one Stop ends the boss music
    const SoundDef& def = SOUNDS[ToIndex(sound)];
    if (!def.music)
    {
        SfxData& data = g_Sfx[ToIndex(sound)];
        for (int i = 0; i < data.voiceCount; ++i)
        {
            if (data.voices[i] == nullptr) continue;
            data.voices[i]->Stop();
            data.voices[i]->FlushSourceBuffers();
        }
        data.nextVoice = 0;
        return;
    }
    const MusicDef* music = FindMusic(sound);
    MusicData& m = g_Music[ToIndex(sound)];
    if (music == nullptr || m.state != MusicState::Playing) return;
    if (music->fadeOut <= 0)
    {
        StopMusicNow(m);
        return;
    }
    m.master.FadeTo(0.0f, music->fadeOut);
    m.state = MusicState::FadingOut;
}

void GameAudio::StopAll()
{
    ClearPending();
    for (int i = 0; i < SOUND_COUNT; ++i)
    {
        SfxData& data = g_Sfx[i];
        for (int v = 0; v < data.voiceCount; ++v)
        {
            if (data.voices[v] == nullptr) continue;
            data.voices[v]->Stop();
            data.voices[v]->FlushSourceBuffers();
        }
        data.nextVoice = 0;
        StopMusicNow(g_Music[i]);
    }
}

void GameAudio::SetPaused(bool paused)
{
    g_PauseTarget = paused;
}

void GameAudio::SetPickScreen(bool open)
{
    g_PickTarget = open;
}

void GameAudio::SetFightState(const FightState& state)
{
    g_Fight = state;
    g_Fight.hpRate = Clamp01(g_Fight.hpRate);
}

void GameAudio::SetBossPhase(int phase)
{
    g_Boss.SetHint(phase);
    if (g_XAudio2 != nullptr) SwitchBossMusic();
}

void GameAudio::OnWindowActivate(bool active)
{
    if (g_XAudio2 == nullptr) return;
    if (!active && !GameSettings::Get().muteInBackground) return; // (MUTE IN THE BACKGROUND is off: the sound goes on)
    if (!active)
    {
        // at once: no waiting for the next update step (the window may not get one)
        g_Active = false;
        g_FocusGain.Jump(0.0f);
        if (g_MasteringVoice != nullptr) g_MasteringVoice->SetVolume(0.0f);
        g_LastMasterGain = 0.0f;
        if (g_EngineRunning)
        {
            g_XAudio2->StopEngine();
            g_EngineRunning = false;
        }
    }
    else
    {
        SetActive(true);
    }
}

void GameAudio::SetBgmEnabled(bool enabled)
{
    g_BgmEnabled = enabled;
    if (!g_BgmEnabled)
    {
        for (int i = 0; i < SOUND_COUNT; ++i)
            StopMusicNow(g_Music[i]);
    }
    ApplyBusVolumes(false);
}

void GameAudio::SetSeEnabled(bool enabled)
{
    g_SeEnabled = enabled;
    if (!g_SeEnabled)
    {
        for (int i = 0; i < SOUND_COUNT; ++i)
        {
            SfxData& data = g_Sfx[i];
            for (int v = 0; v < data.voiceCount; ++v)
            {
                if (data.voices[v] == nullptr) continue;
                data.voices[v]->Stop();
                data.voices[v]->FlushSourceBuffers();
            }
        }
    }
    ApplyBusVolumes(false);
}

bool GameAudio::IsBgmEnabled()
{
    return g_BgmEnabled;
}

bool GameAudio::IsSeEnabled()
{
    return g_SeEnabled;
}

void GameAudio::SetBgmVolume(float volume)
{
    g_BgmSlider = Clamp01(volume);
    ApplyBusVolumes(false);
}

void GameAudio::SetSeVolume(float volume)
{
    g_SeSlider = Clamp01(volume);
    ApplyBusVolumes(false);
}

float GameAudio::GetBgmVolume()
{
    return g_BgmSlider;
}

float GameAudio::GetSeVolume()
{
    return g_SeSlider;
}






