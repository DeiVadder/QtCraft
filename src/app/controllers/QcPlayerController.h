#pragma once

#include "app/models/QcCreativeItemModel.h"
#include "app/models/QcInventoryModel.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcSettingsController;
struct QcSessionParts;

// The local player for the HUD and the inventory screens (GameController.player): inventory and
// crafting, the open machine or chest, health and food.
class QcPlayerController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QcInventoryModel *inventory READ inventory CONSTANT FINAL)
    Q_PROPERTY(double health READ health NOTIFY healthChanged FINAL)
    Q_PROPERTY(double maxHealth READ maxHealth NOTIFY healthChanged FINAL)
    Q_PROPERTY(bool dead READ dead NOTIFY healthChanged FINAL)
    // On fire (decision E-393): the HUD shows flames at the bottom of the view.
    Q_PROPERTY(bool burning READ burning NOTIFY healthChanged FINAL)
    // Status effects (decision E-450): {name, label, icon, colour, seconds, level, levelName} each; the hearts
    // take the colour of the first effect that tints them ("" for the usual red).
    Q_PROPERTY(QVariantList effects READ effects NOTIFY healthChanged FINAL)
    Q_PROPERTY(QString heartColour READ heartColour NOTIFY healthChanged FINAL)
    Q_PROPERTY(int food READ food NOTIFY foodChanged FINAL)
    Q_PROPERTY(int maxFood READ maxFood NOTIFY foodChanged FINAL)
    // Experience (decision E-398): the level and the share of the way to the next one (0..1).
    Q_PROPERTY(int experienceLevel READ experienceLevel NOTIFY experienceChanged FINAL)
    Q_PROPERTY(double experienceProgress READ experienceProgress NOTIFY experienceChanged FINAL)
    // Armour points of what the player wears (0..20 count, decision E-234).
    Q_PROPERTY(int armour READ armour NOTIFY craftingChanged FINAL)
    Q_PROPERTY(int maxArmour READ maxArmour CONSTANT FINAL)
    Q_PROPERTY(int equipmentFirstRow READ equipmentFirstRow CONSTANT FINAL)
    // Item names of the content for the pale icons of empty armour slots (head to feet, empty when
    // the content has no armour for a slot) and the armour points in the HUD (QcArmour).
    Q_PROPERTY(QStringList armourPlaceholders READ armourPlaceholders NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString armourIcon READ armourIcon NOTIFY contentChanged FINAL)
    // The open machine (furnace screen): its name, the share of fuel left and of the item done.
    Q_PROPERTY(QString machineLabel READ machineLabel NOTIFY machineChanged FINAL)
    Q_PROPERTY(double machineBurn READ machineBurn NOTIFY machineChanged FINAL)
    Q_PROPERTY(int containerSlots READ containerSlots NOTIFY machineChanged FINAL)
    Q_PROPERTY(int containerColumns READ containerColumns NOTIFY machineChanged FINAL)
    // The hotbar slot of a backpack opened from the hand (decision E-833), -1 otherwise: the chest
    // screen then shows only the backpack's slots and the hotbar, that slot locked.
    Q_PROPERTY(int containerHeldSlot READ containerHeldSlot NOTIFY machineChanged FINAL)
    Q_PROPERTY(double machineProgress READ machineProgress NOTIFY machineChanged FINAL)
    Q_PROPERTY(int craftingSize READ craftingSize NOTIFY craftingChanged FINAL)
    Q_PROPERTY(QVariantList craftableRecipes READ craftableRecipes NOTIFY craftingChanged FINAL)
    // The creative mode (decision E-282) and its item catalogue (decision E-286).
    Q_PROPERTY(bool creative READ creative NOTIFY gameModeChanged FINAL)
    // The spectator mode (decision E-730): no hotbar, health, food or hand.
    Q_PROPERTY(bool spectator READ spectator NOTIFY gameModeChanged FINAL)
    Q_PROPERTY(QcCreativeItemModel *creativeItems READ creativeItems CONSTANT FINAL)

public:
    QcPlayerController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    QcInventoryModel *inventory() const { return m_inventory; }
    double health() const;
    double maxHealth() const;
    bool dead() const;
    bool burning() const;
    QVariantList effects() const;
    QString heartColour() const;
    int food() const;
    int maxFood() const;
    int experienceLevel() const;
    double experienceProgress() const;
    int armour() const;
    static int maxArmour();
    static int equipmentFirstRow() { return QcInventoryModel::equipmentFirstRow; }
    QStringList armourPlaceholders() const;
    QString armourIcon() const;
    QString machineLabel() const { return m_machineLabel; }
    // Slots per row of the open container's screen (a dispenser's 3 x 3, a hopper's row of 5).
    int containerColumns() const { return m_containerColumns; }
    double machineBurn() const;
    // Slots of the open chest, 0 when none is open.
    int containerSlots() const;
    int containerHeldSlot() const;
    double machineProgress() const;
    int craftingSize() const;
    QVariantList craftableRecipes() const { return m_craftableRecipes; }
    bool creative() const;
    bool spectator() const;
    QcCreativeItemModel *creativeItems() const { return m_creativeItems; }

    Q_INVOKABLE void selectHotbarSlot(int slot) const;
    // Rows of QcInventoryModel; count 0 moves the whole stack.
    Q_INVOKABLE void moveItem(int from, int to, int count = 0) const;
    // Shift-click on a row: the server moves the stack where it belongs; on the crafting result
    // it crafts as often as possible.
    Q_INVOKABLE void quickMove(int row) const;
    Q_INVOKABLE void takeCraftingResult() const;
    Q_INVOKABLE void craftRecipe(const QString &recipe) const;
    Q_INVOKABLE void respawn() const;
    // Creative mode: puts `count` items from the catalogue into the slot of a row of the inventory
    // model (onto the same item up to its stack size, else replacing it); row -1 adds them to the
    // inventory. The server refuses it in other modes.
    Q_INVOKABLE void putCreativeItem(int row, const QString &itemName, int count) const;
    // Creative mode: empties the slot of a row (the bin).
    Q_INVOKABLE void clearSlot(int row) const;

    // Item and recipe names of the session's content in the current language.
    void useContent();
    // Inventory, crafting grid, open machine and the recipes the inventory can make right now.
    void updateInventory();
    // Names the machine that just opened, from the block at its position.
    void nameOpenMachine();
    void updateMachine();

signals:
    void contentChanged();
    void healthChanged();
    void foodChanged();
    void experienceChanged();
    void machineChanged();
    void craftingChanged();
    void gameModeChanged();
    // The game mode refused a mining or use (adventure allow-lists, decision E-735), at most once
    // every QcClientSession::refusalReportTicks; the HUD shows a short notice.
    void actionRefused();

private:
    void updateCraftableRecipes();

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    QcInventoryModel *m_inventory = nullptr;
    QcCreativeItemModel *m_creativeItems = nullptr;
    QString m_machineLabel;
    int m_containerColumns = 9;
    QVariantList m_craftableRecipes;
};
