#include "server/items/QcExperienceOrbManager.h"

#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcExperience.h"
#include "common/physics/QcPhysics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Orbs lying below this speed with no player near stop being simulated.
constexpr double restSpeed = 0.01;
// Share of the speed lost per tick in the air, also while flying to a player (keeps them from
// circling him).
constexpr double airDrag = 0.04;
// Orbs float up in water with this speed.
constexpr double floatSpeed = 1.0;

// A cell of the merge grid as one key: mergeRadius-sized cells, 21 bits per axis.
quint64 cellKey(qint64 x, qint64 y, qint64 z)
{
    constexpr qint64 mask = (qint64(1) << 21) - 1;
    return (quint64(x & mask) << 42) | (quint64(y & mask) << 21) | quint64(z & mask);
}

qint64 cellOf(double coordinate)
{
    return qint64(std::floor(coordinate / QcExperienceOrbManager::mergeRadius));
}

} // namespace

QcExperienceOrbManager::QcExperienceOrbManager(quint64 seed)
    : m_random(quint32(seed ^ (seed >> 32) ^ 0x0E9Bu))
{
}

int QcExperienceOrbManager::drop(int points, const QcVec3 &position, const IdFunction &newId)
{
    if (points <= 0 || QcWorld::isInVoid(position.y)) {
        return 0;
    }

    int added = 0;
    const QList<int> values = QcExperience::orbValues(points, maxOrbsPerDrop);
    for (const int value : values) {
        const double angle = m_random.bounded(2.0 * M_PI);
        const double speed = 0.5 + m_random.bounded(1.0);

        QcExperienceOrb orb;
        orb.id = newId();
        orb.value = value;
        orb.pickupDelay = pickupDelay;
        orb.motion.position = position - QcVec3{0.0, size * 0.5, 0.0};
        orb.motion.velocity = {std::cos(angle) * speed, 2.5 + m_random.bounded(1.5), std::sin(angle) * speed};
        if (add(orb)) {
            ++added;
        } else {
            // The world is full of orbs: the experience goes into the nearest one instead.
            joinNearest(position, value);
        }
    }
    return added;
}

bool QcExperienceOrbManager::add(QcExperienceOrb orb)
{
    if (m_orbs.size() >= m_limit || orb.value <= 0 || QcWorld::isInVoid(orb.motion.position.y)) {
        return false;
    }

    orb.value = std::min(orb.value, maxValue);
    m_orbs.append(orb);
    return true;
}

const QcExperienceOrbManager::PlayerView *QcExperienceOrbManager::nearestPlayer(const QcExperienceOrb &orb, const QList<PlayerView> &players)
{
    const PlayerView *nearest = nullptr;
    double best = attractRange;
    for (const PlayerView &player : players) {
        const double distance = (player.position - orb.motion.position).length();
        if (player.alive && distance < best) {
            best = distance;
            nearest = &player;
        }
    }
    return nearest;
}

void QcExperienceOrbManager::step(QcExperienceOrb &orb, const QList<PlayerView> &players, const QcBlockAccess &world, double dt) const
{
    const PlayerView *target = nearestPlayer(orb, players);
    const QcBlockPos below = QcPhysics::blockContaining(orb.motion.position - QcVec3{0.0, 0.01, 0.0});
    if (!target && orb.resting && QcPhysics::isSolidAt(world, below)) {
        return;
    }

    orb.resting = false;
    const QcBlockPos cell = QcPhysics::blockContaining(orb.motion.position + QcVec3{0.0, 0.1, 0.0});
    orb.motion.inWater = QcPhysics::isLiquidAt(world, cell);

    // Towards the middle of the player's body, the harder the closer (the reference's pull).
    if (target) {
        const QcVec3 aim = target->position + QcVec3{0.0, QcPhysics::playerHeight * 0.5, 0.0} - orb.motion.position;
        const double distance = aim.length();
        const double closeness = 1.0 - distance / attractRange;
        if (distance > 1e-6) {
            orb.motion.velocity += aim * (attractAcceleration * closeness * closeness * dt / distance);
        }
    }

    // Like items, the steering keeps the horizontal flight with a little drag; on the ground without
    // a player near the steering stops the orb.
    QcVec3 wish;
    double speed = 0.0;
    const double horizontal = std::hypot(orb.motion.velocity.x, orb.motion.velocity.z);
    if ((target || !orb.motion.onGround) && horizontal > 0.0) {
        wish = {orb.motion.velocity.x / horizontal, 0.0, orb.motion.velocity.z / horizontal};
        speed = horizontal * (1.0 - airDrag);
    }
    if (orb.motion.inWater && orb.motion.velocity.y < floatSpeed) {
        orb.motion.velocity.y = std::min(floatSpeed, orb.motion.velocity.y + floatSpeed * 0.5);
    }

    QcPhysics::stepEntity(orb.motion, QcPhysics::Steering{.wish = wish, .speed = speed}, QcPhysics::BodySize{size, size}, world, dt);

    orb.resting = !target && orb.motion.onGround && !orb.motion.inWater && orb.motion.velocity.length() < restSpeed;
}

bool QcExperienceOrbManager::collect(QcExperienceOrb &orb, const QList<PlayerView> &players, const GiveFunction &give)
{
    const QcAabb box = QcPhysics::boxAt(orb.motion.position, size, size);
    for (const PlayerView &player : players) {
        if (!player.alive || m_nextCollect.value(player.entityId, 0) > m_ticks) {
            continue;
        }

        QcAabb reach = QcPhysics::boxAt(player.position, QcPhysics::playerWidth, QcPhysics::playerHeight);
        reach.min = reach.min - QcVec3{pickupReach, pickupReach, pickupReach};
        reach.max += QcVec3{pickupReach, pickupReach, pickupReach};
        if (reach.intersects(box)) {
            m_nextCollect.insert(player.entityId, m_ticks + collectIntervalTicks);
            give(player.entityId, orb.value);
            return true;
        }
    }
    return false;
}

void QcExperienceOrbManager::tick(const QcBlockAccess &world, const QList<PlayerView> &players, double dt, const GiveFunction &give)
{
    ++m_ticks;

    // Collected and despawned orbs are marked with value 0 and removed after the loop.
    for (QcExperienceOrb &orb : m_orbs) {
        if (orb.dormant) {
            continue;
        }

        step(orb, players, world, dt);
        orb.age += dt;
        orb.pickupDelay = std::max(0.0, orb.pickupDelay - dt);
        if ((orb.pickupDelay <= 0.0 && collect(orb, players, give)) || orb.age >= despawnSeconds) {
            orb.value = 0;
        }
    }

    if (m_ticks % mergeIntervalTicks == 0) {
        merge();
    }

    m_orbs.removeIf([](const QcExperienceOrb &orb) { return orb.value <= 0 || QcWorld::isInVoid(orb.motion.position.y); });

    // Players who left or have not collected for a while need no entry.
    m_nextCollect.removeIf([this](QHash<quint32, quint64>::iterator entry) { return entry.value() <= m_ticks; });
}

QcExperienceOrb *QcExperienceOrbManager::mergeTarget(const MergeGrid &grid, const QcVec3 &position, int value)
{
    const qint64 cx = cellOf(position.x);
    const qint64 cy = cellOf(position.y);
    const qint64 cz = cellOf(position.z);

    // The 27 cells of the 3 x 3 x 3 block around the orb's cell.
    for (qint64 neighbour = 0; neighbour < 27; ++neighbour) {
        const auto cell = grid.constFind(cellKey(cx + neighbour % 3 - 1, cy + neighbour / 3 % 3 - 1, cz + neighbour / 9 - 1));
        if (cell == grid.cend()) {
            continue;
        }

        for (const qsizetype other : *cell) {
            QcExperienceOrb &candidate = m_orbs[other];
            if ((candidate.motion.position - position).length() <= mergeRadius && candidate.value <= maxValue - value) {
                return &candidate;
            }
        }
    }
    return nullptr;
}

void QcExperienceOrbManager::merge()
{
    // A grid of mergeRadius cells: each awake orb looks for an earlier one in its own and the
    // neighbouring cells, so the work grows with the orbs, not with their pairs.
    MergeGrid grid;
    grid.reserve(m_orbs.size());
    for (qsizetype i = 0; i < m_orbs.size(); ++i) {
        QcExperienceOrb &orb = m_orbs[i];
        if (orb.dormant || orb.value <= 0) {
            continue;
        }

        QcExperienceOrb *target = mergeTarget(grid, orb.motion.position, orb.value);
        if (!target) {
            const QcVec3 &p = orb.motion.position;
            grid[cellKey(cellOf(p.x), cellOf(p.y), cellOf(p.z))].append(i);
            continue;
        }

        // The merged orb keeps the younger age so it does not vanish right after a fresh drop.
        target->value += orb.value;
        target->age = std::min(target->age, orb.age);
        target->pickupDelay = std::max(target->pickupDelay, orb.pickupDelay);
        target->resting = false;
        orb.value = 0;
        ++m_merged;
    }
}

QcExperienceOrb *QcExperienceOrbManager::nearestOrb(const QcVec3 &position, int value)
{
    QcExperienceOrb *nearest = nullptr;
    double best = std::numeric_limits<double>::max();
    for (QcExperienceOrb &orb : m_orbs) {
        // Awake orbs come first: a dormant one far away is only taken when there is no other.
        const double distance = (orb.motion.position - position).length() + (orb.dormant ? 1e9 : 0.0);
        if (orb.value > 0 && orb.value <= maxValue - value && distance < best) {
            best = distance;
            nearest = &orb;
        }
    }
    return nearest;
}

bool QcExperienceOrbManager::joinNearest(const QcVec3 &position, int value)
{
    QcExperienceOrb *nearest = nearestOrb(position, value);
    if (!nearest) {
        return false;
    }

    nearest->value += value;
    nearest->resting = false;
    return true;
}

qint64 QcExperienceOrbManager::totalPoints() const
{
    qint64 total = 0;
    for (const QcExperienceOrb &orb : m_orbs) {
        total += orb.value;
    }
    return total;
}

qsizetype QcExperienceOrbManager::adopt(const QList<QcExperienceOrb> &orbs, const IdFunction &newId)
{
    qsizetype left = 0;
    for (QcExperienceOrb orb : orbs) {
        orb.id = newId();
        if (add(orb)) {
            continue;
        }

        ++left;
        joinNearest(orb.motion.position, orb.value);
    }
    return left;
}

void QcExperienceOrbManager::clear()
{
    m_orbs.clear();
    m_nextCollect.clear();
}
