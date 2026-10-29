// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Qt>

// When the mouse is captured for looking around, as a small state machine without a window: the
// game asks it after every change of the screens and before acting on a mouse button.
//
// Closing a screen (inventory, chest, furnace, pause menu, settings, handbook, chat, death screen)
// brings the capture back at once. A button still held at that moment, or the click that captured
// the mouse, never mines or places: its release is swallowed.
class QcMouseCapture
{
public:
    enum class Change { None, Capture, Release };

    struct Situation
    {
        // Playing, no screen open, alive.
        bool worldInput = false;
        bool windowActive = false;
        // Scripted runs never capture the mouse.
        bool backgroundRun = false;
        // Buttons held right now.
        Qt::MouseButtons held = Qt::NoButton;
    };

    // What to do with the cursor after the situation changed.
    Change update(Situation now);
    // A click into the world while the mouse is free captures it; the click itself does nothing.
    Change clickedIntoWorld(Situation now, Qt::MouseButton button);
    // Whether a release while captured acts (mining stops, a use ends); false for swallowed ones.
    bool acceptRelease(Qt::MouseButton button);

    bool captured() const { return m_captured; }
    // The capture ended for another reason (window lost the focus, world closed).
    void released() { m_captured = false; }
    // The world took the input back at the last update without capturing (scripted run, inactive
    // window); the test hooks report it.
    bool returnedWithoutCapture() const { return m_returnedWithoutCapture; }

private:
    Change capture(Qt::MouseButtons swallow);

    bool m_captured = false;
    bool m_worldInput = false;
    bool m_returnedWithoutCapture = false;
    Qt::MouseButtons m_swallowed = Qt::NoButton;
};
