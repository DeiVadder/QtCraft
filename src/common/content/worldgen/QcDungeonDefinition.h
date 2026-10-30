// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief Dungeons of the terrain generator (\c {"dungeons"} in worldgen/terrain.json, decision
    E-522): a room of \c {wall}/\c {moss} blocks deep underground with a spawner of a weighted kind
    and chests with loot, and corridors carved from it to caves within \c {reach}, lined where they
    border cave or earth and lit sparsely outside the spawner's spawn area.

    Server side only. */
struct QcDungeonSettings
{
    /*! \brief One entry of \c {"mobs"}: a mob kind for the spawner and its weight.

        \c {mob} is the qualified mob name (key \c {"mob"}); \c {weight} is its relative weight
        when the spawner's kind is picked (key \c {"weight"}, at least 1). \qcfields */
    struct Mob
    {
        QString mob;
        int weight = 1;
    };

    /*! No "dungeons" object, or \c {"enabled": false}: none. */
    bool enabled = false;
    /*! One dungeon at most per square region of this many blocks, with \c {chance}. */
    int regionSize = 48;
    /*! Chance that a region gets a dungeon (\c {"chance"}, 0 to 1). */
    double chance = 0.6;
    /*! Lowest and highest height of the room's floor (\c {"minY"}, \c {"maxY"}). \qcshared */
    int minY = 14;
    int maxY = 48;
    /*! Places tried per region; a place where caves take more than \c {maxCaveShare} of the room
        is refused. */
    int attempts = 6;
    /*! Largest share of the room's cells that may be cave (\c {"maxCaveShare"}, 0 to 1). */
    double maxCaveShare = 0.25;
    /*! Outer sizes of the room (walls included), odd; its height is fixed (floor, three cells,
        ceiling). \qcshared */
    int minSize = 7;
    int maxSize = 9;
    /*! Block of the room's walls, floor and ceiling and of the corridor lining (\c {"wall"}). */
    QString wall;
    /*! Block mixed into the walls by \c {mossChance} (\c {"moss"}; defaults to \c {wall}). */
    QString moss;
    /*! Share of \c {moss} among the wall blocks (the floor twice as much). */
    double mossChance = 0.3;
    /*! Spawner block in the room (\c {"spawner"}). */
    QString spawner;
    /*! Mob kinds the spawner's kind is picked from by weight (\c {"mobs"}); at least one. */
    QList<Mob> mobs;
    /*! Chest block placed along the room's walls (\c {"chest"}). */
    QString chest;
    /*! Loot table that fills the chests (\c {"lootTable"}). */
    QString lootTable;
    /*! Fewest and most chests per room (\c {"minChests"}, \c {"maxChests"}, up to 8); fewer
        when the room has fewer free places along its walls. \qcshared */
    int minChests = 1;
    int maxChests = 2;
    /*! Corridors: at most \c {corridors}, each at most \c {maxLength} cells long, to caves found
        within \c {reach} cells of the room's wall (straight, or with one bend). */
    int corridors = 3;
    /*! Longest corridor in cells (\c {"maxLength"}). */
    int maxLength = 28;
    /*! Distance in cells from the room's wall within which caves are searched (\c {"reach"}). */
    int reach = 24;
    /*! A wall \c {light} every \c {lightSpacing} cells of a corridor, only where it is at least
        \c {lightDistance} cells (taxicab) from every cell of the spawner's spawn area. */
    QString light;
    /*! Corridor cells between two lights (\c {"lightSpacing"}). */
    int lightSpacing = 8;
    /*! Smallest taxicab distance in cells of a light from the spawn area
        (\c {"lightDistance"}). */
    int lightDistance = 8;

    /*! Largest accepted \c {regionSize}. */
    static constexpr int maxRegionSize = 512;
    /*! Largest accepted room size (\c {minSize}, \c {maxSize}). */
    static constexpr int maxRoomSize = 15;
    /*! Largest accepted \c {maxLength}, \c {reach}, \c {lightSpacing} and
        \c {lightDistance}. */
    static constexpr int maxCorridorLength = 64;
};
