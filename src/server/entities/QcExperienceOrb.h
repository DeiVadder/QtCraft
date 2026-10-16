#pragma once

#include "QcEntity.h"

// A ball of experience lying or flying in the world (decision E-398); QcExperienceOrbManager moves,
// attracts, merges and ages them, and players collect them.
struct QcExperienceOrb : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::ExperienceOrb;

    QcExperienceOrb()
        : QcEntity(QcEntityKind::ExperienceOrb)
    {
    }

    // Experience points it gives; merged orbs add up.
    int value = 1;
    // Seconds it has lain in loaded chunks; it goes at QcExperienceOrbManager::despawnSeconds.
    double age = 0.0;
    // Seconds before a player can collect it.
    double pickupDelay = 0.0;
    // Lying still with no player near: skipped by physics until one comes or the block below changes.
    bool resting = false;
};
