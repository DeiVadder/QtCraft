// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/preview/QcFramePacer.h"

#include <algorithm>

namespace {

// Timers fire up to a millisecond early; that still is the next frame.
constexpr qint64 earlyNs = 1'000'000;

} // namespace

void QcFramePacer::setLimit(int framesPerSecond)
{
    const int limit = std::max(0, framesPerSecond);
    if (limit != m_limit) {
        m_limit = limit;
        reset();
    }
}

qint64 QcFramePacer::intervalNs() const
{
    return m_limit > 0 ? 1'000'000'000 / m_limit : 0;
}

bool QcFramePacer::frameDue(qint64 nowNs)
{
    if (m_limit == 0) {
        return true;
    }
    if (m_nextFrameNs && nowNs < *m_nextFrameNs - earlyNs) {
        return false;
    }

    // On the grid of the previous frames, so a frame a little late does not lower the rate; after
    // a longer pause the grid starts anew.
    const bool onGrid = m_nextFrameNs && nowNs - *m_nextFrameNs < intervalNs();
    m_nextFrameNs = (onGrid ? *m_nextFrameNs : nowNs) + intervalNs();
    return true;
}

qint64 QcFramePacer::delayNs(qint64 nowNs) const
{
    if (m_limit == 0 || !m_nextFrameNs) {
        return 0;
    }
    return std::max<qint64>(0, *m_nextFrameNs - earlyNs - nowNs);
}
