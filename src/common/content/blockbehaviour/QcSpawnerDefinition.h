// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief What makes a block a monster spawner (\c {"spawner"} in block data, decision E-520): the
    rules it spawns its creature kind by.

    The kind itself belongs to each placed spawner (its block entity, QcSpawners); \c {mob} is
    only the kind a spawner starts with when nothing else names one. Server side only. */
struct QcSpawnerDefinition
{
    /*! Whether the block is a spawner at all. */
    bool isSpawner = false;
    /*! Kind a spawner of this block starts with (empty: none, it waits for /setblock or the
        world generator to name one). */
    QString mob;
    /*! A player must be within this many blocks of the block's centre for it to work. */
    double range = 16.0;
    /*! Creatures of its kind within \c {nearbyRange} blocks of the spawner block on every axis
        (a cube of 9 × 9 × 9 blocks by default, like the reference, apart from the spawn area) at
        most; with as many there it waits for its next delay. \qcshared */
    int maxNearby = 6;
    int nearbyRange = 4;
    /*! Spawn attempts per cycle, each at a random cell of the area. */
    int count = 4;
    /*! Seconds between cycles, drawn evenly from [minDelay, maxDelay] after each cycle that
        spawned. \qcshared */
    double minDelay = 10.0;
    double maxDelay = 40.0;
    /*! Spawn area centred on the block (odd sizes in blocks, x by y by z). \qcshared */
    int areaX = 9;
    int areaY = 3;
    int areaZ = 9;
    /*! Block light at a spawn cell at most (like natural spawning, sky light does not count). */
    int maxLight = 11;

    /*! Largest spawn area size and \c {nearbyRange} the parser accepts. */
    static constexpr int maxArea = 33;
    /*! Largest \c {count}. */
    static constexpr int maxCount = 16;
    /*! Largest \c {maxNearby}. */
    static constexpr int maxNearbyLimit = 64;
    /*! Largest \c {range} in blocks. */
    static constexpr double maxRange = 128.0;
    /*! Longest \c {minDelay} and \c {maxDelay} in seconds. */
    static constexpr double maxDelaySeconds = 3600.0;
};
