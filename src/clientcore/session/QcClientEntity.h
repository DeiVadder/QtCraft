#pragma once

#include "QcAppearance.h"
#include "QcBlockDefinition.h"
#include "QcEquipment.h"
#include "QcItemStack.h"
#include "QcStandPose.h"
#include "QcVec3.h"

#include <QString>

// Client mirror of an entity the server reported (mob, other player or dropped item).
struct QcClientEntity
{
    quint32 id = 0;
    // Index into the content's mobs, -1 for a player, -2 for a dropped item, -3 for a falling block,
    // -4 for an armour stand (see QcMsg::EntitySpawn).
    int mobType = -1;
    // The block of a falling block; air otherwise.
    QcBlockId block = qcAirBlockId;
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
    // Players: their look; all creatures: head pitch, QcMsg::EntityFlag bits and the held item.
    QcAppearance appearance;
    float pitch = 0.0f;
    float previousPitch = 0.0f;
    quint8 flags = 0;
    QcItemId held = qcNoItem;
    // Players and armour stands: their armour slots.
    QcEquipment equipment;
    // Armour stands: pose and what they show.
    QcStandPose stand;
    // Distance walked (drives the stride) and the last step's length per tick.
    double walkDistance = 0.0;
    double lastStep = 0.0;
    // Horizontal distance of all moves since the last client tick, and the walking speed in m/s
    // smoothed over a few ticks (server and client ticks are not in phase).
    double stepThisTick = 0.0;
    double speed = 0.0;
    // Distance walked since the entity's last step sound.
    double sinceStep = 0.0;
    // Client ticks since the arm swing or the death started; negative when not running.
    int swingTicks = -1;
    int deathTicks = -1;
    // A move arrived since the last client tick.
    bool moved = false;

    bool isItem() const { return !item.isEmpty(); }
    bool isPlayer() const { return mobType == -1; }
    bool isFallingBlock() const { return mobType == -3; }
    bool isArmourStand() const { return mobType == -4; }
};
