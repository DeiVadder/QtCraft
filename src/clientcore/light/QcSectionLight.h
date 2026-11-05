// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/light/QcLightNibbles.h"

#include <QByteArray>
#include <QtGlobal>

#include <memory>

/*! \brief Block and sky light of the cells of one meshed section, as the mesher saw them, for
    shaders that read light per cell (decision E-390; today's shaders read the light baked into the
    vertices).

    A channel whose cells all have the same level holds no cells, only that level (decision
    E-389): most sections are dark or fully lit by the sky. Levels are raw (0..15, no time of
    day). */
struct QcSectionLight
{
    /*! Block light and sky light of the cells; null when every cell has \c {uniformBlock} or
        \c {uniformSky}. \qcshared */
    std::shared_ptr<const QcLightNibbles> block;
    std::shared_ptr<const QcLightNibbles> sky;
    /*! Block light level of every cell when \c {block} is null. */
    quint8 uniformBlock = 0;
    /*! Sky light level of every cell when \c {sky} is null. */
    quint8 uniformSky = 0;

    /*! Bytes per cell of texels(): block light, sky light. */
    static constexpr int texelBytes = 2;

    /*! Returns whether both channels are uniform. */
    bool isUniform() const { return !block && !sky; }
    /*! Returns the block light level of the cell \a index (QcChunkSection::index). */
    int blockLevel(int index) const { return block ? block->level(index) : uniformBlock; }
    /*! Returns the sky light level of the cell \a index (QcChunkSection::index). */
    int skyLevel(int index) const { return sky ? sky->level(index) : uniformSky; }

    /*! Returns the cells as RG8 texels of a 16 x 16 x 16 volume (R block light, G sky light,
        level × 17 so 15 reads as 1.0), cells in the order of QcChunkSection::index: x fastest,
        then z, then y, which is a 3D texture of width x, height z and depth y. */
    QByteArray texels() const;
    /*! Returns the light of the channels \a block and \a sky; keeps the nibbles of a channel
        only when its cells differ. */
    static QcSectionLight fromChannels(std::shared_ptr<const QcLightNibbles> block, std::shared_ptr<const QcLightNibbles> sky);
};
