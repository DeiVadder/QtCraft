#pragma once

#include "QcMobManager.h"

// How creatures of QcMobManager pick a player to chase and strike it.
namespace QcMobSteering {

// Mobs only notice and attack within this vertical distance.
inline constexpr double verticalReach = 2.0;
// Eyes of a creature at this share of its height (where its line of sight starts).
inline constexpr double eyeShare = 0.85;
// A hostile creature looks for players every this many ticks (staggered by its id), and chases one
// it lost sight of for this many seconds more, along its path around corners (decision E-401).
inline constexpr int sightCheckTicks = 10;
inline constexpr double targetMemorySeconds = 5.0;

// Yaw in degrees for a horizontal direction (0 looks north, -Z).
float yawTowards(const QcVec3 &direction);
// The player the creature chases this tick (QcCreature::chasing), nullptr for none. When it `looks`,
// it picks the nearest living, targetable player within the kind's sight range that it sees from its
// eyes (one ray to the player's eyes, QcLineOfSight::seesPoint) and remembers them for
// targetMemorySeconds; otherwise, or when it sees none, it keeps its target while it still remembers
// them and they are a valid target within sight range.
const QcMobManager::PlayerView *chooseTarget(QcCreature &mob, const QcMobDefinition &definition,
                                             const QList<QcMobManager::PlayerView> &players, const QcBlockAccess &world,
                                             bool looks);
// Whether the target is within the kind's reach and seen from the creature's eyes (QcLineOfSight,
// as for player attacks, decision E-092): no blows through walls or glass.
bool inMelee(const QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
             const QcBlockAccess &world);
// Turns towards a target in melee (inMelee) and strikes it when the attack is ready.
void strike(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
            QList<QcMobManager::PlayerHit> &hits);
// A hostile creature without AI of its own (a crowd, QcMobCrowd) strikes the nearest player in
// melee (inMelee) when its attack is ready, so a knot of creatures is no safe place; true when it struck.
bool strikeAdjacent(QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players,
                    const QcBlockAccess &world, QList<QcMobManager::PlayerHit> &hits);

} // namespace QcMobSteering
