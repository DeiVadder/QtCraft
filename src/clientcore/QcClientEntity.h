#pragma once

#include "QcVec3.h"

#include <QString>

// Client mirror of an entity the server reported (mob or other player).
struct QcClientEntity
{
    quint32 id = 0;
    // Index into the content's mobs, or -1 for a player.
    int mobType = -1;
    QString name;
    QcVec3 position;
    QcVec3 previousPosition;
    float yaw = 0.0f;
    float previousYaw = 0.0f;
    double health = 0.0;
    // Client ticks left for the hurt flash.
    int hurtTicks = 0;
};
