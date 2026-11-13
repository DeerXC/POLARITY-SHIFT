#include "core/Collision.h"

#include <cmath>

bool Collision::CheckAABB(float ax, float ay, float aHalfWidth, float aHalfHeight, float bx, float by, float bHalfWidth,
                          float bHalfHeight)
{
    const float distanceX = std::fabs(ax - bx);

    const float distanceY = std::fabs(ay - by);

    const float limitX = aHalfWidth + bHalfWidth;

    const float limitY = aHalfHeight + bHalfHeight;

    return distanceX <= limitX && distanceY <= limitY;
}

