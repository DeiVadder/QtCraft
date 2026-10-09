#pragma once

// Shape and biome of one terrain column, before caves and features.
struct QcTerrainColumn
{
    // Height of the terrain surface: the top solid block is at height - 1.
    int height = 0;
    int biome = 0;
    // 0 = ocean, 1 = land; values in between are coasts.
    double landFactor = 1.0;
    // Largest height difference to the four neighbouring columns; set where it is needed
    // (generate(), surfaceBlockAt()), else 0.
    int slope = 0;
    // Overhang amplitude in blocks, blended across biome borders (0: plain column).
    double overhang = 0.0;
};
