// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief Light of an entity: sky light (scaled by daylight in the shader) and block light, both
    0..1. */
struct QcEntityLight
{
    /*! Sky light in [0, 1]. */
    float sky = 1.0f;
    /*! Block light in [0, 1]. */
    float block = 0.0f;

    constexpr QcEntityLight() = default;
    constexpr QcEntityLight(float skyLight, float blockLight = 0.0f)
        : sky(skyLight)
        , block(blockLight)
    {
    }
};
