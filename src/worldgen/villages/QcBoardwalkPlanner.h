// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillageDraft.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <QList>

#include <optional>
#include <utility>

class QcTerrainGenerator;
struct QcTerrainColumn;

/*! \brief The ground of a village over water (decision E-407), from the terrain noise alone like
    the rest of a village plan: pieces and boardwalks lie at one floor height above the water
    surface, wherever the ground lies between the floor and a few blocks below the water surface,
    with posts every few cells down to the bottom, and stairs lead from a boardwalk's end down to
    dry land. */
namespace QcBoardwalkPlanner {

/*! \brief The end of a boardwalk: its last cell, its direction and its odd width. */
struct End
{
    /*! The x and z of the last cell. \qcshared */
    int x = 0;
    int z = 0;
    /*! The direction the boardwalk runs in, the stairs continue in it. \qcshared */
    int dx = 0;
    int dz = 0;
    /*! The odd width in blocks, centred on the last cell. */
    int width = 3;
};

/*! Returns the height of the floor blocks of pieces and boardwalks: \c {stilts.height} blocks
    above the water surface (the block below the sea level of \a terrain), from the style's
    \a stilts. */
int floorY(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts);
/*! Returns whether a piece or boardwalk can stand over the column at \a x, \a z of
    \a terrain: its ground lies at most at the floor height and at most \c {maxDepth} blocks of
    \a stilts below the water surface, and it is no overhang. */
bool carries(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, int x, int z);
/*! Returns the same as the overload above for a \a column of \a terrain already known, with
    the style's \a stilts. */
bool carries(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, const QcTerrainColumn &column);
/*! Returns the floor height for a piece over the rectangle from \a low to \a high (x, z;
    inclusive corners), sampled at its corners, edge middles and centre; std::nullopt where one of
    them cannot carry it (carries() with \a terrain and \a stilts). */
[[nodiscard]] std::optional<int> floorFor(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts,
                                          std::pair<int, int> low, std::pair<int, int> high);
/*! Returns the boardwalk cell over \a column (x, z): \a block at the floor height of
    \a terrain and \a stilts, the post of \a stilts under it on the grid of every
    \c {spacing} cells in both directions (in world coordinates, so crossing boardwalks share
    it). */
QcVillagePlan::Deck deck(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, std::pair<int, int> column,
                         QcBlockId block);
/*! Returns the stairs from the boardwalk's \a end straight on down to dry land, one block lower
    per cell and as wide as the end, the stair block of \a stilts turned in \a content to face
    the boardwalk (up), with posts under them; at most \c {stilts.height} rows. Empty where the
    ground of \a terrain beyond the end is water or at most a block below the floor (a step up
    from there reaches the boardwalk). */
QList<QcVillagePlan::Deck> stairs(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyle::Stilts &stilts,
                                  const End &end);

/*! Returns, over water, the number of samples (every \a step blocks within \a reach of
    \a centre in both directions, from \a terrain) whose ground lies under water, when at least
    60 % of them carry the stilts of \a style and at least the style's \c {stilts.water} share
    are water; std::nullopt otherwise, and where a sample lies near a river (decision E-841) or
    in a delta biome (decision E-850). */
[[nodiscard]] std::optional<int> wetAround(const QcTerrainGenerator &terrain, const QcVillageStyle &style, std::pair<int, int> centre,
                                           int reach, int step);
/*! Returns the stairs from the end of the laid boardwalk \a road of \a draft down to dry land
    (stairs() over \a terrain, turned in \a content), with the land access the wetland keeps dry
    (QcVillageDraft::addStairs); std::nullopt where there are no stairs or they leave the region
    or meet pieces or roads. */
[[nodiscard]] std::optional<QcVillageDraft::Stairs> stairsAt(const QcTerrainGenerator &terrain, const QcContentRegistry &content,
                                                             const QcVillageDraft &draft, const QcVillageDraft::Road &road);

} // namespace QcBoardwalkPlanner
