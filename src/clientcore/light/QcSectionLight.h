#pragma once

#include "QcLightNibbles.h"

#include <QByteArray>
#include <QtGlobal>

#include <memory>

// Block and sky light of the cells of one meshed section, as the mesher saw them, for shaders that
// read light per cell (decision E-390; today's shaders read the light baked into the vertices). A
// channel whose cells all have the same level holds no cells, only that level (decision E-389):
// most sections are dark or fully lit by the sky. Levels are raw (0..15, no time of day).
struct QcSectionLight
{
    // Null when every cell has `uniformBlock` / `uniformSky`.
    std::shared_ptr<const QcLightNibbles> block;
    std::shared_ptr<const QcLightNibbles> sky;
    quint8 uniformBlock = 0;
    quint8 uniformSky = 0;

    // Bytes per cell of texels(): block light, sky light.
    static constexpr int texelBytes = 2;

    bool isUniform() const { return !block && !sky; }
    int blockLevel(int index) const { return block ? block->level(index) : uniformBlock; }
    int skyLevel(int index) const { return sky ? sky->level(index) : uniformSky; }

    // The cells as RG8 texels of a 16 x 16 x 16 volume (R block light, G sky light, level × 17 so
    // 15 reads as 1.0), cells in the order of QcChunkSection::index: x fastest, then z, then y, which
    // is a 3D texture of width x, height z and depth y.
    QByteArray texels() const;
    // Keeps the nibbles of a channel only when its cells differ.
    static QcSectionLight fromChannels(std::shared_ptr<const QcLightNibbles> block, std::shared_ptr<const QcLightNibbles> sky);
};
