#pragma once

#include "clientcore/materials/QcMaterialParams.h"

#include <QColor>
#include <QImage>

// Paints the material maps of one texture from its colour picture (decision E-908), in the labPBR
// encoding the texture packs use (docs/Texturpakete.md):
// - normal map: red and green the tangent-space normal's x (right) and y (up in the picture) mapped
//   from -1..1 to 0..255, blue the material's ambient occlusion (255: none), alpha the height;
// - specular map: red the perceptual smoothness, green F0 * 255 (0..229) or a metal code
//   (230..237, 255), blue the porosity (0..64), alpha the emission. Unlike labPBR (0..254, 255
//   none) the atlas keeps the emission as 0 none .. 255 full, so filtering and mip levels can
//   average it; packEmission() converts a pack's map.
// The relief follows the brightness of the colour (darker is lower: mortar, cracks, grain), its
// neighbours wrap around the tile's edges, so a map tiles as its texture does. Deterministic: the
// same picture and material give the same maps on every platform (no fused multiply-add).
namespace QcMaterialSynthesizer {

// The maps of one tile.
struct Maps
{
    QImage normal;
    QImage specular;
};

// The colours of the definition that tell an inclusion (the ore) from its ground (the stone).
struct Colours
{
    QColor primary;
    QColor secondary;
};

// Normal and specular maps of the RGBA picture `colour` (any square size) with `params`; transparent
// texels get a flat normal.
[[nodiscard]] Maps paint(const QImage &colour, const QcMaterialParams &params, const Colours &colours);

// The flat maps (QcMaterialParams::flat) of a `size` x `size` tile: textures without maps.
[[nodiscard]] Maps flat(int size);

// The encoded texels of `params` (specular map) and of a flat normal, for tests and the flat maps.
[[nodiscard]] QColor specularTexel(const QcMaterialParams &params, float emission);
// A pack's labPBR specular map in the atlas's encoding: emission 255 (none) becomes 0.
[[nodiscard]] QImage packEmission(const QImage &specular);
inline constexpr QRgb flatNormal = 0xFF8080FFu; // qRgba(128, 128, 255, 255) as ARGB

} // namespace QcMaterialSynthesizer
