// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcSlotCursor.h"

#include "app/debug/QcAppLogging.h"
#include "app/models/QcInventoryModel.h"

QcSlotCursor::QcSlotCursor(const QcInventoryModel &inventory, const QcSlotActions &actions, QObject *parent)
    : QObject(parent)
    , m_inventory(inventory)
    , m_actions(actions)
{
    connect(&inventory, &QAbstractItemModel::dataChanged, this, &QcSlotCursor::dropEmptyPick);
    connect(&inventory, &QAbstractItemModel::modelReset, this, &QcSlotCursor::dropEmptyPick);
}

int QcSlotCursor::outputRowOf(QcScreenState::Overlay overlay)
{
    using Overlay = QcScreenState::Overlay;
    return overlay == Overlay::Furnace || overlay == Overlay::Stonecutter ? QcInventoryModel::machineOutputRow : -1;
}

void QcSlotCursor::click(int row, Qt::MouseButton button, bool quick)
{
    if (row < 0 || row >= QcInventoryModel::rowCountTotal) {
        qCWarning(lcGame) << "slot cursor: click on row" << row << "refused, the inventory model has rows 0 to"
                          << QcInventoryModel::rowCountTotal - 1;
        return;
    }
    // The hotbar slot of a backpack opened from the hand takes no clicks (decision E-833).
    if (row == m_actions.lockedRow()) {
        return;
    }

    const bool one = button == Qt::RightButton;
    if (!carriedItem().isEmpty()) {
        putCarried(row, one);
    } else if (row == QcInventoryModel::resultRow) {
        takeResult(row, quick);
    } else if (pickedSlot() < 0) {
        pickUp(row, quick);
    } else {
        putPicked(row, one);
    }
}

void QcSlotCursor::takeFromCatalogue(const QString &itemName, int maxStack, Qt::MouseButton button, bool quick)
{
    if (itemName.isEmpty() || maxStack < 1) {
        qCWarning(lcGame) << "slot cursor: catalogue item" << itemName << "with stack size" << maxStack << "refused";
        return;
    }
    if (pickedSlot() >= 0) {
        m_actions.clearSlot(pickedSlot());
        setPickedSlot(-1);
        return;
    }
    if (quick) {
        m_actions.putCreativeItem(-1, itemName, maxStack);
        return;
    }
    setCarried(itemName, button == Qt::RightButton ? 1 : maxStack);
}

void QcSlotCursor::bin()
{
    if (pickedSlot() >= 0) {
        m_actions.clearSlot(pickedSlot());
    }
    reset();
}

void QcSlotCursor::reset()
{
    setPickedSlot(-1);
    setCarried({}, 0);
}

void QcSlotCursor::showScreen(QcScreenState::Overlay overlay)
{
    m_outputRow = outputRowOf(overlay);
    reset();
}

void QcSlotCursor::pick(int row)
{
    if (row < 0 || row >= QcInventoryModel::rowCountTotal) {
        qCWarning(lcGame) << "slot cursor: pick of row" << row << "refused, the inventory model has rows 0 to"
                          << QcInventoryModel::rowCountTotal - 1;
        return;
    }
    setCarried({}, 0);
    setPickedSlot(row);
}

bool QcSlotCursor::isEmpty(int row) const
{
    return m_inventory.data(m_inventory.index(row), QcInventoryModel::EmptyRole).toBool();
}

void QcSlotCursor::setPickedSlot(int row)
{
    if (m_pickedSlot != row) {
        m_pickedSlot = row;
        emit changed();
    }
}

void QcSlotCursor::setCarried(const QString &itemName, int count)
{
    // Without an item the count means nothing: a hand emptied twice stays quiet.
    const int shownCount = itemName.isEmpty() ? 0 : count;
    if (m_carriedItem != itemName || m_carriedCount != shownCount) {
        m_carriedItem = itemName;
        m_carriedCount = shownCount;
        emit changed();
    }
}

void QcSlotCursor::putCarried(int row, bool one)
{
    if (row != QcInventoryModel::resultRow && row != outputRow()) {
        m_actions.putCreativeItem(row, carriedItem(), one ? 1 : carriedCount());
    }
    // Right click keeps the item carried, so several slots get one each.
    if (!one) {
        setCarried({}, 0);
    }
}

void QcSlotCursor::takeResult(int row, bool quick) const
{
    if (isEmpty(row)) {
        return;
    }
    if (quick) {
        m_actions.quickMove(row);
    } else {
        m_actions.takeCraftingResult();
    }
}

void QcSlotCursor::pickUp(int row, bool quick)
{
    if (isEmpty(row)) {
        return;
    }
    if (quick) {
        m_actions.quickMove(row);
    } else {
        setPickedSlot(row);
    }
}

void QcSlotCursor::putPicked(int row, bool one)
{
    if (row != pickedSlot() && row != outputRow()) {
        m_actions.moveItem(pickedSlot(), row, one ? 1 : 0);
    }
    // Right click keeps the stack picked up so several cells can be filled one by one.
    if (!one || row == outputRow()) {
        setPickedSlot(-1);
    }
}

void QcSlotCursor::dropEmptyPick()
{
    if (pickedSlot() >= 0 && isEmpty(pickedSlot())) {
        setPickedSlot(-1);
    }
}
