#include "clientcore/textures/QcAlphaCoverage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace QcAlphaCoverage {

bool hasTexelBelowCutoff(const QImage &tile)
{
    const QImage image = tile.format() == QImage::Format_RGBA8888 ? tile : tile.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            if (line[std::ptrdiff_t(x) * 4 + 3] < cutoff) {
                return true;
            }
        }
    }
    return false;
}

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

// Spreads the colours of the drawn texels of a tile over its transparent ones, ring by ring; knows
// which texels have a colour already (one flag per texel, row by row).
class ColourSpread
{
public:
    // `tile` must not be shared.
    explicit ColourSpread(QImage &tile)
        : m_tile(tile)
        , m_filled(std::size_t(tile.width()) * std::size_t(tile.height()), false)
    {
    }

    // The texels with alpha above 0 in reading order, marked as filled.
    std::vector<QPoint> drawnTexels()
    {
        std::vector<QPoint> drawn;
        for (int y = 0; y < m_tile.height(); ++y) {
            const uchar *line = m_tile.constScanLine(y);
            for (int x = 0; x < m_tile.width(); ++x) {
                if (line[std::ptrdiff_t(x) * 4 + 3] != 0) {
                    drawn.emplace_back(x, y);
                    m_filled[std::size_t(y) * std::size_t(m_tile.width()) + std::size_t(x)] = true;
                }
            }
        }
        return drawn;
    }

    // Gives the unfilled 4-neighbours of `front` the colour of the texel they were reached from
    // (alpha unchanged) and returns them in that order, the next ring.
    std::vector<QPoint> spreadRing(const std::vector<QPoint> &front)
    {
        constexpr std::array<std::array<int, 2>, 4> steps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        const int width = m_tile.width();
        const int height = m_tile.height();
        std::vector<QPoint> next;
        for (const QPoint &point : front) {
            const uchar *source = m_tile.constScanLine(point.y()) + std::ptrdiff_t(point.x()) * 4;
            for (const std::array<int, 2> &step : steps) {
                const int x = point.x() + step[0];
                const int y = point.y() + step[1];
                if (x < 0 || y < 0 || x >= width || y >= height) {
                    continue;
                }
                const std::size_t index = std::size_t(y) * std::size_t(width) + std::size_t(x);
                if (m_filled[index]) {
                    continue;
                }
                m_filled[index] = true;
                std::copy_n(source, 3, m_tile.scanLine(y) + std::ptrdiff_t(x) * 4);
                next.emplace_back(x, y);
            }
        }
        return next;
    }

private:
    QImage &m_tile;
    std::vector<bool> m_filled;
};

} // namespace

void fillTransparentColour(QImage &tile)
{
    Q_ASSERT(tile.format() == QImage::Format_RGBA8888);
    if (!hasTransparentTexel(tile)) {
        return;
    }
    // Detach once before reading: the colours are copied within this image's own pixels.
    tile.bits();
    ColourSpread spread(tile);
    std::vector<QPoint> front = spread.drawnTexels();
    // Breadth first from every drawn texel: each transparent texel takes the colour of the
    // neighbour that reached it first, so colours spread outwards in rings.
    while (!front.empty()) {
        front = spread.spreadRing(front);
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
