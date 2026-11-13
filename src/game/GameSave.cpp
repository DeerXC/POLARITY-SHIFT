#include "game/GameSave.h"
#include "core/GameSettings.h"
#include "rogue/Balance.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
constexpr char SAVE_FILE_NAME[] = "save.dat";

constexpr int SAVE_VERSION = 8;
constexpr int RESOLUTION_INDEX_MAX = 4; // 1280x720 ... 3840x2160 (see DisplaySystem)

GameRecordData g_RecordData{};

void ResetRecordData()
{
    g_RecordData.highScore = 0;
    g_RecordData.maxCombo = 0;
    g_RecordData.playCount = 0;
    g_RecordData.clearCount = 0;

    g_RecordData.bgmVolumePercent = 70;
    g_RecordData.seVolumePercent = 80;
    g_RecordData.displayMode = 0;
    g_RecordData.resolutionIndex = 0;
    g_RecordData.showFps = 0;
    g_RecordData.backgroundPercent = 40;
}

void ClampRecordData()
{
    if (g_RecordData.highScore < 0)
    {
        g_RecordData.highScore = 0;
    }

    if (g_RecordData.maxCombo < 0)
    {
        g_RecordData.maxCombo = 0;
    }

    if (g_RecordData.playCount < 0)
    {
        g_RecordData.playCount = 0;
    }

    if (g_RecordData.clearCount < 0)
    {
        g_RecordData.clearCount = 0;
    }

    if (g_RecordData.clearCount > g_RecordData.playCount)
    {
        g_RecordData.clearCount = g_RecordData.playCount;
    }

    if (g_RecordData.bgmVolumePercent < 0)
    {
        g_RecordData.bgmVolumePercent = 0;
    }

    if (g_RecordData.bgmVolumePercent > 100)
    {
        g_RecordData.bgmVolumePercent = 100;
    }

    if (g_RecordData.seVolumePercent < 0)
    {
        g_RecordData.seVolumePercent = 0;
    }

    if (g_RecordData.seVolumePercent > 100)
    {
        g_RecordData.seVolumePercent = 100;
    }

    if (g_RecordData.displayMode < 0)
    {
        g_RecordData.displayMode = 0;
    }

    if (g_RecordData.displayMode > 1)
    {
        g_RecordData.displayMode = 1;
    }

    if (g_RecordData.resolutionIndex < 0)
    {
        g_RecordData.resolutionIndex = 0;
    }

    if (g_RecordData.resolutionIndex > RESOLUTION_INDEX_MAX)
    {
        g_RecordData.resolutionIndex = RESOLUTION_INDEX_MAX;
    }

    g_RecordData.showFps = g_RecordData.showFps != 0 ? 1 : 0;
    g_RecordData.backgroundPercent = g_RecordData.backgroundPercent < 0 ? 0 : (g_RecordData.backgroundPercent > 100 ? 100 : g_RecordData.backgroundPercent);
}
}

// ---- run history (history.dat) ---------------------------------------------------------------------------------------------
// line 1: the version, then one run per line, newest first, TAB separated:
//   time  result  wave  bossPhase  mode  score  maxCombo  seconds  absorbs  hits  shards  modules
namespace
{
constexpr char HISTORY_FILE_NAME[] = "history.dat";
constexpr char HISTORY_VERSION[] = "1";
std::vector<RunRecord> g_Runs; // newest first

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    size_t pos = 0;
    while (true)
    {
        const size_t end = line.find('\t', pos);
        if (end == std::string::npos)
        {
            fields.push_back(line.substr(pos));
            break;
        }
        fields.push_back(line.substr(pos, end - pos));
        pos = end + 1;
    }
    return fields;
}

int ToInt(const std::string& text, int low, int high)
{
    const long value = std::strtol(text.c_str(), nullptr, 10);
    if (value < low) return low;
    if (value > high) return high;
    return static_cast<int>(value);
}

void SaveHistory()
{
    std::ofstream file(HISTORY_FILE_NAME, std::ios::trunc);
    if (!file.is_open())
    {
        return;
    }
    file << HISTORY_VERSION << '\n';
    for (const RunRecord& r : g_Runs)
    {
        file << r.time << '\t' << r.result << '\t' << r.wave << '\t' << r.bossPhase << '\t' << r.mode << '\t' << r.score << '\t'
             << r.maxCombo << '\t' << r.seconds << '\t' << r.absorbs << '\t' << r.hits << '\t' << r.shards << '\t' << r.modules << '\n';
    }
}

void LoadHistory()
{
    g_Runs.clear();
    std::ifstream file(HISTORY_FILE_NAME);
    std::string line;
    if (!file.is_open() || !std::getline(file, line))
    {
        return;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != HISTORY_VERSION)
    {
        return; // a file of a version this build does not know is left alone (and replaced by the next run)
    }
    constexpr int BIG = 2000000000;
    while (std::getline(file, line) && static_cast<int>(g_Runs.size()) < RUN_HISTORY_MAX)
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::vector<std::string> f = SplitTabs(line);
        if (f.size() < 11)
        {
            continue;
        }
        RunRecord r;
        r.time = std::strtoll(f[0].c_str(), nullptr, 10);
        r.result = ToInt(f[1], 0, 2);
        r.wave = ToInt(f[2], 1, 5);
        r.bossPhase = ToInt(f[3], 0, 3);
        r.mode = ToInt(f[4], 0, RUN_MODE_PRACTICE);
        r.score = ToInt(f[5], 0, BIG);
        r.maxCombo = ToInt(f[6], 0, BIG);
        r.seconds = ToInt(f[7], 0, BIG);
        r.absorbs = ToInt(f[8], 0, BIG);
        r.hits = ToInt(f[9], 0, BIG);
        r.shards = ToInt(f[10], 0, BIG);
        if (f.size() > 11) r.modules = f[11];
        g_Runs.push_back(r);
    }
}
}

void GameSave::Initialize()
{
    ResetRecordData();
    Load();
    LoadHistory();

    // The best score and the best combo are also the best of the history: a run that was left used to be written to the history only, so the page showed
    // "HIGH SCORE 0" under a run of 24000. (A practice run counts when Balance::PRACTICE_SCORES says so.) Repairs the saves of those versions once.
    bool changed = false;
    for (const RunRecord& r : g_Runs)
    {
        if (r.mode == RUN_MODE_PRACTICE && !Balance::PRACTICE_SCORES) continue;
        if (r.score > g_RecordData.highScore)
        {
            g_RecordData.highScore = r.score;
            changed = true;
        }
        if (r.maxCombo > g_RecordData.maxCombo)
        {
            g_RecordData.maxCombo = r.maxCombo;
            changed = true;
        }
    }
    if (changed) Save();
}

void GameSave::AddRunRecord(const RunRecord& record)
{
    RunRecord clean = record;
    for (char& c : clean.modules)
    {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    g_Runs.insert(g_Runs.begin(), clean);
    if (static_cast<int>(g_Runs.size()) > RUN_HISTORY_MAX)
    {
        g_Runs.resize(RUN_HISTORY_MAX);
    }
    SaveHistory();
}

int GameSave::GetRunRecordCount()
{
    return static_cast<int>(g_Runs.size());
}

const RunRecord& GameSave::GetRunRecord(int index)
{
    static const RunRecord EMPTY{};
    if (index < 0 || index >= static_cast<int>(g_Runs.size()))
    {
        return EMPTY;
    }
    return g_Runs[static_cast<size_t>(index)];
}

void GameSave::Load()
{
    std::ifstream inputFile(SAVE_FILE_NAME);

    if (!inputFile.is_open())
    {
        ResetRecordData();
        return;
    }

    int loadedVersion = 0;

    inputFile >> loadedVersion;

    if (inputFile.fail())
    {
        ResetRecordData();
        return;
    }

    GameRecordData loadedData{};
    loadedData.backgroundPercent = 40; // (the default; saves of version 4 and older do not have it)

    if (loadedVersion == 1)
    {
        inputFile >> loadedData.highScore >> loadedData.maxCombo >> loadedData.playCount >> loadedData.clearCount;

        if (inputFile.fail())
        {
            ResetRecordData();
            return;
        }

        loadedData.bgmVolumePercent = 70;
        loadedData.seVolumePercent = 80;

        loadedData.displayMode = 0;
        loadedData.resolutionIndex = 0;
    }

    else if (loadedVersion == 2)
    {
        inputFile >> loadedData.highScore >> loadedData.maxCombo >> loadedData.playCount >> loadedData.clearCount
            >> loadedData.bgmVolumePercent >> loadedData.seVolumePercent;

        if (inputFile.fail())
        {
            ResetRecordData();
            return;
        }

        loadedData.displayMode = 0;
        loadedData.resolutionIndex = 0;
    }

    else if (loadedVersion >= 3 && loadedVersion <= 8)
    {
        inputFile >> loadedData.highScore >> loadedData.maxCombo >> loadedData.playCount >> loadedData.clearCount
            >> loadedData.bgmVolumePercent >> loadedData.seVolumePercent >> loadedData.displayMode >> loadedData.resolutionIndex;

        if (inputFile.fail())
        {
            ResetRecordData();
            return;
        }

        if (loadedVersion >= 4)
        {
            inputFile >> loadedData.showFps;
            if (inputFile.fail())
            {
                loadedData.showFps = 0;
            }
        }

        if (loadedVersion >= 5)
        {
            inputFile >> loadedData.backgroundPercent;
            if (inputFile.fail())
            {
                loadedData.backgroundPercent = 40;
            }
        }

        if (loadedVersion >= 6)
        {
            // the settings of the engine layer: vsync, fps cap, shake, flashes, mouse steering, the 8 keys
            GameSettings::Values v = GameSettings::Get();
            int vsync = 1, steer = 1;
            inputFile >> vsync >> v.fpsCap >> v.shakePercent >> v.flashPercent >> steer;
            for (int i = 0; i < GameSettings::ACTION_COUNT; ++i) inputFile >> v.keys[i];
            if (!inputFile.fail())
            {
                v.vsync = vsync != 0;
                v.mouseSteering = steer != 0;
                v.shakePercent = v.shakePercent < 0 ? 0 : (v.shakePercent > 100 ? 100 : v.shakePercent);
                v.flashPercent = v.flashPercent < 0 ? 0 : (v.flashPercent > 100 ? 100 : v.flashPercent);
                bool capOk = false;
                for (int cap : GameSettings::FPS_CAPS) capOk = capOk || cap == v.fpsCap;
                if (!capOk) v.fpsCap = 0;
                bool keysOk = true;
                for (int i = 0; i < GameSettings::ACTION_COUNT; ++i)
                {
                    keysOk = keysOk && GameSettings::IsBindable(v.keys[i]);
                    for (int j = 0; j < i; ++j) keysOk = keysOk && v.keys[i] != v.keys[j];
                }
                GameSettings::Edit() = v;
                if (!keysOk) GameSettings::ResetKeys(); // (a damaged file never leaves the ship without a key)
            }
        }

        if (loadedVersion >= 7)
        {
            // version 7: the mouse buttons and the gamepad buttons of the four button actions, the vibration, the dead zone of the stick, the extra pad buttons
            GameSettings::Values v = GameSettings::Get();
            int extra = 1;
            for (int i = 0; i < GameSettings::BUTTON_ACTION_COUNT; ++i) inputFile >> v.mouseButtons[i];
            for (int i = 0; i < GameSettings::BUTTON_ACTION_COUNT; ++i) inputFile >> v.padButtons[i];
            inputFile >> v.vibrationPercent >> v.deadZonePercent >> extra;
            if (!inputFile.fail())
            {
                bool mouseOk = true, padOk = true;
                for (int i = 0; i < GameSettings::BUTTON_ACTION_COUNT; ++i)
                {
                    mouseOk = mouseOk && GameSettings::IsMouseBindable(v.mouseButtons[i]);
                    padOk = padOk && v.padButtons[i] > GameSettings::PadNone && v.padButtons[i] < GameSettings::PadButtonCount;
                    for (int j = 0; j < i; ++j)
                    {
                        mouseOk = mouseOk && v.mouseButtons[i] != v.mouseButtons[j];
                        padOk = padOk && v.padButtons[i] != v.padButtons[j];
                    }
                }
                v.vibrationPercent = v.vibrationPercent < 0 ? 0 : (v.vibrationPercent > 100 ? 100 : v.vibrationPercent);
                v.deadZonePercent = v.deadZonePercent < 5 ? 5 : (v.deadZonePercent > 40 ? 40 : v.deadZonePercent);
                v.padExtraButtons = extra != 0;
                GameSettings::Edit() = v;
                if (!mouseOk) GameSettings::ResetMouse(); // (a damaged file never leaves the ship without a button)
                if (!padOk)
                {
                    const int vibration = v.vibrationPercent, dead = v.deadZonePercent;
                    const bool extraButtons = v.padExtraButtons;
                    GameSettings::ResetPad();
                    GameSettings::Edit().vibrationPercent = vibration;
                    GameSettings::Edit().deadZonePercent = dead;
                    GameSettings::Edit().padExtraButtons = extraButtons;
                }
            }
        }

        if (loadedVersion >= 8)
        {
            // version 8: the master volume, the emphasis of the cues, the heartbeat, the sound in the background
            GameSettings::Values v = GameSettings::Get();
            int heartbeat = 1, mute = 1;
            inputFile >> v.masterPercent >> v.cuePercent >> heartbeat >> mute;
            if (!inputFile.fail())
            {
                v.masterPercent = v.masterPercent < 0 ? 0 : (v.masterPercent > 100 ? 100 : v.masterPercent);
                v.cuePercent = v.cuePercent < 0 ? 0 : (v.cuePercent > 100 ? 100 : v.cuePercent);
                v.heartbeat = heartbeat != 0;
                v.muteInBackground = mute != 0;
                GameSettings::Edit() = v;
            }
        }
    }

    else
    {
        ResetRecordData();
        return;
    }

    g_RecordData = loadedData;

    ClampRecordData();
}

void GameSave::Save()
{
    ClampRecordData();

    std::ofstream outputFile(SAVE_FILE_NAME, std::ios::trunc);

    if (!outputFile.is_open())
    {
        return;
    }

    outputFile << SAVE_VERSION << '\n'
               << g_RecordData.highScore << '\n'
               << g_RecordData.maxCombo << '\n'
               << g_RecordData.playCount << '\n'
               << g_RecordData.clearCount << '\n'
               << g_RecordData.bgmVolumePercent << '\n'
               << g_RecordData.seVolumePercent << '\n'
               << g_RecordData.displayMode << '\n'
               << g_RecordData.resolutionIndex << '\n'
               << g_RecordData.showFps << '\n'
               << g_RecordData.backgroundPercent << '\n';
    {
        const GameSettings::Values& v = GameSettings::Get();
        outputFile << (v.vsync ? 1 : 0) << '\n' << v.fpsCap << '\n' << v.shakePercent << '\n' << v.flashPercent << '\n' << (v.mouseSteering ? 1 : 0) << '\n';
        for (int i = 0; i < GameSettings::ACTION_COUNT; ++i) outputFile << v.keys[i] << '\n';
        for (int i = 0; i < GameSettings::BUTTON_ACTION_COUNT; ++i) outputFile << v.mouseButtons[i] << '\n';
        for (int i = 0; i < GameSettings::BUTTON_ACTION_COUNT; ++i) outputFile << v.padButtons[i] << '\n';
        outputFile << v.vibrationPercent << '\n' << v.deadZonePercent << '\n' << (v.padExtraButtons ? 1 : 0) << '\n';
        outputFile << v.masterPercent << '\n' << v.cuePercent << '\n' << (v.heartbeat ? 1 : 0) << '\n' << (v.muteInBackground ? 1 : 0) << '\n';
    }
}

void GameSave::StartNewPlay()
{
    g_RecordData.playCount++;

    Save();
}

void GameSave::RecordGameOver(int score, int maxCombo)
{
    if (score > g_RecordData.highScore)
    {
        g_RecordData.highScore = score;
    }

    if (maxCombo > g_RecordData.maxCombo)
    {
        g_RecordData.maxCombo = maxCombo;
    }

    Save();
}

void GameSave::RecordAbandoned(int score, int maxCombo)
{
    if (score <= 0 && maxCombo <= 0) return;
    RecordGameOver(score, maxCombo); // (the same bests as a defeat; the number of clears is not touched)
}

void GameSave::RecordGameClear(int score, int maxCombo)
{
    if (score > g_RecordData.highScore)
    {
        g_RecordData.highScore = score;
    }

    if (maxCombo > g_RecordData.maxCombo)
    {
        g_RecordData.maxCombo = maxCombo;
    }

    g_RecordData.clearCount++;

    if (g_RecordData.clearCount > g_RecordData.playCount)
    {
        g_RecordData.clearCount = g_RecordData.playCount;
    }

    Save();
}

void GameSave::SetAudioVolumes(int bgmVolumePercent, int seVolumePercent)
{
    g_RecordData.bgmVolumePercent = bgmVolumePercent;

    g_RecordData.seVolumePercent = seVolumePercent;

    ClampRecordData();

    Save();
}

void GameSave::SetDisplaySettings(int displayMode, int resolutionIndex)
{
    g_RecordData.displayMode = displayMode;

    g_RecordData.resolutionIndex = resolutionIndex;

    ClampRecordData();

    Save();
}

void GameSave::SetShowFps(bool show)
{
    g_RecordData.showFps = show ? 1 : 0;
    Save();
}

void GameSave::SaveSettings()
{
    Save();
}

void GameSave::SetBackgroundPercent(int percent)
{
    g_RecordData.backgroundPercent = percent;
    ClampRecordData();
    Save();
}

const GameRecordData& GameSave::GetRecordData()
{
    return g_RecordData;
}






