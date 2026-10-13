#include "QcAtlasLayout.h"

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
constexpr int minimumMipTile = 8;

} // namespace

QcAtlasLayout::QcAtlasLayout(qsizetype tiles)
{
    m_columns = std::max(1, int(std::ceil(std::sqrt(double(std::max<qsizetype>(1, tiles))))));
    m_rows = std::max(1, int((std::max<qsizetype>(1, tiles) + m_columns - 1) / m_columns));
}

QRectF QcAtlasLayout::uv(qsizetype index) const
{
    const int column = int(index % m_columns);
    const int row = int(index / m_columns);
    const double width = m_columns * cellUnits;
    const double height = m_rows * cellUnits;
    return {(column * cellUnits + gutterUnits) / width, (row * cellUnits + gutterUnits) / height, tileUnits / width, tileUnits / height};
}

int QcAtlasLayout::levelCount(int tileSize)
{
    return std::max(1, std::bit_width(unsigned(std::max(tileSize, minimumMipTile))) - std::bit_width(unsigned(minimumMipTile)) + 1);
}

qint64 QcAtlasLayout::bytes(int tileSize) const
{
    qint64 total = 0;
    QSize size = extent(tileSize);
    for (int level = 0; level < levelCount(tileSize); ++level) {
        total += qint64(size.width()) * size.height() * 4;
        size /= 2;
    }
    return total;
}

QPoint QcAtlasLayout::tileOrigin(qsizetype index, int tileSize) const
{
    const int cell = cellSize(tileSize);
    return {int(index % m_columns) * cell + gutter(tileSize), int(index / m_columns) * cell + gutter(tileSize)};
}

void QcAtlasLayout::place(QImage &atlas, qsizetype index, const QImage &tile) const
{
    const int size = tile.width();
    const int edge = gutter(size);
    const QPoint origin = tileOrigin(index, size);

    // Rows of the tile with their left and right gutter.
    for (int y = 0; y < size; ++y) {
        const auto *source = reinterpret_cast<const quint32 *>(tile.constScanLine(y));
        auto *target = reinterpret_cast<quint32 *>(atlas.scanLine(origin.y() + y)) + origin.x();
        std::memcpy(target, source, std::size_t(size) * 4);
        std::fill(target - edge, target, source[0]);
        std::fill(target + size, target + size + edge, source[size - 1]);
    }

    // The top and bottom gutter repeat the first and last row (with their gutters).
    const std::size_t rowBytes = std::size_t(size + 2 * edge) * 4;
    const int left = origin.x() - edge;
    for (int step = 1; step <= edge; ++step) {
        std::memcpy(atlas.scanLine(origin.y() - step) + std::ptrdiff_t(left) * 4, atlas.constScanLine(origin.y()) + std::ptrdiff_t(left) * 4, rowBytes);
        std::memcpy(atlas.scanLine(origin.y() + size - 1 + step) + std::ptrdiff_t(left) * 4,
                    atlas.constScanLine(origin.y() + size - 1) + std::ptrdiff_t(left) * 4, rowBytes);
    }
}

namespace {

// One pixel of the next level from a 2 x 2 block, colours weighted by alpha. A fully transparent
// block keeps the plain mean of its colours (decision E-332: the colours given to transparent
// texels carry on into the smaller levels instead of turning black).
void average(const std::array<const uchar *, 4> &block, uchar *target)
{
    int alpha = 0;
    std::array<int, 3> colour = {0, 0, 0};
    std::array<int, 3> plain = {0, 0, 0};
    for (const uchar *pixel : block) {
        alpha += pixel[3];
        for (std::size_t c = 0; c < 3; ++c) {
            colour[c] += pixel[c] * pixel[3];
            plain[c] += pixel[c];
        }
    }
    for (std::size_t c = 0; c < 3; ++c) {
        target[c] = alpha > 0 ? uchar((colour[c] + alpha / 2) / alpha) : uchar((plain[c] + 2) / 4);
    }
    target[3] = uchar((alpha + 2) / 4);
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
            average({upper + 8 * x, upper + 8 * x + 4, lower + 8 * x, lower + 8 * x + 4}, target + 4 * x);
        }
    }
    return next;
}
