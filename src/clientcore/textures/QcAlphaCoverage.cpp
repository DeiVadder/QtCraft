#include "QcAlphaCoverage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace QcAlphaCoverage {

namespace {

// Alpha of texel (x, y) of the tile whose top left corner is `origin`.
int alphaAt(const QImage &level, QPoint origin, int x, int y)
{
    return level.constScanLine(origin.y() + y)[std::ptrdiff_t(origin.x() + x) * 4 + 3];
}

// Share of the tile's texels drawn when alpha is multiplied by `scale`.
double coverageAt(const QImage &level, QPoint origin, int tileSize, double scale)
{
    int drawn = 0;
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            drawn += alphaAt(level, origin, x, y) * scale >= cutoff;
        }
    }
    return double(drawn) / (tileSize * tileSize);
}

// The tile is cut out: only fully transparent and fully opaque texels, at least one of each kind.
double cutOutCoverage(const QImage &level, QPoint origin, int tileSize)
{
    int opaque = 0;
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            const int alpha = alphaAt(level, origin, x, y);
            if (alpha != 0 && alpha != 255) {
                return -1.0;
            }
            opaque += alpha == 255;
        }
    }
    return opaque == 0 || opaque == tileSize * tileSize ? -1.0 : double(opaque) / (tileSize * tileSize);
}

// Multiplies the alpha of the whole cell around the tile by `scale`.
void scaleCell(QImage &level, QPoint origin, int tileSize, double scale)
{
    const int gutter = tileSize / 8;
    for (int y = -gutter; y < tileSize + gutter; ++y) {
        uchar *line = level.scanLine(origin.y() + y);
        for (int x = -gutter; x < tileSize + gutter; ++x) {
            uchar &alpha = line[std::ptrdiff_t(origin.x() + x) * 4 + 3];
            alpha = uchar(std::clamp(int(std::lround(alpha * scale)), 0, 255));
        }
    }
}

// True when `tile` has a texel with alpha 0.
bool hasTransparentTexel(const QImage &tile)
{
    for (int y = 0; y < tile.height(); ++y) {
        const uchar *line = tile.constScanLine(y);
        for (int x = 0; x < tile.width(); ++x) {
            if (line[std::ptrdiff_t(x) * 4 + 3] == 0) {
                return true;
            }
        }
    }
    return false;
}

// The texels with alpha above 0 of `tile` in reading order, also marked in `filled` (one flag per
// texel, row by row).
std::vector<QPoint> drawnTexels(const QImage &tile, std::vector<bool> &filled)
{
    std::vector<QPoint> drawn;
    for (int y = 0; y < tile.height(); ++y) {
        const uchar *line = tile.constScanLine(y);
        for (int x = 0; x < tile.width(); ++x) {
            if (line[std::ptrdiff_t(x) * 4 + 3] != 0) {
                drawn.emplace_back(x, y);
                filled[std::size_t(y) * std::size_t(tile.width()) + std::size_t(x)] = true;
            }
        }
    }
    return drawn;
}

// Gives the unfilled 4-neighbours of `front` the colour of the texel they were reached from (alpha
// unchanged) and returns them in that order, the next ring. `tile` must not be shared.
std::vector<QPoint> spreadRing(QImage &tile, const std::vector<QPoint> &front, std::vector<bool> &filled)
{
    constexpr std::array<std::array<int, 2>, 4> steps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    const int width = tile.width();
    const int height = tile.height();
    std::vector<QPoint> next;
    for (const QPoint &point : front) {
        const uchar *source = tile.constScanLine(point.y()) + std::ptrdiff_t(point.x()) * 4;
        for (const std::array<int, 2> &step : steps) {
            const int x = point.x() + step[0];
            const int y = point.y() + step[1];
            if (x < 0 || y < 0 || x >= width || y >= height) {
                continue;
            }
            const std::size_t index = std::size_t(y) * std::size_t(width) + std::size_t(x);
            if (filled[index]) {
                continue;
            }
            filled[index] = true;
            std::copy_n(source, 3, tile.scanLine(y) + std::ptrdiff_t(x) * 4);
            next.emplace_back(x, y);
        }
    }
    return next;
}

} // namespace

void fillTransparentColour(QImage &tile)
{
    Q_ASSERT(tile.format() == QImage::Format_RGBA8888);
    if (!hasTransparentTexel(tile)) {
        return;
    }
    // Detach once before reading: the colours are copied within this image's own pixels.
    tile.bits();
    std::vector<bool> filled(std::size_t(tile.width()) * std::size_t(tile.height()), false);
    std::vector<QPoint> front = drawnTexels(tile, filled);
    // Breadth first from every drawn texel: each transparent texel takes the colour of the
    // neighbour that reached it first, so colours spread outwards in rings.
    while (!front.empty()) {
        front = spreadRing(tile, front, filled);
    }
}

QList<double> measure(const QImage &level, QcAtlasLayout layout, qsizetype tiles, int tileSize)
{
    QList<double> coverage;
    for (qsizetype index = 0; index < tiles; ++index) {
        coverage.append(cutOutCoverage(level, layout.tileOrigin(index, tileSize), tileSize));
    }
    return coverage;
}

void keep(QImage &level, QcAtlasLayout layout, const QList<double> &coverage, int tileSize)
{
    constexpr int searchSteps = 12;
    constexpr double maxScale = 16.0;
    for (qsizetype index = 0; index < coverage.size(); ++index) {
        if (coverage[index] < 0.0) {
            continue;
        }

        // Coverage grows with the scale: bisect for the scale that matches level 0. Alpha is only
        // raised, never lowered: lowering it where averaging filled holes would make thin shapes
        // (blades, stems, torches) vanish first in the distance; a little denser foliage far away
        // is the lesser evil (decision E-332 corrects the earlier reason, entities are not blended).
        const QPoint origin = layout.tileOrigin(index, tileSize);
        if (coverageAt(level, origin, tileSize, 1.0) >= coverage[index]) {
            continue;
        }
        double low = 1.0;
        double high = maxScale;
        for (int step = 0; step < searchSteps; ++step) {
            const double middle = (low + high) / 2.0;
            (coverageAt(level, origin, tileSize, middle) < coverage[index] ? low : high) = middle;
        }
        scaleCell(level, origin, tileSize, high);
    }
}

} // namespace QcAlphaCoverage
