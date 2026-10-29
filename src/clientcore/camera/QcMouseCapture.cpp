// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcMouseCapture.h"

QcMouseCapture::Change QcMouseCapture::update(Situation now)
{
    const bool returned = now.worldInput && !m_worldInput;
    m_worldInput = now.worldInput;
    m_returnedWithoutCapture = false;

    if (!now.worldInput) {
        if (!m_captured) {
            return Change::None;
        }
        m_captured = false;
        m_swallowed = Qt::NoButton;
        return Change::Release;
    }

    // Only the step back from a screen into the world captures: a window that comes back to the
    // front waits for a click, so switching applications never grabs the mouse.
    if (!returned || m_captured) {
        return Change::None;
    }
    if (now.backgroundRun || !now.windowActive) {
        m_returnedWithoutCapture = true;
        return Change::None;
    }
    return capture(now.held);
}

QcMouseCapture::Change QcMouseCapture::clickedIntoWorld(Situation now, Qt::MouseButton button)
{
    m_worldInput = now.worldInput;
    if (m_captured || !now.worldInput || now.backgroundRun || !now.windowActive) {
        return Change::None;
    }
    return capture(now.held | button);
}

bool QcMouseCapture::acceptRelease(Qt::MouseButton button)
{
    if (m_swallowed.testFlag(button)) {
        m_swallowed.setFlag(button, false);
        return false;
    }
    return m_captured;
}

QcMouseCapture::Change QcMouseCapture::capture(Qt::MouseButtons swallow)
{
    m_captured = true;
    m_swallowed = swallow;
    return Change::Capture;
}
