#pragma once

#include "QcBlockPos.h"
#include "QcBlockEntities.h"
#include "QcItemStack.h"
#include "QcMachineRules.h"
#include "QcMessages.h"

#include <QList>
#include <QString>

#include <optional>

class QcBlockScreens;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// Inventories and crafting grids of the players: moving items (also into an open machine or chest),
// crafting, throwing and dropping items.
class QcServerInventory
{
public:
    static constexpr int craftingGridSlot = QcMsg::MoveInventoryItem::craftingGridSlot;
    static constexpr int machineSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int equipmentSlot = QcMsg::MoveInventoryItem::equipmentSlot;
    // Crafting needs a crafting table within this distance for 3 x 3 recipes.
    static constexpr double craftingTableReach = 5.0;
    // Speed of items thrown by a player.
    static constexpr double throwSpeed = 6.0;

    QcServerInventory(QcServerState &state, QcBlockScreens &screens, QcServerSounds &sounds);

    void send(QcServerClient &client);
    // Shift-clicks on a crafting result take it as often as possible.
    static constexpr int maxCraftAllRounds = 64;

    void moveItem(QcServerClient &client, QcMsg::MoveInventoryItem move);
    // Shift-click: moves the whole stack to where it belongs (QcQuickMove).
    void quickMove(QcServerClient &client, QcMsg::QuickMoveItem move);
    void takeCraftingResult(QcServerClient &client);
    void craftAll(QcServerClient &client);
    void craftRecipe(QcServerClient &client, const QcMsg::CraftRecipe &request);
    // Opens the 3 x 3 grid of the crafting table at `pos`.
    void openCraftingTable(QcServerClient &client, QcBlockPos pos);
    void closeCrafting(QcServerClient &client);
    // Moves the crafting grid into the inventory; what does not fit is thrown on the ground, or
    // deleted in a mode with the item catalogue.
    void returnCraftingGrid(QcServerClient &client);
    void dropItem(QcServerClient &client, QcMsg::DropItem message);
    // Drops everything the player carries (death without keepInventory).
    void dropAll(QcServerClient &client);
    // Adds the stack to the inventory; what does not fit is thrown (sending is left to the caller).
    void giveOrThrow(QcServerClient &client, QcItemStack stack);
    // Spawns the stack as an item entity thrown from the player's eyes along the view direction.
    void throwFromPlayer(const QcServerClient &client, QcItemStack stack);
    // Turns one item of the selected stack into `item` (filled bucket, firefly in a bottle); what
    // does not fit into the inventory is thrown.
    void exchangeHeld(QcServerClient &client, const QString &item);
    // Changes what the player wears (sending is left to the caller); others that see them get it
    // with the next sync.
    void setEquipment(QcServerClient &client, const QcEquipment &equipment);
    // Right click with armour: puts the held piece on, the one worn there goes into the hand.
    void equipHeld(QcServerClient &client);
    // Takes over armour worn down by a hit (QcArmour::wear) and tells the player and those who see them.
    void equipmentWorn(QcServerClient &client, const QcEquipment &equipment, bool broke);

private:
    enum class CraftOutcome { Crafted, NoRecipe, NoRoom, TooFar };

    // Inventory, crafting grid and the open machine or chest as one slot list.
    struct OpenSlots
    {
        QcBlockEntities::Machine *machine = nullptr;
        QcBlockEntities::Container *container = nullptr;
        QList<QcItemStack> combined;
    };

    OpenSlots openSlots(QcServerClient &client);
    // Writes the slot list back and sends it; false (and nothing changed) when the machine refuses.
    bool applyOpenSlots(QcServerClient &client, const OpenSlots &open);
    // Takes the crafting result once.
    CraftOutcome craftOnce(QcServerClient &client);
    // Takes the repair result (QcRepair) once.
    CraftOutcome repairOnce(QcServerClient &client);
    void takeOneOfEachCell(QcServerClient &client);
    // Whether the armour slots of the slot list hold only armour made for them.
    bool armourFits(const QList<QcItemStack> &combined) const;
    // The armour slot a shift-clicked stack goes to, or -1.
    int freeArmourSlotFor(const QcServerClient &client, QcItemStack stack) const;

    void finishCrafting(QcServerClient &client, CraftOutcome outcome, bool craftedBefore);
    // The grid of the current size as a size x size list, and the recipe it makes.
    QList<QcItemStack> craftingView(const QcServerClient &client) const;
    std::optional<qsizetype> craftingMatch(const QcServerClient &client) const;
    bool isNearCraftingTable(const QcServerClient &client) const;
    // Whether a crafting table stands in the column (dx, dy) around `feet` within reach.
    bool hasCraftingTableInColumn(QcBlockPos feet, int dx, int dy) const;
    // Whether the moved machine slots hold only fuel in the fuel slot and nothing new in the output.
    bool machineAccepts(const QcBlockEntities::Machine &machine, const QList<QcItemStack> &combined) const;

    QcServerState &m_state;
    QcBlockScreens &m_screens;
    QcServerSounds &m_sounds;
};
