#pragma once
#include "core/AudioLogic.h"
#include "core/GameAudio.h"

// Which bus a sound plays on, how much it matters, whether it ducks the others, and whether it waits for a beat. One place for the opinions about the sounds of
// the game, so that GameAudio.cpp stays a mechanism. (Plain C++: tools/audio/test_audio_map.cpp checks that every sound has an answer.)
//
//   UI      menus, the card screen: never in the way, never ducked
//   PLAYER  the ship: shots, absorbs, repairs, the tactics, the modules
//   WORLD   enemies and the boss: hits, explosions, the patterns of the boss (this bus has the reverb send)
//   ALERT   what the player has to react to or must not miss: warnings, the alarm, the break of an overload
namespace AudioMap
{
enum class Bus
{
    Ui = 0,
    Player = 1,
    World = 2,
    Alert = 3,
    Count = 4
};

inline Bus BusOf(GameSound s)
{
    switch (s)
    {
    case GameSound::MenuCursor:
    case GameSound::MenuDecide:
    case GameSound::MenuMove:
    case GameSound::MenuAdjust:
    case GameSound::MenuApply:
    case GameSound::MenuError:
    case GameSound::MenuOpen:
    case GameSound::UiHint:
    case GameSound::Unlock:
    case GameSound::SynergyFound:
    case GameSound::CutInWave:
        return Bus::Ui;
    case GameSound::SniperLock:
    case GameSound::ChargerWarning:
    case GameSound::BossCannonCharge:
    case GameSound::BossDashWarning:
    case GameSound::BossLaserCharge:
    case GameSound::BossWarning:
    case GameSound::WhiteWarn:
    case GameSound::OverloadAlarm:
    case GameSound::OverloadBreak:
        return Bus::Alert;
    case GameSound::EnemyHitBlue:
    case GameSound::EnemyHitRed:
    case GameSound::EliteHit:
    case GameSound::EnemyExplosion:
    case GameSound::EliteExplosion:
    case GameSound::SniperFire:
    case GameSound::ChargerDash:
    case GameSound::RivalTwin:
    case GameSound::BossPhase:
    case GameSound::BossBerserk:
    case GameSound::BossCannonFire:
    case GameSound::BossCannonBreak:
    case GameSound::BossLaserFire:
    case GameSound::BossHit:
    case GameSound::BossExplosion:
    case GameSound::BossFinalExplosion:
    case GameSound::BossArmor:
    case GameSound::BossVent:
    case GameSound::BossSwap:
    case GameSound::BossFlip:
    case GameSound::EnemyHitArmor:
    case GameSound::EnemyHitGlass:
    case GameSound::EnemyHitClash:
        return Bus::World;
    default:
        return Bus::Player; // the ship, the modules, the tactics, the heartbeat
    }
}

inline AudioLogic::Priority PriorityOf(GameSound s)
{
    using P = AudioLogic::Priority;
    switch (s)
    {
    case GameSound::PlayerDamage:
    case GameSound::PlayerDeath:
    case GameSound::BossWarning:
    case GameSound::BossPhase:
    case GameSound::BossBerserk:
    case GameSound::BossFinalExplosion:
    case GameSound::WhiteWarn:
    case GameSound::OverloadAlarm:
    case GameSound::OverloadBreak:
    case GameSound::BackupRevive:
        return P::Critical;
    case GameSound::PolaritySwitchRed:
    case GameSound::PolaritySwitchBlue:
    case GameSound::EnergyFull:
    case GameSound::AbsorbChime:
    case GameSound::SpecialShot:
    case GameSound::Heal:
    case GameSound::WaveClear:
    case GameSound::SynergyFound:
    case GameSound::Unlock:
    case GameSound::TacticDecoy:
    case GameSound::TacticDash:
    case GameSound::AegisBlock:
    case GameSound::RivalTwin:
    case GameSound::LowHpPulse:
    case GameSound::SniperLock:
    case GameSound::SniperFire:
    case GameSound::ChargerWarning:
    case GameSound::BossCannonCharge:
    case GameSound::BossDashWarning:
    case GameSound::BossLaserCharge:
    case GameSound::BossLaserFire:
    case GameSound::BossCannonBreak:
    case GameSound::BossExplosion:
    case GameSound::EliteExplosion:
        return P::High;
    case GameSound::PlayerShotBlue:
    case GameSound::PlayerShotRed:
    case GameSound::EnemyHitBlue:
    case GameSound::EnemyHitRed:
    case GameSound::EnemyHitArmor:
    case GameSound::EnemyHitGlass:
    case GameSound::EnemyHitClash:
    case GameSound::BossHit:
    case GameSound::BossArmor:
    case GameSound::BossFlip:
        return P::Low;
    default:
        return P::Normal;
    }
}

// A cue that ducks the player and the world while it sounds, in dB (negative), for the length of the sound (the engine reads it from the file). 0 = none.
inline float CueDuckPlayerDb(GameSound s)
{
    switch (s)
    {
    case GameSound::WhiteWarn:
    case GameSound::OverloadAlarm:
    case GameSound::OverloadBreak:
        return -2.5f;
    case GameSound::BossLaserCharge:
    case GameSound::BossDashWarning:
    case GameSound::BossCannonCharge:
    case GameSound::SniperLock:
    case GameSound::ChargerWarning:
        return -1.5f;
    default:
        return 0.0f;
    }
}

inline float CueDuckWorldDb(GameSound s)
{
    switch (s)
    {
    case GameSound::WhiteWarn:
    case GameSound::OverloadAlarm:
    case GameSound::OverloadBreak:
        return -4.5f;
    case GameSound::BossLaserCharge:
    case GameSound::BossDashWarning:
    case GameSound::BossCannonCharge:
    case GameSound::SniperLock:
    case GameSound::ChargerWarning:
        return -2.5f;
    default:
        return 0.0f;
    }
}

// A sound that lands on a line of the music: in beats (4 = the next bar, 1 = the next beat). 0 = at once. `maxWaitSteps` is how long it may wait.
inline double GridBeatsOf(GameSound s)
{
    return s == GameSound::WaveClear ? 1.0 : 0.0;
}

inline int GridMaxWaitSteps(GameSound s)
{
    return s == GameSound::WaveClear ? 20 : 0;
}
}


