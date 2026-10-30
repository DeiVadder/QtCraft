// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/generators/QcBiomeMap.h"

#include <QImage>

class QcContentRegistry;
class QcTerrainGenerator;

/*! \brief Development aid: top-down image of the generated terrain (surface block colour shaded by
    height, water tinted by depth).

    Used to check biome distribution without playing. */
namespace QcMapRenderer {

/*! The square of columns a top-down map shows: \c {radius} blocks around the centre, several
    blocks per pixel. */
using Area = QcBiomeMap::Area;

/*! \brief A vertical cut along the x axis at \c {z}, from \c {centreX - radius} to
    \c {centreX + radius}; \c {scale} pixels per block. \qcfields */
struct Cut
{
    int centreX = 0;
    int z = 0;
    int radius = 0;
    int scale = 1;
};

/*! Returns the top-down map of \a area: one pixel per cell, the colour of the surface block
    (from \a content) of the cell's first column as \a generator makes it, shaded by height, water
    tinted by depth. */
QImage render(const QcTerrainGenerator &generator, const QcContentRegistry &content, Area area);
/*! Returns the vertical \a cut through the chunks \a generator makes, at heights 0 to 143, in the
    block colours of \a content (air shows the sky). Shows ores, caves and layers. */
QImage renderSlice(const QcTerrainGenerator &generator, const QcContentRegistry &content, Cut cut);
/*! Returns the top-down map of \a area as \a generator makes it, with one colour per biome
    instead of the surface block (ocean stays blue): the cells of QcBiomeMap, as the map preview of
    a new world reads them (decision E-921). */
QImage renderBiomes(const QcTerrainGenerator &generator, Area area);

} // namespace QcMapRenderer
