#include "clientcore/player/QcBlockForecast.h"

#include "common/content/definitions/QcItemDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcRaycast.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Where the block `state` placed at `pos` takes room: its collision boxes there and, for a door or
// bed, at its other half too (as the server checks, QcServerInteraction::mayPlace).
QList<QcAabb> roomOf(const QcBlockAccess &world, QcBlockId state, QcBlockPos pos)
{
    const QcContentRegistry &content = world.content();
    const QcBlockDefinition &definition = content.block(state);
    const QcBlockPos other = QcBlockRules::otherHalf(world, pos, state);
    const QcVec3 corner{double(pos.x), double(pos.y), double(pos.z)};
    const QcVec3 toOther{double(other.x - pos.x), double(other.y - pos.y), double(other.z - pos.z)};

    QList<QcAabb> boxes;
    for (const QcAabb &box : content.state(state).collision) {
        boxes.append(box.moved(corner));
        if (definition.door || definition.bed) {
            boxes.append(box.moved(corner + toOther));
        }
    }
    return boxes;
}

// The blocks of a placement: the cell and the second half of a door or bed (as the server sets them,
// QcServerInteraction::applyPlacement).
QList<QcMsg::BlockUpdate> placedCells(const QcBlockAccess &world, QcBlockId state, QcBlockPos pos)
{
    const QcContentRegistry &content = world.content();
    const QcBlockDefinition &definition = content.block(state);
    QList<QcMsg::BlockUpdate> cells{{pos, state}};
    if (definition.door) {
        cells.append({pos.offset(0, 1, 0), content.withProperty(state, u"half"_s, u"top"_s)});
    }
    if (definition.bed) {
        cells.append({QcBlockRules::otherHalf(world, pos, state), content.withProperty(state, u"part"_s, u"head"_s)});
    }
    return cells;
}

} // namespace

QList<QcMsg::BlockUpdate> QcBlockForecast::breaking(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block || *block == qcAirBlockId || !world.content().isValidBlock(*block)) {
        return {};
    }

    QList<QcMsg::BlockUpdate> cells{{pos, qcAirBlockId}};
    const QcBlockDefinition &definition = world.content().block(*block);
    if ((definition.door || definition.bed) && QcBlockRules::hasPartner(world, pos, *block)) {
        cells.append({QcBlockRules::otherHalf(world, pos, *block), qcAirBlockId});
    }
    return cells;
}

bool QcBlockForecast::placesPlainly(const QcItemDefinition &held)
{
    return !held.placesBlock.isEmpty() && held.emptiesTo.isEmpty() && !held.scoops && !held.bottles && !held.placesStand && !held.hangs
           && !held.placesCart && !held.ignites && held.dye.isEmpty() && held.food == 0 && !held.portableContainer;
}

QList<QcMsg::BlockUpdate> QcBlockForecast::placing(const QcBlockAccess &world, const QcItemDefinition &held, const QcBlockRules::Aim &aim,
                                                   const QcVec3 &eye, const QList<QcAabb> &bodies)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> placed = placesPlainly(held) ? content.blockId(held.placesBlock) : std::nullopt;
    const std::optional<QcBlockId> clicked = world.blockAt(aim.clicked);
    if (!placed || !clicked || *clicked == qcAirBlockId || !content.isValidBlock(*clicked) || content.block(*clicked).liquid) {
        return {};
    }

    const QcBlockDefinition &placedDefinition = content.block(*placed);
    if (placedDefinition.sapling.isSapling() || placedDefinition.floats) {
        return {};
    }

    const std::optional<QcBlockRules::Placement> placement = QcBlockRules::placement(world, *placed, aim);
    if (!placement || !world.blockAt(placement->pos) || !QcRaycast::isWithinReach(eye, placement->pos)) {
        return {};
    }

    const QList<QcAabb> room = roomOf(world, placement->state, placement->pos);
    const bool blocked = std::ranges::any_of(room, [&bodies](const QcAabb &box) {
        return std::ranges::any_of(bodies, [&box](const QcAabb &body) { return body.intersects(box); });
    });
    return blocked ? QList<QcMsg::BlockUpdate>{} : placedCells(world, placement->state, placement->pos);
}
