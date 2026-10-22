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
    // Dry ground at the water's edge of a river (decision E-840): the river biome's beach in
    // patches. The water of a river channel has the river biome as `biome`.
    bool riverBank = false;
    // The river near the column (decision E-840): blocks from its middle line (about; 1e9 far from
    // rivers and where the ground lies too deep under water for one) and half the width of its
    // water there (0 where none runs), as QcTerrainGenerator::riverAt() gives them.
    double riverDistance = 1.0e9;
    double riverHalfWidth = 0.0;
};
