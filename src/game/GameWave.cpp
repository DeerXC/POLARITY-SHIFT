#include "game/GameWave.h"
#include "game/GameEnemy.h"
#include "game/GameState.h"
#include "rogue/Balance.h"

#include <cstdlib>

// The waves are a table (WAVES below): every wave has three SEGMENTS, played one after the other:
//   1. OPEN    one kind of question (only the colour, or only the place), so that the player sees what the wave is about
//   2. COMBINE two kinds at once
//   3. TEST    one or two elites (and a few small ships); the wave is cleared when the field is empty after it
// A segment is a few GROUPS that run side by side; a group spawns `count` ships of one type, one every `every` steps after `delay` steps, along a pattern.
// The next segment begins when every ship of this one has come and the field is nearly clear (or a while after the last ship came).
// To change the waves, change the table; the code below only plays it.
namespace
{
enum class WaveState
{
    Starting,
    Playing,
    Cleared,
    Finished
};

enum class Pat
{
    Line,   // all at one height, one colour
    Run,    // runs of three at one height; every run the other colour and another height
    Pairs,  // mirrored above / below the middle, the two colours
    Spread, // anywhere (the big ships: a free height near the middle)
};

struct Group
{
    EnemyType type;
    int count;
    int every;  // steps between two ships of the group
    int delay;  // steps after the start of the segment before the first one
    Pat pat;
    bool elite;
    bool anyElite; // the elite is one of the four templates, drawn when the wave begins (two groups of one wave never draw the same)
};

constexpr int MAX_GROUPS = 4;
struct Segment
{
    int count;
    Group groups[MAX_GROUPS];
};

struct WaveDef
{
    Segment seg[3];
};

constexpr int WAVE_COUNT = GameWave::WAVE_COUNT; // roguelite: 5 waves, then the boss
static_assert(GameWave::WAVE_COUNT == Balance::WAVE_COUNT, "GameWave.h and rogue/Balance.h must agree on the number of waves");

constexpr EnemyType SC = EnemyType::Scout, SI = EnemyType::Sine, HE = EnemyType::Heavy, CH = EnemyType::Charger, TU = EnemyType::Turret, SN = EnemyType::Sniper,
                    RI = EnemyType::Rival;

// type, count, every, delay, pattern, elite, any elite
constexpr WaveDef WAVES[WAVE_COUNT] = {
    // wave 1  ABSORB: lines of one colour; then the two colours; the test is an elite turret
    {{{2, {{SC, 6, 16, 0, Pat::Line, false, false}, {SC, 6, 16, 150, Pat::Line, false, false}}},
      {2, {{SC, 9, 18, 0, Pat::Run, false, false}, {SI, 4, 40, 60, Pat::Pairs, false, false}}},
      {2, {{TU, 1, 1, 0, Pat::Spread, true, false}, {SC, 6, 20, 140, Pat::Run, false, false}}}}},
    // wave 2  SWITCH: the colour changes while the ship moves; the charger wants a step aside
    {{{1, {{SI, 8, 22, 0, Pat::Run, false, false}}},
      {3, {{TU, 1, 1, 0, Pat::Spread, false, false}, {CH, 4, 70, 60, Pat::Spread, false, false}, {SC, 6, 20, 30, Pat::Run, false, false}}},
      {2, {{TU, 1, 1, 0, Pat::Spread, true, false}, {CH, 3, 80, 90, Pat::Spread, false, false}}}}},
    // wave 3  PLACE: mines and needles, a lane to step out of
    {{{2, {{HE, 1, 1, 0, Pat::Spread, false, false}, {SC, 6, 20, 40, Pat::Run, false, false}}},
      {3, {{SN, 1, 1, 0, Pat::Spread, false, false}, {CH, 4, 70, 40, Pat::Spread, false, false}, {SI, 6, 36, 80, Pat::Pairs, false, false}}},
      {2, {{HE, 1, 1, 0, Pat::Spread, true, false}, {SN, 1, 1, 120, Pat::Spread, true, false}}}}},
    // wave 4  ALL OF IT: every question at once; the test is two elites of two of the four templates
    {{{2, {{TU, 1, 1, 0, Pat::Spread, false, false}, {SN, 1, 1, 60, Pat::Spread, false, false}}},
      {4, {{HE, 1, 1, 0, Pat::Spread, false, false}, {CH, 4, 60, 30, Pat::Spread, false, false}, {SI, 6, 34, 40, Pat::Pairs, false, false}, {SC, 6, 18, 120, Pat::Run, false, false}}},
      {3, {{TU, 1, 1, 0, Pat::Spread, true, true}, {TU, 1, 1, 100, Pat::Spread, true, true}, {SC, 6, 22, 60, Pat::Run, false, false}}}}},
    // wave 5  THE RIVAL: the duel of the twins; then a few small ships that fill the bar for the boss
    {{{1, {{RI, 1, 1, 0, Pat::Spread, false, false}}},
      {2, {{SC, 8, 18, 0, Pat::Run, false, false}, {SI, 4, 40, 40, Pat::Pairs, false, false}}},
      {0, {}}}}};

constexpr int WAVE_START_DURATION = 90;
constexpr int WAVE_CLEAR_DURATION = 90;
constexpr int SEGMENT_GAP = 40;       // steps between two segments
constexpr int SEGMENT_LINGER = 300;   // a segment ends this long after its last ship came, even when the field is not clear
constexpr int ACTIVE_LIMIT = 9;       // never more ships than this on the field
constexpr int BULLET_LIMIT = 55;      // no new ship while more enemy bullets than this are on the screen (the field stays readable)
constexpr int BIG_LIMIT = 3;          // never more big ships (heavy / turret / sniper / rival / elites) than this

WaveState g_State = WaveState::Starting;
int g_CurrentWave = 1;
int g_WaveTimer = 0;
int g_TransitionTimer = WAVE_START_DURATION;
int g_GroupIdCounter = 100;

int g_Segment = 0;           // the segment that is running (3 = all done)
int g_SegmentTimer = 0;      // steps since it began
int g_SegmentGap = 0;        // steps until it begins
int g_SpawnedLast = -1;      // the step of the segment at which its last ship came (-1 = not yet)
int g_Spawned[MAX_GROUPS]{}; // ships of each group that have come
int g_NextAt[MAX_GROUPS]{};  // the step of the segment at which the next ship of each group comes
Polarity g_GroupPol[MAX_GROUPS]{};
float g_GroupY[MAX_GROUPS]{};
int g_GroupId[MAX_GROUPS]{};
EnemyType g_EliteDraw[2] = {EnemyType::Turret, EnemyType::Sniper}; // the two templates of the "any elite" groups of this wave

int ClampWaveIndex()
{
    int index = g_CurrentWave - 1;
    if (index < 0) index = 0;
    if (index >= WAVE_COUNT) index = WAVE_COUNT - 1;
    return index;
}

float Rnd(float a, float b)
{
    return a + (b - a) * static_cast<float>(std::rand() % 1000) / 999.0f;
}

Polarity RandomPolarity()
{
    return (std::rand() % 2 == 0) ? Polarity::Red : Polarity::Blue;
}

Polarity Opposite(Polarity p)
{
    return p == Polarity::Red ? Polarity::Blue : Polarity::Red;
}

bool IsBig(EnemyType type)
{
    return type == EnemyType::Heavy || type == EnemyType::Turret || type == EnemyType::Sniper || type == EnemyType::Rival;
}

const Segment& CurrentSegment()
{
    return WAVES[ClampWaveIndex()].seg[g_Segment < 3 ? g_Segment : 2];
}

void BeginSegment(int index)
{
    g_Segment = index;
    g_SegmentTimer = 0;
    g_SpawnedLast = -1;
    if (index >= 3) return;
    const Segment& seg = CurrentSegment();
    for (int k = 0; k < MAX_GROUPS; ++k)
    {
        g_Spawned[k] = 0;
        g_NextAt[k] = k < seg.count ? seg.groups[k].delay : 0;
        g_GroupPol[k] = RandomPolarity();
        g_GroupY[k] = Rnd(-0.50f, 0.50f);
        g_GroupId[k] = ++g_GroupIdCounter;
    }
}

int ActiveCount(const Enemy enemies[], int enemyCount, bool bigOnly)
{
    int n = 0;
    for (int i = 0; i < enemyCount; ++i)
    {
        if (!enemies[i].active) continue;
        if (!bigOnly || IsBig(enemies[i].type) || enemies[i].elite) ++n;
    }
    return n;
}

int EnemyBullets()
{
    int n = 0;
    for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
        if (GameState::enemyBullets[i].active) ++n;
    return n;
}

bool RivalOnField(const Enemy enemies[], int enemyCount)
{
    for (int i = 0; i < enemyCount; ++i)
        if (enemies[i].active && enemies[i].type == EnemyType::Rival) return true;
    return false;
}

// the place and the colour of ship n of a group
void UnitPlace(const Group& g, int k, int n, float& y, Polarity& pol)
{
    switch (g.pat)
    {
    case Pat::Line:
        y = g_GroupY[k];
        pol = g_GroupPol[k];
        break;
    case Pat::Run:
    {
        const int run = n / 3;
        y = (run % 2 == 0) ? g_GroupY[k] : -g_GroupY[k] * 0.8f;
        pol = (run % 2 == 0) ? g_GroupPol[k] : Opposite(g_GroupPol[k]);
        break;
    }
    case Pat::Pairs:
    {
        const float spread = 0.25f + 0.15f * static_cast<float>((n / 2) % 3);
        const bool top = n % 2 == 0;
        y = top ? spread : -spread;
        pol = top ? g_GroupPol[k] : Opposite(g_GroupPol[k]);
        break;
    }
    case Pat::Spread:
    default:
        y = IsBig(g.type) || g.elite ? Rnd(-0.30f, 0.30f) : Rnd(-0.62f, 0.62f);
        pol = RandomPolarity();
        break;
    }
}

// one step of the segment that is running: the groups bring their ships when they are due (and when there is room)
void UpdateSegment(Enemy enemies[], int enemyCount)
{
    if (g_Segment >= 3) return;
    if (g_SegmentGap > 0)
    {
        --g_SegmentGap;
        return;
    }
    const Segment& seg = CurrentSegment();
    ++g_SegmentTimer;
    bool allSpawned = true;
    int anyEliteIndex = 0;
    for (int k = 0; k < seg.count; ++k)
    {
        const Group& g = seg.groups[k];
        const int myElite = g.anyElite ? anyEliteIndex++ : -1;
        if (g_Spawned[k] >= g.count) continue;
        allSpawned = false;
        if (g_SegmentTimer < g_NextAt[k]) continue;
        const EnemyType type = myElite >= 0 ? g_EliteDraw[myElite % 2] : g.type;
        const bool big = IsBig(type) || g.elite;
        if (ActiveCount(enemies, enemyCount, false) >= ACTIVE_LIMIT || (big && ActiveCount(enemies, enemyCount, true) >= BIG_LIMIT) || EnemyBullets() > BULLET_LIMIT)
        {
            g_NextAt[k] = g_SegmentTimer + 12; // no room: a moment later
            continue;
        }
        float y = 0.0f;
        Polarity pol = Polarity::Red;
        UnitPlace(g, k, g_Spawned[k], y, pol);
        const bool ok = g.elite ? GameEnemy::SpawnElite(enemies, enemyCount, type, pol, y, g_GroupId[k])
                                : GameEnemy::Spawn(enemies, enemyCount, type, pol, y, g_GroupId[k]);
        if (!ok)
        {
            g_NextAt[k] = g_SegmentTimer + 8;
            continue;
        }
        ++g_Spawned[k];
        g_NextAt[k] = g_SegmentTimer + g.every + ((g.pat == Pat::Run && g_Spawned[k] % 3 == 0) ? 40 : 0); // (a pause when the colour of a run changes)
    }
    if (!allSpawned)
    {
        return;
    }
    if (g_SpawnedLast < 0) g_SpawnedLast = g_SegmentTimer;
    // the next segment: the field is nearly clear (no big ship, at most two small ones), or the segment has lingered long enough. The duel ends only with the rival.
    if (RivalOnField(enemies, enemyCount)) return;
    const bool clear = ActiveCount(enemies, enemyCount, true) == 0 && ActiveCount(enemies, enemyCount, false) <= 2;
    const bool last = (g_Segment == 2) || (g_Segment < 2 && WAVES[ClampWaveIndex()].seg[g_Segment + 1].count == 0);
    if (last)
    {
        if (ActiveCount(enemies, enemyCount, false) == 0) BeginSegment(3);
        return;
    }
    if (clear || g_SegmentTimer - g_SpawnedLast >= SEGMENT_LINGER)
    {
        BeginSegment(g_Segment + 1);
        g_SegmentGap = SEGMENT_GAP;
    }
}

void BeginCurrentWave()
{
    g_WaveTimer = 0;
    g_State = WaveState::Starting;
    g_TransitionTimer = WAVE_START_DURATION;
    g_SegmentGap = 0;
    // the two templates of the "any elite" groups: two different ones of the four
    static const EnemyType TEMPLATES[4] = {EnemyType::Turret, EnemyType::Sniper, EnemyType::Heavy, EnemyType::Charger};
    const int a = std::rand() % 4;
    const int b = (a + 1 + std::rand() % 3) % 4;
    g_EliteDraw[0] = TEMPLATES[a];
    g_EliteDraw[1] = TEMPLATES[b];
    BeginSegment(0);
}

void BeginNextWave()
{
    g_CurrentWave++;
    if (g_CurrentWave > WAVE_COUNT)
    {
        g_State = WaveState::Finished;
        g_TransitionTimer = 0;
        g_Segment = 3;
        return;
    }
    BeginCurrentWave();
}
}

void GameWave::Initialize()
{
    g_CurrentWave = 1;
    g_GroupIdCounter = 100;
    BeginCurrentWave();
}

void GameWave::Update(Enemy enemies[], int enemyCount)
{
    switch (g_State)
    {
    case WaveState::Starting:
        if (--g_TransitionTimer <= 0)
        {
            g_TransitionTimer = 0;
            g_WaveTimer = 0;
            g_State = WaveState::Playing;
        }
        break;

    case WaveState::Playing:
        g_WaveTimer++;
        UpdateSegment(enemies, enemyCount);
        if (g_Segment >= 3 && !GameEnemy::HasActiveEnemy(enemies, enemyCount))
        {
            g_State = WaveState::Cleared;
            g_TransitionTimer = WAVE_CLEAR_DURATION;
        }
        break;

    case WaveState::Cleared:
        if (--g_TransitionTimer <= 0)
        {
            g_TransitionTimer = 0;
            BeginNextWave();
        }
        break;

    case WaveState::Finished:
        break;
    }
}

int GameWave::GetCurrentWave()
{
    return g_CurrentWave;
}

int GameWave::GetCurrentSegment()
{
    return g_Segment;
}

bool GameWave::IsWaveStarting()
{
    return g_State == WaveState::Starting;
}

bool GameWave::IsWaveCleared()
{
    return g_State == WaveState::Cleared;
}

bool GameWave::IsAllFinished()
{
    return g_State == WaveState::Finished;
}

int GameWave::GetTransitionTimer()
{
    return g_TransitionTimer;
}

void GameWave::PrepareBossRetry()
{
    g_CurrentWave = WAVE_COUNT;
    g_State = WaveState::Finished;
    g_WaveTimer = 0;
    g_TransitionTimer = 0;
    g_Segment = 3;
    g_SegmentGap = 0;
}

void GameWave::DebugFinishAllWaves()
{
    g_CurrentWave = WAVE_COUNT;
    g_State = WaveState::Finished;
    g_TransitionTimer = 0;
    g_Segment = 3;
}
