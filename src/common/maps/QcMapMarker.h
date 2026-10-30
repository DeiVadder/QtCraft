// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTypeInfo>
#include <QtGlobal>

/*! \brief A player or an item frame shown on a map (decisions E-803, E-808): where, in half pixels
    from the map's middle (-128 at the west or north edge, 127 at the east or south edge), and which
    way the player looks or the frame faces, in sixteenths of a full turn clockwise from north.

    A player off the map stands at the edge as a dot (\c {onMap} false) like in the reference, so
    the holder sees where the map lies. */
struct QcMapMarker
{
    /*! How many markers one map shows at most; more players are left out. */
    static constexpr int maxCount = 32;

    /*! East-west place in half pixels from the map's middle. */
    qint8 x = 0;
    /*! North-south place in half pixels from the map's middle. */
    qint8 z = 0;
    /*! Which way it looks or faces, in sixteenths of a turn clockwise from north. */
    quint8 rotation = 0;
    /*! False for a player off the map, shown at the edge as a dot. */
    bool onMap = true;
    /*! The marker of the player who receives it. */
    bool own = false;
    /*! An item frame that shows this map (drawn green; the only markers a framed map shows). */
    bool frame = false;

    bool operator==(const QcMapMarker &) const = default;
};

Q_DECLARE_TYPEINFO(QcMapMarker, Q_PRIMITIVE_TYPE);
