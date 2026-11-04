// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobs/QcMobManager.h"

#include <QHash>

class QcServerDimension;
class QcServerState;

/*! \brief A part of the server that hears creatures leave, in every dimension (decision E-988):
    subscribes to QcMobManager::onDeparture in each open dimension and in each that opens later,
    and calls the listener in the Scope of the dimension the creature leaves. Unsubscribes when it
    goes, which must be before \a state does. */
class QcDimensionDepartures
{
public:
    /*! Calls \a listener for every creature that leaves a dimension of \a state. */
    QcDimensionDepartures(QcServerState &state, QcMobManager::DepartureListener listener);
    /*! Listeners refer to this object. */
    Q_DISABLE_COPY_MOVE(QcDimensionDepartures)
    ~QcDimensionDepartures();

private:
    QcServerState &m_state;
    QcMobManager::DepartureListener m_listener;
    // A dimension that failed to open leaves its entry; only open dimensions are looked up.
    QHash<const QcServerDimension *, QcMobManager::DepartureToken> m_tokens;
};
