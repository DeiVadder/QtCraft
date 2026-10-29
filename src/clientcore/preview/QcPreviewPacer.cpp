// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/preview/QcPreviewPacer.h"

#include <algorithm>

void QcPreviewPacer::setFramesPerSecond(int framesPerSecond)
{
    m_framesPerSecond = std::clamp(framesPerSecond, 1, maxFramesPerSecond);
}

bool QcPreviewPacer::frameDue(bool visible, qint64 nowMs)
{
    if (!visible) {
        hide();
        return false;
    }

    // Timers fire up to a millisecond early; that still is the next frame.
    constexpr qint64 earlyMs = 1;
    if (m_nextFrameMs && nowMs < *m_nextFrameMs - earlyMs) {
        return false;
    }

    // On the grid of the previous frames, so early or late ticks do not change the rate; after a
    // longer pause the grid starts anew.
    const bool onGrid = m_nextFrameMs && nowMs - *m_nextFrameMs < intervalMs();
    m_nextFrameMs = (onGrid ? *m_nextFrameMs : nowMs) + intervalMs();
    return true;
}
