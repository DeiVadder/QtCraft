#include "server/minecarts/QcMinecartManager.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcMinecartBody.h"
#include "server/minecarts/QcMinecartCollisions.h"
#include "server/minecarts/QcMinecartMotion.h"

#include <algorithm>
#include <array>

namespace {

// The cells beyond both ends of `track` at `rail`: level with it, one higher and one lower.
std::array<QcBlockPos, 6> beyondCells(QcBlockPos rail, QcRails::Track track)
{
    std::array<QcBlockPos, 6> cells;
    for (std::size_t end = 0; end < 2; ++end) {
        const QcBlockPos beyond = qcNeighbour(rail, track.ends[end].side);
        for (std::size_t level = 0; level < 3; ++level) {
            cells[end * 3 + level] = beyond.offset(0, static_cast<int>(level) - 1, 0);
        }
    }
    return cells;
}

} // namespace

QcMinecartManager::QcMinecartManager(const QcContentRegistry &content)
    : m_rails(content)
{
    for (const QcItemDefinition &item : content.items()) {
        const std::optional<QcBlockId> block = item.placesCart && !item.cartBlock.isEmpty() ? content.blockId(item.cartBlock) : std::nullopt;
        if (!block) {
            continue;
        }
        const QcBlockDefinition &definition = content.block(*block);
        m_loads.insert(item.name, {block, definition.containerSlots, definition.signal.kind == QcSignalKind::Hopper && definition.containerSlots > 0});
    }
}

QcMinecart QcMinecartManager::fromItem(const QString &item) const
{
    QcMinecart cart;
    cart.item = item;
    cart.stacks = QList<QcItemStack>(m_loads.value(item).slotCount);
    return cart;
}

QList<const QcMinecart *> QcMinecartManager::containersIn(QcBlockPos cell) const
{
    QList<const QcMinecart *> found;
    for (const QcMinecart &cart : m_carts) {
        if (!cart.stacks.isEmpty() && !cart.dormant && QcPhysics::blockContaining(cart.motion.position) == cell) {
            found.append(&cart);
        }
    }
    std::ranges::sort(found, {}, &QcMinecart::id);
    return found;
}

QcAabb QcMinecartManager::bodyOf(const QcMinecart &cart)
{
    return QcPhysics::boxAt(cart.motion.position, QcMinecartBody::width, QcMinecartBody::height);
}

QcMinecart *QcMinecartManager::find(quint32 id)
{
    const auto it = std::ranges::find(m_carts, id, &QcMinecart::id);
    return it == m_carts.end() ? nullptr : &*it;
}

const QcMinecart *QcMinecartManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_carts, id, &QcMinecart::id);
    return it == m_carts.end() ? nullptr : &*it;
}

QcMinecart *QcMinecartManager::ridden(quint32 player)
{
    const auto it = std::ranges::find(m_carts, player, &QcMinecart::rider);
    return player == 0 || it == m_carts.end() ? nullptr : &*it;
}

const QcMinecart *QcMinecartManager::ridden(quint32 player) const
{
    const auto it = std::ranges::find(m_carts, player, &QcMinecart::rider);
    return player == 0 || it == m_carts.end() ? nullptr : &*it;
}

bool QcMinecartManager::add(const QcMinecart &cart)
{
    if (m_carts.size() >= m_maxCarts) {
        return false;
    }
    m_carts.append(cart);
    return true;
}

std::optional<QcMinecart> QcMinecartManager::remove(quint32 id)
{
    const auto it = std::ranges::find(m_carts, id, &QcMinecart::id);
    if (it == m_carts.end()) {
        return std::nullopt;
    }

    QcMinecart removed = *it;
    m_carts.erase(it);
    return removed;
}

qsizetype QcMinecartManager::adopt(const QList<QcMinecart> &carts)
{
    qsizetype left = 0;
    for (QcMinecart cart : carts) {
        if (m_carts.size() >= m_maxCarts) {
            ++left;
            continue;
        }
        cart.settled = false;
        cart.rider = 0;
        m_carts.append(cart);
    }
    return left;
}

std::optional<QcMinecart::Rest> QcMinecartManager::restOf(QcBlockPos rail, const QcVec3 &position, const QcBlockAccess &world) const
{
    const QcBlockId block = world.blockAt(rail).value_or(qcAirBlockId);
    const std::optional<QcRails::Track> &track = m_rails.trackOf(block);
    if (!track) {
        return std::nullopt;
    }

    QcMinecart::Rest rest{position, rail, block, {}};
    const std::array<QcBlockPos, 6> cells = beyondCells(rail, *track);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        rest.beyond[i] = world.blockAt(cells[i]).value_or(qcAirBlockId);
    }
    return rest;
}

QcMinecart::Rest QcMinecartManager::groundRestOf(const QcVec3 &position, const QcBlockAccess &world)
{
    const QcBlockPos cell = QcPhysics::blockContaining(position);
    const QcAabb box = QcPhysics::boxAt(position, QcMinecartBody::width, QcMinecartBody::height);
    const QcBlockPos low = QcPhysics::blockContaining(box.min);
    const QcBlockPos high = QcPhysics::blockContaining(box.max);
    QcMinecart::Rest rest{position, cell, qcAirBlockId, {}, {}, true};
    // Cells beyond the body's columns, and those of a chunk that is not loaded, read as no block.
    rest.around.fill(static_cast<QcBlockId>(~0u));
    std::size_t next = 0;
    for (int x = low.x; x <= std::min(high.x, low.x + 1); ++x) {
        for (int z = low.z; z <= std::min(high.z, low.z + 1); ++z) {
            for (int dy = -1; dy <= 1; ++dy) {
                rest.around[next++] = world.blockAt({x, cell.y + dy, z}).value_or(static_cast<QcBlockId>(~0u));
            }
        }
    }
    return rest;
}

bool QcMinecartManager::isResting(const QcMinecart &cart, const QcVec3 &push, const QcBlockAccess &world) const
{
    // Anything that could move it (speed, a push, a piston, a changed rail or a rail or block laid
    // beyond its ends) wakes it.
    if (!cart.rest || push.length() != 0.0 || cart.motion.velocity.length() != 0.0 || cart.motion.position != cart.rest->position) {
        return false;
    }
    if (cart.rest->offRails) {
        return groundRestOf(cart.rest->position, world) == *cart.rest;
    }
    return restOf(cart.rest->rail, cart.rest->position, world) == cart.rest;
}

bool QcMinecartManager::canRest(const QcMinecart &cart, const QcMinecartMotion::Result &result, const QcBlockAccess &world) const
{
    const std::optional<QcMinecartMotion::Segment> &segment = result.segment;
    if (cart.motion.velocity.length() != 0.0) {
        return false;
    }
    if (!segment) {
        return !result.onRail && !result.waiting && cart.motion.onGround && !cart.motion.inWater;
    }
    // A powered rail that acts (boost, activate) acts on a standing cart too: a block laid at an end
    // starts it, a rider getting in is put out.
    const QcRails::Behaviour rail = m_rails.behaviourOf(world.blockAt(segment->cell).value_or(qcAirBlockId));
    if (rail.powered && rail.action != QcRailAction::None) {
        return false;
    }
    return segment->direction.y == 0.0 || result.heldAtEnd;
}

void QcMinecartManager::tick(const QcBlockAccess &world, const QList<QcAabb> &bodies, double dt)
{
    m_activated.clear();
    const QList<QcMinecartCollisions::Pair> pairs = QcMinecartCollisions::touching(m_carts, m_rails, world);
    const QList<QcVec3> change = QcMinecartCollisions::pushes(m_carts, pairs, bodies);
    QList<QcVec3> before(m_carts.size());
    QList<std::optional<QcMinecartMotion::Result>> moved(m_carts.size());
    for (qsizetype i = 0; i < m_carts.size(); ++i) {
        QcMinecart &cart = m_carts[i];
        before[i] = cart.motion.position;
        if (cart.dormant || isResting(cart, change[i], world)) {
            continue;
        }
        cart.motion.velocity = cart.motion.velocity + change[i];
        moved[i] = QcMinecartMotion::step(cart, m_rails, world, dt);
    }
    m_carts = QcMinecartCollisions::keptApart(std::move(m_carts), pairs, before);

    for (qsizetype i = 0; i < m_carts.size(); ++i) {
        const std::optional<QcMinecartMotion::Result> &step = moved[i];
        if (!step) {
            continue;
        }
        QcMinecart &cart = m_carts[i];
        const QcMinecartMotion::Result &result = *step;
        if (result.activated) {
            cart.enabled = false;
            m_activated.append(cart.id);
        } else if (result.deactivated) {
            cart.enabled = true;
        }
        const std::optional<QcMinecartMotion::Segment> &segment = result.segment;
        if (!canRest(cart, result, world)) {
            cart.rest.reset();
        } else if (segment) {
            cart.rest = restOf(segment->cell, cart.motion.position, world);
        } else {
            cart.rest = groundRestOf(cart.motion.position, world);
        }
    }
}
