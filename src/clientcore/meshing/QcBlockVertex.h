// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

/*! \brief What kind of surface a vertex belongs to, for render modes that treat surfaces
    differently (decision E-390: reflections on water, wind in leaves and plants, glow of light
    sources).

    Today's shaders do not read it. Five bits in the vertex (at most 32 values). */
enum class QcSurfaceMaterial : quint8 {
    /*! Opaque blocks. */
    Solid,
    /*! Alpha-tested blocks drawn in the opaque pass (leaves). */
    Cutout,
    /*! Crossed planes (plants, saplings). */
    Plant,
    /*! Translucent blocks other than liquids (glass, ice). */
    Translucent,
    /*! Translucent liquids (water), drawn from their own list (QcChunkMesh). */
    Water,
    /*! Opaque liquids (lava). */
    Liquid,
    /*! Blocks that give off light (torches, lamps). */
    Emissive,
};

/*! \brief Vertex layout shared by the mesher and the GPU pipeline: position (relative to the
    section's corner, QcChunkMesh::origin) and atlas coordinates as floats, shading, light and the
    surface as unsigned bytes (read as one UNormByte4 attribute), 24 bytes per vertex.

    Light levels k / 15 are stored exactly (17 k); other values to within 1 / 510. */
struct QcBlockVertex
{
    /*! Position along x, relative to the section's corner. */
    float x = 0.0f;
    /*! Position along y, relative to the section's corner. */
    float y = 0.0f;
    /*! Position along z, relative to the section's corner. */
    float z = 0.0f;
    /*! Atlas coordinate u. */
    float u = 0.0f;
    /*! Atlas coordinate v. */
    float v = 0.0f;
    /*! Face shading and ambient occlusion, sky light (scaled by the time of day in the shader),
        block light (torches, independent of the time of day) and the surface: the normal in the
        low three bits (0: none, crossed planes; 1 + QcBlockFace: the face direction) and the
        QcSurfaceMaterial in the high five (decision E-390; today's shaders ignore it). */
    std::array<quint8, 4> light = {255, 255, 0, 0};

    /*! Byte of the face shading and ambient occlusion in \c {light}. */
    static constexpr std::size_t shadeIndex = 0;
    /*! Byte of the sky light in \c {light}. */
    static constexpr std::size_t skyLightIndex = 1;
    /*! Byte of the block light in \c {light}. */
    static constexpr std::size_t blockLightIndex = 2;
    /*! Byte of the surface (normal and material) in \c {light}. */
    static constexpr std::size_t surfaceIndex = 3;

    /*! Returns \a value clamped to [0, 1] as a byte (0..255). */
    static quint8 pack(float value) { return static_cast<quint8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); }
    /*! Returns byte \a value as a float in [0, 1]. */
    static float unpack(quint8 value) { return static_cast<float>(value) / 255.0f; }

    /*! Returns the shading in [0, 1]. */
    float shade() const { return unpack(light[shadeIndex]); }
    /*! Sets the shading to \a value (clamped to [0, 1]). */
    void setShade(float value) { light[shadeIndex] = pack(value); }
    /*! Returns the sky light in [0, 1]. */
    float skyLight() const { return unpack(light[skyLightIndex]); }
    /*! Sets the sky light to \a value (clamped to [0, 1]). */
    void setSkyLight(float value) { light[skyLightIndex] = pack(value); }
    /*! Returns the block light in [0, 1]. */
    float blockLight() const { return unpack(light[blockLightIndex]); }
    /*! Sets the block light to \a value (clamped to [0, 1]). */
    void setBlockLight(float value) { light[blockLightIndex] = pack(value); }

    /*! Returns the face the surface looks towards; nullopt for crossed planes. */
    std::optional<QcBlockFace> normal() const
    {
        const int bits = light[surfaceIndex] & 0x07;
        return bits == 0 ? std::nullopt : std::optional<QcBlockFace>(static_cast<QcBlockFace>(bits - 1));
    }
    /*! Returns the surface class of the vertex. */
    QcSurfaceMaterial material() const { return static_cast<QcSurfaceMaterial>(light[surfaceIndex] >> 3); }
    /*! Sets the surface: the face \a normal looks towards (nullopt for crossed planes) and its \a
        material. */
    void setSurface(std::optional<QcBlockFace> normal, QcSurfaceMaterial material)
    {
        light[surfaceIndex] = static_cast<quint8>((static_cast<quint8>(material) << 3) | (normal ? static_cast<int>(*normal) + 1 : 0));
    }
};
static_assert(sizeof(QcBlockVertex) == 24);
