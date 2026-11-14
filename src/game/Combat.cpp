#include "game/Combat.h"
#include "boss/GameBoss.h"
#include "rogue/ModuleDefs.h"
#include "core/Collision.h"
#include "core/GameAudio.h"
#include "core/PostFx.h"
#include "game/EnemyArt.h"
#include "game/GameEffect.h"
#include "game/GameEnemy.h"
#include "rogue/Balance.h"
#include "game/GamePlayer.h"
#include "game/GamePlayerBullet.h"
#include "game/GameSpecialShot.h"
#include "game/GameState.h"
#include "game/GameTypes.h"
#include "game/ScreenShake.h"
#include "rogue/Modules.h"

#include <algorithm>
#include <cstdio>

namespace Combat
{
static int GetEnemyDamageCooldownFrames(EnemyType type)
{
    switch (type)
    {
    case EnemyType::Heavy:
    case EnemyType::Turret:
    case EnemyType::Sniper:
        return 2;

    case EnemyType::Rival:
        return 3;

    default:
        return 0;
    }
}

bool UsesHeavyEnemyAudio(EnemyType type)
{
    return type == EnemyType::Heavy || type == EnemyType::Turret || type == EnemyType::Sniper || type == EnemyType::Rival;
}

void CheckBulletEnemyCollision()
{
    Bullet* bullets = GamePlayerBullet::GetBullets();

    const int bulletCount = GamePlayerBullet::GetBulletCount();

    for (int bulletIndex = 0; bulletIndex < bulletCount; bulletIndex++)
    {
        Bullet& bullet = bullets[bulletIndex];

        if (!bullet.active)
        {
            continue;
        }

        for (int enemyIndex = 0; enemyIndex < GameState::ENEMY_MAX; enemyIndex++)
        {
            Enemy& enemy = GameState::enemies[enemyIndex];

            if (!enemy.active)
            {
                continue;
            }

            const bool isHit = Collision::CheckAABB(bullet.x, bullet.y, bullet.halfWidth, bullet.halfHeight, enemy.x, enemy.y,
                                                    enemy.halfWidth, enemy.halfHeight);

            if (!isHit)
            {
                continue;
            }

            bullet.active = false;

            if (enemy.damageCooldown > 0)
            {
                break;
            }

            // One base shot is Balance::HP_UNIT points. An elite (and the rival) has a shield until the piercing beam breaks it: a plain shot does
            // Balance::ELITE_SHIELD of its damage (BREACH goes through). The modules scale the damage (Mod::DamageMul).
            const float focus = Mod::OnShotHit(enemyIndex); // FOCUS: hits on the same enemy in a row
            const float shield = enemy.shield ? Mod::ShieldFactor() : 1.0f;
            const bool bonusTarget = enemy.elite || enemy.type == EnemyType::Rival;
            const int damage = Mod::PlayerShotDamage(shield * focus * (bonusTarget ? Mod::BreachBonus() : 1.0f));
            const int dealt = (std::min)(damage, (std::max)(0, enemy.hp));
            enemy.hp -= damage;
            Mod::NoteDamage(static_cast<float>(damage) / static_cast<float>(Balance::HP_UNIT));
            Mod::NoteDealt(static_cast<float>(dealt) / static_cast<float>(Balance::HP_UNIT), enemy.shield ? 1.0f - Balance::ELITE_SHIELD : 0.0f, bonusTarget, false); // (the run report)

            const bool usesHeavyAudio = UsesHeavyEnemyAudio(enemy.type);

            enemy.hitFlashTimer = 4;

            enemy.damageCooldown = GetEnemyDamageCooldownFrames(enemy.type);

            EnemyArt::OnHit(enemy, bullet.x + bullet.halfWidth, bullet.y, shield < 1.0f);

            if (enemy.hp > 0)
            {
                if (shield < 1.0f)
                {
                    GameAudio::PlayAt(GameSound::BossArmor, enemy.x); // the shot is dampened by the shield: a metal knock
                }
                else
                {
                    // the material of the enemy decides the sound: light fighter, armour, glass, blades (see GameAudio::PlayEnemyHit)
                    GameAudio::PlayEnemyHit(static_cast<int>(enemy.type), usesHeavyAudio, enemy.x, bullet.polarity == Polarity::Red); // (glass or ink: the colour of the shot)
                }

                break;
            }

            enemy.hp = 0;

            const int baseScore = enemy.scoreValue;

            float explosionSize = 0.08f;

            if (enemy.type == EnemyType::Heavy)
            {
                explosionSize = 0.16f;
            }
            else if (enemy.type == EnemyType::Charger)
            {
                explosionSize = 0.11f;
            }
            else if (enemy.type == EnemyType::Turret)
            {
                explosionSize = 0.18f;
            }
            else if (enemy.type == EnemyType::Sniper)
            {
                explosionSize = 0.13f;
            }
            else if (enemy.type == EnemyType::Rival)
            {
                explosionSize = 0.22f;
            }

            GameEffect::SpawnExplosion(enemy.x, enemy.y, explosionSize);

            EnemyArt::OnDeath(enemy);

            if (usesHeavyAudio)
            {
                GameAudio::PlayAt(GameSound::EliteExplosion, enemy.x);
            }
            else
            {
                GameAudio::PlayAt(GameSound::EnemyExplosion, enemy.x);
            }

            if (usesHeavyAudio)
            {
                int shakeDuration = 10;

                float shakeStrength = 0.010f;

                if (enemy.type == EnemyType::Rival)
                {
                    shakeDuration = 16;

                    shakeStrength = 0.016f;
                }

                ScreenShake::Start(shakeDuration, shakeStrength);
            }

            enemy.active = false;

            Mod::OnEnemyKilled(enemy);

            const int multiplier = GameState::GetScoreMultiplier();

            GameState::score += baseScore * multiplier;

            break;
        }
    }
}

void ApplySpecialShotDamage()
{
    constexpr int BOSS_DAMAGE = 1;

    const float shotStartX = GameSpecialShot::GetStartX();

    const float shotEndX = GameSpecialShot::GetEndX();

    const float shotCenterY = GameSpecialShot::GetCenterY();

    const float shotHalfHeight = GameSpecialShot::GetHalfHeight();

    const float shotTop = shotCenterY + shotHalfHeight;

    const float shotBottom = shotCenterY - shotHalfHeight;

    for (int enemyIndex = 0; enemyIndex < GameState::ENEMY_MAX; enemyIndex++)
    {
        Enemy& enemy = GameState::enemies[enemyIndex];

        if (!enemy.active)
        {
            continue;
        }

        const float enemyLeft = enemy.x - enemy.halfWidth;

        const float enemyRight = enemy.x + enemy.halfWidth;

        const float enemyTop = enemy.y + enemy.halfHeight;

        const float enemyBottom = enemy.y - enemy.halfHeight;

        const bool overlapsX = shotEndX >= enemyLeft && shotStartX <= enemyRight;

        const bool overlapsY = shotTop >= enemyBottom && shotBottom <= enemyTop;

        if (!overlapsX || !overlapsY)
        {
            continue;
        }

        if (enemy.shield) // the beam breaks the shield of an elite for good
        {
            enemy.shield = false;
            GameEffect::SpawnShieldBreak(enemy.x, enemy.y, enemy.halfWidth * 1.35f);
            GameEffect::SpawnHit(enemy.x, enemy.y, true);
            GameAudio::PlayAt(GameSound::BossCannonBreak, enemy.x);
        }
        const bool bonusTarget = enemy.elite || enemy.type == EnemyType::Rival;
        const int tick = static_cast<int>(static_cast<float>(Mod::SpecialTickDamage()) * (bonusTarget ? Mod::BreachBonus() : 1.0f));
        const int dealt = (std::min)(tick, (std::max)(0, enemy.hp));
        enemy.hp -= tick;
        Mod::NoteDamage(static_cast<float>(tick) / static_cast<float>(Balance::HP_UNIT));
        Mod::NoteDealt(static_cast<float>(dealt) / static_cast<float>(Balance::HP_UNIT), 0.0f, bonusTarget, true); // (the beam broke the shield above: nothing was reduced)
        enemy.hitFlashTimer = 4;

        if (enemy.hp > 0)
        {
            continue;
        }

        const int baseScore = enemy.scoreValue;

        float explosionSize = 0.10f;

        if (enemy.type == EnemyType::Heavy)
        {
            explosionSize = 0.18f;
        }
        else if (enemy.type == EnemyType::Charger)
        {
            explosionSize = 0.13f;
        }

        GameEffect::SpawnExplosion(enemy.x, enemy.y, explosionSize);

        EnemyArt::OnDeath(enemy);

        GameAudio::PlayAt(GameSound::EnemyExplosion, enemy.x);

        if (enemy.type == EnemyType::Heavy)
        {
            ScreenShake::Start(10, 0.010f);
        }

        enemy.hp = 0;
        enemy.active = false;

        Mod::OnEnemyKilled(enemy);

        const int multiplier = GameState::GetScoreMultiplier();

        GameState::score += baseScore * multiplier;
    }

    // (the score of the boss is paid while its bar goes down: Mod::UpdateBossScore)
    GameBoss::CheckSpecialShotHit(shotStartX, shotEndX, shotCenterY, shotHalfHeight, BOSS_DAMAGE, Mod::SpecialDamageMul());
}

void StartPlayerDeath()
{
    // roguelite: the backup core module revives the player once per run
    if (Mod::TryBackupCore())
    {
        return;
    }

    GameState::playerHp = 0;

    GameState::playerDeathTimer = GameState::DEATH_ANIM_DURATION;

    GameAudio::Play(GameSound::PlayerDeath);
}

bool HurtPlayer(int amount, const char* source)
{
    if (GameState::playerHp <= 0)
    {
        return false;
    }

    amount = Mod::ScaleDamageTaken(amount, source);
    if (amount <= 0)
    {
        // the AEGIS shield took the hit: no damage, a short grace time, a clang
        if (GameState::playerInvincibleTimer < 40) GameState::playerInvincibleTimer = 40;
        GameEffect::SpawnAbsorb(GameState::player.x, GameState::player.y, 0.12f, GameState::player.polarity);
        GameAudio::Play(GameSound::BossArmor);
        ScreenShake::Start(6, 0.006f);
        Mod::LogEvent("blocked", source);
        return true;
    }
    GameState::playerHp -= amount;
    Mod::LogEvent("hurt", source);
    Mod::NoteHit();

    ScreenShake::Start(8 + amount / 8, 0.010f + 0.0002f * static_cast<float>(amount));
    PostFx::Kick(0.012f); // (the colour fringe only flashes up when the ship is hit)

    if (GameState::playerHp <= 0)
    {
        StartPlayerDeath();
    }
    else
    {
        GameAudio::Play(GameSound::PlayerDamage);
        GameState::playerInvincibleTimer = Mod::InvincibleFrames();
    }

    return true;
}

void CheckPlayerEnemyCollision()
{
    if (GameState::playerHp <= 0 || GameState::playerInvincibleTimer > 0)
    {
        return;
    }

    for (int enemyIndex = 0; enemyIndex < GameState::ENEMY_MAX; enemyIndex++)
    {
        if (!GameState::enemies[enemyIndex].active)
        {
            continue;
        }

        const bool isHit = Collision::CheckAABB(
            GameState::player.x, GameState::player.y, GamePlayer::GetHurtHalfWidth(GameState::player), GamePlayer::GetHurtHalfHeight(GameState::player),
            GameState::enemies[enemyIndex].x, GameState::enemies[enemyIndex].y, GameState::enemies[enemyIndex].halfWidth, GameState::enemies[enemyIndex].halfHeight);

        if (!isHit)
        {
            continue;
        }

        // an enemy that flies into the ship is gone; the mini boss (the rival) is not: it hurts the ship and stays
        if (GameState::enemies[enemyIndex].type != EnemyType::Rival) GameState::enemies[enemyIndex].active = false;
        HurtPlayer(Balance::DMG_RAM, "ram");

        return;
    }
}

void CheckPlayerEnemyBulletCollision()
{
    if (GameState::playerHp <= 0)
    {
        return;
    }

    const float siphonScale = Mod::SiphonScale();

    // the rail of an elite sniper (white: it cannot be absorbed, AEGIS stops it)
    if (GameState::playerInvincibleTimer <= 0 && GameEnemy::RailHits(GameState::player, GamePlayer::GetHurtHalfHeight(GameState::player)))
    {
        HurtPlayer(Balance::DMG_ELITE_LASER, "white_sniper_rail");
        if (GameState::playerHp <= 0) return;
    }

    for (int bulletIndex = 0; bulletIndex < GameState::ENEMY_BULLET_MAX; bulletIndex++)
    {
        EnemyBullet& enemyBullet = GameState::enemyBullets[bulletIndex];

        if (!enemyBullet.active)
        {
            continue;
        }

        // a white (neutral) bullet is never the colour of the ship: it cannot be absorbed ...
        bool isSamePolarity = (!enemyBullet.neutral && GameState::player.polarity == enemyBullet.polarity) || enemyBullet.deflected; // (a deflected bullet stays absorbable until it is in the ship)
        // ... unless it grazes a ship that has DEFLECT: the first time it touches the net of the ship (a little wider than the box that absorbs) it is rolled once
        if (!isSamePolarity && enemyBullet.neutral && !enemyBullet.grazed && Mod::DeflectChance() > 0.0f
            && Collision::CheckAABB(GameState::player.x, GameState::player.y, GameState::player.halfWidth * Mod::DeflectNetScale(), GameState::player.halfHeight * Mod::DeflectNetScale(),
                                    enemyBullet.x, enemyBullet.y, enemyBullet.halfWidth, enemyBullet.halfHeight))
        {
            enemyBullet.grazed = true;
            if (Mod::TryDeflect())
            {
                isSamePolarity = true;
                enemyBullet.deflected = true;
                enemyBullet.energyMul = Mod::DeflectEnergyMul();
                GameEffect::SpawnDeflect(enemyBullet.x, enemyBullet.y);
            }
        }

        // same colour: absorbed by the whole ship box; other colour: only the small hurt box hurts
        bool isHit = Collision::CheckAABB(GameState::player.x, GameState::player.y,
                                          isSamePolarity ? GameState::player.halfWidth : GamePlayer::GetHurtHalfWidth(GameState::player),
                                          isSamePolarity ? GameState::player.halfHeight : GamePlayer::GetHurtHalfHeight(GameState::player),
                                          enemyBullet.x, enemyBullet.y, enemyBullet.halfWidth, enemyBullet.halfHeight);

        // siphon: same-colour bullets are absorbed from further away (the effect shows the net taking it)
        bool bySiphon = false;
        if (!isHit && isSamePolarity && siphonScale > 1.0f)
        {
            bySiphon = isHit =
                Collision::CheckAABB(GameState::player.x, GameState::player.y, GameState::player.halfWidth * siphonScale, GameState::player.halfHeight * siphonScale,
                                     enemyBullet.x, enemyBullet.y, enemyBullet.halfWidth, enemyBullet.halfHeight);
        }

        if (!isHit)
        {
            continue;
        }

        enemyBullet.active = false;

        if (isSamePolarity)
        {
            GameEffect::SpawnAbsorb(GameState::player.x, GameState::player.y, 0.075f, enemyBullet.polarity);
            if (bySiphon) GameEffect::SpawnSiphon(enemyBullet.x, enemyBullet.y, enemyBullet.polarity);

            GameAudio::PlayAbsorb(enemyBullet.polarity == Polarity::Red); // the colour of the bullet that was absorbed

            // (the run report: a bullet that only the wide net of SIPHON caught, a white bullet that DEFLECT turned)
            Mod::OnAbsorb(GameState::ENERGY_PER_ABSORB * enemyBullet.energyMul, enemyBullet.neutral ? static_cast<int>(Mod::Id::Deflect) : (bySiphon ? static_cast<int>(Mod::Id::Siphon) : -1));

            continue;
        }

        if (GameState::playerInvincibleTimer > 0)
        {
            continue;
        }

        // (a white bullet is "white_<enemy>" in the run log: AEGIS stops these)
        char source[32];
        std::snprintf(source, sizeof source, "%s%s", enemyBullet.neutral ? "white_" : "", GameEnemy::OwnerName(enemyBullet.owner));
        HurtPlayer(enemyBullet.damage > 0 ? enemyBullet.damage : Balance::DMG_BULLET[0], source);

        return;
    }
}
}







