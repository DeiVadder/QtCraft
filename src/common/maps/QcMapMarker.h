#pragma once

#include <QTypeInfo>
#include <QtGlobal>

// A player or an item frame shown on a map (decisions E-803, E-808): where, in half pixels from the
// map's middle (-128 at the west or north edge, 127 at the east or south edge), and which way the
// player looks or the frame faces, in sixteenths of a full turn clockwise from north. A player off
// the map stands at the edge as a dot (`onMap` false) like in the reference, so the holder sees
// where the map lies.
struct QcMapMarker
{
    // How many markers one map shows at most; more players are left out.
    static constexpr int maxCount = 32;

    qint8 x = 0;
    qint8 z = 0;
    quint8 rotation = 0;
    bool onMap = true;
    // The marker of the player who receives it.
    bool own = false;
    // An item frame that shows this map (drawn green; the only markers a framed map shows).
    bool frame = false;

    bool operator==(const QcMapMarker &) const = default;
};

Q_DECLARE_TYPEINFO(QcMapMarker, Q_PRIMITIVE_TYPE);
