#pragma once

#include <QtGlobal>

#include <optional>

// Two presses of the jump key in quick succession toggle flying in creative mode (decision E-284).
// Like the sprint's double tap (QcSprintLatch), taps are taken from the key press events with their
// time, so a short tap between two frames still counts; a third press after a toggle starts a new
// pair instead of toggling again.
class QcFlightLatch
{
public:
    // Two presses at most this far apart toggle flying (the model waits about as long).
    static constexpr qint64 doubleTapMs = 300;

    // A press of the jump key (not an auto-repeat) at `timestampMs` (any monotonic clock); true when
    // it completes a double tap.
    bool jumpPressed(qint64 timestampMs);
    // Forgets earlier taps: a screen opened, the player died or the window lost the focus.
    void reset() { m_lastPress.reset(); }

private:
    std::optional<qint64> m_lastPress;
};
