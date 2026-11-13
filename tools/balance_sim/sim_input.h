#pragma once
// the keys of the simulated player: the bot sets them before every update step, the stub of GameInput reads them
namespace SimInput
{
extern bool down[256];
extern bool prev[256];
inline void Clear() { for (int i = 0; i < 256; ++i) down[i] = false; }
inline void EndFrame() { for (int i = 0; i < 256; ++i) prev[i] = down[i]; }
}

// the sounds that the game logic asked for (stubs.cpp counts them): a test looks at WHAT was played, not at how it sounds
#include "core/GameAudio.h"
namespace SimAudio
{
constexpr int MAX = 256;
extern int count[MAX];
extern int total;
void Note(GameSound s);
inline void Reset()
{
    for (int i = 0; i < MAX; ++i) count[i] = 0;
    total = 0;
}
inline int Count(GameSound s) { return count[static_cast<int>(s)]; }
}
