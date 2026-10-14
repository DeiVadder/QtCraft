#include "QcArmourStandManager.h"

#include "QcWorldConstants.h"

#include <algorithm>

QcAabb QcArmourStandManager::bodyOf(const QcArmourStand &stand)
{
    const double scale = stand.pose.small ? 0.5 : 1.0;
    const double half = QcStandPose::bodyWidth * scale * 0.5;
    const QcVec3 &p = stand.motion.position;
    return {{p.x - half, p.y, p.z - half}, {p.x + half, p.y + QcStandPose::bodyHeight * scale, p.z + half}};
}

QcArmourStand *QcArmourStandManager::find(quint32 id)
{
    const auto it = std::ranges::find(m_stands, id, &QcArmourStand::id);
    return it == m_stands.end() ? nullptr : &*it;
}

const QcArmourStand *QcArmourStandManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_stands, id, &QcArmourStand::id);
    return it == m_stands.end() ? nullptr : &*it;
}

bool QcArmourStandManager::add(const QcArmourStand &stand)
{
    if (isFull()) {
        return false;
    }
    m_stands.append(stand);
    return true;
}

std::optional<QcArmourStand> QcArmourStandManager::remove(quint32 id)
{
    const auto it = std::ranges::find(m_stands, id, &QcArmourStand::id);
    if (it == m_stands.end()) {
        return std::nullopt;
    }

    QcArmourStand removed = *it;
    m_stands.erase(it);
    return removed;
}

void QcArmourStandManager::tick(double dt)
{
    // Stands do not move; one that is in the void anyway (a save edited by hand) goes, without
    // dropping its armour (decision E-288).
    m_stands.removeIf([](const QcArmourStand &stand) { return QcWorld::isInVoid(stand.motion.position.y); });

    // The break window runs out also while a stand rests: it is a time limit for the player who hit
    // it, not a simulation of the stand, and it can only close (a resting stand cannot be hit).
    for (QcArmourStand &stand : m_stands) {
        stand.sinceHit = std::min(QcArmourStand::breakWindow, stand.sinceHit + dt);
    }
}

qsizetype QcArmourStandManager::restore(QList<QcArmourStand> stands)
{
    const qsizetype dropped = std::max<qsizetype>(0, stands.size() - m_maxStands);
    if (dropped > 0) {
        stands.resize(m_maxStands);
    }
    m_stands = std::move(stands);
    return dropped;
}
