// preview_enemies: runs the REAL enemies (GameEnemy::Update) and draws them with the REAL drawing code (GameEnemy::Draw, EnemyArt, GameEffect) through the software renderer of
// preview_render.cpp, and writes a picture at the moments that matter: arriving, idle, winding up, firing, ramming, hit, badly damaged, destroyed.
//   preview_enemies <asset dir> <output dir> [type]      type 0..6 (scout sine heavy charger turret sniper rival), default all
#include "game/EnemyArt.h"
#include "game/GameEffect.h"
#include "game/GameEnemy.h"
#include "game/GamePlayer.h"
#include "game/GameTypes.h"
#include <cstdio>
#include <cstdlib>
#include <string>

bool preview_load(const char* directory);
void preview_begin(int width, int height, float r, float g, float b);
void preview_skip();
void preview_shift(float dx);
bool preview_save(const char* path, float cx, float cy, int halfW, int halfH);

namespace
{
constexpr int W = 2560, H = 1440;
const char* NAMES[7] = {"scout", "sine", "heavy", "charger", "turret", "sniper", "rival"};
Enemy g_Enemies[16];
EnemyBullet g_Bullets[192];
Player g_Player;

void Frame(bool capture, const std::string& path, float cx, float cy)
{
    if (capture) { preview_begin(W, H, 0.012f, 0.016f, 0.032f); preview_shift(-cx); } else preview_skip();
    GameEnemy::Draw(g_Enemies, 16);
    GameEnemy::DrawBullets(g_Bullets, 192);
    GameEffect::Draw();
    if (capture) preview_save(path.c_str(), 0.0f, cy, 380, 270);
}

void Run(int type, Polarity pol, const std::string& out)
{
    GameEnemy::Initialize(g_Enemies, 16, g_Bullets, 192);
    GameEffect::Initialize();
    GamePlayer::Initialize(g_Player);
    g_Player.x = -0.75f;
    g_Player.y = 0.0f;
    g_Player.polarity = Polarity::Blue;
    GameEnemy::Spawn(g_Enemies, 16, static_cast<EnemyType>(type), pol, 0.0f, 1);
    Enemy& e = g_Enemies[0];
    bool got[8] = {};
    int rushFrames = 0, hurtAt = -1, deathAt = -1, hitAt = -1;
    int deathShots = 0;
    auto path = [&](const char* label) { return out + "/" + NAMES[type] + "_" + label + ".ppm"; };
    for (int f = 0; f < 900; ++f)
    {
        GameEnemy::BeginStep(g_Enemies, 16, g_Bullets, 192);
        GameEnemy::Update(g_Enemies, 16, g_Bullets, 192, g_Player);
        GameEffect::Update();
        for (auto& b : g_Bullets) b.active = false; // only the enemy is looked at (the bullets have their own drawing)
        bool capture = false;
        const char* label = nullptr;
        if (e.active)
        {
            const bool warning = e.state == EnemyState::AttackWarning || e.state == EnemyState::ChargeWarning;
            if (!got[0] && e.state == EnemyState::Enter && e.x < 0.97f) { got[0] = true; capture = true; label = "1_enter"; }
            else if (!got[1] && e.age > 220 && !warning && e.kick == 0 && !(e.state == EnemyState::Attack)) { got[1] = true; capture = true; label = "2_idle"; }
            else if (!got[2] && e.age > 130 && warning && (e.warningTimer <= 7 || (e.state == EnemyState::ChargeWarning && e.stateTimer <= 12))) { got[2] = true; capture = true; label = "3_windup"; }
            else if (!got[3] && e.age > 130 && e.kick == 8) { got[3] = true; capture = true; label = "4_fire"; }
            else if (!got[4] && ((e.type == EnemyType::Charger && e.state == EnemyState::Charging) || (e.type == EnemyType::Rival && e.state == EnemyState::Attack && e.pendingAttackType == EnemyAttackType::Dash)))
            {
                if (++rushFrames == 12) { got[4] = true; capture = true; label = "5_rush"; }
            }
            if (!got[5] && e.age == 330) { e.hitFlashTimer = 4; EnemyArt::OnHit(e, e.x - e.halfWidth, e.y, e.shield); hitAt = f; got[5] = true; }
            if (hitAt == f - 1 && hitAt >= 0) { capture = true; label = "6_hit"; hitAt = -1; }
            if (e.age == 420 && e.maxHp >= 100) { e.hp = e.maxHp * 28 / 100; hurtAt = f; }
            if (hurtAt >= 0 && f == hurtAt + 34 && e.maxHp >= 100) { capture = true; label = "7_hurt"; hurtAt = -1; }
            if (e.age == 520) { GameEffect::SpawnExplosion(e.x, e.y, e.type == EnemyType::Heavy ? 0.16f : 0.10f); EnemyArt::OnDeath(e); deathAt = f; e.active = false; }
        }
        else if (deathAt >= 0)
        {
            const int d = f - deathAt;
            if (d == 5 || d == 13 || d == 26) { capture = true; static char buf[32]; std::snprintf(buf, sizeof buf, "8_death%d", ++deathShots); label = buf; }
            if (d > 30) break;
        }
        Frame(capture, capture ? path(label) : std::string(), g_Enemies[0].x, g_Enemies[0].y);
        if (!e.active && deathAt < 0) break;
    }
}

// the moves of the rival, one picture of each at the end of its warning, and one of the dash itself
void RunRival(const std::string& out)
{
    GameEnemy::Initialize(g_Enemies, 16, g_Bullets, 192);
    GameEffect::Initialize();
    GamePlayer::Initialize(g_Player);
    g_Player.x = -0.75f;
    g_Player.y = 0.10f;
    g_Player.polarity = Polarity::Blue;
    GameEnemy::Spawn(g_Enemies, 16, EnemyType::Rival, Polarity::Red, 0.0f, 1);
    Enemy& e = g_Enemies[0];
    const char* MOVES[] = {"Straight", "Aimed", "Fan", "Ring", "HalfRing", "Homing", "Counter", "Needles", "Dash"};
    bool got[16] = {};
    int dashFrames = 0, hurt = 0;
    for (int f = 0; f < 6000; ++f)
    {
        GameEnemy::BeginStep(g_Enemies, 16, g_Bullets, 192);
        GameEnemy::Update(g_Enemies, 16, g_Bullets, 192, g_Player);
        GameEffect::Update();
        if (f == 2500 && !hurt) { e.hp = e.maxHp * 40 / 100; hurt = 1; } // the second half of the duel
        bool capture = false;
        std::string label;
        const int move = static_cast<int>(e.pendingAttackType);
        if (e.state == EnemyState::AttackWarning && e.warningTimer == 6 && !got[move])
        {
            got[move] = true; capture = true; label = std::string("w_") + MOVES[move] + (hurt ? "_tier2" : "");
        }
        if (e.state == EnemyState::Attack && e.pendingAttackType == EnemyAttackType::Dash)
        {
            if (++dashFrames == 14 && !got[10]) { got[10] = true; capture = true; label = "a_Dash"; }
        }
        else dashFrames = 0;
        if (capture) { for (auto& b : g_Bullets) (void)b; }
        Frame(capture, capture ? out + "/rival_" + label + ".ppm" : std::string(), e.x, e.y);
        bool all = true;
        for (int m : {2, 3, 4, 7, 8, 10}) all &= got[m];
        if (all && hurt) break;
        if (all && !hurt) { f = 2500; }
    }
}
}

int main(int argc, char** argv)
{
    if (argc < 3 || !preview_load(argv[1])) { std::fprintf(stderr, "usage: preview_enemies <asset dir> <output dir> [type]\n"); return 1; }
    const std::string out = argv[2];
    for (int type = 0; type < 7; ++type)
    {
        if (argc > 3 && std::atoi(argv[3]) != type) continue;
        if (type == static_cast<int>(EnemyType::Rival) && argc > 4) { RunRival(out); continue; }
        Run(type, type == 5 ? Polarity::Red : (type % 2 == 0 ? Polarity::Red : Polarity::Blue), out);
        std::printf("%s done\n", NAMES[type]);
    }
    return 0;
}
