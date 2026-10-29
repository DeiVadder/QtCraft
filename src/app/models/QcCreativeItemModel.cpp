// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcCreativeItemModel.h"

#include "app/models/QcItemIconUrls.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcCreativeItemModel::QcCreativeItemModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // New icons come with new URLs (decision E-650).
    connect(QcItemIconUrls::instance(), &QcItemIconUrls::generationChanged, this, [this] {
        if (rowCount() > 0) {
            emit dataChanged(index(0), index(rowCount() - 1), {IconRole});
        }
    });
}

int QcCreativeItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(shown().size());
}

QVariant QcCreativeItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= shown().size() || !content()) {
        return {};
    }

    const QcCreativeCatalog::Entry &entry = *shown()[index.row()];
    const QcItemDefinition &item = content()->item(entry.item);
    switch (role) {
    case ItemNameRole:
        return item.name;
    case LabelRole:
        return entry.label;
    case IconRole:
        return QcItemIconUrls::url(item.name);
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
    setContent(std::move(content));
    m_language = language;
    setCatalog(this->content() ? QcCreativeCatalog(*this->content(), language) : QcCreativeCatalog());
    setShown(catalog().filter(m_category, m_query));
    endResetModel();
    emit categoryNamesChanged();
    emit countChanged();
}

void QcCreativeItemModel::setCategory(int category)
{
    const auto wanted = static_cast<QcCreativeCatalog::Category>(std::clamp(category, 0, QcCreativeCatalog::categoryCount - 1));
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
    setShown(catalog().filter(m_category, m_query));
    endResetModel();
    emit countChanged();
}

QStringList QcCreativeItemModel::categoryNames() const
{
    // The handbook's name of a category (content text "handbook.category.<key>"), else `fallback`.
    const auto handbookName = [this](const QString &key, const QString &fallback) {
        const QString text = u"handbook.category."_s + key;
        if (!content() || !content()->translations().has(text, QString::fromLatin1(QcTranslations::fallbackLanguage))) {
            return fallback;
        }
        return content()->translations().translate(text, m_language);
    };

    // In the order of QcCreativeCatalog::Category.
    return {tr("All"),
            handbookName(u"blocks"_s, tr("Blocks")),
            handbookName(u"parts"_s, tr("Building parts")),
            tr("Signal Dust"),
            handbookName(u"tools"_s, tr("Tools and armour")),
            handbookName(u"food"_s, tr("Food")),
            tr("Other items")};
}
