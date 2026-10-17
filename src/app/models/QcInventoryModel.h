#pragma once

#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcMessages.h"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcContentRegistry;

// The player's slots for QML: rows 0..35 inventory (hotbar = 0..8), 36..44 crafting grid
// (three per row), 45 the crafting result, 46.. the open block: a machine (input, fuel, output) or
// the slots of a chest (up to QcMsg::MachineState::maxSlots), then the four armour slots (head,
// chest, legs, feet). `tooltip` is the name with armour points and durability.
class QcInventoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int selectedSlot READ selectedSlot NOTIFY selectedSlotChanged FINAL)

public:
    enum Role { ItemNameRole = Qt::UserRole + 1, LabelRole, CountRole, IconRole, DurabilityRole, EmptyRole, TooltipRole };

    explicit QcInventoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setContent(std::shared_ptr<const QcContentRegistry> content, QString language);
    static constexpr int gridFirstRow = QcInventory::slotCount;
    static constexpr int resultRow = gridFirstRow + 9;
    static constexpr int machineFirstRow = resultRow + 1;
    static constexpr int equipmentFirstRow = machineFirstRow + QcMsg::MachineState::maxSlots;
    static constexpr int rowCountTotal = equipmentFirstRow + int(QcEquipment::slotCount);

    // Protocol slot (QcMsg::MoveInventoryItem) of a row; -1 for the crafting result.
    static int slotOfRow(int row);

    void setInventory(const QcInventory &inventory, const QList<QcItemStack> &craftingGrid = {},
                      QcItemStack craftingResult = {}, const QList<QcItemStack> &machine = {}, const QcEquipment &equipment = {});
    int selectedSlot() const { return m_inventory.selectedHotbarSlot(); }

    // The tooltip of a stack: its name, armour points and durability left (decision E-234).
    static QString tooltipOf(const QcContentRegistry &content, QcItemStack stack, const QString &label);

signals:
    void selectedSlotChanged();

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QString m_language;
    QcInventory m_inventory;
    QList<QcItemStack> m_craftingGrid = QList<QcItemStack>(9);
    QcItemStack m_craftingResult;
    QList<QcItemStack> m_machine = QList<QcItemStack>(3);
    QcEquipment m_equipment;
};
