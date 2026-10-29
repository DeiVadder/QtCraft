// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/boats/QcBoxGrid.h"

#include <algorithm>
#include <cmath>

namespace {

int columnOf(double coordinate)
{
    return static_cast<int>(std::floor(coordinate / QcBoxGrid::columnSize));
}

qint64 keyOf(int x, int z)
{
    return (static_cast<qint64>(x) << 32) | static_cast<quint32>(z);
}

} // namespace

void QcBoxGrid::add(qsizetype index, const QcAabb &box)
{
    for (int x = columnOf(box.min.x); x <= columnOf(box.max.x); ++x) {
        for (int z = columnOf(box.min.z); z <= columnOf(box.max.z); ++z) {
            m_columns[keyOf(x, z)].append(index);
        }
    }
}

QList<qsizetype> QcBoxGrid::near(const QcAabb &box) const
{
    QList<qsizetype> found;
    int columns = 0;
    for (int x = columnOf(box.min.x); x <= columnOf(box.max.x); ++x) {
        for (int z = columnOf(box.min.z); z <= columnOf(box.max.z); ++z) {
            const auto it = m_columns.constFind(keyOf(x, z));
            if (it != m_columns.constEnd()) {
                found.append(*it);
                ++columns;
            }
        }
    }
    if (columns <= 1) {
        return found;
    }
    // A box covering several columns is listed in each.
    std::ranges::sort(found);
    found.erase(std::ranges::unique(found).begin(), found.end());
    return found;
}
