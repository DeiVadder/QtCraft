#pragma once

#include "QcMobManager.h"

// How creatures of QcMobManager pick a player to chase and steer towards it.
namespace QcMobSteering {

// Mobs only notice and attack within this vertical distance.
inline constexpr double verticalReach = 2.0;

// Yaw in degrees for a horizontal direction (0 looks north, -Z).
float yawTowards(const QcVec3 &direction);
// The nearest living, targetable player within the kind's sight range; nullptr when none.
const QcMobManager::PlayerView *nearestTarget(const QcMobManager::Mob &mob, const QcMobDefinition &definition,
                                              const QList<QcMobManager::PlayerView> &players);
// Steering towards the target; strikes it when it is in reach.
QcPhysics::Steering chase(QcMobManager::Mob &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                          QList<QcMobManager::PlayerHit> &hits);

} // namespace QcMobSteering
