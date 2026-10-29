// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobs/QcMobManager.h"

#include <optional>

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

// A leaper (QcMobDefinition::leapSpeed, decision E-451) leaps at a target between these distances
// on the ground and at most a block above or below it, with this chance in each tick it thinks, this
// fast upwards.
inline constexpr double leapMin = 2.0;
inline constexpr double leapMax = 4.0;
inline constexpr double leapChance = 0.2;
inline constexpr double leapLift = 6.0;
// Seconds a creature neutral by day stays angry at the player that hurt it (decision E-451).
inline constexpr double provokedSeconds = 30.0;

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
// A creature neutral by day turns on the player whose blow or shot hurt it since it last thought
// (QcCreature::struckBy, decision E-451) for provokedSeconds; damage without a player (a fall, fire)
// provokes nobody. The anger ends early when that player is gone, dead or no target.
void provoke(QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players);
// The player an angry creature neutral by day hunts, whatever the time of day; nullptr for none.
const QcMobManager::PlayerView *provoker(const QcCreature &mob, const QcMobDefinition &definition,
                                         const QList<QcMobManager::PlayerView> &players);
// Whether a creature neutral by day leaves players alone now (decision E-451): by day, above ground
// (not spawned in a cave) and not angry at a player (provoke).
bool calm(const QcCreature &mob, const QcMobDefinition &definition, double worldTime);
// A leaper on the ground leaps at a target within its range now and then; true when it leapt.
bool leap(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target, QRandomGenerator &random);
// Turns towards a target in melee (inMelee) and strikes it when the attack is ready; the hit, if any.
[[nodiscard]] std::optional<QcMobManager::PlayerHit> strike(QcCreature &mob, const QcMobDefinition &definition,
                                                            const QcMobManager::PlayerView &target);
// A hostile creature without AI of its own (a crowd, QcMobCrowd) strikes the nearest player in
// melee (inMelee) when its attack is ready, so a knot of creatures is no safe place (a neutral one only
// the player that provoked it).
struct Melee
{
    // A player was in melee: the creature struck at him.
    bool struck = false;
    // The blow, when it landed.
    std::optional<QcMobManager::PlayerHit> hit;
};
[[nodiscard]] Melee strikeAdjacent(QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players,
                                   const QcBlockAccess &world);

} // namespace QcMobSteering
