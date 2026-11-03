// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/materials/QcMaterialParams.h"

#include <QColor>
#include <QImage>

/*! \brief Paints the material maps of one texture from its colour picture (decision E-908).

    The maps use the labPBR encoding the texture packs use (docs/Texturpakete.md):

    \list
    \li normal map: red and green the tangent-space normal's x (right) and y (up in the picture)
        mapped from -1..1 to 0..255, blue the material's ambient occlusion (255: none), alpha the
        height;
    \li specular map: red the perceptual smoothness, green F0 * 255 (0..229) or a metal code
        (230..237, 255), blue the porosity (0..64), alpha the emission. Unlike labPBR (0..254, 255
        none) the atlas keeps the emission as 0 none .. 255 full, so filtering and mip levels can
        average it; packEmission() converts a pack's map.
    \endlist
    The relief follows the brightness of the colour (darker is lower: mortar, cracks, grain), its
    neighbours wrap around the tile's edges, so a map tiles as its texture does. Deterministic:
    the same picture and material give the same maps on every platform (no fused multiply-add). */
namespace QcMaterialSynthesizer {

/*! \brief The normal and specular maps of one tile, or one mip level of both material atlases
    (QcMaterialMips::halve). */
struct Maps
{
    /*! The normal map. */
    QImage normal;
    /*! The specular map. */
    QImage specular;
};

/*! \brief The colours of the definition that tell an inclusion (the ore) from its ground (the
    stone). */
struct Colours
{
    /*! The ground's colour (the stone). */
    QColor primary;
    /*! The inclusion's colour (the ore). */
    QColor secondary;
};

/*! Returns the normal and specular maps of the RGBA picture \a colour (any square size) with
    \a params, telling inclusions by \a colours; transparent texels get a flat normal. */
[[nodiscard]] Maps paint(const QImage &colour, const QcMaterialParams &params, const Colours &colours);

/*! Returns the flat maps (QcMaterialParams::flat) of a \a size x \a size tile: textures without
    maps. */
[[nodiscard]] Maps flat(int size);

/*! Returns the specular texel of \a params with \a emission, for tests and the flat maps. */
[[nodiscard]] QColor specularTexel(const QcMaterialParams &params, float emission);
/*! Returns the pack's labPBR specular map \a specular in the atlas's encoding: emission 255
    (none) becomes 0. */
[[nodiscard]] QImage packEmission(const QImage &specular);
/*! The texel of a flat normal map: no tilt, no occlusion, full height. */
inline constexpr QRgb flatNormal = 0xFF8080FFu; // qRgba(128, 128, 255, 255) as ARGB

} // namespace QcMaterialSynthesizer
