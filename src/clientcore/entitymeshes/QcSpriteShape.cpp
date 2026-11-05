// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcSpriteShape.h"

#include <QImage>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// Opaque texels from a predicate over a square of `size`.
template<typename Opaque>
[[nodiscard]] QList<bool> collect(int size, const Opaque &isOpaque)
{
    QList<bool> texels(static_cast<qsizetype>(size) * size, false);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            texels[static_cast<qsizetype>(y) * size + x] = isOpaque(x, y);
        }
    }
    return texels;
}

// Light of the faces by the direction they face in the sprite's space: front and back like a flat
// sprite, the top brightest, the underside darkest.
constexpr float faceShade = 0.95f;
constexpr float topShade = 1.0f;
constexpr float sideShade = 0.75f;
constexpr float bottomShade = 0.55f;

// Builds the quads of a solid form; points and picture positions are given in texel coordinates (x
// from the left, y from the top, 0..size of the shape) and z in texels from the middle; it is one
// base texel thick.
class FormBuilder
{
public:
    explicit FormBuilder(const QcSpriteShape &shape)
        : m_shape(shape)
        , m_texels(shape.size())
        , m_halfThickness(shape.texelsPerBase() * 0.5)
    {
    }

    [[nodiscard]] QList<QcSpriteShape::FormQuad> build() const
    {
        QList<QcSpriteShape::FormQuad> quads;
        quads.reserve(m_shape.sides().size() + 2);
        const double n = m_texels;
        for (const double side : {m_halfThickness, -m_halfThickness}) {
            const std::array<QcVec3, 4> points = {{{0.0, n, side}, {n, n, side}, {n, 0.0, side}, {0.0, 0.0, side}}};
            std::array<QcSpriteShape::FormCorner, 4> corners;
            for (std::size_t i = 0; i < corners.size(); ++i) {
                corners[i] = {points[i], QPointF(points[i].x, points[i].y)};
            }
            quads.append(quad(corners, QcVec3{0.0, 0.0, side}, faceShade));
        }
        for (const QcSpriteShape::Side &side : m_shape.sides()) {
            if (!hasSide(side.texel - along(side.facing), side.facing)) {
                quads.append(run(side));
            }
        }
        return quads;
    }

private:
    using Facing = QcSpriteShape::Facing;

    // The texel a side faces from `facing`'s point of view, one step off the texel.
    [[nodiscard]] static QPoint step(Facing facing)
    {
        switch (facing) {
        case Facing::Left:
            return {-1, 0};
        case Facing::Right:
            return {1, 0};
        case Facing::Up:
            return {0, -1};
        case Facing::Down:
            break;
        }
        return {0, 1};
    }

    // The way a run of sides goes on: down the picture for left and right, else to the right.
    [[nodiscard]] static QPoint along(Facing facing) { return facing == Facing::Left || facing == Facing::Right ? QPoint(0, 1) : QPoint(1, 0); }

    [[nodiscard]] bool hasSide(QPoint texel, Facing facing) const
    {
        const QPoint next = texel + step(facing);
        return m_shape.opaque(texel.x(), texel.y()) && !m_shape.opaque(next.x(), next.y());
    }

    // One quad for the straight run of sides starting at `first`: it stretches along the middle of
    // those texels in the picture, so each stretch of the side keeps the colour of its own texel.
    [[nodiscard]] QcSpriteShape::FormQuad run(QcSpriteShape::Side first) const
    {
        const Facing facing = first.facing;
        const QPoint way = along(facing);
        int length = 1;
        while (hasSide(first.texel + way * length, facing)) {
            ++length;
        }

        // The edge's first corner: the texel's top left corner, moved over to its far side.
        const QPoint out = step(facing);
        const QPointF start(first.texel.x() + (out.x() > 0 ? 1 : 0), first.texel.y() + (out.y() > 0 ? 1 : 0));
        const QPointF end = start + QPointF(way * length);
        const QPointF middle = QPointF(first.texel) + QPointF(0.5, 0.5);
        const auto corner = [&](QPointF at, double z) {
            const QPointF picture = way.x() != 0 ? QPointF(at.x(), middle.y()) : QPointF(middle.x(), at.y());
            return QcSpriteShape::FormCorner{{at.x(), at.y(), z}, picture};
        };
        return quad({corner(start, -m_halfThickness), corner(end, -m_halfThickness), corner(end, m_halfThickness), corner(start, m_halfThickness)}, QcVec3{static_cast<double>(out.x()), static_cast<double>(-out.y()), 0.0},
                    shadeOf(facing));
    }

    [[nodiscard]] static float shadeOf(Facing facing)
    {
        switch (facing) {
        case Facing::Left:
        case Facing::Right:
            return sideShade;
        case Facing::Up:
            return topShade;
        case Facing::Down:
            break;
        }
        return bottomShade;
    }

    // Texel coordinates (y down) -> the local space of a sprite one unit across (y up, centred on x).
    [[nodiscard]] QcVec3 local(const QcVec3 &t) const
    {
        const double n = m_texels;
        return {t.x / n - 0.5, 1.0 - t.y / n, t.z / n};
    }

    // A quad of corners in texel coordinates facing `outward` (in the local space, y up):
    // counter-clockwise seen from outside like every entity face.
    [[nodiscard]] QcSpriteShape::FormQuad quad(std::array<QcSpriteShape::FormCorner, 4> corners, const QcVec3 &outward, float shade) const
    {
        // Texel space has y down, so the winding is checked in the local space.
        const QcVec3 a = local(corners[1].point) - local(corners[0].point);
        const QcVec3 b = local(corners[2].point) - local(corners[0].point);
        const QcVec3 normal{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        if (normal.dot(outward) < 0.0) {
            std::swap(corners[1], corners[3]);
        }

        QcSpriteShape::FormQuad result;
        result.shade = shade;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            result.corners[i] = {local(corners[i].point), corners[i].texture / m_texels};
        }
        return result;
    }

    const QcSpriteShape &m_shape;
    int m_texels;
    double m_halfThickness;
};

} // namespace

QcSpriteShape QcSpriteShape::fromImage(const QImage &tile)
{
    QcSpriteShape shape;
    if (tile.isNull() || tile.width() != tile.height()) {
        return shape;
    }

    const QImage argb = tile.convertToFormat(QImage::Format_ARGB32);
    shape.m_size = argb.width();
    shape.m_opaque = collect(shape.m_size, [&argb](int x, int y) { return qAlpha(argb.pixel(x, y)) >= minOpaqueAlpha; });
    shape.measure();
    return shape;
}

QcSpriteShape QcSpriteShape::fromRows(const QStringList &rows)
{
    QcSpriteShape shape;
    shape.m_size = static_cast<int>(rows.size());
    shape.m_opaque = collect(shape.m_size, [&rows](int x, int y) { return x < rows[y].size() && rows[y][x] == u'#'; });
    shape.measure();
    return shape;
}

void QcSpriteShape::measure()
{
    QPointF sum;
    int count = 0;
    for (int y = 0; y < m_size; ++y) {
        for (int x = 0; x < m_size; ++x) {
            if (!opaque(x, y)) {
                continue;
            }
            const QPoint texel(x, y);
            m_bounds |= QRect(texel, QSize(1, 1));
            sum += QPointF(x + 0.5, y + 0.5);
            ++count;
            m_sides.append(sidesOf(texel));
        }
    }
    if (count > 0) {
        m_centroid = sum / count;
        m_form = FormBuilder(*this).build();
    }
}

QList<QcSpriteShape::Side> QcSpriteShape::sidesOf(QPoint texel) const
{
    // The neighbour each side faces.
    static constexpr std::array<std::pair<QPoint, Facing>, 4> neighbours = {{
        {{-1, 0}, Facing::Left},
        {{1, 0}, Facing::Right},
        {{0, -1}, Facing::Up},
        {{0, 1}, Facing::Down},
    }};

    QList<Side> sides;
    for (const auto &[step, facing] : neighbours) {
        if (!opaque(texel.x() + step.x(), texel.y() + step.y())) {
            sides.append({texel, facing});
        }
    }
    return sides;
}

bool QcSpriteShape::opaque(int x, int y) const
{
    if (x < 0 || y < 0 || x >= m_size || y >= m_size) {
        return false;
    }
    return m_opaque[static_cast<qsizetype>(y) * m_size + x];
}

QcSpriteShape::Run QcSpriteShape::leftRun(int y) const
{
    int left = m_bounds.left();
    while (left <= m_bounds.right() && !opaque(left, y)) {
        ++left;
    }
    int width = 0;
    while (left + width <= m_bounds.right() && opaque(left + width, y)) {
        ++width;
    }
    return {left, width};
}

QPoint QcSpriteShape::handleEnd() const
{
    const int bottom = m_bounds.bottom();
    const double base = texelsPerBase();
    // A pommel: the highest row, among the lowest three base texels, whose run is at least two
    // base texels wide and half a base texel wider than the handle line just above them (a
    // handle drawn in steps is as wide all along).
    const int lowest = std::max(m_bounds.top(), bottom - static_cast<int>(std::ceil(3.0 * base)) + 1);
    const double knob = std::max(2.0 * base, leftRun(lowest - 1).width + 0.5 * base);
    for (int y = lowest; y <= bottom; ++y) {
        const Run run = leftRun(y);
        if (run.width >= knob) {
            return {run.right(), y};
        }
    }
    return {leftRun(bottom).left, bottom};
}

QPoint QcSpriteShape::handle() const
{
    if (isEmpty()) {
        return {};
    }

    // The climb ends on the lowest row of the base texel two base texels above the one the end
    // lies in.
    QPoint grip = handleEnd();
    const double base = texelsPerBase();
    const int endBase = static_cast<int>(std::floor(grip.y() / base));
    const int top = static_cast<int>(std::ceil((endBase - 1) * base)) - 1;

    // Up the handle: up and right (a diagonal handle), straight up (a stick), up and left.
    static constexpr std::array<QPoint, 3> steps = {{{1, -1}, {0, -1}, {-1, -1}}};
    while (grip.y() > top) {
        const auto step = std::ranges::find_if(steps, [&](QPoint s) { return opaque(grip.x() + s.x(), grip.y() + s.y()); });
        if (step == steps.end()) {
            break;
        }
        grip += *step;
    }
    return grip;
}

QPointF QcSpriteShape::handlePoint() const
{
    if (isEmpty()) {
        return {};
    }
    // The climb ends on the lowest texel row of a base texel (it starts on the bottom row of the
    // picture and climbs whole base texels); the middle of that base texel lies half a base texel
    // above the row's lower edge. Across, painted outlines put the texel near the base texel's
    // middle already.
    const QPoint texel = handle();
    return {texel.x() + 0.5, texel.y() + 1.0 - texelsPerBase() * 0.5};
}
