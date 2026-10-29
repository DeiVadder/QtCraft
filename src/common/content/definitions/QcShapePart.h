// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcShapeCondition.h"
#include "common/core/QcAabb.h"

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
    // Boxes pointed at (aimed, outlined), neither drawn nor collided with: a wire too thin to hit gets
    // a flat box over the whole cell (tripwire). Once a part of a state has them, only these boxes of
    // all its parts are pointed at.
    QList<QcAabb> target;
    // Texture slot of the block used for all faces of these boxes; empty uses the face textures.
    // "$<property>" takes the slot named by the state's value of that property (the lens colour of
    // the signal indicator light, decision E-765).
    QString texture;
    // Each face shows the whole texture stretched over it (torches) instead of the part of the
    // texture where the face lies in the cell.
    bool stretch = false;

    bool operator==(const QcShapePart &) const = default;
};
