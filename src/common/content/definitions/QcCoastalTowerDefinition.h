#pragma once

#include <QList>
#include <QString>

// Coastal towers of the terrain generator (`"coastalTowers"` in worldgen/terrain.json, decision
// E-605): a tower from structure data on land at or a little above sea level with ocean near it,
// standing on a foundation down to the ground. At most one per square region of `regionSize`
// blocks, with `chance`; the tower lies wholly inside its region. Server side only.
struct QcCoastalTowerSettings
{
    struct Structure
    {
        QString structure;
        int weight = 1;
    };

    // No "coastalTowers" object, or `"enabled": false`: none.
    bool enabled = false;
    int regionSize = 160;
    double chance = 0.6;
    // Sites checked fully per region (land near the sea found on a grid of `gridStep` blocks).
    int attempts = 16;
    int gridStep = 6;
    // The ground's top under the tower lies from sea level up to `maxRise` blocks above it, and its
    // highest and lowest columns differ by at most `maxSlope`.
    int maxRise = 5;
    int maxSlope = 3;
    // Ocean within `coastDistance` blocks of the tower's edge: a column of the sea (land factor below
    // one half) whose ground lies at least `oceanDepth` blocks below the water's surface.
    int coastDistance = 12;
    int oceanDepth = 3;
    // Blocks kept free of villages and mine shaft entrances around the tower.
    int clearance = 8;
    // The block under the tower down to the ground, at most `maxFoundation` blocks deep.
    QString foundation;
    int maxFoundation = 12;
    QList<Structure> structures;

    static constexpr int maxRegionSize = 512;
    static constexpr int maxDistance = 48;
};
