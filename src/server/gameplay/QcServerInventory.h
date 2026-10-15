#pragma once

#include "QcBlockPos.h"
#include "QcBlockEntities.h"
#include "QcBlockScreens.h"
#include "QcItemStack.h"
#include "QcMachineRules.h"
#include "QcMessages.h"

#include <QList>
#include <QString>

#include <optional>

class QcBlockScreens;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// Inventories of the players: moving items between the inventory, the crafting grid, armour slots and
// an open machine or chest, throwing and dropping items, armour worn and taken off. Crafting itself is
// QcServerCrafting's.
class QcServerInventory
{
public:
    static constexpr int craftingGridSlot = QcMsg::MoveInventoryItem::craftingGridSlot;
    static constexpr int machineSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int equipmentSlot = QcMsg::MoveInventoryItem::equipmentSlot;
    // Speed of items thrown by a player.
    static constexpr double throwSpeed = 6.0;

    QcServerInventory(QcServerState &state, QcBlockScreens &screens, QcServerSounds &sounds);

    void send(QcServerClient &client);

    void moveItem(QcServerClient &client, QcMsg::MoveInventoryItem move);
    // Shift-click: moves the whole stack to where it belongs (QcQuickMove).
    void quickMove(QcServerClient &client, QcMsg::QuickMoveItem move);
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
    // One use of the held tool (a hoe tilling, flint and steel lighting fire), except in creative
    // mode; a tool used up breaks. Tells the player.
    void wearHeld(QcServerClient &client);

private:
    // Inventory, crafting grid and the open machine or chest as one slot list.
    struct OpenSlots
    {
        QcBlockEntities::Machine *machine = nullptr;
        std::optional<QcBlockScreens::OpenContainer> container;
        QList<QcItemStack> combined;
    };

    OpenSlots openSlots(QcServerClient &client);
    // Writes the slot list back and sends it; when the armour slots or the machine refuse, nothing
    // changes and the client gets its unchanged slots again.
    void applyOpenSlots(QcServerClient &client, const OpenSlots &open);
    // Whether the armour slots of the slot list hold only armour made for them.
    bool armourFits(const QList<QcItemStack> &combined) const;
    // The armour slot a shift-clicked stack goes to, or -1.
    int freeArmourSlotFor(const QcServerClient &client, QcItemStack stack) const;

    // Whether the moved machine slots hold only fuel in the fuel slot and nothing new in the output.
    bool machineAccepts(const QcBlockEntities::Machine &machine, const QList<QcItemStack> &combined) const;

    QcServerState &m_state;
    QcBlockScreens &m_screens;
    QcServerSounds &m_sounds;
};
