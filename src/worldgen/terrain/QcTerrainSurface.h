#pragma once

#include "worldgen/terrain/QcRiverField.h"
#include "worldgen/terrain/QcTerrainColumn.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

// The ground blocks of a terrain column from its biome: the surface block (under water the bed with
// its patches, river banks, beaches, snow above the snow line, rock on steep slopes) and the soil
// below it. Deterministic per seed and thread-safe.
class QcTerrainSurface
{
public:
    // Both references must outlive the surface (the terrain generator owns all three).
    QcTerrainSurface(const QcTerrainDefinitions &definitions, const QcRiverField &rivers, quint64 seed);

    // The top block of a column at x, z (its `slope` set where the biome has steep rock).
    [[nodiscard]] QcBlockId surface(const QcTerrainColumn &column, int x, int z) const;
    // The ground's own block below the surface down to the subsurface depth.
    [[nodiscard]] QcBlockId subsurface(const QcTerrainColumn &column, QcBlockId surface) const;

private:
    using Biome = QcTerrainDefinitions::Biome;

    // The ground block under the water of a column: the biome's, or one of its patches.
    [[nodiscard]] QcBlockId underwater(const Biome &biome, int x, int z) const;

    const QcTerrainDefinitions &m_definitions;
    const QcRiverField &m_rivers;
    quint64 m_seed = 0;
};
