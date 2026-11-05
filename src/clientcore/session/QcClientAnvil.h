// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientBlockScreen.h"

#include <QList>

/*! \brief The anvil's screen the server opened for the local player (decision E-866): its left and
    right slots, the name in its field, the output and its cost as the server last sent them
    (state()); the player renames and takes the output.

    Items go in and out through the inventory's slot messages (left and right are the machine's
    input and fuel slots while the screen is open). Part of QcClientSession; GUI-free. */
class QcClientAnvil : public QcClientBlockScreenOf<QcMsg::OpenAnvil, QcMsg::AnvilState, QcMsg::CloseAnvil>
{
    Q_OBJECT

public:
    /*! Returns the left and right slot and the output, laid out like a machine's slots for the
        inventory model. */
    [[nodiscard]] QList<QcItemStack> slotStacks() const { return {state().left, state().right, state().output}; }
    /*! Sends \a name, the text of the name field. Returns false when no screen is open. */
    bool rename(const QString &name);
    /*! Asks the server for the output and cost it last sent. Returns false when there is none. */
    bool forge();

protected:
    /*! Returns whether \a a and \a b have the same slots, name, output and cost. */
    bool sameState(const QcMsg::AnvilState &a, const QcMsg::AnvilState &b) const override;
};
