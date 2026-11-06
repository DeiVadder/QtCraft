// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockBox.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcXzRect.h"

#include <QList>

#include <algorithm>
#include <array>
#include <utility>

/*! \brief One fortress of the nether as QcFortressBuilder writes it (decision E-1070): rooms joined
    by straight passages, all from the plan and the terrain noise alone, so every chunk writes its
    own cells without looking at its neighbours. Cells are world positions.

    A room's and a passage's "floor" is the cell of the floor block; one stands one cell above
    it. */
struct QcFortressPlan
{
    /*! The free cells over a passage's floor, and over a room's. \qcshared */
    static constexpr int passageHeight = 4;
    static constexpr int roomHeight = 5;
    /*! The difference between the floors of the two levels. */
    static constexpr int levelRise = 7;
    /*! The cells from a passage's middle lane to its wall or parapet. */
    static constexpr int passageHalf = 2;

    /*! \brief A side of a room, with the number of quarter turns from north. */
    enum class Side : quint8
    {
        /*! Towards -z. */
        North = 0,
        /*! Towards +x. */
        East = 1,
        /*! Towards +z. */
        South = 2,
        /*! Towards -x. */
        West = 3,
    };

    /*! Returns the unit step (x, z) towards \a side. */
    static constexpr std::pair<int, int> stepOf(Side side)
    {
        constexpr std::array<std::pair<int, int>, 4> steps{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
        return steps[static_cast<std::size_t>(side)];
    }
    /*! Returns the side opposite to \a side. */
    static constexpr Side opposite(Side side) { return static_cast<Side>((static_cast<int>(side) + 2) % 4); }
    /*! Returns the side a quarter turn clockwise of \a side. */
    static constexpr Side clockwise(Side side) { return static_cast<Side>((static_cast<int>(side) + 1) % 4); }
    /*! Returns the cell \a out cells from \a from towards \a side and \a along cells a quarter
        turn clockwise of it. */
    static QcBlockPos onSide(QcBlockPos from, Side side, int out, int along)
    {
        const auto [outX, outZ] = stepOf(side);
        const auto [alongX, alongZ] = stepOf(clockwise(side));
        return from.offset(outX * out + alongX * along, 0, outZ * out + alongZ * along);
    }

    /*! \brief What stands in a room. */
    enum class RoomKind : quint8
    {
        /*! Passages meet, nothing more. */
        Crossing,
        /*! A hall over both levels with a flight of stairs along one wall. */
        Stairwell,
        /*! An open terrace with the spawner on a dais. */
        Platform,
        /*! A hall with four beds of soil and crops around a chest. */
        Garden,
    };

    /*! \brief A square room around \c {centre}, its outer walls \c {half} cells from the middle.
        \qcfields */
    struct Room
    {
        RoomKind kind = RoomKind::Crossing;
        QcBlockPos centre;
        int half = 4;
        bool roofed = true;
        /*! The sides with a passage on its floor, and on the upper floor of a stairwell. */
        std::array<bool, 4> doors{};
        std::array<bool, 4> upperDoors{};
        /*! The wall of a stairwell's flight: no passage leaves the upper floor there. */
        Side flight = Side::North;

        /*! Returns the highest floor of the room. */
        [[nodiscard]] int topFloor() const { return centre.y + (kind == RoomKind::Stairwell ? levelRise : 0); }
        /*! Returns the box of its floor, walls and roof. */
        [[nodiscard]] QcBlockBox shell() const
        {
            return {{centre.x - half, centre.y, centre.z - half}, {centre.x + half, topFloor() + roomHeight + 1, centre.z + half}};
        }
    };

    /*! \brief A straight passage of \c {length} cells from \c {start} (the floor cell of its first
        cell in the middle lane) towards \c {side}, climbing \c {rise} cells in the middle of its
        length. \qcfields */
    struct Passage
    {
        QcBlockPos start;
        Side side = Side::North;
        int length = 0;
        int rise = 0;
        bool roofed = true;

        /*! Returns how far the floor has climbed at the cell \a along. */
        [[nodiscard]] int climbed(int along) const { return std::clamp(along - (length - rise) / 2 + 1, 0, rise); }
        /*! Returns the floor cell \a along the passage, \a lateral cells to the right of the
            lane. */
        [[nodiscard]] QcBlockPos floorAt(int along, int lateral) const
        {
            const auto [stepX, stepZ] = stepOf(side);
            const auto [rightX, rightZ] = stepOf(clockwise(side));
            return start.offset(stepX * along + rightX * lateral, climbed(along), stepZ * along + rightZ * lateral);
        }
        /*! Returns the box of its floors, walls and roof. */
        [[nodiscard]] QcBlockBox shell() const
        {
            const QcBlockPos a = floorAt(0, -passageHalf);
            const QcBlockPos b = floorAt(length - 1, passageHalf);
            return {{std::min(a.x, b.x), std::min(a.y, b.y) - 1, std::min(a.z, b.z)},
                    {std::max(a.x, b.x), std::max(a.y, b.y) + passageHeight + 1, std::max(a.z, b.z)}};
        }
    };

    /*! \brief A chest at \c {at}, its front towards \c {facing}. \qcfields */
    struct Chest
    {
        QcBlockPos at;
        Side facing = Side::North;
    };

    /*! The region of the plan. \qcshared */
    int regionX = 0;
    int regionZ = 0;
    /*! The rooms, the first in the middle of the net. */
    QList<Room> rooms;
    /*! The passages. */
    QList<Passage> passages;
    /*! The chests. */
    QList<Chest> chests;
    /*! The spawner of the platform. */
    QList<QcBlockPos> spawners;
    /*! The first cells of the piers: each goes down from there to the rock. */
    QList<QcBlockPos> piers;
    /*! The boxes of the rooms and passages as the spawn area takes them (decision E-1074): each
        part's walls and roof, a cell around it and down to where its piers stand. */
    QList<QcBlockBox> parts;
    /*! The columns the fortress and its seals may touch. */
    QcXzRect bounds;

    /*! Returns whether a cell of the plan may lie in \a area. */
    [[nodiscard]] bool touches(QcXzRect area) const { return bounds.intersects(area); }
    /*! Returns the middle of the net: where it stands, for /locate. */
    [[nodiscard]] QcBlockPos middle() const { return rooms.constFirst().centre.offset(0, 1, 0); }
};
