#include "clientcore/textures/QcTextureBuildingPatterns.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

// Mortar of the brick pattern: rows of four pixels, every other row shifted by half a brick.
bool isMortar(int x, int y)
{
    const int offset = (y / 4) % 2 == 0 ? 0 : 4;
    return y % 4 == 3 || (x + offset) % 8 == 7;
}

QColor paintMossyBricks(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Moss (secondary) grows in soft patches, most of all in the mortar.
    const double patch = noise(spec.seed + 11, x / 3, y / 3) + 0.4 * noise(spec.seed + 12, x, y) + (isMortar(x, y) ? 0.5 : 0.0);
    if (patch > 0.55) {
        return varied(spec, spec.secondary, x, y, 7);
    }
    return isMortar(x, y) ? scaled(spec.primary, 0.66) : color;
}

QColor paintCrackedBricks(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (isMortar(x, y)) {
        return varied(spec, spec.secondary, x, y, 5);
    }

    // Two zigzag cracks through the bricks, in the mortar colour but darker.
    const bool first = y >= 1 && y <= 10 && x == 4 + (y / 2) % 2 + y / 5;
    const bool second = x >= 8 && x <= 14 && y == 9 + (x / 3) % 2 + (x - 8) / 4;
    if (first || second) {
        return scaled(spec.secondary, 0.75);
    }
    return color;
}

QColor paintChiseledBricks(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // Twice the distance from the centre along the larger axis: 1 in the middle, 15 at the edge.
    const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15));
    if (ring == 15) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (ring == 13) {
        return scaled(spec.primary, 1.12);
    }
    if (ring == 9 || ring == 3) {
        return scaled(spec.secondary, 0.9);
    }
    if (ring == 7 || ring == 1) {
        return scaled(spec.primary, 1.1);
    }
    return color;
}

QColor paintSmoothStone(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // A smooth face with a thin darker frame.
    const bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    return edge ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintGlazedTerracotta(const QcTextureSpec &spec, int x, int y, QColor color)
{
    // An ornament that is not symmetric: a frame on the top and left edges, a quarter ring around the
    // bottom-right corner and a diamond near the top-left one, so turned blocks next to each other
    // form larger figures.
    const int dx = size - 1 - x;
    const int dy = size - 1 - y;
    const int corner = dx * dx + dy * dy;
    const int diamond = std::abs(x - 4) + std::abs(y - 4);
    if (x == 0 || y == 0 || (corner >= 64 && corner <= 100)) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    if (corner < 16) {
        return scaled(spec.secondary, 0.8);
    }
    if (diamond <= 2) {
        return diamond == 2 ? scaled(spec.primary, 0.7) : varied(spec, spec.secondary, x, y, 5);
    }
    if (x == y && x > 6 && corner > 100) {
        return scaled(spec.primary, 0.82);
    }
    return color;
}

constexpr std::array<std::pair<Pattern, Painter>, 5> painters = {{
    {Pattern::MossyBricks, paintMossyBricks},
    {Pattern::CrackedBricks, paintCrackedBricks},
    {Pattern::ChiseledBricks, paintChiseledBricks},
    {Pattern::SmoothStone, paintSmoothStone},
    {Pattern::GlazedTerracotta, paintGlazedTerracotta},
}};

} // namespace

QcTexturePaint::Painter qcBuildingPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
