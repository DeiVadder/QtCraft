#include "clientcore/textures/QcTextureFarmPatterns.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;
using namespace QcTexturePaint;

// Stalks at every third column, slightly bent, with an ear in the secondary colour at the top.
QColor paintCrop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int column = x - int(hashPixel(spec.seed, x / 3, 0) % 2);
    if (column % 3 != 1) {
        return clear();
    }
    const int top = 1 + int(hashPixel(spec.seed + 5, x / 3, 1) % 4);
    if (y < top) {
        return clear();
    }
    return y < top + 5 ? varied(spec, spec.secondary, x, y, 3) : color;
}

// Leafy tufts from the ground; the bottom rows show what grows below (secondary).
QColor paintRootCrop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int tuft = x / 4;
    const int centre = tuft * 4 + 2;
    const int top = 3 + int(hashPixel(spec.seed, tuft, 0) % 5);
    const int spread = (y - top) / 3 + 1;
    if (y >= size - 3 && std::abs(x - centre) <= 1) {
        return varied(spec, spec.secondary, x, y, 7);
    }
    if (y < top || std::abs(x - centre) > spread || (x + y) % 3 == 0) {
        return clear();
    }
    return color;
}

// A thin stem in the middle with a leaf on each side.
QColor paintStem(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool stem = (x == 7 || x == 8) && y >= 2;
    const bool leftLeaf = y >= 5 && y <= 7 && x >= 3 && x <= 6 && x + y >= 10;
    const bool rightLeaf = y >= 9 && y <= 11 && x >= 9 && x <= 12 && x - y <= 2;
    if (stem) {
        return color;
    }
    return leftLeaf || rightLeaf ? varied(spec, spec.secondary, x, y, 5) : clear();
}

// Soil with furrows running along x every fourth row.
QColor paintFarmland(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (y % 4 == 3) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return y % 4 == 0 ? scaled(color, 1.12) : color;
}

// Ribs running from top to bottom with darker grooves between them.
QColor paintPumpkinSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (x % 4 == 0) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return x % 4 == 2 ? scaled(color, 1.1) : color;
}

// Top of a pumpkin: the ribs meeting at a stalk in the middle.
QColor paintPumpkinTop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int dx = x - 7;
    const int dy = y - 7;
    if (dx * dx + dy * dy <= 2) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return (dx == 0 || dy == 0 || dx == dy || dx == -dy) ? scaled(color, 0.85) : color;
}

// Dark stripes wandering down a light green rind.
QColor paintMelonSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int wobble = int(hashPixel(spec.seed, 0, y) % 2);
    return (x + wobble) % 5 == 0 || (x + wobble) % 5 == 1 ? varied(spec, spec.secondary, x, y, 3) : color;
}

// Bound stalks spreading into ears at the top.
QColor paintWheat(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool tie = y == 10 && x >= 6 && x <= 9;
    const int spread = y < 7 ? (7 - y) / 2 + 2 : (y > 11 ? (y - 11) / 2 + 1 : 1);
    const bool inside = y >= 1 && y <= 15 && std::abs(2 * x - 15) <= 2 * spread + 1;
    if (tie) {
        return scaled(spec.secondary, 0.7);
    }
    if (!inside || (y < 7 && x % 2 == y % 2)) {
        return clear();
    }
    return y < 7 ? color : varied(spec, spec.secondary, x, y, 5);
}

// A loaf seen from the side: a crust (primary) with light cuts on top.
QColor paintBread(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = (x - 7.5) / 6.5;
    const double dy = (y - 9.5) / 4.0;
    if (dx * dx + dy * dy > 1.0) {
        return clear();
    }
    const bool cut = y == 7 && (x == 4 || x == 5 || x == 8 || x == 9 || x == 12);
    return cut ? varied(spec, spec.secondary, x, y, 3) : color;
}

// An orange root tapering to the bottom left with a green tuft at the top right.
QColor paintCarrot(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int along = x + (size - 1 - y);
    const int across = std::abs(x - (size - 1 - y));
    const bool root = along >= 4 && along <= 21 && across <= (along - 2) / 6 + 1;
    const bool tuft = along >= 22 && along <= 27 && across <= 2;
    if (tuft) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (!root) {
        return clear();
    }
    return along % 5 == 0 ? scaled(color, 0.85) : color;
}

// A half round slice: red flesh with seeds, a pale rim and a green rind at the bottom.
QColor paintMelonSlice(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = x - 7.5;
    const double dy = y - 4.0;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (y < 4 || distance > 7.5) {
        return clear();
    }
    if (distance > 6.5) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    if (distance > 5.5) {
        return QColor(232, 240, 180);
    }
    const bool seed = hashPixel(spec.seed, x, y) % 11 == 0;
    return seed ? QColor(40, 30, 20) : color;
}

// A round pie from above: a light crust edge (secondary) around the filling with a lattice.
QColor paintPie(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = x - 7.5;
    const double dy = y - 8.5;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (distance > 6.8) {
        return clear();
    }
    const bool lattice = distance <= 5.6 && ((x + y) % 4 == 0 || (x - y + 16) % 4 == 0);
    return distance > 5.6 || lattice ? varied(spec, spec.secondary, x, y, 3) : color;
}

// Straw from top to bottom: stalks of slightly varied shade, some darker (secondary), tied by two
// bands of darker, flattened straw.
QColor paintHaySide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool band = y == 3 || y == 4 || y == 11 || y == 12;
    const bool darkStalk = hashPixel(spec.seed, x, 0) % 4 == 0;
    const QColor stalk = darkStalk ? spec.secondary : color;
    if (band) {
        return scaled(varied(spec, spec.secondary, x, y, 3), 0.75);
    }
    return varied(spec, x % 2 == 0 ? stalk : scaled(stalk, 0.92), x, y, 7);
}

// The cut ends of the stalks: dots of light and dark straw in a bundle, a darker rim.
QColor paintHayTop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (x == 0 || y == 0 || x == size - 1 || y == size - 1) {
        return scaled(color, 0.8);
    }
    const quint32 end = hashPixel(spec.seed, x, y) % 5;
    if (end == 0) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return end == 1 ? scaled(color, 1.1) : varied(spec, color, x, y, 5);
}

// A C-shaped steel striker (secondary) at the top left and a flint chip (primary) at the bottom right.
QColor paintFlintAndSteel(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = x - 6.0;
    const double dy = y - 6.0;
    const double ring = std::sqrt(dx * dx + dy * dy);
    const bool striker = ring >= 3.0 && ring <= 4.6 && !(x > 6 && std::abs(y - 6) <= 1);
    const int along = x + y;
    const int across = std::abs(x - y);
    const bool flint = along >= 17 && along <= 27 && across <= 3 - std::abs(along - 22) / 3;
    if (striker) {
        return y < 5 ? scaled(spec.secondary, 1.15) : spec.secondary;
    }
    if (flint) {
        return varied(spec, along % 3 == 0 ? scaled(color, 1.3) : color, x, y, 9);
    }
    return clear();
}

// The ribs of a pumpkin's side with a carved face (secondary): two triangular eyes and a jagged mouth.
QColor paintCarvedPumpkin(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int fromCentre = std::abs(2 * x - 15);
    const bool eye = y >= 4 && y <= 7 && fromCentre >= 3 && fromCentre <= 11 && std::abs(fromCentre - 7) <= y - 4;
    const bool mouth = y >= 10 && y <= 12 && fromCentre <= 11 && !(y == 10 && x % 4 == 1) && !(y == 12 && x % 4 == 3);
    if (eye || mouth) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return paintPumpkinSide(spec, x, y, color);
}

// Whether (x, y) lies on a ring around (cx, cy) between the squared radii `inner` and `outer`.
bool onRing(int x, int y, int cx, int cy, std::pair<int, int> radii)
{
    const int distance = (x - cx) * (x - cx) + (y - cy) * (y - cy);
    return distance >= radii.first && distance <= radii.second;
}

// A pair of blades (primary) from the top right down to two dark handle rings (secondary) at the
// bottom left.
QColor paintShears(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if (onRing(x, y, 4, 11, {3, 7}) || onRing(x, y, 8, 13, {2, 5})) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    const int diagonal = x + y;
    if (x >= 7 && y >= 1 && (diagonal == 15 || diagonal == 16)) {
        return diagonal == 15 ? scaled(color, 1.15) : color;
    }
    return clear();
}

constexpr std::array<std::pair<Pattern, Painter>, 17> painters = {{
    {Pattern::Crop, paintCrop},
    {Pattern::RootCrop, paintRootCrop},
    {Pattern::Stem, paintStem},
    {Pattern::Farmland, paintFarmland},
    {Pattern::PumpkinSide, paintPumpkinSide},
    {Pattern::PumpkinTop, paintPumpkinTop},
    {Pattern::MelonSide, paintMelonSide},
    {Pattern::Wheat, paintWheat},
    {Pattern::Bread, paintBread},
    {Pattern::Carrot, paintCarrot},
    {Pattern::MelonSlice, paintMelonSlice},
    {Pattern::Pie, paintPie},
    {Pattern::HaySide, paintHaySide},
    {Pattern::HayTop, paintHayTop},
    {Pattern::FlintAndSteel, paintFlintAndSteel},
    {Pattern::CarvedPumpkin, paintCarvedPumpkin},
    {Pattern::Shears, paintShears},
}};

} // namespace

QcTexturePaint::Painter qcFarmPatternPainter(QcTextureSpec::Pattern pattern)
{
    for (const auto &[candidate, painter] : painters) {
        if (candidate == pattern) {
            return painter;
        }
    }
    return nullptr;
}
