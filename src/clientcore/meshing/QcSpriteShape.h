#pragma once

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QStringList>

class QImage;

// The opaque texels of an item sprite (decision E-831): a held item is drawn as its sprite with
// sides one texel thick around these texels, and the hand grips it where they say. Texel x runs
// from the left, y from the top, as in the image.
class QcSpriteShape
{
public:
    // A texel's alpha (0..255) from which it counts as opaque: the alpha test of entity.frag
    // discards below 0.1, so every texel the GPU draws gets sides.
    static constexpr int minOpaqueAlpha = 26;

    // The side of an opaque texel next to a clear one (or the outside), seen from the texel.
    enum class Facing : quint8 {
        Left,
        Right,
        Up,
        Down,
    };
    struct Side
    {
        QPoint texel;
        Facing facing = Facing::Left;
    };

    QcSpriteShape() = default;

    // Texels of `tile` with at least minOpaqueAlpha are opaque; a tile that is not square is empty.
    [[nodiscard]] static QcSpriteShape fromImage(const QImage &tile);
    // Rows of '#' (opaque) and anything else (clear), all as long as there are rows (tests).
    [[nodiscard]] static QcSpriteShape fromRows(const QStringList &rows);

    // Texels along each edge.
    int size() const { return m_size; }
    bool isEmpty() const { return m_bounds.isEmpty(); }
    // False outside the shape.
    bool opaque(int x, int y) const;
    // The smallest rectangle around the opaque texels.
    QRect bounds() const { return m_bounds; }
    // The middle of the opaque texels (their mean centre), in texels; (0, 0) when empty.
    QPointF centroid() const { return m_centroid; }
    // Every edge between an opaque texel and a clear one, row by row: found once per shape, an
    // extruded sprite draws a side quad on each.
    const QList<Side> &sides() const { return m_sides; }

    // The texel a fist holds a tool by: from the lowest opaque texel (the leftmost of its row, the
    // end of a handle drawn from the lower left) up to two steps further along opaque texels, up
    // and to the right first. The end of the handle stays below the fist. (0, 0) when empty.
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
