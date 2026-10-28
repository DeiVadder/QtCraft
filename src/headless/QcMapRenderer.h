#pragma once

#include "worldgen/generators/QcBiomeMap.h"

#include <QImage>

class QcContentRegistry;
class QcTerrainGenerator;

// Development aid: top-down image of the generated terrain (surface block colour shaded by height,
// water tinted by depth). Used to check biome distribution without playing.
namespace QcMapRenderer {

// The square of columns a top-down map shows: `radius` blocks around the centre, several blocks
// per pixel.
using Area = QcBiomeMap::Area;

// A vertical cut along the x axis at `z`, from `centreX - radius` to `centreX + radius`; `scale`
// pixels per block.
struct Cut
{
    int centreX = 0;
    int z = 0;
    int radius = 0;
    int scale = 1;
};

QImage render(const QcTerrainGenerator &generator, const QcContentRegistry &content, Area area);
// Vertical cut through the generated chunks at heights 0 to 143. Shows ores, caves and layers.
QImage renderSlice(const QcTerrainGenerator &generator, const QcContentRegistry &content, Cut cut);
// Same view with one colour per biome instead of the surface block (ocean stays blue): the cells of
// QcBiomeMap, as the map preview of a new world reads them (decision E-921).
QImage renderBiomes(const QcTerrainGenerator &generator, Area area);

} // namespace QcMapRenderer
