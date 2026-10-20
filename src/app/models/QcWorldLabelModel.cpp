#include "app/models/QcWorldLabelModel.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

QcWorldLabelModel::QcWorldLabelModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void QcWorldLabelModel::update(const QList<QcScreenLabel> &labels, const QcContentRegistry &content, const QString &language)
{
    const bool sameRows = language == m_language && labels.size() == m_rows.size()
                          && std::ranges::equal(labels, m_rows, {}, &QcScreenLabel::key, [](const Row &row) { return row.label.key; });
    if (sameRows) {
        for (qsizetype i = 0; i < labels.size(); ++i) {
            m_rows[i].label = labels[i];
        }
        if (!m_rows.isEmpty()) {
            emit dataChanged(index(0), index(int(m_rows.size()) - 1), {XRole, YRole, ScaleRole, OpacityRole, MaxWidthRole});
        }
        return;
    }

    beginResetModel();
    m_language = language;
    m_rows.clear();
    for (const QcScreenLabel &label : labels) {
        m_rows.append({label, content.translations().translate(label.text, language)});
    }
    endResetModel();
}

void QcWorldLabelModel::clear()
{
    if (m_rows.isEmpty()) {
        return;
    }
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

int QcWorldLabelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant QcWorldLabelModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }

    const Row &row = m_rows[index.row()];
    switch (role) {
    case TitleRole:
        return row.title;
    case DetailRole:
        return row.label.detail;
    case HeadingRole:
        return row.label.heading;
    case XRole:
        return row.label.x;
    case YRole:
        return row.label.y;
    case ScaleRole:
        return row.label.scale;
    case OpacityRole:
        return row.label.opacity;
    case MaxWidthRole:
        return row.label.maxWidth;
    default:
        return {};
    }
}

QHash<int, QByteArray> QcWorldLabelModel::roleNames() const
{
    return {{TitleRole, "title"},   {DetailRole, "detail"}, {HeadingRole, "heading"},     {XRole, "labelX"},
            {YRole, "labelY"},      {ScaleRole, "labelScale"}, {OpacityRole, "labelOpacity"}, {MaxWidthRole, "labelMaxWidth"}};
}
