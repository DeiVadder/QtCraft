#include "QcCreativeItemModel.h"

#include "QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcCreativeItemModel::QcCreativeItemModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QcCreativeItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

QVariant QcCreativeItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_shown.size() || !m_content) {
        return {};
    }

    const QcCreativeCatalog::Entry &entry = *m_shown[index.row()];
    const QcItemDefinition &item = m_content->item(entry.item);
    switch (role) {
    case ItemNameRole:
        return item.name;
    case LabelRole:
        return entry.label;
    case IconRole:
        return QString(u"image://items/"_s + item.name);
    case MaxStackRole:
        return item.maxStack;
    default:
        return {};
    }
}

QHash<int, QByteArray> QcCreativeItemModel::roleNames() const
{
    return {{ItemNameRole, "itemName"}, {LabelRole, "label"}, {IconRole, "icon"}, {MaxStackRole, "maxStack"}};
}

void QcCreativeItemModel::setContent(std::shared_ptr<const QcContentRegistry> content, const QString &language)
{
    beginResetModel();
    m_content = std::move(content);
    m_catalog = m_content ? QcCreativeCatalog(*m_content, language) : QcCreativeCatalog();
    m_shown = m_catalog.filter(m_category, m_query);
    endResetModel();
    emit categoryNamesChanged();
    emit countChanged();
}

void QcCreativeItemModel::setCategory(int category)
{
    const auto wanted = QcCreativeCatalog::Category(std::clamp(category, 0, QcCreativeCatalog::categoryCount - 1));
    if (wanted == m_category) {
        return;
    }
    m_category = wanted;
    emit categoryChanged();
    refilter();
}

void QcCreativeItemModel::setQuery(const QString &query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    refilter();
}

void QcCreativeItemModel::refilter()
{
    beginResetModel();
    m_shown = m_catalog.filter(m_category, m_query);
    endResetModel();
    emit countChanged();
}

QStringList QcCreativeItemModel::categoryNames()
{
    // In the order of QcCreativeCatalog::Category.
    return {tr("All"), tr("Blocks"), tr("Building parts"), tr("Tools and armour"), tr("Food"), tr("Other items")};
}
