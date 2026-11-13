#include "rogue/Modules.h"
#include "game/GameEnemy.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/GameWave.h"
#include "boss/GameBoss.h"
#include "rogue/Balance.h"
#include "rogue/ModulesInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// Challenge director: enemy hp / bullet speed per challenge level, extra elites, the wave route.
namespace Mod
{
// ---------------------------------------------------------------- challenge director (levels, elite fleet, supply run)
// Works from the outside: it adjusts objects that the waves have just spawned and adds extra elites
// with GameEnemy::Spawn, so GameWave.cpp / GameEnemy.cpp stay untouched.
bool IsPlaying()
{
    return !GameWave::IsWaveStarting() && !GameWave::IsWaveCleared() && !GameWave::IsAllFinished();
}

float BulletSpeedScale()
{
    return 1.0f + LEVEL_BULLET_STEP * static_cast<float>(g_RunLevel);
}

float EnemyHpScale()
{
    return 1.0f + LEVEL_HP_STEP * static_cast<float>(g_RunLevel);
}

// extra elites that arrive during a wave
int ElitesPlanned(int wave)
{
    int count = 0;
    if (wave == ROUTE_WAVE && g_Branch == 1) count += 1; // elite fleet: one more elite in wave 3 and one in wave 4
    if (wave == ROUTE_WAVE + 1 && g_Branch == 1) count += 1;
    if (wave == 1 && g_RunLevel >= 3) count += 1;
    if (wave == 2 && g_RunLevel >= 2) count += 1;
    if (wave == 3 && g_RunLevel >= 1) count += 1;
    if (wave == 4 && g_RunLevel >= 3) count += 1;
    return count;
}

// the extra elites: one of the four templates
EnemyType EliteType(int wave, int index)
{
    static const EnemyType TEMPLATES[4] = {EnemyType::Turret, EnemyType::Sniper, EnemyType::Heavy, EnemyType::Charger};
    if (wave == 1) return EnemyType::Charger;
    return TEMPLATES[(std::rand() + index) % 4];
}

bool IsEliteType(const Enemy& enemy)
{
    return enemy.elite || enemy.type == EnemyType::Rival;
}

int ActiveEnemyCount()
{
    int count = 0;
    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        if (GameState::enemies[i].active) ++count;
    return count;
}

bool EliteOnField()
{
    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
    {
        if (GameState::enemies[i].active && IsEliteType(GameState::enemies[i])) return true;
    }
    return false;
}

// objects that appeared since the last frame: enemies get their hp scaled, bullets their speed
void DirectorScaleNew()
{
    if (IsPlaying() && !GameBoss::IsActive())
    {
        for (int i = 0; i < GameState::ENEMY_MAX; ++i)
            if (GameState::enemies[i].active && !g_PrevEnemyActive[i]) ++g_WaveSpawned;
    }
    const float hpScale = EnemyHpScale();
    const float speedScale = BulletSpeedScale();

    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
    {
        Enemy& enemy = GameState::enemies[i];
        if (!enemy.active || g_PrevEnemyActive[i] || enemy.maxHp < 2 * Balance::HP_UNIT || hpScale == 1.0f) continue; // (scouts keep their one shot)
        enemy.maxHp = (std::max)(1, static_cast<int>(std::lround(static_cast<float>(enemy.maxHp) * hpScale)));
        enemy.hp = (std::min)(enemy.maxHp, (std::max)(1, static_cast<int>(std::lround(static_cast<float>(enemy.hp) * hpScale))));
    }

    if (speedScale == 1.0f) return;
    for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
    {
        EnemyBullet& bullet = GameState::enemyBullets[i];
        if (!bullet.active || g_PrevBulletActive[i]) continue;
        bullet.speed *= speedScale;
        bullet.velocityX *= speedScale;
        bullet.velocityY *= speedScale;
    }
}

void DirectorSpawnElites()
{
    if (!IsPlaying()) return;

    const int wave = GameWave::GetCurrentWave();
    if (g_ElitesSpawned >= ElitesPlanned(wave)) return;

    const int due = ELITE_FIRST_FRAME + g_ElitesSpawned * ELITE_GAP_FRAMES;
    const bool fieldEmpty = ActiveEnemyCount() == 0 && g_PlayFrames >= ELITE_EARLY_MIN_FRAME;
    if (g_PlayFrames < due && !fieldEmpty) return;
    if (EliteOnField() || ActiveEnemyCount() > GameState::ENEMY_MAX - 5) return;

    const EnemyType type = EliteType(wave, g_ElitesSpawned);
    const float y = (static_cast<float>((g_ElitesSpawned * 2 + wave) % 3) - 1.0f) * 0.42f;
    const Polarity polarity = (std::rand() % 2 == 0) ? Polarity::Red : Polarity::Blue;

    if (GameEnemy::SpawnElite(GameState::enemies, GameState::ENEMY_MAX, type, polarity, y, 7000 + wave * 10 + g_ElitesSpawned))
    {
        ++g_ElitesSpawned;
        WriteLogRow("elite", type == EnemyType::Heavy ? "heavy" : (type == EnemyType::Turret ? "turret" : (type == EnemyType::Sniper ? "sniper" : "charger")));
    }
}

void DirectorSnapshot()
{
    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        g_PrevEnemyActive[i] = GameState::enemies[i].active;
    for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
        g_PrevBulletActive[i] = GameState::enemyBullets[i].active;
}
}
