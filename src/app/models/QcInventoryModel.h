// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcMessages.h"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>
#include <optional>

class QcContentRegistry;
struct QcItemDefinition;

// The player's slots for QML: rows 0..35 inventory (hotbar = 0..8), 36..44 crafting grid
// (three per row), 45 the crafting result, 46.. the open block: a machine (input, fuel, output) or
// the slots of a chest (up to QcMsg::MachineState::maxSlots), then the four armour slots (head,
// chest, legs, feet). `tooltip` is the name with armour points and durability.
class QcInventoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int selectedSlot READ selectedSlot NOTIFY selectedSlotChanged FINAL)
    // The name of the stack in the selected hotbar slot (its custom name if it has one), empty for
    // an empty slot; the HUD shows it briefly over the hotbar (decision E-885).
    Q_PROPERTY(QString selectedLabel READ selectedLabel NOTIFY selectedLabelChanged FINAL)

public:
    enum Role { ItemNameRole = Qt::UserRole + 1, LabelRole, CountRole, IconRole, DurabilityRole, EmptyRole, TooltipRole, CustomNameRole };

    explicit QcInventoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setContent(std::shared_ptr<const QcContentRegistry> content, QString language);
    static constexpr int gridFirstRow = QcInventory::slotCount;
    static constexpr int resultRow = gridFirstRow + 9;
    static constexpr int machineFirstRow = resultRow + 1;
    static constexpr int equipmentFirstRow = machineFirstRow + QcMsg::MachineState::maxSlots;
    static constexpr int rowCountTotal = equipmentFirstRow + static_cast<int>(QcEquipment::slotCount);

    // Protocol slot (QcMsg::MoveInventoryItem) of a row; -1 for the crafting result.
    static int slotOfRow(int row);

    void setInventory(const QcInventory &inventory, const QList<QcItemStack> &craftingGrid = {},
                      const QcItemStack &craftingResult = {}, const QList<QcItemStack> &machine = {}, const QcEquipment &equipment = {});
    int selectedSlot() const { return m_inventory.selectedHotbarSlot(); }
    QString selectedLabel() const;

    // The tooltip of a stack: its name, armour points and durability left (decision E-234); for a
    // filled map its number and, with `mapScale` (when the client has the map), its scale.
    static QString tooltipOf(const QcContentRegistry &content, const QcItemStack &stack, const QString &label,
                             std::optional<int> mapScale = std::nullopt);
    // The tooltip lines of what a stack holds (a backpack, decision E-832): the first
    // maxContentLines stacks by name and count, then how many more; empty without contents.
    static constexpr int maxContentLines = 5;
    static QStringList contentsLines(const QcContentRegistry &content, const QcItemStack &stack, const QString &language);
    // Finds the scale of a map the client has (decision E-802), for the tooltips of filled maps.
    void setMapScales(std::function<std::optional<int>(quint32)> mapScale) { m_mapScale = std::move(mapScale); }

signals:
    void selectedSlotChanged();
    // Another hotbar slot was selected, or another item (or name) came into the selected slot; a
    // change of the count alone does not count (decision E-885).
    // Not for the first inventory of a world (joining shows no name).
    void selectedItemAnnounced();
    // The label may read differently: another item, or another language or content.
    void selectedLabelChanged();

private:
    // Whether two stacks show the same name over the hotbar: both empty, or the same item with the
    // same custom name.
    static bool sameItem(const QcItemStack &a, const QcItemStack &b);
    // The scale for the tooltip of a filled map the client has.
    std::optional<int> mapScaleOf(const QcItemDefinition &item, const QcItemStack &stack) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QString m_language;
    // An inventory of the current content arrived: later changes of the selected item are announced.
    bool m_inventoryShown = false;
    QcInventory m_inventory;
    QList<QcItemStack> m_craftingGrid = QList<QcItemStack>(9);
    QcItemStack m_craftingResult;
    QList<QcItemStack> m_machine = QList<QcItemStack>(3);
    QcEquipment m_equipment;
    std::function<std::optional<int>(quint32)> m_mapScale;
};
