#include "QcInventoryModel.h"

#include "QcContentRegistry.h"

using namespace Qt::StringLiterals;

QcInventoryModel::QcInventoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QcInventoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : QcInventory::slotCount;
}

QVariant QcInventoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= QcInventory::slotCount) {
        return {};
    }
    const QcItemStack &stack = m_inventory.slot(index.row());
    const bool empty = stack.isEmpty() || !m_content || !m_content->isValidItem(stack.item);
    if (role == EmptyRole) {
        return empty;
    }
    if (empty) {
        return role == CountRole ? QVariant(0) : QVariant(QString());
    }
    const QcItemDefinition &item = m_content->item(stack.item);
    switch (role) {
    case ItemNameRole:
        return item.name;
    case LabelRole:
        return m_content->translate(item.translationKey, m_language);
    case CountRole:
        return stack.count;
    case IconRole:
        return QString(u"image://items/"_s + item.name);
    case DurabilityRole:
        return item.durability > 0 ? 1.0 - double(stack.damage) / item.durability : -1.0;
    default:
        return {};
    }
}

QHash<int, QByteArray> QcInventoryModel::roleNames() const
{
    return {{ItemNameRole, "itemName"}, {LabelRole, "label"},           {CountRole, "count"},
            {IconRole, "icon"},         {DurabilityRole, "durability"}, {EmptyRole, "empty"}};
}

void QcInventoryModel::setContent(std::shared_ptr<const QcContentRegistry> content, QString language)
{
    beginResetModel();
    m_content = std::move(content);
    m_language = std::move(language);
    endResetModel();
}

void QcInventoryModel::setInventory(const QcInventory &inventory)
{
    const int previousSelection = m_inventory.selectedHotbarSlot();
    m_inventory = inventory;
    emit dataChanged(index(0), index(QcInventory::slotCount - 1));
    if (previousSelection != m_inventory.selectedHotbarSlot()) {
        emit selectedSlotChanged();
    }
}
