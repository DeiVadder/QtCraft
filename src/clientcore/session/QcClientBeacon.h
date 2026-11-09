// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientBlockScreen.h"

#include <QList>

/*! \brief The beacon's screen the server opened for the local player (decision E-1142): its
    payment slot, level and effects as the server last sent them (state()); the player chooses the
    effects and pays.

    The payment goes in and out through the inventory's slot messages (the slot is the machine's
    input slot while the screen is open). Part of QcClientSession; GUI-free. */
class QcClientBeacon : public QcClientBlockScreenOf<QcMsg::OpenBeacon, QcMsg::BeaconState, QcMsg::CloseBeacon>
{
    Q_OBJECT

public:
    /*! Returns the payment slot, laid out like a machine's input slot for the inventory model. */
    [[nodiscard]] QList<QcItemStack> slotStacks() const { return {state().payment}; }
    /*! Asks the server to give \a primary and \a secondary (empty: none) for the payment in the
        slot. Returns false when the slot is empty, no primary is chosen or the screen is closed;
        the server checks the rest. */
    bool choose(const QString &primary, const QString &secondary);
    /*! Handles \a message as QcClientBlockScreenOf::handle(); an OpenBeacon also forgets the
        last state, so the state the server sends for the beacon opened always arrives as a change
        (opening another beacon may find the same level and effects). Returns whether the message
        was the screen's. */
    bool handle(const QcMessage &message) override;

protected:
    /*! Returns whether \a a and \a b have the same payment, level and effects. */
    bool sameState(const QcMsg::BeaconState &a, const QcMsg::BeaconState &b) const override;
};
