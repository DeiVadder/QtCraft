#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <QRandomGenerator>

#include <optional>

struct QcCreature;
struct QcMobDefinition;

// How creatures teleport (QcMobDefinition::teleport, decision E-460): to a random safe place
// (QcSafeSpot::randomAround) within the kind's range around them or around a point (an angry one
// next to its target), at most once per cooldown, never when confined or without a mind (decision
// E-510). The creature lands there at rest with no path; QcCreature::teleportedFrom tells the server
// to announce it.
namespace QcMobTeleport {

// How close to a target an angry creature teleports (metres around the target).
inline constexpr double towardsRange = 4.0;

// Teleports `mob` near `around` (its own place when none); false when its kind does not teleport,
// it is confined or without a mind, still cooling down or finds no safe place.
bool teleport(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, QRandomGenerator &random,
              const std::optional<QcVec3> &around = std::nullopt);
// Puts `mob` at `target` at rest without a path, to be announced as a teleport (QcCreature::
// teleportedFrom), whatever its kind (also a pet next to its owner, decision E-480).
void land(QcCreature &mob, const QcVec3 &target);

} // namespace QcMobTeleport
