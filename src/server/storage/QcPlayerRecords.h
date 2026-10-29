// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/storage/QcPlayerStorage.h"

struct QcServerClient;

// What is saved of a player and how it comes back (decision E-367): the state of a connected player
// as a QcPlayerData for QcPlayerStorage, and a saved one put onto a player who joins again.
namespace QcPlayerRecords {

// A player's saved state and the number of items of an open crafting grid that did not fit into
// its inventory.
struct Record
{
    QcPlayerData player;
    int lost = 0;
};

// The player's saved state. Items in an open crafting grid go into the inventory, as if the grid had
// been closed.
[[nodiscard]] Record of(const QcServerClient &client, const QcContentRegistry &content);
// Puts a saved state onto a player who joins again, with the rules of the saved game mode applied.
void restore(QcServerClient &client, const QcPlayerData &player);

} // namespace QcPlayerRecords
