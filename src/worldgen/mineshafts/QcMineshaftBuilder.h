#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/mineshafts/QcMineshaftBlocks.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"

// Writes the cells of a mine shaft that lie in one chunk (decision E-570), the same in any order of
// the chunks: reads only the chunk's own blocks before the mine shaft. In this order: pillars under
// open floors of bridges (posts down to the ground, at most `maxPillar`), planks for open floors,
// the open cells, liquid beside them and sand or gravel above them turned into `seal` (generated
// liquids stay still, and nothing falls in once a neighbour changes), rooms from
// structures, then supports, rails, cobwebs, lights and spawners. The entrance comes in a pass of
// its own after the terrain's trees, cacti and surface pools (buildEntrance).
class QcMineshaftBuilder
{
public:
    QcMineshaftBuilder(const QcMineshaftBlocks &blocks, int maxPillar);

    void build(QcChunk &chunk, const QcMineshaftPlan &plan) const;
    // Empties the headframe's box (three by three, three high above the ground's top) and the shaft,
    // seals liquid beside them, lines the shaft where it is open, then the ladder and the headframe.
    void buildEntrance(QcChunk &chunk, const QcMineshaftPlan::Entrance &entrance) const;

private:
    // The chunk's columns (inclusive), with a margin, as a quick test for a box of cells.
    struct Area
    {
        int x0 = 0;
        int z0 = 0;
        int x1 = 0;
        int z1 = 0;

        bool overlaps(int lowX, int lowZ, int highX, int highZ) const { return highX >= x0 && lowX <= x1 && highZ >= z0 && lowZ <= z1; }
    };

    void placePillars(QcChunk &chunk, const QcMineshaftPlan &plan) const;
    void placeNodeFloors(QcChunk &chunk, const QcMineshaftPlan &plan, Area area) const;
    void placeCorridorFloors(QcChunk &chunk, const QcMineshaftPlan &plan) const;
    // Opens the cells of the plan's nodes and corridors in the chunk; seals liquid beside them and
    // sand or gravel above them (also beside cells in the neighbouring chunks).
    void open(QcChunk &chunk, const QcMineshaftPlan &plan, Area area) const;
    void seal(QcChunk &chunk, QcBlockPos cell) const;
    // Planks where the cell is open (not solid or liquid); nothing outside the chunk.
    void plankIfOpen(QcChunk &chunk, QcBlockPos cell) const;

    const QcMineshaftBlocks &m_blocks;
    int m_maxPillar = 20;
};
