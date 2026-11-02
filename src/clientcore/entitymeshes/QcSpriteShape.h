// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QStringList>

class QImage;

/*! \brief The opaque texels of an item sprite (decision E-831): a held item is drawn as its sprite
    with sides one texel thick around these texels, and the hand grips it where they say.

    Texel x runs from the left, y from the top, as in the image. */
class QcSpriteShape
{
public:
    /*! A texel's alpha (0..255) from which it counts as opaque: the alpha test of entity.frag
        discards below 0.1, so every texel the GPU draws gets sides. */
    static constexpr int minOpaqueAlpha = 26;

    /*! \brief The side of an opaque texel next to a clear one (or the outside), seen from the
        texel. */
    enum class Facing : quint8 {
        Left,  // Towards smaller x
        Right, // Towards larger x
        Up,    // Towards the top of the image
        Down,  // Towards the bottom of the image
    };
    /*! \brief One side quad of an extruded sprite: the opaque texel and the way its side faces.
        \qcfields */
    struct Side
    {
        QPoint texel;
        Facing facing = Facing::Left;
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
    /*! Returns whether the shape has no opaque texel. */
    bool isEmpty() const { return m_bounds.isEmpty(); }
    /*! Returns whether texel (\a x, \a y) is opaque; false outside the shape. */
    bool opaque(int x, int y) const;
    /*! Returns the smallest rectangle around the opaque texels. */
    QRect bounds() const { return m_bounds; }
    /*! Returns the middle of the opaque texels (their mean centre), in texels; (0, 0) when
        empty. */
    QPointF centroid() const { return m_centroid; }
    /*! Returns every edge between an opaque texel and a clear one, row by row: found once per
        shape, an extruded sprite draws a side quad on each. */
    const QList<Side> &sides() const { return m_sides; }

    /*! Returns the texel a fist holds a tool by: from the lowest opaque texel (the leftmost of
        its row, the end of a handle drawn from the lower left) up to two steps further along
        opaque texels, up and to the right first. The end of the handle stays below the fist. (0,
        0) when empty. */
    QPoint handle() const;

private:
    // Fills the bounds, the centroid and the sides from m_opaque.
    void measure();
    // The sides of the opaque texel `texel` that face a clear one.
    [[nodiscard]] QList<Side> sidesOf(QPoint texel) const;

    int m_size = 0;
    QList<bool> m_opaque;
    QRect m_bounds;
    QPointF m_centroid;
    QList<Side> m_sides;
};
