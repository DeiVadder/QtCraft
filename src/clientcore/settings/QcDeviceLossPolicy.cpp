// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcDeviceLossPolicy.h"

#include <algorithm>

QcDeviceLossPolicy::QcDeviceLossPolicy(qint64 coolDownMs)
    : m_coolDownMs(std::max<qint64>(0, coolDownMs))
{
}

QcDeviceLossPolicy::Response QcDeviceLossPolicy::deviceLost(qint64 nowMs, bool realistic)
{
    // The classic look was drawn (as during a cool-down): not the realistic mode's fault.
    if (!realistic || fellBack()) {
        return Response::Recover;
    }

    const std::optional<qint64> last = lastRealisticLossMs();
    const bool repeated = last && nowMs - *last <= repeatWindowMs;
    setLastRealisticLossMs(nowMs);
    if (repeated) {
        setFellBack(true);
        return Response::FallBack;
    }
    setCoolDownEndMs(nowMs + coolDownMs());
    return Response::CoolDown;
}

bool QcDeviceLossPolicy::realisticAllowed(qint64 nowMs) const
{
    return !fellBack() && nowMs >= coolDownEndMs();
}

qint64 QcDeviceLossPolicy::coolDownLeftMs(qint64 nowMs) const
{
    return fellBack() ? 0 : std::max<qint64>(0, coolDownEndMs() - nowMs);
}

void QcDeviceLossPolicy::choseAgain()
{
    *this = QcDeviceLossPolicy(coolDownMs());
}
