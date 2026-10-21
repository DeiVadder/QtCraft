#include "clientcore/maps/QcMapColors.h"

#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/maps/QcMapPixel.h"

#include <QHash>

#include <utility>

namespace {

// Tiles of this size are enough for an average (a lower mip level of a high resolution pack).
constexpr int averageTileSize = 16;

// The texture a block shows from above.
QString topTexture(const QcBlockDefinition &block)
{
    const QString &top = block.texture(QcBlockFace::Up);
    return top.isEmpty() ? block.texture(QcBlockFace::North) : top;
}

QRgb shaded(QRgb colour, QcMapPixel::Shade shade)
{
    const int brightness = QcMapPixel::brightness[int(shade)];
    return qRgb(qRed(colour) * brightness / 255, qGreen(colour) * brightness / 255, qBlue(colour) * brightness / 255);
}

} // namespace

QRgb QcMapColors::average(const QImage &texture)
{
    const QImage image = texture.convertToFormat(QImage::Format_ARGB32);
    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    qint64 weight = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int alpha = qAlpha(line[x]);
            red += qint64(qRed(line[x])) * alpha;
            green += qint64(qGreen(line[x])) * alpha;
            blue += qint64(qBlue(line[x])) * alpha;
            weight += alpha;
        }
    }

    if (weight == 0) {
        return unknown;
    }
    return qRgb(int(red / weight), int(green / weight), int(blue / weight));
}

QList<QRgb> QcMapColors::ofAtlas(const QcContentRegistry &content, const QcTextureAtlas &atlas)
{
    // Many blocks share a texture (slabs, stairs): each one is averaged once.
    QHash<QString, QRgb> byTexture;
    QList<QRgb> colours;
    colours.reserve(content.blocks().size());
    for (const QcBlockDefinition &block : content.blocks()) {
        const QString texture = topTexture(block);
        if (const auto found = byTexture.constFind(texture); found != byTexture.cend()) {
            colours.append(*found);
            continue;
        }
        const QRgb colour = atlas.contains(texture) ? average(atlas.tile(texture, averageTileSize)) : unknown;
        byTexture.insert(texture, colour);
        colours.append(colour);
    }
    return colours;
}

QImage QcMapColors::render(const QcMapData &map, const QList<QRgb> &colours)
{
    QImage image(QcMapData::size, QcMapData::size, QImage::Format_ARGB32);
    for (int y = 0; y < QcMapData::size; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < QcMapData::size; ++x) {
            const quint16 pixel = map.at(QPoint(x, y));
            const std::optional<quint32> block = QcMapPixel::blockOf(pixel);
            if (!block) {
                line[x] = qRgba(0, 0, 0, 0);
                continue;
            }
            const QRgb colour = std::cmp_less(*block, colours.size()) ? colours[qsizetype(*block)] : unknown;
            line[x] = shaded(colour, QcMapPixel::shadeOf(pixel));
        }
    }
    return image;
}
