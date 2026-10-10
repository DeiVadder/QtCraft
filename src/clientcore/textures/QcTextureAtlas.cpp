#include "QcTextureAtlas.h"

#include "QcTextureEmblem.h"
#include "QcTextureSynthesizer.h"

#include <QBuffer>
#include <QImageReader>
#include <QPainter>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int tileSize = QcTextureSynthesizer::tileSize;
// Larger PNGs are refused before decoding; they would be scaled down to one tile anyway.
constexpr int maxPngSide = 128;

// Accepts #RRGGBB and #RRGGBBAA.
std::optional<QColor> parseColor(const QJsonValue &value)
{
    const QString text = value.toString();
    if (!text.startsWith(u'#') || (text.size() != 7 && text.size() != 9)) {
        return std::nullopt;
    }

    bool ok = false;
    const uint rgb = QStringView(text).mid(1, 6).toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }

    int alpha = 255;
    if (text.size() == 9) {
        alpha = int(QStringView(text).mid(7, 2).toUInt(&ok, 16));
        if (!ok) {
            return std::nullopt;
        }
    }
    return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF), alpha);
}

QImage missingTile()
{
    QImage image(tileSize, tileSize, QImage::Format_RGBA8888);
    for (int y = 0; y < tileSize; ++y) {
        for (int x = 0; x < tileSize; ++x) {
            image.setPixelColor(x, y, ((x / 8) + (y / 8)) % 2 == 0 ? QColor(255, 0, 255) : QColor(0, 0, 0));
        }
    }
    return image;
}

std::optional<QImage> renderTexture(const QcTextureDefinition &definition, QString &problem)
{
    if (!definition.png.isEmpty()) {
        // Read the size from the header first: a tiny file can describe a huge image.
        QBuffer buffer;
        buffer.setData(definition.png);
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (!size.isValid()) {
            problem = u"%1: PNG cannot be decoded"_s.arg(definition.name);
            return std::nullopt;
        }
        if (size.width() > maxPngSide || size.height() > maxPngSide) {
            problem = u"%1: PNG must be at most %2 x %2 pixels"_s.arg(definition.name).arg(maxPngSide);
            return std::nullopt;
        }

        QImage image;
        if (!reader.read(&image)) {
            problem = u"%1: PNG cannot be decoded"_s.arg(definition.name);
            return std::nullopt;
        }
        return image.scaled(tileSize, tileSize, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .convertToFormat(QImage::Format_RGBA8888);
    }

    const QJsonObject &json = definition.procedural;
    const std::optional<QcTextureSpec::Pattern> pattern = qcTexturePatternFromName(json.value(u"pattern"_s).toString());
    const std::optional<QColor> primary = parseColor(json.value(u"primary"_s));
    const std::optional<QColor> secondary =
        json.contains(u"secondary"_s) ? parseColor(json.value(u"secondary"_s)) : primary;
    if (!pattern || !primary || !secondary) {
        problem = u"%1: needs a known 'pattern' and colours '#RRGGBB[AA]'"_s.arg(definition.name);
        return std::nullopt;
    }

    QcTextureSpec spec;
    spec.pattern = *pattern;
    spec.primary = *primary;
    spec.secondary = *secondary;
    spec.variation = std::clamp(json.value(u"variation"_s).toDouble(0.1), 0.0, 1.0);
    spec.seed = quint32(json.value(u"seed"_s).toInteger(0));
    QString emblemProblem;
    if (spec.pattern == QcTextureSpec::Pattern::Emblem && !QcTextureEmblem::read(json, spec, emblemProblem)) {
        problem = u"%1: %2"_s.arg(definition.name, emblemProblem);
        return std::nullopt;
    }
    return QcTextureSynthesizer::synthesize(spec);
}

} // namespace

QcTextureAtlas QcTextureAtlas::build(const QcContentRegistry &content)
{
    QcTextureAtlas atlas;
    QList<std::pair<QString, QImage>> tiles;
    tiles.append({QString::fromLatin1(missingTextureName), missingTile()});

    for (const QcTextureDefinition &definition : content.textures()) {
        QString problem;
        const std::optional<QImage> image = renderTexture(definition, problem);
        if (!image) {
            atlas.m_problems.append(problem);
            continue;
        }
        tiles.append({definition.name, *image});
    }

    int tilesPerRow = 1;
    while (qsizetype(tilesPerRow) * tilesPerRow < tiles.size()) {
        tilesPerRow *= 2;
    }

    const int extent = tilesPerRow * tileSize;
    atlas.m_image = QImage(extent, extent, QImage::Format_RGBA8888);
    atlas.m_image.fill(Qt::transparent);
    QPainter painter(&atlas.m_image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    // A tiny inset keeps nearest sampling at tile borders from picking the neighbouring tile.
    const double inset = 0.01 / extent;
    for (qsizetype i = 0; i < tiles.size(); ++i) {
        const int column = int(i % tilesPerRow);
        const int row = int(i / tilesPerRow);
        painter.drawImage(column * tileSize, row * tileSize, tiles[i].second);
        const double span = 1.0 / tilesPerRow;
        atlas.m_uvs.insert(tiles[i].first,
                           QRectF(column * span + inset, row * span + inset, span - 2 * inset, span - 2 * inset));
    }
    painter.end();

    for (const QcBlockDefinition &block : content.blocks()) {
        for (const QString &texture : block.faceTextures) {
            if (!texture.isEmpty() && !atlas.m_uvs.contains(texture)) {
                atlas.m_problems.append(u"Block %1 uses unknown texture %2"_s.arg(block.name, texture));
            }
        }
    }
    atlas.m_problems.removeDuplicates();
    return atlas;
}

QRectF QcTextureAtlas::uv(const QString &textureName) const
{
    const auto it = m_uvs.constFind(textureName);
    return it != m_uvs.constEnd() ? *it : m_uvs.value(QString::fromLatin1(missingTextureName));
}

QImage QcTextureAtlas::tile(const QString &textureName) const
{
    const QRectF rect = uv(textureName);
    const int x = int(std::floor(rect.x() * m_image.width()));
    const int y = int(std::floor(rect.y() * m_image.height()));
    return m_image.copy(x, y, tileSize, tileSize);
}
