#pragma once

#include <map>
#include <string>

// Test switches read from config.ini (next to the exe, i.e. in the working directory).
//   start_at_boss=1   "START GAME" jumps straight into the boss fight
//   boss_phase=2|3    with start_at_boss: the boss is pushed to that phase as soon as the fight begins
//   god_mode=1        the player cannot be hurt
//   modules=all       start with these modules: all, or a list such as 0,4,9 (see Mod::Id)
//   shards=500        hangar test: the shard total is set to this value at every start of the game
//   unlock_all=1      hangar test: every module is available in the pick screen, and every talent is on
//   no_talents=1      with unlock_all: the talents are not on (they are bought in the hangar as usual)
//   flip_present=1    present with the flip model (if the frame rate is uneven in the window, try it; the FPS overlay shows FLIP)
// The parser is forgiving: spaces around '=', upper case, true/on/yes, ';' or '#' comments, UTF-8 / UTF-16 / BOM,
// and full-width characters typed with a Japanese IME (U+FF1D '=', U+FF11 '1', U+FF3F '_' ...).
// Every start of the game writes debug_report.txt next to the exe: which folder was used and what was read.
// Delete the keys (or the file) before you record the demo video / submit the build.
namespace DebugOptions
{
bool StartAtBoss();
bool GodMode();
bool PostFxOff(); // post_fx=0: no bloom / grade / vignette (the picture goes straight to the screen)
bool FlipPresent(); // flip_present=1: the swap chain uses the flip model (can be smoother in a window; the FPS overlay then shows FLIP)
int BossPhase();
std::string ModuleList();
int Shards();
bool UnlockAll();
bool NoTalents(); // no_talents=1: unlock_all does not give the talents (the balance simulator plays without them)

// Normalised content of config.ini (lower-case keys, trimmed). Also used by Loc to save its own keys
// without destroying the debug keys.
std::map<std::string, std::string> ReadAll();

void WriteReport();
}
