// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcStandPose.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcArmourStand.h"

#include <QList>
#include <QString>

/*! \brief Armour stands of a world (decision E-238): placed entities that wear armour and hold an
    item, with a pose, but no AI, no health and no physics; they stand where they were placed.

    Not creatures: they never count towards the mob limits. Pure data; QcServerStands does what
    players and commands do to them. */
class QcArmourStandManager
{
public:
    /*! Stands in the world by default (QcServerLimits::maxArmourStands); a save holds at most
        the highest limit. */
    static constexpr int maxStands = static_cast<int>(QcServerLimits{}.maxArmourStands);
    /*! Stands a save holds at most. */
    static constexpr int maxStoredStands = 65536;
    /*! Characters of a stand's name at most. */
    static constexpr int maxNameLength = 32;

    /*! Returns the box of the body of \a stand. */
    static QcAabb bodyOf(const QcArmourStand &stand);

    /*! Returns every stand. */
    const QList<QcArmourStand> &stands() const { return m_stands; }
    /*! Returns the stands for moving them from outside (pistons push them, decision E-337);
        never add or remove through it. */
    QList<QcArmourStand> &editableStands() { return m_stands; }
    /*! Returns the stand with entity \a id, or null when there is none. */
    QcArmourStand *find(quint32 id);
    /*! Returns the stand with entity \a id, or null when there is none. */
    const QcArmourStand *find(quint32 id) const;
    /*! Adds \a stand. Returns \c false when the limit is reached. */
    bool add(const QcArmourStand &stand);
    /*! Removes the stand with entity \a id. Returns it (to drop what it carried), or nothing
        when there is none. */
    std::optional<QcArmourStand> remove(quint32 id);
    /*! Returns whether the limit is reached. */
    bool isFull() const { return m_stands.size() >= m_maxStands; }
    /*! Returns the number of stands the world may hold. */
    qsizetype limit() const { return m_maxStands; }
    /*! Sets the number of stands the world may hold to \a limit. */
    void setLimit(qsizetype limit) { m_maxStands = limit; }
    /*! Advances the hit timers by \a dt seconds and removes stands in the void. */
    void tick(double dt);
    /*! Replaces all stands with \a stands, e.g. from a save. Returns how many entries beyond
        the limit were left out. */
    qsizetype restore(QList<QcArmourStand> stands);
    /*! Adds the \a stands of a chunk that came back (decision E-374); returns how many were left
        out at the limit. */
    qsizetype adopt(const QList<QcArmourStand> &stands);

private:
    QList<QcArmourStand> m_stands;
    qsizetype m_maxStands = maxStands;
};
