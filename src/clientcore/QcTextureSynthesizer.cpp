#include "QcTextureSynthesizer.h"


#include <algorithm>
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
