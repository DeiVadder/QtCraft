#include "server/items/QcItemEntityManager.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "server/blocks/QcLiquidFlow.h"
#include "server/core/QcServerLogging.h"


#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

// Resting items below this speed stop being simulated.
constexpr double restSpeed = 0.01;
// Share of the horizontal speed lost per tick in the air.
constexpr double airDrag = 0.02;
// Items younger than this are kept awake when a chunk loads with too many items.
constexpr double freshSeconds = 10.0;

QcAabb itemBox(const QcVec3 &position)
{
    return QcPhysics::boxAt(position, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize);
}

} // namespace

QcItemEntityManager::QcItemEntityManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_random(quint32(seed ^ (seed >> 32) ^ 0x17E3u))
{
}

quint32 QcItemEntityManager::spawn(const Drop &drop, quint32 id)
{
    if (drop.stack.isEmpty() || !m_content->isValidItem(drop.stack.item) || QcWorld::isInVoid(drop.position.y)) {
        return 0;
    }

    // More than a stack (e.g. a generous block drop) becomes several items; the rest waits a tick.
    // Only the first part keeps what its saved entry held for a newer version (decision E-420): a
    // copy for every part would multiply it.
    const int maxStack = m_content->item(drop.stack.item).maxStack;
    if (drop.stack.count > maxStack) {
        Drop rest = drop;
        rest.stack.count = drop.stack.count - maxStack;
        rest.kept = {};
        Drop first = drop;
        first.stack.count = maxStack;

        while (rest.stack.count > 0) {
            Drop part = rest;
            part.stack.count = std::min(rest.stack.count, maxStack);
            rest.stack.count -= part.stack.count;
            enqueue(part);
        }
        return spawn(first, id);
    }

    if (m_activeCount >= m_activeLimit) {
        enqueue(drop);
        return 0;
    }

    QcItemEntity item;
    item.id = id;
    item.stack = drop.stack;
    item.motion.position = drop.position;
    item.motion.velocity = drop.velocity;
    item.pickupDelay = drop.pickupDelay;
    item.age = drop.age;
    item.kept = drop.kept;
    m_items.append(item);
    m_stirred.append(item.motion.position);
    ++m_activeCount;
    return id;
}

quint32 QcItemEntityManager::spawnScattered(QcItemStack stack, const QcVec3 &position, quint32 id)
{
    const double angle = m_random.bounded(2.0 * M_PI);
    const double speed = 0.5 + m_random.bounded(1.0);

    Drop drop;
    drop.stack = stack;
    drop.position = position - QcVec3{0.0, itemSize * 0.5, 0.0};
    drop.velocity = {std::cos(angle) * speed, 3.0 + m_random.bounded(1.5), std::sin(angle) * speed};
    return spawn(drop, id);
}

void QcItemEntityManager::enqueue(const Drop &drop)
{
    // Equal stacks waiting close together become one entry, so mining in one place does not fill
    // the queue.
    const int maxStack = m_content->item(drop.stack.item).maxStack;
    for (Drop &waiting : m_queue) {
        if (waiting.stack.stacksWith(drop.stack)
            && waiting.stack.count + drop.stack.count <= maxStack && (waiting.position - drop.position).length() <= mergeRadius) {
            waiting.stack.count += drop.stack.count;
            return;
        }
    }

    if (m_queue.size() >= m_queueLimit) {
        m_discarded += m_queue.first().stack.count;
        ++m_discardedUnreported;
        m_queue.removeFirst();
    }

    m_queue.append(drop);
}

void QcItemEntityManager::step(QcItemEntity &item, const QcBlockAccess &world, double dt)
{
    const QcBlockPos below = QcPhysics::blockContaining(item.motion.position - QcVec3{0.0, 0.01, 0.0});
    if (item.resting && QcPhysics::isSolidAt(world, below)) {
        return;
    }

    item.resting = false;
    const QcBlockPos cell = QcPhysics::blockContaining(item.motion.position + QcVec3{0.0, 0.1, 0.0});
    item.motion.inWater = QcPhysics::isLiquidAt(world, cell);

    // Items float up in water and drift with its current (decision E-296), and stop on the ground. In the air they keep most of their speed:
    // steering towards a slightly slower velocity in the flight direction makes QcEntityMotion::step's air
    // control act as weak drag (about two percent per tick) instead of the strong player control.
    QcVec3 wish;
    double speed = 0.0;
    const double horizontal = std::hypot(item.motion.velocity.x, item.motion.velocity.z);
    if (!item.motion.onGround && !item.motion.inWater && horizontal > 0.0) {
        const double blend = std::min(1.0, QcPhysics::airAcceleration * dt);
        wish = {item.motion.velocity.x / horizontal, 0.0, item.motion.velocity.z / horizontal};
        speed = horizontal * std::max(0.0, 1.0 - airDrag / blend);
    }
    if (item.motion.inWater) {
        wish = QcLiquidFlow::current(world, cell);
        speed = wish.length() > 0.0 ? driftSpeed : 0.0;
    }

    // On ice they slide farther than walkers (decision E-430).
    const QcPhysics::Steering steering{.wish = wish, .speed = speed, .jump = item.motion.inWater, .slideDrag = QcPhysics::itemDrag};
    item.motion.step(steering, QcPhysics::BodySize{itemSize, itemSize}, world, dt);

    item.resting = item.motion.onGround && !item.motion.inWater && item.motion.velocity.length() < restSpeed;
    m_stirred.append(item.motion.position);
}

void QcItemEntityManager::merge(const QList<bool> &active)
{
    // Only awake items take part; sleeping and unloaded ones would make this quadratic in all items.
    QList<qsizetype> awake;
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (active[i]) {
            awake.append(i);
        }
    }

    for (qsizetype a = 0; a < awake.size(); ++a) {
        QcItemEntity &target = m_items[awake[a]];
        if (target.stack.isEmpty()) {
            continue;
        }

        const int maxStack = m_content->item(target.stack.item).maxStack;
        for (qsizetype b = a + 1; b < awake.size() && target.stack.count < maxStack; ++b) {
            QcItemEntity &other = m_items[awake[b]];
            if (other.stack.isEmpty() || !other.stack.stacksWith(target.stack)
                || (other.motion.position - target.motion.position).length() > mergeRadius) {
                continue;
            }

            const int moved = std::min(other.stack.count, maxStack - target.stack.count);
            target.stack.count += moved;
            other.stack.count -= moved;

            // The merged stack keeps the younger age so it does not vanish right after a fresh drop.
            target.age = std::min(target.age, other.age);
            target.pickupDelay = std::max(target.pickupDelay, other.pickupDelay);
            m_stirred.append(target.motion.position);
        }
    }
}

void QcItemEntityManager::pickUp(QcItemEntity &item, const QList<PlayerView> &players, const GiveFunction &give)
{
    const QcAabb box = itemBox(item.motion.position);
    for (const PlayerView &player : players) {
        if (!player.alive || item.stack.isEmpty()) {
            continue;
        }

        QcAabb reach = QcPhysics::boxAt(player.position, QcPhysics::playerWidth, QcPhysics::playerHeight);
        reach.min = reach.min - QcVec3{pickupReach, pickupReach, pickupReach};
        reach.max += QcVec3{pickupReach, pickupReach, pickupReach};

        if (reach.intersects(box)) {
            item.stack.count = std::clamp(give(player.entityId, item.stack), 0, item.stack.count);
        }
    }
}

QList<QcItemEntity> QcItemEntityManager::removeFinished(const QList<bool> &awake)
{
    QList<QcItemEntity> despawned;
    int awakeCount = 0;
    QList<QcItemEntity> kept;
    kept.reserve(m_items.size());

    for (qsizetype i = 0; i < m_items.size(); ++i) {
        const QcItemEntity &item = m_items[i];
        if (item.stack.isEmpty() || QcWorld::isInVoid(item.motion.position.y)) {
            continue;
        }

        if (awake[i] && item.age >= despawnSeconds) {
            despawned.append(item);
            continue;
        }

        awakeCount += awake[i] ? 1 : 0;
        kept.append(item);
    }

    m_items = std::move(kept);
    m_activeCount = awakeCount;
    return despawned;
}

void QcItemEntityManager::spawnQueued(const TickCallbacks &callbacks)
{
    // Waiting drops appear as soon as there is room again, but only where a player is near.
    for (qsizetype i = 0; i < m_queue.size() && m_activeCount < m_activeLimit;) {
        if (!callbacks.isLoaded(m_queue[i].position)) {
            ++i;
            continue;
        }

        const Drop drop = m_queue.takeAt(i);
        spawn(drop, callbacks.nextEntityId());
    }
}

QList<QcItemEntity> QcItemEntityManager::tick(const QcBlockAccess &world, const QList<PlayerView> &players, double dt,
                                                           const TickCallbacks &callbacks)
{
    ++m_ticks;
    if (m_discardedUnreported > 0) {
        qCWarning(lcItems) << "Too many dropped items; discarded" << std::exchange(m_discardedUnreported, 0) << "waiting drops";
    }

    // Items in chunks that are not loaded rest (QcEntities::settle).
    QList<bool> loaded(m_items.size());
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        loaded[i] = !m_items[i].dormant;
    }

    balanceDormancy(loaded);

    QList<bool> awake(m_items.size());
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        awake[i] = loaded[i] && !m_items[i].surplus;
    }

    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (!awake[i]) {
            continue;
        }

        QcItemEntity &item = m_items[i];
        step(item, world, dt);
        item.age += dt;
        item.pickupDelay = std::max(0.0, item.pickupDelay - dt);

        if (item.pickupDelay <= 0.0) {
            pickUp(item, players, callbacks.give);
        }
    }

    if (m_ticks % mergeIntervalTicks == 0) {
        merge(awake);
    }

    // Remove picked-up, merged and expired items.
    QList<QcItemEntity> despawned = removeFinished(awake);
    spawnQueued(callbacks);
    enforceTotalLimit();
    return despawned;
}

void QcItemEntityManager::balanceDormancy(const QList<bool> &loaded)
{
    // Keep at most the limit of items awake in loaded chunks. Surplus items (a chunk full of saved
    // items was loaded) stay where they are but sleep, the youngest first; they wake oldest first
    // when room frees up. Their ids stay the same, nothing goes through the lossy queue.
    QList<qsizetype> awake;
    QList<qsizetype> sleeping;
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (loaded[i]) {
            (m_items[i].surplus ? sleeping : awake).append(i);
        }
    }

    // Fresh drops (just mined or thrown) are the last to fall asleep, then the youngest go first.
    const auto sleepOrder = [this](qsizetype index) {
        const double age = m_items[index].age;
        return age < freshSeconds ? std::numeric_limits<double>::max() - age : age;
    };
    const auto byAge = [&sleepOrder](qsizetype a, qsizetype b) { return sleepOrder(a) < sleepOrder(b); };

    if (awake.size() > m_activeLimit) {
        std::ranges::sort(awake, byAge);
        for (qsizetype i = 0; i < awake.size() - m_activeLimit; ++i) {
            m_items[awake[i]].surplus = true;
        }
    } else if (awake.size() < m_activeLimit && !sleeping.isEmpty()) {
        std::ranges::sort(sleeping, [&byAge](qsizetype a, qsizetype b) { return byAge(b, a); });
        for (qsizetype i = 0; i < sleeping.size() && i < m_activeLimit - awake.size(); ++i) {
            m_items[sleeping[i]].surplus = false;
            // A hopper below may be asleep; it takes note of the item now.
            m_stirred.append(m_items[sleeping[i]].motion.position);
        }
    }
}

void QcItemEntityManager::enforceTotalLimit()
{
    if (m_items.size() <= m_totalLimit) {
        return;
    }

    // Forget the oldest items in unloaded chunks first; nobody is near them.
    qsizetype excess = m_items.size() - m_totalLimit;
    QList<qsizetype> candidates;
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (m_items[i].dormant) {
            candidates.append(i);
        }
    }

    std::ranges::sort(candidates, [this](qsizetype a, qsizetype b) { return m_items[a].age > m_items[b].age; });
    QList<bool> remove(m_items.size(), false);
    for (qsizetype i = 0; i < candidates.size() && excess > 0; ++i, --excess) {
        remove[candidates[i]] = true;
    }
    for (qsizetype i = 0; i < m_items.size() && excess > 0; ++i) {
        if (!remove[i]) {
            remove[i] = true;
            --excess;
        }
    }

    QList<QcItemEntity> kept;
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (remove[i]) {
            m_discarded += m_items[i].stack.count;
        } else {
            kept.append(m_items[i]);
        }
    }

    qCWarning(lcItems) << "Too many dropped items in the world; discarded" << (m_items.size() - kept.size()) << "of them";
    m_items = std::move(kept);
}

void QcItemEntityManager::clear()
{
    m_items.clear();
    m_queue.clear();
    m_stirred.clear();
    m_activeCount = 0;
}

void QcItemEntityManager::restore(const QList<Drop> &drops, const std::function<quint32()> &nextEntityId)
{
    clear();
    adopt(drops, nextEntityId);
}

QList<QcItemEntityManager::Drop> QcItemEntityManager::takeQueued(const std::function<bool(const Drop &)> &match)
{
    QList<Drop> taken;
    m_queue.removeIf([&](const Drop &drop) {
        if (!match(drop)) {
            return false;
        }
        taken.append(drop);
        return true;
    });
    return taken;
}

void QcItemEntityManager::adopt(const QList<Drop> &drops, const std::function<quint32()> &nextEntityId)
{
    for (const Drop &drop : drops) {
        if (drop.stack.isEmpty() || !m_content->isValidItem(drop.stack.item)) {
            continue;
        }

        // Beyond the total limit drops wait in the (bounded) queue, as before saving.
        if (m_items.size() >= m_totalLimit) {
            enqueue(drop);
            continue;
        }

        QcItemEntity item;
        item.id = nextEntityId();
        item.stack = drop.stack;
        item.motion.position = drop.position;
        item.motion.velocity = drop.velocity;
        item.pickupDelay = drop.pickupDelay;
        item.age = drop.age;
        item.kept = drop.kept;
        m_items.append(item);
    }

    // Recounted on the first tick, when it is known which chunks are loaded.
    m_activeCount = 0;
}

qsizetype QcItemEntityManager::removeWhere(const std::function<bool(const QcItemEntity &)> &match)
{
    return m_items.removeIf([&match](const QcItemEntity &item) { return !item.surplus && match(item); });
}

bool QcItemEntityManager::take(quint32 id, int count)
{
    const auto it = std::ranges::find(m_items, id, &QcItemEntity::id);
    if (it == m_items.end() || it->surplus || it->stack.isEmpty() || count <= 0 || it->stack.count < count) {
        return false;
    }

    it->stack.count -= count;
    if (it->stack.count <= 0) {
        m_items.erase(it);
    }
    return true;
}

void QcItemEntityManager::setLimits(qsizetype active, qsizetype queued, qsizetype total)
{
    m_activeLimit = active;
    m_queueLimit = queued;
    m_totalLimit = total;
}
