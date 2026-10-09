#pragma once

#include <QImage>

class QcContentRegistry;
class QcTerrainGenerator;

// Development aid: top-down image of the generated terrain (surface block colour shaded by height,
// water tinted by depth). Used to check biome distribution without playing.
namespace QcMapRenderer {

QImage render(const QcTerrainGenerator &generator, const QcContentRegistry &content, int centreX, int centreZ,
              int radius, int blocksPerPixel);
// Vertical cut through the generated chunks along the x axis at `z`, from `centreX - radius` to
// `centreX + radius` and heights 0 to 143; `scale` pixels per block. Shows ores, caves and
// layers.
QImage renderSlice(const QcTerrainGenerator &generator, const QcContentRegistry &content, int centreX, int z, int radius, int scale);
// Same view with one colour per biome instead of the surface block (ocean stays blue).
QImage renderBiomes(const QcTerrainGenerator &generator, int centreX, int centreZ, int radius, int blocksPerPixel);

} // namespace QcMapRenderer
