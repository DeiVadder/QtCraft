// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/textures/QcAtlasLayout.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace {

// A cell is 10 eighths of a tile, the gutter one eighth.
constexpr double cellUnits = 10.0;
constexpr double gutterUnits = 1.0;
constexpr double tileUnits = 8.0;

} // namespace

QcAtlasLayout::QcAtlasLayout(qsizetype tiles)
{
    m_columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(std::max<qsizetype>(1, tiles))))));
    m_rows = std::max(1, static_cast<int>((std::max<qsizetype>(1, tiles) + m_columns - 1) / m_columns));
}

QRectF QcAtlasLayout::uv(qsizetype index) const
{
    const int column = static_cast<int>(index % m_columns);
    const int row = static_cast<int>(index / m_columns);
    const double width = m_columns * cellUnits;
    const double height = m_rows * cellUnits;
    return {(column * cellUnits + gutterUnits) / width, (row * cellUnits + gutterUnits) / height, tileUnits / width, tileUnits / height};
}

int QcAtlasLayout::levelCount(int tileSize)
{
    return std::max(1, std::bit_width(static_cast<unsigned>(std::max(tileSize, minimumMipTile))) - std::bit_width(static_cast<unsigned>(minimumMipTile)) + 1);
}

QSize QcAtlasLayout::gridOf(QSize lastLevel)
{
    // Packs are powers of two from 16 (QcTexturePackManifest::isValidResolution), so the last level
    // is made of whole cells of 10 pixels.
    const int cell = cellSize(minimumMipTile);
    Q_ASSERT(lastLevel.width() % cell == 0 && lastLevel.height() % cell == 0);
    return {std::max(1, lastLevel.width() / cell), std::max(1, lastLevel.height() / cell)};
}

qint64 QcAtlasLayout::bytes(int tileSize) const
{
    qint64 total = 0;
    QSize size = extent(tileSize);
    for (int level = 0; level < levelCount(tileSize); ++level) {
        total += static_cast<qint64>(size.width()) * size.height() * 4;
        size /= 2;
    }
    return total;
}

QPoint QcAtlasLayout::tileOrigin(qsizetype index, int tileSize) const
{
    const int cell = cellSize(tileSize);
    return {static_cast<int>(index % m_columns) * cell + gutter(tileSize), static_cast<int>(index / m_columns) * cell + gutter(tileSize)};
}

void QcAtlasLayout::place(QImage &atlas, qsizetype index, const QImage &tile) const
{
    const int size = tile.width();
    const int edge = gutter(size);
    const QPoint origin = tileOrigin(index, size);

    // Rows of the tile with their left and right gutter.
    for (int y = 0; y < size; ++y) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a 32-bit image scan line is an array of quint32 (Qt idiom)
        const auto *source = reinterpret_cast<const quint32 *>(tile.constScanLine(y));
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): a 32-bit image scan line is an array of quint32 (Qt idiom)
        auto *target = reinterpret_cast<quint32 *>(atlas.scanLine(origin.y() + y)) + origin.x();
        std::memcpy(target, source, static_cast<std::size_t>(size) * 4);
        std::fill(target - edge, target, source[0]);
        std::fill(target + size, target + size + edge, source[size - 1]);
    }

    // The top and bottom gutter repeat the first and last row (with their gutters).
    const std::size_t rowBytes = static_cast<std::size_t>(size + 2 * edge) * 4;
    const int left = origin.x() - edge;
    for (int step = 1; step <= edge; ++step) {
        std::memcpy(atlas.scanLine(origin.y() - step) + static_cast<std::ptrdiff_t>(left) * 4,
            atlas.constScanLine(origin.y()) + static_cast<std::ptrdiff_t>(left) * 4, rowBytes);
        std::memcpy(atlas.scanLine(origin.y() + size - 1 + step) + static_cast<std::ptrdiff_t>(left) * 4,
                    atlas.constScanLine(origin.y() + size - 1) + static_cast<std::ptrdiff_t>(left) * 4, rowBytes);
    }
}

namespace {

// One pixel (RGBA) of the next level from a 2 x 2 block, colours weighted by alpha. A fully transparent
// block keeps the plain mean of its colours (decision E-332: the colours given to transparent
// texels carry on into the smaller levels instead of turning black).
[[nodiscard]] std::array<uchar, 4> average(const std::array<const uchar *, 4> &block)
{
    std::array<uchar, 4> pixel{};
    int alpha = 0;
    std::array<int, 3> colour = {0, 0, 0};
    std::array<int, 3> plain = {0, 0, 0};
    for (const uchar *texel : block) {
        alpha += texel[3];
        for (std::size_t c = 0; c < 3; ++c) {
            colour[c] += texel[c] * texel[3];
            plain[c] += texel[c];
        }
    }
    for (std::size_t c = 0; c < 3; ++c) {
        pixel[c] = alpha > 0 ? static_cast<uchar>((colour[c] + alpha / 2) / alpha) : static_cast<uchar>((plain[c] + 2) / 4);
    }
    pixel[3] = static_cast<uchar>((alpha + 2) / 4);
    return pixel;
}

} // namespace

qsizetype QcAtlasLayout::firstLevelWithin(const QList<QImage> &levels, int maxSize)
{
    for (qsizetype level = 0; level < levels.size(); ++level) {
        if (levels[level].width() <= maxSize && levels[level].height() <= maxSize) {
            return level;
        }
    }
    return -1;
}

QImage QcAtlasLayout::halve(const QImage &level)
{
    QImage next(level.width() / 2, level.height() / 2, QImage::Format_RGBA8888);
    for (int y = 0; y < next.height(); ++y) {
        const uchar *upper = level.constScanLine(2 * y);
        const uchar *lower = level.constScanLine(2 * y + 1);
        uchar *target = next.scanLine(y);
        for (std::ptrdiff_t x = 0; x < next.width(); ++x) {
            const std::array<uchar, 4> pixel = average({upper + 8 * x, upper + 8 * x + 4, lower + 8 * x, lower + 8 * x + 4});
            std::ranges::copy(pixel, target + 4 * x);
        }
    }
    return next;
}
