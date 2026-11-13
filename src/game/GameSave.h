#pragma once

#include <string>

struct GameRecordData
{
    int highScore;
    int maxCombo;
    int playCount;
    int clearCount;

    int bgmVolumePercent;
    int seVolumePercent;

    int displayMode;

    int resolutionIndex;
    int showFps; // 0 / 1: FPS overlay
    int backgroundPercent; // 0..100: the flat field behind the game (settings: BACKGROUND)
};

// One finished run (history.dat keeps the last RUN_HISTORY_MAX, newest first).
constexpr int RUN_HISTORY_MAX = 50;
constexpr int RUN_MODE_PRACTICE = 9; // RunRecord::mode of a boss practice run; 0..3 are the challenge levels
struct RunRecord
{
    long long time = 0;   // unix time at the end of the run
    int result = 0;       // 0 defeated, 1 cleared, 2 abandoned (restart / quit to title after at least 20 s)
    int wave = 1;         // 1..4 = wave of the end, 5 = the boss
    int bossPhase = 0;    // 1..3 when wave == 5
    int mode = 0;         // challenge level 0..3, RUN_MODE_PRACTICE
    int score = 0;
    int maxCombo = 0;
    int seconds = 0;
    int absorbs = 0;
    int hits = 0;
    int shards = 0;       // shards given for the run
    std::string modules;  // module codes in the order they were picked, comma separated
};

namespace GameSave
{
void Initialize();

void Load();
void Save();

void StartNewPlay();

void RecordGameOver(int score, int maxCombo);

void RecordGameClear(int score, int maxCombo);

// A run that was left (RESTART, TITLE, the window closed): the score and the combo it reached still count for the records page, like a defeat.
void RecordAbandoned(int score, int maxCombo);

void SetAudioVolumes(int bgmVolumePercent, int seVolumePercent);

void SetDisplaySettings(int displayMode, int resolutionIndex);
void SetShowFps(bool show);
void SetBackgroundPercent(int percent);
// The settings of core/GameSettings (VSYNC, the frame cap, shake, flashes, mouse steering, the keys, the mouse and pad buttons, vibration, dead zone): the settings page changes GameSettings::Edit() and calls this.
void SaveSettings();

const GameRecordData& GetRecordData();
// ---- run history -------------------------------------------------------------------------------------------------------------
void AddRunRecord(const RunRecord& record);
int GetRunRecordCount();
const RunRecord& GetRunRecord(int index); // 0 = the newest run
}





