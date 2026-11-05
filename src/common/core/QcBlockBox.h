// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

/*! \brief A box of block cells from \c {low} to \c {high}, both corners inclusive (decision
    E-975). */
struct QcBlockBox
{
    /*! The corner with the lowest x, y and z. */
    QcBlockPos low;
    /*! The corner with the highest x, y and z. */
    QcBlockPos high;

    bool operator==(const QcBlockBox &) const = default;

    /*! Returns whether the cell \a pos lies in the box. */
    bool contains(QcBlockPos pos) const
    {
        return pos.x >= low.x && pos.x <= high.x && pos.y >= low.y && pos.y <= high.y && pos.z >= low.z && pos.z <= high.z;
    }
    /*! Returns whether \a other shares at least one cell with this box. */
    bool intersects(const QcBlockBox &other) const
    {
        return low.x <= other.high.x && other.low.x <= high.x && low.y <= other.high.y && other.low.y <= high.y && low.z <= other.high.z
               && other.low.z <= high.z;
    }
};

Q_DECLARE_TYPEINFO(QcBlockBox, Q_PRIMITIVE_TYPE);
