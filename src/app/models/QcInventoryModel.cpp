#include "app/models/QcInventoryModel.h"

#include "app/models/QcEnchantmentTexts.h"
#include "app/models/QcItemIconUrls.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/maps/QcMapData.h"
#include "common/protocol/QcMessages.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcInventoryModel::QcInventoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // New icons come with new URLs (decision E-650).
    connect(QcItemIconUrls::instance(), &QcItemIconUrls::generationChanged, this, [this] {
        if (rowCount() > 0) {
            emit dataChanged(index(0), index(rowCount() - 1), {IconRole});
        }
    });
}

int QcInventoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : rowCountTotal;
}

QVariant QcInventoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCountTotal) {
        return {};
    }

    const int row = index.row();
    QcItemStack stack = m_craftingResult;
    if (row < gridFirstRow) {
        stack = m_inventory.slot(row);
    } else if (row < resultRow) {
        stack = m_craftingGrid.value(row - gridFirstRow);
    } else if (row >= equipmentFirstRow) {
        stack = m_equipment.stacks[std::size_t(row - equipmentFirstRow)];
    } else if (row >= machineFirstRow) {
        stack = m_machine.value(row - machineFirstRow);
    }

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
        return m_content->translations().translate(item.translationKey, m_language);
    case CountRole:
        return stack.count;
    case IconRole:
        return QcItemIconUrls::url(item.name);
    case DurabilityRole:
        return item.durability > 0 ? 1.0 - double(stack.damage) / item.durability : -1.0;
    case TooltipRole: {
        QStringList lines = tooltipOf(*m_content, stack, m_content->translations().translate(item.translationKey, m_language), mapScaleOf(item, stack))
                                .split(u'\n');
        // Enchantments right below the name, as in the reference (decision E-864).
        const QStringList enchantments = QcEnchantmentTexts::linesOf(*m_content, stack, m_language);
        for (qsizetype i = 0; i < enchantments.size(); ++i) {
            lines.insert(1 + i, enchantments[i]);
        }
        lines.append(contentsLines(*m_content, stack, m_language));
        return lines.join(u'\n');
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> QcInventoryModel::roleNames() const
{
    return {{ItemNameRole, "itemName"}, {LabelRole, "label"},           {CountRole, "count"},
            {IconRole, "icon"},         {DurabilityRole, "durability"}, {EmptyRole, "empty"},
            {TooltipRole, "tooltip"}};
}

void QcInventoryModel::setContent(std::shared_ptr<const QcContentRegistry> content, QString language)
{
    beginResetModel();
    m_content = std::move(content);
    m_language = std::move(language);
    endResetModel();
}

int QcInventoryModel::slotOfRow(int row)
{
    if (row >= equipmentFirstRow && row < rowCountTotal) {
        return QcMsg::MoveInventoryItem::equipmentSlot + (row - equipmentFirstRow);
    }
    if (row >= machineFirstRow && row < equipmentFirstRow) {
        return QcMsg::MoveInventoryItem::machineInputSlot + (row - machineFirstRow);
    }
    return row >= 0 && row < resultRow ? row : -1;
}

void QcInventoryModel::setInventory(const QcInventory &inventory, const QList<QcItemStack> &craftingGrid,
                                    const QcItemStack &craftingResult, const QList<QcItemStack> &machine, const QcEquipment &equipment)
{
    const int previousSelection = m_inventory.selectedHotbarSlot();
    m_inventory = inventory;
    m_craftingGrid = craftingGrid.size() == 9 ? craftingGrid : QList<QcItemStack>(9);
    m_craftingResult = craftingResult;
    m_machine = machine.mid(0, QcMsg::MachineState::maxSlots);
    m_equipment = equipment;
    emit dataChanged(index(0), index(rowCountTotal - 1));
    if (previousSelection != m_inventory.selectedHotbarSlot()) {
        emit selectedSlotChanged();
    }
}

std::optional<int> QcInventoryModel::mapScaleOf(const QcItemDefinition &item, const QcItemStack &stack) const
{
    if (item.map != QcMapItem::Filled || !m_mapScale) {
        return std::nullopt;
    }
    return m_mapScale(stack.mapId());
}

QStringList QcInventoryModel::contentsLines(const QcContentRegistry &content, const QcItemStack &stack, const QString &language)
{
    QStringList lines;
    int more = 0;
    for (const QcItemStack &inside : stack.contents()) {
        if (inside.isEmpty()) {
            continue;
        }
        if (lines.size() >= maxContentLines || !content.isValidItem(inside.item)) {
            more += 1;
            continue;
        }
        const QString name = content.translations().translate(content.item(inside.item).translationKey, language);
        lines.append(inside.count > 1 ? tr("%1 × %2").arg(name).arg(inside.count) : name);
    }
    if (more > 0) {
        lines.append(tr("and %n more", nullptr, more));
    }
    return lines;
}

QString QcInventoryModel::tooltipOf(const QcContentRegistry &content, const QcItemStack &stack, const QString &label, std::optional<int> mapScale)
{
    if (stack.isEmpty() || !content.isValidItem(stack.item)) {
        return label;
    }

    const QcItemDefinition &item = content.item(stack.item);
    QStringList lines{label};
    if (item.armour) {
        lines.append(tr("Armour: +%1").arg(item.armour->points));
    }
    // A filled map carries its map id as item data (decisions E-798, E-823).
    if (item.map == QcMapItem::Filled) {
        lines.append(tr("Map #%1").arg(stack.mapId()));
        if (mapScale) {
            lines.append(tr("Scale 1:%1").arg(1 << std::clamp(*mapScale, 0, QcMapData::maxScale)));
        }
    }
    if (item.durability > 0) {
        lines.append(tr("Durability: %1 / %2").arg(item.durability - stack.damage).arg(item.durability));
    }
    return lines.join(u'\n');
}
