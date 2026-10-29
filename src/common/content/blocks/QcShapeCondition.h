// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// A part of a shape applies when every condition holds: the block's value of `property` is one of `values`.
struct QcShapeCondition
{
    QString property;
    QStringList values;

    bool operator==(const QcShapeCondition &) const = default;
};
