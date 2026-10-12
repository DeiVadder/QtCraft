#include "QcAlphaCoverage.h"

#include <algorithm>
#include <cmath>

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

} // namespace

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
        // raised, never lowered: lowering would make the tile faint where it is blended (dropped
        // items, the hand); averaged holes stay a little denser instead.
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
