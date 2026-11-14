// POLARITY SHIFT - balance simulator. It runs the REAL game logic (enemies, waves, boss, modules, combat: ../../src) without a window: the renderer,
// the audio and the keyboard are stand-ins (stubs.cpp), and a bot presses the keys. The update step of a run is the sequence of PlayScene::Update.
//   sim --runs 200 --seed 1 --skill average --unlock all|start --talents none|all --draft random|none --mods 3,5 --boss 0|1
// One line per run on stdout: seed,skill,result,wave,bossPhase,frames,hp,hits,absorbs,mask,rivalFrames,rivalDamage,bossFrames,overloads,breaks,hpAtBoss,start2..start6 (frame at which wave 2..5 and the boss began)   (result 0 died, 1 cleared, 2 timed out)
#include "boss/GameBoss.h"
#include "core/Collision.h"
#include "core/DebugOptions.h"
#include "game/Combat.h"
#include "game/GameEffect.h"
#include "game/GameEnemy.h"
#include "game/GamePlayer.h"
#include "game/GamePlayerBullet.h"
#include "game/GameSpecialShot.h"
#include "game/GameState.h"
#include "game/GameWave.h"
#include "game/ScreenShake.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include "sim_input.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace
{
// ---------------------------------------------------------------------------------------------------------------------------------------------
// the player: three levels of skill
// ---------------------------------------------------------------------------------------------------------------------------------------------
struct Skill
{
    const char* name;
    int period;        // frames between two decisions
    int latency;       // frames between a decision and the keys
    float noise;       // how far the bot misjudges where things are (normalized units)
    float errorRate;   // chance that a decision is a wrong one
    int horizon;       // how many frames ahead it looks
    float focus;       // (unused: there is no precision mode any more)
    int switchDelay;   // frames between seeing that the colour is wrong and pressing the key
    float absorbSeek;  // how much it likes to stand where a bullet of its colour comes
    int specialDelay;  // frames between a full bar and the key
    float breakRate;   // chance that a run is played for the overload: the bar is kept for the moment the boss charges its finisher, then fired at the weak point
};
const Skill SKILLS[3] = {{"novice", 7, 9, 0.025f, 0.08f, 12, 0.10f, 20, 0.06f, 90, 0.0f},
                         {"average", 4, 6, 0.012f, 0.03f, 16, 0.50f, 12, 0.10f, 40, 0.55f},
                         {"expert", 2, 3, 0.004f, 0.01f, 22, 0.90f, 7, 0.13f, 6, 0.95f}};

struct Obj
{
    float x, y, vx, vy, hw, hh;
    int colour; // 0 red, 1 blue, 2 hurts every colour (white bullets, bodies)
};
struct Move
{
    int dx, dy;
    bool focus;
};

std::mt19937 g_Rng(1);
float Gauss(float sigma)
{
    std::normal_distribution<float> d(0.0f, sigma);
    return d(g_Rng);
}
float Uni()
{
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(g_Rng);
}

constexpr float SPEED_X = 0.018f * (720.0f / 1280.0f) * 1.08f;
constexpr float SPEED_Y = 0.018f;
constexpr float HURT_HW = 0.030f; // the core of the ship that can be hurt (GamePlayer.cpp HURT_HALF_*)
constexpr float HURT_HH = 0.040f;

struct Bot
{
    Skill sk;
    int frame = 0;
    Move current{0, 0, false};
    std::vector<std::pair<int, Move>> queue; // (frame at which it takes effect, the move): a decision reaches the keys after the latency
    int switchAt = -1;
    int lastSwitch = -1000;
    int fullSince = -1;
    int pickWait = 0;
    bool holdMode = false;  // this run plays for the overload (see Skill::breakRate)
    int windowMax = 0;      // frames the overload charge that is on has lasted in total (0 = none is charging)
    std::vector<Obj> objs;
    std::vector<GameBoss::SimLane> lanes;
    float nx = 0.0f, ny = 0.0f;

    void Gather()
    {
        objs.clear();
        for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
        {
            const EnemyBullet& b = GameState::enemyBullets[i];
            if (!b.active) continue;
            objs.push_back(Obj{b.x, b.y, b.velocityX, b.velocityY, b.halfWidth, b.halfHeight, b.neutral ? 2 : (b.polarity == Polarity::Red ? 0 : 1)});
        }
        GameBoss::SimBolt bolts[400];
        const int n = GameBoss::GetBoltsForSim(bolts, 400);
        for (int i = 0; i < n; ++i)
            objs.push_back(Obj{bolts[i].x, bolts[i].y, bolts[i].vx, bolts[i].vy, bolts[i].halfWidth, bolts[i].halfHeight, bolts[i].white ? 2 : (bolts[i].red ? 0 : 1)});
        for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        {
            const Enemy& e = GameState::enemies[i];
            if (!e.active) continue;
            float vx = 0.0f, vy = 0.0f;
            if ((e.type == EnemyType::Charger && e.state == EnemyState::Charging)
                || (e.type == EnemyType::Rival && e.state == EnemyState::Attack && e.pendingAttackType == EnemyAttackType::Dash))
            {
                vx = e.vx;
                vy = e.vy;
            }
            else if (e.type == EnemyType::Rival && (e.state == EnemyState::Reposition || e.state == EnemyState::Evade)) // a person sees where it is going
            {
                vx = (std::max)(-0.022f, (std::min)(0.022f, e.targetX - e.x));
                vy = (std::max)(-0.024f, (std::min)(0.024f, e.targetY - e.y));
            }
            else if (e.state == EnemyState::Enter) // scouts and sines never stop: they fly across the whole screen
            {
                vx = e.type == EnemyType::Rival ? -0.009f : -std::fabs(e.vx);
            }
            objs.push_back(Obj{e.x, e.y, vx, vy, e.halfWidth, e.halfHeight, 2});
        }
        lanes.resize(8);
        const int ln = GameBoss::GetLanesForSim(lanes.data(), 8);
        lanes.resize(static_cast<size_t>(ln));
        {
            // the rails of the elite snipers: where they cross the column of the ship
            float rx[4], ry[4], ra[4];
            int rf[4];
            const int rn = GameEnemy::GetRailsForSim(rx, ry, ra, rf, 4);
            for (int k = 0; k < rn; ++k)
            {
                const float c = std::cos(ra[k]);
                if (c > -0.05f) continue;
                const float aspect = 720.0f / 1280.0f;
                const float y = ry[k] + (GameState::player.x - rx[k]) * std::tan(ra[k]) / aspect;
                GameBoss::SimLane lane{};
                lane.y = y;
                lane.halfHeight = 0.024f / aspect / std::fabs(c);
                lane.framesUntil = rf[k];
                lanes.push_back(lane);
            }
        }
        for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        {
            const Enemy& e = GameState::enemies[i];
            // the lane of a dash of the rival is drawn on the screen: it follows the ship until 14 frames before the dash and is locked after that
            if (e.active && e.type == EnemyType::Rival && e.state == EnemyState::AttackWarning && e.pendingAttackType == EnemyAttackType::Dash)
                lanes.push_back(GameBoss::SimLane{e.targetY, e.halfHeight * 1.2f, (std::max)(0, e.warningTimer - 14), 1.0f, false, 0.0f, 45});
            // ... and while it dashes the lane is the line it flies along: where that line is when it reaches the ship's column
            if (e.active && e.type == EnemyType::Rival && e.state == EnemyState::Attack && e.pendingAttackType == EnemyAttackType::Dash && e.vx < -0.001f)
            {
                const float px = GameState::player.x;
                const float frames = (e.x - px) / -e.vx;
                lanes.push_back(GameBoss::SimLane{e.y + e.vy / e.vx * (px - e.x), e.halfHeight * 1.2f, (std::max)(0, static_cast<int>(frames) - 8), 0.0f, false, 0.0f, 0});
            }
        }
    }

    bool Hostile(const Obj& o, int colour) const { return o.colour == 2 || o.colour != colour; }

    // how bad a plan is: the sum, over the frames of the horizon, of the hits that it would take (a near hit weighs more)
    float Risk(const Move& m, int colour, float& absorbed) const
    {
        const Player& p = GameState::player;
        const int H = sk.horizon;
        float px = p.x, py = p.y;
        const float spx = SPEED_X;
        const float spy = SPEED_Y;
        float len = std::sqrt(static_cast<float>(m.dx * m.dx + m.dy * m.dy));
        const float ux = len > 0.0f ? static_cast<float>(m.dx) / len : 0.0f;
        const float uy = len > 0.0f ? static_cast<float>(m.dy) / len : 0.0f;
        const float hw = HURT_HW;
        const float hh = HURT_HH;
        float risk = 0.0f;
        absorbed = 0.0f;
        const float scale = Mod::SiphonScale();
        for (const Obj& o : objs)
        {
            // only the objects that can come near the ship within the horizon
            const float ox0 = o.x + nx, oy0 = o.y + ny;
            const float ox1 = ox0 + o.vx * static_cast<float>(H), oy1 = oy0 + o.vy * static_cast<float>(H);
            const float reachX = spx * static_cast<float>(H) + 0.15f, reachY = spy * static_cast<float>(H) + 0.20f;
            if ((std::max)(ox0, ox1) < p.x - reachX || (std::min)(ox0, ox1) > p.x + reachX) continue;
            if ((std::max)(oy0, oy1) < p.y - reachY || (std::min)(oy0, oy1) > p.y + reachY) continue;
            const bool hostile = Hostile(o, colour);
            bool counted = false;
            px = p.x;
            py = p.y;
            for (int k = 1; k <= H; ++k)
            {
                px = (std::max)(-0.94f, (std::min)(-0.20f, px + ux * spx));
                py = (std::max)(-0.85f, (std::min)(0.85f, py + uy * spy));
                const float bx = ox0 + o.vx * static_cast<float>(k), by = oy0 + o.vy * static_cast<float>(k);
                if (hostile)
                {
                    if (std::fabs(px - bx) <= hw + o.hw && std::fabs(py - by) <= hh + o.hh)
                    {
                        risk += static_cast<float>(H - k + 2) / static_cast<float>(H);
                        break;
                    }
                }
                else if (!counted && std::fabs(px - bx) <= p.halfWidth * scale + o.hw && std::fabs(py - by) <= p.halfHeight * scale + o.hh)
                {
                    absorbed += 1.0f;
                    counted = true;
                }
            }
        }
        {
            // (the piercing beam does not protect the ship any more: the lanes count while it fires, too)
            // the white beams: a lane that is still charging follows the ship (and keeps its distance from the middle), so the lane is simulated along the plan
            for (const GameBoss::SimLane& lane : lanes)
            {
                const int span = (std::min)(lane.framesUntil + lane.delay + 50, 140);
                float ly = lane.y;
                px = p.x;
                py = p.y;
                bool hit = false;
                for (int k = 1; k <= span && !hit; ++k)
                {
                    py = (std::max)(-0.85f, (std::min)(0.85f, py + uy * spy));
                    if (lane.chase > 0.0f && k <= lane.framesUntil)
                    {
                        const float target = lane.mirror ? -py : py;
                        ly += (std::max)(-lane.chase, (std::min)(lane.chase, target - ly));
                        ly = (std::max)(-0.62f, (std::min)(0.62f, ly));
                        if (lane.minAbs > 0.0f && std::fabs(ly) < lane.minAbs) ly = ly < 0.0f ? -lane.minAbs : lane.minAbs;
                    }
                    if (k >= lane.framesUntil + lane.delay && std::fabs(py - ly) <= lane.halfHeight + hh + 0.015f)
                    {
                        risk += 3.0f;
                        hit = true;
                    }
                }
            }
        }
        return risk;
    }

    bool Target(float& tx, float& ty) const
    {
        if (GameBoss::GetTargetPosition(tx, ty)) return true;
        float best = 1.0e9f;
        bool found = false;
        for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        {
            const Enemy& e = GameState::enemies[i];
            if (!e.active || e.x > 1.0f) continue;
            if (e.x < best)
            {
                best = e.x;
                tx = e.x;
                ty = e.y;
                found = true;
            }
        }
        return found;
    }

    void Decide()
    {
        Gather();
        nx = Gauss(sk.noise);
        ny = Gauss(sk.noise);
        const int colour = GameState::player.polarity == Polarity::Red ? 0 : 1;
        // the colour: which of the two is hit less right now (the plan "stand still")
        {
            float a, b;
            const float riskNow = Risk(Move{0, 0, false}, colour, a);
            const float riskOther = Risk(Move{0, 0, false}, 1 - colour, b);
            const bool wantSwitch = riskNow > 0.6f && riskOther + 0.8f < riskNow;
            if (wantSwitch && switchAt < 0 && frame - lastSwitch > 20) switchAt = frame + sk.switchDelay + static_cast<int>(Uni() * 6.0f);
            if (!wantSwitch && switchAt >= 0 && riskNow < 0.3f) switchAt = -1; // it was not needed after all
        }
        float tx = 0.0f, ty = 0.0f;
        const bool haveTarget = Target(tx, ty);
        Move best{0, 0, false};
        float bestCost = 1.0e9f;
        std::vector<std::pair<Move, float>> all;
        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int f = 0; f < 2; ++f)
                {
                    if (f == 1) continue; // (there is no precision mode any more)
                    const Move m{dx, dy, f == 1};
                    float absorbed = 0.0f;
                    const float risk = Risk(m, colour, absorbed);
                    const float sx = SPEED_X, sy = SPEED_Y;
                    const float endY = GameState::player.y + static_cast<float>(dy) * sy * 6.0f;
                    const float endX = GameState::player.x + static_cast<float>(dx) * sx * 6.0f;
                    float cost = risk * 10.0f - absorbed * sk.absorbSeek * 4.0f + (m.focus ? 0.6f : 0.0f);
                    if (haveTarget) cost += 0.9f * std::fabs(endY - ty);
                    cost += 4.0f * (std::max)(0.0f, endX + 0.30f);          // it does not leave the left side
                    cost += 3.0f * (std::max)(0.0f, -0.80f - endX);
                    cost += (dx == 0 && dy == 0) ? 0.0f : 0.05f;
                    all.push_back({m, cost});
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        best = m;
                    }
                }
            }
        }
        if (Uni() < sk.errorRate) best = all[static_cast<size_t>(Uni() * static_cast<float>(all.size()))].first; // a wrong decision
        queue.push_back({frame + sk.latency, best});
        if (std::getenv("SIM_DEBUG2") && frame % 100 < sk.period)
        {
            float a;

            std::fprintf(stderr, "f=%d objs=%zu stayRisk=%.2f best=(%d,%d,%d) cost=%.2f target=%d (%.2f,%.2f) ship(%.2f,%.2f)\n", frame, objs.size(), Risk(Move{0, 0, false}, colour, a), best.dx, best.dy, best.focus, bestCost, haveTarget, tx, ty, GameState::player.x, GameState::player.y);
        }
    }

    void Pick()
    {
        // the pick screens: a random card (the luck of the draft); a locked route is not chosen
        if (Mod::g_BranchOpen)
        {
            if (Mod::g_BranchTimer <= Mod::CHOICE_INPUT_LOCK + 2) return;
            int options[3], n = 0;
            for (int i = 0; i < Mod::g_RouteCount; ++i)
                if (!Mod::RouteLocked(Mod::g_RouteKind[i])) options[n++] = i;
            if (n == 0) n = 1, options[0] = 0;
            const int slot = options[static_cast<int>(Uni() * static_cast<float>(n)) % n];
            SimInput::down['1' + slot] = true;
            return;
        }
        if (Mod::g_ChoiceTimer <= Mod::CHOICE_INPUT_LOCK + 2) return;
        const int slot = static_cast<int>(Uni() * static_cast<float>(Mod::g_ChoiceCount)) % (std::max)(1, Mod::g_ChoiceCount);
        SimInput::down['1' + slot] = true;
    }

    void Update(bool draft)
    {
        ++frame;
        SimInput::Clear();
        if (Mod::ChoiceOpen())
        {
            if (draft)
                Pick();
            else
            {
                Mod::g_ChoiceOpen = false; // no picks: the build of the run is fixed by --mods
                Mod::g_BranchOpen = false;
                Mod::g_ChoiceDone = true;
                Mod::g_PicksLeft = 0;
                if (Mod::g_RouteStage == 1) Mod::g_BranchDone = true, Mod::g_Branch = Uni() < 0.5f ? 1 : 2; // the route is chosen by chance, like the drafts do
                if (Mod::g_RouteStage == 2) Mod::g_Branch2Done = true;
            }
            return;
        }
        if (GameState::playerHp <= 0) return;
        if (frame % sk.period == 0) Decide();
        while (!queue.empty() && queue.front().first <= frame)
        {
            current = queue.front().second;
            queue.erase(queue.begin());
        }
        SimInput::down[VK_SPACE] = true; // it shoots all the time
        if (current.dx < 0) SimInput::down['A'] = true;
        if (current.dx > 0) SimInput::down['D'] = true;
        if (current.dy < 0) SimInput::down['S'] = true;
        if (current.dy > 0) SimInput::down['W'] = true;

        if (switchAt >= 0 && frame >= switchAt)
        {
            SimInput::down['C'] = true;
            lastSwitch = frame;
            switchAt = -1;
        }
        // (with TWIN half a bar fires a half beam: the bot fires it as soon as it can, except when it keeps the bar for an overload)
        const bool twin = Mod::g_Owned[static_cast<int>(Mod::Id::Twin)];
        const bool full = GameState::energy >= Mod::EnergyMax() || (twin && !holdMode && GameState::energy >= Mod::EnergyMax() / 2);
        if (!full) fullSince = -1;
        else if (fullSince < 0) fullSince = frame;
        int windowLeft = 0;
        bool charging = GameBoss::GetInterruptWindowForSim(windowLeft);
        if (!charging) windowMax = 0;
        else if (windowMax == 0) windowMax = windowLeft;
        bool rivalOnField = false;
        for (int i = 0; i < GameState::ENEMY_MAX && !rivalOnField; ++i) rivalOnField = GameState::enemies[i].active && GameState::enemies[i].type == EnemyType::Rival;
        if (full && !GameSpecialShot::IsActive())
        {
            const bool fight = GameBoss::IsBattleStarted() || rivalOnField;
            if (!fight || !holdMode)
            {
                if (frame - fullSince >= sk.specialDelay) SimInput::down['V'] = true;
            }
            else if (charging)
            {
                // the beam goes when the charge has been seen for a moment (the reaction time of the skill)
                if (windowMax - windowLeft >= sk.specialDelay) SimInput::down['V'] = true;
            }
            else if (frame - fullSince > 900)
            {
                SimInput::down['V'] = true; // the bar has been full for a long time and no overload came: it is not wasted
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------
// one run: the update step of PlayScene::Update (without the screens)
// ---------------------------------------------------------------------------------------------------------------------------------------------
struct Result
{
    int result = 2;
    int wave = 1;
    int bossPhase = 0;
    int frames = 0;
    int hp = 0;
    int hits = 0;
    int absorbs = 0;
    unsigned mask = 0;
    int rivalFrames = 0; // frames in which the mini boss (the rival) was on the field
    int rivalDamage = 0; // HP the ship lost meanwhile
    int bossFrames = 0;  // frames from the warning of the boss to the end of the run
    int overloads = 0;   // overloads the boss charged / how many of them were broken
    int breaks = 0;
    int phaseAt[4] = {};  // boss frame at which phase 2 and 3 began (index = phase)
    int hitsAtP3 = -1;    // the hits of the run when phase 3 began
    int p3Open = 0;       // frames of phase 3 with the shell of the core open
    int hpBoss = -1;     // HP of the ship when the boss appeared
    int waveStart[8] = {}; // the frame at which wave 1 .. 5 and the boss began (index = wave number)
    int hitsAt[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // the hits of the run when wave 1 .. 5 and the boss began
};

void PlayStep(bool& dead, bool& cleared)
{
    Mod::UpdateUi();
    if (GameState::hitStop > 0)
    {
        GameState::hitStop--;
        return;
    }
    if (Mod::ChoiceOpen()) Mod::UpdateChoice();
    if (!Mod::StepWorld()) return;
    GamePlayer::BeginStep(GameState::player);
    GamePlayerBullet::BeginStep();
    GameEnemy::BeginStep(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX);
    Mod::Tick();
    ScreenShake::Update();
    GameEffect::Update();
    if (GameState::playerInvincibleTimer > 0) GameState::playerInvincibleTimer--;
    if (GameState::energyFlashTimer > 0) GameState::energyFlashTimer--;
    if (GameState::comboRankEffectTimer > 0) GameState::comboRankEffectTimer--;
    if (GameState::playerDeathTimer > 0)
    {
        GameState::playerDeathTimer--;
        if (GameState::playerDeathTimer <= 0)
        {
            GameState::playerDeathTimer = 0;
            dead = true;
        }
        return;
    }
    if (!GameBoss::IsDefeated())
    {
        GamePlayer::Update(GameState::player, GameState::shotCoolTime);
        Mod::AfterPlayerUpdate();
        GamePlayerBullet::Update();
        if (GameSpecialShot::TryActivate(GameState::player, GameState::energy, Mod::EnergyMax()))
        {
            Mod::OnSpecialActivated();
            ScreenShake::Start(10, 0.009f);
        }
    }
    const bool shouldApplySpecialDamage = GameSpecialShot::Update(GameState::player);
    if (shouldApplySpecialDamage && !GameBoss::IsDefeated()) Combat::ApplySpecialShotDamage();
    Mod::UpdateSpecial();
    if (!GameBoss::IsDefeated())
    {
        GameWave::Update(GameState::enemies, GameState::ENEMY_MAX);
        Mod::UpdateWaveEvents();
        if (GameWave::IsAllFinished() && !GameBoss::IsActive())
        {
            GameBoss::Start();
            Mod::LogEvent("boss_start", "");
        }
        GameEnemy::Update(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX, GameState::player);
    }
    GameBoss::Update(GameState::player);
    {
        const int hitStop = GameBoss::ConsumeHitStop();
        if (hitStop > GameState::hitStop) GameState::hitStop = hitStop;
        int d = 0;
        float s = 0.0f;
        while (GameBoss::ConsumeShake(d, s)) ScreenShake::Start(d, s);
        GameState::energy += GameBoss::ConsumeEnergyReward();
        if (GameState::energy > Mod::EnergyMax()) GameState::energy = Mod::EnergyMax();
    }
    if (!GameBoss::IsDefeated())
    {
        Mod::UpdateZones();
        Combat::CheckBulletEnemyCollision();
        GameBoss::CheckPlayerBullets(GamePlayerBullet::GetBullets(), GamePlayerBullet::GetBulletCount());
        if (GameState::playerHp > 0)
        {
            const bool invincible = GameState::playerInvincibleTimer > 0;
            const BossBulletHitResult hit = GameBoss::CheckPlayerCollision(GameState::player, invincible);
            if (hit == BossBulletHitResult::Absorbed)
            {
                GameEffect::SpawnAbsorb(GameState::player.x, GameState::player.y, 0.095f, GameState::player.polarity);
                Mod::OnAbsorb(GameState::ENERGY_PER_ABSORB_BOSS);
                Mod::FlushBossAbsorbBonus();
            }
            else if (hit == BossBulletHitResult::Damaged)
            {
                Combat::HurtPlayer(GameBoss::GetLastHitDamage(), GameBoss::GetLastHitSource());
            }
        }
        Combat::CheckPlayerEnemyCollision();
        Combat::CheckPlayerEnemyBulletCollision();
    }
    if (GameBoss::IsDefeatAnimationFinished())
    {
        cleared = true;
        Mod::LogEvent("boss_clear", "");
    }
}

void StartRun(const std::vector<int>& mods)
{
    GamePlayer::Initialize(GameState::player);
    GameState::player.polarity = Polarity::Blue;
    GamePlayerBullet::Initialize();
    GameSpecialShot::Initialize();
    GameEffect::Initialize();
    GameEnemy::Initialize(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX);
    GameWave::Initialize();
    GameBoss::Initialize();
    GameState::ResetRun();
    ScreenShake::Reset();
    Mod::Reset();
    for (int id : mods) Mod::Grant(static_cast<Mod::Id>(id));
}

Result RunOne(const Skill& skill, bool draft, bool withBoss, const std::vector<int>& mods, int maxFrames)
{
    StartRun(mods);
    Bot bot;
    bot.sk = skill;
    bot.holdMode = Uni() < skill.breakRate;
    Result r;
    bool dead = false, cleared = false;
    for (int f = 0; f < maxFrames && !dead && !cleared; ++f)
    {
        bot.Update(draft);
        const int hpBefore = GameState::playerHp;
        bool rivalBefore = false;
        for (int i = 0; i < GameState::ENEMY_MAX; ++i)
            if (GameState::enemies[i].active && GameState::enemies[i].type == EnemyType::Rival) rivalBefore = true;
        PlayStep(dead, cleared);
        if (rivalBefore)
        {
            ++r.rivalFrames;
            if (GameState::playerHp < hpBefore)
            {
                r.rivalDamage += hpBefore - (std::max)(0, GameState::playerHp);
                if (std::getenv("SIM_RIVAL")) // which move of the rival did it?
                {
                    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
                    {
                        const Enemy& e = GameState::enemies[i];
                        if (e.active && e.type == EnemyType::Rival)
                            std::fprintf(stderr, "RIVALHIT move=%d state=%d dmg=%d rival(%.2f,%.2f)->(%.2f,%.2f) ship(%.2f,%.2f) timer=%d\n", static_cast<int>(e.pendingAttackType), static_cast<int>(e.state), hpBefore - (std::max)(0, GameState::playerHp), e.x, e.y, e.targetX, e.targetY, GameState::player.x, GameState::player.y, e.stateTimer);
                    }
                }
            }
        }
        if (std::getenv("SIM_DEBUG") && GameState::playerHp < hpBefore)
        {
            // what hurt the ship: the closest enemy and the closest bullet
            float dBullet = 9.0f, dEnemy = 9.0f;
            int bPol = -1, bNeutral = 0, eType = -1;
            for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i)
            {
                const EnemyBullet& b = GameState::enemyBullets[i];
                if (!b.active) continue;
                const float d = std::fabs(b.x - GameState::player.x) + std::fabs(b.y - GameState::player.y);
                if (d < dBullet) { dBullet = d; bPol = b.polarity == Polarity::Red; bNeutral = b.neutral; }
            }
            for (int i = 0; i < GameState::ENEMY_MAX; ++i)
            {
                const Enemy& e = GameState::enemies[i];
                if (!e.active) continue;
                const float d = std::fabs(e.x - GameState::player.x) + std::fabs(e.y - GameState::player.y);
                if (d < dEnemy) { dEnemy = d; eType = static_cast<int>(e.type); }
            }
            std::fprintf(stderr, "atk=%d bossphase=%d ", GameBoss::GetAttackForSim(), GameBoss::IsActive() ? GameBoss::GetPhase() : 0);
            if (GameBoss::GetAttackForSim() == 9 || GameBoss::GetAttackForSim() == 8) // (the overload beam, the rail strike)
            {
                GameBoss::SimLane ls[8];
                const int ln = GameBoss::GetLanesForSim(ls, 8);
                std::fprintf(stderr, "[ship y=%.3f focus=%d lanes:", GameState::player.y, bot.current.focus);
                for (int i = 0; i < ln; ++i) std::fprintf(stderr, " (y=%.3f half=%.3f in=%d)", ls[i].y, ls[i].halfHeight, ls[i].framesUntil);
                std::fprintf(stderr, "] ");
            }
            std::fprintf(stderr, "f=%d hp %d->%d wave %d ship(%.2f,%.2f) pol=%d | nearest bullet d=%.3f pol=%d neutral=%d | nearest enemy d=%.3f type=%d | move(%d,%d,f%d)\n", f, hpBefore, GameState::playerHp,
                         GameWave::GetCurrentWave(), GameState::player.x, GameState::player.y, GameState::player.polarity == Polarity::Red, dBullet, bPol, bNeutral, dEnemy, eType, bot.current.dx, bot.current.dy, bot.current.focus);
        }
        SimInput::EndFrame();
        r.frames = f + 1;
        if (GameBoss::IsActive()) ++r.bossFrames;
        {
            const int w = GameBoss::IsActive() ? Balance::BOSS_WAVE : GameWave::GetCurrentWave();
            if (w >= 1 && w <= Balance::BOSS_WAVE && r.waveStart[w] == 0) r.waveStart[w] = f + 1;
            if (w >= 1 && w <= Balance::BOSS_WAVE && r.hitsAt[w] < 0) r.hitsAt[w] = Mod::g_RunHits;
            if (GameBoss::IsActive() && r.hpBoss < 0) r.hpBoss = (std::max)(0, GameState::playerHp);
            if (GameBoss::IsActive() && GameBoss::GetPhase() >= 2 && r.phaseAt[2] == 0) r.phaseAt[2] = r.bossFrames;
            if (GameBoss::IsActive() && GameBoss::GetPhase() >= 3 && r.phaseAt[3] == 0)
            {
                r.phaseAt[3] = r.bossFrames;
                r.hitsAtP3 = Mod::g_RunHits;
            }
            if (GameBoss::IsActive() && GameBoss::GetPhase() >= 3 && GameBoss::IsBattleStarted() && GameBoss::IsCoreExposedForSim()) ++r.p3Open;
        }
        if (!withBoss && GameBoss::IsActive()) // waves only: the run counts as cleared when the boss would appear
        {
            cleared = true;
            r.bossPhase = 0;
        }
    }
    r.result = dead ? 0 : (cleared ? 1 : 2);
    r.wave = GameBoss::IsActive() ? Balance::BOSS_WAVE : GameWave::GetCurrentWave();
    r.bossPhase = GameBoss::IsActive() ? GameBoss::GetPhase() : 0;
    r.hp = (std::max)(0, GameState::playerHp);
    for (int i = 0; i < Mod::COUNT; ++i)
        if (Mod::g_Owned[i]) r.mask |= 1u << i;
    r.absorbs = Mod::g_RunAbsorbs;
    r.hits = Mod::g_RunHits;
    GameBoss::GetBreakStatsForSim(r.overloads, r.breaks);
    return r;
}
}

int main(int argc, char** argv)
{
    int runs = 100;
    unsigned seed = 1;
    std::string skillName = "average", unlock = "all", talents = "none";
    bool draft = true, withBoss = true;
    std::vector<int> mods;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--runs") runs = std::atoi(next().c_str());
        else if (a == "--seed") seed = static_cast<unsigned>(std::atoi(next().c_str()));
        else if (a == "--skill") skillName = next();
        else if (a == "--unlock") unlock = next();
        else if (a == "--talents") talents = next(); // none (default: a new player has none) | all
        else if (a == "--draft") draft = next() != "none";
        else if (a == "--boss") withBoss = next() != "0";
        else if (a == "--mods")
        {
            std::string list = next();
            size_t pos = 0;
            while (pos < list.size())
            {
                size_t end = list.find(',', pos);
                if (end == std::string::npos) end = list.size();
                if (end > pos) mods.push_back(std::atoi(list.substr(pos, end - pos).c_str()));
                pos = end + 1;
            }
        }
    }
    {
        std::ofstream cfg("config.ini");
        cfg << "[game]\nunlock_all=" << (unlock == "all" ? 1 : 0) << "\nno_talents=" << (talents == "all" ? 0 : 1) << "\n";
    }
    int skillIndex = 1;
    for (int i = 0; i < 3; ++i)
        if (skillName == SKILLS[i].name) skillIndex = i;
    std::srand(seed);
    g_Rng.seed(seed * 7919u + 13u);
    for (int n = 0; n < runs; ++n)
    {
        std::srand(seed * 100003u + static_cast<unsigned>(n));
        g_Rng.seed(seed * 7919u + static_cast<unsigned>(n) * 31u + 13u);
        const Result r = RunOne(SKILLS[skillIndex], draft, withBoss, mods, 60 * 60 * 9);
        if (const char* statsPath = std::getenv("SIM_STATS_FILE")) // what the modules did in this run (the report of the game over page): one line per module, and one for the run
        {
            std::ofstream stats(statsPath, std::ios::app);
            stats << "run," << seed << ',' << n << ',' << r.result << ',' << r.wave << ',' << r.bossPhase << ',' << r.frames << ',' << Mod::g_Branch << ',' << Mod::ReportTotalDamage() << ',' << Mod::ReportTotalEnergy() << ',' << Mod::g_OrderCount << ',' << Mod::g_RunHits << '\n';
            for (int i = 0; i < Mod::ModuleReportCount(); ++i)
            {
                const Mod::ModuleReportRow row = Mod::ModuleReportAt(i);
                stats << "mod," << seed << ',' << n << ',' << r.result << ',' << row.module << ',' << row.level << ',' << row.damage << ',' << row.energy << ',' << row.hp << ',' << row.count << '\n';
            }
        }
        std::printf("%u,%s,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", seed, SKILLS[skillIndex].name, r.result, r.wave, r.bossPhase, r.frames, r.hp, r.hits, r.absorbs, r.mask, r.rivalFrames, r.rivalDamage, r.bossFrames, r.overloads, r.breaks, r.hpBoss, r.waveStart[2], r.waveStart[3], r.waveStart[4], r.waveStart[5], r.waveStart[6], r.phaseAt[2], r.phaseAt[3], r.hitsAtP3 >= 0 ? r.hits - r.hitsAtP3 : -1, r.p3Open, r.hitsAt[1], r.hitsAt[2], r.hitsAt[3], r.hitsAt[4], r.hitsAt[5], r.hitsAt[6], static_cast<int>(Mod::g_StatKillEnergy), static_cast<int>(Mod::g_StatAbsorbEnergy));
    }
    return 0;
}


