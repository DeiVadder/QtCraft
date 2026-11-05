// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcPointsRow.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QcPointsRow::QcPointsRow(int pointsPerIcon, QObject *parent)
    : QAbstractListModel(parent)
    , m_pointsPerIcon(std::max(1, pointsPerIcon))
{
}

QList<double> QcPointsRow::fills(double points, double maxPoints, int pointsPerIcon)
{
    const double perIcon = std::max(1, pointsPerIcon);
    const int icons = static_cast<int>(std::ceil(std::max(0.0, maxPoints) / perIcon));
    QList<double> result;
    result.reserve(icons);
    for (int i = 0; i < icons; ++i) {
        result.append(std::clamp((points - i * perIcon) / perIcon, 0.0, 1.0));
    }
    return result;
}

void QcPointsRow::setShown(bool shown)
{
    if (shown == m_shown) {
        return;
    }

    m_shown = shown;
    emit shownChanged();
}

void QcPointsRow::setPoints(double points, double maxPoints)
{
    setFills(fills(points, maxPoints, m_pointsPerIcon));
}

void QcPointsRow::setFills(const QList<double> &fills)
{
    if (fills.size() != iconFills().size()) {
        beginResetModel();
        storeFills(fills);
        endResetModel();
        return;
    }

    // Only the icons whose fill changed are told.
    const QList<double> current = iconFills();
    for (qsizetype i = 0; i < fills.size(); ++i) {
        if (fills[i] != current[i]) {
            storeFill(i, fills[i]);
            emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)), {FillRole});
        }
    }
}

int QcPointsRow::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(iconFills().size());
}

QVariant QcPointsRow::data(const QModelIndex &index, int role) const
{
    if (index.parent().isValid() || index.row() < 0 || index.row() >= rowCount() || role != FillRole) {
        return {};
    }
    return iconFills().at(index.row());
}

QHash<int, QByteArray> QcPointsRow::roleNames() const
{
    return {{FillRole, "fill"_ba}};
}
