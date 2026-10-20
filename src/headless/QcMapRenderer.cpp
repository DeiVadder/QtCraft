#include "headless/QcMapRenderer.h"

#include "common/content/registry/QcContentRegistry.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QColor>
#include <QHash>
#include <QPoint>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QColor blockColour(const QcContentRegistry &content, QcBlockId block)
{
    const QcTextureDefinition *texture = content.assets().texture(content.block(block).texture(QcBlockFace::Up));
    // Ores show the colour of their speckles.
    const bool ore = texture && texture->procedural.value(u"pattern"_s).toString() == u"ore"_s;
    const QString primary = texture ? texture->procedural.value(ore ? u"secondary"_s : u"primary"_s).toString() : QString();
    const QColor colour = QColor::fromString(primary.left(7));
    return colour.isValid() ? colour : QColor(255, 0, 255);
}

// Water tinted by depth, land shaded by height.
QColor mapPixel(const QcTerrainGenerator &generator, const QcTerrainGenerator::Column &column, const QColor &colour)
{
    const QColor water(50, 90, 200);
    const int depth = generator.seaLevel() - column.height;
    if (depth > 0) {
        return QColor::fromRgbF(water.redF() * 0.8f, water.greenF() * 0.8f, std::clamp(water.blueF() * (1.1f - float(depth) * 0.02f), 0.3f, 1.0f));
    }
    const float light = std::clamp(0.75f + float(column.height - generator.seaLevel()) * 0.006f, 0.6f, 1.25f);
    return QColor::fromRgbF(std::min(1.0f, colour.redF() * light), std::min(1.0f, colour.greenF() * light),
                            std::min(1.0f, colour.blueF() * light));
}

// Everything below the highest mountains; the sky above is left out.
constexpr int sliceHeight = 144;

void fillCell(QImage &image, QPoint topLeft, int scale, const QColor &colour)
{
    for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) {
            image.setPixelColor(topLeft.x() + dx, topLeft.y() + dy, colour);
        }
    }
}

} // namespace

namespace QcMapRenderer {

QImage render(const QcTerrainGenerator &generator, const QcContentRegistry &content, Area area)
{
    const int size = 2 * area.radius / area.blocksPerPixel;
    QImage image(size, size, QImage::Format_RGB32);
    QHash<QcBlockId, QColor> colours;

    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const int x = area.centreX - area.radius + px * area.blocksPerPixel;
            const int z = area.centreZ - area.radius + py * area.blocksPerPixel;
            const QcBlockId surface = generator.surfaceBlockAt(x, z);
            if (!colours.contains(surface)) {
                colours.insert(surface, blockColour(content, surface));
            }
            image.setPixelColor(px, py, mapPixel(generator, generator.column(x, z), colours.value(surface)));
        }
    }

    return image;
}

QImage renderBiomes(const QcTerrainGenerator &generator, Area area)
{
    const int size = 2 * area.radius / area.blocksPerPixel;
    QImage image(size, size, QImage::Format_RGB32);

    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const int x = area.centreX - area.radius + px * area.blocksPerPixel;
            const int z = area.centreZ - area.radius + py * area.blocksPerPixel;
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

QImage renderSlice(const QcTerrainGenerator &generator, const QcContentRegistry &content, Cut cut)
{
    const int width = 2 * cut.radius;
    QImage image(width * cut.scale, sliceHeight * cut.scale, QImage::Format_RGB32);
    image.fill(QColor(135, 170, 220));
    QHash<QcBlockId, QColor> colours;
    const QcBlockPos first{cut.centreX - cut.radius, 0, cut.z};
    std::shared_ptr<QcChunk> chunk;

    for (int i = 0; i < width; ++i) {
        const QcBlockPos column{first.x + i, 0, cut.z};
        if (!chunk || chunk->pos() != column.chunk()) {
            chunk = generator.generate(column.chunk());
        }
        for (int y = 0; y < sliceHeight; ++y) {
            const QcBlockId block = chunk->block(column.localX(), y, column.localZ());
            if (block == qcAirBlockId) {
                continue;
            }
            if (!colours.contains(block)) {
                colours.insert(block, blockColour(content, block));
            }
            fillCell(image, {i * cut.scale, (sliceHeight - 1 - y) * cut.scale}, cut.scale, colours.value(block));
        }
    }

    return image;
}

} // namespace QcMapRenderer
