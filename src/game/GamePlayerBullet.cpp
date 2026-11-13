#include "game/GamePlayerBullet.h"
#include "game/GameVfx.h"
#include "core/GameTimer.h"

using GameVfx::Sprite;

namespace
{
constexpr float BULLET_SPEED = 0.030f;
constexpr float BULLET_HALF_WIDTH = 0.040f;
constexpr float BULLET_HALF_HEIGHT = 0.012f;

Bullet g_PlayerBullets[GamePlayerBullet::MAX_COUNT]{};
int g_BulletAge[GamePlayerBullet::MAX_COUNT]{};
float g_PrevX[GamePlayerBullet::MAX_COUNT]{}; // x at the start of the last step (render interpolation)
}

void GamePlayerBullet::Initialize()
{
    for (int i = 0; i < MAX_COUNT; i++)
    {
        g_PlayerBullets[i] = {};
        g_PlayerBullets[i].active = false;
        g_BulletAge[i] = 0;
    }
}

bool GamePlayerBullet::Spawn(float x, float y, Polarity polarity)
{
    for (int i = 0; i < MAX_COUNT; i++)
    {
        Bullet& bullet = g_PlayerBullets[i];
        if (bullet.active)
        {
            continue;
        }

        bullet.active = true;
        bullet.x = x;
        bullet.y = y;
        bullet.halfWidth = BULLET_HALF_WIDTH;
        bullet.halfHeight = BULLET_HALF_HEIGHT;
        bullet.speed = BULLET_SPEED;
        bullet.polarity = polarity;
        g_BulletAge[i] = 0;
        g_PrevX[i] = x;
        return true;
    }
    return false;
}

void GamePlayerBullet::BeginStep()
{
    for (int i = 0; i < MAX_COUNT; i++)
    {
        g_PrevX[i] = g_PlayerBullets[i].x;
    }
}

void GamePlayerBullet::Update()
{
    for (int i = 0; i < MAX_COUNT; i++)
    {
        Bullet& bullet = g_PlayerBullets[i];
        if (!bullet.active)
        {
            continue;
        }

        bullet.x += bullet.speed;
        g_BulletAge[i]++;

        if (bullet.x > 1.20f)
        {
            bullet.active = false;
        }
    }
}

void GamePlayerBullet::Draw()
{
    for (int i = 0; i < MAX_COUNT; i++)
    {

        const Bullet& bullet = g_PlayerBullets[i];
        if (!bullet.active)
        {
            continue;
        }

        float r, g, b;
        GameVfx::GetPolarityColor(bullet.polarity == Polarity::Red, r, g, b);

        const int age = g_BulletAge[i];
        const float stretch = age < 4 ? 0.45f + static_cast<float>(age) * 0.14f : 1.0f;
        const float bulletX = GameTimer::Blend(g_PrevX[i], bullet.x); // between the last two steps
        const float head = bulletX + 0.03f;
        const float length = 0.16f * stretch;

        // (the style: the ship's own shots are thin and quiet - the enemy bullets are what the eye must find)
        GameVfx::Draw(Sprite::Spark, head - length * 0.5f, bullet.y, length, 0.026f, 0.0f, r, g, b, 0.42f);
        GameVfx::Draw(Sprite::Spark, head - length * 0.40f, bullet.y, length * 0.80f, 0.008f, 0.0f, 1.0f, 1.0f, 1.0f, 0.80f);
        GameVfx::Draw(Sprite::Spark, head - length * 0.95f, bullet.y, length * 1.3f, 0.007f, 0.0f, r, g, b, 0.14f); // faint long trail

        if (age < 3)
        {
            const float flash = 1.0f - static_cast<float>(age) / 3.0f;
            const float spawnX = bulletX - static_cast<float>(age) * BULLET_SPEED;
            GameVfx::DrawRound(Sprite::Glow, spawnX, bullet.y, 0.035f * flash + 0.01f, r, g, b, flash * 0.45f);
        }
    }
}

Bullet* GamePlayerBullet::GetBullets()
{
    return g_PlayerBullets;
}

int GamePlayerBullet::GetBulletCount()
{
    return MAX_COUNT;
}


