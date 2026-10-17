#pragma once

#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <QList>

#include <optional>

class QcTerrainGenerator;

// The ground of a village over water (decision E-407), from the terrain noise alone like the rest of
// a village plan: pieces and boardwalks lie at one floor height above the water surface, wherever
// the ground lies between the floor and a few blocks below the water surface, with posts every few
// cells down to the bottom, and stairs lead from a boardwalk's end down to dry land.
namespace QcBoardwalkPlanner {

// The end of a boardwalk: its last cell, its direction and its odd width.
struct End
{
    int x = 0;
    int z = 0;
    int dx = 0;
    int dz = 0;
    int width = 3;
};

// The height of the floor blocks of pieces and boardwalks.
int floorY(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts);
// Whether a piece or boardwalk can stand over the column: its ground lies at most at the floor
// height and at most `maxDepth` blocks below the water surface, and it is no overhang.
bool carries(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, int x, int z);
// The floor height for a piece over the rectangle (inclusive corners), sampled at its corners, edge
// middles and centre; std::nullopt where one of them cannot carry it.
[[nodiscard]] std::optional<int> floorFor(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts,
                                          std::pair<int, int> low, std::pair<int, int> high);
// The boardwalk cell over a column: `block` at the floor height, a post under it on the grid of
// every `spacing` cells in both directions (in world coordinates, so crossing boardwalks share it).
QcVillagePlan::Deck deck(const QcTerrainGenerator &terrain, const QcVillageStyle::Stilts &stilts, std::pair<int, int> column,
                         QcBlockId block);
// The stairs from a boardwalk's end straight on down to dry land, one block per cell, each facing
// the boardwalk (up), with posts under them; empty where the ground beyond the end is water or at
// most a block below the floor (a step up from there reaches the boardwalk).
QList<QcVillagePlan::Deck> stairs(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyle::Stilts &stilts,
                                  const End &end);

} // namespace QcBoardwalkPlanner
