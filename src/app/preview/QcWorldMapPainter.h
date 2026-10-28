#pragma once

#include "worldgen/generators/QcBiomeMap.h"

#include <QImage>
#include <QList>

class QcContentRegistry;

// Draws the cells of a QcBiomeMap for the map preview of a new world (decision E-921), one pixel per
// cell: land in its biome's colour (`mapColour` of the content, else hues spread over the biomes),
// lit from the north-west by the slope to the neighbouring cell and a little brighter with height;
// the sea and lakes blue, darker with depth, or ice where their top layer freezes; rivers in their river biome's colour, lightened. Pure
// functions: the preview's worker thread calls them.
namespace QcWorldMapPainter {

// The colour of every biome of `content` (index: biome), 0xRRGGBB.
[[nodiscard]] QList<quint32> biomeColours(const QcContentRegistry &content);
// The colour of the sea in the legend (and of shallow water on the map), 0xRRGGBB.
[[nodiscard]] quint32 seaColour();
// The colour of frozen sea and lakes, 0xRRGGBB.
[[nodiscard]] quint32 iceColour();
[[nodiscard]] QImage paint(const QcBiomeMap &map, const QList<quint32> &colours, int seaLevel);

} // namespace QcWorldMapPainter
