#include "QcMapRenderer.h"

#include "QcContentRegistry.h"
#include "QcTerrainGenerator.h"

#include <QColor>
#include <QHash>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QColor blockColour(const QcContentRegistry &content, QcBlockId block)
{
    const QcTextureDefinition *texture = content.texture(content.block(block).texture(QcBlockFace::Up));
    // Ores show the colour of their speckles.
    const bool ore = texture && texture->procedural.value(u"pattern"_s).toString() == u"ore"_s;
    const QString primary = texture ? texture->procedural.value(ore ? u"secondary"_s : u"primary"_s).toString() : QString();
    const QColor colour = QColor::fromString(primary.left(7));
    return colour.isValid() ? colour : QColor(255, 0, 255);
}

} // namespace

namespace QcMapRenderer {

QImage render(const QcTerrainGenerator &generator, const QcContentRegistry &content, int centreX, int centreZ,
              int radius, int blocksPerPixel)
{
    const int size = 2 * radius / blocksPerPixel;
    QImage image(size, size, QImage::Format_RGB32);
    QHash<QcBlockId, QColor> colours;
    const QColor water(50, 90, 200);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const int x = centreX - radius + px * blocksPerPixel;
            const int z = centreZ - radius + py * blocksPerPixel;
            const QcTerrainGenerator::Column column = generator.column(x, z);
            const QcBlockId surface = generator.surfaceBlockAt(x, z);
            if (!colours.contains(surface)) {
                colours.insert(surface, blockColour(content, surface));
            }
            const QColor colour = colours.value(surface);
            const int depth = generator.seaLevel() - column.height;
            QColor pixel;
            if (depth > 0) {
                pixel = QColor::fromRgbF(water.redF() * 0.8f, water.greenF() * 0.8f,
                                         std::clamp(water.blueF() * (1.1f - float(depth) * 0.02f), 0.3f, 1.0f));
            } else {
                const float light = std::clamp(0.75f + float(column.height - generator.seaLevel()) * 0.006f, 0.6f, 1.25f);
                pixel = QColor::fromRgbF(std::min(1.0f, colour.redF() * light), std::min(1.0f, colour.greenF() * light),
                                         std::min(1.0f, colour.blueF() * light));
            }
            image.setPixelColor(px, py, pixel);
        }
    }
    return image;
}

QImage renderBiomes(const QcTerrainGenerator &generator, int centreX, int centreZ, int radius, int blocksPerPixel)
{
    const int size = 2 * radius / blocksPerPixel;
    QImage image(size, size, QImage::Format_RGB32);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const int x = centreX - radius + px * blocksPerPixel;
            const int z = centreZ - radius + py * blocksPerPixel;
            const QcTerrainGenerator::Column column = generator.column(x, z);
            if (column.height <= generator.seaLevel()) {
                image.setPixelColor(px, py, QColor(20, 30, 60));
                continue;
            }
            // Evenly spread hues; brighter with height.
            const float light = std::clamp(0.45f + float(column.height - generator.seaLevel()) * 0.008f, 0.4f, 0.95f);
            image.setPixelColor(px, py, QColor::fromHsvF(std::fmod(float(column.biome) * 0.381966f, 1.0f), 0.65f, light));
        }
    }
    return image;
}

QImage renderSlice(const QcTerrainGenerator &generator, const QcContentRegistry &content, int centreX, int z, int radius, int scale)
{
    const int width = 2 * radius;
    // Everything below the highest mountains; the sky above is left out.
    constexpr int height = 144;
    QImage image(width * scale, height * scale, QImage::Format_RGB32);
    image.fill(QColor(135, 170, 220));
    QHash<QcBlockId, QColor> colours;
    const QcBlockPos first{centreX - radius, 0, z};
    std::shared_ptr<QcChunk> chunk;
    for (int i = 0; i < width; ++i) {
        const QcBlockPos column{first.x + i, 0, z};
        if (!chunk || chunk->pos() != column.chunk()) {
            chunk = generator.generate(column.chunk());
        }
        for (int y = 0; y < height; ++y) {
            const QcBlockId block = chunk->block(column.localX(), y, column.localZ());
            if (block == qcAirBlockId) {
                continue;
            }
            if (!colours.contains(block)) {
                colours.insert(block, blockColour(content, block));
            }
            const QColor colour = colours.value(block);
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    image.setPixelColor(i * scale + dx, (height - 1 - y) * scale + dy, colour);
                }
            }
        }
    }
    return image;
}

} // namespace QcMapRenderer
