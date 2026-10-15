#pragma once

#include "QcTextureDefinition.h"
#include "QcTexturePack.h"

#include <QImage>
#include <QString>

#include <optional>

// Where each texture of the atlas comes from (decision E-297): the active packs in priority order,
// then the content's own definition (the base 16 x 16 textures and those of mods). Within one pack
// a PNG image comes before a procedural spec of the same name, and a pack with "procedural": true
// finally renders the content's procedural definition at its resolution.
namespace QcTextureSources {

struct Resolved
{
    QImage image;
    // The id of the pack that gave the texture, "" for the content itself.
    QString pack;
};

// The highest resolution of the packs, at least 16 and at most `maxResolution`.
int resolutionOf(const QcTexturePackStack &packs, int maxResolution);

// The texture of `definition` at `resolution`; unset with `problem` set when even the content's own
// definition cannot be drawn.
[[nodiscard]] std::optional<Resolved> resolve(const QcTextureDefinition &definition, const QcTexturePackStack &packs, int resolution, QString &problem);

// `image` scaled to `resolution` x `resolution`: pixel-exact when enlarging, averaged when shrinking.
QImage toResolution(const QImage &image, int resolution);

} // namespace QcTextureSources
