#pragma once

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

// Soft edges of cold biomes (decision E-858): whether the top water layer of a column freezes, by
// its temperature plus a noise (a ragged ice edge with single floes beyond it; rivers only further
// into the cold), and the jittered offset of the column whose biome gives a column its ground
// blocks, so grass, gravel and snow mix over a few blocks at a biome border. Pure functions of the
// seed and the position; thread-safe.
class QcColdEdges
{
public:
    QcColdEdges(const QcTerrainDefinitions::ColdEdges &settings, quint64 seed);

    // Whether water freezes by temperature; otherwise by the biomes' frozenWater.
    bool byTemperature() const { return m_settings.byTemperature; }
    // Blocks a column's ground biome may lie away (0: every column keeps its own).
    int groundBlend() const { return m_settings.groundBlend; }

    // The top water layer of a column with this climate temperature freezes. `riverShare` (0 to 1)
    // moves the threshold from temperature towards the lower riverTemperature: 1 in a river channel,
    // falling off in the still water along a river's line (decision E-914), so the threshold has no
    // step where a river meets a lake or the sea.
    [[nodiscard]] bool freezes(double temperature, double riverShare, int x, int z) const;

    // The column, at most groundBlend() blocks away along each axis, whose biome lends a column at
    // x, z its ground blocks: a smooth noise with a little per-column scatter, so the biomes'
    // ground interlocks in lobes and specks rather than meeting on a line.
    struct Offset
    {
        int dx = 0;
        int dz = 0;
    };
    [[nodiscard]] Offset groundOffset(int x, int z) const;

private:
    QcTerrainDefinitions::ColdEdges m_settings;
    quint64 m_seed = 0;
    QcNoise m_iceNoise;
    QcNoise m_groundNoiseX;
    QcNoise m_groundNoiseZ;
};
