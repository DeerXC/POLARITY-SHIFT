// step_b_test: the five waves, the mines of the heavy, the elites and the twin rivals (the real GameWave / GameEnemy / GameSpecialShot).
//   1. five waves of three segments each (open, combine, test); wave 1 opens with scouts only and its test is an elite turret; 5 begins with the rival
//   2. a mine of the heavy: big, slow, a fuse; it bursts into a ring of its own colour when the fuse is out, and is gone when it is absorbed (deactivated) before
//   3. the elites: each of the four templates has more life, a shield (a plain shot does 75 %; the piercing beam breaks it), and its own move
//   5. the rival twin: in the second half a wingman of the other colour joins; they keep apart and never dash together
//   step_b_test returns 1 when a check fails
#include "boss/GameBoss.h"
#include "game/Combat.h"
#include "game/GameEnemy.h"
#include "game/GamePlayer.h"
#include "game/GamePlayerBullet.h"
#include "rogue/Modules.h"
#include "rogue/ModulesInternal.h"
#include "game/GameSpecialShot.h"
#include "game/GameState.h"
#include "game/GameWave.h"
#include "rogue/Balance.h"
#include "sim_input.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

static int g_Failures = 0;
static void Check(bool ok, const char* what)
{
    std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_Failures;
}

static Player MakePlayer(float y = 0.85f)
{
    Player p{};
    p.x = -0.85f;
    p.y = y;
    p.halfWidth = 0.06f;
    p.halfHeight = 0.08f;
    p.speed = 0.018f;
    p.polarity = Polarity::Blue;
    return p;
}

static void Reset()
{

    GameEnemy::Initialize(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX);
}

static int ActiveBullets()
{
    int n = 0;
    for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i) n += GameState::enemyBullets[i].active ? 1 : 0;
    return n;
}

static void Step(const Player& player)
{
    GameEnemy::BeginStep(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX);
    GameEnemy::Update(GameState::enemies, GameState::ENEMY_MAX, GameState::enemyBullets, GameState::ENEMY_BULLET_MAX, player);
}

static Enemy* Find(EnemyType type)
{
    for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        if (GameState::enemies[i].active && GameState::enemies[i].type == type) return &GameState::enemies[i];
    return nullptr;
}

static void TestWaves()
{
    std::printf("waves\n");
    Reset();
    GameWave::Initialize();
    Player player = MakePlayer();
    std::set<int> seen[GameWave::WAVE_COUNT + 2];
    std::set<int> openSeen[GameWave::WAVE_COUNT + 2];
    int elites[GameWave::WAVE_COUNT + 2] = {};
    bool segmentsInOrder = true;
    int lastSegment = 0, lastSegWave = 1;
    bool rivalFirstInLast = false, rivalEarly = false, firstOfLastSeen = false;
    int maxWave = 1;
    int age[GameState::ENEMY_MAX] = {};
    for (int frame = 0; frame < 60 * 60 * 6 && !GameWave::IsAllFinished(); ++frame)
    {
        GameWave::Update(GameState::enemies, GameState::ENEMY_MAX);
        Step(player);
        const int wave = GameWave::GetCurrentWave();
        if (wave > maxWave) maxWave = wave;
        const int seg = GameWave::GetCurrentSegment();
        if (wave == lastSegWave && seg < lastSegment) segmentsInOrder = false;
        if (wave != lastSegWave) lastSegment = 0;
        lastSegWave = wave;
        lastSegment = seg;
        for (int i = 0; i < GameState::ENEMY_MAX; ++i)
        {
            Enemy& e = GameState::enemies[i];
            if (!e.active)
            {
                age[i] = 0;
                continue;
            }
            if (age[i]++ == 0 && wave >= 1 && wave <= GameWave::WAVE_COUNT)
            {
                seen[wave].insert(static_cast<int>(e.type));
                if (seg == 0) openSeen[wave].insert(static_cast<int>(e.type));
                if (e.elite) ++elites[wave];
                if (wave == GameWave::WAVE_COUNT && !firstOfLastSeen)
                {
                    firstOfLastSeen = true;
                    rivalFirstInLast = (e.type == EnemyType::Rival);
                }
                if (wave < GameWave::WAVE_COUNT && e.type == EnemyType::Rival) rivalEarly = true;
            }
            if (age[i] > 25) // the player kills everything a moment after it appears
            {
                e.hp = 0;
                e.active = false;
            }
        }
        for (int i = 0; i < GameState::ENEMY_BULLET_MAX; ++i) GameState::enemyBullets[i].active = false;
    }
    Check(GameWave::IsAllFinished(), "the waves end");
    Check(maxWave == GameWave::WAVE_COUNT + 1, "there are exactly five waves (the counter is on 6 when the last one is done: the boss)");
    for (int w = 1; w <= GameWave::WAVE_COUNT; ++w)
        if (seen[w].empty()) Check(false, "a wave brought no enemy at all");
    auto only = [&](int wave, std::set<int> allowed)
    {
        for (int t : seen[wave])
            if (!allowed.count(t)) return false;
        return !seen[wave].empty();
    };
    (void)only;
    std::printf("  (elites per wave: %d %d %d %d %d)\n", elites[1], elites[2], elites[3], elites[4], elites[5]);
    Check(segmentsInOrder, "the segments of a wave come in order (open, combine, test)");
    Check(openSeen[1].size() == 1 && openSeen[1].count(static_cast<int>(EnemyType::Scout)), "wave 1 opens with scouts only");
    Check(elites[1] == 1 && seen[1].count(static_cast<int>(EnemyType::Turret)), "the test of wave 1 is one elite turret");
    Check(seen[2].count(static_cast<int>(EnemyType::Charger)), "wave 2 has chargers");
    Check(seen[3].count(static_cast<int>(EnemyType::Heavy)) && seen[3].count(static_cast<int>(EnemyType::Sniper)) && elites[3] == 2, "wave 3: heavies and snipers, two elites");
    Check(elites[4] == 2, "wave 4: two elites");
    Check(rivalFirstInLast, "wave 5: the rival is the first to come");
    Check(!rivalEarly, "no rival before wave 5");
}

static void TestMine()
{
    std::printf("mines\n");
    Reset();
    Player player = MakePlayer(0.0f);
    GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Heavy, Polarity::Red, 0.0f, 1);
    int mineSlot = -1;
    for (int i = 0; i < 1200 && mineSlot < 0; ++i)
    {
        Step(player);
        for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k)
            if (GameState::enemyBullets[k].active && GameState::enemyBullets[k].fuse > 0) mineSlot = k;
    }
    Check(mineSlot >= 0, "the heavy lays a mine");
    if (mineSlot < 0) return;
    EnemyBullet mine = GameState::enemyBullets[mineSlot];
    Check(mine.size > 1.5f && mine.halfWidth > 0.030f, "the mine is big (drawn and hit as such)");
    Check(mine.polarity == Polarity::Red, "the ship is blue: the mine is red (the colour the ship does not have)");
    int mines = 0;
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) mines += (GameState::enemyBullets[k].active && GameState::enemyBullets[k].fuse > 0) ? 1 : 0;
    Check(mines == 2, "two mines bracket the ship");
    // park the heavy out of the way: it must not fire more while the fuse burns
    for (auto& e : GameState::enemies) e.active = false;
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k)
        if (k != mineSlot && GameState::enemyBullets[k].active) GameState::enemyBullets[k].active = false;
    const float speed0 = GameState::enemyBullets[mineSlot].speed;
    int fuse = GameState::enemyBullets[mineSlot].fuse;
    for (int i = 0; i < 20; ++i) Step(player);
    Check(GameState::enemyBullets[mineSlot].speed < speed0, "it slows down");
    Check(GameState::enemyBullets[mineSlot].fuse < fuse, "the fuse burns");
    Check(ActiveBullets() <= 2, "it has not burst yet");
    for (int i = 0; i < 400 && GameState::enemyBullets[mineSlot].fuse > 0; ++i) Step(player);
    int ring = 0, same = 0;
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k)
    {
        const EnemyBullet& b = GameState::enemyBullets[k];
        if (b.active && b.fuse == 0 && b.size == 1.0f)
        {
            ++ring;
            if (b.polarity == Polarity::Red) ++same;
        }
    }
    Check(ring >= 7, "when the fuse is out the mine bursts into a ring (at least seven bullets from the first mine)");
    Check(same > 0 && same < ring, "the ring has both colours");
}

// the elites: the four templates, the shield, and the moves of their own
static Enemy* SpawnEliteAt(EnemyType type, Player& player, int frames)
{
    Reset();
    GameEnemy::SpawnElite(GameState::enemies, GameState::ENEMY_MAX, type, Polarity::Red, 0.0f, 1);
    for (int i = 0; i < frames; ++i) Step(player);
    return Find(type);
}

static void TestElites()
{
    std::printf("elites\n");
    Player player = MakePlayer(0.0f);
    // life and shield
    Reset();
    GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Turret, Polarity::Red, 0.0f, 1);
    const Enemy plain = *Find(EnemyType::Turret);
    Reset();
    GameEnemy::SpawnElite(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Turret, Polarity::Red, 0.0f, 1);
    const Enemy elite = *Find(EnemyType::Turret);
    Check(!plain.elite && !plain.shield, "a plain turret has no shield");
    Check(elite.elite && elite.shield && elite.maxHp == Balance::ELITE_HP_TURRET * Balance::HP_UNIT && elite.halfWidth > plain.halfWidth, "an elite turret: its life, a shield, a bigger body");
    // a plain shot on the shield does 75 %; the beam breaks it
    Mod::Reset();
    GameState::ResetRun();
    Enemy* e = SpawnEliteAt(EnemyType::Heavy, player, 200);
    if (e)
    {
        const int hp0 = e->hp;
        Bullet* all = GamePlayerBullet::GetBullets();
        for (int k = 0; k < GamePlayerBullet::GetBulletCount(); ++k) all[k].active = false;
        Bullet& b = all[0];
        b = {};
        b.active = true;
        b.x = e->x - e->halfWidth - 0.01f;
        b.y = e->y;
        b.halfWidth = 0.02f;
        b.halfHeight = 0.01f;
        b.speed = 0.03f;
        b.polarity = Polarity::Blue;
        e->damageCooldown = 0;
        Combat::CheckBulletEnemyCollision();
        const int dealt = hp0 - e->hp;
        std::printf("  (a plain shot on the shield took %d of %d)\n", dealt, Balance::HP_UNIT);
        Check(dealt == static_cast<int>(std::lround(Balance::HP_UNIT * 0.75f)), "a plain shot on the shield of an elite does 75 %");
        GameState::player.x = e->x - 0.5f;
        GameState::player.y = e->y;
        GameState::energy = Mod::EnergyMax();
        SimInput::Clear();
        SimInput::down['V'] = true;
        GameSpecialShot::TryActivate(GameState::player, GameState::energy, Mod::EnergyMax());
        SimInput::EndFrame();
        SimInput::Clear(); // (the key is let go again)
        SimInput::EndFrame();
        GameSpecialShot::Update(GameState::player);
        Combat::ApplySpecialShotDamage();
        Check(!e->shield, "the piercing beam breaks the shield");
        GameSpecialShot::Initialize();
    }
    // the moves of their own
    auto sawMove = [&](EnemyType type, EnemyAttackType move, int frames)
    {
        Enemy* x = SpawnEliteAt(type, player, 1);
        for (int i = 0; i < frames && x; ++i)
        {
            for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k)
                if (GameState::enemyBullets[k].fuse == 0) GameState::enemyBullets[k].active = false;
            Step(player);
            x = Find(type);
            if (x && x->pendingAttackType == move && (x->state == EnemyState::AttackWarning || x->state == EnemyState::Attack)) return true;
        }
        return false;
    };
    Check(sawMove(EnemyType::Turret, EnemyAttackType::CrossNet, 60 * 25), "the elite turret fires its CROSS NET");
    Check(sawMove(EnemyType::Sniper, EnemyAttackType::Rail, 60 * 25), "the elite sniper fires its RAIL");
    Check(sawMove(EnemyType::Heavy, EnemyAttackType::Minefield, 60 * 25), "the elite heavy lays its MINEFIELD");
    // the rail hurts the ship on its line, not beside it
    {
        Enemy* sn = SpawnEliteAt(EnemyType::Sniper, player, 1);
        bool hitOn = false, hitOff = false;
        for (int i = 0; i < 60 * 25 && sn; ++i)
        {
            Step(player);
            sn = Find(EnemyType::Sniper);
            float x0[4], y0[4], ang[4];
            int fu[4];
            const int n = GameEnemy::GetRailsForSim(x0, y0, ang, fu, 4);
            if (n > 0 && fu[0] == 0)
            {
                Player on = player;
                on.x = -0.6f;
                on.y = y0[0] + (on.x - x0[0]) * std::tan(ang[0]) / (720.0f / 1280.0f);
                hitOn = GameEnemy::RailHits(on, GamePlayer::GetHurtHalfHeight(on));
                Player off = on;
                off.y += 0.4f;
                hitOff = GameEnemy::RailHits(off, GamePlayer::GetHurtHalfHeight(off));
                break;
            }
        }
        Check(hitOn && !hitOff, "the rail hurts on its line and not beside it");
    }
    // the elite charger dashes several times and stays on the screen
    {
        Enemy* ch = SpawnEliteAt(EnemyType::Charger, player, 1);
        int dashes = 0;
        EnemyState prev = EnemyState::Enter;
        for (int i = 0; i < 60 * 20 && ch; ++i)
        {
            Step(player);
            ch = Find(EnemyType::Charger);
            if (ch && ch->state == EnemyState::Charging && prev != EnemyState::Charging) ++dashes;
            if (ch) prev = ch->state;
        }
        std::printf("  (the elite charger dashed %d times in 20 s)\n", dashes);
        Check(ch != nullptr, "the elite charger stays on the screen");
        Check(dashes >= 5, "... and dashes again and again (the zigzag: three in a row)");
    }
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) GameState::enemyBullets[k].active = false;
}

static int RivalCount()
{
    int n = 0;
    for (auto& e : GameState::enemies) n += (e.active && e.type == EnemyType::Rival) ? 1 : 0;
    return n;
}

static void TestRivalTwin()
{
    std::printf("rival twin\n");
    Reset();
    GameEnemy::SetTwinEnabled(true);
    Player player = MakePlayer(0.0f);
    GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Rival, Polarity::Red, 0.0f, 1);
    SimAudio::Reset();
    bool otherColourAtArrival = false;
    for (int i = 0; i < 120; ++i)
    {
        Step(player);
        if (RivalCount() == 2 && !otherColourAtArrival) // (the colour is compared when the twin arrives: the glow of a rival flips now and then, as a tell)
        {
            Enemy* a = nullptr;
            Enemy* b = nullptr;
            for (auto& e : GameState::enemies)
                if (e.active && e.type == EnemyType::Rival) (a ? b : a) = &e;
            otherColourAtArrival = a && b && a->polarity != b->polarity;
            if (!otherColourAtArrival) break;
        }
    }
    Check(RivalCount() == 2, "the duel is against two rivals (the twin comes in a moment after the first one)");
    Check(SimAudio::Count(GameSound::RivalTwin) == 1, "the twin has an entrance sound of its own, once");
    Check(SimAudio::Count(GameSound::BossCannonCharge) == 0, "... and it is not the whine of the boss cannon (3 kHz and rising)");
    Enemy* first = nullptr;
    Enemy* wing = nullptr;
    for (auto& e : GameState::enemies)
    {
        if (!e.active || e.type != EnemyType::Rival) continue;
        if (e.rivalRole == 1) wing = &e; else first = &e;
    }
    Check(first != nullptr && wing != nullptr, "one of them is the first, the other one the twin");
    if (!first || !wing) return;
    Check(otherColourAtArrival, "the twin arrives in the other colour");
    const int loneLife = Balance::ENEMY_HP[static_cast<int>(EnemyType::Rival)] * Balance::HP_UNIT;
    Check(wing->maxHp == first->maxHp && first->maxHp >= loneLife * 108 / 100 && first->maxHp <= loneLife * 112 / 100, "both have 110 % of the life of a lone rival");
    int overlap = 0, bothDash = 0, frames = 0;
    std::set<int> moves;
    int mines[2] = {0, 0}, dashes[2] = {0, 0};
    int lastMove[2] = {-1, -1};
    for (int i = 0; i < 60 * 150; ++i)
    {
        player.y = 0.85f;
        for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) GameState::enemyBullets[k].active = false;
        Step(player);
        Enemy* a = nullptr;
        Enemy* b = nullptr;
        for (auto& e : GameState::enemies)
            if (e.active && e.type == EnemyType::Rival) (a ? b : a) = &e;
        if (!a || !b) break;
        a->hp = a->maxHp; // (nothing damages them here)
        b->hp = b->maxHp;
        ++frames;
        if (std::fabs(a->x - b->x) < a->halfWidth + b->halfWidth && std::fabs(a->y - b->y) < a->halfHeight + b->halfHeight) ++overlap;
        const bool da = (a->state == EnemyState::AttackWarning || a->state == EnemyState::Attack) && a->pendingAttackType == EnemyAttackType::Dash;
        const bool db = (b->state == EnemyState::AttackWarning || b->state == EnemyState::Attack) && b->pendingAttackType == EnemyAttackType::Dash;
        if (da && db) ++bothDash;
        for (Enemy* e : {a, b})
        {
            if (e->state != EnemyState::AttackWarning) continue;
            const int type = static_cast<int>(e->pendingAttackType);
            moves.insert(type);
            const int role = e->rivalRole;
            if (lastMove[role] != type + 1000 * (e->warningTimer > 0 ? 0 : 1))
            {
                if (e->warningTimer >= 20 && lastMove[role] != type)
                {
                    if (e->pendingAttackType == EnemyAttackType::Mine) ++mines[role];
                    if (e->pendingAttackType == EnemyAttackType::Dash) ++dashes[role];
                }
                lastMove[role] = type;
            }
        }
    }
    std::printf("  (%d steps with both rivals; overlap in %d; mines first/twin %d/%d, dashes first/twin %d/%d; %d kinds of move seen)\n", frames, overlap, mines[0], mines[1], dashes[0], dashes[1], static_cast<int>(moves.size()));
    Check(frames > 6000, "the two rivals keep fighting");
    Check(overlap * 50 < frames, "the two rivals keep apart (overlap in under 2 % of the steps)");
    Check(bothDash == 0, "never two dashes (two lanes) at the same time");
    Check(moves.count(static_cast<int>(EnemyAttackType::Mine)) && moves.count(static_cast<int>(EnemyAttackType::Homing)), "they use the mines of the heavy and the shots of the turret that follow the ship");
    Check(moves.count(static_cast<int>(EnemyAttackType::Dash)) && moves.count(static_cast<int>(EnemyAttackType::Needles)) && moves.count(static_cast<int>(EnemyAttackType::HalfRing))
              && moves.count(static_cast<int>(EnemyAttackType::Ring)) && moves.count(static_cast<int>(EnemyAttackType::Fan)),
          "... and the charger, the sniper, the half rings, the heavy ring and the fan: all the moves of the elites");
    Check(mines[0] >= mines[1] && dashes[1] >= dashes[0], "the first one keeps its distance (mines), the twin closes in (dashes)");
    Enemy* w = nullptr;
    for (auto& e : GameState::enemies)
        if (e.active && e.type == EnemyType::Rival && e.rivalRole == 1) w = &e;
    if (w)
    {
        w->hp = 0;
        w->active = false;
    }
    for (int i = 0; i < 120; ++i) Step(player);
    Check(RivalCount() == 1, "when one rival falls the other one is alone");
}

// the rings are arcs that are centred on the ship: most of the bullets of a ring fly towards it (a full ring wasted half of them)
static void TestAimedRings()
{
    std::printf("aimed rings\n");
    Reset();
    GameEnemy::SetTwinEnabled(false);
    Player player = MakePlayer(0.0f);
    player.x = -0.80f;
    GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Heavy, Polarity::Red, 0.0f, 1);
    int ringsSeen = 0, towards = 0, total = 0;
    for (int i = 0; i < 60 * 40 && ringsSeen < 3; ++i)
    {
        Enemy* h = Find(EnemyType::Heavy);
        if (!h) break;
        const bool ring = h->pendingAttackType == EnemyAttackType::Ring && h->state == EnemyState::Attack;
        const int before = ActiveBullets();
        Step(player);
        h = Find(EnemyType::Heavy);
        if (ring && h && ActiveBullets() - before >= 14)
        {
            ++ringsSeen;
            for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k)
            {
                const EnemyBullet& b = GameState::enemyBullets[k];
                if (!b.active || b.fuse > 0) continue;
                ++total;
                if (b.velocityX < 0.0f) ++towards;
            }
        }
        for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) GameState::enemyBullets[k].active = false;
    }
    std::printf("  (%d rings of the heavy; %d of %d bullets fly towards the ship)\n", ringsSeen, towards, total);
    Check(ringsSeen >= 2, "the heavy fires rings");
    Check(total > 0 && towards * 100 >= total * 95, "all the bullets of a ring fly towards the ship (a full ring: half of them)");
}

// what absorbing a bullet gives: a mine three times, a bullet that follows the ship twice, the rest the plain value
static void TestAbsorbEnergy()
{
    std::printf("absorb energy\n");
    Mod::Reset();
    GameState::ResetRun();
    GamePlayer::Initialize(GameState::player);
    GameState::player.polarity = Polarity::Red;
    auto gainOf = [&](float mul, bool homing, int fuse)
    {
        Mod::Reset();
        GameState::energy = 0;
        EnemyBullet& b = GameState::enemyBullets[0];
        b = {};
        b.active = true;
        b.x = GameState::player.x;
        b.y = GameState::player.y;
        b.halfWidth = b.halfHeight = 0.02f;
        b.polarity = Polarity::Red;
        b.isHoming = homing;
        b.fuse = fuse;
        b.energyMul = mul;
        Combat::CheckPlayerEnemyBulletCollision();
        return GameState::energy;
    };
    const int plain = gainOf(1.0f, false, 0);
    const int homing = gainOf(Balance::ABSORB_MUL_HOMING, true, 0);
    const int mine = gainOf(Balance::ABSORB_MUL_MINE, false, 60);
    std::printf("  (a plain bullet %d, a bullet that follows the ship %d, a mine %d)\n", plain, homing, mine);
    Check(plain >= 5 && plain <= 7, "a plain bullet gives about 6");
    Check(homing >= 2 * plain - 1 && homing <= 2 * plain + 1, "a bullet that follows the ship gives twice that");
    Check(mine >= 3 * plain - 1 && mine <= 3 * plain + 1, "a mine gives three times that");
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) GameState::enemyBullets[k].active = false;
}

// the modules that answer the bodies and the white bullets
static void TestSurviveModules()
{
    std::printf("pulse, aegis and deflect\n");
    Mod::Reset();
    GameState::ResetRun();
    GamePlayer::Initialize(GameState::player);
    Mod::Grant(Mod::Id::Pulse);
    GameState::playerHp = Mod::MaxHp();
    Check(Mod::ScaleDamageTaken(16, "ram") == 12 && Mod::ScaleDamageTaken(16, "boss_body_ram") == 12, "PULSE: a body hits 25 % softer (16 -> 12)");
    Check(Mod::ScaleDamageTaken(16, "sniper") == 16, "... a bullet does not");
    Mod::Reset();
    GameState::ResetRun();
    GamePlayer::Initialize(GameState::player);
    Mod::Grant(Mod::Id::Aegis);
    GameState::playerHp = Mod::MaxHp();
    Check(Mod::ScaleDamageTaken(13, "sniper") == 0, "AEGIS: the shield takes a hit of ANY kind (a coloured bullet) - it took white bullets only, and DEFLECT answers those");
    Check(Mod::ScaleDamageTaken(13, "white_sniper") == 13, "... once: the shield has to recharge (the next hit, a white bullet, goes through)");
    Check(Mod::ScaleDamageTaken(18, "boss_beam_rail_shot") == 18, "... and so does a beam of the boss while it is down");
    for (int i = 0; i < Balance::AEGIS_ABSORBS; ++i) Mod::OnAbsorb(6.0f);
    Check(Mod::ScaleDamageTaken(18, "boss_beam_rail_shot") == 0, "... after the absorbed bullets it stops a beam of the boss");
    Check(Mod::ScaleDamageTaken(16, "ram") == 16, "... and then a body hit is not stopped (it is recharging again)");
    for (int i = 0; i < Balance::AEGIS_ABSORBS; ++i) Mod::OnAbsorb(6.0f);
    Check(Mod::ScaleDamageTaken(16, "ram") == 0, "... but a body is, once it is back");
    // deflect
    Mod::Reset();
    GameState::ResetRun();
    GamePlayer::Initialize(GameState::player);
    Mod::Grant(Mod::Id::Deflect);
    int absorbed = 0, energyOfAbsorbed = 0;
    const int trials = 400;
    for (int n = 0; n < trials; ++n)
    {
        GameState::playerHp = 100;
        GameState::playerInvincibleTimer = 0;
        GameState::energy = 0;
        EnemyBullet& b = GameState::enemyBullets[0];
        b = {};
        b.active = true;
        b.neutral = true;
        b.x = GameState::player.x;
        b.y = GameState::player.y;
        b.halfWidth = b.halfHeight = 0.02f;
        b.damage = 13;
        b.energyMul = 1.0f;
        Combat::CheckPlayerEnemyBulletCollision();
        if (GameState::playerHp == 100 && GameState::energy > 0)
        {
            ++absorbed;
            if (energyOfAbsorbed == 0) energyOfAbsorbed = GameState::energy; // (the first one: the chain has not heated up yet)
        }
    }
    std::printf("  (%d of %d white bullets that grazed the ship were absorbed)\n", absorbed, trials);
    Check(absorbed > trials * 78 / 100 && absorbed < trials * 92 / 100, "DEFLECT: about 85 % of the white bullets that graze the ship are absorbed");
    Check(energyOfAbsorbed >= 16 && energyOfAbsorbed <= 20, "... and each of them gives three times the energy of a plain bullet");
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) GameState::enemyBullets[k].active = false;
}

// the new modules and the tactics (the real Mod:: code; the TACTIC key is SHIFT)
static void TestModulesAndTactics()
{
    std::printf("modules and tactics\n");
    auto fresh = [&]()
    {
        Reset();
        Mod::Reset();
        GameState::ResetRun();
        GamePlayer::Initialize(GameState::player);
        GameState::player.x = -0.5f;
        GameState::player.y = 0.0f;
        GameState::playerHp = Mod::MaxHp();
    };
    // ECHO: the arc jumps to two more enemies
    fresh();
    Mod::Grant(Mod::Id::Echo);
    for (int k = 0; k < 4; ++k)
        GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Heavy, Polarity::Red, -0.6f + 0.4f * static_cast<float>(k), 1);
    for (auto& e : GameState::enemies)
        if (e.active) e.x = 0.2f;
    int before = 0;
    for (auto& e : GameState::enemies) before += e.active ? e.hp : 0;
    int hit = 0;
    Mod::FireEchoArc(GameState::player.x, GameState::player.y);
    int after = 0;
    for (auto& e : GameState::enemies)
    {
        if (!e.active) continue;
        after += e.hp;
        if (e.hp < e.maxHp) ++hit;
    }
    Check(hit == 3, "ECHO: one arc hits three enemies (the nearest and two jumps)");
    // OVERFLOW: a full bar stores the absorbed bullets, the next full beam is longer and harder
    fresh();
    Mod::Grant(Mod::Id::Overflow);
    GameState::energy = Mod::EnergyMax();
    for (int i = 0; i < 20; ++i) Mod::OnAbsorb(6.0f);
    int energy = GameState::energy;
    Check(Mod::BeginBeam(energy) && energy == 0, "a full bar fires a full beam");
    Check(std::fabs(Mod::SpecialDamageMul() - 1.48f) < 0.01f && std::fabs(Mod::BeamDurationScale() - 1.48f) < 0.01f, "OVERFLOW: 12 stored bullets make it 48 % longer and harder");
    // TWIN: with half a bar a half beam; without TWIN nothing
    fresh();
    energy = Mod::EnergyMax() / 2;
    Check(!Mod::BeginBeam(energy), "without TWIN half a bar fires nothing");
    Mod::Grant(Mod::Id::Twin);
    energy = Mod::EnergyMax() * 3 / 4;
    Check(Mod::BeginBeam(energy) && energy == Mod::EnergyMax() / 4 && std::fabs(Mod::BeamDurationScale() - 0.5f) < 0.01f, "TWIN: three quarters of a bar fire a half beam that costs half a bar");
    // AFTERGLOW: after the beam the shots hit harder for a while
    fresh();
    Mod::Grant(Mod::Id::Afterglow);
    Check(std::fabs(Mod::DamageMul() - 1.0f) < 0.001f, "AFTERGLOW: plain shots before the beam");
    Mod::g_AfterglowTimer = 10;
    Check(std::fabs(Mod::DamageMul() - 1.4f) < 0.001f, "... 40 % harder after it");
    // FOCUS: hits on one target in a row
    fresh();
    Mod::Grant(Mod::Id::Focus);
    float m = 1.0f;
    for (int i = 0; i < 12; ++i) m = Mod::OnShotHit(3);
    Check(std::fabs(m - 1.4f) < 0.001f, "FOCUS: ten hits on one target: +40 %");
    Check(std::fabs(Mod::OnShotHit(4) - 1.0f) < 0.001f, "... another target starts again");
    // BACKWATER: under half of the HP the beam needs less
    fresh();
    Mod::Grant(Mod::Id::Backwater);
    const int needFull = Mod::EnergyMax();
    GameState::playerHp = Mod::MaxHp() / 3;
    Check(Mod::EnergyMax() == needFull - Balance::BACKWATER_ENERGY_CUT, "BACKWATER: under half of the HP the beam needs less");
    // resonance: two break modules: the beam needs 10 less
    fresh();
    Mod::Grant(Mod::Id::Ember);
    Mod::Grant(Mod::Id::Breach);
    Check(Mod::EnergyMax() == GameState::MAX_ENERGY - Balance::RES_BREAK_ENERGY_CUT, "two break modules: the beam needs 10 less");
    // tactic DASH: the ship goes forward, invincible
    fresh();
    Mod::g_Tactic = static_cast<int>(Mod::Tactic::Dash);
    const float x0 = GameState::player.x;
    SimInput::Clear();
    SimInput::down[VK_SHIFT] = true;
    Mod::AfterPlayerUpdate();
    SimInput::EndFrame();
    for (int i = 0; i < 20; ++i)
    {
        SimInput::Clear();
        Mod::AfterPlayerUpdate();
        SimInput::EndFrame();
    }
    Check(GameState::player.x - x0 > 0.35f, "DASH: the ship dashes forward when it does not move");
    Check(Mod::g_TacticCooldown > 0, "... and the tactic recharges");
    // tactic CHARGE: hold, release: a beam
    fresh();
    Mod::g_Tactic = static_cast<int>(Mod::Tactic::Charge);
    GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Heavy, Polarity::Red, 0.0f, 1);
    Enemy* heavy = Find(EnemyType::Heavy);
    heavy->x = 0.3f;
    heavy->y = 0.0f;
    const int hp0 = heavy->hp;
    for (int i = 0; i < Balance::CHARGE_FULL + 4; ++i)
    {
        SimInput::Clear();
        SimInput::down[VK_SHIFT] = true;
        Mod::AfterPlayerUpdate();
        SimInput::EndFrame();
    }
    Check(Mod::MoveSpeedScale() < 0.9f, "CHARGE: the ship is slower while it charges");
    SimInput::Clear();
    Mod::AfterPlayerUpdate();
    SimInput::EndFrame();
    Check(heavy->hp < hp0 - 8 * Balance::HP_UNIT + 1, "... a full charge hits for 8 shots");
    Check(Mod::g_TacticBeamHalf >= 0.05f, "... and its beam is high enough to break an overload");
    // tactic DECOY: a decoy of the colour of the ship absorbs its bullets
    fresh();
    Mod::g_Tactic = static_cast<int>(Mod::Tactic::Decoy);
    GameState::player.polarity = Polarity::Blue;
    SimInput::Clear();
    SimInput::down[VK_SHIFT] = true;
    Mod::AfterPlayerUpdate();
    SimInput::EndFrame();
    Check(Mod::g_Decoy.active && Mod::g_Decoy.polarity == Polarity::Blue, "DECOY: dropped where the ship is, in its colour");
    GameState::player.x = 0.6f; // the ship flies away
    EnemyBullet& b = GameState::enemyBullets[0];
    b = {};
    b.active = true;
    b.x = Mod::g_Decoy.x + 0.02f;
    b.y = Mod::g_Decoy.y;
    b.halfWidth = b.halfHeight = 0.02f;
    b.polarity = Polarity::Blue;
    b.energyMul = 1.0f;
    GameState::energy = 0;
    Mod::UpdateZones();
    Check(!b.active && GameState::energy > 0, "... it absorbs a bullet of its colour and gives energy");
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) GameState::enemyBullets[k].active = false;
    Mod::Reset();
}

// the piercing beam does not make the ship invulnerable any more
static void TestBeamNoShield()
{
    std::printf("the beam does not protect the ship\n");
    Reset();
    Mod::Reset();
    GameState::ResetRun();
    GamePlayer::Initialize(GameState::player);
    GameState::player.x = -0.5f;
    GameState::player.y = 0.0f;
    GameState::player.polarity = Polarity::Blue;
    GameState::playerHp = Mod::MaxHp();
    GameState::playerInvincibleTimer = 0;
    GameState::energy = Mod::EnergyMax();
    SimInput::Clear();
    SimInput::down['V'] = true;
    const bool fired = GameSpecialShot::TryActivate(GameState::player, GameState::energy, Mod::EnergyMax());
    SimInput::EndFrame();
    Check(fired && GameSpecialShot::IsActive(), "the beam is on");
    EnemyBullet& b = GameState::enemyBullets[0];
    b = {};
    b.active = true;
    b.x = GameState::player.x;
    b.y = GameState::player.y;
    b.halfWidth = b.halfHeight = 0.02f;
    b.polarity = Polarity::Red; // the other colour than the ship
    b.damage = 8;
    b.energyMul = 1.0f;
    Combat::CheckPlayerEnemyBulletCollision();
    Check(GameState::playerHp < Mod::MaxHp(), "a bullet of the other colour hurts the ship while the beam is on");
    GameState::playerHp = Mod::MaxHp();
    GameState::playerInvincibleTimer = 0;
    GameEnemy::Spawn(GameState::enemies, GameState::ENEMY_MAX, EnemyType::Scout, Polarity::Red, 0.0f, 1);
    Enemy* scout = Find(EnemyType::Scout);
    scout->x = GameState::player.x;
    scout->y = GameState::player.y;
    Combat::CheckPlayerEnemyCollision();
    Check(GameState::playerHp < Mod::MaxHp(), "an enemy that flies into the ship hurts it while the beam is on");
    for (int k = 0; k < GameState::ENEMY_BULLET_MAX; ++k) GameState::enemyBullets[k].active = false;
    Mod::Reset();
}

int main()
{
    TestWaves();
    TestMine();
    TestElites();
    TestRivalTwin();
    TestAimedRings();
    TestAbsorbEnergy();
    TestSurviveModules();
    TestBeamNoShield();
    TestModulesAndTactics();
    std::printf(g_Failures == 0 ? "all checks passed\n" : "%d check(s) FAILED\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}


