#pragma once

#include "QcPlayerStorage.h"

struct QcServerClient;

// What is saved of a player and how it comes back (decision E-367): the state of a connected player
// as a QcPlayerData for QcPlayerStorage, and a saved one put onto a player who joins again.
namespace QcPlayerRecords {

// The player's saved state. Items in an open crafting grid go into the inventory, as if the grid had
// been closed; `lost` counts those that did not fit.
QcPlayerData of(const QcServerClient &client, const QcContentRegistry &content, int &lost);
// Puts a saved state onto a player who joins again, with the rules of the saved game mode applied.
void restore(QcServerClient &client, const QcPlayerData &player);

} // namespace QcPlayerRecords
