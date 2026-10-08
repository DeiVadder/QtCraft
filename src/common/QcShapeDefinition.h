#pragma once

#include "QcShapePart.h"

#include <QList>
#include <QString>

// Block geometry from data. A shape without parts is the full cube. Parts are authored for a block
// facing north and standing on the floor: `rotate` names a property whose value north, east, south
// or west turns the shape about the vertical axis, `flip` a property whose value "top" mirrors it
// upside down.
struct QcShapeDefinition
{
    static constexpr int maxParts = 32;
    static constexpr int maxBoxesPerPart = 16;
    // Boxes, crossed regions and collision boxes of all parts together.
    static constexpr int maxBoxes = 64;
    static constexpr int maxConditions = 8;
    // Collision boxes may reach this high (fences, walls).
    static constexpr double maxCollisionHeight = 1.5;

    QString name;
    QList<QcShapePart> parts;
    QString rotate;
    QString flip;

    bool isEmpty() const { return parts.isEmpty(); }
    int boxCount() const
    {
        qsizetype count = 0;
        for (const QcShapePart &part : parts) {
            count += part.boxes.size() + part.crosses.size() + part.collision.size();
        }
        return int(count);
    }
    bool operator==(const QcShapeDefinition &) const = default;
};
