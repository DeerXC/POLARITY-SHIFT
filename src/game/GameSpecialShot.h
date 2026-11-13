#pragma once

#include "game/GameTypes.h"

namespace GameSpecialShot
{
void Initialize();

bool TryActivate(const Player& player, int& energy, int maxEnergy);

bool Update(const Player& player);

void Draw(const Player& player);

bool IsActive();

float GetStartX();
float GetEndX();
float GetCenterY();
float GetHalfHeight();
}
