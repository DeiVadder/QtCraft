// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/admin/QcAdminLineModel.h"

#include <QDateTime>
#include <QLocale>

using namespace Qt::StringLiterals;

QcAdminLineModel::QcAdminLineModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QcAdminLineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_lines.size());
}

QVariant QcAdminLineModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }

    const Line &line = m_lines[index.row()];
    switch (role) {
    case TimeRole:
        return QLocale().toString(QDateTime::fromMSecsSinceEpoch(line.timeMs).time(), u"HH:mm:ss"_s);
    case LevelRole:
        return line.level;
    case SourceRole:
        return line.source;
    case TextRole:
    case Qt::DisplayRole:
        return line.text;
    default:
        return {};
    }
}

QHash<int, QByteArray> QcAdminLineModel::roleNames() const
{
    return {{TimeRole, "time"}, {LevelRole, "level"}, {SourceRole, "source"}, {TextRole, "text"}};
}

void QcAdminLineModel::append(const QList<Line> &lines, qsizetype limit)
{
    if (lines.isEmpty()) {
        return;
    }
    beginInsertRows({}, static_cast<int>(m_lines.size()), static_cast<int>(m_lines.size() + lines.size() - 1));
    m_lines.append(lines);
    endInsertRows();
    if (m_lines.size() > limit) {
        beginRemoveRows({}, 0, static_cast<int>(m_lines.size() - limit - 1));
        m_lines.remove(0, m_lines.size() - limit);
        endRemoveRows();
    }
    emit countChanged();
}

void QcAdminLineModel::replace(const QList<Line> &lines)
{
    beginResetModel();
    m_lines = lines;
    endResetModel();
    emit countChanged();
}

void QcAdminLineModel::clear()
{
    replace({});
}
