// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcShapePart.h"

#include <QList>
#include <QString>

/*! \brief Block geometry from data.

    A shape without parts is the full cube. Parts are authored for a block facing north and
    standing on the floor: \c {rotate} names a property whose value north, east, south or west
    turns the shape about the vertical axis, \c {flip} a property whose value "top" mirrors it
    upside down. \c {spin} names a property with the values 0..15 that turns the shape in steps
    of 22.5° about the vertical axis (standing signs, decision E-766): the boxes (collision,
    targeting) turn by whole quarters, the drawn faces by the rest as well
    (QcBlockState::fineTurns). */
struct QcShapeDefinition
{
    /*! Most parts of a shape. */
    static constexpr int maxParts = 32;
    /*! Most entries of each box list (boxes, crosses, collision, target) of one part. */
    static constexpr int maxBoxesPerPart = 16;
    /*! Boxes, crossed regions, collision and target boxes of all parts together (boxCount()). */
    static constexpr int maxBoxes = 64;
    /*! Most conditions in one \c {"when"} object. */
    static constexpr int maxConditions = 8;
    /*! Collision boxes may reach this high (fences, walls). */
    static constexpr double maxCollisionHeight = 1.5;

    /*! Qualified name of a shape file; empty for a block's inline shape. */
    QString name;
    /*! Parts of the shape (\c {"parts"}, or one part from \c {"boxes"}, \c {"crosses"} and
        \c {"collision"} given directly). */
    QList<QcShapePart> parts;
    /*! State property that turns the shape about the vertical axis (\c {"rotate"}); empty for
        none. */
    QString rotate;
    /*! State property whose value "top" mirrors the shape upside down (\c {"flip"}); empty for
        none. */
    QString flip;
    /*! State property with the values 0..15 that turns the shape in steps of 22.5°
        (\c {"spin"}); empty for none. */
    QString spin;
    /*! Whether the textures of its faces stay as on the world's faces whatever \c {rotate} turns
        (building parts, so boards and brick courses line up with the full blocks beside them,
        like the reference's uvlock); directional shapes (repeaters, observers, beds) turn theirs
        (E-874). */
    bool lockTextures = false;

    /*! Returns whether the shape has no parts, which makes it the full cube. */
    bool isEmpty() const { return parts.isEmpty(); }
    /*! Returns the number of boxes, crossed regions, collision and target boxes of all parts. */
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
