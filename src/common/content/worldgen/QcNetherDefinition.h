// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcStructureDefinition.h"

#include <QList>
#include <QString>
#include <QStringList>

/*! \brief The settings of the nether generator (worldgen/nether.json, decision E-1030): the blocks
    of its terrain, the shape of its 3D noise and what it scatters over the caves. Its biomes and
    ores are ordinary biome and ore files with \c {"dimension": "nether"}.

    The defaults are the base game's shape; a pack replaces the file as a whole. */
struct QcNetherSettings
{
    /*! \brief The 3D noise of the terrain (\c {"shape"}): solid where the noise plus the pull of
        the floor and the roof is above 0. \qcfields */
    struct Shape
    {
        /*! Blocks per noise cell along x and z, and along y. \qcshared */
        double scale = 56.0;
        double verticalScale = 28.0;
        /*! Layers of the noise. */
        int octaves = 3;
        /*! Below this height the ground closes, fully \c {floorFade} blocks further down.
            \qcshared */
        int floorHeight = 30;
        int floorFade = 16;
        /*! Above this height the roof closes, fully \c {roofFade} blocks further up.
            \qcshared */
        int roofHeight = 100;
        int roofFade = 22;
        /*! Added to the noise everywhere: below 0 for more room. */
        double solidity = -0.06;
    };

    /*! \brief Blocks that replace the ground along the lava (\c {"shores"}): gravel and soul sand
        banks. Where the noise of scale \c {scale} is above \c {threshold}, the top \c {depth}
        blocks of a floor within \c {height} blocks of the lava become \c {block}. \qcfields */
    struct Shore
    {
        QString block;
        double scale = 12.0;
        double threshold = 0.2;
        int height = 4;
        int depth = 3;
    };

    /*! \brief Clusters that hang from the roof of a cave (\c {"clusters"}): glowstone. \c {tries}
        columns per chunk look for a roof of \c {on} between \c {minY} and \c {maxY}; each cluster
        grows to at most \c {size} blocks, each touching exactly one before it, within
        \c {radius} columns and \c {depth} blocks below its start; only in \c {biomes} (empty: in
        all). \qcfields */
    struct Cluster
    {
        QString block;
        QString on;
        int tries = 6;
        int minY = 40;
        int maxY = 124;
        int size = 40;
        int radius = 4;
        int depth = 9;
        QStringList biomes;
    };

    /*! \brief Stone posts rising from the floors and hanging from the roofs (\c {"pillars"},
        decision E-1047): where a noise of \c {scale} lies above \c {threshold}, two blocks wide
        and up to \c {height} tall, in \c {biomes}. \qcfields */
    struct Pillar
    {
        QString block;
        QStringList biomes;
        double scale = 18.0;
        double threshold = 0.1;
        int height = 12;
    };

    /*! \brief Still pools sunk one block into the floors (\c {"pools"}, decision E-1047): where
        a noise of \c {scale} lies above \c {threshold}, in a cell with rock on all four sides;
        \c {rim} covers the floor around them, down to \c {rimBand} below the threshold, in
        \c {biomes}. \qcfields */
    struct Pool
    {
        QString block;
        QString rim;
        QStringList biomes;
        double scale = 12.0;
        double threshold = 0.2;
        double rimBand = 0.08;
    };

    /*! \brief Springs of a liquid in the rock (\c {"springs"}): \c {tries} cells per chunk between
        \c {minY} and \c {maxY}; a cell of \c {in} walled in on all sides but one becomes the
        liquid, which starts to flow when the chunk arrives. \qcfields */
    struct Spring
    {
        QString block;
        QString in;
        int tries = 8;
        int minY = 36;
        int maxY = 120;
    };

    /*! \brief Natural fires (\c {"fires"}): \c {tries} columns per chunk; the first floor of a
        block of \c {on} above the lava gets \c {block}. \qcfields */
    struct Fire
    {
        QString block;
        QStringList on;
        int tries = 2;
    };

    /*! \brief A structure of the nether (\c {"structures"}): one try per square region of
        \c {spacing} blocks, the copy wholly in its region, standing on a floor above the lava
        with room for it. \qcfields */
    struct Structure
    {
        QString structure;
        int spacing = 256;
        double chance = 0.5;
    };

    /*! \brief The fortress of the nether (\c {"fortress"}, decisions E-1070 to E-1075): a net of
        rooms and passages on a square lattice of \c {lattice} points \c {spacing} blocks apart, on
        two levels, at most one per square region of \c {regionSize} blocks and wholly inside it.
        A \c {regionSize} of 0 (no entry) means no fortresses.

        The floor of its lower level lies between \c {lowestFloor} and \c {highestFloor}; it has
        \c {minRooms} to \c {maxRooms} rooms there and up to \c {upperRooms} above. A passage or
        room with at least \c {roofShare} of its samples in rock gets walls and a roof, the others
        stand open as bridges. \c {spawnerMob} is the kind of the platform's spawner,
        \c {lootTable} fills its chests, \c {monsters} are the kinds of its spawn area (E-871).
        \qcfields */
    struct Fortress
    {
        int regionSize = 0;
        double chance = 0.5;
        int attempts = 6;
        int lowestFloor = 44;
        int highestFloor = 60;
        int lattice = 5;
        int spacing = 24;
        int minRooms = 10;
        int maxRooms = 16;
        int upperRooms = 4;
        double loopChance = 0.25;
        double roofShare = 0.3;
        double chestChance = 0.35;
        QString brick;
        QString cracked;
        QString chiseled;
        QString lane;
        QString fence;
        QString stairs;
        QString light;
        QString soil;
        QString crop;
        QString chest;
        QString spawner;
        QString spawnerMob;
        QString lootTable;
        QList<QcStructureDefinition::SpawnArea::Monster> monsters;
    };

    /*! \brief The station a demonstration world's nether builds at its arrival (\c {"station"},
        decision E-1034): \c {structure} with its middle where the showcase structure \c {portal}
        leads to. \qcfields */
    struct Station
    {
        QString structure;
        QString portal;
    };

    /*! The block of the rock (\c {"filler"}). */
    QString filler = QStringLiteral("qtcraft:netherrack");
    /*! The block of the floor and the roof (\c {"bedrock"}). */
    QString bedrock = QStringLiteral("qtcraft:bedrock");
    /*! The liquid of the sea (\c {"liquid"}). */
    QString liquid = QStringLiteral("qtcraft:lava");
    /*! The highest block of the sea (\c {"liquidLevel"}). */
    int liquidLevel = 31;
    /*! The height of the bedrock roof (\c {"roof"}); nothing is generated above it. */
    int roof = 127;
    /*! How many layers the bedrock of the floor and the roof reaches into the rock, thinning out
        (\c {"bedrockLayers"}). */
    int bedrockLayers = 4;
    /*! Blocks per cell of the two noises of the biome map (\c {"biomeScale"}). */
    double biomeScale = 200.0;
    /*! The noise of the terrain. */
    Shape shape;
    /*! The banks along the lava. */
    QList<Shore> shores;
    /*! The clusters on the roofs. */
    QList<Cluster> clusters;
    /*! The springs. */
    QList<Spring> springs;
    /*! The posts of the biomes that have them. */
    QList<Pillar> pillars;
    /*! The pools of the biomes that have them. */
    QList<Pool> pools;
    /*! The natural fires. */
    QList<Fire> fires;
    /*! The structures. */
    QList<Structure> structures;
    /*! The arrival station of a demonstration world; no structure for none. */
    Station station;
    /*! The fortresses. */
    Fortress fortress;
};
