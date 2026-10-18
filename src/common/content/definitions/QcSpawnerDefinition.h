#pragma once

#include <QString>

// What makes a block a monster spawner (`"spawner"` in block data, decision E-520): the rules it
// spawns its creature kind by. The kind itself belongs to each placed spawner (its block entity,
// QcSpawners); `mob` is only the kind a spawner starts with when nothing else names one. Server
// side only.
struct QcSpawnerDefinition
{
    // Whether the block is a spawner at all.
    bool isSpawner = false;
    // Kind a spawner of this block starts with (empty: none, it waits for /setblock or the world
    // generator to name one).
    QString mob;
    // A player must be within this many blocks of the block's centre for it to work.
    double range = 16.0;
    // Creatures of its kind within `nearbyRange` blocks of the spawner block on every axis (a cube of
    // 9 × 9 × 9 blocks by default, like the reference, apart from the spawn area) at most; with as
    // many there it waits for its next delay.
    int maxNearby = 6;
    int nearbyRange = 4;
    // Spawn attempts per cycle, each at a random cell of the area.
    int count = 4;
    // Seconds between cycles, drawn evenly from [minDelay, maxDelay] after each cycle that spawned.
    double minDelay = 10.0;
    double maxDelay = 40.0;
    // Spawn area centred on the block (odd sizes in blocks, x by y by z).
    int areaX = 9;
    int areaY = 3;
    int areaZ = 9;
    // Block light at a spawn cell at most (like natural spawning, sky light does not count).
    int maxLight = 11;

    static constexpr int maxArea = 33;
    static constexpr int maxCount = 16;
    static constexpr int maxNearbyLimit = 64;
    static constexpr double maxRange = 128.0;
    static constexpr double maxDelaySeconds = 3600.0;
};
