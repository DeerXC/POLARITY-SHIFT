#pragma once

enum class Polarity
{
    Red,
    Blue
};

enum class EnemyType
{
    Scout,
    Sine,
    Heavy,
    Charger,
    Turret,
    Sniper,
    Rival
};

enum class EnemyState
{
    Enter,
    Wait,
    Observe,
    Attack,
    Exit,

    AttackWarning,
    Reposition,
    Evade,

    ChargeWarning,
    Charging
};

enum class EnemyAttackType
{
    Straight,
    Aimed,
    Fan,
    Ring,
    HalfRing,
    Homing,
    Counter, // the rival answers the beam it has just dodged
    Needles, // the rival (sniper kit): white needles at where the ship is going to be (they cannot be absorbed)
    Dash,    // the rival (charger kit): it locks a lane and rams along it
    Mine,      // the heavy: two slow big bullets that burst into a ring when their fuse runs out
    CrossNet,  // the elite turret: two wide fans of the two colours at once, crossing, with a gap
    Rail,      // the elite sniper: a white laser across the screen, along the line it has locked
    Minefield  // the elite heavy: four mines around the ship, bursting one after the other
};

struct Player
{
    float x;
    float y;

    float halfWidth;
    float halfHeight;

    float speed;

    Polarity polarity;
};

struct Bullet
{
    float x;
    float y;

    float halfWidth;
    float halfHeight;

    float speed;

    Polarity polarity;

    bool active;
};

struct Enemy
{
    EnemyType type;
    Polarity polarity;
    EnemyState state;

    float x;
    float y;

    float halfWidth;
    float halfHeight;

    float vx;
    float vy;

    float baseY;
    float moveTime;
    float amplitude;
    float frequency;

    float targetX;
    float targetY;

    int hp;
    int maxHp;

    int stateTimer;
    int shotTimer;
    int hitFlashTimer;
    int damageCooldown;

    int attackPatternIndex;
    int warningTimer;

    int attackCount;
    int decisionTimer;
    int polaritySwitchTimer;

    int evadeCooldown;
    bool specialShotReactionUsed;

    EnemyAttackType pendingAttackType;

    int scoreValue;
    int groupId;

    bool active;
    float prevX; // position at the start of the last update step (render interpolation)
    float prevY;
    float fxX;          // the rival: where its last dodge started
    float fxY;
    int fxKind;         // the rival: 1 = dodge streak, 2 = arrival flash, 3 = the second half of the duel begins
    int fxTimer;
    bool phaseTwoShown; // the rival: the flash of the second half has been played
    bool phaseThreeShown; // the rival: the flash of the last quarter has been played
    int comboLeft;      // the rival: 1 while the move that is running was chained to the one before (it never chains twice)
    float aux;          // the rival: the angle of the orbit (half ring), or the distance that is left of a dash
    int age;            // update steps since the enemy appeared (the art uses it: entrance streaks, idle animation)
    int kick;           // steps since the enemy last fired (counts down from 8): the art kicks the hull back
    bool elite;         // an elite of its type (turret, sniper, heavy, charger): more life, faster, a high-risk move of its own
    bool shield;        // an elite (and the rival) has a shield until the piercing beam breaks it: a plain shot does Balance::ELITE_SHIELD of its damage
    int cycle;          // attack cycles done (the elite move comes every few cycles; the turret alternates fans and half rings)
    int dashes;         // the elite charger: dashes left of the zigzag
    int rivalRole;      // the rival: 0 = the first one, 1 = the wingman that joins in the second half of the duel
};

struct EnemyBullet
{
    float x;
    float y;

    float halfWidth;
    float halfHeight;

    float velocityX;

    float velocityY;

    float speed;
    float angle;

    float turnRate;

    int homingTimer;

    Polarity polarity;

    bool isHoming;
    int damage;   // HP the ship loses when it is hit by this bullet (Balance::DMG_BULLET)
    bool neutral; // a white bullet: it cannot be absorbed, it hurts whatever the colour of the ship is
    int owner;    // EnemyType of the enemy that fired it (the run log says what hurt the ship)
    float energyMul; // what absorbing it gives, as a multiple of a plain bullet: 3 for a mine, 2 for a bullet that follows the ship, 1 for the rest (Balance::ABSORB_MUL_*)
    bool grazed;  // DEFLECT: the chance to absorb it has been rolled already (a white bullet that grazes the ship rolls once)
    bool deflected; // DEFLECT: the roll was won: the white bullet is absorbed like one of the colour of the ship, as soon as it reaches the ship
    int fuse;     // a mine of the heavy: steps until it bursts into a ring (0 = an ordinary bullet)
    int burst;    // ... how many bullets it bursts into (0 = MINE_RING)
    float size;   // 1 = an ordinary bullet; a mine is drawn and hit 1.9 times as big
    bool active;
    float prevX; // position at the start of the last update step (render interpolation)
    float prevY;
};

