// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcRefusalThrottle.h"

bool QcRefusalThrottle::report(quint64 tick)
{
    if (tick < m_nextReport) {
        return false;
    }
    m_nextReport = tick + reportTicks;
    return true;
}
