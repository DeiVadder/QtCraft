#pragma once

#include "common/maps/QcMapData.h"

#include <QImage>
#include <QList>
#include <QRgb>

class QcContentRegistry;
class QcTextureAtlas;

// The colours maps are drawn with (decision E-801): every block definition shows the average
// colour of its top face's texture as the world is drawn, so texture packs colour maps too. Worked
// out once when the atlas is (re)built, not per frame; the map image is a pure function of a map's
// pixels and these colours.
namespace QcMapColors {

// A block without a texture (or a pixel naming a block the client does not know).
inline constexpr QRgb unknown = qRgb(128, 128, 128);

// The average colour of `texture`, each texel weighted by its alpha (glass, water and leaves show
// their visible part); `unknown` for a null or fully transparent image. The result is opaque.
[[nodiscard]] QRgb average(const QImage &texture);

// The colour of each block definition (index as in QcContentRegistry::blocks() and the map pixels):
// the average of the texture of its top face (the north face for blocks without one, like crossed
// plants) in `atlas`; `unknown` where the atlas lacks the texture.
[[nodiscard]] QList<QRgb> ofAtlas(const QcContentRegistry &content, const QcTextureAtlas &atlas);

// The map as an image of QcMapData::size squared pixels: blank pixels transparent (the paper shows
// through), the others the colour of their block times the brightness of their shade
// (QcMapPixel::brightness); blocks beyond `colours` are `unknown`.
[[nodiscard]] QImage render(const QcMapData &map, const QList<QRgb> &colours);

} // namespace QcMapColors
