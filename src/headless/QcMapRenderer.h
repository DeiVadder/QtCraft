#pragma once

#include <QImage>

class QcContentRegistry;
class QcTerrainGenerator;

// Development aid: top-down image of the generated terrain (surface block colour shaded by height,
// water tinted by depth). Used to check biome distribution without playing.
namespace QcMapRenderer {

QImage render(const QcTerrainGenerator &generator, const QcContentRegistry &content, int centreX, int centreZ,
              int radius, int blocksPerPixel);

} // namespace QcMapRenderer
