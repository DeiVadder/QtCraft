#include "QcTextureSynthesizer.h"


#include <algorithm>
#include <array>
#include <cmath>

namespace {

quint32 hashPixel(quint32 seed, int x, int y)
{
    quint32 h = seed * 0x9E3779B1u ^ quint32(x) * 0x85EBCA77u ^ quint32(y) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// Uniform value in [-1, 1].
double noise(quint32 seed, int x, int y)
{
    return double(hashPixel(seed, x, y) & 0xFFFFu) / 32767.5 - 1.0;
}

QColor scaled(const QColor &color, double factor, int alpha = 255)
{
    const auto channel = [factor](int value) { return std::clamp(int(std::lround(value * factor)), 0, 255); };
    return QColor(channel(color.red()), channel(color.green()), channel(color.blue()), alpha);
}

QColor varied(const QcTextureSpec &spec, const QColor &color, int x, int y, quint32 salt = 0)
{
    return scaled(color, 1.0 + spec.variation * noise(spec.seed + salt, x, y));
}

void paint(QImage &image, const QcTextureSpec &spec)
{
    constexpr int size = QcTextureSynthesizer::tileSize;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            QColor color = varied(spec, spec.primary, x, y);
            switch (spec.pattern) {
            case QcTextureSpec::Pattern::Noise:
                break;
            case QcTextureSpec::Pattern::Speckle:
                if ((hashPixel(spec.seed + 7, x, y) % 5) == 0) {
                    color = varied(spec, spec.secondary, x, y, 3);
                }
                break;
            case QcTextureSpec::Pattern::Planks: {
                const int row = y / 4;
                const int seamX = (row % 2 == 0) ? 3 : 11;
                if (y % 4 == 3 || x == seamX) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else {
                    color = scaled(spec.primary, 1.0 + spec.variation * 0.5 * noise(spec.seed, x / 4, y));
                }
                break;
            }
            case QcTextureSpec::Pattern::Bricks: {
                const int row = y / 4;
                const int offset = (row % 2 == 0) ? 0 : 4;
                if (y % 4 == 3 || (x + offset) % 8 == 7) {
                    color = varied(spec, spec.secondary, x, y, 5);
                }
                break;
            }
            case QcTextureSpec::Pattern::LogSide:
                if ((x + int(hashPixel(spec.seed, x, 0) % 3)) % 4 == 0) {
                    color = varied(spec, spec.secondary, x, y, 5);
                }
                break;
            case QcTextureSpec::Pattern::LogTop: {
                const double dx = x - 7.5;
                const double dy = y - 7.5;
                const double radius = std::sqrt(dx * dx + dy * dy);
                if (radius > 6.8) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else if (int(radius) % 2 == 0) {
                    color = scaled(color, 0.88);
                }
                break;
            }
            case QcTextureSpec::Pattern::Leaves:
                if ((hashPixel(spec.seed + 11, x, y) % 6) == 0) {
                    color = QColor(0, 0, 0, 0);
                } else if ((hashPixel(spec.seed + 13, x, y) % 4) == 0) {
                    color = varied(spec, spec.secondary, x, y, 3);
                }
                break;
            case QcTextureSpec::Pattern::Glass: {
                const bool frame = x == 0 || y == 0 || x == size - 1 || y == size - 1;
                const bool streak = (x - y == 3 || x - y == 4) && x > 2 && x < 10;
                if (frame) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else if (streak) {
                    color = QColor(255, 255, 255, 140);
                } else {
                    color = QColor(spec.primary.red(), spec.primary.green(), spec.primary.blue(), 40);
                }
                break;
            }
            case QcTextureSpec::Pattern::GrassSide: {
                const int fringe = 3 + int(hashPixel(spec.seed + 17, x, 0) % 3);
                if (y < fringe) {
                    color = varied(spec, spec.secondary, x, y, 3);
                }
                break;
            }
            case QcTextureSpec::Pattern::Ore: {
                const int cluster = int(hashPixel(spec.seed + 19, x / 3, y / 3) % 7);
                const bool inside = (hashPixel(spec.seed + 23, x, y) % 3) != 0;
                if (cluster == 0 && inside) {
                    color = varied(spec, spec.secondary, x, y, 29);
                }
                break;
            }
            case QcTextureSpec::Pattern::Water:
                color = scaled(spec.primary, 1.0 + spec.variation * std::sin((x + 2 * y) * 0.7 + noise(spec.seed, x, y)),
                               spec.primary.alpha());
                break;
            case QcTextureSpec::Pattern::Tool: {
                // Diagonal handle from bottom-left with a head at the top-right.
                const bool handle = x + y == size - 1 && x < 10;
                const bool head = (y <= 5 && x >= 7 && x + y >= 10 && x + y <= 14) || (y < 2 && x > 8);
                if (head) {
                    color = varied(spec, spec.primary, x, y);
                } else if (handle) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Pickaxe:
            case QcTextureSpec::Pattern::Axe:
            case QcTextureSpec::Pattern::Shovel:
            case QcTextureSpec::Pattern::Sword:
            case QcTextureSpec::Pattern::Stick: {
                // Diagonal handle from the bottom left; the head shape depends on the tool.
                const bool handle = x + y == size - 1 && x <= 9 && x >= 2;
                bool head = false;
                switch (spec.pattern) {
                case QcTextureSpec::Pattern::Pickaxe:
                    head = (y >= 1 && y <= 2 && x >= 5 && x <= 14) || (x >= 13 && x <= 14 && y <= 10 && y >= 1)
                           || (y >= 1 && y <= 3 && x >= 4 && x <= 5);
                    break;
                case QcTextureSpec::Pattern::Axe:
                    head = x >= 8 && x <= 13 && y >= 1 && y <= 7 && !(x <= 9 && y >= 5);
                    break;
                case QcTextureSpec::Pattern::Shovel:
                    head = x >= 10 && x <= 14 && y >= 1 && y <= 5 && x + y >= 12 && x + y <= 18;
                    break;
                case QcTextureSpec::Pattern::Sword:
                    head = (x + y == size - 1 || x + y == size) && x >= 6 && x <= 14;
                    break;
                default:
                    break;
                }
                const bool swordHandle = spec.pattern == QcTextureSpec::Pattern::Sword && x + y == size - 1 && x < 6 && x >= 1;
                if (head) {
                    color = varied(spec, spec.primary, x, y);
                } else if (handle || swordHandle || (spec.pattern == QcTextureSpec::Pattern::Stick && x + y == size - 1 && x >= 3 && x <= 12)) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Ingot: {
                const bool body = y >= 6 && y <= 10 && x >= 2 + (10 - y) / 2 && x <= 13 - (10 - y) / 2;
                color = body ? (y == 6 ? scaled(spec.primary, 1.25) : varied(spec, spec.primary, x, y)) : QColor(0, 0, 0, 0);
                break;
            }
            case QcTextureSpec::Pattern::Gem: {
                const int distance = std::abs(x - 7) + std::abs(y - 8);
                color = distance <= 6 ? (distance <= 2 ? scaled(spec.primary, 1.3) : varied(spec, spec.primary, x, y))
                                      : QColor(0, 0, 0, 0);
                break;
            }
            case QcTextureSpec::Pattern::Sapling: {
                const bool stem = (x == 7 || x == 8) && y >= 9;
                const double dx = x - 7.5;
                const double dy = y - 6.0;
                const bool crown = dx * dx + dy * dy * 1.4 < 26.0 + 8.0 * noise(spec.seed, x, y)
                                   && (hashPixel(spec.seed + 37, x, y) % 5) != 0 && y <= 11;
                if (crown) {
                    color = (hashPixel(spec.seed + 41, x, y) % 3) == 0 ? varied(spec, spec.primary, x, y, 3)
                                                                       : varied(spec, spec.primary, x, y);
                } else if (stem) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Face: {
                const bool eyeRow = y == 7 || y == 8;
                const bool leftEye = x >= 3 && x <= 5;
                const bool rightEye = x >= 10 && x <= 12;
                if (eyeRow && (leftEye || rightEye)) {
                    // The inner column of each eye is the pupil.
                    color = (x == 5 || x == 10) ? spec.secondary : spec.primary;
                } else if (y == 12 && x >= 6 && x <= 9) {
                    color = scaled(spec.secondary, 1.6);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Furnace: {
                const bool opening = x >= 3 && x <= 12 && y >= 8 && y <= 13;
                const bool lintel = x >= 2 && x <= 13 && y == 7;
                if (opening) {
                    // Glowing embers at the bottom of the opening, grate bars above them.
                    const bool ember = y == 13 && (hashPixel(spec.seed + 43, x, y) % 2) == 0;
                    if (ember) {
                        color = QColor(230, 120, 40);
                    } else if (y == 10 && x % 2 == 1) {
                        color = scaled(spec.secondary, 2.5);
                    } else {
                        color = varied(spec, spec.secondary, x, y, 7);
                    }
                } else if (lintel || y == 15 || y == 0) {
                    color = scaled(spec.primary, 0.75);
                }
                break;
            }
            case QcTextureSpec::Pattern::Torch:
                // Stretched over the whole torch box: the top quarter burns.
                if (y < 4) {
                    color = y < 2 ? scaled(spec.secondary, 1.15) : varied(spec, spec.secondary, x, y, 3);
                } else {
                    color = varied(spec, spec.primary, x, y, 5);
                }
                break;
            case QcTextureSpec::Pattern::TorchItem: {
                // A thin stick with a glowing tip and a flame tapering upwards, narrower at the
                // bottom than at its widest row so it reads as a flame, not as a cross.
                static constexpr std::array<std::pair<int, int>, 6> flameRows = {
                    {{8, 8}, {7, 8}, {7, 9}, {6, 9}, {6, 9}, {7, 8}}};
                const bool inFlame = y >= 1 && y <= 6 && x >= flameRows[std::size_t(y - 1)].first
                                     && x <= flameRows[std::size_t(y - 1)].second;
                const bool stick = (x == 7 || x == 8) && y >= 7 && y <= 15;
                if (inFlame) {
                    const bool core = (x == 7 || x == 8) && y >= 2 && y <= 5;
                    const bool edge = x == flameRows[std::size_t(y - 1)].first || x == flameRows[std::size_t(y - 1)].second;
                    if (y == 6) {
                        color = QColor(150, 60, 20);
                    } else if (core && y >= 3) {
                        color = QColor(255, 244, 190);
                    } else if (edge && y >= 3) {
                        color = scaled(spec.secondary, 0.85);
                    } else {
                        color = varied(spec, spec.secondary, x, y, 3);
                    }
                } else if (stick) {
                    color = varied(spec, x == 8 ? scaled(spec.primary, 0.75) : spec.primary, x, y, 5);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Ladder: {
                const bool rail = x == 2 || x == 3 || x == 12 || x == 13;
                const bool rung = (y % 4 == 1) && x > 3 && x < 12;
                if (rail) {
                    color = varied(spec, spec.primary, x, y, 5);
                } else if (rung) {
                    color = scaled(spec.primary, 0.9);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::DoorLower:
            case QcTextureSpec::Pattern::DoorUpper: {
                const bool lower = spec.pattern == QcTextureSpec::Pattern::DoorLower;
                const bool frame = x == 0 || x == size - 1 || (lower ? y == size - 1 : y == 0);
                const bool seam = x % 5 == 0;
                const bool window = !lower && x >= 3 && x <= 12 && y >= 2 && y <= 9 && x != 7 && x != 8 && y != 5;
                if (window) {
                    color = QColor(0, 0, 0, 0);
                } else if (frame || (!lower && (x == 2 || x == 13) && y >= 1 && y <= 10)) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else if (lower && x == 12 && (y == 1 || y == 2)) {
                    color = QColor(70, 70, 76);
                } else if (seam) {
                    color = scaled(spec.primary, 0.82);
                } else {
                    color = scaled(spec.primary, 1.0 + spec.variation * 0.5 * noise(spec.seed, x / 5, y));
                }
                break;
            }
            case QcTextureSpec::Pattern::Trapdoor: {
                const bool frame = x == 0 || y == 0 || x == size - 1 || y == size - 1;
                const bool hole = y >= 3 && y <= 6 && ((x >= 3 && x <= 6) || (x >= 9 && x <= 12));
                if (hole) {
                    color = QColor(0, 0, 0, 0);
                } else if (frame || y == 8) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else if (x % 4 == 0) {
                    color = scaled(spec.primary, 0.85);
                }
                break;
            }
            case QcTextureSpec::Pattern::ChestSide:
            case QcTextureSpec::Pattern::ChestFront:
            case QcTextureSpec::Pattern::ChestTop: {
                const bool top = spec.pattern == QcTextureSpec::Pattern::ChestTop;
                const bool frame = x <= 1 || x >= size - 2 || y >= size - 1 || (top ? y <= 1 : y <= 2);
                const bool lid = !top && (y == 6 || y == 7);
                const bool latch = spec.pattern == QcTextureSpec::Pattern::ChestFront && x >= 7 && x <= 8 && y >= 5 && y <= 9;
                if (latch) {
                    color = varied(spec, spec.secondary, x, y, 9);
                } else if (frame || lid) {
                    color = scaled(spec.primary, 0.62);
                } else if (y % 4 == 3) {
                    color = scaled(spec.primary, 0.85);
                }
                break;
            }
            case QcTextureSpec::Pattern::Lava: {
                const double wave = std::sin(x * 0.9 + 1.7 * noise(spec.seed, x / 3, y / 3)) + std::cos(y * 0.75 + x * 0.3);
                if (wave > 1.1) {
                    color = varied(spec, spec.secondary, x, y, 3);
                } else if (wave < -1.3 || (hashPixel(spec.seed + 47, x, y) % 11) == 0) {
                    color = scaled(spec.primary, 0.7);
                }
                break;
            }
            case QcTextureSpec::Pattern::Flower: {
                const double dx = x - 7.5;
                const double dy = y - 5.0;
                const double radius = dx * dx + dy * dy;
                const bool stem = (x == 7 || x == 8) && y >= 7;
                const bool leaf = (y == 11 && x >= 4 && x <= 6) || (y == 10 && x >= 9 && x <= 11);
                if (radius <= 2.0) {
                    color = QColor(250, 220, 90);
                } else if (radius <= 13.0 && (hashPixel(spec.seed + 53, x, y) % 7) != 0) {
                    color = varied(spec, spec.primary, x, y);
                } else if (stem || leaf) {
                    color = varied(spec, spec.secondary, x, y, 5);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::TallGrass: {
                const int height = 5 + int(hashPixel(spec.seed + 59, x, 0) % 10);
                const bool blade = (hashPixel(spec.seed + 61, x, 1) % 3) != 0 && y >= size - height;
                if (!blade) {
                    color = QColor(0, 0, 0, 0);
                } else if (y < size - height + 2) {
                    color = varied(spec, spec.secondary, x, y, 3);
                }
                break;
            }
            case QcTextureSpec::Pattern::Mushroom: {
                const double dx = (x - 7.5) / 6.5;
                const double dy = (y - 7.5) / 4.0;
                const bool cap = dx * dx + dy * dy <= 1.0 && y <= 8;
                const bool stem = x >= 6 && x <= 9 && y >= 8;
                if (cap) {
                    color = (hashPixel(spec.seed + 67, x / 2, y / 2) % 5) == 0 ? scaled(spec.primary, 1.8) : varied(spec, spec.primary, x, y);
                } else if (stem) {
                    color = varied(spec, QColor(224, 214, 186), x, y, 5);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::SugarCane: {
                const bool stalk = x == 2 || x == 3 || x == 7 || x == 8 || x == 12 || x == 13;
                const bool leaf = (y == 5 && x >= 4 && x <= 6) || (y == 10 && x >= 9 && x <= 11) || (y == 13 && x >= 0 && x <= 1);
                if (stalk) {
                    color = (y + x / 5) % 5 == 0 ? scaled(spec.primary, 0.7) : varied(spec, spec.primary, x, y);
                } else if (leaf) {
                    color = varied(spec, spec.secondary, x, y, 3);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Bottle: {
                const double dx = x - 7.5;
                const double dy = y - 10.0;
                const bool body = dx * dx + dy * dy <= 22.0;
                const bool neck = x >= 6 && x <= 9 && y >= 2 && y <= 6;
                const bool cork = x >= 6 && x <= 9 && y == 1;
                const bool edge = body && dx * dx + dy * dy > 13.0;
                if (cork) {
                    color = QColor(150, 110, 70);
                } else if (edge || (neck && (x == 6 || x == 9))) {
                    color = varied(spec, spec.primary, x, y);
                } else if (body && y >= 8) {
                    // Glowing contents sparkle; an empty bottle shows a pale glass tint.
                    color = spec.secondary.alpha() == 0 ? scaled(spec.primary, 1.15)
                                                        : ((hashPixel(spec.seed + 71, x, y) % 4) == 0 ? QColor(255, 255, 200)
                                                                                                      : varied(spec, spec.secondary, x, y, 3));
                } else if (body || neck) {
                    color = scaled(spec.primary, 1.15);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Bucket: {
                const int inset = 3 + (y - 5) / 4;
                const bool body = y >= 5 && y <= 14 && x >= inset && x <= size - 1 - inset;
                const bool handle = (y == 1 && x >= 5 && x <= 10) || ((x == 4 || x == 11) && y >= 2 && y <= 4);
                if (body && y <= 6 && x > inset && x < size - 1 - inset) {
                    color = spec.secondary.alpha() == 0 ? scaled(spec.primary, 0.45) : varied(spec, spec.secondary, x, y, 3);
                } else if (body) {
                    color = x == inset || x == size - 1 - inset ? scaled(spec.primary, 0.75) : varied(spec, spec.primary, x, y);
                } else if (handle) {
                    color = scaled(spec.primary, 0.6);
                } else {
                    color = QColor(0, 0, 0, 0);
                }
                break;
            }
            case QcTextureSpec::Pattern::Lantern: {
                const bool frame = x <= 2 || x >= size - 3 || y <= 2 || y >= size - 3;
                const bool bar = x == 7 || x == 8 || y == 7 || y == 8;
                if (frame) {
                    color = varied(spec, spec.primary, x, y);
                } else if (bar) {
                    color = scaled(spec.primary, 0.6);
                } else {
                    color = (hashPixel(spec.seed + 73, x, y) % 6) == 0 ? QColor(255, 255, 210) : varied(spec, spec.secondary, x, y, 3);
                }
                break;
            }
            case QcTextureSpec::Pattern::Lump: {
                const double dx = x - 7.5;
                const double dy = y - 7.5;
                if (dx * dx + dy * dy > 30.0 + 6.0 * noise(spec.seed, x, y)) {
                    color = QColor(0, 0, 0, 0);
                } else if ((hashPixel(spec.seed + 31, x, y) % 4) == 0) {
                    color = varied(spec, spec.secondary, x, y, 3);
                }
                break;
            }
            }
            if (spec.pattern != QcTextureSpec::Pattern::Glass && spec.pattern != QcTextureSpec::Pattern::Water
                && color.alpha() != 0) {
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
