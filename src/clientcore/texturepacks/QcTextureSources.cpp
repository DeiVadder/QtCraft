// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/texturepacks/QcTextureSources.h"

#include "clientcore/textures/QcProceduralTexture.h"
#include "clientcore/textures/QcTextureSynthesizer.h"

#include <QBuffer>
#include <QImageReader>
#include <QPainter>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcTextureSources {

namespace {

// Content PNGs (mods) wider than this are refused before decoding (E-300 keeps the old limit),
// and so are taller ones unless they are a strip of square frames (decision E-878).
constexpr int maxContentPngSide = 128;

// The frames of a vertical strip of square frames of `size`; 0 for any other shape.
int stripFrames(QSize size)
{
    if (size.width() <= 0 || size.height() <= size.width() || size.height() % size.width() != 0) {
        return 0;
    }
    return size.height() / size.width();
}

// How many frames a content PNG of `size` holds: a strip of square frames (decision E-878), else
// one picture (scaled to a square, as before animations). A strip of more than
// QcTextureAnimation::maxFrames frames is refused by contentPng().
int framesOfPng(QSize size)
{
    const int frames = stripFrames(size);
    return frames > 1 && frames <= QcTextureAnimation::maxFrames ? frames : 1;
}

// Why a content PNG of `size` is refused before decoding, or nullopt: wider than
// maxContentPngSide, taller without being a strip, or a strip of too many frames.
std::optional<QString> pngSizeProblem(const QString &texture, QSize size)
{
    const int frames = stripFrames(size);
    if (frames > QcTextureAnimation::maxFrames) {
        return u"%1: the PNG is a strip of %2 frames, at most %3 animate"_s.arg(texture).arg(frames).arg(QcTextureAnimation::maxFrames);
    }
    if (size.width() > maxContentPngSide || (size.height() > maxContentPngSide && frames == 0)) {
        return u"%1: PNG must be at most %2 x %2 pixels, or a vertical strip of square frames at most %2 pixels wide"_s.arg(texture).arg(
            maxContentPngSide);
    }
    return std::nullopt;
}

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
    if (const std::optional<QString> problem = pngSizeProblem(definition.name, size)) {
        return qcError(*problem);
    }

    QImage image;
    if (!reader.read(&image)) {
        return qcError(u"%1: PNG cannot be decoded"_s.arg(definition.name));
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

// The frame count of a content PNG from its header, without decoding it.
int contentPngFrames(const QcTextureDefinition &definition)
{
    QBuffer buffer;
    buffer.setData(definition.png);
    const QImageReader reader(&buffer, "PNG");
    return framesOfPng(reader.size());
}

// The animation of a strip of `frames` frames: timed as the content's definition says when it has
// an animation itself, else at the default frame time.
QcTextureAnimation stripAnimation(int frames, const QcTextureDefinition &definition)
{
    QcTextureAnimation animation = QcProceduralTexture::animationOf(definition.procedural);
    animation.frames = frames;
    return animation;
}

// The procedural definition a pack paints for `definition`: its own spec, or with "procedural": true
// the content's definition (not a mod's PNG); nullptr for neither.
const QJsonObject *packProcedural(const QcTexturePack &pack, const QcTextureDefinition &definition)
{
    const auto spec = pack.specs.constFind(definition.name);
    if (spec != pack.specs.constEnd()) {
        return &*spec;
    }
    return pack.manifest.procedural && definition.png.isEmpty() ? &definition.procedural : nullptr;
}

// What one pack has for the texture, at the pack's resolution, with every frame.
// Specs are painted at the pack's resolution, but never larger than the atlas tile.
[[nodiscard]] std::optional<Resolved> fromPack(const QcTexturePack &pack, const QcTextureDefinition &definition, int tileSize)
{
    const auto image = pack.images.constFind(definition.name);
    if (image != pack.images.constEnd()) {
        const QList<QImage> later = pack.laterFrames.value(definition.name);
        return Resolved{*image, pack.id, stripAnimation(static_cast<int>(later.size()) + 1, definition), later};
    }

    const QJsonObject *procedural = packProcedural(pack, definition);
    if (!procedural) {
        return std::nullopt;
    }

    QcResult<QList<QImage>> rendered = QcProceduralTexture::renderFrames(*procedural, std::min(pack.manifest.resolution, tileSize));
    if (!rendered || rendered->isEmpty()) {
        return std::nullopt;
    }
    const QImage first = rendered->takeFirst();
    return Resolved{first, pack.id, QcProceduralTexture::animationOf(*procedural), *rendered};
}

// The content's own texture at 16 x 16 (procedural) or at its PNG's size, with every frame.
[[nodiscard]] QcResult<Resolved> fromContent(const QcTextureDefinition &definition)
{
    if (definition.png.isEmpty()) {
        QcResult<QList<QImage>> frames = QcProceduralTexture::renderFrames(definition.procedural, QcTextureSynthesizer::tileSize);
        if (!frames) {
            return qcError(u"%1: %2"_s.arg(definition.name, frames.error()));
        }
        const QImage first = frames->takeFirst();
        return Resolved{first, {}, QcProceduralTexture::animationOf(definition.procedural), *frames};
    }

    const QcResult<QImage> png = contentPng(definition);
    if (!png) {
        return qcError(png.error());
    }
    const int frames = framesOfPng(png->size());
    const int side = png->height() / frames;
    QList<QImage> later;
    for (int frame = 1; frame < frames; ++frame) {
        later.append(png->copy(0, frame * side, png->width(), side));
    }
    return Resolved{png->copy(0, 0, png->width(), side), {}, stripAnimation(frames, definition), later};
}

} // namespace

QImage Resolved::strip() const
{
    QImage strip(image.width(), image.height() * static_cast<int>(laterFrames.size() + 1), QImage::Format_RGBA8888);
    strip.fill(Qt::transparent);
    QPainter painter(&strip);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, image);
    for (qsizetype frame = 0; frame < laterFrames.size(); ++frame) {
        painter.drawImage(0, image.height() * static_cast<int>(frame + 1), laterFrames[frame]);
    }
    painter.end();
    return strip;
}

int resolutionOf(const QcTexturePackStack &packs, int maxResolution)
{
    int resolution = QcTextureSynthesizer::tileSize;
    for (const QcTexturePackRef &pack : packs) {
        resolution = std::max(resolution, pack->manifest.resolution);
    }
    return std::min(resolution, std::max(maxResolution, QcTextureSynthesizer::tileSize));
}

QcResult<Resolved> resolve(const QcTextureDefinition &definition, const QcTexturePackStack &packs, int resolution)
{
    std::optional<Resolved> found;
    for (const QcTexturePackRef &pack : packs) {
        found = fromPack(*pack, definition, resolution);
        if (found) {
            break;
        }
    }
    if (!found) {
        QcResult<Resolved> content = fromContent(definition);
        if (!content) {
            return qcError(content.error());
        }
        found = std::move(*content);
    }

    // Content PNGs of any size and the content's 16 x 16 textures are scaled pixel by pixel, as
    // before texture packs existed; a pack's pictures exactly or averaged (toResolution).
    const bool pixelExact = found->pack.isEmpty();
    const auto scaled = [pixelExact, resolution](const QImage &frame) {
        if (!pixelExact) {
            return toResolution(frame, resolution);
        }
        return frame.scaled(resolution, resolution, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888);
    };
    found->image = scaled(found->image);
    for (QImage &frame : found->laterFrames) {
        frame = scaled(frame);
    }
    return std::move(*found);
}

QcTextureAnimation animationOf(const QcTextureDefinition &definition, const QcTexturePackStack &packs)
{
    for (const QcTexturePackRef &pack : packs) {
        if (pack->images.contains(definition.name)) {
            return stripAnimation(static_cast<int>(pack->laterFrames.value(definition.name).size()) + 1, definition);
        }
        if (const QJsonObject *procedural = packProcedural(*pack, definition)) {
            return QcProceduralTexture::animationOf(*procedural);
        }
    }
    if (definition.png.isEmpty()) {
        return QcProceduralTexture::animationOf(definition.procedural);
    }
    return stripAnimation(contentPngFrames(definition), definition);
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
