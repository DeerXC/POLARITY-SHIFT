#pragma once

#include "game/GameTypes.h"

namespace GameEnemy
{
void Initialize(Enemy enemies[], int enemyCount, EnemyBullet enemyBullets[], int enemyBulletCount);

bool Spawn(Enemy enemies[], int enemyCount, EnemyType type, Polarity polarity, float spawnY, int groupId);

// Call first in every world step (render interpolation).
void BeginStep(Enemy enemies[], int enemyCount, EnemyBullet enemyBullets[], int enemyBulletCount);
void Update(Enemy enemies[], int enemyCount, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player);

void Draw(const Enemy enemies[], int enemyCount);

void DrawBullets(const EnemyBullet enemyBullets[], int enemyBulletCount);

bool HasActiveEnemy(const Enemy enemies[], int enemyCount);
// Spawn the ELITE version of a turret / sniper / heavy / charger (its template: more life, a shield, a faster beat, a move of its own).
bool SpawnElite(Enemy enemies[], int enemyCount, EnemyType type, Polarity polarity, float spawnY, int groupId);
// The rail of an elite sniper is firing and touches the ship (its hurt box)?
bool RailHits(const Player& player, float hurtHalfHeight);
// The rails for the bot of the simulator: origin, angle (screen space), steps until it fires (0 = firing). Returns how many.
int GetRailsForSim(float* x0, float* y0, float* angle, int* framesUntil, int capacity);
// Test hook: false = a rival comes alone (the twin does not join). True by default.
void SetTwinEnabled(bool enabled);
// Name of the enemy type for the run log ("heavy", "scout", ...).
const char* OwnerName(int owner);

}
