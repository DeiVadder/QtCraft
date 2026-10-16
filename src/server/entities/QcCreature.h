#pragma once

#include "QcEntity.h"
#include "QcMobPath.h"
#include "QcVec3.h"
#include "QcVillager.h"

// A creature (decision E-364): a mob of the content's definitions (`type` indexes them) with its
// health, behaviour timers and breeding state. QcMobManager moves and spawns them.
struct QcCreature : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::Creature;

    QcCreature()
        : QcEntity(QcEntityKind::Creature)
    {
    }

    int type = 0;
    double health = 0.0;
    double attackCooldown = 0.0;
    double hurtTimer = 0.0;
    double wanderTimer = 0.0;
    QcVec3 wanderDirection;
    double fleeTimer = 0.0;
    bool hurtThisTick = false;
    // Struck at a player this tick (animation).
    bool attackedThisTick = false;
    bool blockedLastTick = false;
    // Spawned on a cave floor: night creatures there stay in daylight.
    bool underground = false;
    // Never despawns and is saved with the world (peaceful animals).
    bool persistent = false;
    // Spawned by a rule for the night only: leaves by day like night creatures.
    bool nightRule = false;
    // Entity id of the player it chases; 0 for none. It chases a player it sees and keeps chasing for
    // `targetMemory` seconds after it lost sight of them (decision E-401).
    quint32 chasing = 0;
    double targetMemory = 0.0;
    // The speed it decided on when it last thought: a throttled creature in a crowd coasts along its
    // path at it between its thoughts (decisions E-401, E-402).
    double lastSpeed = 0.0;
    // Its goal and the path there (decision E-400); not saved, a loaded creature finds a new one.
    QcMobPath path;
    // Breeding (decision E-293): seconds left in love, until it may breed again and until a
    // young one grows up (> 0: young).
    double love = 0.0;
    double breedCooldown = 0.0;
    double growUp = 0.0;
    // Born from breeding (counted against QcMobBreeding::maxBredPerKind).
    bool bred = false;
    // Came by /summon (counted against QcServerLimits::summonLimit); not saved.
    bool summoned = false;
    // Home, work and routine of a villager (QcMobDefinition::villager, decision E-411); unused for
    // other kinds.
    QcVillager villager;

    bool isYoung() const { return growUp > 0.0; }
};
