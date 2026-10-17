#include "worldgen/villages/QcVillageDraft.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"

#include <algorithm>

int QcVillageDraft::turnsFacing(int fx, int fz)
{
    if (fz > 0) {
        return 0;
    }
    if (fx < 0) {
        return 1;
    }
    return fz < 0 ? 2 : 3;
}

QcVillageDraft::Rect QcVillageDraft::brush(std::pair<int, int> cell, int dx, int dz, bool square)
{
    const auto [x, z] = cell;
    if (square) {
        return {x - roadHalfWidth, z - roadHalfWidth, x + roadHalfWidth, z + roadHalfWidth};
    }
    // Across an axis run only.
    return {x - roadHalfWidth * std::abs(dz), z - roadHalfWidth * std::abs(dx), x + roadHalfWidth * std::abs(dz), z + roadHalfWidth * std::abs(dx)};
}

double QcVillageDraft::random()
{
    return qcPositionRandom(seeds.layout, region.x, nextRandom++, region.z);
}

double QcVillageDraft::variantRandom()
{
    return qcPositionRandom(seeds.variant, region.x, nextVariant++, region.z);
}

double QcVillageDraft::cropRandom()
{
    return qcPositionRandom(seeds.crop, region.x, nextCrop++, region.z);
}

bool QcVillageDraft::insideRadius(Rect rect) const
{
    const int reach = region.radius;
    return rect.x0 >= centreX - reach && rect.x1 <= centreX + reach && rect.z0 >= centreZ - reach && rect.z1 <= centreZ + reach;
}

bool QcVillageDraft::touchesRoad(Rect rect, int gap) const
{
    for (int z = rect.z0 - gap; z <= rect.z1 + gap; ++z) {
        for (int x = rect.x0 - gap; x <= rect.x1 + gap; ++x) {
            if (roadCells.contains(cellKey(x, z))) {
                return true;
            }
        }
    }
    return false;
}

void QcVillageDraft::markRoad(Rect rect)
{
    for (int z = rect.z0; z <= rect.z1; ++z) {
        for (int x = rect.x0; x <= rect.x1; ++x) {
            roadCells.insert(cellKey(x, z));
        }
    }
    roads.append(rect);
}

void QcVillageDraft::addRoadCell(const QcTerrainGenerator &terrain, std::pair<int, int> cell, bool branch, std::optional<int> level)
{
    const auto [x, z] = cell;
    const bool underPiece = std::ranges::any_of(pieces, [&](Rect piece) { return piece.intersects({x, z, x, z}, 0); });
    if (underPiece || roadCells.contains(cellKey(x, z))) {
        return;
    }

    roadCells.insert(cellKey(x, z));
    if (!style->overWater) {
        const int ground = terrain.column(x, z).height - 1;
        const int y = level && std::abs(*level - ground) <= 1 ? *level : ground;
        (branch ? plan.branchRoads : plan.roads).append({x, y, z});
    } else {
        const QcBlockId block = branch ? plan.branchPathBlock : plan.pathBlock;
        plan.decks.append(QcBoardwalkPlanner::deck(terrain, style->stilts, {x, z}, block));
    }
}
