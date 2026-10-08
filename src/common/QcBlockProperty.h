#pragma once

#include <QString>
#include <QStringList>

// A named block state property (facing, half, open, ...) and its possible values; the first value is
// the default.
struct QcBlockProperty
{
    static constexpr int maxProperties = 8;
    static constexpr int maxValues = 16;

    QString name;
    QStringList values;

    bool operator==(const QcBlockProperty &) const = default;
};
