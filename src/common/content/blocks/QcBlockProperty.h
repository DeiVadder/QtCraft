// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief A named block state property (facing, half, open, ...) and its possible values; the
    first value is the default. */
struct QcBlockProperty
{
    /*! Most state properties a block may have. */
    static constexpr int maxProperties = 8;
    /*! Enough for the 25 notes of the note block (decision E-767). */
    static constexpr int maxValues = 32;

    /*! Property name (the key in the block's \c {"states"} object). */
    QString name;
    /*! Possible values in their order; the first is the default. */
    QStringList values;

    bool operator==(const QcBlockProperty &) const = default;
};
