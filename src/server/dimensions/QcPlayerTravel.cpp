// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcPlayerTravel.h"

#include "server/chunks/QcChunkStreaming.h"
#include "server/core/QcServerState.h"

namespace QcPlayerTravel {

void move(QcServerState &state, QcServerClient &player, QcServerDimension &dimension, const QcVec3 &position)
{
    {
        const QcServerState::Scope here(state, state.dimensionOf(player));
        // What he had open stays behind; what lay in a grid falls here, not where he goes.
        state.closeScreens(player);
        // A rider or driver is moved out of his minecart or boat (decisions E-383, E-920).
        if (QcMinecart *cart = state.entities().minecarts().ridden(player.id)) {
            cart->rider = 0;
            cart->rest.reset();
        }
        if (QcBoat *boat = state.entities().boats().drivenBy(player.id)) {
            boat->driver = 0;
        }
    }

    if (&dimension != &state.dimensionOf(player)) {
        state.moveClient(player, dimension);
    }
    dimension.world().loadChunk(QcChunkStreaming::chunkOf(position));
    player.motion.position = position;
    player.motion.velocity = {};
    player.previousPosition.reset();
    player.fallPeak = position.y;
    // Inputs from before the move no longer move with old effects (decision E-992).
    player.movementEpochs.forgetPast();
    player.mining.pos.reset();
    player.stateDirty = true;
}

} // namespace QcPlayerTravel
