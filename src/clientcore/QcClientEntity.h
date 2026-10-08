#pragma once

#include "QcItemStack.h"
#include "QcVec3.h"

#include <QString>

// Client mirror of an entity the server reported (mob, other player or dropped item).
struct QcClientEntity
{
    quint32 id = 0;
    // Index into the content's mobs, -1 for a player, -2 for a dropped item (see QcMsg::EntitySpawn).
    int mobType = -1;
    // The stack of a dropped item; empty otherwise.
    QcItemStack item;
    QString name;
    QcVec3 position;
    QcVec3 previousPosition;
    float yaw = 0.0f;
    float previousYaw = 0.0f;
    double health = 0.0;
    // Client ticks left for the hurt flash.
    int hurtTicks = 0;

    bool isItem() const { return !item.isEmpty(); }
};
