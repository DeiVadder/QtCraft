// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockBox.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcXzRect.h"
#include "worldgen/fortress/QcFortressPlan.h"

#include <QList>

#include <algorithm>
#include <array>

/*! \brief One stronghold as QcStrongholdBuilder writes it (decision E-1171): pieces as in the
    original game, each a box of stone bricks entered through a doorway in its back wall, grown
    from a spiral staircase; all from the seed and the terrain noise alone, so every chunk writes
    its own cells without looking at its neighbours. Cells are world positions.

    A piece is laid out in its own frame: \c {x} across from its left wall as one walks in,
    \c {y} up from its floor, \c {z} along from its back wall (the doorway) to its front wall.
    Pieces never overlap; two pieces touching share no cell. */
struct QcStrongholdPlan
{
    /*! The side type of the fortress, with its steps and turns. */
    using Side = QcFortressPlan::Side;

    /*! \brief What a piece is. */
    enum class Kind : quint8
    {
        /*! The spiral staircase the stronghold grows from, without a doorway. */
        Start,
        /*! A straight corridor, with doorways to the sides in some. */
        Corridor,
        /*! A straight corridor with a chest in a niche. */
        ChestCorridor,
        /*! A hall with two prison cells behind bars. */
        Prison,
        /*! A short corridor turning left. */
        LeftTurn,
        /*! A short corridor turning right. */
        RightTurn,
        /*! A room with doorways ahead and to both sides: plain, with a pillar, a fountain or a
            store room (its \c {variant}). */
        Crossing,
        /*! A straight flight of stairs down. */
        StraightStairs,
        /*! A spiral staircase down. */
        SpiralStairs,
        /*! A library of one storey, or of two (variant 1). */
        Library,
        /*! The room of the end portal: a pool of lava, stairs, the spawner and the frames. */
        PortalRoom,
    };

    /*! \brief What closes a doorway. */
    enum class Door : quint8
    {
        /*! Nothing. */
        Opening,
        /*! A wooden door. */
        WoodDoor,
        /*! Bars around an opening. */
        Grates,
        /*! An iron door with a button on each side. */
        IronDoor,
    };

    /*! \brief A doorway out of a piece: forward, to the left or to the right. */
    enum class Exit : quint8
    {
        /*! Straight on, in the wall opposite the doorway in. */
        Forward = 0,
        /*! In the wall to the left. */
        Left = 1,
        /*! In the wall to the right. */
        Right = 2,
    };

    /*! \brief Where the doorways of a kind of piece lie in its frame: \c {x}, \c {y} and \c {z} of
        the middle cell of the doorway's lowest row. \qcfields */
    struct Doorway
    {
        Exit exit = Exit::Forward;
        int x = 0;
        int y = 0;
        int z = 0;
    };

    /*! \brief The size of a kind of piece in its frame (\c {width} across, \c {height},
        \c {depth} along) and where its doorway in (\c {doorX}, \c {doorY}) and the doorways out
        (\c {exits}, \c {exitCount} of them) lie. \qcfields */
    struct Shape
    {
        int width = 5;
        int height = 5;
        int depth = 7;
        int doorX = 2;
        int doorY = 1;
        std::array<Doorway, 3> exits{};
        int exitCount = 0;
    };

    /*! Returns the shape of \a kind; \a tall gives the library of two storeys. */
    [[nodiscard]] static Shape shapeOf(Kind kind, bool tall);

    /*! \brief A piece of \c {kind} walked into towards \c {facing}, filling \c {box}, its doorway
        in closed by \c {door}; \c {variant} picks the furnishing of crossings and libraries, and
        \c {opened} has a bit for each exit (Exit) a further piece leads on from. \qcfields */
    struct Piece
    {
        Kind kind = Kind::Corridor;
        Side facing = Side::North;
        QcBlockBox box;
        Door door = Door::Opening;
        int variant = 0;
        int opened = 0;

        /*! Returns the world cell \a x, \a y, \a z of its frame. */
        [[nodiscard]] QcBlockPos at(int x, int y, int z) const;
        /*! Returns the world box between the cells \a a and \a b of its frame (x, y, z). */
        [[nodiscard]] QcBlockBox span(QcBlockPos a, QcBlockPos b) const;
        /*! Returns the world side of \a exit. */
        [[nodiscard]] Side sideOf(Exit exit) const;
        /*! Returns the side to its left as one walks in. */
        [[nodiscard]] Side left() const { return static_cast<Side>((static_cast<int>(facing) + 3) % 4); }
        /*! Returns the side to its right as one walks in. */
        [[nodiscard]] Side right() const { return QcFortressPlan::clockwise(facing); }
        /*! Returns its shape. */
        [[nodiscard]] Shape shape() const { return shapeOf(kind, kind == Kind::Library && variant == 1); }
        /*! Returns whether a further piece leads on from \a exit. */
        [[nodiscard]] bool opens(Exit exit) const { return (opened & (1 << static_cast<int>(exit))) != 0; }
        /*! Returns the box of a piece of \a kind (\a tall: a library of two storeys) facing
            \a facing whose doorway in has its middle lowest cell at \a door. */
        [[nodiscard]] static QcBlockBox placed(Kind kind, bool tall, Side facing, QcBlockPos door);
    };

    /*! \brief Which loot a chest holds. */
    enum class Loot : quint8
    {
        /*! The loot of a chest in a corridor (corridorLoot). */
        Corridor,
        /*! The loot of the store room of a crossing (storeLoot). */
        Store,
        /*! The loot of a library (libraryLoot). */
        Library,
    };

    /*! \brief A chest at \c {at} with its front to \c {facing}, filled with \c {loot}.
        \qcfields */
    struct Chest // NOLINT(qc-duplicate-struct): the loot of a stronghold, another enum than QcBastionPlan::Loot
    {
        QcBlockPos at;
        Side facing = Side::North;
        Loot loot = Loot::Corridor;
    };

    /*! \brief An end portal frame at \c {at} facing \c {facing} (towards the middle of the
        portal), with an eye or not. \qcfields */
    struct Frame
    {
        QcBlockPos at;
        Side facing = Side::North;
        bool eye = false;
    };

    /*! The stronghold's number among the 128 of the world (QcStrongholdRings). */
    int index = 0;
    /*! The pieces, the starting staircase first. */
    QList<Piece> pieces;
    /*! The chests. */
    QList<Chest> chests;
    /*! The spawner of the portal room. */
    QList<QcBlockPos> spawners;
    /*! The twelve frames of the portal room. */
    QList<Frame> frames;
    /*! The columns its pieces fill. */
    QcXzRect bounds;

    /*! Returns whether a cell of the plan may lie in \a area. */
    [[nodiscard]] bool touches(QcXzRect area) const { return bounds.intersects(area); }
    /*! Returns whether a piece holds the cell \a at, its walls included. */
    [[nodiscard]] bool holds(QcBlockPos at) const
    {
        return std::ranges::any_of(pieces, [at](const Piece &piece) { return piece.box.contains(at); });
    }
    /*! Returns where it stands, for /locate and the eye: the cell over the floor at the foot of
        the starting staircase. */
    [[nodiscard]] QcBlockPos middle() const { return pieces.constFirst().at(2, 1, 3); }
};
