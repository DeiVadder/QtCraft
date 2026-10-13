#include "QcTextureSignalPatterns.h"

#include <array>
#include <cstdlib>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

// Loose grains (primary, some darker in the secondary colour) over two thirds of the tile; the rest
// stays open, so the ground shows through the wire.
QColor paintDust(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const quint32 grain = hashPixel(spec.seed, x / 2, y / 2) % 6;
    if (grain == 0 || (grain == 1 && hashPixel(spec.seed + 1, x, y) % 2 == 0)) {
        return clear();
    }
    return grain == 2 ? varied(spec, spec.secondary, x, y, 3) : color;
}

// A stick (primary) standing in a small stone base (secondary), as an item icon.
QColor paintLever(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool base = y >= 11 && y <= 14 && x >= 3 && x <= 12;
    const bool stick = (x == 7 || x == 8) && y >= 2 && y <= 10;
    if (base) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return stick ? color : clear();
}

// A smooth stone slab (primary) with a darker rim and a wire (secondary) running from the bottom to
// an arrow head at the top: the direction the signal leaves.
QColor paintRepeater(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (x == 0 || y == 0 || x == size - 1 || y == size - 1) {
        return scaled(color, 0.8);
    }

    const bool wire = (x == 7 || x == 8) && y >= 3 && y <= 13;
    const int arrow = std::abs(2 * x - 15);
    const bool head = y >= 2 && y <= 5 && arrow <= 2 * (y - 2) + 1;
    return wire || head ? varied(spec, spec.secondary, x, y, 7) : color;
}

// Stone (primary) in a wooden frame (secondary) two pixels wide, with darker corners.
QColor paintPistonSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool frame = x < 2 || y < 2 || x >= size - 2 || y >= size - 2;
    if (!frame) {
        return hashPixel(spec.seed, x, y) % 7 == 0 ? scaled(color, 0.85) : color;
    }

    const bool corner = (x < 2 || x >= size - 2) && (y < 2 || y >= size - 2);
    return scaled(varied(spec, spec.secondary, x, y, 9), corner ? 0.75 : 1.0);
}

constexpr std::array<std::pair<Pattern, Painter>, 4> painters = {{
    {Pattern::Dust, paintDust},
    {Pattern::Lever, paintLever},
    {Pattern::Repeater, paintRepeater},
    {Pattern::PistonSide, paintPistonSide},
}};

} // namespace

QcTexturePaint::Painter qcSignalPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
