#pragma once

#include "QcServerLimits.h"
#include "QcAabb.h"
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
    // A stand breaks when the same player hits it twice within this time (a single hit only wobbles
    // it). Longer than the attack cooldown (QcServerCombat::playerAttackCooldown), during which hits
    // do not count.
    static constexpr double breakWindow = 1.0;
    static constexpr int maxNameLength = 32;

    struct Stand
    {
        quint32 id = 0;
        QcVec3 position;
        float yaw = 0.0f;
        QcStandPose pose;
        QcEquipment equipment;
        QcItemStack held;
        // Shown above it when pose.showName is set; at most maxNameLength Unicode code points (QcNameText).
        QString name;
        // The cape it wears (an appearance of the kind cape, decision E-239); empty for none.
        QString cape;
        // The item it drops when broken (the one that placed it).
        QString item;
        // Seconds since the last hit; a second hit by `hitBy` within breakWindow breaks it.
        double sinceHit = breakWindow;
        // The player (entity id) whose hit started the break window; 0 for none.
        quint32 hitBy = 0;

        bool operator==(const Stand &) const = default;
    };

    static QcAabb bodyOf(const Stand &stand);

    const QList<Stand> &stands() const { return m_stands; }
    Stand *find(quint32 id);
    const Stand *find(quint32 id) const;
    // False when the limit is reached.
    bool add(const Stand &stand);
    // Removes the stand and returns it (to drop what it carried).
    std::optional<Stand> remove(quint32 id);
    bool isFull() const { return m_stands.size() >= m_maxStands; }
    qsizetype limit() const { return m_maxStands; }
    void setLimit(qsizetype limit) { m_maxStands = limit; }
    // Advances the hit timers and removes stands in the void.
    void tick(double dt);
    // Replaces all stands, e.g. from a save; entries beyond the limit are left out and counted.
    qsizetype restore(QList<Stand> stands);

private:
    QList<Stand> m_stands;
    qsizetype m_maxStands = maxStands;
};
