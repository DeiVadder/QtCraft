// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <array>

/*! \brief Vertex of creature, player, item and hand meshes: like QcBlockVertex plus a tint
    colour that the texture is multiplied with (outfit colours, the red flash of a hit). */
struct QcEntityVertex
{
    /*! Position along x, relative to the mesh's origin. */
    float x = 0.0f;
    /*! Position along y, relative to the mesh's origin. */
    float y = 0.0f;
    /*! Position along z, relative to the mesh's origin. */
    float z = 0.0f;
    /*! Atlas coordinate u. */
    float u = 0.0f;
    /*! Atlas coordinate v. */
    float v = 0.0f;
    /*! Face shading in [0, 1]. */
    float shade = 1.0f;
    /*! Sky light in [0, 1]. */
    float skyLight = 1.0f;
    /*! Block light (torches) in [0, 1]. */
    float blockLight = 0.0f;
    /*! RGBA, 255 = unchanged. */
    std::array<quint8, 4> tint = {255, 255, 255, 255};
};
