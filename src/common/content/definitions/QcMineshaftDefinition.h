// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

// Abandoned mine shafts of the terrain generator (`"mineshafts"` in worldgen/terrain.json, decision
// E-570): a network of corridors on several levels below a central room, laid out on a square
// lattice, with wooden supports, rails, cobwebs, chest minecarts with loot and spawners; levels
// joined by staircases, plank bridges on pillars where a corridor crosses a cave, and a ladder
// shaft with a headframe up to the surface in some of them. Rooms come from structures that name
// a `"mineshaft"` weight. Server side only.
struct QcMineshaftSettings
{
    // A biome with its own chance of a surface entrance.
    struct BiomeChance
    {
        QString biome;
        double chance = 0.0;
    };

    // No "mineshafts" object, or `"enabled": false`: none.
    bool enabled = false;
    // One mine shaft at most per square region of this many blocks, with `chance`; the whole
    // network stays inside its region.
    int regionSize = 128;
    double chance = 0.3;
    // Feet height of the lowest level at least and of the top level at most.
    int minY = 14;
    int maxY = 44;
    // Places tried for the central room per region.
    int attempts = 6;
    int minLevels = 2;
    int maxLevels = 3;
    // Height between two levels: also the number of steps of a staircase, and the lattice spacing
    // is three more (the corridor between two crossings is as long as a staircase).
    int levelSpacing = 6;
    // Lattice steps from the centre a crossing may lie.
    int radius = 4;
    // Crossings and corners per level, about.
    int minNodes = 7;
    int maxNodes = 13;
    // Lattice steps a level grows straight on from a node at most per try (decision E-889): runs of
    // crossings in a line read as long corridors; 1: one step per try.
    int maxRun = 1;
    // Chance of a corridor between two crossings of a level that are joined another way already.
    double loopChance = 0.25;
    // Chance of a second staircase between two levels.
    double extraStairChance = 0.5;
    // Central room: half its inner width (inner width 2 * roomHalf + 1) and its inner height.
    int roomHalf = 4;
    int roomHeight = 4;
    // Ground left above every corridor at least.
    int groundCover = 4;
    // Pillars under bridges reach the ground within this many blocks, or the corridor is not made.
    int maxPillar = 20;
    // A support (two posts and a beam) every this many cells of a corridor.
    int supportSpacing = 3;
    QString planks;
    QString post;
    QString rail;
    double railChance = 0.6;
    QString cobweb;
    double cobwebChance = 0.08;
    // A `light` on a support beam with `lightChance`, only `lightDistance` cells (taxicab) away from
    // every spawner's spawn area.
    QString light;
    double lightChance = 0.3;
    int lightDistance = 8;
    QString ladder;
    // Turns liquid beside a corridor into this block, so generated liquids stay still.
    QString seal;
    QString spawner;
    QString mob;
    int minSpawners = 1;
    int maxSpawners = 2;
    // A chest minecart of `cart` on a corridor with rails with `cartChance`, filled from `lootTable`.
    QString cart;
    double cartChance = 0.2;
    QString lootTable;
    // Surface entrance: a ladder shaft from the central room up with a headframe, with
    // `entranceChance`, or the chance of the biome at the shaft.
    double entranceChance = 0.3;
    QList<BiomeChance> entranceBiomes;
    // The kind of a surface entrance, drawn by weight per mine shaft (`"entranceKinds"`, decision
    // E-895): a ladder shaft with a headframe, or an inclined tunnel (steps four cells high with
    // supports, rails with `railChance`) from a side of the central room up to the ground. When the
    // drawn kind finds no place, the other one (with a weight above 0) is tried.
    int ladderWeight = 1;
    int inclineWeight = 0;
    // Rooms from structures with a `"mineshaft"` weight at dead ends: at most `maxRooms`, each dead
    // end with `roomChance`.
    double roomChance = 0.3;
    int maxRooms = 2;

    static constexpr int maxRegionSize = 512;
    static constexpr int maxRadius = 8;
    static constexpr int maxRunLength = 8;
    static constexpr int maxLevelCount = 6;
    // Rooms from structures fit into a lattice cell beside their corridor.
    static constexpr int maxRoomFootprint = 7;
    static constexpr int maxEntranceWeight = 100;

    // The lattice spacing: crossings are this many blocks apart.
    int spacing() const { return levelSpacing + 3; }
    // How far from its centre a mine shaft may reach (room, corridors, structures; not the
    // entrance's headframe, which stays above the room).
    int extent() const { return radius * spacing() + maxRoomFootprint / 2 + 2; }
    // Whether a structure fits as a room: odd sides of at most maxRoomFootprint (its door in the
    // middle of a side), lower than a level.
    bool fitsRoom(int sizeX, int sizeY, int sizeZ) const
    {
        return sizeX <= maxRoomFootprint && sizeZ <= maxRoomFootprint && sizeX % 2 == 1 && sizeZ % 2 == 1 && sizeY < levelSpacing;
    }
    double entranceChanceIn(const QString &biome) const
    {
        for (const BiomeChance &entry : entranceBiomes) {
            if (entry.biome == biome) {
                return entry.chance;
            }
        }
        return entranceChance;
    }
};
