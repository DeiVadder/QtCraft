// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientBlockScreen.h"

#include <QList>

/*! \brief The smithing table's screen the server opened for the local player (decision E-1115):
    its template, base and addition slots and the output as the server last sent them (state());
    the player takes the output.

    Items go in and out through the inventory's slot messages (the three slots are the machine's
    input, fuel and output slots while the screen is open). Part of QcClientSession; GUI-free. */
class QcClientSmithing : public QcClientBlockScreenOf<QcMsg::OpenSmithing, QcMsg::SmithingState, QcMsg::CloseSmithing>
{
    Q_OBJECT

public:
    /*! Returns the template, base and addition slots and the output, laid out like a machine's
        slots for the inventory model. */
    [[nodiscard]] QList<QcItemStack> slotStacks() const { return {state().templateStack, state().base, state().addition, state().output}; }
    /*! Asks the server for the output it last sent. Returns false when there is none. */
    bool forge();

protected:
    /*! Returns whether \a a and \a b have the same slots and output. */
    bool sameState(const QcMsg::SmithingState &a, const QcMsg::SmithingState &b) const override;
};
