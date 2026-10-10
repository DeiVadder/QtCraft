#include "QcTextureSynthesizer.h"

#include "QcTextureArmourPatterns.h"
#include "QcTextureCraftPatterns.h"
#include "QcTextureEmblem.h"
#include "QcTexturePaint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

using Pattern = QcTextureSpec::Pattern;

using namespace QcTexturePaint;

static_assert(QcTexturePaint::size == QcTextureSynthesizer::tileSize);

QColor paintNoise(const QcTextureSpec &, int, int, QColor color)
{
    return color;
}

QColor paintSpeckle(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return (hashPixel(spec.seed + 7, x, y) % 5) == 0 ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintPlanks(const QcTextureSpec &spec, int x, int y, QColor)
{
    const int row = y / 4;
    const int seamX = (row % 2 == 0) ? 3 : 11;
    if (y % 4 == 3 || x == seamX) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return scaled(spec.primary, 1.0 + spec.variation * 0.5 * noise(spec.seed, x / 4, y));
}

QColor paintBricks(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int row = y / 4;
    const int offset = (row % 2 == 0) ? 0 : 4;
    return y % 4 == 3 || (x + offset) % 8 == 7 ? varied(spec, spec.secondary, x, y, 5) : color;
}

QColor paintLogSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    return (x + int(hashPixel(spec.seed, x, 0) % 3)) % 4 == 0 ? varied(spec, spec.secondary, x, y, 5) : color;
}

QColor paintLogTop(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = x - 7.5;
    const double dy = y - 7.5;
    const double radius = std::sqrt(dx * dx + dy * dy);
    if (radius > 6.8) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return int(radius) % 2 == 0 ? scaled(color, 0.88) : color;
}

QColor paintLeaves(const QcTextureSpec &spec, int x, int y, QColor color)
{
    if ((hashPixel(spec.seed + 11, x, y) % 6) == 0) {
        return clear();
    }
    return (hashPixel(spec.seed + 13, x, y) % 4) == 0 ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintGlass(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool frame = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    const bool streak = (x - y == 3 || x - y == 4) && x > 2 && x < 10;
    if (frame) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (streak) {
        return QColor(255, 255, 255, 140);
    }
    return QColor(spec.primary.red(), spec.primary.green(), spec.primary.blue(), 40);
}

QColor paintGrassSide(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int fringe = 3 + int(hashPixel(spec.seed + 17, x, 0) % 3);
    return y < fringe ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintOre(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int cluster = int(hashPixel(spec.seed + 19, x / 3, y / 3) % 7);
    const bool inside = (hashPixel(spec.seed + 23, x, y) % 3) != 0;
    return cluster == 0 && inside ? varied(spec, spec.secondary, x, y, 29) : color;
}

QColor paintWater(const QcTextureSpec &spec, int x, int y, QColor)
{
    return scaled(spec.primary, 1.0 + spec.variation * std::sin((x + 2 * y) * 0.7 + noise(spec.seed, x, y)), spec.primary.alpha());
}

QColor paintTool(const QcTextureSpec &spec, int x, int y, QColor)
{
    // Diagonal handle from bottom-left with a head at the top-right.
    const bool handle = x + y == size - 1 && x < 10;
    const bool head = (y <= 5 && x >= 7 && x + y >= 10 && x + y <= 14) || (y < 2 && x > 8);
    if (head) {
        return varied(spec, spec.primary, x, y);
    }
    if (handle) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return clear();
}

// Head shape of each tool pattern (none for a stick).
bool toolHead(Pattern pattern, int x, int y)
{
    switch (pattern) {
    case Pattern::Pickaxe:
        return (y >= 1 && y <= 2 && x >= 5 && x <= 14) || (x >= 13 && x <= 14 && y <= 10 && y >= 1)
               || (y >= 1 && y <= 3 && x >= 4 && x <= 5);
    case Pattern::Axe:
        return x >= 8 && x <= 13 && y >= 1 && y <= 7 && !(x <= 9 && y >= 5);
    case Pattern::Shovel:
        return x >= 10 && x <= 14 && y >= 1 && y <= 5 && x + y >= 12 && x + y <= 18;
    case Pattern::Sword:
        return (x + y == size - 1 || x + y == size) && x >= 6 && x <= 14;
    default:
        return false;
    }
}

QColor paintToolWithHead(const QcTextureSpec &spec, int x, int y, QColor)
{
    // Diagonal handle from the bottom left; the head shape depends on the tool.
    const bool handle = x + y == size - 1 && x <= 9 && x >= 2;
    const bool head = toolHead(spec.pattern, x, y);
    const bool swordHandle = spec.pattern == Pattern::Sword && x + y == size - 1 && x < 6 && x >= 1;
    if (head) {
        return varied(spec, spec.primary, x, y);
    }
    if (handle || swordHandle || (spec.pattern == Pattern::Stick && x + y == size - 1 && x >= 3 && x <= 12)) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return clear();
}

QColor paintIngot(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool body = y >= 6 && y <= 10 && x >= 2 + (10 - y) / 2 && x <= 13 - (10 - y) / 2;
    return body ? (y == 6 ? scaled(spec.primary, 1.25) : varied(spec, spec.primary, x, y)) : clear();
}

QColor paintGem(const QcTextureSpec &spec, int x, int y, QColor)
{
    const int distance = std::abs(x - 7) + std::abs(y - 8);
    return distance <= 6 ? (distance <= 2 ? scaled(spec.primary, 1.3) : varied(spec, spec.primary, x, y)) : clear();
}

QColor paintSapling(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool stem = (x == 7 || x == 8) && y >= 9;
    const double dx = x - 7.5;
    const double dy = y - 6.0;
    const bool crown = dx * dx + dy * dy * 1.4 < 26.0 + 8.0 * noise(spec.seed, x, y) && (hashPixel(spec.seed + 37, x, y) % 5) != 0
                       && y <= 11;
    if (crown) {
        return (hashPixel(spec.seed + 41, x, y) % 3) == 0 ? varied(spec, spec.primary, x, y, 3) : varied(spec, spec.primary, x, y);
    }
    if (stem) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return clear();
}

QColor paintFace(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool eyeRow = y == 7 || y == 8;
    const bool leftEye = x >= 3 && x <= 5;
    const bool rightEye = x >= 10 && x <= 12;
    if (eyeRow && (leftEye || rightEye)) {
        // The inner column of each eye is the pupil.
        return (x == 5 || x == 10) ? spec.secondary : spec.primary;
    }
    if (y == 12 && x >= 6 && x <= 9) {
        return scaled(spec.secondary, 1.6);
    }
    return clear();
}

QColor paintFurnace(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool opening = x >= 3 && x <= 12 && y >= 8 && y <= 13;
    const bool lintel = x >= 2 && x <= 13 && y == 7;
    if (opening) {
        // Glowing embers at the bottom of the opening, grate bars above them.
        const bool ember = y == 13 && (hashPixel(spec.seed + 43, x, y) % 2) == 0;
        if (ember) {
            return QColor(230, 120, 40);
        }
        if (y == 10 && x % 2 == 1) {
            return scaled(spec.secondary, 2.5);
        }
        return varied(spec, spec.secondary, x, y, 7);
    }
    return lintel || y == 15 || y == 0 ? scaled(spec.primary, 0.75) : color;
}

QColor paintTorch(const QcTextureSpec &spec, int x, int y, QColor)
{
    // Stretched over the whole torch box: the top quarter burns.
    if (y < 4) {
        return y < 2 ? scaled(spec.secondary, 1.15) : varied(spec, spec.secondary, x, y, 3);
    }
    return varied(spec, spec.primary, x, y, 5);
}

QColor paintTorchItem(const QcTextureSpec &spec, int x, int y, QColor)
{
    // A thin stick with a glowing tip and a flame tapering upwards, narrower at the bottom than at
    // its widest row so it reads as a flame, not as a cross.
    static constexpr std::array<std::pair<int, int>, 6> flameRows = {{{8, 8}, {7, 8}, {7, 9}, {6, 9}, {6, 9}, {7, 8}}};
    const bool inFlame = y >= 1 && y <= 6 && x >= flameRows[std::size_t(y - 1)].first && x <= flameRows[std::size_t(y - 1)].second;
    const bool stick = (x == 7 || x == 8) && y >= 7 && y <= 15;
    if (stick) {
        return varied(spec, x == 8 ? scaled(spec.primary, 0.75) : spec.primary, x, y, 5);
    }
    if (!inFlame) {
        return clear();
    }

    const bool core = (x == 7 || x == 8) && y >= 2 && y <= 5;
    const bool edge = x == flameRows[std::size_t(y - 1)].first || x == flameRows[std::size_t(y - 1)].second;
    if (y == 6) {
        return QColor(150, 60, 20);
    }
    if (core && y >= 3) {
        return QColor(255, 244, 190);
    }
    if (edge && y >= 3) {
        return scaled(spec.secondary, 0.85);
    }
    return varied(spec, spec.secondary, x, y, 3);
}

QColor paintLadder(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool rail = x == 2 || x == 3 || x == 12 || x == 13;
    const bool rung = (y % 4 == 1) && x > 3 && x < 12;
    if (rail) {
        return varied(spec, spec.primary, x, y, 5);
    }
    if (rung) {
        return scaled(spec.primary, 0.9);
    }
    return clear();
}

QColor paintDoor(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool lower = spec.pattern == Pattern::DoorLower;
    const bool frame = x == 0 || x == size - 1 || (lower ? y == size - 1 : y == 0);
    const bool seam = x % 5 == 0;
    const bool window = !lower && x >= 3 && x <= 12 && y >= 2 && y <= 9 && x != 7 && x != 8 && y != 5;

    if (window) {
        return clear();
    }
    if (frame || (!lower && (x == 2 || x == 13) && y >= 1 && y <= 10)) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    if (lower && x == 12 && (y == 1 || y == 2)) {
        return QColor(70, 70, 76);
    }
    if (seam) {
        return scaled(spec.primary, 0.82);
    }
    return scaled(spec.primary, 1.0 + spec.variation * 0.5 * noise(spec.seed, x / 5, y));
}

QColor paintTrapdoor(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool frame = x == 0 || y == 0 || x == size - 1 || y == size - 1;
    const bool hole = y >= 3 && y <= 6 && ((x >= 3 && x <= 6) || (x >= 9 && x <= 12));
    if (hole) {
        return clear();
    }
    if (frame || y == 8) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return x % 4 == 0 ? scaled(spec.primary, 0.85) : color;
}

QColor paintChest(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const bool top = spec.pattern == Pattern::ChestTop;
    const bool frame = x <= 1 || x >= size - 2 || y >= size - 1 || (top ? y <= 1 : y <= 2);
    const bool lid = !top && (y == 6 || y == 7);
    const bool latch = spec.pattern == Pattern::ChestFront && x >= 7 && x <= 8 && y >= 5 && y <= 9;
    if (latch) {
        return varied(spec, spec.secondary, x, y, 9);
    }
    if (frame || lid) {
        return scaled(spec.primary, 0.62);
    }
    return y % 4 == 3 ? scaled(spec.primary, 0.85) : color;
}

QColor paintLava(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double wave = std::sin(x * 0.9 + 1.7 * noise(spec.seed, x / 3, y / 3)) + std::cos(y * 0.75 + x * 0.3);
    if (wave > 1.1) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return wave < -1.3 || (hashPixel(spec.seed + 47, x, y) % 11) == 0 ? scaled(spec.primary, 0.7) : color;
}

QColor paintFlower(const QcTextureSpec &spec, int x, int y, QColor)
{
    const double dx = x - 7.5;
    const double dy = y - 5.0;
    const double radius = dx * dx + dy * dy;
    const bool stem = (x == 7 || x == 8) && y >= 7;
    const bool leaf = (y == 11 && x >= 4 && x <= 6) || (y == 10 && x >= 9 && x <= 11);

    if (radius <= 2.0) {
        return QColor(250, 220, 90);
    }
    if (radius <= 13.0 && (hashPixel(spec.seed + 53, x, y) % 7) != 0) {
        return varied(spec, spec.primary, x, y);
    }
    if (stem || leaf) {
        return varied(spec, spec.secondary, x, y, 5);
    }
    return clear();
}

QColor paintTallGrass(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const int height = 5 + int(hashPixel(spec.seed + 59, x, 0) % 10);
    const bool blade = (hashPixel(spec.seed + 61, x, 1) % 3) != 0 && y >= size - height;
    if (!blade) {
        return clear();
    }
    return y < size - height + 2 ? varied(spec, spec.secondary, x, y, 3) : color;
}

QColor paintMushroom(const QcTextureSpec &spec, int x, int y, QColor)
{
    const double dx = (x - 7.5) / 6.5;
    const double dy = (y - 7.5) / 4.0;
    const bool cap = dx * dx + dy * dy <= 1.0 && y <= 8;
    const bool stem = x >= 6 && x <= 9 && y >= 8;
    if (cap) {
        return (hashPixel(spec.seed + 67, x / 2, y / 2) % 5) == 0 ? scaled(spec.primary, 1.8) : varied(spec, spec.primary, x, y);
    }
    if (stem) {
        return varied(spec, QColor(224, 214, 186), x, y, 5);
    }
    return clear();
}

QColor paintSugarCane(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool stalk = x == 2 || x == 3 || x == 7 || x == 8 || x == 12 || x == 13;
    const bool leaf = (y == 5 && x >= 4 && x <= 6) || (y == 10 && x >= 9 && x <= 11) || (y == 13 && x >= 0 && x <= 1);
    if (stalk) {
        return (y + x / 5) % 5 == 0 ? scaled(spec.primary, 0.7) : varied(spec, spec.primary, x, y);
    }
    if (leaf) {
        return varied(spec, spec.secondary, x, y, 3);
    }
    return clear();
}

QColor paintBottle(const QcTextureSpec &spec, int x, int y, QColor)
{
    const double dx = x - 7.5;
    const double dy = y - 10.0;
    const bool body = dx * dx + dy * dy <= 22.0;
    const bool neck = x >= 6 && x <= 9 && y >= 2 && y <= 6;
    const bool cork = x >= 6 && x <= 9 && y == 1;
    const bool edge = body && dx * dx + dy * dy > 13.0;

    if (cork) {
        return QColor(150, 110, 70);
    }
    if (edge || (neck && (x == 6 || x == 9))) {
        return varied(spec, spec.primary, x, y);
    }
    if (body && y >= 8) {
        // Glowing contents sparkle; an empty bottle shows a pale glass tint.
        if (spec.secondary.alpha() == 0) {
            return scaled(spec.primary, 1.15);
        }
        return (hashPixel(spec.seed + 71, x, y) % 4) == 0 ? QColor(255, 255, 200) : varied(spec, spec.secondary, x, y, 3);
    }
    if (body || neck) {
        return scaled(spec.primary, 1.15);
    }
    return clear();
}

QColor paintBucket(const QcTextureSpec &spec, int x, int y, QColor)
{
    const int inset = 3 + (y - 5) / 4;
    const bool body = y >= 5 && y <= 14 && x >= inset && x <= size - 1 - inset;
    const bool handle = (y == 1 && x >= 5 && x <= 10) || ((x == 4 || x == 11) && y >= 2 && y <= 4);
    if (body && y <= 6 && x > inset && x < size - 1 - inset) {
        return spec.secondary.alpha() == 0 ? scaled(spec.primary, 0.45) : varied(spec, spec.secondary, x, y, 3);
    }
    if (body) {
        return x == inset || x == size - 1 - inset ? scaled(spec.primary, 0.75) : varied(spec, spec.primary, x, y);
    }
    if (handle) {
        return scaled(spec.primary, 0.6);
    }
    return clear();
}

QColor paintLantern(const QcTextureSpec &spec, int x, int y, QColor)
{
    const bool frame = x <= 2 || x >= size - 3 || y <= 2 || y >= size - 3;
    const bool bar = x == 7 || x == 8 || y == 7 || y == 8;
    if (frame) {
        return varied(spec, spec.primary, x, y);
    }
    if (bar) {
        return scaled(spec.primary, 0.6);
    }
    return (hashPixel(spec.seed + 73, x, y) % 6) == 0 ? QColor(255, 255, 210) : varied(spec, spec.secondary, x, y, 3);
}

QColor paintLump(const QcTextureSpec &spec, int x, int y, QColor color)
{
    const double dx = x - 7.5;
    const double dy = y - 7.5;
    if (dx * dx + dy * dy > 30.0 + 6.0 * noise(spec.seed, x, y)) {
        return clear();
    }
    return (hashPixel(spec.seed + 31, x, y) % 4) == 0 ? varied(spec, spec.secondary, x, y, 3) : color;
}

// Darker towards the left and right edge, so a flat face of a box reads as a rounded limb.
double roundShade(int x)
{
    const double across = (x - 7.5) / 7.5;
    return 1.0 - 0.16 * across * across;
}

QColor paintLimb(const QcTextureSpec &spec, int x, int y, QColor)
{
    // Large soft patches instead of per-pixel grain.
    const double mottle = 0.5 * noise(spec.seed, x / 3, y / 3) + 0.5 * noise(spec.seed + 1, x / 2, y / 4);
    const double shade = roundShade(x) * (1.0 + spec.variation * mottle);
    // A faint knuckle line and three finger gaps near the lower end.
    if ((y == 12 && x % 4 != 0) || (y >= 13 && x % 4 == 0 && x > 0)) {
        return scaled(spec.secondary, shade);
    }
    return scaled(spec.primary, shade);
}

QColor paintSleeve(const QcTextureSpec &spec, int x, int y, QColor)
{
    const double weave = (x + y) % 2 == 0 ? 1.0 : 0.96;
    // Folds run diagonally around the sleeve.
    const double fold = 1.0 - 0.08 * std::max(0.0, std::sin((x + 2.0 * y + double(spec.seed % 7)) * 0.55));
    const double shade = roundShade(x) * weave * fold * (1.0 + spec.variation * noise(spec.seed, x, y));
    if (y >= 13) {
        return scaled(spec.secondary, shade);
    }
    if (y == 12) {
        // Stitches of the cuff's seam.
        return x % 2 == 0 ? scaled(spec.primary, 1.05) : scaled(spec.secondary, shade * 0.85);
    }
    return scaled(spec.primary, shade);
}

constexpr std::array<std::pair<Pattern, Painter>, 43> painters = {{
    {Pattern::Noise, paintNoise},
    {Pattern::Speckle, paintSpeckle},
    {Pattern::Planks, paintPlanks},
    {Pattern::Bricks, paintBricks},
    {Pattern::LogSide, paintLogSide},
    {Pattern::LogTop, paintLogTop},
    {Pattern::Leaves, paintLeaves},
    {Pattern::Glass, paintGlass},
    {Pattern::GrassSide, paintGrassSide},
    {Pattern::Ore, paintOre},
    {Pattern::Water, paintWater},
    {Pattern::Tool, paintTool},
    {Pattern::Lump, paintLump},
    {Pattern::Pickaxe, paintToolWithHead},
    {Pattern::Axe, paintToolWithHead},
    {Pattern::Shovel, paintToolWithHead},
    {Pattern::Sword, paintToolWithHead},
    {Pattern::Stick, paintToolWithHead},
    {Pattern::Ingot, paintIngot},
    {Pattern::Gem, paintGem},
    {Pattern::Sapling, paintSapling},
    {Pattern::Face, paintFace},
    {Pattern::Furnace, paintFurnace},
    {Pattern::Torch, paintTorch},
    {Pattern::TorchItem, paintTorchItem},
    {Pattern::Ladder, paintLadder},
    {Pattern::DoorLower, paintDoor},
    {Pattern::DoorUpper, paintDoor},
    {Pattern::Trapdoor, paintTrapdoor},
    {Pattern::ChestSide, paintChest},
    {Pattern::ChestFront, paintChest},
    {Pattern::ChestTop, paintChest},
    {Pattern::Lava, paintLava},
    {Pattern::Flower, paintFlower},
    {Pattern::TallGrass, paintTallGrass},
    {Pattern::Mushroom, paintMushroom},
    {Pattern::SugarCane, paintSugarCane},
    {Pattern::Bottle, paintBottle},
    {Pattern::Bucket, paintBucket},
    {Pattern::Lantern, paintLantern},
    {Pattern::Limb, paintLimb},
    {Pattern::Sleeve, paintSleeve},
    {Pattern::Emblem, QcTextureEmblem::paint},
}};

Painter painterFor(Pattern pattern)
{
    const auto it = std::ranges::find(painters, pattern, &std::pair<Pattern, Painter>::first);
    if (it != painters.end()) {
        return it->second;
    }
    if (const Painter craft = qcCraftPatternPainter(pattern)) {
        return craft;
    }
    const Painter armour = qcArmourPatternPainter(pattern);
    return armour ? armour : paintNoise;
}

void paint(QImage &image, const QcTextureSpec &spec)
{
    const Painter painter = painterFor(spec.pattern);
    // Only glass, water and the glass-like craft patterns keep partial transparency.
    const bool translucent = spec.pattern == Pattern::Glass || spec.pattern == Pattern::Water || qcCraftPatternIsTranslucent(spec.pattern);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            QColor color = painter(spec, x, y, varied(spec, spec.primary, x, y));
            if (!translucent && color.alpha() != 0) {
                color.setAlpha(255);
            }
            image.setPixelColor(x, y, color);
        }
    }
}

} // namespace

QImage QcTextureSynthesizer::synthesize(const QcTextureSpec &spec)
{
    QImage image(tileSize, tileSize, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    paint(image, spec);
    return image;
}
