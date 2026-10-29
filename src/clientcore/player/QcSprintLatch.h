// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <optional>

// Whether the player sprints, from the keys, as in the model (decision E-280): a press of the
// sprint key while moving forward, or a double tap of the forward key, starts a sprint, which then
// lasts while the forward key stays held, also after the sprint key is released. Releasing
// forward or sneaking ends it. Taps are taken from the key press events with their time, so a
// short tap between two frames still counts as the first of a double tap.
class QcSprintLatch
{
public:
    // Two presses of the forward key at most this far apart start a sprint.
    static constexpr qint64 doubleTapMs = 300;

    // A press of the forward key (not an auto-repeat) at `timestampMs` (any monotonic clock).
    void forwardPressed(qint64 timestampMs);
    // Once per frame with the held keys; returns whether to sprint.
    bool update(bool forwardHeld, bool sprintHeld, bool sneakHeld);
    bool active() const { return m_active; }
    // Forgets the sprint and earlier taps: a screen opened, the player died or the window lost the
    // focus, so a tap before and one after do not make a double tap.
    void reset();

private:
    bool m_active = false;
    bool m_doubleTapped = false;
    std::optional<qint64> m_lastForwardPress;
};
