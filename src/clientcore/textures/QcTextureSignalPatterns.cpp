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

// Riveted iron plates (primary) with a seam across the middle and rivets in the corners (secondary).
QColor paintHopper(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool rivet = (x == 2 || x == size - 3) && (y == 2 || y == size - 3 || y == 9);
    const bool seam = y == 8 || x == 0 || x == size - 1;
    if (rivet) {
        return scaled(varied(spec, spec.secondary, x, y, 3), 1.4);
    }
    return seam ? varied(spec, spec.secondary, x, y, 5) : color;
}

// The rim seen from above (primary) two pixels wide around the dark bowl (secondary).
QColor paintHopperTop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool rim = x < 2 || y < 2 || x >= size - 2 || y >= size - 2;
    return rim ? color : varied(spec, spec.secondary, x, y, 7);
}

// A funnel as an icon: a wide bowl narrowing to a spout, on a transparent background.
QColor paintHopperItem(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int half = y <= 6 ? 7 : (y <= 10 ? 4 : (y <= 13 ? 2 : -1));
    const int fromCentre = std::abs(2 * x - 15) / 2;
    if (half < 0 || y < 2 || fromCentre > half) {
        return clear();
    }
    return y == 2 || fromCentre == half ? varied(spec, spec.secondary, x, y, 9) : color;
}

// A smooth slab (primary) with a darker rim, three torch spots and a wire between them (secondary).
QColor paintComparator(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (x == 0 || y == 0 || x == size - 1 || y == size - 1) {
        return scaled(color, 0.8);
    }

    const auto spot = [x, y](int cx, int cy) { return std::abs(x - cx) <= 1 && std::abs(y - cy) <= 1; };
    const bool wire = (y == 11 && x >= 4 && x <= 11) || ((x == 7 || x == 8) && y >= 4 && y <= 11);
    return spot(7, 3) || spot(4, 11) || spot(11, 11) || wire ? varied(spec, spec.secondary, x, y, 11) : color;
}

// A stone face (primary) with a dark slit of two eyes across the middle (secondary).
QColor paintObserverFace(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool slit = y >= 6 && y <= 9 && x >= 2 && x <= 13;
    const bool eye = slit && y >= 7 && y <= 8 && (x == 4 || x == 5 || x == 10 || x == 11);
    if (eye) {
        return scaled(color, 1.5);
    }
    return slit ? varied(spec, spec.secondary, x, y, 13) : color;
}

// The back: a frame and a round spot in the middle (secondary) where the pulse leaves.
QColor paintObserverBack(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int dx = 2 * x - 15;
    const int dy = 2 * y - 15;
    if (dx * dx + dy * dy <= 30) {
        return varied(spec, spec.secondary, x, y, 15);
    }
    return x < 1 || y < 1 || x >= size - 1 || y >= size - 1 ? scaled(color, 0.75) : color;
}

// A side with an arrow (secondary) pointing up, towards the face.
QColor paintObserverSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int arrow = std::abs(2 * x - 15);
    const bool head = y >= 3 && y <= 7 && arrow <= 2 * (y - 3) + 1;
    const bool shaft = (x == 7 || x == 8) && y >= 7 && y <= 12;
    return head || shaft ? varied(spec, spec.secondary, x, y, 17) : color;
}

// A stone face (primary) with a round mouth (dispenser) or a narrow slot (dropper) in the secondary colour.
QColor paintMouth(const QcTextureSpec &spec, int x, int y, QColor color, bool round)
{
    const int dx = 2 * x - 15;
    const int dy = 2 * y - 15;
    const bool hole = round ? dx * dx + dy * dy <= 50 : std::abs(dx) <= 7 && std::abs(dy) <= 3;
    const bool rim = round ? dx * dx + dy * dy <= 80 : std::abs(dx) <= 9 && std::abs(dy) <= 5;
    if (hole) {
        return varied(spec, spec.secondary, x, y, 19);
    }
    if (rim) {
        return scaled(color, 0.7);
    }
    return hashPixel(spec.seed, x, y) % 9 == 0 ? scaled(color, 0.85) : color;
}

QColor paintDispenser(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintMouth(spec, x, y, color, true);
}

QColor paintDropper(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return paintMouth(spec, x, y, color, false);
}

constexpr std::array<std::pair<Pattern, Painter>, 13> painters = {{
    {Pattern::Dust, paintDust},
    {Pattern::Lever, paintLever},
    {Pattern::Repeater, paintRepeater},
    {Pattern::PistonSide, paintPistonSide},
    {Pattern::Hopper, paintHopper},
    {Pattern::HopperTop, paintHopperTop},
    {Pattern::HopperItem, paintHopperItem},
    {Pattern::Comparator, paintComparator},
    {Pattern::ObserverFace, paintObserverFace},
    {Pattern::ObserverBack, paintObserverBack},
    {Pattern::ObserverSide, paintObserverSide},
    {Pattern::Dispenser, paintDispenser},
    {Pattern::Dropper, paintDropper},
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
