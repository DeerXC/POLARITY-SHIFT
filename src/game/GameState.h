#pragma once
#include "game/GameTypes.h"
#include "rogue/Balance.h"

// Data that every part of a run shares: the player, the enemies, the score and the current scene.
// Everything that belongs to one screen only (menus, title ship, pause menu ...) lives in that scene's own file.
namespace GameState
{
constexpr int ENEMY_MAX = 16;
constexpr int ENEMY_BULLET_MAX = 192;

constexpr int PLAYER_MAX_HP = Balance::PLAYER_MAX_HP; // HP points, not lives (the modules and the challenge level change the maximum: Mod::MaxHp())
constexpr int DEATH_ANIM_DURATION = 60;

constexpr int COMBO_RANK_EFFECT_DURATION = 42;
constexpr int BOSS_PHASE_CHANGE_DURATION = 75;

constexpr int MAX_ENERGY = Balance::ENERGY_MAX;
constexpr float ENERGY_PER_ABSORB = Balance::ABSORB_ENERGY;           // before the chain and the modules
constexpr float ENERGY_PER_ABSORB_BOSS = Balance::ABSORB_ENERGY_BOSS;
constexpr int ENERGY_FLASH_DURATION = 12;

enum class Scene
{
    Title,
    HowTo,
    Records,
    Settings,
    Game,
    GameClear,
    GameOver
};

extern Scene scene;

extern Player player;
extern Enemy enemies[ENEMY_MAX];
extern EnemyBullet enemyBullets[ENEMY_BULLET_MAX];

extern int shotCoolTime;
extern int hitStop; // frames the whole game is frozen for (boss hits, backup core)

extern int score;
extern int energy;
extern int energyFlashTimer;

extern int combo; // the STREAK: damage dealt since the ship was last hurt, in shots (Mod::NoteDamage). A hit drops it a tier (Mod::NoteHit)
extern int maxCombo;
extern int comboRankEffectTimer;
extern int comboRankEffectValue;

extern int playerHp;
extern int playerDeathTimer;
extern int playerInvincibleTimer;

extern int resultDisplayTimer;
extern bool resultRecorded;

// Back to the values of a fresh run (player / enemy objects are initialised by their own modules).
void ResetRun();

// 1x .. 4x, from the current combo.
int GetScoreMultiplier(); // 1 / 2 / 3: the tier of the streak (combo) plus one
}
