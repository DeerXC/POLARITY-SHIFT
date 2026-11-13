#include "game/GameState.h"

namespace GameState
{
Scene scene = Scene::Title;

Player player{};
Enemy enemies[ENEMY_MAX]{};
EnemyBullet enemyBullets[ENEMY_BULLET_MAX]{};

int shotCoolTime = 0;
int hitStop = 0;

int score = 0;
int energy = 0;
int energyFlashTimer = 0;

int combo = 0;
int maxCombo = 0;
int comboRankEffectTimer = 0;
int comboRankEffectValue = 1;

int playerHp = PLAYER_MAX_HP;
int playerDeathTimer = 0;
int playerInvincibleTimer = 0;

int resultDisplayTimer = 0;
bool resultRecorded = false;

void ResetRun()
{
    shotCoolTime = 0;

    score = 0;
    energy = 0;
    energyFlashTimer = 0;

    combo = 0;
    maxCombo = 0;

    comboRankEffectTimer = 0;
    comboRankEffectValue = 1;

    playerHp = PLAYER_MAX_HP;
    playerDeathTimer = 0;
    playerInvincibleTimer = 0;

    resultDisplayTimer = 0;
    resultRecorded = false;
}

// The score multiplier comes from the STREAK (combo holds it: the damage dealt since the last hit, in shots; Mod::NoteDamage)
int GetScoreMultiplier()
{
    if (combo >= Balance::STREAK_TIER2) return 3;
    if (combo >= Balance::STREAK_TIER1) return 2;
    return 1;
}
}
