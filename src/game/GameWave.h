#pragma once

#include "game/GameTypes.h"

namespace GameWave
{
constexpr int WAVE_COUNT = 5; // the same number as Balance::WAVE_COUNT (rogue/Balance.h); the boss comes after the last wave
void Initialize();

void Update(Enemy enemies[], int enemyCount);

int GetCurrentWave();
int GetCurrentSegment(); // 0 open, 1 combine, 2 test, 3 done (the run log and the tests read it)

bool IsWaveStarting();
bool IsWaveCleared();
bool IsAllFinished();

int GetTransitionTimer();

void PrepareBossRetry();

void DebugFinishAllWaves();
}

