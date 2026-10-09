#pragma once

#include "QcChunk.h"
#include "QcNoise.h"
#include "QcTerrainColumn.h"

#include <array>

struct QcTerrainDefinitions;
struct QcTerrainSettings;
class QcTerrainColumnGrid;

// Connected cave systems: spaghetti tunnels of varying width and caverns with pillars, evaluated on
// a coarse lattice and interpolated. Caves stay below a rock roof except in entrance patches and
// hold lava lakes deep down.
class QcCaveCarver
{
public:
    QcCaveCarver(quint64 seed, const QcTerrainSettings &terrain, const QcTerrainDefinitions &definitions);

    bool enabled() const { return m_enabled; }
    // Highest block caves may remove in a column: below a rock roof except at entrances, and below
    // a thicker lid under water.
    int top(const QcTerrainColumn &column, int x, int z) const;
    // True where a cave carves the terrain; `column` is the terrain column at x, z.
    bool isCave(int x, int y, int z, const QcTerrainColumn &column) const;
    void carve(QcChunk &chunk, const QcTerrainColumnGrid &columns) const;

private:
    // Cave field values at the lattice points of one chunk.
    struct Lattice;

    Lattice lattice(QcChunkPos pos, int rows) const;
    void carveColumn(QcChunk &chunk, const Lattice &lattice, int localX, int localZ, int columnTop) const;
    double density(int field, int x, int y, int z) const;
    // Whether the cave fields (interpolated at a block) remove that block.
    bool carves(const std::array<double, 4> &values, int y) const;

    bool m_enabled = true;
    int m_seaLevel = 62;
    QcBlockId m_bedrock = qcAirBlockId;
    double m_tunnelWidth = 0.075;
    double m_cavernThreshold = 0.58;
    QcBlockId m_liquid = qcAirBlockId;
    int m_liquidLevel = 10;
    int m_roof = 4;
    // Entrance noise above this value lets caves open to the surface.
    double m_entranceThreshold = 1.0;

    QcNoise m_tunnelNoiseA;
    QcNoise m_tunnelNoiseB;
    QcNoise m_cavernNoise;
    QcNoise m_tunnelWidthNoise;
    QcNoise m_entranceNoise;
};
