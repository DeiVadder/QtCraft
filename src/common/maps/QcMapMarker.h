#pragma once

#include <QTypeInfo>
#include <QtGlobal>

// A player shown on a map (decision E-803): where, in half pixels from the map's middle (-128 at
// the west or north edge, 127 at the east or south edge), and which way the player looks, in
// sixteenths of a full turn clockwise from north. A player off the map stands at the edge as a dot
// (`onMap` false) like in the reference, so the holder sees where the map lies.
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

    bool operator==(const QcMapMarker &) const = default;
};

Q_DECLARE_TYPEINFO(QcMapMarker, Q_PRIMITIVE_TYPE);
