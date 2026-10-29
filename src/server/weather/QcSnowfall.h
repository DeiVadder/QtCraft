// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"


#include <optional>

class QcServerBlocks;
class QcServerState;
class QcServerWeather;

// Snow settling on the ground (decision E-396). While it snows, every tick looks at a few random
// columns near each player (`columnsPerTick` of the content's weather, within `reach` blocks) and
// puts a snow layer on top of a column where snow falls on it (QcServerWeather::isSnowingAt: a cold
// climate or high up, the open sky, a loaded column), the cell is air and the block below is opaque
// with a full top face (not on ice, glass, leaves or another layer). The cost per tick is bounded by
// the players, not by the world; snow piles up over minutes like in the reference game and never
// fills the world: one layer per column, no layer under a roof. Snow does not melt (the reference
// game melts it only by block light, which this game leaves out).
class QcSnowfall
{
public:
    // Columns within this many blocks of a player (each axis) get snow.
    static constexpr int reach = 48;

    QcSnowfall(QcServerState &state, QcServerBlocks &blocks, const QcServerWeather &weather);

    // One simulated tick.
    void tick();
    // Snow layers put down since the server started (tests, measurements).
    quint64 layers() const { return m_layers; }
    // Columns looked at since the server started (tests, measurements).
    quint64 columns() const { return m_columns; }

private:
    // Purpose of the snowfall's draws (QcWeatherCycle::Draws).
    static constexpr quint64 snowRandom = 2;

    // Puts `layer` on the column (x, z) if it may lie there; true when it did.
    bool settle(int x, int z, QcBlockId layer);

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    const QcServerWeather &m_weather;
    std::optional<QcBlockId> m_layer;
    quint64 m_layers = 0;
    quint64 m_columns = 0;
};
