// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/blockrules/QcSafeStand.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcBlockAccess.h"

#include <optional>

namespace {

// The definition at `pos`, none when its chunk is not loaded or the id is unknown.
const QcBlockDefinition *definitionAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block || !world.content().isValidBlock(*block)) {
        return nullptr;
    }
    return &world.content().block(*block);
}

// Whether a body may be in a cell of `definition`: nothing solid, liquid, hurting or slowing.
bool passable(const QcBlockDefinition &definition)
{
    return !definition.solid && !definition.liquid && definition.contactDamage <= 0.0 && !definition.fire.isFire && definition.slowsInside >= 1.0;
}

// Whether `definition` hurts or burns what comes near: lava, fire, magma, a lit campfire.
bool hazard(const QcBlockDefinition &definition)
{
    return definition.isLava() || definition.fire.isFire || definition.contactDamage > 0.0;
}

} // namespace

namespace QcSafeStand {

bool roomAt(const QcBlockAccess &world, QcBlockPos feet)
{
    const QcBlockDefinition *ground = definitionAt(world, feet.offset(0, -1, 0));
    const QcBlockDefinition *body = definitionAt(world, feet);
    const QcBlockDefinition *head = definitionAt(world, feet.offset(0, 1, 0));
    if (!ground || !body || !head) {
        return false;
    }
    const bool firm = ground->solid && !ground->liquid && ground->contactDamage <= 0.0 && ground->hardness >= 0.0;
    return firm && passable(*body) && passable(*head);
}

bool fits(const QcBlockAccess &world, QcBlockPos feet)
{
    if (!roomAt(world, feet)) {
        return false;
    }
    // One layer below the feet to one above the head, hazardRadius to each side.
    constexpr int side = 2 * hazardRadius + 1;
    constexpr int layers = 4;
    for (int cell = 0; cell < side * side * layers; ++cell) {
        const QcBlockPos at = feet.offset(cell % side - hazardRadius, cell / (side * side) - 1, cell / side % side - hazardRadius);
        const QcBlockDefinition *near = definitionAt(world, at);
        if (!near || hazard(*near)) {
            return false;
        }
    }
    return true;
}

} // namespace QcSafeStand
