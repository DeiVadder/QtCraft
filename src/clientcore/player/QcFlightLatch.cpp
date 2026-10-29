// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcFlightLatch.h"

bool QcFlightLatch::jumpPressed(qint64 timestampMs)
{
    const bool doubleTap = m_lastPress && timestampMs >= *m_lastPress && timestampMs - *m_lastPress <= doubleTapMs;
    // The press that completes a pair does not count as the first of the next one.
    m_lastPress = doubleTap ? std::nullopt : std::optional<qint64>(timestampMs);
    return doubleTap;
}
