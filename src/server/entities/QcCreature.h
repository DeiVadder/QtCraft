#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcVec3.h"
#include "common/physics/QcPose.h"
#include "server/entities/QcComponent.h"
#include "server/entities/QcEntity.h"
#include "server/entities/QcHurtWindow.h"
#include "server/mobs/QcArcherState.h"
#include "server/pathing/QcMobPath.h"
#include "server/villages/QcVillager.h"

#include <optional>

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
    // The short invulnerability after a hurt (game rule mobInvulnerabilityTicks, decision E-429).
    QcHurtWindow hurt;
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
    // The persistent id of a creature that is saved (decision E-420; 0 for the others): unique in its
    // world and kept across saving, loading and unloading, unlike the entity id (QcEntity::id), which
    // only holds while the world is open. Villages claim beds and workstations under it; other saved
    // data refers to a creature by it (QcMobManager::nextUid gives them out).
    quint64 uid = 0;
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
    // How it holds itself (decision E-423): a villager lies in its bed, where the physics leaves it
    // (decision E-411). Its box (QcEntities::boxOf), its eyes and its animation follow from it; not
    // saved, a loaded creature stands.
    QcPose pose = QcPose::Standing;
    // Came by /summon (counted against QcServerLimits::summonLimit); not saved.
    bool summoned = false;
    // Home, work and routine of a villager (QcMobDefinition::villager, decision E-411): a component
    // only creatures of a villager kind have (decision E-423).
    QcComponent<QcVillager> villager;
    // The draw and aim of an archer (QcMobDefinition::Ai::Archer, decision E-426): a component only
    // creatures with the archer AI have.
    QcComponent<QcArcherState> archer;
    // The block a creature that carries blocks holds (QcMobDefinition::carries, decision E-460):
    // its state (qcAirBlockId: none) and the item of the block, shown in its hand. Saved with it.
    QcBlockId carried = qcAirBlockId;
    QcItemId carriedItem = qcNoItem;
    // Kept with the world only for the block it carries (QcCreatureTraits made it persistent when it
    // took the block): it is let go again when it puts the block down. A creature that was kept
    // before keeps that. Saved with it.
    bool keptForBlock = false;
    // Stays where a structure put it (QcStructureDefinition::Mob::confined, decision E-460): it
    // neither teleports nor takes or places blocks. Saved with it.
    bool confined = false;
    // Game ticks until it may teleport again (QcTeleportDefinition::cooldownSeconds); not saved.
    int teleportCooldown = 0;
    // Where it teleported from since the creatures' last tick: QcCreatureTraits plays the sounds and
    // shows the clients the new place at once (not slid there); not saved.
    std::optional<QcVec3> teleportedFrom;
    // A neutral creature (QcMobDefinition::Ai::Neutral) chases the player that provoked it for
    // `angerTimer` seconds more (renewed while it sees them); 0 for none. Not saved.
    quint32 provokedBy = 0;
    double angerTimer = 0.0;
    // The player whose blow or shot hurt it last (QcMobManager::damage; 0: none, or damage without
    // a player such as lightning, fire or water): a neutral creature turns on that player when it
    // next thinks, which clears it (other brains leave it); not saved.
    quint32 struckBy = 0;

    bool isYoung() const { return growUp > 0.0; }
    // Whether it is a villager: it has the villager component (QcMobManager::spawn and restore give
    // it to every creature of a villager kind).
    bool isVillager() const { return villager.has(); }
};
