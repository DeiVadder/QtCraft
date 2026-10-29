// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcSpriteShape.h"

#include <QImage>

#include <array>
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

QPoint QcSpriteShape::handle() const
{
    if (isEmpty()) {
        return {};
    }

    const int bottom = m_bounds.bottom();
    QPoint grip(m_bounds.left(), bottom);
    while (!opaque(grip.x(), bottom)) {
        grip.rx() += 1;
    }

    // Up the handle: up and right (a diagonal handle), straight up (a stick), up and left.
    static constexpr std::array<QPoint, 3> steps = {{{1, -1}, {0, -1}, {-1, -1}}};
    for (int climbed = 0; climbed < 2; ++climbed) {
        for (const QPoint step : steps) {
            if (opaque(grip.x() + step.x(), grip.y() + step.y())) {
                grip += step;
                break;
            }
        }
    }
    return grip;
}
