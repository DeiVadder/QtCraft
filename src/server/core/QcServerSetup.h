// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/generators/QcWorldSetup.h"

class QcBlockUpdateQueue;
class QcServerState;

/*! \brief Applies a world generator's one-time setup to a world that was just created (decision
    E-221): time of day and game rules, persistent creatures, contents of chests and machines, block
    updates (queued like those of a command; they wait for their chunks) and growing saplings.

    Chunks with contents or saplings are generated on the spot. Marks the world as set up. */
namespace QcServerSetup {

/*! Applies \a setup to the world of \a state; block updates go to \a updates. */
void apply(QcServerState &state, QcBlockUpdateQueue &updates, const QcWorldSetup &setup);
/*! Applies what \a setup places in the current dimension of \a state: creatures, stands,
    vehicles, contents, signs and block updates (they go to \a updates), but not the time and the
    rules, which are the world's and belong to the overworld's generator. The way a dimension
    other than the overworld is set up the first time it opens (decision E-988). */
void applyContents(QcServerState &state, QcBlockUpdateQueue &updates, const QcWorldSetup &setup);

} // namespace QcServerSetup
