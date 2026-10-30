// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

/*! \brief Turns block states with a structure (decision E-219): \c {turns} quarter turns clockwise
    seen from above (north becomes east).

    Any property value "north", "east", "south" or "west" turns along (facing, attach), the
    connection properties named after the four sides swap their values (fences, walls, panes) and
    "axis" swaps x and z on odd turns (logs), and a "rotation" with 16 values (a standing sign's
    sixteenths of a turn) adds four per turn. Other properties stay. A turned value the block
    does not have keeps the old one, with a warning once per block and value. */
namespace QcStateRotation {

/*! Returns \a direction ("north", "east", "south" or "west") turned by \a turns quarter turns
    clockwise (negative turns go counter-clockwise); any other text comes back unchanged. */
QString turnedDirection(const QString &direction, int turns);
/*! Returns the block state \a state of \a content turned by \a turns quarter turns clockwise;
    an invalid state, or a multiple of four turns, comes back unchanged. */
QcBlockId turned(const QcContentRegistry &content, QcBlockId state, int turns);

} // namespace QcStateRotation
