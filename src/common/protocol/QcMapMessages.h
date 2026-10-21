#pragma once

// Protocol messages about maps (decision E-802). Part of the QcMessage variant (QcMessages.h).

#include "common/maps/QcMapMarker.h"

#include <QList>
#include <QPoint>
#include <QRect>

namespace QcMsg {

// Server -> client: what is new of map `id` for a player holding it (decision E-802): its place on
// the grid, the pixels of `area` row by row (QcMapPixel; an empty area sends none) and the players
// shown on it now (QcMapMarker). The first update of a map the client has not seen covers every
// pixel shown so far; later ones only the rectangle that changed since. The pixels travel
// compressed when that is shorter.
struct MapUpdate
{
    quint32 id = 0;
    quint8 scale = 0;
    QPoint centre;
    QRect area;
    QList<quint16> pixels;
    QList<QcMapMarker> markers;
};

} // namespace QcMsg
