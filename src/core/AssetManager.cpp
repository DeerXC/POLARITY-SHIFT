#include "core/AssetManager.h"
#include "core/Renderer2D.h"

#include <Windows.h>

#include <string>

namespace
{
constexpr int TEXTURE_COUNT = static_cast<int>(AssetManager::Tex::Count);

// One path per AssetManager::Tex value, in the same order.
const wchar_t* const TEXTURE_PATHS[TEXTURE_COUNT] = {
    L"assets/textures/player/ship_red.png",   L"assets/textures/player/ship_blue.png", L"assets/textures/enemies/scout.png",
    L"assets/textures/enemies/sine.png",      L"assets/textures/enemies/heavy.png",    L"assets/textures/enemies/charger.png",
    L"assets/textures/enemies/turret.png",    L"assets/textures/enemies/sniper.png",
    L"assets/textures/enemies/rival.png",     L"assets/textures/boss/hull.png",        L"assets/textures/boss/hull_bare.png",
    L"assets/textures/boss/hull_berserk.png", L"assets/textures/boss/pod_red.png",     L"assets/textures/boss/pod_blue.png",
    L"assets/textures/bullets/atlas.png",     L"assets/textures/effects/glow.png",     L"assets/textures/effects/ring.png",
    L"assets/textures/effects/spark.png",     L"assets/textures/effects/flame.png",    L"assets/textures/effects/beam.png",
    L"assets/textures/effects/flow.png",      L"assets/textures/effects/shard.png",    L"assets/textures/ui/hud_atlas.png",
    L"assets/textures/ui/module_icons.png",
    L"assets/textures/enemies/scout_glow.png",   L"assets/textures/enemies/sine_glow.png",    L"assets/textures/enemies/heavy_glow.png",
    L"assets/textures/enemies/charger_glow.png", L"assets/textures/enemies/turret_glow.png",  L"assets/textures/enemies/sniper_glow.png",
    L"assets/textures/enemies/rival_glow.png",
    L"assets/textures/boss/rail.png",
    L"assets/textures/player/title_ship_red.png", L"assets/textures/player/title_ship_blue.png",
    L"assets/textures/effects/galaxy.png"};

int g_Textures[TEXTURE_COUNT];
}

bool AssetManager::Initialize()
{
    std::string missing;

    for (int i = 0; i < TEXTURE_COUNT; i++)
    {
        g_Textures[i] = Renderer2D_LoadTexture(TEXTURE_PATHS[i]);

        if (g_Textures[i] < 0)
        {
            missing += "\n  ";
            for (const wchar_t* c = TEXTURE_PATHS[i]; *c != L'\0'; ++c)
            {
                missing += static_cast<char>(*c);
            }
        }
    }

    if (!missing.empty())
    {
        const std::string message = "These game files are missing or cannot be read:" + missing;
        MessageBoxA(nullptr, message.c_str(), "POLARITY SHIFT", MB_OK | MB_ICONERROR);
        return false;
    }

    return true;
}

int AssetManager::Get(Tex tex)
{
    const int index = static_cast<int>(tex);
    if (index < 0 || index >= TEXTURE_COUNT)
    {
        return -1;
    }

    return g_Textures[index];
}

int AssetManager::EnemyGlow(int enemyType)
{
    if (enemyType < 0 || enemyType > 7)
    {
        return -1;
    }
    return Get(static_cast<Tex>(static_cast<int>(Tex::EnemyScoutGlow) + enemyType));
}

void AssetManager::BulletUv(int cell, float& u0, float& v0, float& u1, float& v1)
{
    // A 1 px inset keeps bilinear filtering inside the cell.
    const float column = static_cast<float>(cell % 4);
    const float row = static_cast<float>(cell / 4);

    u0 = (column * 128.0f + 1.0f) / 512.0f;
    u1 = (column * 128.0f + 127.0f) / 512.0f;
    v0 = (row * 128.0f + 1.0f) / 256.0f;
    v1 = (row * 128.0f + 127.0f) / 256.0f;
}





