// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QStringList>

#include <algorithm>
#include <array>

class QImage;

/*! \brief The opaque texels of an item sprite (decisions E-831, E-991): a held or dropped item is
    drawn as its sprite with sides one texel thick around these texels, and the hand grips it where
    they say.

    Texel x runs from the left, y from the top, as in the image. */
class QcSpriteShape
{
public:
    /*! A texel's alpha (0..255) from which it counts as opaque: the alpha test of entity.frag
        discards below 0.1, so every texel the GPU draws gets sides. */
    static constexpr int minOpaqueAlpha = 26;
    /*! Texels along an edge of the game's own sprites: a texel of this size is how thick a solid
        sprite is and how far a fist climbs a handle, in sprites of every resolution. */
    static constexpr int baseTexels = 16;

    /*! \brief The side of an opaque texel next to a clear one (or the outside), seen from the
        texel. */
    enum class Facing : quint8 {
        Left,  // Towards smaller x
        Right, // Towards larger x
        Up,    // Towards the top of the image
        Down,  // Towards the bottom of the image
    };
    /*! \brief One side of an extruded sprite, a texel long: the opaque texel and the way its side
        faces. \qcfields */
    struct Side
    {
        QPoint texel;
        Facing facing = Facing::Left;
    };

    /*! \brief A corner of the solid form: \c {point} in the local space of a sprite one unit
        across (x across and centred, y up from 0 to 1, z through it and centred on 0), \c {texture}
        in the unit square of the picture (x from the left, y from the top). \qcfields */
    struct FormCorner
    {
        QcVec3 point;
        QPointF texture;
    };
    /*! \brief A quad of the solid form: its corners counter-clockwise seen from outside and the
        light of the way it faces. \qcfields */
    struct FormQuad
    {
        std::array<FormCorner, 4> corners;
        float shade = 1.0f;
    };

    QcSpriteShape() = default;

    /*! Returns the shape of \a tile: texels with at least minOpaqueAlpha are opaque; a tile that
        is not square is empty. */
    [[nodiscard]] static QcSpriteShape fromImage(const QImage &tile);
    /*! Returns the shape of \a rows: '#' opaque and anything else clear, all as long as there
        are rows (tests). */
    [[nodiscard]] static QcSpriteShape fromRows(const QStringList &rows);

    /*! Returns the texels along each edge. */
    int size() const { return m_size; }
    /*! Returns how many texels of this shape make one base texel (baseTexels across the
        sprite): 1 in sprites of baseTexels or fewer. */
    double texelsPerBase() const { return std::max(1.0, m_size / static_cast<double>(baseTexels)); }
    /*! Returns whether the shape has no opaque texel. */
    bool isEmpty() const { return m_bounds.isEmpty(); }
    /*! Returns whether texel (\a x, \a y) is opaque; false outside the shape. */
    bool opaque(int x, int y) const;
    /*! Returns the smallest rectangle around the opaque texels. */
    QRect bounds() const { return m_bounds; }
    /*! Returns the middle of the opaque texels (their mean centre), in texels; (0, 0) when
        empty. */
    QPointF centroid() const { return m_centroid; }
    /*! Returns every edge between an opaque texel and a clear one, row by row, found once per
        shape; form() joins each straight run of them into one quad. */
    const QList<Side> &sides() const { return m_sides; }
    /*! Returns the sprite as a solid one base texel thick (one texel in smaller sprites): its
        front and back as whole quads (clear texels fall to the alpha test), then one quad on
        every straight run of sides, stretched along the middle of the run's texels so that each
        texel's side keeps that texel's colour. Found once per shape: held and dropped items only
        place these corners (decision E-991). */
    const QList<FormQuad> &form() const { return m_form; }

    /*! Returns the texel a fist holds a tool by: from the lowest opaque texel (the leftmost of
        its row, the end of a handle drawn from the lower left) up to two base texels further along
        opaque texels, up and to the right first. The end of the handle stays below the fist. (0,
        0) when empty. */
    QPoint handle() const;
    /*! Returns the point a fist holds a tool by, in texels: across the middle of handle(), up
        the middle of the base texel whose lowest row handle() is in, so the fist sits as high on
        the handle in every resolution. (0, 0) when empty. */
    QPointF handlePoint() const;

private:
    // Fills the bounds, the centroid, the sides and the form from m_opaque.
    void measure();
    // The sides of the opaque texel `texel` that face a clear one.
    [[nodiscard]] QList<Side> sidesOf(QPoint texel) const;

    int m_size = 0;
    QList<bool> m_opaque;
    QRect m_bounds;
    QPointF m_centroid;
    QList<Side> m_sides;
    QList<FormQuad> m_form;
};
