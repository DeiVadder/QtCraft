// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief The strongholds of the terrain generator (\c {"strongholds"} in worldgen/terrain.json,
    decisions E-1170 to E-1174), from overworld generator version 4 on: 128 underground mazes of
    stone bricks on eight rings around the origin, each grown from a spiral staircase into
    corridors, crossings, prison cells, libraries and one portal room with twelve end portal
    frames over a pool of lava.

    The rings and the kinds of rooms follow the original game; the settings name the blocks, the
    loot tables and the heights. Server side only. */
struct QcStrongholdSettings
{
    /*! No "strongholds" object, or \c {"enabled": false}: none. */
    bool enabled = false;
    /*! The floor of the room under the starting staircase lies between these heights at most
        and at least, and every floor of a stronghold at \c {lowestFloor} or above. \qcshared */
    int minStartFloor = 16;
    int maxStartFloor = 28;
    int lowestFloor = 6;
    /*! Ground left over the top of the starting staircase at least. */
    int groundCover = 6;
    /*! The pieces of one stronghold at most, and how far (blocks along x or z) a piece may lie
        from the starting staircase. \qcshared */
    int maxPieces = 60;
    int reach = 112;
    /*! The chance of an eye in each end portal frame of the portal room. */
    double eyeChance = 0.1;
    /*! The share of the wall bricks that are cracked, mossy and infested (drawn in this order
        per cell, the rest plain). \qcshared */
    double crackedShare = 0.2;
    double mossyShare = 0.3;
    double infestedShare = 0.05;

    /*! The plain, cracked, mossy, chiseled and infested bricks of the walls (\c {"blocks"}:
        \c {"bricks"}, \c {"cracked"}, \c {"mossy"}, \c {"chiseled"}, \c {"infested"}).
        \qcshared */
    QString bricks;
    QString cracked;
    QString mossy;
    QString chiseled;
    QString infested;
    /*! The stairs and bottom slabs of the bricks (\c {"stairs"}, \c {"slab"}). \qcshared */
    QString stairs;
    QString slab;
    /*! The doors of the doorways and their bars (\c {"woodDoor"}, \c {"ironDoor"},
        \c {"bars"}), the button beside an iron door (\c {"button"}). \qcshared */
    QString woodDoor;
    QString ironDoor;
    QString bars;
    QString button;
    /*! The furnishing of libraries and store rooms (\c {"bookshelf"}, \c {"planks"},
        \c {"fence"}, \c {"ladder"}, \c {"cobweb"}, \c {"torch"}). \qcshared */
    QString bookshelf;
    QString planks;
    QString fence;
    QString ladder;
    QString cobweb;
    QString torch;
    /*! The water of the fountains and the lava of the portal room (\c {"water"}, \c {"lava"}).
        \qcshared */
    QString water;
    QString lava;
    /*! The chests, the spawner and the end portal frame (\c {"chest"}, \c {"spawner"},
        \c {"portalFrame"}; the frame has the states \c {facing} and \c {eye}). \qcshared */
    QString chest;
    QString spawner;
    QString portalFrame;
    /*! The mob of the spawner in the portal room (\c {"mob"}). */
    QString mob;
    /*! The loot tables of the chests in corridors, in store rooms and in libraries
        (\c {"corridorLoot"}, \c {"storeLoot"}, \c {"libraryLoot"}). \qcshared */
    QString corridorLoot;
    QString storeLoot;
    QString libraryLoot;
};
