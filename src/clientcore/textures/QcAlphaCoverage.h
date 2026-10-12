#pragma once

#include "QcAtlasLayout.h"

#include <QImage>
#include <QList>

// Keeps cut-out textures (leaves, plants, torches) the same density in every mip level (decision
// E-300). Averaging alpha makes thin shapes fade below the shader's cut-off (they vanish in the
// distance). Per tile and level, the alpha is raised so that the share of texels the shader draws
// comes back to that of level 0 (coverage scaling as in Castaño's "Computing alpha mipmaps", only
// upwards, so holes filled by averaging stay: leaves get a little denser). Translucent tiles
// (glass, water) are left alone.
namespace QcAlphaCoverage {

// The shader discards texels with alpha below 0.1.
inline constexpr int cutoff = 26;

// Per tile: the share of texels at or above the cut-off for cut-out tiles (alpha only 0 or 255,
// with at least one transparent texel), -1 for all others.
QList<double> measure(const QImage &level, QcAtlasLayout layout, qsizetype tiles, int tileSize);

// Raises the alpha of every cut-out tile's cell (gutter included) in `level`, whose tiles are
// `tileSize` pixels, until its share of drawn texels reaches `coverage`.
void keep(QImage &level, QcAtlasLayout layout, const QList<double> &coverage, int tileSize);

} // namespace QcAlphaCoverage
