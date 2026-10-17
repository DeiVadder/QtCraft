#pragma once

#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcStandPose.h"
#include "server/entities/QcEntity.h"

#include <QString>

// An armour stand (decisions E-238, E-364): wears armour, holds an item and has a pose; it has no
// AI, no health and no physics of its own and stands where it was placed until something pushes it.
struct QcArmourStand : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::ArmourStand;

    // A stand breaks when the same player hits it twice within this time (a single hit only wobbles
    // it). Longer than the attack cooldown (QcServerCombat::playerAttackCooldown), during which hits
    // do not count.
    static constexpr double breakWindow = 1.0;

    QcArmourStand()
        : QcEntity(QcEntityKind::ArmourStand)
    {
    }

    QcStandPose pose;
    QcEquipment equipment;
    QcItemStack held;
    // Shown above it when pose.showName is set; at most QcArmourStandManager::maxNameLength Unicode
    // code points (QcNameText).
    QString name;
    // The cape it wears (an appearance of the kind cape, decision E-239); empty for none.
    QString cape;
    // The item it drops when broken (the one that placed it).
    QString item;
    // Seconds since the last hit; a second hit by `hitBy` within breakWindow breaks it.
    double sinceHit = breakWindow;
    // The player (entity id) whose hit started the break window; 0 for none.
    quint32 hitBy = 0;

    bool operator==(const QcArmourStand &) const = default;
};
