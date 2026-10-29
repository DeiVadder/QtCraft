// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"

#include <QHash>
#include <QList>

// Boxes sorted into the columns of a horizontal grid (decision E-920), so a box is compared only with
// the boxes in the columns it covers instead of with every other: the boats that push each other and
// the creatures a boat picks up. Each box is kept under its index in the caller's list.
class QcBoxGrid
{
public:
    // Width of a column in metres: wider than a boat, so a boat's box covers at most 2 x 2 columns.
    static constexpr double columnSize = 2.0;

    void add(qsizetype index, const QcAabb &box);
    // The indices of the boxes in the columns `box` covers, ascending and each once: every box that
    // overlaps `box` is among them (and some that do not).
    [[nodiscard]] QList<qsizetype> near(const QcAabb &box) const;
    bool isEmpty() const { return m_columns.isEmpty(); }

private:
    QHash<qint64, QList<qsizetype>> m_columns;
};
