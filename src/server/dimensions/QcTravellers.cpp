// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcTravellers.h"

bool QcTravellers::stand(quint32 id, int waitTicks)
{
    if (mustStepOut(id)) {
        return false;
    }

    const int inside = ticksInside(id) + 1;
    if (inside < waitTicks) {
        m_ticksInside.insert(id, inside);
        return false;
    }
    m_ticksInside.remove(id);
    return true;
}

void QcTravellers::stepOut(quint32 id)
{
    m_ticksInside.remove(id);
    m_mustStepOut.remove(id);
}

void QcTravellers::playerArrived(quint32 id)
{
    m_ticksInside.remove(id);
    m_mustStepOut.insert(id);
}

void QcTravellers::forgetRested(quint64 tick)
{
    m_resting.removeIf([tick](const auto &entry) { return entry.value() <= tick; });
}

void QcTravellers::keepPlayers(const QSet<quint32> &playing)
{
    m_ticksInside.removeIf([&playing](const auto &entry) { return !playing.contains(entry.key()); });
    m_mustStepOut.removeIf([&playing](quint32 id) { return !playing.contains(id); });
}
