#include "game/GameEnemy.h"
#include "game/EnemyInternal.h"
#include "game/GameVfx.h"
#include "game/EnemyArt.h"
#include "game/GameEffect.h"

#include "core/Renderer2D.h"
#include "core/AssetManager.h"
#include "game/GameSpecialShot.h"
#include "core/GameAudio.h"
#include "game/ScreenShake.h"
#include "game/GameState.h"
#include "rogue/Balance.h"
#include "rogue/Meta.h"
#include "rogue/Modules.h"
#include "core/GameTimer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// The cues for the telegraphs of the charger (the lane is locked) and the sniper (it takes aim): a pinging sound, the world is lowered for a moment. Set it to 0 to take them out.
#ifndef GAME_AUDIO_TELEGRAPH_CUES
#define GAME_AUDIO_TELEGRAPH_CUES 1
#endif


namespace EnemyInternal
{
MuzzleVisual g_MuzzleVisuals[MUZZLE_VISUAL_MAX]{};
int g_UpdateFrame = 0;

void RecordMuzzleVisual(float x, float y, float angle, Polarity polarity)
{
    for (auto& v : g_MuzzleVisuals)
    {
        if (v.timer >= 5 && std::fabs(v.x - x) < 0.003f && std::fabs(v.y - y) < 0.003f && v.polarity == polarity)
        {
            return;
        }
    }

    for (auto& v : g_MuzzleVisuals)
    {
        if (v.timer > 0)
        {
            continue;
        }

        v.x = x;
        v.y = y;
        v.angle = std::atan2(std::sin(angle) * ASPECT, std::cos(angle));
        v.polarity = polarity;
        v.timer = 6;
        return;
    }
}

void VisualColor(Polarity p, float& r, float& g, float& b)
{
    GameVfx::GetPolarityColor(p == Polarity::Red, r, g, b);
}

void InitializeEnemyByType(Enemy& enemy, EnemyType type)
{
    enemy.type = type;
    enemy.state = EnemyState::Enter;

    enemy.x = 1.15f;
    enemy.y = 0.0f;

    enemy.vx = 0.0f;
    enemy.vy = 0.0f;

    enemy.baseY = 0.0f;
    enemy.moveTime = 0.0f;
    enemy.amplitude = 0.0f;
    enemy.frequency = 0.0f;

    enemy.targetX = 0.0f;
    enemy.targetY = 0.0f;

    enemy.hp = 1;
    enemy.maxHp = 1;

    enemy.stateTimer = 0;
    enemy.shotTimer = 120;
    enemy.hitFlashTimer = 0;

    enemy.attackPatternIndex = 0;
    enemy.warningTimer = 0;

    enemy.attackCount = 0;
    enemy.decisionTimer = 0;
    enemy.polaritySwitchTimer = 0;

    enemy.evadeCooldown = 0;
    enemy.specialShotReactionUsed = false;

    enemy.pendingAttackType = EnemyAttackType::Straight;

    enemy.scoreValue = 100;
    enemy.groupId = -1;
    enemy.fxX = 0.0f;
    enemy.fxY = 0.0f;
    enemy.fxKind = 0;
    enemy.fxTimer = 0;
    enemy.phaseTwoShown = false;
    enemy.phaseThreeShown = false;
    enemy.comboLeft = 0;
    enemy.aux = 0.0f;
    enemy.age = 0;
    enemy.kick = 0;
    enemy.elite = false;
    enemy.shield = false;
    enemy.cycle = 0;
    enemy.dashes = 0;
    enemy.rivalRole = 0;
    switch (type)
    {
    case EnemyType::Scout:
        enemy.halfWidth = 0.059f; // (x 1.18: it read too small)
        enemy.halfHeight = 0.094f;

        enemy.vx = 0.0048f;

        enemy.hp = 1;
        enemy.maxHp = 1;

        enemy.shotTimer = 32 + std::rand() % 28;

        enemy.scoreValue = 100;
        break;

    case EnemyType::Sine:
        enemy.halfWidth = 0.054f; // (x 1.18)
        enemy.halfHeight = 0.087f;

        enemy.vx = 0.0055f;

        enemy.amplitude = 0.13f;
        enemy.frequency = 0.90f;

        enemy.hp = 2;
        enemy.maxHp = 2;

        enemy.shotTimer = 28 + std::rand() % 25;

        enemy.scoreValue = 200;
        break;

    case EnemyType::Heavy:
        enemy.halfWidth = 0.095f;
        enemy.halfHeight = 0.145f;

        enemy.vx = 0.0030f;
        enemy.vy = 0.0f;

        enemy.hp = 16;
        enemy.maxHp = 16;

        enemy.stateTimer = 0;
        enemy.shotTimer = 20;

        enemy.scoreValue = 700;
        break;

    case EnemyType::Charger:
        enemy.halfWidth = 0.062f; // (x 1.12)
        enemy.halfHeight = 0.095f;

        enemy.vx = 0.0040f;
        enemy.vy = 0.0f;

        enemy.hp = 2;
        enemy.maxHp = 2;

        enemy.stateTimer = 0;
        enemy.shotTimer = 0;

        enemy.scoreValue = 300;
        break;

    case EnemyType::Turret:
        enemy.halfWidth = 0.085f;
        enemy.halfHeight = 0.120f;

        enemy.vx = 0.0035f;
        enemy.vy = 0.0f;

        enemy.hp = 22;
        enemy.maxHp = 22;

        enemy.stateTimer = 0;
        enemy.shotTimer = 0;
        enemy.warningTimer = 0;

        enemy.attackPatternIndex = 0;

        enemy.pendingAttackType = EnemyAttackType::Fan;

        enemy.scoreValue = 900;
        break;

    case EnemyType::Sniper:
        enemy.halfWidth = 0.060f;
        enemy.halfHeight = 0.090f;

        enemy.vx = 0.0065f;
        enemy.vy = 0.0f;

        enemy.hp = 14;
        enemy.maxHp = 14;

        enemy.stateTimer = 0;
        enemy.shotTimer = 0;
        enemy.warningTimer = 0;

        enemy.attackPatternIndex = 0;

        enemy.pendingAttackType = EnemyAttackType::Aimed;

        enemy.scoreValue = 650;
        break;

    case EnemyType::Rival:
        enemy.halfWidth = 0.060f;
        enemy.halfHeight = 0.090f;

        enemy.vx = 0.0080f;
        enemy.vy = 0.0100f;

        enemy.hp = 40;
        enemy.maxHp = 40;

        enemy.targetX = 0.52f;
        enemy.targetY = 0.0f;

        enemy.stateTimer = 0;
        enemy.shotTimer = 120;
        enemy.hitFlashTimer = 0;
        enemy.damageCooldown = 0;

        enemy.attackPatternIndex = 0;
        enemy.attackCount = 0;
        enemy.decisionTimer = 0;
        enemy.polaritySwitchTimer = 0;

        enemy.pendingAttackType = EnemyAttackType::Aimed;

        enemy.scoreValue = 1600;
        break;
    }

    // the hit points come from one table (Balance.h), in tenths of a shot
    enemy.maxHp = Balance::ENEMY_HP[static_cast<int>(type)] * Balance::HP_UNIT;
    enemy.hp = enemy.maxHp;
    enemy.shield = (type == EnemyType::Rival); // (the rival has the shield of an elite)
}

float ClampFloat(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

float MoveTowards(float current, float target, float maxDelta)
{
    if (current < target)
    {

        current += maxDelta;

        if (current > target)
        {
            current = target;
        }
    }
    else if (current > target)
    {
        current -= maxDelta;

        if (current < target)
        {
            current = target;
        }
    }

    return current;
}

float NormalizeAngle(float angle)
{
    while (angle > PI)
    {
        angle -= PI * 2.0f;
    }

    while (angle < -PI)
    {
        angle += PI * 2.0f;
    }

    return angle;
}

// ---- elites do not pile up ---------------------------------------------------------------------------------------------------
// Heavy / turret / sniper / rival are big and all stop at x = 0.6 .. 0.7. Two of them that come in at the same height would
// sit on top of each other, so a new elite is moved to the nearest height where it has room, and a sniper that repositions picks a
// height that is free, too.
Enemy* g_EnemyList = nullptr; // the enemy array of the Spawn / Update call that is running
int g_EnemyListCount = 0;

// the big ships (they stop at x = 0.6 .. 0.75 and must not sit on top of each other)
bool IsEliteEnemy(EnemyType type)
{

    return type == EnemyType::Heavy || type == EnemyType::Turret || type == EnemyType::Sniper || type == EnemyType::Rival;
}

// ---- the elite templates ------------------------------------------------------------------------------------------------------------------------------------
// Every type that has an elite version has one fixed template (no random traits): more life (Balance::ELITE_HP_*), a faster beat, a stronger version of its own
// attack, and one high-risk move that only the elite has (every few cycles; it is shown longer and brighter than the plain attacks):
//   turret  BASTION : waits 30 % shorter, fans of 2 more bullets;    CROSS NET  - two wide fans of the two colours at once, crossing, a gap of two bullets
//   sniper  FALCON  : moves 40 % faster, five needles instead of 3;  RAIL       - a white laser across the screen along the line it locked
//   heavy   FORTRESS: three mines instead of two, they burst denser; MINEFIELD  - four mines around the ship, bursting one after the other
//   charger HOUND   : dashes 30 % faster, stays, leaves more bullets; ZIGZAG     - three dashes in a row, each lane shown before it
constexpr int ELITE_MOVE_EVERY = 3;   // cycles between two elite moves (the charger: 2)
constexpr float ELITE_WAIT = 0.70f;   // an elite waits this much of the time a plain one waits
constexpr int CROSSNET_WARN = 36;
constexpr int MINEFIELD_WARN = 34;

int ScaledWait(const Enemy& enemy, int frames)
{
    return enemy.elite ? (std::max)(4, static_cast<int>(static_cast<float>(frames) * ELITE_WAIT)) : frames;
}

void MakeElite(Enemy& enemy)
{
    int hp = 0;
    switch (enemy.type)
    {
    case EnemyType::Turret:
        hp = Balance::ELITE_HP_TURRET;
        break;
    case EnemyType::Sniper:
        hp = Balance::ELITE_HP_SNIPER;
        break;
    case EnemyType::Heavy:
        hp = Balance::ELITE_HP_HEAVY;
        enemy.vx *= 0.85f;
        break;
    case EnemyType::Charger:
        hp = Balance::ELITE_HP_CHARGER;
        break;
    default:
        return;
    }
    enemy.elite = true;
    enemy.shield = true;
    enemy.maxHp = hp * Balance::HP_UNIT;
    enemy.hp = enemy.maxHp;
    enemy.halfWidth *= Balance::ELITE_SIZE;
    enemy.halfHeight *= Balance::ELITE_SIZE;
    enemy.scoreValue *= 3;
}
Rail g_Rails[4]{};

void StartRail(float x0, float y0, float tx, float ty)
{
    for (Rail& r : g_Rails)
    {
        if (r.active) continue;
        r = Rail{true, 1, 0, x0, y0, std::atan2((ty - y0) * ASPECT, tx - x0)};
        GameAudio::PlayAt(GameSound::BossDashWarning, x0);
        return;
    }
}

void UpdateRails()
{
    for (Rail& r : g_Rails)
    {
        if (!r.active) continue;
        ++r.t;
        if (r.state == 1 && r.t >= RAIL_WARN)
        {
            r.state = 2;
            r.t = 0;
            GameAudio::PlayAt(GameSound::BossLaserFire, r.x0);
            ScreenShake::Start(5, 0.004f);
        }
        else if (r.state == 2 && r.t >= RAIL_FIRE)
        {
            r.active = false;
        }
    }
}

// distance of a point to the line of a rail, across it (screen units); 9 = behind the sniper
float RailDistance(const Rail& r, float px, float py)
{
    const float dx = px - r.x0;
    const float dy = (py - r.y0) * ASPECT;
    const float c = std::cos(r.angle), s = std::sin(r.angle);
    if (dx * c + dy * s < 0.0f) return 9.0f;
    return std::fabs(-dx * s + dy * c);
}

// distance to the nearest other elite minus the room the two bodies need: >= 0 means the height is free
float EliteClearance(float y, float halfHeight, const Enemy* self)
{
    float clearance = 10.0f;
    if (g_EnemyList == nullptr)
    {
        return clearance;
    }
    for (int i = 0; i < g_EnemyListCount; i++)
    {
        const Enemy& other = g_EnemyList[i];

        if (&other == self || !other.active || !IsEliteEnemy(other.type))
        {
            continue;
        }
        const float needed = (halfHeight + other.halfHeight) * 1.25f + 0.04f;
        const float room = std::fabs(y - other.y) - needed;
        if (room < clearance)
        {
            clearance = room;
        }
    }
    return clearance;
}

float PickEliteSpawnY(EnemyType type, float requestedY, float halfHeight)
{
    (void)type;
    const float limit = 0.64f;
    constexpr float STEP = 0.06f;
    float bestY = ClampFloat(requestedY, -limit, limit);
    float bestRoom = EliteClearance(bestY, halfHeight, nullptr);
    if (bestRoom >= 0.0f)
    {
        return bestY;
    }
    for (int k = 1; k <= 24; k++)
    {
        for (int side = 0; side < 2; side++)
        {
            const float y = ClampFloat(requestedY + (side == 0 ? 1.0f : -1.0f) * STEP * static_cast<float>(k), -limit, limit);
            const float room = EliteClearance(y, halfHeight, nullptr);
            if (room >= 0.0f)
            {
                return y;
            }
            if (room > bestRoom)
            {
                bestRoom = room;
                bestY = y;
            }
        }
    }
    return bestY; // nothing is free: the place with the most room
}

// the type of the enemy that is being updated: the bullets it fires take its damage, and the sniper's bullets are white (neutral)
int g_ShooterType = 0;
bool g_NextElite = false; // set by GameEnemy::SpawnElite for the Spawn that follows
int g_LastBulletIndex = -1; // the slot of the bullet that SpawnEnemyBullet filled last
int g_BulletsSpawned = 0; // counts every bullet an enemy fires: the update of an enemy that raised it has fired (EnemyArt kicks the hull back)

bool SpawnEnemyBullet(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, float angle, float speed,
                      Polarity polarity, bool isHoming = false, float turnRate = 0.0f, int homingTimer = 0, bool white = false)
{
    for (int i = 0; i < enemyBulletCount; i++)
    {
        EnemyBullet& bullet = enemyBullets[i];

        if (bullet.active)
        {
            continue;
        }

        bullet = {};

        bullet.active = true;

        bullet.x = x;
        bullet.y = y;

        bullet.halfWidth = 0.026f;
        bullet.halfHeight = 0.026f;

        bullet.speed = speed;
        bullet.angle = angle;

        bullet.velocityX = std::cos(angle) * speed;

        bullet.velocityY = std::sin(angle) * speed;

        bullet.polarity = polarity;

        bullet.isHoming = isHoming;
        bullet.turnRate = turnRate;
        bullet.homingTimer = homingTimer;
        bullet.damage = Balance::DMG_BULLET[g_ShooterType];
        bullet.neutral = white || g_ShooterType == static_cast<int>(EnemyType::Sniper); // white: it cannot be absorbed, only avoided
        bullet.owner = g_ShooterType;
        bullet.burst = 0;
        bullet.energyMul = isHoming ? Balance::ABSORB_MUL_HOMING : 1.0f;
        bullet.grazed = false;
        bullet.fuse = 0;
        bullet.size = 1.0f;
        g_LastBulletIndex = i;
        bullet.prevX = x; // a new bullet is drawn from where it was fired
        bullet.prevY = y;
        RecordMuzzleVisual(x, y, angle, polarity);

        return true;
    }

    return false;
}

void SwitchEnemyPolarity(Enemy& enemy)
{
    if (enemy.polarity == Polarity::Red)
    {
        enemy.polarity = Polarity::Blue;
    }
    else
    {
        enemy.polarity = Polarity::Red;
    }

    enemy.polaritySwitchTimer = 18;
}

// An "aimed ring": the bullets of a ring that fly away from the ship are wasted (the ship is on the left, most of the screen is behind the enemy), so every ring of the
// enemies is a wide arc that is centred on the ship. The number of bullets stays the same: they are closer together, and all of them are bullets that can hit the ship
// (or that it can absorb).
constexpr float AIMED_RING_ARC = PI * 0.85f; // 153 degrees: only the bullets within about 35 degrees of the line to the ship can reach it from the other side of the screen

void SpawnHalfRingShot(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, int shotCount, float centerAngle, float spreadAngle, float speed,
                       Polarity polarity);

void SpawnAimedRing(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, int shotCount, float aimAngle, float speed, Polarity polarity,
                    float arc = AIMED_RING_ARC)
{
    SpawnHalfRingShot(enemyBullets, enemyBulletCount, x, y, shotCount, aimAngle, arc, speed, polarity);
}

// ---- the mines of the heavy -------------------------------------------------------------------------------------------------------------------------------

// A mine is a big slow bullet that slows down even more and bursts into a ring of its own colour when the fuse is out. It does not hurt the ship that has its
// colour (the ship absorbs it, and then there is no ring), and it does not move on in the colour that the ship does not have: the ring is what is dangerous.
constexpr float MINE_SPEED = 0.0066f;
constexpr float MINE_DRAG = 0.988f;
constexpr float MINE_SIZE = 1.9f;
constexpr int MINE_RING = 7;
constexpr float MINE_RING_SPEED = 0.0058f;

void SpawnMine(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, float angle, Polarity polarity)
{
    if (!SpawnEnemyBullet(enemyBullets, enemyBulletCount, x, y, angle, MINE_SPEED, polarity) || g_LastBulletIndex < 0)
    {
        return;
    }
    EnemyBullet& mine = enemyBullets[g_LastBulletIndex];
    mine.fuse = MINE_FUSE;
    mine.energyMul = Balance::ABSORB_MUL_MINE;
    mine.size = MINE_SIZE;
    mine.halfWidth = 0.026f * MINE_SIZE * 0.85f;
    mine.halfHeight = 0.026f * MINE_SIZE * 0.85f;
}

bool IsRivalPhaseTwo(const Enemy& enemy)
{
    return enemy.type == EnemyType::Rival && enemy.maxHp > 0 && enemy.hp <= enemy.maxHp / 2;
}

// the duel with two rivals (see SpawnRivalWingman)
int g_NextRivalRole = 0;         // set before Spawn: 1 = the wingman
bool g_RivalWingmanOut = false; // the wingman has joined this duel
bool g_RivalLogged = false;
bool g_TwinEnabled = true;      // (the tests switch the twin off to look at one rival)
int g_RivalsAlive = 0;          // rivals on the field (counted at the start of every step)
int g_RivalFrame = -1;          // the step in which the velocity of the ship was last updated (once per step, whatever the number of rivals)

// The three tiers of the duel: 1 = full life, 2 = half life (the second half), 3 = the last quarter.
int RivalTier(const Enemy& enemy)
{
    if (enemy.type != EnemyType::Rival || enemy.maxHp <= 0) return 1;
    int tier = 1;
    if (enemy.hp <= enemy.maxHp / 4) tier = 3;
    else if (enemy.hp <= enemy.maxHp / 2) tier = 2;
    if (g_RivalWingmanOut && g_RivalsAlive <= 1) tier = 3; // the one that is left on its own fights as it does in its last quarter
    return tier;
}

Polarity OppositeOf(Polarity polarity)
{
    return polarity == Polarity::Red ? Polarity::Blue : Polarity::Red;
}

// (the overload belongs to the boss alone: a rival, like every other enemy, is only shot down)
constexpr int RIVAL_LIFE_PERCENT = 110; // each of the twins has this much of the life of a lone rival

// frames of warning of a move of the mini boss (SelectRivalAttack sets them, GetWarningProgress reads them). Every move that can have the colour of the glow flipped
// against the ship (all but the needles) gives the ship at least 24 frames: the colour switch locks for 18.
int RivalWarningFrames(EnemyAttackType type, int tier)
{
    switch (type)
    {
    case EnemyAttackType::Fan:
        return tier >= 3 ? 24 : (tier == 2 ? 26 : 28);
    case EnemyAttackType::Ring:
        return tier >= 3 ? 28 : (tier == 2 ? 30 : 34);
    case EnemyAttackType::HalfRing:
        return tier >= 3 ? 28 : (tier == 2 ? 30 : 32);
    case EnemyAttackType::Needles:
        return tier >= 2 ? 32 : 38; // the last 12 frames are the locked line
    case EnemyAttackType::Dash:
        return tier >= 3 ? 36 : (tier == 2 ? 40 : 44);
    case EnemyAttackType::Counter:
        return tier >= 2 ? 14 : 18;
    case EnemyAttackType::Mine:
        return tier >= 3 ? 26 : 30;
    case EnemyAttackType::Homing:
        return tier >= 3 ? 24 : 28;
    default:
        return 26;
    }
}

// velocity of the ship as the mini boss sees it (smoothed): the needles are aimed at where the ship is going, not at where it is
float g_RivalPrevPX = 0.0f;
float g_RivalPrevPY = 0.0f;
float g_RivalVelX = 0.0f;
float g_RivalVelY = 0.0f;
bool g_RivalSeen = false;

bool IsRivalInsideSpecialShot(const Enemy& enemy)
{
    if (!GameSpecialShot::IsActive())
    {
        return false;
    }

    const float shotStartX = GameSpecialShot::GetStartX();

    const float shotEndX = GameSpecialShot::GetEndX();

    const float shotCenterY = GameSpecialShot::GetCenterY();

    const float shotHalfHeight = GameSpecialShot::GetHalfHeight();

    const float enemyLeft = enemy.x - enemy.halfWidth;

    const float enemyRight = enemy.x + enemy.halfWidth;

    const float enemyTop = enemy.y + enemy.halfHeight;

    const float enemyBottom = enemy.y - enemy.halfHeight;

    const float shotTop = shotCenterY + shotHalfHeight;

    const float shotBottom = shotCenterY - shotHalfHeight;

    const bool overlapsX = enemyRight >= shotStartX && enemyLeft <= shotEndX;

    const bool overlapsY = enemyTop >= shotBottom && enemyBottom <= shotTop;

    return overlapsX && overlapsY;
}

bool TryBeginRivalEvade(Enemy& enemy)
{
    if (enemy.type != EnemyType::Rival)
    {
        return false;
    }
    if (!GameSpecialShot::IsActive())
    {
        enemy.specialShotReactionUsed = false;
        return false;
    }
    if (enemy.specialShotReactionUsed)
    {
        return false;
    }
    if (enemy.evadeCooldown > 0)
    {

        enemy.specialShotReactionUsed = true;
        return false;
    }
    if (!IsRivalInsideSpecialShot(enemy))
    {
        return false;
    }
    enemy.specialShotReactionUsed = true;
    const bool isPhaseTwo = IsRivalPhaseTwo(enemy);
    const int evadeChance = isPhaseTwo ? 75 : 55;
    const int randomValue = std::rand() % 100;
    if (randomValue >= evadeChance)
    {
        return false;
    }
    const float shotCenterY = GameSpecialShot::GetCenterY();
    float evadeDirection = 1.0f;
    if (enemy.y < shotCenterY)
    {
        evadeDirection = -1.0f;
    }
    else if (std::fabs(enemy.y - shotCenterY) < 0.025f)
    {
        evadeDirection = std::rand() % 2 == 0 ? 1.0f : -1.0f;
    }
    const float evadeDistance = isPhaseTwo ? 0.36f : 0.30f;
    enemy.targetX = ClampFloat(enemy.x + 0.05f, 0.28f, 0.72f);
    enemy.targetY = ClampFloat(enemy.y + evadeDirection * evadeDistance, -0.68f, 0.68f);
    enemy.state = EnemyState::Evade;
    enemy.warningTimer = 8;
    enemy.stateTimer = 32;
    enemy.shotTimer = 0;
    enemy.evadeCooldown = isPhaseTwo ? 105 : 150;
    // the dodge is seen and heard: a mark above the ship (DrawRivalEvadeWarning), a streak from here to the new place and the ship torn into its two
    // colours (DrawRivalFx), a dash sound and a burst of sparks where it was
    enemy.fxKind = 1;
    enemy.fxX = enemy.x;
    enemy.fxY = enemy.y;
    enemy.fxTimer = 36;
    GameAudio::PlayAt(GameSound::ChargerDash, enemy.x);
    GameEffect::SpawnHit(enemy.x, enemy.y, true);
    return true;
}

const Enemy* OtherRival(const Enemy& self)
{
    if (g_EnemyList == nullptr) return nullptr;
    for (int i = 0; i < g_EnemyListCount; ++i)
        if (&g_EnemyList[i] != &self && g_EnemyList[i].active && g_EnemyList[i].type == EnemyType::Rival) return &g_EnemyList[i];
    return nullptr;
}

void SelectRivalRepositionTarget(Enemy& enemy, const Player& player)
{
    constexpr float MIN_X = 0.28f;
    constexpr float MAX_X = 0.68f;

    constexpr float MIN_Y = -0.62f;
    constexpr float MAX_Y = 0.62f;

    const float distanceX = enemy.x - player.x;

    if (enemy.x < 0.10f)
    {
        // coming back from the end of a dash (it stands at the left wall, where the ship is): it leaves on its own side of the ship, never through it
        enemy.targetX = 0.46f;
        enemy.targetY = ClampFloat(enemy.y + (enemy.y >= player.y ? 0.30f : -0.30f), MIN_Y, MAX_Y);
        return;
    }
    if (distanceX < 0.42f)
    {
        enemy.targetX = enemy.x + 0.22f;
    }
    else if (distanceX > 1.05f)
    {
        enemy.targetX = enemy.x - 0.16f;
    }
    else
    {
        enemy.targetX = 0.42f + static_cast<float>(std::rand() % 4) * 0.07f;
    }

    if (enemy.targetX < MIN_X)
    {
        enemy.targetX = MIN_X;
    }

    if (enemy.targetX > MAX_X)
    {
        enemy.targetX = MAX_X;
    }

    const bool isPhaseTwo = IsRivalPhaseTwo(enemy);

    float verticalDistance = 0.28f;

    if (isPhaseTwo)
    {
        verticalDistance = 0.20f;
    }

    // A person sees the energy bar fill up: when the ship can fire its beam any moment, the rival stays out of the lane of the beam (the half height of the beam is 0.10)
    if (GameState::energy * 10 >= GameState::MAX_ENERGY * 9)
    {
        verticalDistance = 0.36f;
    }

    const bool moveAbovePlayer = std::rand() % 2 == 0;

    if (moveAbovePlayer)
    {
        enemy.targetY = player.y + verticalDistance;
    }
    else
    {
        enemy.targetY = player.y - verticalDistance;
    }

    const float randomOffset = static_cast<float>(std::rand() % 5 - 2) * 0.035f;

    enemy.targetY += randomOffset;

    if (enemy.targetY < MIN_Y)
    {
        enemy.targetY = MIN_Y;
    }

    if (enemy.targetY > MAX_Y)
    {
        enemy.targetY = MAX_Y;
    }

    // two rivals keep apart: the second one takes the other side of the first one's height
    if (const Enemy* other = OtherRival(enemy))
    {
        const float oy = (other->state == EnemyState::Reposition) ? other->targetY : other->y;
        if (std::fabs(enemy.targetY - oy) < 0.40f)
        {
            float y = enemy.targetY >= oy ? oy + 0.40f : oy - 0.40f;
            if (y > MAX_Y || y < MIN_Y) y = enemy.targetY >= oy ? oy - 0.40f : oy + 0.40f;
            enemy.targetY = ClampFloat(y, MIN_Y, MAX_Y);
        }
    }
}

// The mini boss (the rival). A duelist that has the kit of every elite the ship has met, and plays like a person:
//   TURRET   FAN       three volleys of a fan: two of one colour, then (after a pause, with the colour of its glow flipped) one of the other: A A B
//   HEAVY    RING      a ring that turns, then an aimed fan of seven (tier 3: a second ring)
//   SNIPER   NEEDLES   white needles at where the ship is GOING (not absorbable); the line follows the ship and locks for the last 12 frames
//   ORBITER  HALF RING it circles round a point and throws half rings at the ship: A A B (tier 3: A A B B)
//   CHARGER  DASH      it coils back, locks a lane, shoots a fan and rams along it; at the end of the lane it is open for a moment, then it returns
// One move is ONE colour (the flip of the glow in the middle of a fan or half ring announces itself): the bullets of a move never arrive in both colours at once. The rival READS
// the ship: it takes the colour the ship does NOT have about half of the time (the ship has to switch), and the colour the ship has the rest (a free absorb). It chooses its
// moves by how far the ship is (it closes in with the dash when the ship is far, backs off to rings when it is close), never repeats a move, keeps out of the lane of the
// beam when the ship's energy is full, dodges a beam that is already firing (and answers with a counter fan), and in the second half it sometimes chains two moves without a pause.
int RivalMoveIndex(EnemyAttackType type)
{
    switch (type)
    {
    case EnemyAttackType::Fan:
        return 0;
    case EnemyAttackType::Ring:
        return 1;
    case EnemyAttackType::Needles:
        return 2;
    case EnemyAttackType::HalfRing:
        return 3;
    case EnemyAttackType::Dash:
        return 4;
    case EnemyAttackType::Mine:
        return 5;
    case EnemyAttackType::Homing:
        return 6;
    default:
        return -1;
    }
}

// The twins have the moves of every elite: the turret (fan, with the flip of the glow, and the shot that follows the ship), the heavy (ring and fan, and the mines),
// the sniper (white needles), the half rings and the charger (the dash).
constexpr int RIVAL_MOVE_COUNT = 7;
constexpr EnemyAttackType RIVAL_MOVES[RIVAL_MOVE_COUNT] = {EnemyAttackType::Fan,      EnemyAttackType::Ring, EnemyAttackType::Needles, EnemyAttackType::HalfRing,
                                                           EnemyAttackType::Dash,     EnemyAttackType::Mine, EnemyAttackType::Homing};

// percent weights of the moves (fan, ring, needles, half ring, dash, mine, homing) for a ship that is far / at a middle distance / close. The two twins have a role: the one
// that was there first (role 0) keeps its distance and fires (the turret, the heavy, the sniper), the one that comes after it (role 1) closes in (the charger, the half rings).
void RivalWeights(const Enemy& enemy, const Player& player, int tier, bool allowDash, int w[RIVAL_MOVE_COUNT])
{
    static const int FAR[RIVAL_MOVE_COUNT] = {22, 10, 28, 10, 30, 8, 14};
    static const int MID[RIVAL_MOVE_COUNT] = {28, 18, 20, 22, 12, 14, 12};
    static const int NEAR[RIVAL_MOVE_COUNT] = {20, 32, 14, 30, 4, 16, 8};
    const float distance = enemy.x - player.x;
    const int* base = distance > 1.05f ? FAR : (distance > 0.65f ? MID : NEAR);
    for (int k = 0; k < RIVAL_MOVE_COUNT; ++k)
    {
        w[k] = base[k];
    }
    if (enemy.rivalRole == 0)
    {
        w[0] += 8;  // fan
        w[2] += 8;  // needles
        w[5] += 10; // mine
        w[6] += 8;  // homing
    }
    else
    {
        w[4] += 14; // dash
        w[3] += 10; // half ring
        w[1] += 8;  // ring
    }
    if (tier >= 2)
    {
        w[4] += 10;
        w[2] += 5;
    }
    if (tier >= 3)
    {
        w[1] += 8;
    }
    if (!allowDash)
    {
        w[4] = 0;
    }
    if (const Enemy* other = OtherRival(enemy)) // one lane at a time
    {
        const bool otherDashes = (other->state == EnemyState::AttackWarning || other->state == EnemyState::Attack) && other->pendingAttackType == EnemyAttackType::Dash;
        if (otherDashes) w[4] = 0;
    }
    const int last = RivalMoveIndex(enemy.pendingAttackType);
    if (last >= 0)
    {
        w[last] = 0; // it never repeats itself
    }
}

// the colour of the next move: the colour that the ship does not have (it has to switch) about half of the time, the colour it has otherwise
Polarity RivalColour(const Player& player, int tier)
{
    const int chanceOpposite = tier >= 3 ? 60 : (tier == 2 ? 50 : 35);
    return std::rand() % 100 < chanceOpposite ? OppositeOf(player.polarity) : player.polarity;
}

void SelectRivalAttack(Enemy& enemy, const Player& player, bool chained = false)
{
    const int tier = RivalTier(enemy);
    int w[RIVAL_MOVE_COUNT];
    RivalWeights(enemy, player, tier, !chained, w);
    int sum = 0;
    for (int k = 0; k < RIVAL_MOVE_COUNT; ++k)
    {
        sum += w[k];
    }
    int roll = std::rand() % (sum > 0 ? sum : 1);
    int pick = 0;
    for (int k = 0; k < RIVAL_MOVE_COUNT; ++k)
    {
        if (roll < w[k])
        {
            pick = k;
            break;
        }
        roll -= w[k];
    }
    const EnemyAttackType type = RIVAL_MOVES[pick];
    enemy.pendingAttackType = type;
    enemy.warningTimer = RivalWarningFrames(type, tier);
    enemy.comboLeft = chained ? 1 : 0;
    enemy.targetX = player.x;
    enemy.targetY = player.y;
    if (type != EnemyAttackType::Needles) // the needles are white: they have no colour to read the ship with
    {
        const Polarity wanted = RivalColour(player, tier);
        if (wanted != enemy.polarity)
        {
            SwitchEnemyPolarity(enemy); // the glow flips, and the ring of the flip is the announcement
        }
    }
    enemy.state = EnemyState::AttackWarning;
}

void SpawnStraightShot(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, float speed, Polarity polarity)
{
    SpawnEnemyBullet(enemyBullets, enemyBulletCount, x, y, PI, speed, polarity);
}

void SpawnFanShot(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, float baseAngle, float spreadAngle,
                  int shotCount, float speed, Polarity polarity)
{
    if (shotCount <= 0)
    {
        return;
    }

    if (shotCount == 1)
    {
        SpawnEnemyBullet(enemyBullets, enemyBulletCount, x, y, baseAngle, speed, polarity);

        return;
    }

    const float startAngle = baseAngle - spreadAngle * 0.5f;

    const float angleStep = spreadAngle / static_cast<float>(shotCount - 1);

    for (int i = 0; i < shotCount; i++)
    {
        const float angle = startAngle + angleStep * static_cast<float>(i);

        SpawnEnemyBullet(enemyBullets, enemyBulletCount, x, y, angle, speed, polarity);
    }
}

void SpawnHalfRingShot(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, int shotCount, float centerAngle,
                       float spreadAngle, float speed, Polarity polarity)
{
    if (shotCount <= 0)
    {
        return;
    }

    if (shotCount == 1)
    {
        SpawnEnemyBullet(enemyBullets, enemyBulletCount, x, y, centerAngle, speed, polarity);

        return;
    }

    const float startAngle = centerAngle - spreadAngle * 0.5f;

    const float angleStep = spreadAngle / static_cast<float>(shotCount - 1);

    for (int i = 0; i < shotCount; i++)
    {
        const float angle = startAngle + angleStep * static_cast<float>(i);

        SpawnEnemyBullet(enemyBullets, enemyBulletCount, x, y, angle, speed, polarity);
    }
}

void SpawnHomingShot(EnemyBullet enemyBullets[], int enemyBulletCount, float x, float y, const Player& player, float speed,
                     Polarity polarity)
{
    const float directionX = player.x - x;

    const float directionY = player.y - y;

    const float angle = std::atan2(directionY, directionX);

    constexpr float TURN_RATE = PI / 120.0f;

    constexpr int HOMING_DURATION = 75;

    SpawnEnemyBullet(enemyBullets, enemyBulletCount, x, y, angle, speed, polarity, true, TURN_RATE, HOMING_DURATION);
}

void UpdateScout(Enemy& enemy, EnemyBullet enemyBullets[], int enemyBulletCount)
{
    enemy.x -= enemy.vx;

    enemy.shotTimer--;

    if (enemy.shotTimer > 0)
    {
        return;
    }

    const float startX = enemy.x - enemy.halfWidth;

    SpawnStraightShot(enemyBullets, enemyBulletCount, startX, enemy.y, 0.0090f, enemy.polarity);

    enemy.attackPatternIndex++;

    enemy.shotTimer = 72 + std::rand() % 36;
}

void UpdateSine(Enemy& enemy, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    enemy.x -= enemy.vx;

    enemy.moveTime += 1.0f / 60.0f;

    enemy.y = enemy.baseY + std::sin(enemy.moveTime * enemy.frequency * PI) * enemy.amplitude;

    if (enemy.state == EnemyState::AttackWarning)
    {
        enemy.warningTimer--;

        if (enemy.warningTimer > 0)
        {
            return;
        }

        const float startX = enemy.x - enemy.halfWidth;

        if (enemy.pendingAttackType == EnemyAttackType::Homing)
        {
            SpawnHomingShot(enemyBullets, enemyBulletCount, startX, enemy.y, player, 0.0075f, enemy.polarity);

            enemy.shotTimer = 95 + std::rand() % 35;
        }
        else
        {
            const float aimedAngle = std::atan2(enemy.targetY - enemy.y, enemy.targetX - startX);


            SpawnFanShot(enemyBullets, enemyBulletCount, startX, enemy.y, aimedAngle, PI * 0.12f, 3, 0.0105f, enemy.polarity);

            enemy.shotTimer = 62 + std::rand() % 28;
        }

        enemy.attackPatternIndex++;

        enemy.state = EnemyState::Attack;

        enemy.stateTimer = 12;
        enemy.warningTimer = 0;

        return;
    }

    if (enemy.state == EnemyState::Attack)
    {
        enemy.stateTimer--;

        if (enemy.stateTimer <= 0)
        {
            enemy.state = EnemyState::Enter;

            enemy.stateTimer = 0;
        }

        return;
    }

    enemy.shotTimer--;

    if (enemy.shotTimer > 0)
    {
        return;
    }

    const bool useHomingShot = enemy.attackPatternIndex % 3 == 2;

    if (useHomingShot)
    {
        enemy.pendingAttackType = EnemyAttackType::Homing;

        enemy.warningTimer = 36;
    }
    else
    {
        enemy.pendingAttackType = EnemyAttackType::Aimed;

        enemy.warningTimer = 24;
    }

    enemy.targetX = player.x;
    enemy.targetY = player.y;

    enemy.state = EnemyState::AttackWarning;

    enemy.stateTimer = 0;
}

void UpdateHeavy(Enemy& enemy, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    switch (enemy.state)
    {
    case EnemyState::Enter:
        enemy.x -= enemy.vx;
        if (enemy.x <= 0.68f)
        {
            enemy.x = 0.68f;
            enemy.state = EnemyState::Wait;
            enemy.stateTimer = 18;
        }
        break;

    case EnemyState::Wait:
        if (--enemy.stateTimer > 0) break;
        if (enemy.elite && enemy.cycle % ELITE_MOVE_EVERY == ELITE_MOVE_EVERY - 1)
        {
            // FORTRESS: the minefield. Four places around the ship are marked; the mines are laid there when the mark is full
            enemy.pendingAttackType = EnemyAttackType::Minefield;
            enemy.warningTimer = MINEFIELD_WARN;
            enemy.targetX = player.x;
            enemy.targetY = player.y;
        }
        else if (enemy.attackPatternIndex % 3 == 2)
        {
            enemy.pendingAttackType = EnemyAttackType::Ring;
            enemy.warningTimer = 34;
        }
        else if (enemy.attackPatternIndex % 3 == 1)
        {
            enemy.pendingAttackType = EnemyAttackType::Mine; // the heavy lays two mines that bracket the ship (the elite: three)
            enemy.warningTimer = 30;
        }
        else
        {
            enemy.pendingAttackType = EnemyAttackType::Fan;
            enemy.warningTimer = 22;
        }
        enemy.state = EnemyState::AttackWarning;
        break;

    case EnemyState::AttackWarning:
        if (--enemy.warningTimer > 0) break;
        enemy.state = EnemyState::Attack;
        enemy.shotTimer = 1;
        break;

    case EnemyState::Attack:
    {
        if (--enemy.shotTimer > 0) break;
        const float startX = enemy.x - enemy.halfWidth;
        const float aimed = std::atan2(player.y - enemy.y, player.x - startX);
        if (enemy.pendingAttackType == EnemyAttackType::Minefield)
        {
            static const float OFFX[4] = {0.24f, 0.0f, -0.20f, 0.0f};
            static const float OFFY[4] = {0.0f, 0.34f, 0.0f, -0.34f};
            for (int k = 0; k < 4; ++k)
            {
                const float mx = ClampFloat(enemy.targetX + OFFX[k], -0.92f, 0.40f);
                const float my = ClampFloat(enemy.targetY + OFFY[k], -0.84f, 0.84f);
                const Polarity mineColour = (k % 2 == 0) ? Polarity::Red : Polarity::Blue; // (the minefield: both colours)
                if (SpawnEnemyBullet(enemyBullets, enemyBulletCount, mx, my, 0.0f, 0.0001f, mineColour) && g_LastBulletIndex >= 0)
                {
                    EnemyBullet& mine = enemyBullets[g_LastBulletIndex];
                    mine.fuse = 70 + 22 * k; // one after the other: the way out opens and closes
                    mine.burst = 6;
                    mine.energyMul = Balance::ABSORB_MUL_MINE;
                    mine.size = MINE_SIZE;
                    mine.halfWidth = 0.026f * MINE_SIZE * 0.85f;
                    mine.halfHeight = 0.026f * MINE_SIZE * 0.85f;
                    GameEffect::SpawnAbsorb(mx, my, 0.08f, enemy.polarity);
                }
            }
            GameAudio::PlayAt(GameSound::EliteHit, enemy.x);
            ++enemy.cycle;
        }
        else if (enemy.pendingAttackType == EnemyAttackType::Ring)
        {
            SpawnAimedRing(enemyBullets, enemyBulletCount, startX, enemy.y, enemy.elite ? 20 : 16, aimed, 0.0078f, enemy.polarity);
            ++enemy.cycle;
        }
        else if (enemy.pendingAttackType == EnemyAttackType::Mine)
        {
            // a mine has the colour that the ship does NOT have when it is laid: to defuse it (absorb it, three times the energy) the ship has to switch
            const Polarity mineColour = OppositeOf(player.polarity);
            SpawnMine(enemyBullets, enemyBulletCount, startX, enemy.y, aimed + 0.26f, mineColour);
            SpawnMine(enemyBullets, enemyBulletCount, startX, enemy.y, aimed - 0.26f, mineColour);
            if (enemy.elite)
            {
                SpawnMine(enemyBullets, enemyBulletCount, startX, enemy.y, aimed, OppositeOf(mineColour)); // (the third one of the elite: the other colour)
                for (int k = 0; k < enemyBulletCount; ++k) // (the mines of the elite burst denser)
                    if (enemyBullets[k].active && enemyBullets[k].fuse == MINE_FUSE) enemyBullets[k].burst = 9;
            }
        }
        else
        {
            SpawnFanShot(enemyBullets, enemyBulletCount, startX, enemy.y, aimed, PI * 0.42f, enemy.elite ? 9 : 7, 0.0090f, enemy.polarity);
        }
        if (enemy.pendingAttackType != EnemyAttackType::Minefield) enemy.attackPatternIndex++;
        enemy.state = EnemyState::Wait;
        enemy.stateTimer = ScaledWait(enemy, 45 + std::rand() % 20);
        enemy.warningTimer = 0;
        break;
    }

    case EnemyState::Exit:
        enemy.x += 0.004f;
        break;

    default:
        break;
    }
}

void UpdateTurret(Enemy& enemy, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    // A cycle is a fan (A, A, then the other colour after a pause) or a half ring (A, A, the glow flips, B); the two alternate. The elite (BASTION) waits
    // shorter, its fans and half rings have two more bullets, and every third cycle is the CROSS NET.
    switch (enemy.state)
    {
    case EnemyState::Enter:
        enemy.x -= enemy.vx;
        if (enemy.x <= 0.68f)
        {
            enemy.x = 0.68f;
            enemy.state = EnemyState::Wait;
            enemy.stateTimer = 30;
            enemy.attackPatternIndex = 0;
        }
        break;

    case EnemyState::Wait:
        if (--enemy.stateTimer > 0) break;
        if (enemy.elite && enemy.cycle % ELITE_MOVE_EVERY == ELITE_MOVE_EVERY - 1)
        {
            enemy.pendingAttackType = EnemyAttackType::CrossNet;
            enemy.warningTimer = CROSSNET_WARN;
            enemy.aux = static_cast<float>(std::rand() % 5) - 2.0f; // where the gap is (in bullets from the line to the ship)
        }
        else
        {
            enemy.pendingAttackType = (enemy.cycle % 2 == 0) ? EnemyAttackType::Fan : EnemyAttackType::HalfRing;
            enemy.warningTimer = 30;
        }
        enemy.attackPatternIndex = 0;
        enemy.state = EnemyState::AttackWarning;
        break;

    case EnemyState::AttackWarning:
        if (--enemy.warningTimer > 0) break;
        enemy.state = EnemyState::Attack;
        enemy.shotTimer = 1;
        break;

    case EnemyState::Attack:
    {
        if (--enemy.shotTimer > 0) break;
        const float startX = enemy.x - enemy.halfWidth;
        const float startY = enemy.y;
        const float aimedAngle = std::atan2(player.y - startY, player.x - startX);
        const int extra = enemy.elite ? 2 : 0;
        const Polarity other = enemy.polarity == Polarity::Red ? Polarity::Blue : Polarity::Red;
        bool done = false;
        if (enemy.pendingAttackType == EnemyAttackType::CrossNet)
        {
            // the cross net: 19 bullets over a wide arc at the ship, the upper half one colour and the lower half the other, two bullets missing (the way through)
            constexpr int COUNT = 19;
            const float spread = PI * 0.62f;
            const int gap = COUNT / 2 + static_cast<int>(enemy.aux);
            for (int k = 0; k < COUNT; ++k)
            {
                if (k == gap || k == gap + 1) continue;
                const float a = aimedAngle - spread * 0.5f + spread * static_cast<float>(k) / static_cast<float>(COUNT - 1);
                SpawnEnemyBullet(enemyBullets, enemyBulletCount, startX, startY, a, 0.0080f, k < COUNT / 2 ? enemy.polarity : other);
            }
            GameAudio::PlayAt(GameSound::BossCannonFire, enemy.x);
            done = true;
        }
        else if (enemy.pendingAttackType == EnemyAttackType::HalfRing)
        {
            SpawnHalfRingShot(enemyBullets, enemyBulletCount, startX, startY, 9 + extra, aimedAngle + static_cast<float>(enemy.attackPatternIndex - 1) * 0.12f, PI * 0.92f, 0.0078f,
                              enemy.attackPatternIndex == 2 ? other : enemy.polarity);
            enemy.attackPatternIndex++;
            done = enemy.attackPatternIndex >= 3;
            if (enemy.attackPatternIndex == 2) enemy.polaritySwitchTimer = 18; // the glow flips: the next half ring has the other colour
            enemy.shotTimer = enemy.attackPatternIndex == 2 ? 44 : 22;
        }
        else
        {
            if (enemy.attackPatternIndex == 0)
                SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, aimedAngle, PI * 0.30f, 5 + extra, 0.0090f, enemy.polarity);
            else if (enemy.attackPatternIndex == 1)
                SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, aimedAngle + 0.10f, PI * 0.40f, 7 + extra, 0.0087f, enemy.polarity);
            else
                SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, aimedAngle - 0.10f, PI * 0.40f, 7 + extra, 0.0087f, other);
            enemy.attackPatternIndex++;
            done = enemy.attackPatternIndex >= 3;
            enemy.shotTimer = enemy.attackPatternIndex == 2 ? 46 : 18; // the volley of the other colour comes 46 steps after the last one of the first colour: time to switch
        }
        if (done)
        {
            enemy.polarity = other;
            enemy.attackPatternIndex = 0;
            ++enemy.cycle;
            enemy.state = EnemyState::Wait;
            enemy.stateTimer = ScaledWait(enemy, 65 + std::rand() % 21);
            enemy.shotTimer = 0;
            enemy.warningTimer = 0;
        }
        break;
    }

    case EnemyState::Exit:
        enemy.x += 0.004f;
        break;

    default:
        break;
    }
}

void UpdateSniper(Enemy& enemy, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    // The elite (FALCON) moves 40 % faster, fires five needles, and every third cycle the RAIL: a white laser along the line it locked.
    switch (enemy.state)
    {
    case EnemyState::Enter:
        enemy.x -= enemy.vx;
        if (enemy.x <= 0.72f)
        {
            enemy.x = 0.72f;
            enemy.state = EnemyState::Wait;
            enemy.stateTimer = 24;
            enemy.attackPatternIndex = 0;
        }
        break;

    case EnemyState::Wait:
        if (--enemy.stateTimer > 0) break;
        enemy.targetX = player.x;
        enemy.targetY = player.y;
        if (enemy.elite && enemy.cycle % ELITE_MOVE_EVERY == ELITE_MOVE_EVERY - 1)
        {
            enemy.pendingAttackType = EnemyAttackType::Rail;
            StartRail(enemy.x - enemy.halfWidth, enemy.y, player.x, player.y);
            enemy.warningTimer = RAIL_WARN + RAIL_FIRE;
        }
        else
        {
            enemy.pendingAttackType = EnemyAttackType::Aimed;
            enemy.warningTimer = 40;
        }
        enemy.attackPatternIndex = 0;
        enemy.state = EnemyState::AttackWarning;
        break;

    case EnemyState::AttackWarning:
        if (--enemy.warningTimer > 0) break;
        enemy.state = EnemyState::Attack;
        enemy.shotTimer = 1;
        break;

    case EnemyState::Attack:
    {
        if (--enemy.shotTimer > 0) break;
        const int shots = enemy.pendingAttackType == EnemyAttackType::Rail ? 0 : (enemy.elite ? 5 : 3);
        if (enemy.attackPatternIndex < shots)
        {
            static const float OFFSET[5] = {0.0f, 0.075f, -0.075f, 0.15f, -0.15f};
            const float startX = enemy.x - enemy.halfWidth;
            const float targetY = enemy.targetY + OFFSET[enemy.attackPatternIndex];
            const float shotAngle = std::atan2(targetY - enemy.y, enemy.targetX - startX);
            SpawnEnemyBullet(enemyBullets, enemyBulletCount, startX, enemy.y, shotAngle, 0.020f, enemy.polarity);
            enemy.attackPatternIndex++;
            enemy.shotTimer = 8;
            if (enemy.attackPatternIndex < shots) break;
        }
        ++enemy.cycle;
        enemy.attackPatternIndex = 0;
        {
            const int positionIndex = std::rand() % 5;
            constexpr float REPOSITION_Y[5] = {0.58f, 0.30f, 0.00f, -0.30f, -0.58f};
            enemy.targetX = 0.58f + static_cast<float>(std::rand() % 3) * 0.08f;
            enemy.targetY = REPOSITION_Y[positionIndex];
            for (int k = 0; k < 5; k++) // the first height (from the random one on) that no other elite occupies
            {
                const float y = REPOSITION_Y[(positionIndex + k) % 5];
                if (EliteClearance(y, enemy.halfHeight, &enemy) >= 0.0f)
                {
                    enemy.targetY = y;
                    break;
                }
            }
        }
        enemy.state = EnemyState::Reposition;
        enemy.stateTimer = 90;
        enemy.shotTimer = 0;
        enemy.warningTimer = 0;
        break;
    }

    case EnemyState::Reposition:
    {
        const float k = enemy.elite ? 1.4f : 1.0f;
        enemy.x = MoveTowards(enemy.x, enemy.targetX, 0.010f * k);
        enemy.y = MoveTowards(enemy.y, enemy.targetY, 0.013f * k);
        enemy.stateTimer--;
        const bool reachedTarget = std::fabs(enemy.targetX - enemy.x) <= 0.005f && std::fabs(enemy.targetY - enemy.y) <= 0.005f;
        if (reachedTarget || enemy.stateTimer <= 0)
        {
            enemy.x = enemy.targetX;
            enemy.y = enemy.targetY;
            enemy.state = EnemyState::Wait;
            enemy.stateTimer = ScaledWait(enemy, 28 + std::rand() % 18);
        }
        break;
    }

    case EnemyState::Exit:
        enemy.x += 0.006f;
        break;

    default:
        break;
    }
}

// The twin: a rival of the other colour flies in from the right, on the other side of the screen. It is a rival of its own (its own life and its own moves).
// Both have 72 % of the life that a lone rival had; when one of the two is down, the other one fights as it does in its second half.
void SpawnRivalWingman(const Enemy& primary)
{
    g_RivalWingmanOut = true;
    g_NextRivalRole = 1;
    const Polarity pol = primary.polarity == Polarity::Red ? Polarity::Blue : Polarity::Red;
    const float y = ClampFloat(primary.y >= 0.0f ? -0.45f : 0.45f, -0.62f, 0.62f);
    if (!GameEnemy::Spawn(g_EnemyList, g_EnemyListCount, EnemyType::Rival, pol, y, primary.groupId)) g_NextRivalRole = 0;
    Mod::LogEvent("rival_twin", "");
    GameAudio::PlayAt(GameSound::RivalTwin, 0.6f); // (a low horn and a glass note: it used to be the whine of the boss cannon, 3 kHz and rising)
}

void UpdateRival(Enemy& enemy, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    enemy.moveTime += 1.0f / 60.0f;
    if (enemy.polaritySwitchTimer > 0)
    {
        enemy.polaritySwitchTimer--;
    }
    if (enemy.evadeCooldown > 0)
    {
        enemy.evadeCooldown--;
    }
    if (enemy.fxTimer > 0)
    {
        enemy.fxTimer--;
    }
    else
    {
        enemy.fxKind = 0;
    }
    // two rivals do not fly through each other: while they move about (not during a dash) the one pushes the other away
    if (enemy.state == EnemyState::Enter || enemy.state == EnemyState::Observe || enemy.state == EnemyState::Reposition || enemy.state == EnemyState::Wait || enemy.state == EnemyState::Evade)
    {
        if (const Enemy* other = OtherRival(enemy))
        {
            const float dx = enemy.x - other->x;
            const float dy = enemy.y - other->y;
            if (std::fabs(dx) < (enemy.halfWidth + other->halfWidth) * 1.7f && std::fabs(dy) < (enemy.halfHeight + other->halfHeight) * 1.7f)
            {
                // (the one that is charging stands still: the other one gives way)
                const bool otherCharges = other->state == EnemyState::AttackWarning || other->state == EnemyState::Attack;
                enemy.y = ClampFloat(enemy.y + (dy >= 0.0f ? 0.020f : -0.020f) * (otherCharges ? 1.5f : 1.0f), -0.80f, 0.80f);
                enemy.x = ClampFloat(enemy.x + (dx >= 0.0f ? 0.006f : -0.006f), -0.9f, 1.2f);
            }
        }
    }
    if (!GameSpecialShot::IsActive())
    {
        enemy.specialShotReactionUsed = false;
    }
    // where the ship is going (the needles are aimed ahead of it)
    if (!g_RivalSeen)
    {
        g_RivalSeen = true;
        g_RivalPrevPX = player.x;
        g_RivalPrevPY = player.y;
        g_RivalVelX = g_RivalVelY = 0.0f;
    }
    if (g_RivalFrame != g_UpdateFrame) // (the ship is measured once per step, not once per rival)
    {
        g_RivalFrame = g_UpdateFrame;
        g_RivalVelX = g_RivalVelX * 0.7f + (player.x - g_RivalPrevPX) * 0.3f;
        g_RivalVelY = g_RivalVelY * 0.7f + (player.y - g_RivalPrevPY) * 0.3f;
        g_RivalPrevPX = player.x;
        g_RivalPrevPY = player.y;
    }
    // the twin comes in a moment after the first one has taken its place (the duel is always against the two of them)
    if (g_TwinEnabled && enemy.rivalRole == 0 && !g_RivalWingmanOut && g_RivalsAlive == 1 && enemy.age > 40) SpawnRivalWingman(enemy);
    const int tier = RivalTier(enemy);
    // The second half and the last quarter of the duel begin with a flash: the rival stops for a moment, two shock rings run out of it, the armour cracks (DrawRivalFx).
    if ((tier >= 2 && !enemy.phaseTwoShown) || (tier >= 3 && !enemy.phaseThreeShown))
    {
        if (tier >= 2) enemy.phaseTwoShown = true;
        if (tier >= 3) enemy.phaseThreeShown = true;
        enemy.fxKind = 3;
        enemy.fxTimer = 44;
        enemy.state = EnemyState::Observe;
        enemy.decisionTimer = 44;
        enemy.warningTimer = 0;
        GameEffect::SpawnAbsorb(enemy.x, enemy.y, 0.20f, enemy.polarity);
        GameEffect::SpawnExplosion(enemy.x, enemy.y, 0.09f);
        GameAudio::PlayAt(GameSound::BossBerserk, enemy.x);
        ScreenShake::Start(14, 0.012f);
        return;
    }
    const bool canReactToSpecialShot =
        enemy.state == EnemyState::Observe || enemy.state == EnemyState::Reposition || enemy.state == EnemyState::AttackWarning;
    if (canReactToSpecialShot)
    {
        const bool startedEvade = TryBeginRivalEvade(enemy);
        if (startedEvade)
        {
            return;
        }
    }
    const bool two = tier >= 2;
    switch (enemy.state)
    {
    case EnemyState::Enter:
        enemy.x = MoveTowards(enemy.x, 0.58f, 0.0090f);
        enemy.y = MoveTowards(enemy.y, 0.0f, 0.0090f);
        if (std::fabs(enemy.x - 0.58f) <= 0.005f)
        {
            enemy.x = 0.58f;
            enemy.state = EnemyState::Observe;
            enemy.decisionTimer = 16 + std::rand() % 10;
        }
        break;
    case EnemyState::Observe:
        enemy.decisionTimer--;
        if (enemy.decisionTimer > 0)
        {
            break;
        }
        SelectRivalRepositionTarget(enemy, player);
        enemy.state = EnemyState::Reposition;
        enemy.stateTimer = 75;
        break;
    case EnemyState::Reposition:
    {
        // (after a dash the rival is far to the left: it flies back fast)
        const float moveSpeedX = enemy.x < 0.10f ? 0.022f : (tier >= 3 ? 0.0125f : (two ? 0.0105f : 0.0080f));
        const float moveSpeedY = tier >= 3 ? 0.0155f : (two ? 0.0130f : 0.0100f);
        enemy.x = MoveTowards(enemy.x, enemy.targetX, moveSpeedX);
        enemy.y = MoveTowards(enemy.y, enemy.targetY, moveSpeedY);
        enemy.stateTimer--;
        const bool reachedX = std::fabs(enemy.targetX - enemy.x) <= 0.006f;
        const bool reachedY = std::fabs(enemy.targetY - enemy.y) <= 0.006f;
        if ((reachedX && reachedY) || enemy.stateTimer <= 0)
        {
            enemy.x = enemy.targetX;
            enemy.y = enemy.targetY;
            SelectRivalAttack(enemy, player);
        }
        break;
    }
    case EnemyState::Evade:
    {
        if (enemy.warningTimer > 0)
        {
            enemy.warningTimer--;
            break;
        }
        const float evadeSpeedX = two ? 0.0140f : 0.0115f;
        const float evadeSpeedY = two ? 0.0240f : 0.0200f;
        enemy.x = MoveTowards(enemy.x, enemy.targetX, evadeSpeedX);
        enemy.y = MoveTowards(enemy.y, enemy.targetY, evadeSpeedY);
        enemy.stateTimer--;
        const bool reachedX = std::fabs(enemy.targetX - enemy.x) <= 0.006f;
        const bool reachedY = std::fabs(enemy.targetY - enemy.y) <= 0.006f;
        if ((reachedX && reachedY) || enemy.stateTimer <= 0)
        {
            enemy.x = enemy.targetX;
            enemy.y = enemy.targetY;
            // It lands with a flash and answers at once: a quick fan at where the ship is NOW, in a colour chosen by reading the ship. A dodged beam has a price.
            const Polarity wanted = RivalColour(player, tier);
            if (wanted != enemy.polarity)
            {
                SwitchEnemyPolarity(enemy);
            }
            GameEffect::SpawnAbsorb(enemy.x, enemy.y, 0.16f, enemy.polarity);
            GameAudio::PlayPolaritySwitch(enemy.polarity == Polarity::Red, enemy.x); // (the rival switches too: the same two voices)
            enemy.fxKind = 2;
            enemy.fxTimer = 16;
            enemy.pendingAttackType = EnemyAttackType::Counter;
            enemy.comboLeft = 0;
            enemy.targetX = player.x;
            enemy.targetY = player.y;
            enemy.warningTimer = RivalWarningFrames(EnemyAttackType::Counter, tier);
            enemy.state = EnemyState::AttackWarning;
            enemy.stateTimer = 0;
        }
        break;
    }
    case EnemyState::Wait: // the end of a dash: it stands where the lane ended, open to shots, then it goes back
        // ... and if it ended on top of the ship (the ship did not leave the lane) it glides away from it, so that one ram is one hit, not a hit every 84 frames
        if (std::fabs(enemy.x - player.x) < 0.30f && std::fabs(enemy.y - player.y) < 0.30f)
        {
            enemy.y = ClampFloat(enemy.y + (enemy.y >= player.y ? 0.011f : -0.011f), -0.80f, 0.80f);
            enemy.x += 0.004f;
        }
        enemy.stateTimer--;
        if (enemy.stateTimer <= 0)
        {
            enemy.state = EnemyState::Observe;
            enemy.decisionTimer = 4;
        }
        break;
    case EnemyState::AttackWarning:
        if (enemy.pendingAttackType == EnemyAttackType::Needles && enemy.warningTimer > NEEDLE_LOCK_FRAMES)
        {
            // the line follows the ship and aims ahead of it; the last 12 frames it is locked (what the telegraph shows is where the needles go)
            const float lead = 22.0f;
            enemy.targetX = ClampFloat(player.x + g_RivalVelX * lead, -0.92f, 0.10f);
            enemy.targetY = ClampFloat(player.y + g_RivalVelY * lead, -0.80f, 0.80f);
        }
        else if (enemy.pendingAttackType == EnemyAttackType::Dash)
        {
            // the lane follows the ship until 14 frames before the dash, then it is locked; meanwhile the rival coils back (the charger kit)
            if (enemy.warningTimer > 14)
            {
                enemy.targetX = ClampFloat(player.x, -0.92f, 0.30f);
                enemy.targetY = ClampFloat(player.y, -0.78f, 0.78f);
            }
            enemy.x = (std::min)(enemy.x + 0.0014f, 0.76f);
        }
        enemy.warningTimer--;
        if (enemy.warningTimer > 0)
        {
            break;
        }
        enemy.state = EnemyState::Attack;
        enemy.shotTimer = 1;
        enemy.attackPatternIndex = 0;
        if (enemy.pendingAttackType == EnemyAttackType::HalfRing)
        {
            // the orbit: it circles round a point that is one radius to its left (so it starts where it stands), the direction follows its colour
            enemy.targetX = enemy.x - RIVAL_ORBIT_X;
            enemy.targetY = enemy.y;
            enemy.aux = 0.0f;
            enemy.vy = enemy.polarity == Polarity::Red ? 1.0f : -1.0f;
        }
        else if (enemy.pendingAttackType == EnemyAttackType::Dash)
        {
            // the lane: from here to the locked place, and 0.10 beyond it; the rival never leaves the play field
            const float startX = enemy.x - enemy.halfWidth;
            const float dx = enemy.targetX - startX;
            const float dy = (enemy.targetY - enemy.y) * ASPECT;
            const float length = std::sqrt(dx * dx + dy * dy);
            const float speed = tier >= 3 ? 0.034f : (two ? 0.030f : 0.026f);
            if (length > 0.02f)
            {
                enemy.vx = dx / length * speed;
                enemy.vy = dy / length * speed / ASPECT;
            }
            else
            {
                enemy.vx = -speed;
                enemy.vy = 0.0f;
            }
            enemy.aux = length + 0.10f;
            const float aimed = std::atan2(enemy.targetY - enemy.y, enemy.targetX - startX);
            SpawnFanShot(enemyBullets, enemyBulletCount, startX, enemy.y, aimed, PI * 0.16f, two ? 5 : 3, 0.0090f, enemy.polarity);
            GameAudio::PlayAt(GameSound::ChargerDash, enemy.x);
            enemy.fxKind = 1;
            enemy.fxX = enemy.x;
            enemy.fxY = enemy.y;
            enemy.fxTimer = 36;
        }
        break;
    case EnemyState::Attack:
    {
        const EnemyAttackType move = enemy.pendingAttackType;
        if (move == EnemyAttackType::Dash)
        {
            enemy.x += enemy.vx;
            enemy.y += enemy.vy;
            enemy.aux -= std::sqrt(enemy.vx * enemy.vx + enemy.vy * enemy.vy * ASPECT * ASPECT);
            enemy.x = ClampFloat(enemy.x, -0.84f, 0.90f);
            enemy.y = ClampFloat(enemy.y, -0.80f, 0.80f);
            const bool wall = enemy.x <= -0.84f || std::fabs(enemy.y) >= 0.80f;
            if (enemy.aux <= 0.0f || wall)
            {
                enemy.vx = enemy.vy = 0.0f;
                enemy.attackCount++;
                enemy.attackPatternIndex = 0;
                enemy.state = EnemyState::Wait;
                enemy.stateTimer = tier >= 3 ? 22 : (two ? 30 : 40);
                GameEffect::SpawnAbsorb(enemy.x, enemy.y, 0.14f, enemy.polarity);
            }
            break;
        }
        if (move == EnemyAttackType::HalfRing)
        {
            enemy.aux += 0.060f * enemy.vy;
            enemy.x = ClampFloat(enemy.targetX + std::cos(enemy.aux) * RIVAL_ORBIT_X, 0.30f, 0.72f);
            enemy.y = ClampFloat(enemy.targetY + std::sin(enemy.aux) * RIVAL_ORBIT_Y, -0.66f, 0.66f);
        }
        enemy.shotTimer--;
        if (enemy.shotTimer > 0)
        {
            break;
        }
        const float startX = enemy.x - enemy.halfWidth;
        const float startY = enemy.y;
        const int index = enemy.attackPatternIndex;
        const float aimed = std::atan2(player.y - startY, player.x - startX);
        bool more = false;
        int nextTimer = 0;
        switch (move)
        {
        case EnemyAttackType::Fan:
        {
            // the turret kit: A, A, then the glow flips and one of B after a pause. Every volley is aimed at where the ship is NOW.
            const int count = tier >= 3 ? 9 : (two ? 7 : 5);
            const float speed = tier >= 3 ? 0.0096f : 0.0090f;
            if (index == 0)
            {
                SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, aimed, PI * 0.30f, count, speed, enemy.polarity);
                nextTimer = 18;
            }
            else if (index == 1)
            {
                SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, aimed + 0.10f, PI * 0.40f, count + 2, speed * 0.97f, enemy.polarity);
                SwitchEnemyPolarity(enemy); // the tell: the glow flips to the colour of the next volley
                nextTimer = tier >= 3 ? 38 : (two ? 42 : 46);
            }
            else
            {
                SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, aimed - 0.10f, PI * 0.40f, count + 2, speed * 0.97f, enemy.polarity);
            }
            enemy.attackPatternIndex++;
            more = enemy.attackPatternIndex < 3;
            break;
        }
        case EnemyAttackType::Ring:
        {
            // the heavy kit: a ring that turns, then an aimed fan of seven; the last quarter adds a second ring that fills the gaps of the first. One colour all through.
            if (index == 0 || index == 2)
            {
                const int count = two ? 18 : 16;
                const float halfStep = AIMED_RING_ARC / static_cast<float>(count - 1) * 0.5f; // the second ring fills the gaps of the first one
                SpawnAimedRing(enemyBullets, enemyBulletCount, startX, startY, count, aimed + (index == 2 ? halfStep : 0.0f), 0.0078f, enemy.polarity);
                nextTimer = tier >= 3 ? 24 : (two ? 28 : 34);
            }
            else
            {
                SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, aimed, PI * 0.42f, 7, 0.0090f, enemy.polarity);
                nextTimer = 22;
            }
            enemy.attackPatternIndex++;
            more = enemy.attackPatternIndex < (tier >= 3 ? 3 : 2);
            break;
        }
        case EnemyAttackType::HalfRing:
        {
            // the half-ring kit: half rings at the ship while it circles: A, A, B (tier 3: A, A, B, B); the glow flips before the third
            const int waves = tier >= 3 ? 4 : 3;
            const int bullets = tier >= 3 ? 11 : 9;
            SpawnHalfRingShot(enemyBullets, enemyBulletCount, startX, startY, bullets, aimed + static_cast<float>(index - 1) * 0.12f, PI * 0.92f, 0.0078f, enemy.polarity);
            if (index == 1)
            {
                SwitchEnemyPolarity(enemy);
            }
            enemy.attackPatternIndex++;
            more = enemy.attackPatternIndex < waves;
            nextTimer = two ? 18 : 22;
            break;
        }
        case EnemyAttackType::Mine:
        {
            // the heavy kit: two mines that bracket the ship (they burst into an arc towards it when the fuse is out; absorbed, they give three times the energy)
            SpawnMine(enemyBullets, enemyBulletCount, startX, startY, aimed + 0.26f, OppositeOf(player.polarity));
            SpawnMine(enemyBullets, enemyBulletCount, startX, startY, aimed - 0.26f, OppositeOf(player.polarity));
            if (tier >= 3) SpawnMine(enemyBullets, enemyBulletCount, startX, startY, aimed + PI, player.polarity); // (a third one that drifts past the ship from behind it)
            break;
        }
        case EnemyAttackType::Homing:
        {
            // the turret kit: shots that follow the ship (a short while); absorbed, they give twice the energy
            const int shots = tier >= 3 ? 3 : 2;
            SpawnHomingShot(enemyBullets, enemyBulletCount, startX, startY, player, 0.0078f, enemy.polarity);
            enemy.attackPatternIndex++;
            more = enemy.attackPatternIndex < shots;
            nextTimer = 20;
            break;
        }
        case EnemyAttackType::Needles:
        {
            // the sniper kit: white, fast, at the place that the telegraph locked on; each one a little off the one before (the ship cannot just step aside by a hair)
            const int shots = two ? 5 : 3;
            static const float OFFSET[5] = {0.0f, 0.08f, -0.08f, 0.16f, -0.16f};
            const float angle = std::atan2(enemy.targetY + OFFSET[index % 5] - startY, enemy.targetX - startX);
            SpawnEnemyBullet(enemyBullets, enemyBulletCount, startX, startY, angle, 0.0200f, enemy.polarity, false, 0.0f, 0, true);
            enemy.attackPatternIndex++;
            more = enemy.attackPatternIndex < shots;
            nextTimer = tier >= 3 ? 6 : 8;
            break;
        }
        case EnemyAttackType::Counter:
            SpawnFanShot(enemyBullets, enemyBulletCount, startX, startY, std::atan2(enemy.targetY - startY, enemy.targetX - startX), PI * 0.30f, two ? 9 : 7, 0.0112f, enemy.polarity);
            break;
        default:
            break;
        }
        if (more)
        {
            enemy.shotTimer = nextTimer;
            break;
        }
        enemy.attackCount++;
        enemy.attackPatternIndex = 0;
        enemy.vy = 0.0f;
        // In the second half it sometimes chains a second move without a pause (never twice in a row, never a dash): the warning of the second move is still whole.
        const bool wasChained = enemy.comboLeft > 0;
        enemy.comboLeft = 0;
        if (!wasChained && tier >= 2 && move != EnemyAttackType::Counter && std::rand() % 100 < (tier >= 3 ? 40 : 25))
        {
            SelectRivalAttack(enemy, player, true);
            enemy.shotTimer = 0;
            break;
        }
        enemy.state = EnemyState::Observe;
        enemy.decisionTimer = tier >= 3 ? 4 + std::rand() % 4 : (two ? 6 + std::rand() % 5 : 9 + std::rand() % 6); // (the duel was too calm: the pauses are shorter again)
        enemy.shotTimer = 0;
        enemy.warningTimer = 0;
        break;
    }
    case EnemyState::Exit:
        enemy.x += 0.0090f;
        break;
    default:
        break;
    }
}

void UpdateCharger(Enemy& enemy, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    // A charger locks a lane and dashes along it, leaving a trail of slow bullets of its colour; a plain one leaves the screen, the elite (HOUND) stays: it dashes
    // 30 % faster, stops after a dash, and every second time it dashes three times in a row (the ZIGZAG), each lane shown before it.
    const float speed = enemy.elite ? 0.0325f : 0.025f;
    switch (enemy.state)
    {
    case EnemyState::Enter:
        enemy.x -= enemy.vx;
        if (enemy.x <= 0.75f)
        {
            enemy.x = 0.75f;
            enemy.state = EnemyState::ChargeWarning;
            enemy.stateTimer = 45;
            enemy.targetX = player.x;
            enemy.targetY = player.y;
            enemy.dashes = 1;
        }
        break;

    case EnemyState::ChargeWarning:
        if (enemy.stateTimer > 14)
        {
            enemy.targetX = player.x; // the lane follows the ship, then it is locked for the last 14 steps
            enemy.targetY = player.y;
        }
        if (--enemy.stateTimer <= 0)
        {
            const float aimedAngle = std::atan2(enemy.targetY - enemy.y, enemy.targetX - enemy.x);
            SpawnFanShot(enemyBullets, enemyBulletCount, enemy.x - enemy.halfWidth, enemy.y, aimedAngle, PI * 0.16f, 3, 0.0085f, enemy.polarity);
            const float directionX = enemy.targetX - enemy.x;
            const float directionY = enemy.targetY - enemy.y;
            const float length = std::sqrt(directionX * directionX + directionY * directionY);
            if (length > 0.0001f)
            {
                enemy.vx = directionX / length * speed;
                enemy.vy = directionY / length * speed;
            }
            else
            {
                enemy.vx = -speed;
                enemy.vy = 0.0f;
            }
            enemy.aux = length + 0.25f; // (the elite stops a little beyond the place it locked)
            enemy.state = EnemyState::Charging;
            enemy.shotTimer = 0;
        }
        break;

    case EnemyState::Charging:
    {
        enemy.x += enemy.vx;
        enemy.y += enemy.vy;
        // the trail: slow bullets of its colour drift off up and down from the lane
        if (++enemy.shotTimer % (enemy.elite ? 8 : 14) == 0)
        {
            SpawnEnemyBullet(enemyBullets, enemyBulletCount, enemy.x, enemy.y, PI * 0.5f, 0.0035f, enemy.polarity);
            SpawnEnemyBullet(enemyBullets, enemyBulletCount, enemy.x, enemy.y, -PI * 0.5f, 0.0035f, enemy.polarity);
        }
        if (!enemy.elite) break;
        enemy.aux -= std::sqrt(enemy.vx * enemy.vx + enemy.vy * enemy.vy);
        const bool wall = enemy.x < -0.86f || enemy.x > 0.86f || std::fabs(enemy.y) > 0.80f;
        if (enemy.aux > 0.0f && !wall) break;
        enemy.x = ClampFloat(enemy.x, -0.86f, 0.86f);
        enemy.y = ClampFloat(enemy.y, -0.80f, 0.80f);
        enemy.vx = enemy.vy = 0.0f;
        if (--enemy.dashes > 0)
        {
            // the next dash of the zigzag: a short warning, the lane shown
            enemy.state = EnemyState::ChargeWarning;
            enemy.stateTimer = 24;
            enemy.targetX = player.x;
            enemy.targetY = player.y;
            break;
        }
        ++enemy.cycle;
        enemy.state = EnemyState::Wait;
        enemy.stateTimer = ScaledWait(enemy, 70 + std::rand() % 20);
        break;
    }

    case EnemyState::Wait:
        // the elite between two runs: it drifts back to the right, then locks the next lane
        enemy.x = MoveTowards(enemy.x, 0.70f, 0.008f);
        if (--enemy.stateTimer > 0) break;
        enemy.dashes = (enemy.cycle % 2 == 1) ? 3 : 1;
        enemy.state = EnemyState::ChargeWarning;
        enemy.stateTimer = 40;
        enemy.targetX = player.x;
        enemy.targetY = player.y;
        break;

    default:
        break;
    }
}

void UpdateEnemyBulletArray(EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    for (int i = 0; i < enemyBulletCount; i++)
    {
        EnemyBullet& bullet = enemyBullets[i];

        if (!bullet.active)
        {
            continue;
        }

        if (bullet.isHoming && bullet.homingTimer > 0)
        {
            const float targetAngle = std::atan2(player.y - bullet.y, player.x - bullet.x);

            float angleDifference = NormalizeAngle(targetAngle - bullet.angle);

            if (angleDifference > bullet.turnRate)
            {
                angleDifference = bullet.turnRate;
            }
            else if (angleDifference < -bullet.turnRate)
            {
                angleDifference = -bullet.turnRate;
            }

            bullet.angle = NormalizeAngle(bullet.angle + angleDifference);

            bullet.velocityX = std::cos(bullet.angle) * bullet.speed;

            bullet.velocityY = std::sin(bullet.angle) * bullet.speed;

            bullet.homingTimer--;

            if (bullet.homingTimer <= 0)
            {
                bullet.homingTimer = 0;
                bullet.isHoming = false;
            }
        }

        if (bullet.fuse > 0)
        {
            bullet.speed *= MINE_DRAG;
            bullet.velocityX = std::cos(bullet.angle) * bullet.speed;
            bullet.velocityY = std::sin(bullet.angle) * bullet.speed;
            if (--bullet.fuse == 0)
            {
                const float mx = bullet.x;
                const float my = bullet.y;
                const Polarity mp = bullet.polarity;
                bullet.active = false;
                g_ShooterType = static_cast<int>(EnemyType::Heavy);
                // the burst has both colours, one bullet after the other: half of it can be absorbed, the other half has to be dodged
                {
                    const int count = bullet.burst > 0 ? bullet.burst : MINE_RING;
                    const float aim = std::atan2(player.y - my, player.x - mx);
                    const float spread = PI * 1.10f;
                    const Polarity other = mp == Polarity::Red ? Polarity::Blue : Polarity::Red;
                    for (int k = 0; k < count; ++k)
                    {
                        const float a = aim - spread * 0.5f + spread * static_cast<float>(k) / static_cast<float>(count > 1 ? count - 1 : 1);
                        SpawnEnemyBullet(enemyBullets, enemyBulletCount, mx, my, a, MINE_RING_SPEED, k % 2 == 0 ? mp : other);
                    }
                }
                GameEffect::SpawnAbsorb(mx, my, 0.09f, mp);
                GameAudio::PlayAt(GameSound::EliteHit, mx);
                continue;
            }
        }

        bullet.x += bullet.velocityX;

        bullet.y += bullet.velocityY;

        if (bullet.x < -1.25f || bullet.x > 1.25f || bullet.y < -1.25f || bullet.y > 1.25f)
        {
            bullet.active = false;
        }
    }
}
}

using namespace EnemyInternal;

void GameEnemy::Initialize(Enemy enemies[], int enemyCount, EnemyBullet enemyBullets[], int enemyBulletCount)
{
    EnemyArt::Reset();
    g_RivalSeen = false;
    for (auto& v : g_MuzzleVisuals)
        v = {};

    for (int i = 0; i < enemyCount; i++)
    {
        enemies[i] = {};
        enemies[i].active = false;
    }

    for (int i = 0; i < enemyBulletCount; i++)
    {
        enemyBullets[i] = {};
        enemyBullets[i].active = false;
    }
}

bool GameEnemy::Spawn(Enemy enemies[], int enemyCount, EnemyType type, Polarity polarity, float spawnY, int groupId)
{
    for (int i = 0; i < enemyCount; i++)
    {
        if (enemies[i].active)
        {
            continue;
        }

        InitializeEnemyByType(enemies[i], type);
        if (g_NextElite) MakeElite(enemies[i]);
        g_NextElite = false;
        if (type == EnemyType::Rival) g_RivalSeen = false;

        enemies[i].polarity = polarity;
        if (type == EnemyType::Rival)
        {
            enemies[i].rivalRole = g_NextRivalRole;
            enemies[i].maxHp = (enemies[i].maxHp * RIVAL_LIFE_PERCENT) / 100;
            enemies[i].hp = enemies[i].maxHp;
            if (g_NextRivalRole == 0)
            {
                g_RivalWingmanOut = false; // a new duel
            }
            g_NextRivalRole = 0;
        }

        float y = spawnY;
        if (IsEliteEnemy(type))
        {
            g_EnemyList = enemies; // (enemies[i] is still inactive: it does not count itself)
            g_EnemyListCount = enemyCount;
            y = PickEliteSpawnY(type, spawnY, enemies[i].halfHeight);
        }

        enemies[i].y = y;
        enemies[i].baseY = y;
        enemies[i].groupId = groupId;
        enemies[i].prevX = enemies[i].x; // (render interpolation: a new enemy does not slide in from the origin)
        enemies[i].prevY = enemies[i].y;

        const int shotDelayOffset = (i % 4) * 12;

        enemies[i].shotTimer += shotDelayOffset;

        enemies[i].active = true;

        return true;
    }

    return false;
}

bool GameEnemy::SpawnElite(Enemy enemies[], int enemyCount, EnemyType type, Polarity polarity, float spawnY, int groupId)
{
    g_NextElite = true;
    const bool ok = Spawn(enemies, enemyCount, type, polarity, spawnY, groupId);
    g_NextElite = false;
    return ok;
}

bool GameEnemy::RailHits(const Player& player, float hurtHalfHeight)
{
    for (const Rail& r : g_Rails)
        if (r.active && r.state == 2 && RailDistance(r, player.x, player.y) < RAIL_HALF + hurtHalfHeight * ASPECT) return true;
    return false;
}

int GameEnemy::GetRailsForSim(float* x0, float* y0, float* angle, int* framesUntil, int capacity)
{
    int n = 0;
    for (const Rail& r : g_Rails)
    {
        if (!r.active || n >= capacity) continue;
        x0[n] = r.x0;
        y0[n] = r.y0;
        angle[n] = r.angle;
        framesUntil[n] = r.state == 1 ? RAIL_WARN - r.t : 0;
        ++n;
    }
    return n;
}

void GameEnemy::SetTwinEnabled(bool enabled)
{
    g_TwinEnabled = enabled;
}

const char* GameEnemy::OwnerName(int owner)
{
    static const char* const NAMES[7] = {"scout", "sine", "heavy", "charger", "turret", "sniper", "rival"};
    return (owner >= 0 && owner < 7) ? NAMES[owner] : "enemy";
}

// First thing of every world step: remember where everything is, so that the drawing can blend from there to the new position.
void GameEnemy::BeginStep(Enemy enemies[], int enemyCount, EnemyBullet enemyBullets[], int enemyBulletCount)
{
    for (int i = 0; i < enemyCount; i++)
    {
        enemies[i].prevX = enemies[i].x;
        enemies[i].prevY = enemies[i].y;
    }
    for (int i = 0; i < enemyBulletCount; i++)
    {
        enemyBullets[i].prevX = enemyBullets[i].x;
        enemyBullets[i].prevY = enemyBullets[i].y;
    }
}

void GameEnemy::Update(Enemy enemies[], int enemyCount, EnemyBullet enemyBullets[], int enemyBulletCount, const Player& player)
{
    for (auto& v : g_MuzzleVisuals)
        if (v.timer > 0) --v.timer;

    ++g_UpdateFrame;

    g_EnemyList = enemies;
    g_EnemyListCount = enemyCount;
    {
        int rivals = 0;
        for (int i = 0; i < enemyCount; i++)
            if (enemies[i].active && enemies[i].type == EnemyType::Rival) ++rivals;
        if (rivals > 0 && g_RivalsAlive == 0 && !g_RivalLogged)
        {
            Mod::LogEvent("rival_start", "");
            g_RivalLogged = true;
        }
        if (rivals == 0 && g_RivalsAlive > 0)
        {
            Mod::LogEvent("rival_end", "");
            g_RivalLogged = false;
        }
        g_RivalsAlive = rivals;
    }

    for (int i = 0; i < enemyCount; i++)
    {
        if (!enemies[i].active)
        {
            continue;
        }

        if (enemies[i].hitFlashTimer >= 4)
        {
            const bool heavyHit = enemies[i].type != EnemyType::Scout && enemies[i].type != EnemyType::Sine
                                  && enemies[i].type != EnemyType::Charger;

            const float random = GameVfx::Hash(g_UpdateFrame * 7 + i * 13 + enemies[i].hp);

            GameEffect::SpawnHit(enemies[i].x - enemies[i].halfWidth * 0.55f,
                                 enemies[i].y + (random - 0.5f) * enemies[i].halfHeight * 0.9f, heavyHit);
        }

        if (enemies[i].hitFlashTimer > 0)
        {
            enemies[i].hitFlashTimer--;
        }

        if (enemies[i].damageCooldown > 0)
        {
            enemies[i].damageCooldown--;
        }

        g_ShooterType = static_cast<int>(enemies[i].type);
        ++enemies[i].age;
        if (enemies[i].kick > 0)
        {
            --enemies[i].kick;
        }
        const int bulletsBefore = g_BulletsSpawned;
#if GAME_AUDIO_TELEGRAPH_CUES
        const EnemyState stateBefore = enemies[i].state;
#endif

        switch (enemies[i].type)
        {
        case EnemyType::Scout:
            UpdateScout(enemies[i], enemyBullets, enemyBulletCount);
            break;

        case EnemyType::Sine:
            UpdateSine(enemies[i], enemyBullets, enemyBulletCount, player);
            break;

        case EnemyType::Heavy:
            UpdateHeavy(enemies[i], enemyBullets, enemyBulletCount, player);
            break;

        case EnemyType::Charger:
            UpdateCharger(enemies[i], enemyBullets, enemyBulletCount, player);
            break;

        case EnemyType::Turret:
            UpdateTurret(enemies[i], enemyBullets, enemyBulletCount, player);
            break;

        case EnemyType::Sniper:
            UpdateSniper(enemies[i], enemyBullets, enemyBulletCount, player);
            break;

        case EnemyType::Rival:
            UpdateRival(enemies[i], enemyBullets, enemyBulletCount, player);
            break;
        }

        if (g_BulletsSpawned != bulletsBefore)
        {
            enemies[i].kick = 8; // it fired this step
        }
#if GAME_AUDIO_TELEGRAPH_CUES
        // the two threats that have a telegraph on the screen and had none for the ears: a charger that locks its lane, a sniper that takes aim (cues: they lower the world for a moment)
        if (enemies[i].state != stateBefore)
        {
            if (enemies[i].type == EnemyType::Charger && enemies[i].state == EnemyState::ChargeWarning) GameAudio::PlayAt(GameSound::ChargerWarning, enemies[i].x);
            if (enemies[i].type == EnemyType::Sniper && enemies[i].state == EnemyState::AttackWarning) GameAudio::PlayAt(GameSound::SniperLock, enemies[i].x);
        }
#endif

        if (enemies[i].x < -1.3f || enemies[i].x > 1.4f || enemies[i].y < -1.3f || enemies[i].y > 1.3f)
        {
            enemies[i].active = false;
        }
    }

    UpdateRails();
    UpdateEnemyBulletArray(enemyBullets, enemyBulletCount, player);
    EnemyArt::Update(enemies, enemyCount);
}



bool GameEnemy::HasActiveEnemy(const Enemy enemies[], int enemyCount)
{
    for (int i = 0; i < enemyCount; i++)
    {
        if (enemies[i].active)
        {
            return true;
        }
    }

    return false;
}






