#include "worldgen/villages/QcVillagePlanCache.h"

#include "worldgen/villages/QcVillagePlan.h"

#include <QMutexLocker>

#include <algorithm>
#include <iterator>

namespace {

qint64 regionKey(int regionX, int regionZ)
{
    return (static_cast<qint64>(regionX) << 32) | static_cast<quint32>(regionZ);
}

} // namespace

QcVillagePlanCache::QcVillagePlanCache(qsizetype capacity)
    : m_capacity(std::max<qsizetype>(1, capacity))
{
}

qsizetype QcVillagePlanCache::size() const
{
    const QMutexLocker locker(&m_mutex);
    return m_entries.size();
}

std::shared_ptr<const QcVillagePlan> QcVillagePlanCache::plan(int regionX, int regionZ, const Planner &planner) const
{
    const qint64 key = regionKey(regionX, regionZ);
    {
        const QMutexLocker locker(&m_mutex);
        if (std::optional<std::shared_ptr<const QcVillagePlan>> found = findLocked(key)) {
            return *found;
        }
    }

    std::shared_ptr<const QcVillagePlan> plan = planner(regionX, regionZ);
    m_planned.fetch_add(1, std::memory_order_relaxed);

    const QMutexLocker locker(&m_mutex);
    if (std::optional<std::shared_ptr<const QcVillagePlan>> found = findLocked(key)) {
        return *found;
    }
    insertLocked(key, plan);
    return plan;
}

std::optional<std::shared_ptr<const QcVillagePlan>> QcVillagePlanCache::planned(int regionX, int regionZ) const
{
    const QMutexLocker locker(&m_mutex);
    return findLocked(regionKey(regionX, regionZ));
}

std::optional<std::shared_ptr<const QcVillagePlan>> QcVillagePlanCache::findLocked(qint64 key) const
{
    if (const auto it = m_entries.find(key); it != m_entries.end()) {
        it->used = ++m_clock;
        return it->plan;
    }

    const auto held = m_released.find(key);
    if (held == m_released.end()) {
        return std::nullopt;
    }
    std::shared_ptr<const QcVillagePlan> plan = held->lock();
    m_released.erase(held);
    if (!plan) {
        return std::nullopt;
    }
    insertLocked(key, plan);
    return plan;
}

void QcVillagePlanCache::insertLocked(qint64 key, std::shared_ptr<const QcVillagePlan> plan) const
{
    if (m_entries.size() >= m_capacity) {
        evictOldestLocked();
    }
    m_entries.insert(key, {std::move(plan), ++m_clock});
}

void QcVillagePlanCache::evictOldestLocked() const
{
    auto oldest = m_entries.begin();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->used < oldest->used) {
            oldest = it;
        }
    }
    if (oldest == m_entries.end()) {
        return;
    }
    if (oldest->plan) {
        m_released.insert(oldest.key(), oldest->plan);
    }
    m_entries.erase(oldest);

    // The released list keeps only plans that are still held: emptied of the others now and then.
    if (m_released.size() > m_capacity) {
        for (auto it = m_released.begin(); it != m_released.end();) {
            it = it->expired() ? m_released.erase(it) : std::next(it);
        }
    }
}
