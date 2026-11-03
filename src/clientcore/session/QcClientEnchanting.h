// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientBlockScreen.h"

#include <QList>

/*! \brief The enchanting table's screen the server opened for the local player (decision E-864):
    its item and lapis slots, the bookshelves that count and the three offers as the server last
    sent them (state()), and the offer the player takes.

    Items go in and out through the inventory's slot messages (item and lapis are the machine's
    input and fuel slots while the screen is open). Part of QcClientSession; GUI-free. */
class QcClientEnchanting
    : public QcClientBlockScreenOf<QcMsg::OpenEnchanting, QcMsg::EnchantingState, QcMsg::CloseEnchanting>
{
    Q_OBJECT

public:
    /*! Returns the item and lapis slots and an unused output slot, laid out like a machine's
        slots for the inventory model. */
    [[nodiscard]] QList<QcItemStack> slotStacks() const { return {state().item, state().lapis, {}}; }
    /*! Asks the server for offer \a slot (0–2). Returns false when no screen is open or the slot is
        invalid. */
    bool choose(int slot);

protected:
    /*! Returns whether \a a and \a b have the same slots, shelves and offers (the rune seed
        changes only along with them). */
    bool sameState(const QcMsg::EnchantingState &a, const QcMsg::EnchantingState &b) const override;
};
