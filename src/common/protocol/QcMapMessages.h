// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages about maps (decision E-802). Part of the QcMessage variant (QcMessages.h).

#include "common/maps/QcMapMarker.h"

#include <QList>
#include <QPoint>
#include <QRect>

/* QcMsg is documented in QcSessionMessages.h. */
namespace QcMsg {

/*! \brief Server -> client: what is new of map \c {id} for a player holding it (decision E-802):
    its place on the grid, the pixels of \c {area} row by row (QcMapPixel; an empty area sends none)
    and the players shown on it now (QcMapMarker).

    The first update of a map the client has not seen covers every pixel shown so far; later ones
    only the rectangle that changed since. The pixels travel compressed when that is shorter. */
struct MapUpdate
{
    /*! The map's id. */
    quint32 id = 0;
    /*! The map's scale (QcMapData::scale). */
    quint8 scale = 0;
    /*! The block column at its middle (QcMapData::centre). */
    QPoint centre;
    /*! The rectangle of pixels sent. */
    QRect area;
    /*! The pixels of \c {area}, row by row. */
    QList<quint16> pixels;
    /*! The players shown on it. */
    QList<QcMapMarker> markers;
};

} // namespace QcMsg
