#pragma once
#include "game/GameTypes.h"

// Hits between the player, the player's shots, enemies and enemy bullets.
// Called once per frame by PlayScene; the roguelite modules hook in through the Mod:: calls inside.
namespace Combat
{
bool UsesHeavyEnemyAudio(EnemyType type);

void CheckBulletEnemyCollision();      // player bullets vs enemies
void ApplySpecialShotDamage();         // the special beam vs enemies and the boss
void CheckPlayerEnemyCollision();      // ramming
void CheckPlayerEnemyBulletCollision(); // absorb or damage
void StartPlayerDeath();               // honours the backup core module
bool HurtPlayer(int amount, const char* source = ""); // source: what hurt the ship (run_log.csv)           // the ship loses HP (invincibility frames, death, sounds, shake): every source of damage goes through here
}
