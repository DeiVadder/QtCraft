// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/storage/QcPlayerStorage.h"

struct QcServerClient;

/*! \brief What is saved of a player and how it comes back (decision E-367): the state of a
    connected player as a QcPlayerData for QcPlayerStorage, and a saved one put onto a player who
    joins again. */
namespace QcPlayerRecords {

/*! \brief A player's saved state and the number of items of an open crafting grid that did not
    fit into its inventory. \qcfields */
struct Record
{
    QcPlayerData player;
    int lost = 0;
};

/*! Returns the saved state of \a client, items named after \a content. Items in an open crafting
    grid go into the inventory, as if the grid had been closed. */
[[nodiscard]] Record of(const QcServerClient &client, const QcContentRegistry &content);
/*! Puts the saved state \a player onto \a client, who joins again, with the rules of the saved
    game mode applied. */
void restore(QcServerClient &client, const QcPlayerData &player);

} // namespace QcPlayerRecords
