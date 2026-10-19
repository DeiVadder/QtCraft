#include "clientcore/texturepacks/QcTextureSources.h"

#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureSynthesizer.h"

#include <QBuffer>
#include <QImageReader>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcTextureSources {

namespace {

// Content PNGs (mods) larger than this are refused before decoding (E-300 keeps the old limit).
constexpr int maxContentPngSide = 128;

[[nodiscard]] QcResult<QImage> contentPng(const QcTextureDefinition &definition)
{
    // Read the size from the header first: a tiny file can describe a huge image.
    QBuffer buffer;
    buffer.setData(definition.png);
    QImageReader reader(&buffer, "PNG");
    const QSize size = reader.size();
    if (!size.isValid()) {
        return qcError(u"%1: PNG cannot be decoded"_s.arg(definition.name));
    }
    if (size.width() > maxContentPngSide || size.height() > maxContentPngSide) {
        return qcError(u"%1: PNG must be at most %2 x %2 pixels"_s.arg(definition.name).arg(maxContentPngSide));
    }

    QImage image;
    if (!reader.read(&image)) {
        return qcError(u"%1: PNG cannot be decoded"_s.arg(definition.name));
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

// What one pack has for the texture, at the pack's resolution.
// Specs are painted at the pack's resolution, but never larger than the atlas tile.
[[nodiscard]] std::optional<QImage> fromPack(const QcTexturePack &pack, const QcTextureDefinition &definition, int tileSize)
{
    const auto image = pack.images.constFind(definition.name);
    if (image != pack.images.constEnd()) {
        return *image;
    }

    const auto spec = pack.specs.constFind(definition.name);
    const QJsonObject *procedural = spec != pack.specs.constEnd() ? &*spec : nullptr;
    if (!procedural && pack.manifest.procedural && definition.png.isEmpty()) {
        procedural = &definition.procedural;
    }
    if (!procedural) {
        return std::nullopt;
    }

    const QcResult<QImage> rendered = QcProceduralTexture::render(*procedural, std::min(pack.manifest.resolution, tileSize));
    return rendered ? std::optional<QImage>(*rendered) : std::nullopt;
}

} // namespace

int resolutionOf(const QcTexturePackStack &packs, int maxResolution)
{
    int resolution = QcTextureSynthesizer::tileSize;
    for (const QcTexturePackRef &pack : packs) {
        resolution = std::max(resolution, pack->manifest.resolution);
    }
    return std::min(resolution, std::max(maxResolution, int(QcTextureSynthesizer::tileSize)));
}

QcResult<Resolved> resolve(const QcTextureDefinition &definition, const QcTexturePackStack &packs, int resolution)
{
    for (const QcTexturePackRef &pack : packs) {
        if (const std::optional<QImage> image = fromPack(*pack, definition, resolution)) {
            return Resolved{toResolution(*image, resolution), pack->id};
        }
    }

    const QcResult<QImage> base = definition.png.isEmpty() ? QcProceduralTexture::render(definition.procedural, QcTextureSynthesizer::tileSize)
                                                           : contentPng(definition);
    if (!base) {
        return qcError(definition.png.isEmpty() ? u"%1: %2"_s.arg(definition.name, base.error()) : base.error());
    }
    // Content PNGs of any size are scaled pixel by pixel, as before texture packs existed.
    return Resolved{base->scaled(resolution, resolution, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888),
                    {}};
}

QImage toResolution(const QImage &image, int resolution)
{
    if (image.width() == resolution && image.height() == resolution) {
        return image.convertToFormat(QImage::Format_RGBA8888);
    }

    const bool enlarging = image.width() <= resolution && image.height() <= resolution;
    return image.scaled(resolution, resolution, Qt::IgnoreAspectRatio, enlarging ? Qt::FastTransformation : Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBA8888);
}

} // namespace QcTextureSources
