// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QSet>
#include <QtGlobal>

/*! \brief Who waits in a portal and who may not travel yet (decision E-1024).

    A player travels after standing in a portal for its wait time (in survival 4 s, in creative at
    once); after a journey he has to step out of the portal before it counts again. A creature,
    item or minecart travels at once, then not again for the portal's cooldown. Kept by entity id,
    which is unique across the dimensions; lives in the server's thread. */
class QcTravellers
{
public:
    /*! The player \a id stands in a portal for another tick, which he leaves through after
        \a waitTicks ticks inside (0 or 1: at once). Returns whether he travels now. */
    bool stand(quint32 id, int waitTicks);
    /*! The player \a id stands in no portal: his wait starts over, and he may travel again. */
    void stepOut(quint32 id);
    /*! Returns whether the player \a id has stood in a portal since he arrived through one. */
    bool mustStepOut(quint32 id) const { return m_mustStepOut.contains(id); }
    /*! Returns how many ticks the player \a id has stood in a portal. */
    int ticksInside(quint32 id) const { return m_ticksInside.value(id); }
    /*! The player \a id arrived through a portal: he stands in the one at his arrival. */
    void playerArrived(quint32 id);
    /*! Forgets the wait and the step-out of every player not in \a playing (players who left). */
    void keepPlayers(const QSet<quint32> &playing);

    /*! Returns whether the creature, item or minecart \a id may travel in game tick \a tick. */
    bool mayTravel(quint32 id, quint64 tick) const { return m_resting.value(id) <= tick; }
    /*! The creature, item or minecart \a id arrived; it may travel again from game tick
        \a until. */
    void entityArrived(quint32 id, quint64 until) { m_resting.insert(id, until); }
    /*! Forgets the cooldowns that ended before game tick \a tick. */
    void forgetRested(quint64 tick);

private:
    // NOLINT(qc-member-access): wait state changed in place only by this class's own methods.
    QHash<quint32, int> m_ticksInside;
    // NOLINT(qc-member-access): changed in place only by this class's own methods.
    QSet<quint32> m_mustStepOut;
    // NOLINT(qc-member-access): changed in place only by this class's own methods.
    QHash<quint32, quint64> m_resting;
};
