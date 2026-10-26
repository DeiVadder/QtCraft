#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <optional>

class QcTerrainGenerator;

// The ground around a structure on the surface blended to its floor (decision E-889, the rule's
// `blend`): within `blend` blocks of the footprint the ground may move from the terrain towards the
// floor by `slope` blocks per block inward from the blend's outer end, like the wetland of a
// village (decision E-660), so the ground right beside the walls lies level with the floor wherever
// the terrain rises less than the freedom there, and returns to the terrain without a cliff.
// Lowered ground loses its plants, raised ground is filled with the ground below; the top block
// is the terrain's surface block. Columns under water keep their ground, and no ground is lowered
// below the sea. A pure function of the placement and the terrain noise per column, the same in any
// order of the chunks. Thread-safe.
class QcSurfaceBlend
{
public:
    // How far the ground may differ from the terrain per block of distance inward from the blend's
    // outer end (the wetland's slope).
    static constexpr double slope = 0.6;

    explicit QcSurfaceBlend(const QcTerrainGenerator &terrain);

    // The top block a column at `distance` blocks from the footprint gets, whose terrain ground's
    // top block is `ground`, around a floor whose top block is `floor`.
    [[nodiscard]] static int levelAt(int floor, int ground, double distance, int blend);
    // Blocks from a column to the nearest column of a footprint (0 inside).
    [[nodiscard]] static double distanceTo(const QcStructurePlacement &placement, int x, int z);
    // The new top block of the column (x, z) whose terrain ground's top block is `ground`, if the
    // blend around the placement moves it (within `blend` of the footprint, not under water).
    [[nodiscard]] std::optional<int> topAt(const QcStructurePlacement &placement, int blend, int x, int z, int ground) const;
    // Shapes the chunk's columns within `blend` of the placement's footprint (not the footprint).
    void shape(QcChunk &chunk, const QcStructurePlacement &placement, int blend) const;

private:
    // Moves one column's ground top from `ground` to `target`: `local` is the column in the chunk.
    void shapeColumn(QcChunk &chunk, QcBlockPos local, int ground, int target) const;

    const QcTerrainGenerator &m_terrain;
};
