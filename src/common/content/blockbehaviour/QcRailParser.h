// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

struct QcJsonFieldReader;

/*! \brief Parses the rail fields of blocks (decisions E-382, E-384): what a rail does to carts
    ("railAction") and the states its track is made of. */
namespace QcRailParser {

/*! Returns the "railAction" read by \a reader for \a block (read so far, with its "rail" field):
    none, boost or activate; only a rail may have one. Failures go to \a reader. */
[[nodiscard]] QcRailAction parseAction(const QcBlockDefinition &block, QcJsonFieldReader &reader);
/*! Checks the rail \a block and fails \a reader on problems. A rail needs the states its track
    is made of (decision E-382): the direction it faces and its shape; a rail without the shape
    \c {curve} never bends (decision E-384). A rail that acts on carts needs its \c {powered}
    state and never bends. A rail is neither solid nor a liquid. */
void checkStates(const QcBlockDefinition &block, QcJsonFieldReader &reader);

} // namespace QcRailParser
