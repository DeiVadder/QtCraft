// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobbrains/QcMobTeleport.h"

#include "common/content/definitions/QcMobDefinition.h"
#include "common/physics/QcPhysics.h"
#include "server/entities/QcCreature.h"
#include "server/gameplay/QcSafeSpot.h"

#include <cmath>

namespace QcMobTeleport {

bool teleport(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, QRandomGenerator &random,
              const std::optional<QcVec3> &around)
{
    if (!definition.teleport || mob.confined || mob.noAi || mob.teleportCooldown > 0) {
        return false;
    }

    const QcTeleportDefinition &rules = *definition.teleport;
    const QcSafeSpot::Search search{around ? towardsRange : rules.range, rules.attempts, QcSafeSpot::cellsOf(definition.height)};
    const std::optional<QcVec3> target = QcSafeSpot::randomAround(world, around.value_or(mob.motion.position), search, random);
    if (!target) {
        return false;
    }
    land(mob, *target);
    mob.teleportCooldown = static_cast<int>(std::lround(rules.cooldownSeconds / QcPhysics::tickDuration));
    return true;
}

void land(QcCreature &mob, const QcVec3 &target)
{
    // The first place of the tick is the one the clients last saw.
    if (!mob.teleportedFrom) {
        mob.teleportedFrom = mob.motion.position;
    }
    mob.motion.position = target;
    mob.motion.velocity = {};
    mob.motion.onGround = true;
    mob.motion.inWater = false;
    mob.path.clear();
}

} // namespace QcMobTeleport
