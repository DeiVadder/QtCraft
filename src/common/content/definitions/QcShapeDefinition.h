#pragma once

#include "common/content/definitions/QcShapePart.h"

#include <QList>
#include <QString>

// Block geometry from data. A shape without parts is the full cube. Parts are authored for a block
// facing north and standing on the floor: `rotate` names a property whose value north, east, south
// or west turns the shape about the vertical axis, `flip` a property whose value "top" mirrors it
// upside down. `spin` names a property with the values 0..15 that turns the shape in steps of 22.5°
// about the vertical axis (standing signs, decision E-766): the boxes (collision, targeting) turn by
// whole quarters, the drawn faces by the rest as well (QcBlockState::fineTurns).
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
    QString spin;

    bool isEmpty() const { return parts.isEmpty(); }
    int boxCount() const
    {
        qsizetype count = 0;
        for (const QcShapePart &part : parts) {
            count += part.boxes.size() + part.crosses.size() + part.collision.size() + part.target.size();
        }
        return static_cast<int>(count);
    }
    bool operator==(const QcShapeDefinition &) const = default;
};
