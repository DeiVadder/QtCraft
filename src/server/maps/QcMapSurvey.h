// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/maps/QcMapData.h"

#include <QList>
#include <QPointF>

#include <optional>

class QcServerWorld;

/*! \brief How a map is revealed (decision E-800), as in the reference: a player holding a map looks
    at the pixels within revealRadius blocks of where it stands, each tick at every sweep-th column
    of pixels (another one each tick), so a map fills in over sweep ticks while its holder walks
    and each tick costs little.

    A pixel shows the most common top block of the columns it covers (one column at scale 0;
    blocks drawn nowhere, like the barrier, are looked through; no block at all: blank). Land is
    shaded by the mean height of its columns against the pixel north of it (flat when that one is
    blank or not loaded), water by the mean liquid depth over the pixel's columns, both with a
    fine checker pattern that deep water no longer shows; pixels with a column in a chunk that is
    not loaded stay as they are. */
namespace QcMapSurvey {

/*! A holder looks at the pixels within this many blocks of where it stands. */
inline constexpr int revealRadius = 128;
/*! Each tick a holder looks at every sweep-th column of pixels (by the tick), so a map fills in
    over sweep ticks. */
inline constexpr int sweep = 16;
/*! Water this deep or deeper counts as deep (no checker pattern any more). */
inline constexpr int deepWater = 10;

/*! Returns the pixels of \a map that a holder standing at \a holder (block x, z) looks at in
    tick \a tick. */
[[nodiscard]] QList<QPoint> pixelsDue(const QcMapData &map, QPointF holder, quint64 tick);
/*! Returns what pixel \a pixel of \a map shows now in \a world (QcMapPixel); std::nullopt when a
    chunk it covers is not loaded. */
[[nodiscard]] std::optional<quint16> pixelOf(const QcServerWorld &world, const QcMapData &map, QPoint pixel);

/*! \brief One pixel looked at: where it is and what it shows now (QcMapPixel). \qcfields */
struct Surveyed
{
    QPoint pixel;
    quint16 value = 0;
};
/*! Returns what \a pixels of \a map (in the order of pixelsDue) show now in \a world, leaving
    out those over unloaded chunks; the columns of a pixel are counted once, also as the north
    neighbour of the pixel after it. */
[[nodiscard]] QList<Surveyed> survey(const QcServerWorld &world, const QcMapData &map, const QList<QPoint> &pixels);

} // namespace QcMapSurvey
