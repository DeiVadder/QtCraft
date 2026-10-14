#include "QcArmourStandManager.h"

#include "QcWorldConstants.h"

#include <algorithm>

QcAabb QcArmourStandManager::bodyOf(const Stand &stand)
{
    const double scale = stand.pose.small ? 0.5 : 1.0;
    const double half = QcStandPose::bodyWidth * scale * 0.5;
    const QcVec3 &p = stand.position;
    return {{p.x - half, p.y, p.z - half}, {p.x + half, p.y + QcStandPose::bodyHeight * scale, p.z + half}};
}

QcArmourStandManager::Stand *QcArmourStandManager::find(quint32 id)
{
    const auto it = std::ranges::find(m_stands, id, &Stand::id);
    return it == m_stands.end() ? nullptr : &*it;
}

const QcArmourStandManager::Stand *QcArmourStandManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_stands, id, &Stand::id);
    return it == m_stands.end() ? nullptr : &*it;
}

bool QcArmourStandManager::add(const Stand &stand)
{
    if (isFull()) {
        return false;
    }
    m_stands.append(stand);
    return true;
}

std::optional<QcArmourStandManager::Stand> QcArmourStandManager::remove(quint32 id)
{
    const auto it = std::ranges::find(m_stands, id, &Stand::id);
    if (it == m_stands.end()) {
        return std::nullopt;
    }

    Stand removed = *it;
    m_stands.erase(it);
    return removed;
}

void QcArmourStandManager::tick(double dt)
{
    // Stands do not move; one that is in the void anyway (a save edited by hand) goes, without
    // dropping its armour (decision E-288).
    m_stands.removeIf([](const Stand &stand) { return QcWorld::isInVoid(stand.position.y); });

    for (Stand &stand : m_stands) {
        stand.sinceHit = std::min(breakWindow, stand.sinceHit + dt);
    }
}

qsizetype QcArmourStandManager::restore(QList<Stand> stands)
{
    const qsizetype dropped = std::max<qsizetype>(0, stands.size() - m_maxStands);
    if (dropped > 0) {
        stands.resize(m_maxStands);
    }
    m_stands = std::move(stands);
    return dropped;
}
