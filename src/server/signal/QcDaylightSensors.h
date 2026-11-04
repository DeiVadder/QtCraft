// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"

class QcServerBlocks;
class QcServerState;
class QcSignalTable;

/*! \brief The daylight sensors of the server (decision E-982): every
    QcSignalDefinition::daylightIntervalTicks each loaded sensor takes the strength the sky above it
    and the sun give.

    The sensors come from QcServerWorld::daylightSensors(), an index that chunks arriving and
    leaving and blocks being set keep up to date; a pass costs one look per sensor and scans no
    chunk. A sensor next to an unloaded chunk keeps its strength until the chunk is there: daylight
    never asks for a load ticket. */
class QcDaylightSensors
{
public:
    QcDaylightSensors(QcServerState &state, QcServerBlocks &blocks, const QcSignalTable &table);

    /*! Runs in every tick of the signal phase; only ticks divisible by the interval read the
        sensors. */
    void tick();
    /*! A player used the sensor \a block at \a pos: it changes between day and night and takes its
        new strength at once. */
    void turnRound(QcBlockPos pos, QcBlockId block);

    /*! Returns the strength (0..15) of a sensor as in the role model. \a skyLight is the light of
        its cell, \a darkening the levels the sky loses now (QcDayCycle::skyDarkening), \a sunAngle
        the sun's position (QcDayCycle::sunAngle). What is left of the light counts by the sun's
        height, taken a fifth of the way towards noon, so that the signal fades out only after
        sunset. \a inverted gives 15 minus the light, without looking at the sun. */
    [[nodiscard]] static int strength(int skyLight, int darkening, double sunAngle, bool inverted);

private:
    // The strength of a sensor at `pos` with the sky and the sun as they are now.
    [[nodiscard]] int strengthAt(QcBlockPos pos, bool inverted) const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    const QcSignalTable &m_table;
};
