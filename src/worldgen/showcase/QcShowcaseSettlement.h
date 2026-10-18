#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

#include <optional>

// The village station of the showcase (decision E-490), east of the rows of the village area: a
// bed growthGap blocks beyond the first row, which that row's village grows to hold, and gap blocks
// beyond the rows a settlement within one chunk: three beds and a composter, a ripe wheat field
// within reach of the composter, a chest of bread to throw, two homeless villagers that move in, and
// a short rail with a minecart to carry one away. The beds and the composter are points of interest
// of the area (QcShowcaseArea::pointsOfInterest): they count as placed, not as part of a village plan.
namespace QcShowcaseSettlement {

// Blocks between the rows' east edge and the settlement (beyond joining any row).
inline constexpr int gap = 80;
// Blocks between the first row's east edge and the bed that makes it grow.
inline constexpr int growthGap = 20;

// Where the station lies in the village area (local coordinates).
struct Frame
{
    // The width of the rows and the local z of the first (southernmost) row's road.
    int rowsWidth = 0;
    int firstRoad = 0;
    // The settlement's north west corner (chunk aligned in world coordinates).
    int x = 0;
    int z = 0;
};

// The local x of the settlement for rows `rowsWidth` wide in an area whose west edge is at world x
// `west`: chunk aligned.
int settlementX(int rowsWidth, int west);
// The width the area needs to hold the settlement at local x `x` (with a gap beyond it).
int areaWidth(int x);
// The local z of the settlement in an area `depth` deep whose north edge is at world z `north`:
// chunk aligned and beyond the reach of the areas south of it; none when the area is too shallow.
std::optional<int> settlementZ(int depth, int north, int frontGap);

// Builds the station into `area` (whose north west corner on the ground is `origin`); returns its
// points of interest in world coordinates.
QList<QcBlockPos> build(const QcShowcaseContext &context, QcStructure &area, QcBlockPos origin, Frame frame);

} // namespace QcShowcaseSettlement
