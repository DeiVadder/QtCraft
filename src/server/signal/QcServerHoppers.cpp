// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/signal/QcServerHoppers.h"

#include "common/blockrules/QcBlockRules.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/signal/QcSignalTable.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <tuple>

namespace {

bool before(QcBlockPos a, QcBlockPos b)
{
    return std::tuple{a.y, a.z, a.x} < std::tuple{b.y, b.z, b.x};
}

bool fillsUnitCell(const QList<QcAabb> &boxes)
{
    constexpr double slack = 1e-6;
    return std::ranges::any_of(boxes, [](const QcAabb &box) {
        return box.min.x <= slack && box.min.y <= slack && box.min.z <= slack && box.max.x >= 1.0 - slack && box.max.y >= 1.0 - slack
               && box.max.z >= 1.0 - slack;
    });
}

QList<int> allSlots(qsizetype count)
{
    QList<int> cells(count);
    std::iota(cells.begin(), cells.end(), 0);
    return cells;
}

} // namespace

QcServerHoppers::QcServerHoppers(QcServerState &state, const QcSignalTable &table)
    : m_state(state)
    , m_table(table)
{
    const QcContentRegistry &content = state.content();
    m_fillsCell.resize(static_cast<std::size_t>(content.stateCount()));
    for (std::size_t id = 0; id < m_fillsCell.size(); ++id) {
        m_fillsCell[id] = fillsUnitCell(content.state(static_cast<QcBlockId>(id)).collision);
    }
}

bool QcServerHoppers::fillsCell(QcBlockId block) const
{
    return static_cast<std::size_t>(block) < m_fillsCell.size() && m_fillsCell[static_cast<std::size_t>(block)];
}

std::optional<QcBlockFace> QcServerHoppers::facingOf(QcBlockPos hopper) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(hopper);
    return block ? m_table.at(*block).facing : std::nullopt;
}

void QcServerHoppers::tick()
{
    m_stats = {};
    wakeAround();

    const QList<QcBlockPos> hoppers = actors();
    m_stats.acting = hoppers.size();
    if (hoppers.isEmpty()) {
        return;
    }

    QcInventoryAccess access(m_state);
    QSet<QcBlockPos> moved = push(hoppers, access);
    moved.unite(pull(hoppers, access));

    // Moved: rest for the cooldown, then look again. Nothing to do: sleep until woken.
    for (const QcBlockPos &hopper : std::as_const(moved)) {
        startCooldown(hopper);
    }
    m_stats.moved = moved.size();
}

void QcServerHoppers::startCooldown(QcBlockPos hopper)
{
    const quint64 ready = m_state.clock().tick() + static_cast<quint64>(m_state.hoppers().cooldownTicks);
    if (QcBlockEntities::Container *container = m_state.blockEntities().container(hopper)) {
        container->readyAt = ready;
    }
    m_state.hoppers().wakeAt(hopper, ready);
}

quint64 QcServerHoppers::readyAt(QcBlockPos hopper) const
{
    const QcBlockEntities::Container *container = m_state.blockEntities().container(hopper);
    return container ? container->readyAt : 0;
}

void QcServerHoppers::wakeAround()
{
    QcHopperQueue &hoppers = m_state.hoppers();
    const QcBlockIndex &index = m_state.world().hoppers();
    QList<QcBlockPos> cells = m_state.world().hoppers().takeTouched();
    cells.append(QList<QcBlockPos>(hoppers.touched.begin(), hoppers.touched.end()));
    hoppers.touched.clear();
    const QList<QcVec3> stirred = m_state.entities().items().takeStirred();
    if (index.size() == 0) {
        return;
    }

    for (const QcBlockPos &cell : std::as_const(cells)) {
        if (index.contains(cell)) {
            hoppers.wake(cell);
        }
        for (const QcBlockFace face : qcAllBlockFaces) {
            if (index.contains(qcNeighbour(cell, face))) {
                hoppers.wake(qcNeighbour(cell, face));
            }
        }
    }

    for (const QcVec3 &position : stirred) {
        const QcBlockPos cell = QcPhysics::blockContaining(position);
        if (index.contains(cell) && position.y - cell.y >= bowlHeight) {
            hoppers.wake(cell);
        }
        if (index.contains(cell.offset(0, -1, 0))) {
            hoppers.wake(cell.offset(0, -1, 0));
        }
    }
}

QList<QcBlockPos> QcServerHoppers::actors()
{
    QcHopperQueue &hoppers = m_state.hoppers();
    const QSet<QcBlockPos> due = hoppers.takeDue(m_state.clock().tick());
    m_stats.woken = due.size();

    QList<QcBlockPos> ready;
    for (const QcBlockPos &pos : std::as_const(due)) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
        if (!block || !m_state.world().hoppers().contains(pos) || m_table.at(*block).locked) {
            // Gone, unloaded or locked: unlocking or loading is a block change that wakes it.
            continue;
        }
        if (const quint64 readyTick = readyAt(pos); readyTick > m_state.clock().tick()) {
            hoppers.wakeAt(pos, readyTick);
            continue;
        }
        const std::optional<QcBlockFace> facing = m_table.at(*block).facing;
        if (!m_state.world().blockAt(pos.offset(0, 1, 0)) || !facing || !m_state.world().blockAt(qcNeighbour(pos, *facing))) {
            hoppers.wakeAt(pos, m_state.clock().tick() + unloadedRetryTicks);
            continue;
        }
        ready.append(pos);
    }
    std::ranges::sort(ready, before);

    const auto budget = static_cast<qsizetype>(m_state.config().limits.hopperBudget);
    if (ready.size() > budget) {
        // Overloaded: go on after the hopper where the last tick stopped, the rest waits.
        const auto start = m_cursor ? std::ranges::upper_bound(ready, *m_cursor, before) : ready.begin();
        std::ranges::rotate(ready, start);
        for (qsizetype i = budget; i < ready.size(); ++i) {
            hoppers.wake(ready[i]);
        }
        ready.resize(budget);
        m_cursor = ready.constLast();
    }
    return ready;
}

QSet<QcBlockPos> QcServerHoppers::push(const QList<QcBlockPos> &hoppers, QcInventoryAccess &access)
{
    QSet<QcBlockPos> moved;
    QList<QcInventoryAccess::Insertion> wanted;
    QList<std::pair<QcBlockPos, int>> sources;
    for (const QcBlockPos &hopper : hoppers) {
        const std::optional<QcBlockFace> facing = facingOf(hopper);
        const QcBlockPos target = facing ? qcNeighbour(hopper, *facing) : hopper;
        // An entry that cannot be made (the table is full) is no inventory: nothing moves there.
        if (!facing || !access.ensure(target)) {
            continue;
        }

        // The first slot whose item fits, as in the reference.
        const QcBlockFace face = QcBlockRules::opposite(*facing);
        const QList<QcItemStack> stacks = access.slotsAt(hopper);
        const QList<QcItemStack> room = access.slotsAt(target);
        const QList<int> cells = access.slotsIn(target, face);
        for (qsizetype slot = 0; slot < stacks.size(); ++slot) {
            const QcItemStack unit = stacks[slot].withCount(1);
            if (!stacks[slot].isEmpty() && access.slotFor(target, room, cells, unit) >= 0) {
                wanted.append({target, face, unit, static_cast<int>(sources.size()), -1});
                sources.append({hopper, static_cast<int>(slot)});
                break;
            }
        }
    }

    QList<QcBlockPos> received;
    const QList<QcInventoryAccess::Insertion> kept = access.resolve(wanted);
    for (const QcInventoryAccess::Insertion &insertion : kept) {
        const auto &[hopper, slot] = sources[insertion.giver];
        if (!access.put(insertion.target, insertion.slot, insertion.stack) || !access.take(hopper, slot, 1)) {
            continue;
        }
        moved.insert(hopper);
        if (access.wasEmptyBefore(insertion.target)) {
            received.append(insertion.target);
        }
    }
    access.finish();

    // A hopper that was empty waits its cooldown before passing the item on (as in the reference).
    for (const QcBlockPos &target : std::as_const(received)) {
        if (m_state.world().hoppers().contains(target) && !moved.contains(target)) {
            startCooldown(target);
        }
    }
    return moved;
}

QSet<QcBlockPos> QcServerHoppers::pull(const QList<QcBlockPos> &hoppers, QcInventoryAccess &access)
{
    // All decided on the world after the pushes, then applied: an item moves one hopper per step.
    // A hopper that was empty and got an item in the push step rests now (as in the reference).
    QList<Pull> pulls;
    QList<QcBlockPos> searching;
    for (const QcBlockPos &hopper : hoppers) {
        if (readyAt(hopper) > m_state.clock().tick()) {
            continue;
        }
        const Intake intake = pullFor(hopper, access);
        if (intake.pull) {
            pulls.append(*intake.pull);
        } else if (intake.searches) {
            searching.append(hopper);
        }
    }

    // Item entities only for the hoppers that search, in one pass over the items.
    buildGrid(searching);
    for (const QcBlockPos &hopper : std::as_const(searching)) {
        if (std::optional<Pull> pull = pickUp(hopper, access)) {
            pulls.append(*pull);
        }
    }

    QSet<QcBlockPos> moved;
    for (const Pull &pull : std::as_const(pulls)) {
        if (apply(pull, access)) {
            moved.insert(pull.hopper);
        }
    }
    access.finish();
    return moved;
}

bool QcServerHoppers::apply(const Pull &pull, QcInventoryAccess &access)
{
    // An item entity is taken first (it may be gone); a slot only after the put succeeded.
    if (pull.entity != 0 && !m_state.entities().items().take(pull.entity, pull.stack.count)) {
        return false;
    }

    int placed = 0;
    for (const auto &[slot, count] : pull.places) {
        placed += access.put(pull.hopper, slot, pull.stack.withCount(count)) ? count : 0;
    }
    if (pull.entity == 0) {
        return placed > 0 && access.take(pull.source, pull.slot, 1);
    }
    if (placed < pull.stack.count) {
        // Cannot happen after ensure(); never lose the rest.
        m_state.entities().items().spawn({pull.stack.withCount(pull.stack.count - placed), {pull.hopper.x + 0.5, pull.hopper.y + 1.0, pull.hopper.z + 0.5}, {}},
                            m_state.entities().newId());
    }
    return true;
}

QcServerHoppers::Intake QcServerHoppers::pullFor(QcBlockPos hopper, QcInventoryAccess &access) const
{
    // The hopper gets its entry before it decides to take anything.
    if (!access.ensure(hopper)) {
        return {};
    }

    const QcBlockPos above = hopper.offset(0, 1, 0);
    if (access.hasInventory(above) && access.ensure(above)) {
        const QList<QcItemStack> source = access.slotsAt(above);
        const QList<QcItemStack> stacks = access.slotsAt(hopper);
        const QList<int> out = access.slotsOut(above, QcBlockFace::Down);
        for (const int slot : out) {
            const QcItemStack unit = source[slot].withCount(1);
            const bool takes = !source[slot].isEmpty() && access.mayTakeOut(above, slot, unit);
            const int into = takes ? access.slotFor(hopper, stacks, allSlots(stacks.size()), unit) : -1;
            if (into >= 0) {
                return {Pull{hopper, above, slot, 0, unit, {{into, 1}}}, false};
            }
        }
        return {};
    }

    // A block filling its cell leaves no room for items above the hopper: no search at all.
    return {std::nullopt, !fillsCell(m_state.world().blockAt(above).value_or(qcAirBlockId))};
}

std::optional<QcServerHoppers::Pull> QcServerHoppers::pickUp(QcBlockPos hopper, QcInventoryAccess &access)
{
    ++m_stats.searches;

    const QList<QcItemEntity> &items = m_state.entities().items().items();
    QList<qsizetype> candidates = m_grid.value(hopper.offset(0, 1, 0));
    const QList<qsizetype> inBowl = m_grid.value(hopper);
    for (const qsizetype index : inBowl) {
        if (items[index].motion.position.y - hopper.y >= bowlHeight) {
            candidates.append(index);
        }
    }
    // The oldest item first (entity ids grow): nothing about places or directions decides.
    std::ranges::sort(candidates, [&items](qsizetype a, qsizetype b) { return items[a].id < items[b].id; });

    const QList<QcItemStack> stacks = access.slotsAt(hopper);
    for (const qsizetype index : std::as_const(candidates)) {
        const QcItemStack stack = items[index].stack;
        const QList<std::pair<int, int>> places = access.fit(hopper, stacks, allSlots(stacks.size()), stack);
        int fits = 0;
        for (const auto &[slot, count] : places) {
            fits += count;
        }
        if (fits > 0) {
            return Pull{hopper, hopper, -1, items[index].id, stack.withCount(fits), places};
        }
    }
    return std::nullopt;
}

void QcServerHoppers::buildGrid(const QList<QcBlockPos> &searching)
{
    m_grid.clear();
    if (searching.isEmpty()) {
        return;
    }

    // Only the cells the searching hoppers look at: their bowls and the cells above them.
    for (const QcBlockPos &hopper : searching) {
        m_grid.insert(hopper, {});
        m_grid.insert(hopper.offset(0, 1, 0), {});
    }
    const QList<QcItemEntity> &items = m_state.entities().items().items();
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (items[i].surplus || items[i].stack.isEmpty()) {
            continue;
        }
        // Items in unloaded chunks never lie in a hopper's cells (hoppers act only when loaded).
        const auto cell = m_grid.find(QcPhysics::blockContaining(items[i].motion.position));
        if (cell != m_grid.end()) {
            cell->append(i);
        }
    }
}
