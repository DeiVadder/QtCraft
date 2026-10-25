#pragma once

#include "clientcore/meshing/QcBlockRenderTable.h"

#include <QImage>

#include <array>

// Inventory icon of a block state: its boxes drawn from above at an angle (top, east and south
// faces), textured from the atlas and shaded like the world, so stairs, slabs and fences look
// different from their base block.
namespace QcBlockIcon {

inline constexpr int iconSize = 48;
// Icons of texture packs above 16 x 16 (decision E-300).
inline constexpr int largeIconSize = 64;
// Atlas tiles the icons are drawn from at most: enough for a face of a large icon.
inline constexpr int sourceTileSize = 32;

// The atlas darkened by the face shading of the world, per QcBlockFace; made once for all icons.
std::array<QImage, 6> shadedAtlases(const QImage &atlas);

QImage render(const QcBlockRenderTable::Entry &entry, const std::array<QImage, 6> &shadedAtlas, int size = iconSize);

// One cell of a block that fills several (a bed's foot and head, decision E-885): its state's entry,
// drawn `offset` cells away from the first part.
struct Part
{
    const QcBlockRenderTable::Entry *entry = nullptr;
    std::array<int, 3> offset{};
};

// The icon of a block of several cells: all parts together, scaled evenly so that the bounds of
// their geometry take as much of the icon as a full block, centred there. A part without an entry
// draws nothing; a single part draws like render(entry).
QImage render(const QList<Part> &parts, const std::array<QImage, 6> &shadedAtlas, int size = iconSize);

} // namespace QcBlockIcon
