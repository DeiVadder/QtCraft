// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcBlockRenderTable.h"

#include <QImage>

#include <array>

/*! \brief Inventory icon of a block state: its boxes drawn from above at an angle (top, east and
    south faces), textured from the atlas and shaded like the world, so stairs, slabs and fences
    look different from their base block. */
namespace QcBlockIcon {

/*! Edge length of an icon in pixels for textures of 16 x 16. */
inline constexpr int iconSize = 48;
/*! Icons of texture packs above 16 x 16 (decision E-300). */
inline constexpr int largeIconSize = 64;
/*! Atlas tiles the icons are drawn from at most: enough for a face of a large icon. */
inline constexpr int sourceTileSize = 32;

/*! Returns \a atlas darkened by the face shading of the world, per QcBlockFace; made once for
    all icons. */
std::array<QImage, 6> shadedAtlases(const QImage &atlas);

/*! Returns the icon of the block state \a entry, \a size pixels across, drawn from \a shadedAtlas
    (shadedAtlases()). */
QImage render(const QcBlockRenderTable::Entry &entry, const std::array<QImage, 6> &shadedAtlas, int size = iconSize);

/*! \brief One cell of a block that fills several (a bed's foot and head, decision E-885): its
    state's entry, drawn \c {offset} cells away from the first part. */
struct Part
{
    /*! Render data of the part's state; null draws nothing. */
    const QcBlockRenderTable::Entry *entry = nullptr;
    /*! Cells from the first part along x, y and z. */
    std::array<int, 3> offset{};
};

/*! Returns the icon of a block of several cells, \a size pixels across, drawn from
    \a shadedAtlas: all \a parts together, scaled evenly so that the bounds of their geometry
    take as much of the icon as a full block, centred there. A part without an entry draws
    nothing; a single part draws like render(entry). */
QImage render(const QList<Part> &parts, const std::array<QImage, 6> &shadedAtlas, int size = iconSize);

/*! \brief A quad of any size and place with the colour its texels are multiplied with (RGBA, 255
    keeps a channel): a face of a creature's model (QcMobIcon). */
struct FreeQuad
{
    /*! The quad: its corners and their atlas coordinates. */
    QcBlockRenderTable::Quad quad;
    /*! Colour the quad's texels are multiplied with. */
    std::array<quint8, 4> tint{255, 255, 255, 255};
};

/*! Returns the icon of the free \a quads, \a size pixels across, drawn from \a shadedAtlas:
    scaled evenly so that the bounds of their geometry take as much of the icon as a full block,
    centred there. */
QImage renderFitted(QList<FreeQuad> quads, const std::array<QImage, 6> &shadedAtlas, int size = iconSize);

} // namespace QcBlockIcon
