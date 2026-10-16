#pragma once

#include "QcNoise.h"
#include "QcVillagePlan.h"

#include <optional>

class QcTerrainGenerator;

// The ground a village over water shapes around itself (decision E-407, QcVillagePlan::Wetland):
// within the wetland's margin of its pieces and boardwalks, frayed by noise, the ground lies a block
// or two below the water surface with a bottom of mud, deep water filled up to it; over the blend
// width beyond, the ground rises back to the terrain, so no cliff cuts the neighbouring land; the
// land access beside the stairs stays as it is. A pure function of the seed, the plan and the
// terrain noise per column, so every chunk shapes its own columns the same in any order. Thread-safe.
class QcWetlandShaper
{
public:
    QcWetlandShaper(const QcTerrainGenerator &terrain, quint64 seed);

    // The height of the ground's top block the column gets; std::nullopt keeps the terrain (outside
    // the wetland, in the land access, or dry ground the blend leaves as it is).
    [[nodiscard]] std::optional<int> groundAt(const QcVillagePlan &plan, int x, int z) const;
    // Shapes the chunk's columns of the plan's wetland, before the village is built into it: lowered
    // ground is removed (water up to the water surface, air above), raised ground filled with the
    // bottom, the top block is the bottom under water, else the terrain's surface block.
    void shape(QcChunk &chunk, const QcVillagePlan &plan) const;

private:
    // groundAt() for a column whose terrain ground's top block is `ground`.
    [[nodiscard]] std::optional<int> targetAt(const QcVillagePlan &plan, int x, int z, int ground) const;
    // Shapes one column: `local` is the column in the chunk with y the terrain ground's top block.
    void shapeColumn(QcChunk &chunk, QcBlockPos local, int target, QcBlockId bottom) const;
    // Distance from the column to the nearest piece or boardwalk (0 inside one).
    static double distanceToWet(const QcVillagePlan::Wetland &wetland, int x, int z);

    const QcTerrainGenerator &m_terrain;
    QcNoise m_fray;
};
