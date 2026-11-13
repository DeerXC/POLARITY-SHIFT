#pragma once

enum class GameSound
{
    // ---- music (the layers and the tracks: GameAudio.cpp, MUSIC)
    TitleBgm,
    StageBgm,
    BossBgm,
    BossBerserkBgm,
    ClearBgm,
    GameOverBgm,
    BossPhase2Bgm, // the theme of phase 2 (BossBgm = phase 1, BossBerserkBgm = phase 3): the engine switches between them, see SetBossPhase()
    // ---- interface: the two colours fused (a click of ink that opens into a ring of glass); quiet, short, never in the way
    MenuCursor,
    MenuDecide,
    MenuMove,
    MenuAdjust,
    MenuApply,
    MenuError,
    MenuOpen,
    UiHint,
    Unlock,       // something bought in the hangar
    SynergyFound, // a pair of modules has combined for the first time
    // ---- the ship. The two colours have a voice each (version 4): BLUE = glass (soft attack, a ring), RED = ink (a click, a blade, no tail)
    PlayerShotBlue, // the quietest sounds of the pack: 4 variants each
    PlayerShotRed,
    PolaritySwitchBlue, // the ship switches TO blue
    PolaritySwitchRed,  // ... TO red
    AbsorbBlue,         // the chain climbs a pentatonic scale while it repeats quickly; a chime on the 3rd and 5th
    AbsorbRed,
    AbsorbChime,        // played by the engine on top of the 3rd and 5th absorb of a chain
    EnergyFull,
    PlayerDamage,
    PlayerDeath,
    SpecialShot,        // the breach beam
    Heal,               // the ship is repaired
    AegisBlock,         // the shield took a white bullet
    BackupRevive,       // the backup core brings the ship back
    EchoArc,            // the arc of ECHO
    TacticDecoy,
    TacticDash,
    LowHpPulse,         // the heartbeat of a nearly lost ship: played by the engine itself (see SetFightState)
    // ---- enemies (the hit of a shot takes the colour of the shot; the elites have a material: armour, glass, blades)
    EnemyHitBlue,
    EnemyHitRed,
    EliteHit,
    EnemyHitArmor,
    EnemyHitGlass,
    EnemyHitClash,
    EnemyExplosion,
    EliteExplosion,
    SniperLock,     // the sniper takes aim: the cue (two pings of a cold sine)
    SniperFire,
    ChargerWarning, // the charger locks its lane: the cue (a growl that rises)
    ChargerDash,
    RivalTwin,      // the second rival of the duel (wave 5) joins: a low horn and a glass note that meet (it was the whine of the boss cannon: far too sharp)
    // ---- the boss
    BossWarning,
    BossPhase,
    BossBerserk,
    BossCannonCharge,
    BossCannonFire,
    BossCannonBreak,
    BossDashWarning,
    BossLaserCharge,
    BossLaserFire,
    BossHit,
    BossArmor,
    BossExplosion,
    BossFinalExplosion,
    BossVent,
    BossSwap,
    BossFlip,
    WhiteWarn,     // the announcement of a volley of white needles (three pings that climb)
    OverloadAlarm, // the boss charges an overload: the alarm
    OverloadBreak, // ... and the player breaks it
    // ---- the flow of a run
    WaveClear,     // a wave is cleared: a stinger that lands on a beat of the music
    CutInWave,     // the blue card of a new wave
    Count
};

// Audio. Two buses (music, sound effects) with their own volume, a table with the mix of every sound (GameAudio.cpp, SOUNDS and MUSIC):
//   - music: PlayLoop fades in, Stop fades out, so a Stop() followed by a PlayLoop() of another track is a cross fade. A track can have an
//     intro that plays once and a loop part (assets/audio/bgm/loops.txt). A battle track is made of LAYERS (stems) that are faded in and
//     out with the action (the "heat" of the sounds that are played; boss phases; the card screen). A sound that has several tracks
//     (StageBgm) plays another one each time. The music is ducked while a big sound plays, lowered and muffled while the game is paused
//     and on the card screen, and it is silent (the whole audio is stopped) while the game is not the active window.
//   - effects: variants (name.wav, name_2.wav ...), a small random pitch spread (so that a sound that repeats does not machine-gun), a
//     minimum time between two plays, panning to the position on the screen, a pitch that climbs while a sound repeats quickly (absorb).
//   - the volume sliders of the settings page are curved (see AudioLogic::VolumeCurve).
// Version 2 (see GameAudio.cpp, AudioMap.h, AudioLogic.h):
//   - four buses for the effects (UI, PLAYER, WORLD, ALERT) under one effects bus; the cues (ALERT) lower the player and the world for as long as they sound, so that
//     they are always heard; the world has a small send to a reverb that grows in the boss fight; a limiter on the output (the sum never clips)
//   - priorities (a burst of hits cannot push out a warning), the body of the fight steps back when many voices play (AudioLogic::DensityGain)
//   - the music has a clock: the layers come in on the next bar, a stinger lands on a beat (tempo table in GameAudio.cpp, MUSIC)
//   - a low HP: heartbeat and a muffled music; an overload: the music holds its breath (SetFightState)
// The sound world (tools/audio: build_sfx.py + sfx/ for the effects, build_bgm.py + bgm6/ for the music) is ONE world with TWO faces:
//   - BLUE (P3R): glass, bells, a soft attack and a long ring.  RED (P5R): a click, a blade, metal, a hard attack and no tail.  The interface fuses the two (a click that opens into a ring).
//   - the ship, its shots, the hits on the light fighters, the absorb chain and the switch have a voice per colour (PlayPlayerShot, PlayEnemyHit(..., red), PlayAbsorb, PlayPolaritySwitch);
//   - the music has two faces too: layers 1 and 2 of a battle track play the same notes in the glass and in the ink voices, and the engine cross-fades them with the colour of the ship
//     (SetMusicPolarity, AudioLogic::PolarityMix: slowly, so that a player who switches all the time hears both);
//   - the cues that were never played (a charger that locks its lane, a sniper that takes aim) are played now; the cues lower the music for a moment;
//   - the music can be Ogg Vorbis (core/OggDecode.h).
namespace GameAudio
{
bool Initialize();
void Finalize();
void Update(); // once per game update step (1/60 s): fades, ducking, layers, pause, focus
void Play(GameSound sound);
void PlayAt(GameSound sound, float x); // x on the screen, -1 .. 1: the sound is panned (mono effects)
// The hit of a bullet on an enemy: the material decides the sound. enemyType = static_cast<int>(EnemyType) (0 scout, 1 sine, 2 heavy,
// 3 charger, 4 turret, 5 sniper, 6 rival), elite = the game's own decision. `red`: the colour of the shot that hit (the light fighters answer it in glass or in ink).
void PlayEnemyHit(int enemyType, bool elite, float x, bool red = false);
// The two colours sound different. PlayPolaritySwitch: toRed = the colour the ship (or the rival at x) has NOW, after the switch. PlayAbsorb: red = the colour of the bullet; the
// chain of that colour (the pitch climbs, the chime on the 3rd and 5th). PlayPlayerShot: the colour of the ship.
void PlayPolaritySwitch(bool toRed, float x = 0.0f);
void PlayAbsorb(bool red);
void PlayPlayerShot(bool red);
// The colour that the MUSIC follows (not at once: AudioLogic::PolarityMix). The game sets it every step with the colour of the ship; the title screen with the colour of its ship.
void SetMusicPolarity(bool red);
void PlayLoop(GameSound sound);
void Stop(GameSound sound); // music fades out, effects stop at once
void StopAll();             // at once
void SetPaused(bool paused);
void SetPickScreen(bool open); // the card / route screen is open: only the foundation of the music plays, a little muffled
// 1..3: the boss has changed phase. The boss has ONE theme in three tempos (BossBgm, BossPhase2Bgm, BossBerserkBgm): the engine switches to the
// track of the new phase at once, and every track starts with a hit, so the music lands on the flash of the transformation. Called by
// GameBoss::BeginTransition. (Without it the engine counts the "boss phase" sounds, which is less exact.)
    void SetBossPhase(int phase);
void OnWindowActivate(bool active); // WM_ACTIVATE: the audio stops at once when the window is deactivated (Update() also notices it)
void SetBgmEnabled(bool enabled);
void SetSeEnabled(bool enabled);
bool IsBgmEnabled();
bool IsSeEnabled();
// What the engine needs to know about the fight to score it, once per update step (Game_Update): the heartbeat and the muffled music of a ship that is nearly
// lost, the tension of the music while an overload charges. `active`: the fight is running (not in a menu, not paused, not on the card screen).
struct FightState
{
    bool active = false;
    bool alive = true;
    float hpRate = 1.0f;       // 0..1
    bool bossFight = false;
    bool overload = false;     // an overload of the boss is charging
    float overloadRate = 0.0f; // 0..1: how far
};
void SetFightState(const FightState& state);
// (The master volume, the emphasis of the cues, the heartbeat and the sound in the background are settings: core/GameSettings.h. The engine reads them every step.)
void SetBgmVolume(float volume); // the position of the slider, 0..1
void SetSeVolume(float volume);
float GetBgmVolume();
float GetSeVolume();
}




