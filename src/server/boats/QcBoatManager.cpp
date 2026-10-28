#include "server/boats/QcBoatManager.h"

#include "common/physics/QcBoatMotion.h"
#include "common/physics/QcPhysics.h"
#include "server/boats/QcBoxGrid.h"
#include "server/core/QcServerWorld.h"

#include <algorithm>
#include <cmath>

QcAabb QcBoatManager::bodyOf(const QcBoat &boat)
{
    return QcBoatMotion::boxAt(boat.motion.position);
}

QcBoat *QcBoatManager::find(quint32 id)
{
    const auto it = std::ranges::find(m_boats, id, &QcBoat::id);
    return it == m_boats.end() ? nullptr : &*it;
}

const QcBoat *QcBoatManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_boats, id, &QcBoat::id);
    return it == m_boats.end() ? nullptr : &*it;
}

QcBoat *QcBoatManager::drivenBy(quint32 player)
{
    const auto it = std::ranges::find(m_boats, player, &QcBoat::driver);
    return player == 0 || it == m_boats.end() ? nullptr : &*it;
}

const QcBoat *QcBoatManager::drivenBy(quint32 player) const
{
    const auto it = std::ranges::find(m_boats, player, &QcBoat::driver);
    return player == 0 || it == m_boats.end() ? nullptr : &*it;
}

const QcBoat *QcBoatManager::carrying(quint32 creature) const
{
    const auto it = std::ranges::find(m_boats, creature, &QcBoat::passenger);
    return creature == 0 || it == m_boats.end() ? nullptr : &*it;
}

bool QcBoatManager::add(const QcBoat &boat)
{
    if (m_boats.size() >= limit()) {
        return false;
    }
    m_boats.append(boat);
    return true;
}

std::optional<QcBoat> QcBoatManager::remove(quint32 id)
{
    const auto it = std::ranges::find(m_boats, id, &QcBoat::id);
    if (it == m_boats.end()) {
        return std::nullopt;
    }

    QcBoat removed = *it;
    m_boats.erase(it);
    return removed;
}

qsizetype QcBoatManager::adopt(const QList<QcBoat> &boats)
{
    qsizetype left = 0;
    for (QcBoat boat : boats) {
        if (m_boats.size() >= limit()) {
            ++left;
            continue;
        }
        boat.settled = false;
        boat.driver = 0;
        boat.passenger = 0;
        boat.rest.reset();
        m_boats.append(boat);
    }
    return left;
}

QcVec3 QcBoatManager::pushOn(qsizetype self, const QList<QcAabb> &boxes, const QcBoxGrid &grid)
{
    const QcAabb &box = boxes.at(self);
    const QcVec3 centre = (box.min + box.max) * 0.5;
    const double half = (box.max.x - box.min.x) * 0.5;
    QcVec3 push;
    const QList<qsizetype> near = grid.near(box);
    for (const qsizetype index : near) {
        const QcAabb &other = boxes.at(index);
        if (index == self || !other.intersects(box)) {
            continue;
        }
        const QcVec3 otherCentre = (other.min + other.max) * 0.5;
        const QcVec3 away{centre.x - otherCentre.x, 0.0, centre.z - otherCentre.z};
        const double distance = away.length();
        const double reach = half + (other.max.x - other.min.x) * 0.5;
        const double share = std::clamp(1.0 - distance / reach, 0.0, 1.0);
        // On the same spot a push has no direction: towards +x, the same for every boat.
        const QcVec3 direction = distance > 1e-4 ? away * (1.0 / distance) : QcVec3{1.0, 0.0, 0.0};
        push += direction * share;
    }
    return push;
}

namespace {

// The cells a resting boat watches: the columns its body covers, from the cell under its feet to the
// cell of its top.
struct Watched
{
    QcBlockPos low;
    QcBlockPos high;
};

Watched watchedBy(const QcBoat &boat)
{
    const QcAabb box = QcBoatManager::bodyOf(boat);
    const QcBlockPos low = QcPhysics::blockContaining(box.min);
    const QcBlockPos high = QcPhysics::blockContaining(box.max);
    return {low.offset(0, -1, 0), high};
}

} // namespace

QcBoat::Rest QcBoatManager::restOf(const QcBoat &boat, const QcServerWorld &world)
{
    QcBoat::Rest rest{boat.motion.position, boat.yaw, world.blockChanges(), {}};
    rest.around.fill(static_cast<QcBlockId>(~0u));
    const Watched cells = watchedBy(boat);
    std::size_t next = 0;
    for (int x = cells.low.x; x <= std::min(cells.high.x, cells.low.x + 2); ++x) {
        for (int z = cells.low.z; z <= std::min(cells.high.z, cells.low.z + 2); ++z) {
            for (int y = cells.low.y; y <= std::min(cells.high.y, cells.low.y + 2); ++y) {
                rest.around[next++] = world.blockAt({x, y, z}).value_or(static_cast<QcBlockId>(~0u));
            }
        }
    }
    return rest;
}

std::optional<QcBoat::Rest> QcBoatManager::keptRest(const QcBoat &boat, const QcVec3 &push, const QcServerWorld &world)
{
    // Anything that could move it (a driver, a push, speed, a turn, a piston or a command that moved
    // it) wakes it.
    const std::optional<QcBoat::Rest> &rest = boat.rest;
    if (!rest || boat.driver != 0 || push != QcVec3{} || boat.motion.velocity != QcVec3{} || boat.spin != 0.0f
        || boat.motion.position != rest->position || boat.yaw != rest->yaw) {
        return std::nullopt;
    }

    // Its blocks are read again only when one of their chunks changed since they were read.
    const Watched cells = watchedBy(boat);
    const QcChunkPos first = cells.low.chunk();
    const QcChunkPos last = cells.high.chunk();
    bool changed = false;
    for (int x = first.x; x <= last.x && !changed; ++x) {
        for (int z = first.z; z <= last.z && !changed; ++z) {
            changed = world.lastChangeIn({x, z}) > rest->readAt;
        }
    }
    if (!changed) {
        return rest;
    }
    const QcBoat::Rest now = restOf(boat, world);
    return now.around == rest->around ? std::optional(now) : std::nullopt;
}

bool QcBoatManager::canRest(const QcBoat &boat)
{
    return (boat.motion.inWater || boat.motion.onGround) && boat.motion.velocity.length() < restSpeed && std::abs(boat.spin) < restSpin;
}

QList<QcVec3> QcBoatManager::pushes(const QList<QcAabb> &bodies) const
{
    // The pushes come from where everything was before any boat moved, so the order of the table
    // decides nothing. Boats whose chunk is not loaded are no obstacle. Boats at rest never overlap
    // each other (they would push each other and not rest), so a resting boat looks only at the
    // movers (awake and driven boats and living bodies); an awake boat looks at the resting ones too.
    const qsizetype count = m_boats.size();
    QList<QcAabb> boxes(count + bodies.size());
    QcBoxGrid movers;
    QcBoxGrid resting;
    bool anyAwake = false;
    for (qsizetype index = 0; index < count; ++index) {
        const QcBoat &boat = m_boats.at(index);
        if (boat.dormant) {
            continue;
        }
        boxes[index] = bodyOf(boat);
        if (!boat.rest || boat.driver != 0) {
            movers.add(index, boxes.at(index));
            anyAwake = anyAwake || boat.driver == 0;
        }
    }
    for (qsizetype index = 0; index < bodies.size(); ++index) {
        boxes[count + index] = bodies.at(index);
        movers.add(count + index, bodies.at(index));
    }
    for (qsizetype index = 0; anyAwake && index < count; ++index) {
        const QcBoat &boat = m_boats.at(index);
        if (!boat.dormant && boat.rest && boat.driver == 0) {
            resting.add(index, boxes.at(index));
        }
    }

    QList<QcVec3> pushes(count);
    for (qsizetype index = 0; index < count; ++index) {
        const QcBoat &boat = m_boats.at(index);
        if (boat.dormant || boat.driver != 0) {
            continue;
        }
        if (!movers.isEmpty()) {
            pushes[index] = pushOn(index, boxes, movers);
        }
        if (!boat.rest && !resting.isEmpty()) {
            pushes[index] += pushOn(index, boxes, resting);
        }
    }
    return pushes;
}

void QcBoatManager::tick(const QcServerWorld &world, const QList<QcAabb> &bodies)
{
    const QList<QcVec3> pushes = this->pushes(bodies);
    m_stepped = 0;
    for (qsizetype index = 0; index < m_boats.size(); ++index) {
        QcBoat &boat = m_boats[index];
        if (boat.dormant || boat.driver != 0) {
            continue;
        }
        boat.rest = keptRest(boat, pushes.at(index), world);
        if (boat.rest) {
            continue;
        }

        ++m_stepped;
        QcBoatState state = boat.state();
        state.motion.velocity += pushes.at(index) * pushPerTick;
        state.step(std::nullopt, world);
        boat.setState(state);
        if (pushes.at(index) == QcVec3{} && canRest(boat)) {
            // What is left of its speed and turn would move it less than a millimetre.
            boat.motion.velocity = {};
            boat.spin = 0.0f;
            boat.rest = restOf(boat, world);
        }
    }
}
