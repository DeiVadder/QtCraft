// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Qt>

/*! \brief When the mouse is captured for looking around, as a small state machine without a window:
    the game asks it after every change of the screens and before acting on a mouse button.

    Closing a screen (inventory, chest, furnace, pause menu, settings, handbook, chat, death
    screen) brings the capture back at once. A button still held at that moment, or the click
    that captured the mouse, never mines or places: its release is swallowed. */
class QcMouseCapture
{
public:
    /*! What to do with the cursor. */
    enum class Change {
        None,    // leave the cursor as it is
        Capture, // capture the mouse
        Release  // free the cursor
    };

    /*! \brief What the game is doing now, as far as the capture cares. */
    struct Situation
    {
        /*! Playing, no screen open, alive. */
        bool worldInput = false;
        /*! The game's window has the focus. */
        bool windowActive = false;
        /*! Scripted runs never capture the mouse. */
        bool backgroundRun = false;
        /*! Buttons held right now. */
        Qt::MouseButtons held = Qt::NoButton;
    };

    /*! Returns what to do with the cursor after the situation changed to \a now. */
    Change update(Situation now);
    /*! A click into the world while the mouse is free captures it; the click itself does nothing.
        Returns what to do with the cursor after a click of \a button in the situation \a now. */
    Change clickedIntoWorld(Situation now, Qt::MouseButton button);
    /*! Returns whether a release of \a button while captured acts (mining stops, a use ends);
        false for swallowed ones. */
    bool acceptRelease(Qt::MouseButton button);

    /*! Returns whether the mouse is captured. */
    bool captured() const { return m_captured; }
    /*! The capture ended for another reason (window lost the focus, world closed). */
    void released() { m_captured = false; }
    /*! Returns whether the world took the input back at the last update without capturing
        (scripted run, inactive window); the test hooks report it. */
    bool returnedWithoutCapture() const { return m_returnedWithoutCapture; }

private:
    Change capture(Qt::MouseButtons swallow);

    bool m_captured = false;
    bool m_worldInput = false;
    bool m_returnedWithoutCapture = false;
    Qt::MouseButtons m_swallowed = Qt::NoButton;
};
