// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientBeacon.h"

bool QcClientBeacon::sameState(const QcMsg::BeaconState &a, const QcMsg::BeaconState &b) const
{
    return a.payment == b.payment && a.level == b.level && a.primary == b.primary && a.secondary == b.secondary;
}

bool QcClientBeacon::choose(const QString &primary, const QString &secondary)
{
    if (state().payment.isEmpty() || primary.isEmpty()) {
        return false;
    }
    return sendWhileOpen(QcMsg::ChooseBeaconEffects{primary, secondary});
}

bool QcClientBeacon::handle(const QcMessage &message)
{
    if (std::holds_alternative<QcMsg::OpenBeacon>(message)) {
        setState({});
    }
    return QcClientBlockScreenOf::handle(message);
}
