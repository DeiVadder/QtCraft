#include "server/signal/QcPistonMover.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcBodyFit.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcBlockRules.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/signal/QcSignalTable.h"

#include <algorithm>
#include <array>
#include <cmath>


using namespace Qt::StringLiterals;

namespace {

double along(const QcVec3 &v, QcFaceOffset axis)
{
    return v.x * axis.dx + v.y * axis.dy + v.z * axis.dz;
}

QcVec3 centreOf(QcBlockPos pos)
{
    return {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
}

} // namespace

QcPistonMover::QcPistonMover(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, const QcSignalTable &table)
    : m_state(state)
    , m_blocks(blocks)
    , m_sounds(sounds)
    , m_table(table)
{
}

qsizetype QcPistonMover::apply(const QcPistonPlan &plan)
{
    if (plan.extend) {
        extend(plan);
    } else {
        retract(plan);
    }
    return plan.moved.size() + plan.broken.size() + m_bodiesLookedAt;
}

QcBlockId QcPistonMover::headState(const QcPistonPlan &plan) const
{
    const QcContentRegistry &content = m_table.content();
    const std::optional<QcBlockId> base = m_state.world().blockAt(plan.base);
    const std::optional<QcBlockId> head = base ? content.blockId(content.block(*base).signal.head) : std::nullopt;
    if (!head) {
        return qcAirBlockId;
    }
    const QcBlockId facing = content.withProperty(*head, u"facing"_s, QcBlockRules::faceName(plan.facing));
    return m_table.withFlag(facing, u"sticky"_s, plan.sticky);
}

void QcPistonMover::extend(const QcPistonPlan &plan)
{
    m_bodiesLookedAt = 0;
    for (const QcBlockPos &cell : plan.broken) {
        m_blocks.breakBlock(cell, true);
    }

    // Read every block first, then move from the far end, so each lands in a cell already emptied.
    QList<QcBlockId> blocks;
    for (const QcBlockPos &cell : plan.moved) {
        blocks.append(m_state.world().blockAt(cell).value_or(qcAirBlockId));
    }
    for (qsizetype i = plan.moved.size() - 1; i >= 0; --i) {
        moveBlock(plan.moved[i], qcNeighbour(plan.moved[i], plan.facing), blocks[i]);
    }

    const std::optional<QcBlockId> base = m_state.world().blockAt(plan.base);
    m_blocks.setAndBroadcast(plan.head(), headState(plan));
    if (base) {
        m_blocks.setAndBroadcast(plan.base, m_table.withFlag(*base, u"extended"_s, true));
    }
    m_state.blockEntities().memoryAt(plan.base).extendingSince = m_state.clock().tick();

    QList<QcBlockPos> occupied{plan.head()};
    for (const QcBlockPos &cell : plan.moved) {
        occupied.append(qcNeighbour(cell, plan.facing));
    }
    pushBodies(occupied, plan.facing);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::pistonOut), centreOf(plan.base));
}

void QcPistonMover::retract(const QcPistonPlan &plan)
{
    m_bodiesLookedAt = 0;
    const QcBlockPos head = plan.head();
    const std::optional<QcBlockId> headBlock = m_state.world().blockAt(head);
    if (!plan.moved.isEmpty()) {
        const QcBlockPos pulled = plan.moved.constFirst();
        const QcBlockId block = m_state.world().blockAt(pulled).value_or(qcAirBlockId);
        moveBlock(pulled, head, block);
        m_blocks.setAndBroadcast(pulled, qcAirBlockId);
    } else if (headBlock && m_table.at(*headBlock).kind == QcSignalKind::PistonHead) {
        m_blocks.setAndBroadcast(head, qcAirBlockId);
    }

    if (const std::optional<QcBlockId> base = m_state.world().blockAt(plan.base)) {
        m_blocks.setAndBroadcast(plan.base, m_table.withFlag(*base, u"extended"_s, false));
    }
    if (m_state.blockEntities().memory(plan.base)) {
        m_state.blockEntities().memoryAt(plan.base).extendingSince.reset();
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::pistonIn), centreOf(plan.base));
}

void QcPistonMover::moveBlock(QcBlockPos from, QcBlockPos to, QcBlockId block)
{
    // Screens open on the block close: its cell will hold another block (decision E-337).
    for (const auto &client : m_state.clients()) {
        if (client->screen.open == from) {
            client->screen.open.reset();
            client->screen.sent.reset();
            client->connection->send(QcMsg::CloseMachine{});
        }
    }

    // Everything of the cell first (contents, cooldown, timers, memory, scheduled tick: a dispenser
    // waiting to fire fires where it is now), so setting the block at `to` keeps it.
    m_state.blockEntities().relocate(from, to);
    // The source cell empties in the world at once: the block pushed next lands in it, and a stale
    // portable container there would otherwise leave as an item carrying that block's contents
    // (decision E-832).
    if (!m_blocks.setQuietly(from, qcAirBlockId)) {
        qCWarning(lcServer) << "A piston could not empty the cell of a moved block at" << from.x << from.y << from.z;
    }
    m_state.hoppers().wake(to);
    // The planner moves blocks only within loaded chunks and the world height (QcPistonPlanner), so
    // the cell takes the block; the block entity moved first and would otherwise sit on air.
    if (!m_blocks.setQuietly(to, block)) {
        qCWarning(lcServer) << "A piston could not place a moved block at" << to.x << to.y << to.z;
        Q_ASSERT(false);
    }

    // The clients see the source empty at once and the block sliding into its new cell.
    m_blocks.sendBlock(from, qcAirBlockId);
    QcMovingBlock moving;
    moving.id = m_state.entities().newId();
    moving.block = block;
    moving.from = from;
    moving.to = to;
    moving.start = m_state.clock().tick();
    const bool shown = m_state.entities().movingBlocks().add(moving);
    if (!shown) {
        m_blocks.sendBlock(to, block);
    }
}

double QcPistonMover::pushDistance(const QcAabb &body, const QList<QcBlockPos> &cells, QcBlockFace towards)
{
    const QcFaceOffset axis = qcFaceOffset(towards);
    double distance = 0.0;
    for (const QcBlockPos &cell : cells) {
        const QcVec3 corner{double(cell.x), double(cell.y), double(cell.z)};
        const QcAabb box{corner, corner + QcVec3{1.0, 1.0, 1.0}};
        if (!body.intersects(box)) {
            continue;
        }
        // Along the axis, the body's back has to clear the cell's front face.
        const double front = along(axis.dx + axis.dy + axis.dz > 0 ? box.max : box.min, axis);
        const double back = along(axis.dx + axis.dy + axis.dz > 0 ? body.min : body.max, axis);
        distance = std::max(distance, front - back + pushGap);
    }
    return std::min(distance, 1.0 + pushGap);
}

void QcPistonMover::notePush(quint32 body, const QcAabb &box, const QList<QcBlockPos> &cells, QcBlockFace towards)
{
    const double distance = pushDistance(box, cells, towards);
    if (distance <= 0.0) {
        return;
    }
    // Per body and direction the largest push of the step; opposite pushes cancel each other out,
    // whatever order the pistons came in.
    std::array<double, qcBlockFaceCount> &pushes = m_pushes[body];
    pushes[std::size_t(towards)] = std::max(pushes[std::size_t(towards)], distance);
}

void QcPistonMover::pushBodies(const QList<QcBlockPos> &cells, QcBlockFace towards)
{
    m_bodiesLookedAt = 0;
    m_state.entities().forEachBody([&](const QcEntity &body) {
        ++m_bodiesLookedAt;
        if (const std::optional<QcAabb> box = m_state.entities().boxOf(body)) {
            notePush(body.id, *box, cells, towards);
        }
    });
}

QcVec3 QcPistonMover::takePush(quint32 body)
{
    const auto it = m_pushes.constFind(body);
    if (it == m_pushes.constEnd()) {
        return {};
    }

    QcVec3 total;
    for (const QcBlockFace face : qcAllBlockFaces) {
        const QcFaceOffset offset = qcFaceOffset(face);
        total += QcVec3{double(offset.dx), double(offset.dy), double(offset.dz)} * (*it)[std::size_t(face)];
    }
    m_pushes.erase(it);
    return total;
}

QcVec3 QcPistonMover::moveBody(const QcAabb &box, const QcVec3 &push) const
{
    // Along the push, stopped by what is solid in the world after the move.
    const QcVec3 moved = QcPhysics::moveBox(box, push, m_state.world()).applied;
    const QcAabb after = box.moved(moved);
    if (!overlapsSolid(after)) {
        return moved;
    }

    // Still inside a block (the head or a pushed block came down on it, or it was pushed against a
    // wall): out to the nearest free cell, up first, then along the push, back, sideways, down. The
    // order is relative to the push and the body, so it is the same in every rotation.
    const QList<QcVec3> steps = escapes(after, push);
    for (const QcVec3 &step : steps) {
        if (!overlapsSolid(after.moved(step))) {
            return moved + step;
        }
    }
    // Nowhere to go: it stays and suffocates (QcEnvironmentDamage::applySuffocation).
    return moved;
}

QList<QcVec3> QcPistonMover::escapes(const QcAabb &box, const QcVec3 &push)
{
    const QcVec3 up{0.0, 1.0, 0.0};
    const QcVec3 forward = push.length() > 0.0 ? push.normalized() : up;
    QList<QcVec3> steps{up, up * 2.0};
    if (std::abs(forward.y) < 0.5) {
        const QcVec3 left{forward.z, 0.0, -forward.x};
        steps.append({forward, forward * -1.0, left, left * -1.0});
    } else {
        // Pushed up or down: first towards the side of its cell the body is nearer to (only a body
        // exactly in the middle falls back to north, east, south, west).
        const QcVec3 centre = (box.min + box.max) * 0.5;
        const double dx = centre.x - (std::floor(centre.x) + 0.5);
        const double dz = centre.z - (std::floor(centre.z) + 0.5);
        const QcVec3 aside = std::abs(dx) > std::abs(dz) ? QcVec3{dx > 0.0 ? 1.0 : -1.0, 0.0, 0.0}
                             : dz != 0.0                  ? QcVec3{0.0, 0.0, dz > 0.0 ? 1.0 : -1.0}
                                                          : QcVec3{0.0, 0.0, -1.0};
        const QcVec3 across{-aside.z, 0.0, aside.x};
        steps.append({aside, across, aside * -1.0, across * -1.0});
    }
    steps.append(up * -1.0);
    return steps;
}

bool QcPistonMover::overlapsSolid(const QcAabb &box) const
{
    // Cells not loaded block: a body is never pushed into them.
    return QcBodyFit::overlapsBlocks(m_state.world(), box, QcBodyFit::Unloaded::Blocked);
}

void QcPistonMover::applyPushes()
{
    if (m_pushes.isEmpty()) {
        return;
    }

    // Each body moves on its own against the world after the moves, so the order does not matter.
    m_state.entities().forEachBody([this](QcEntity &body) {
        const QcVec3 push = takePush(body.id);
        if (push.length() <= 0.0) {
            return;
        }
        if (const std::optional<QcAabb> box = m_state.entities().boxOf(body)) {
            m_state.entities().displace(body, moveBody(*box, push));
        }
    });
    m_pushes.clear();
}
