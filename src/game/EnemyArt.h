#pragma once
#include "game/GameTypes.h"

// The look of the enemies: how a hull moves (tilt, recoil, shake), the emissive layer that makes its plating red or blue, the effects that belong to ONE kind of enemy (the arcs
// between the claws of the turret, the ribbon of the sine, the steam of the heavy ...), and the particles of its damage and death.
//
// Every enemy hull is two pictures: a neutral gunmetal hull (assets/textures/enemies/<name>.png) and an emissive layer (<name>_glow.png): the chevrons, the hazard stripes, the
// edge trim, the lenses and the lights of the engines. The game draws the emissive layer additively in the colour of the enemy (red / blue; white for the sniper, whose bullets
// are white), so a glance at the plating tells which colour an enemy fires, and the layer can pulse, flow, flicker and flare with what the enemy is doing.
//
// Nothing here touches the logic: GameEnemy.cpp hands over what the enemy is doing, EnemyArt draws. The random numbers are its own (the game's std::rand() is not used).
namespace EnemyArt
{
// The margin of the emissive picture, as a share of the sprite, on every side (tools: make_enemy_glow.py): the picture is 1 + 2 * GLOW_PAD times as large as the hull.
constexpr float GLOW_PAD = 0.06f;

struct View
{
    const Enemy* enemy = nullptr;
    float x = 0.0f; // where the hull is drawn (normalized), already interpolated between two steps
    float y = 0.0f;
    float vx = 0.0f; // how far it moved in the last step (normalized units)
    float vy = 0.0f;
    float hw = 0.0f; // half size of the hull picture
    float hh = 0.0f;
    float alpha = 1.0f;
    float r = 1.0f; // the colour that the enemy shows now (the turret flips it before its third volley)
    float g = 1.0f;
    float b = 1.0f;
    float charge = 0.0f; // 0..1: how far its warning has come (0 when it is not warning)
    int frame = 0;       // counts drawn frames: every animation runs off this
};

// Where the hull picture stands this frame: the position of the logic plus the shake, the recoil and the tilt of the enemy.
struct Pose
{
    float x = 0.0f;
    float y = 0.0f;
    float rot = 0.0f; // radians, counter clockwise on the screen (the nose points left at 0)
    float hw = 0.0f;
    float hh = 0.0f;
    // a point of the hull picture (u, v in 0..1, v down) in the world
    void Point(float u, float v, float& px, float& py) const;
    // a point at (lx, ly) from the centre of the hull, in the hull's own frame (normalized x / y units, y up), in the world
    void Local(float lx, float ly, float& px, float& py) const;
};

Pose MakePose(const View& view);
// the size of the hull picture relative to the hit box of the enemy (the picture is a little larger than the box)
void VisualScale(EnemyType type, float& widthScale, float& heightScale);

void Reset(); // a new run: no particles
void Update(Enemy enemies[], int count); // once per world step: the particles move, damaged enemies smoke and spark, heavies steam

// drawing, in this order for every enemy: DrawUnder (behind the hull), the hull, DrawGlow (the emissive layer), DrawOver (in front); DrawParticles once at the end
void DrawUnder(const View& view, const Pose& pose);
void DrawGlow(const View& view, const Pose& pose);
void DrawOver(const View& view, const Pose& pose);
void DrawParticles();

// events
void OnHit(const Enemy& enemy, float hitX, float hitY, bool armourHeld); // a shot of the ship hit it (armourHeld: the shot was of its own colour and an armoured enemy shrugged most of it off)
void OnDeath(const Enemy& enemy);                                       // it is destroyed: debris, blasts and smoke that fit what it is
}
