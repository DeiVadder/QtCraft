// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcBossBarModel.h"

#include "common/content/mobs/QcBossBarDefinition.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <array>

QcBossBarModel::QcBossBarModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void QcBossBarModel::update(const QList<QcMsg::BossBar> &bars, const QcContentRegistry *content, const QString &language)
{
    QList<Row> rows;
    rows.reserve(bars.size());
    for (const QcMsg::BossBar &bar : bars) {
        const QString key = QStringLiteral("mob.") + bar.kind.section(u':', 0, 0) + u'.' + bar.kind.section(u':', 1);
        const QString name = !bar.name.isEmpty() ? bar.name : content ? content->translations().translate(key, language) : bar.kind;
        rows.append({bar.entity, name, bar.progress, bar.colour});
    }
    if (rows == m_rows) {
        return;
    }

    // The same bars in the same order: only their data changed.
    const bool sameBars = std::ranges::equal(rows, m_rows, {}, &Row::entity, &Row::entity);
    if (!sameBars) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        return;
    }
    m_rows = rows;
    emit dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
}

int QcBossBarModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant QcBossBarModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case NameRole:
        return row.name;
    case ProgressRole:
        return row.progress;
    case ColourRole:
        return colourOf(row.colour);
    default:
        return {};
    }
}

QHash<int, QByteArray> QcBossBarModel::roleNames() const
{
    return {{NameRole, "name"}, {ProgressRole, "progress"}, {ColourRole, "barColour"}};
}

QColor QcBossBarModel::colourOf(quint8 colour)
{
    // Pink, blue, red, green, yellow, purple, white (QcBossBarColour).
    static constexpr std::array<QRgb, qcBossBarColours> colours{0xffec4bb4, 0xff1fb8e6, 0xffe6352b, 0xff40c934,
                                                                0xffe6d42b, 0xff9a3be0, 0xffe8e8e8};
    return QColor::fromRgba(colours.at(std::min<std::size_t>(colour, colours.size() - 1)));
}
