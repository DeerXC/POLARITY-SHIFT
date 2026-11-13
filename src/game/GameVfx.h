#pragma once

namespace GameVfx
{
enum class Sprite
{
    Glow,
    Ring,
    Spark,
    Flame,
    Beam,
    Flow,
    Shard
};

constexpr float ASPECT = 720.0f / 1280.0f;

void Draw(Sprite sprite, float x, float y, float width, float height, float angle, float r, float g, float b, float a);
void DrawRound(Sprite sprite, float x, float y, float size, float r, float g, float b, float a);
void DrawStrip(Sprite sprite, float left, float top, float right, float bottom, float u0, float u1, float r, float g, float b,
               float a);
void DrawAlpha(Sprite sprite, float x, float y, float width, float height, float angle, float r, float g, float b, float a);
void DrawLine(float x1, float y1, float x2, float y2, float thickness, float r, float g, float b, float a);
// a hard-edged bar (a solid quad, no soft ends): the lines of the effects in the style guide. additive = light, else paint
void DrawBar(float x1, float y1, float x2, float y2, float thickness, float r, float g, float b, float a, bool additive = true);
// a hard triangle / diamond (normalized coordinates; y is scaled by ASPECT round the centre, so the shape stays true on screen)
void DrawTri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a, bool additive = true);
void DrawDiamond(float x, float y, float radius, float angle, float r, float g, float b, float a, bool additive = true);

// speed = distance per update step (normalized units). The streak is as long as the bullet is fast:
// a slow bullet is a short bolt, only a fast one (sniper, player shots) is a long needle.
// Enemy bullets come in six kinds; the colour is the polarity (red is always pointed, blue always round):
//   normal  homing (isHoming)  mine (sizeScale > 1.5)  boss    (+ the white bullet: DrawNeutralBullet)
enum BulletStyle { BulletNormal = 0, BulletBoss = 2 };
void DrawEnemyBullet(float x, float y, float angle, bool isRed, bool isHoming, int frame, float speed = 0.012f, float sizeScale = 1.0f, int style = BulletNormal);
// A white bullet (the sniper's): a long needle with a ring around it. It is not red and not blue: it cannot be absorbed.
void DrawNeutralBullet(float x, float y, float angle, int frame, float speed = 0.012f);
void GetPolarityColor(bool isRed, float& r, float& g, float& b);
float Hash(int seed);
}



