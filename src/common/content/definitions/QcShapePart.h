#pragma once

#include "QcAabb.h"
#include "QcShapeCondition.h"

#include <QList>
#include <QString>

struct QcShapePart
{
    QList<QcShapeCondition> when;
    // Boxes in block-local metres (0..1); they are drawn, targeted and, unless `collision` replaces
    // them, collided with.
    QList<QcAabb> boxes;
    // Regions drawn as two crossed planes (plants); targeted by their box, never collided with.
    QList<QcAabb> crosses;
    // Collision boxes instead of `boxes` (fences reach 1.5 m, an open gate has none).
    bool customCollision = false;
    QList<QcAabb> collision;
    // Texture slot of the block used for all faces of these boxes; empty uses the face textures.
    QString texture;
    // Each face shows the whole texture stretched over it (torches) instead of the part of the
    // texture where the face lies in the cell.
    bool stretch = false;

    bool operator==(const QcShapePart &) const = default;
};
