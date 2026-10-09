#pragma once

#include "QcInventoryModel.h"

#include <QObject>
#include <QString>
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
    Q_PROPERTY(int food READ food NOTIFY foodChanged FINAL)
    Q_PROPERTY(int maxFood READ maxFood NOTIFY foodChanged FINAL)
    // The open machine (furnace screen): its name, the share of fuel left and of the item done.
    Q_PROPERTY(QString machineLabel READ machineLabel NOTIFY machineChanged FINAL)
    Q_PROPERTY(double machineBurn READ machineBurn NOTIFY machineChanged FINAL)
    Q_PROPERTY(int containerSlots READ containerSlots NOTIFY machineChanged FINAL)
    Q_PROPERTY(double machineProgress READ machineProgress NOTIFY machineChanged FINAL)
    Q_PROPERTY(int craftingSize READ craftingSize NOTIFY craftingChanged FINAL)
    Q_PROPERTY(QVariantList craftableRecipes READ craftableRecipes NOTIFY craftingChanged FINAL)

public:
    QcPlayerController(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent = nullptr);

    QcInventoryModel *inventory() const { return m_inventory; }
    double health() const;
    double maxHealth() const;
    bool dead() const;
    int food() const;
    int maxFood() const;
    QString machineLabel() const { return m_machineLabel; }
    double machineBurn() const;
    // Slots of the open chest, 0 when none is open.
    int containerSlots() const;
    double machineProgress() const;
    int craftingSize() const;
    QVariantList craftableRecipes() const { return m_craftableRecipes; }

    Q_INVOKABLE void selectHotbarSlot(int slot) const;
    // Rows of QcInventoryModel; count 0 moves the whole stack.
    Q_INVOKABLE void moveItem(int from, int to, int count = 0) const;
    Q_INVOKABLE void takeCraftingResult() const;
    Q_INVOKABLE void craftRecipe(const QString &recipe) const;
    Q_INVOKABLE void respawn() const;

    // Item and recipe names of the session's content in the current language.
    void useContent();
    // Inventory, crafting grid, open machine and the recipes the inventory can make right now.
    void updateInventory();
    // Names the machine that just opened, from the block at its position.
    void nameOpenMachine();
    void updateMachine();

signals:
    void healthChanged();
    void foodChanged();
    void machineChanged();
    void craftingChanged();

private:
    void updateCraftableRecipes();

    const QcSessionParts &m_parts;
    const QcSettingsController &m_settings;
    QcInventoryModel *m_inventory = nullptr;
    QString m_machineLabel;
    QVariantList m_craftableRecipes;
};
