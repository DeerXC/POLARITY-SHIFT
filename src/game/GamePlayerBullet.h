#pragma once

#include "game/GameTypes.h"

namespace GamePlayerBullet
{
constexpr int MAX_COUNT = 32;

void Initialize();

bool Spawn(float x, float y, Polarity polarity);

void BeginStep(); // first in every world step (render interpolation)
void Update();
void Draw();

Bullet* GetBullets();
int GetBulletCount();
}
