// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobbrains/QcBarterSight.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/mobbrains/QcMobSteering.h"

#include <algorithm>
#include <limits>

namespace {

// Whether `player` may be its foe at all and how far away he is (negative: not a foe).
double foeDistance(const QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &player, const QcContentRegistry &content)
{
    if (!definition.barter || !player.alive || !player.targetable || QcBarterSight::spares(player, *definition.barter, content)) {
        return -1.0;
    }
    const double distance = (player.position - mob.motion.position).length();
    return distance <= definition.sightRange * player.sightShare ? distance : -1.0;
}

} // namespace

namespace QcBarterSight {

bool spares(const QcMobManager::PlayerView &player, const QcBarterDefinition &barter, const QcContentRegistry &content)
{
    return std::ranges::any_of(player.worn, [&](QcItemId item) { return content.isValidItem(item) && barter.sparedBy.contains(content.item(item).name); });
}

const QcMobManager::PlayerView *foe(const QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context, bool looks)
{
    const QcContentRegistry &content = context.world.content();
    const QList<QcMobManager::PlayerView> &players = *context.players;
    if (!looks) {
        const auto chased = std::ranges::find(players, mob.chasing, &QcMobManager::PlayerView::entityId);
        return mob.chasing != 0 && chased != players.end() && foeDistance(mob, definition, *chased, content) >= 0.0 ? &*chased : nullptr;
    }
    const QcVec3 eyes = mob.motion.position + QcVec3{0.0, definition.height * QcMobSteering::eyeShare, 0.0};
    const QcMobManager::PlayerView *nearest = nullptr;
    double best = std::numeric_limits<double>::max();
    for (const QcMobManager::PlayerView &player : players) {
        const double distance = foeDistance(mob, definition, player, content);
        if (distance >= 0.0 && distance < best
            && QcLineOfSight::seesPoint(context.world, eyes, player.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0})) {
            nearest = &player;
            best = distance;
        }
    }
    return nearest;
}

} // namespace QcBarterSight
