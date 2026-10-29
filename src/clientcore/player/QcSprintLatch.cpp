// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcSprintLatch.h"

void QcSprintLatch::forwardPressed(qint64 timestampMs)
{
    m_doubleTapped = m_lastForwardPress && timestampMs >= *m_lastForwardPress && timestampMs - *m_lastForwardPress <= doubleTapMs;
    m_lastForwardPress = timestampMs;
}

void QcSprintLatch::reset()
{
    m_active = false;
    m_doubleTapped = false;
    m_lastForwardPress.reset();
}

bool QcSprintLatch::update(bool forwardHeld, bool sprintHeld, bool sneakHeld)
{
    if (!forwardHeld || sneakHeld) {
        m_active = false;
        m_doubleTapped = false;
        return false;
    }

    if (sprintHeld || m_doubleTapped) {
        m_active = true;
    }
    m_doubleTapped = false;
    return m_active;
}
