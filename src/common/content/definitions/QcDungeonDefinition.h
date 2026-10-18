#pragma once

#include <QList>
#include <QString>

// Dungeons of the terrain generator (`"dungeons"` in worldgen/terrain.json, decision E-522): a room
// of `wall`/`moss` blocks deep underground with a spawner of a weighted kind and chests with loot,
// and corridors carved from it to caves within `reach`, lined where they border cave or earth and
// lit sparsely outside the spawner's spawn area. Server side only.
struct QcDungeonSettings
{
    struct Mob
    {
        QString mob;
        int weight = 1;
    };

    // No "dungeons" object, or `"enabled": false`: none.
    bool enabled = false;
    // One dungeon at most per square region of this many blocks, with `chance`.
    int regionSize = 48;
    double chance = 0.6;
    // Height of the room's floor.
    int minY = 14;
    int maxY = 48;
    // Places tried per region; a place where caves take more than `maxCaveShare` of the room is refused.
    int attempts = 6;
    double maxCaveShare = 0.25;
    // Outer sizes of the room (walls included), odd; its height is fixed (floor, three cells, ceiling).
    int minSize = 7;
    int maxSize = 9;
    QString wall;
    QString moss;
    // Share of `moss` among the wall blocks (the floor twice as much).
    double mossChance = 0.3;
    QString spawner;
    QList<Mob> mobs;
    QString chest;
    QString lootTable;
    int minChests = 1;
    int maxChests = 2;
    // Corridors: at most `corridors`, each at most `maxLength` cells long, to caves found within
    // `reach` cells of the room's wall (straight, or with one bend).
    int corridors = 3;
    int maxLength = 28;
    int reach = 24;
    // A wall `light` every `lightSpacing` cells of a corridor, only where it is at least
    // `lightDistance` cells (taxicab) from every cell of the spawner's spawn area.
    QString light;
    int lightSpacing = 8;
    int lightDistance = 8;

    static constexpr int maxRegionSize = 512;
    static constexpr int maxRoomSize = 15;
    static constexpr int maxCorridorLength = 64;
};
