// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/bosses/QcHydraSummoning.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerState.h"
#include "server/golems/QcServerGolems.h"
#include "server/mobbrains/QcMobSteering.h"

#include <cmath>

namespace {

// The name of the loaded block at `pos`, empty for none.
QString nameAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    return block && world.content().isValidBlock(*block) ? world.content().block(*block).name : QString();
}

// The T whose middle head is `head`, its arms along (dx, dz), if complete.
std::optional<QcHydraSummoning::Shape> shapeAround(const QcBlockAccess &world, QcBlockPos head, int dx, int dz, const QcHydraDefinition &hydra)
{
    const QcBlockPos middle = head.offset(0, -1, 0);
    const QcBlockPos foot = head.offset(0, -2, 0);
    const QcHydraSummoning::Shape shape{foot,
                                        dx != 0,
                                        {head.offset(-dx, 0, -dz), head, head.offset(dx, 0, dz), middle.offset(-dx, 0, -dz), middle,
                                         middle.offset(dx, 0, dz), foot}};
    for (std::size_t i = 0; i < 3; ++i) {
        if (nameAt(world, shape.cells[i]) != hydra.summonHead) {
            return std::nullopt;
        }
    }
    for (std::size_t i = 3; i < shape.cells.size(); ++i) {
        if (!hydra.summonBody.contains(nameAt(world, shape.cells[i]))) {
            return std::nullopt;
        }
    }
    // Beside the foot only air (the role model's pattern).
    if (world.blockAt(foot.offset(-dx, 0, -dz)) != qcAirBlockId || world.blockAt(foot.offset(dx, 0, dz)) != qcAirBlockId) {
        return std::nullopt;
    }
    return shape;
}

} // namespace

QcHydraSummoning::QcHydraSummoning(QcServerState &state, QcServerBlocks &blocks)
    : m_state(state)
    , m_blocks(blocks)
{
}

std::optional<QcHydraSummoning::Shape> QcHydraSummoning::shapeAt(const QcBlockAccess &world, QcBlockPos placed, const QcHydraDefinition &hydra)
{
    if (nameAt(world, placed) != hydra.summonHead) {
        return std::nullopt;
    }
    // The placed head is the left, middle or right one of a T along x or along z.
    for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{0, 1}}) {
        for (int along = -1; along <= 1; ++along) {
            if (std::optional<Shape> shape = shapeAround(world, placed.offset(-along * dx, 0, -along * dz), dx, dz, hydra)) {
                return shape;
            }
        }
    }
    return std::nullopt;
}

std::optional<quint32> QcHydraSummoning::summon(QcBlockPos pos)
{
    const QcContentRegistry &content = m_state.content();
    for (const QcCreatureKindId type : content.mobs().ids()) {
        const QcMobDefinition &definition = content.mobs()[type];
        const std::optional<Shape> shape = definition.hydra ? shapeAt(m_state.world(), pos, *definition.hydra) : std::nullopt;
        if (!shape) {
            continue;
        }

        // Like a golem's, the blocks give their life to the hydra.
        const quint32 id = QcServerGolems::wake(m_state, m_blocks, type, shape->cells, shape->foot);
        if (QcCreature *hydra = m_state.entities().creatures().find(id); hydra && hydra->hydra) {
            // It faces across its T and charges with a third of its health (the role model's).
            hydra->yaw = QcMobSteering::yawTowards(shape->alongX ? QcVec3{0.0, 0.0, 1.0} : QcVec3{1.0, 0.0, 0.0});
            hydra->health = definition.maxHealth * QcHydraDefinition::chargeHealthShare;
            hydra->hydra->chargeTicks = std::max(1, static_cast<int>(std::lround(definition.hydra->chargeSeconds / QcPhysics::tickDuration)));
        }
        for (const auto &client : m_state.clients()) {
            const QcVec3 offset = client->motion.position - QcVec3{shape->foot.x + 0.5, static_cast<double>(shape->foot.y), shape->foot.z + 0.5};
            if (client->hasBody() && offset.length() <= summonReach) {
                m_state.advancements().trigger(*client, {.trigger = QcAdvancementTrigger::EntitySummoned, .entity = definition.name});
            }
        }
        return id;
    }
    return std::nullopt;
}
