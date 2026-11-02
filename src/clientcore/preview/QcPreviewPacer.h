// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <optional>

/*! \brief Frame pacing of the character preview: it draws only while it is visible and at most at
    its frame rate, so an open screen with a preview adds a small, bounded load and a hidden one
    none.

    The clock is passed in (milliseconds of a monotonic clock), so the rules are testable. */
class QcPreviewPacer
{
public:
    /*! Frame rate of a new pacer. */
    static constexpr int defaultFramesPerSecond = 30;
    /*! Highest frame rate. */
    static constexpr int maxFramesPerSecond = 60;

    /*! Returns the frame rate. */
    int framesPerSecond() const { return m_framesPerSecond; }
    /*! Sets the frame rate to \a framesPerSecond, clamped to 1..maxFramesPerSecond. */
    void setFramesPerSecond(int framesPerSecond);
    /*! Returns the shortest time between two frames in milliseconds. */
    int intervalMs() const { return 1000 / m_framesPerSecond; }

    /*! Returns whether to draw a frame at \a nowMs; when it says yes, the frame counts as drawn.
        Never while not \a visible; the first frame after becoming visible at once, later ones an
        interval apart (a timer a millisecond early still counts). */
    bool frameDue(bool visible, qint64 nowMs);
    /*! Hidden: the next visible frame is drawn at once. */
    void hide() { m_nextFrameMs.reset(); }

private:
    int m_framesPerSecond = defaultFramesPerSecond;
    std::optional<qint64> m_nextFrameMs;
};
