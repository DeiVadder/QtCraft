#include "QcMapRenderer.h"

#include "QcContentRegistry.h"
#include "QcTerrainGenerator.h"

#include <QColor>
#include <QHash>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QColor blockColour(const QcContentRegistry &content, QcBlockId block)
{
    const QcTextureDefinition *texture = content.texture(content.block(block).texture(QcBlockFace::Up));
    const QString primary = texture ? texture->procedural.value(u"primary"_s).toString() : QString();
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

} // namespace QcMapRenderer
