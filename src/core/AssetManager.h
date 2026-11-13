#pragma once

// Every texture of the game is loaded once, in Initialize(), so a missing file is found before the game opens.
namespace AssetManager
{
enum class Tex
{
    PlayerRed,
    PlayerBlue,
    EnemyScout,
    EnemySine,
    EnemyHeavy,
    EnemyCharger,
    EnemyTurret,
    EnemySniper,
    EnemyRival,
    BossHull,
    BossHullBare,
    BossHullBerserk,
    BossPodRed,
    BossPodBlue,
    Bullets,
    FxGlow,
    FxRing,
    FxSpark,
    FxFlame,
    FxBeam,
    FxFlow,
    FxShard,
    HudAtlas,
    ModuleIcons,
    // the emissive layer of every enemy (assets/textures/enemies/<name>_glow.png, drawn additively in the colour of the enemy), in the order of EnemyType
    EnemyScoutGlow,
    EnemySineGlow,
    EnemyHeavyGlow,
    EnemyChargerGlow,
    EnemyTurretGlow,
    EnemySniperGlow,
    EnemyRivalGlow,
    BossRail, // the rail gun of the boss (slides out of its central pod for the beam attacks)
    TitleShipRed,  // the hero ship of the title screen (768 x 768, the same model as the game ship)
    TitleShipBlue,
    Galaxy, // the Milky Way of the fight background: a simulated all-round panorama in three flat tones, 7200 x 1600 (tools/make_galaxy.py), drawn dim and drifting slowly
    Count
};

// Loads all textures. When a file is missing or unreadable a message box lists them and false is returned.
bool Initialize();

// Texture handle for Renderer2D.
int Get(Tex tex);
// The emissive layer of an enemy: enemyType = static_cast<int>(EnemyType). The picture has a margin of 6 % of the sprite on every side (the bloom), see EnemyArt::GLOW_PAD.
int EnemyGlow(int enemyType);

// Cells of the bullet atlas (512 x 256, 4 x 2 cells of 128 px).
enum BulletCell
{
    RedSolid = 0,
    RedHollow = 1,
    BlueSolid = 2,
    BlueHollow = 3,
    MarkerRed = 4,
    MarkerBlue = 5
};

void BulletUv(int cell, float& u0, float& v0, float& u1, float& v1);
}





