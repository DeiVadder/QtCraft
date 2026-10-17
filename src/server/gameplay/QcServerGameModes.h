#pragma once

#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcGameMode.h"
#include "common/protocol/QcMessages.h"

#include <optional>

class QcContentRegistry;
class QcServerCrafting;
class QcServerInventory;
struct QcServerClient;
class QcServerState;

// The game modes of the players (decisions E-282 to E-285): the mode of new players, changing a
// player's mode with the inventory set that belongs to it, the items of the creative catalogue and
// picking blocks with the middle mouse button.
class QcServerGameModes
{
public:
    QcServerGameModes(QcServerState &state, QcServerInventory &inventory, QcServerCrafting &crafting);

    // The mode of a player who joins the world for the first time: the server's setting
    // (server.json, an unsaved world) or else the world's default.
    QcGameMode defaultMode() const;
    // Puts the player into `mode`. A mode of the other inventory set swaps the inventory and armour
    // with the kept ones (the crafting grid goes back into the old inventory first); a mode without
    // flight ends a flight. Tells the player and sends the inventory.
    void setMode(QcServerClient &client, QcGameMode mode);
    // What follows from the mode without changing it (frozen hunger); after joining.
    static void applyRules(QcServerClient &client);
    static void send(QcServerClient &client);

    // QcMsg::CreativeItem: refused (the inventory is sent again) unless the player's mode has the
    // item catalogue.
    void creativeItem(QcServerClient &client, QcMsg::CreativeItem message);
    // QcMsg::PickBlock: selects the block's item in the hotbar or swaps it in from the storage
    // rows; in creative mode it comes from the catalogue when the inventory has none.
    void pickBlock(QcServerClient &client, QcBlockPos pos);

    // The item that stands for a block: the block's own item, else one that places it (seeds for a
    // crop), else its first drop.
    [[nodiscard]] static std::optional<QcItemId> itemForBlock(const QcContentRegistry &content, QcBlockId block);

private:
    // A stack of a known item within its limits; empty for unknown items and no count.
    QcItemStack validStack(QcItemStack stack) const;
    void putIntoSlot(QcServerClient &client, int slot, QcItemStack stack) const;
    // Whether the armour slot took the stack (empty clears it).
    bool putIntoArmourSlot(QcServerClient &client, int slot, QcItemStack stack) const;
    // The hotbar slot a picked item goes to: the selected one when empty, else the first empty
    // one, else the selected one.
    static int pickTarget(const QcServerClient &client);

    QcServerState &m_state;
    QcServerInventory &m_inventory;
    QcServerCrafting &m_crafting;
};
