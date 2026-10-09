#pragma once

#include "QcBlockPos.h"
#include "QcContainerManager.h"
#include "QcItemStack.h"
#include "QcMachineManager.h"
#include "QcMessages.h"

#include <QList>
#include <QString>

#include <optional>

class QcServerMachines;
struct QcServerClient;
struct QcServerState;

// Inventories and crafting grids of the players: moving items (also into an open machine or chest),
// crafting, throwing and dropping items.
class QcServerInventory
{
public:
    static constexpr int craftingGridSlot = QcMsg::MoveInventoryItem::craftingGridSlot;
    static constexpr int machineSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    // Crafting needs a crafting table within this distance for 3 x 3 recipes.
    static constexpr double craftingTableReach = 5.0;
    // Speed of items thrown by a player.
    static constexpr double throwSpeed = 6.0;

    QcServerInventory(QcServerState &state, QcServerMachines &machines);

    void send(QcServerClient &client);
    void moveItem(QcServerClient &client, QcMsg::MoveInventoryItem move);
    void takeCraftingResult(QcServerClient &client);
    void craftRecipe(QcServerClient &client, const QcMsg::CraftRecipe &request);
    // Opens the 3 x 3 grid of the crafting table at `pos`.
    void openCraftingTable(QcServerClient &client, QcBlockPos pos);
    void closeCrafting(QcServerClient &client);
    // Moves the crafting grid into the inventory; what does not fit is thrown on the ground.
    void returnCraftingGrid(QcServerClient &client);
    void dropItem(QcServerClient &client, QcMsg::DropItem message);
    // Drops everything the player carries (death without keepInventory).
    void dropAll(QcServerClient &client);
    // Spawns the stack as an item entity thrown from the player's eyes along the view direction.
    void throwFromPlayer(const QcServerClient &client, QcItemStack stack);
    // Turns one item of the selected stack into `item` (filled bucket, firefly in a bottle); what
    // does not fit into the inventory is thrown.
    void exchangeHeld(QcServerClient &client, const QString &item);

private:
    // The grid of the current size as a size x size list, and the recipe it makes.
    QList<QcItemStack> craftingView(const QcServerClient &client) const;
    std::optional<qsizetype> craftingMatch(const QcServerClient &client) const;
    bool isNearCraftingTable(const QcServerClient &client) const;
    // Whether a crafting table stands in the column (dx, dy) around `feet` within reach.
    bool hasCraftingTableInColumn(QcBlockPos feet, int dx, int dy) const;
    // Whether the moved machine slots hold only fuel in the fuel slot and nothing new in the output.
    bool machineAccepts(const QcMachineManager::Machine &machine, const QList<QcItemStack> &combined) const;

    QcServerState &m_state;
    QcServerMachines &m_machines;
};
