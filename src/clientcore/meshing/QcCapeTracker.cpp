// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcCapeTracker.h"

void QcCapeTracker::beginFrame(double seconds, double time)
{
    ++m_frame;
    m_seconds = seconds;
    m_time = time;
}

QcCapeSwing::Angles QcCapeTracker::advance(quint32 id, const QcCapeDrive &drive)
{
    const auto found = m_swings.find(id);
    if (found == m_swings.end()) {
        Tracked &added = m_swings[id];
        added.swing.settle(drive, m_time);
        added.frame = m_frame;
        return added.swing.angles();
    }

    found->swing.advance(drive, m_seconds, m_time);
    found->frame = m_frame;
    return found->swing.angles();
}

void QcCapeTracker::endFrame()
{
    m_swings.removeIf([this](QHash<quint32, Tracked>::iterator it) { return it->frame != m_frame; });
}
