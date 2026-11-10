// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

class QcServerDimension;
class QcServerState;
struct QcServerClient;

/*! \brief Moving a player to a position, in his dimension or another one (decision E-988): the
    server's side of a teleport, a respawn and later of a portal. */
namespace QcPlayerTravel {

/*! Moves \a player of \a state to \a position in \a dimension: his screens end, out of his
    minecart or boat, out of his dimension's lists and into those of \a dimension, with the chunk
    there loaded at once so he does not fall through it. The client takes the position from the
    next PlayerState. */
void move(QcServerState &state, QcServerClient &player, QcServerDimension &dimension, const QcVec3 &position);

} // namespace QcPlayerTravel
