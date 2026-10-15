#pragma once

#include "QcServerLimits.h"
#include "QcAabb.h"
#include "QcArmourStand.h"
#include "QcEquipment.h"
#include "QcStandPose.h"

#include <QList>
#include <QString>

// Armour stands of a world (decision E-238): placed entities that wear armour and hold an item,
// with a pose, but no AI, no health and no physics; they stand where they were placed. Not
// creatures: they never count towards the mob limits. Pure data; QcServerStands does what players
// and commands do to them.
class QcArmourStandManager
{
public:
    // Stands in the world by default (QcServerLimits::maxArmourStands); a save holds at most the
    // highest limit.
    static constexpr int maxStands = int(QcServerLimits{}.maxArmourStands);
    static constexpr int maxStoredStands = 65536;
    static constexpr int maxNameLength = 32;

    static QcAabb bodyOf(const QcArmourStand &stand);

    const QList<QcArmourStand> &stands() const { return m_stands; }
    // For moving stands from outside (pistons push them, decision E-337); never add or remove through it.
    QList<QcArmourStand> &editableStands() { return m_stands; }
    QcArmourStand *find(quint32 id);
    const QcArmourStand *find(quint32 id) const;
    // False when the limit is reached.
    bool add(const QcArmourStand &stand);
    // Removes the stand and returns it (to drop what it carried).
    std::optional<QcArmourStand> remove(quint32 id);
    bool isFull() const { return m_stands.size() >= m_maxStands; }
    qsizetype limit() const { return m_maxStands; }
    void setLimit(qsizetype limit) { m_maxStands = limit; }
    // Advances the hit timers and removes stands in the void.
    void tick(double dt);
    // Replaces all stands, e.g. from a save; entries beyond the limit are left out and counted.
    qsizetype restore(QList<QcArmourStand> stands);
    // Adds the stands of a chunk that came back (decision E-374); returns how many were left out at the limit.
    qsizetype adopt(const QList<QcArmourStand> &stands);

private:
    QList<QcArmourStand> m_stands;
    qsizetype m_maxStands = maxStands;
};
