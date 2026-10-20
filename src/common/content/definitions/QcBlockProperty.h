#pragma once

#include <QString>
#include <QStringList>

// A named block state property (facing, half, open, ...) and its possible values; the first value is
// the default.
struct QcBlockProperty
{
    static constexpr int maxProperties = 8;
    // Enough for the 25 notes of the note block (decision E-767).
    static constexpr int maxValues = 32;

    QString name;
    QStringList values;

    bool operator==(const QcBlockProperty &) const = default;
};
