// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcTextureOutline.h"

#include <QList>
#include <QPoint>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <utility>

namespace QcTextureOutline {

namespace {

// Whether pixel (x, y) of the base is visible. The cut-out shapes are icons that never repeat,
// so the pixels beyond the picture's border are clear: a handle in one corner and a head in the
// opposite one do not join across the border.
bool visible(const QImage &base, int x, int y)
{
    return base.valid(x, y) && base.pixelColor(x, y).alpha() > 0;
}

bool hasClearPixel(const QImage &base)
{
    for (int y = 0; y < base.height(); ++y) {
        for (int x = 0; x < base.width(); ++x) {
            if (!visible(base, x, y)) {
                return true;
            }
        }
    }
    return false;
}

// The enlarged picture as the discs and strokes of the 16 x 16 one cover it. Positions are counted
// in half pixels of the enlarged picture, so the centres of both pictures' pixels are integers:
// pixel x of the enlarged picture has its centre at 2x + 1, pixel k of the base at (2k + 1) * scale,
// and the strokes reach `scale` (half a base pixel) to each side.
class Canvas
{
public:
    Canvas(const QImage &base, int resolution)
        : m_base(base)
        , m_resolution(resolution)
        , m_scale(resolution / base.width())
        , m_source(static_cast<qsizetype>(resolution) * resolution, -1)
        , m_distance(static_cast<qsizetype>(resolution) * resolution, std::numeric_limits<qint64>::max())
    {
    }

    // Covers what lies within half a base pixel of the straight line between the centres of the
    // base pixels `from` and `to` (the same pixel for a disc), both visible pixels of the picture.
    // That lies within the cells of the two pixels' bounding box: a pixel of the enlarged picture
    // outside it is more than half a base pixel away from both centres in one direction.
    void addStroke(QPoint from, QPoint to)
    {
        const QPoint a = centre(from);
        const QPoint b = centre(to);
        const int left = std::min(from.x(), to.x()) * scale();
        const int top = std::min(from.y(), to.y()) * scale();
        const int right = (std::max(from.x(), to.x()) + 1) * scale();
        const int bottom = (std::max(from.y(), to.y()) + 1) * scale();
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const QPoint p(2 * x + 1, 2 * y + 1);
                if (within(p, a, b)) {
                    claim(x, y, nearer(p, {from, to}));
                }
            }
        }
    }

    // Returns `enlarged` (as RGBA8888) cut to the covered pixels.
    [[nodiscard]] QImage cut(const QImage &enlarged) const
    {
        QImage result = enlarged.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < resolution(); ++y) {
            uchar *row = result.scanLine(y);
            for (int x = 0; x < resolution(); ++x) {
                uchar *pixel = row + static_cast<std::ptrdiff_t>(x) * 4;
                const int source = m_source.at(static_cast<qsizetype>(y) * resolution() + x);
                if (source < 0) {
                    std::fill(pixel, pixel + 4, uchar{0});
                } else if (pixel[3] == 0) {
                    const QColor colour = m_base.pixelColor(source % m_base.width(), source / m_base.width());
                    pixel[0] = static_cast<uchar>(colour.red());
                    pixel[1] = static_cast<uchar>(colour.green());
                    pixel[2] = static_cast<uchar>(colour.blue());
                    pixel[3] = static_cast<uchar>(colour.alpha());
                }
            }
        }
        return result;
    }

private:
    // Edge of the enlarged picture in pixels.
    int resolution() const
    {
        return m_resolution;
    }

    // Pixels of the enlarged picture per pixel of the base.
    int scale() const
    {
        return m_scale;
    }

    QPoint centre(QPoint pixel) const
    {
        return {(2 * pixel.x() + 1) * scale(), (2 * pixel.y() + 1) * scale()};
    }

    static qint64 squared(QPoint vector)
    {
        return static_cast<qint64>(vector.x()) * vector.x() + static_cast<qint64>(vector.y()) * vector.y();
    }

    static qint64 dot(QPoint first, QPoint second)
    {
        return static_cast<qint64>(first.x()) * second.x() + static_cast<qint64>(first.y()) * second.y();
    }

    // Whether `p` lies within `scale` of the segment from `a` to `b`, exactly in integers.
    bool within(QPoint p, QPoint a, QPoint b) const
    {
        const qint64 radius = static_cast<qint64>(scale()) * scale();
        const QPoint d = b - a;
        const QPoint w = p - a;
        const qint64 along = dot(w, d);
        const qint64 length = squared(d);
        if (along <= 0) {
            return squared(w) <= radius;
        }
        if (along >= length) {
            return squared(p - b) <= radius;
        }
        return squared(w) * length - along * along <= radius * length;
    }

    // The end of the stroke nearer to `p`, as the index of its pixel in the base, with the
    // squared distance to it.
    std::pair<int, qint64> nearer(QPoint p, std::array<QPoint, 2> ends) const
    {
        std::pair<int, qint64> best{-1, std::numeric_limits<qint64>::max()};
        for (const QPoint end : ends) {
            const qint64 distance = squared(p - centre(end));
            if (distance < best.second) {
                best = {end.y() * m_base.width() + end.x(), distance};
            }
        }
        return best;
    }

    // Marks the pixel (x, y) as covered, coloured by the nearest base pixel of all strokes
    // covering it.
    void claim(int x, int y, std::pair<int, qint64> source)
    {
        const qsizetype index = static_cast<qsizetype>(y) * resolution() + x;
        if (source.second < m_distance.at(index)) {
            m_source[index] = source.first;
            m_distance[index] = source.second;
        }
    }

    const QImage &m_base;
    const int m_resolution;
    const int m_scale;
    QList<int> m_source;
    QList<qint64> m_distance;
};

// The corners of a clear base pixel whose two neighbours meeting there are visible: a diagonal
// stroke joins the two when the pixel across the corner is clear too (a step of a diagonal line,
// also where it meets another part, like a handle under a pickaxe's head) or when the two opposite
// neighbours are clear (a concave corner, which Scale2x fills as well).
void addDiagonals(Canvas &canvas, const QImage &base, QPoint pixel)
{
    static constexpr std::array<QPoint, 4> corners = {QPoint{-1, -1}, QPoint{1, -1}, QPoint{-1, 1}, QPoint{1, 1}};
    for (const QPoint corner : corners) {
        const QPoint side(pixel.x() + corner.x(), pixel.y());
        const QPoint other(pixel.x(), pixel.y() + corner.y());
        const bool sides = visible(base, side.x(), side.y()) && visible(base, other.x(), other.y());
        const bool opposite = visible(base, pixel.x() - corner.x(), pixel.y()) || visible(base, pixel.x(), pixel.y() - corner.y());
        const bool across = visible(base, pixel.x() + corner.x(), pixel.y() + corner.y());
        if (sides && (!across || !opposite)) {
            canvas.addStroke(side, other);
        }
    }
}

// A pixel at the end of a band two pixels thick, joined to the band only by its diagonal neighbour
// (the tip of a blade): a stroke to the band's other row, a knight's move away, tapers the band
// to a point instead of leaving a step. Ends where both rows (the guard of a sword) or neither (a
// line one pixel thick) continue stay round.
void addTaper(Canvas &canvas, const QImage &base, QPoint pixel)
{
    QPoint diagonal;
    int neighbours = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx != 0 || dy != 0) && visible(base, pixel.x() + dx, pixel.y() + dy)) {
                ++neighbours;
                diagonal = QPoint(dx, dy);
            }
        }
    }
    if (neighbours != 1 || diagonal.x() == 0 || diagonal.y() == 0) {
        return;
    }

    const QPoint along = pixel + diagonal + QPoint(0, diagonal.y());
    const QPoint across = pixel + diagonal + QPoint(diagonal.x(), 0);
    const bool alongVisible = visible(base, along.x(), along.y());
    if (alongVisible != visible(base, across.x(), across.y())) {
        canvas.addStroke(pixel, alongVisible ? along : across);
    }
}

} // namespace

QImage cut(const QImage &enlarged, const QImage &base)
{
    const bool square = base.width() == base.height() && enlarged.width() == enlarged.height();
    if (base.isNull() || !square || enlarged.width() % base.width() != 0 || !hasClearPixel(base)) {
        return enlarged;
    }

    Canvas canvas(base, enlarged.width());
    for (int y = 0; y < base.height(); ++y) {
        for (int x = 0; x < base.width(); ++x) {
            const QPoint pixel(x, y);
            if (!visible(base, x, y)) {
                addDiagonals(canvas, base, pixel);
                continue;
            }
            canvas.addStroke(pixel, pixel);
            addTaper(canvas, base, pixel);
            if (visible(base, x + 1, y)) {
                canvas.addStroke(pixel, {x + 1, y});
            }
            if (visible(base, x, y + 1)) {
                canvas.addStroke(pixel, {x, y + 1});
            }
        }
    }
    return canvas.cut(enlarged);
}

} // namespace QcTextureOutline
