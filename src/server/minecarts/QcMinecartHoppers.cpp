#include "server/minecarts/QcMinecartHoppers.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/machines/QcBlockScreens.h"
#include "server/machines/QcInventoryAccess.h"

#include <algorithm>
#include <numeric>

using namespace Qt::StringLiterals;

namespace {

QcBlockPos cellOf(const QcMinecart &cart)
{
    return QcPhysics::blockContaining(cart.motion.position);
}

} // namespace

QcMinecartHoppers::QcMinecartHoppers(QcServerState &state, QcBlockScreens &screens)
    : m_state(state)
    , m_screens(screens)
{
}

int QcMinecartHoppers::slotFor(const QList<QcItemStack> &stacks, const QcItemStack &unit, int maxStack)
{
    for (qsizetype slot = 0; slot < stacks.size(); ++slot) {
        const QcItemStack &stack = stacks[slot];
        if (stack.isEmpty() || (stack.stacksWith(unit) && stack.count < maxStack)) {
            return int(slot);
        }
    }
    return -1;
}

void QcMinecartHoppers::tick()
{
    m_stats = {};
    m_gridBuilt = false;
    const QList<qsizetype> carts = actors();
    m_stats.acting = carts.size();
    if (carts.isEmpty()) {
        return;
    }

    QcInventoryAccess access(m_state);
    QList<QcMinecart> &all = m_state.entities().minecarts().editableAll();
    const quint64 now = m_state.clock().tick();
    for (const qsizetype index : carts) {
        QcMinecart &cart = all[index];
        const QcBlockPos below = cellOf(cart).offset(0, -1, 0);
        bool moved = m_state.world().hoppers().contains(below) && hopperReady(below) && unloadInto(cart, below, access);

        const bool hopper = m_state.entities().minecarts().loadOf(cart).hopper;
        if (hopper && cart.enabled && cart.readyAt <= now) {
            const Pull pull = pullFromAbove(cart, access);
            const bool pulled = pull == Pull::Moved || (pull == Pull::NoContainer && pickUp(cart));
            if (pulled) {
                cart.readyAt = now + quint64(m_state.hoppers().cooldownTicks);
            }
            moved = moved || pulled;
        }
        if (moved) {
            changed(cart);
        }
    }
    access.finish();
}

QList<qsizetype> QcMinecartHoppers::actors()
{
    const QList<QcMinecart> &all = m_state.entities().minecarts().all();
    QList<qsizetype> carts;
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (!all[i].stacks.isEmpty() && !all[i].dormant) {
            carts.append(i);
        }
    }
    std::ranges::sort(carts, [&all](qsizetype a, qsizetype b) { return all[a].id < all[b].id; });

    const auto budget = qsizetype(m_state.config().limits.hopperBudget);
    if (carts.size() > budget) {
        // Overloaded: go on after the cart where the last tick stopped.
        const auto start = std::ranges::upper_bound(carts, m_cursor, {}, [&all](qsizetype index) { return all[index].id; });
        std::ranges::rotate(carts, start);
        carts.resize(budget);
        m_cursor = all[carts.constLast()].id;
    }
    return carts;
}

bool QcMinecartHoppers::hopperReady(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || m_state.content().property(*block, u"enabled"_s) == u"false"_s) {
        return false;
    }
    const QcBlockEntities::Container *container = m_state.blockEntities().container(pos);
    return !container || container->readyAt <= m_state.clock().tick();
}

bool QcMinecartHoppers::unloadInto(QcMinecart &cart, QcBlockPos hopper, QcInventoryAccess &access)
{
    if (!access.ensure(hopper)) {
        return false;
    }

    const QList<QcItemStack> room = access.slotsAt(hopper);
    QList<int> cells(room.size());
    std::ranges::iota(cells, 0);
    for (QcItemStack &stack : cart.stacks) {
        const QcItemStack unit = stack.withCount(1);
        const int slot = stack.isEmpty() ? -1 : access.slotFor(hopper, room, cells, unit);
        if (slot < 0 || !access.put(hopper, slot, unit)) {
            continue;
        }

        stack.count -= 1;
        if (stack.count <= 0) {
            stack = {};
        }
        // The hopper rests for its cooldown, then passes the item on.
        const quint64 ready = m_state.clock().tick() + quint64(m_state.hoppers().cooldownTicks);
        if (QcBlockEntities::Container *container = m_state.blockEntities().container(hopper)) {
            container->readyAt = ready;
        }
        m_state.hoppers().wakeAt(hopper, ready);
        ++m_stats.moved;
        return true;
    }
    return false;
}

QcMinecartHoppers::Pull QcMinecartHoppers::pullFromAbove(QcMinecart &cart, QcInventoryAccess &access)
{
    const QcBlockPos above = cellOf(cart).offset(0, 1, 0);
    if (!access.hasInventory(above) || !access.ensure(above)) {
        return Pull::NoContainer;
    }

    const QList<QcItemStack> source = access.slotsAt(above);
    const QList<int> out = access.slotsOut(above, QcBlockFace::Down);
    for (const int slot : out) {
        const QcItemStack unit = source[slot].withCount(1);
        if (source[slot].isEmpty() || !access.mayTakeOut(above, slot, unit)) {
            continue;
        }
        const int into = slotFor(cart.stacks, unit, m_state.content().item(unit.item).maxStack);
        if (into < 0 || !access.take(above, slot, 1)) {
            continue;
        }

        QcItemStack &target = cart.stacks[into];
        target = target.isEmpty() ? unit : target.withCount(target.count + 1);
        ++m_stats.moved;
        return Pull::Moved;
    }
    return Pull::Nothing;
}

void QcMinecartHoppers::buildGrid()
{
    m_grid.clear();
    m_gridBuilt = true;
    const QList<QcItemEntity> &items = m_state.entities().items().items();
    for (qsizetype i = 0; i < items.size(); ++i) {
        if (!items[i].surplus && !items[i].stack.isEmpty() && !items[i].dormant) {
            m_grid[QcPhysics::blockContaining(items[i].motion.position)].append(i);
        }
    }
}

bool QcMinecartHoppers::pickUp(QcMinecart &cart)
{
    if (!m_gridBuilt) {
        buildGrid();
    }

    const QList<QcItemEntity> &items = m_state.entities().items().items();
    QList<qsizetype> candidates = m_grid.value(cellOf(cart));
    candidates.append(m_grid.value(cellOf(cart).offset(0, 1, 0)));
    // The oldest item first (entity ids grow).
    std::ranges::sort(candidates, [&items](qsizetype a, qsizetype b) { return items[a].id < items[b].id; });

    for (const qsizetype index : std::as_const(candidates)) {
        const QcItemEntity &item = items[index];
        const int maxStack = m_state.content().item(item.stack.item).maxStack;
        QList<QcItemStack> stacks = cart.stacks;
        int taken = 0;
        for (; taken < item.stack.count; ++taken) {
            const QcItemStack unit = item.stack.withCount(1);
            const int slot = slotFor(stacks, unit, maxStack);
            if (slot < 0) {
                break;
            }
            stacks[slot] = stacks[slot].isEmpty() ? unit : unit.withCount(stacks[slot].count + 1);
        }
        // The item entity may be gone already (another cart took it in this tick).
        if (taken > 0 && m_state.entities().items().take(item.id, taken)) {
            cart.stacks = stacks;
            m_stats.moved += taken;
            m_gridBuilt = false;
            return true;
        }
    }
    return false;
}

void QcMinecartHoppers::changed(const QcMinecart &cart)
{
    m_screens.sendToCartViewers(cart.id);
    m_state.noteContents(cellOf(cart));
}
