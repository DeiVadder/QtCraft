// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief A part of a shape applies when every condition holds: the block's value of
    \c {property} is one of \c {values}. */
struct QcShapeCondition
{
    /*! Name of the block's state property. */
    QString property;
    /*! Values of the property for which the condition holds. */
    QStringList values;

    bool operator==(const QcShapeCondition &) const = default;
};
