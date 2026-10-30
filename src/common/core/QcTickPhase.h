// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <array>

/*! \brief The phases of a server tick in the order QcServer::tick runs them, plus the messages the
    server handles between two ticks (decision E-225).

    The ids name them in the BENCH line (tick_<id>_avg_ms), in JSON and in the control interface. */
enum class QcTickPhase : quint8 {
    /*! Leaving players: their crafting grid returns, their file is written. */
    Players,
    /*! The autosave when due, and damaged chunks reported. */
    Saving,
    /*! Generated chunks taken over, waiting block updates of newly loaded chunks queued. */
    ChunkLoading,
    /*! Queued block updates (neighbour changes) and the contents spilled by blocks set outside a
        block change. */
    BlockUpdates,
    /*! Signal Dust circuits: plates, dust networks, components, pistons (decision E-335). */
    Signal,
    /*! Liquids flowing. */
    Liquids,
    /*! Fire taking its steps from its own queue (decision E-392). */
    Fire,
    /*! The weather's course, lightning and the weather sent to the players (decision E-395). */
    Weather,
    /*! Players' health and hunger, creature behaviour, spawning and despawning, contact damage. */
    Mobs,
    /*! Projectiles flying, hitting, sticking and picked up (decision E-425). */
    Projectiles,
    /*! Dropped items (saplings and seeds that plant themselves) and experience orbs. */
    Items,
    /*! Plants growing, and animals breeding and laying eggs. */
    Plants,
    /*! Falling blocks. */
    FallingBlocks,
    /*! Lit explosives counting down, explosions and the blocks they break (decision E-602). */
    Explosions,
    /*! Minecarts rolling along rails and their riders (decision E-382). */
    Minecarts,
    /*! Machines and kitchen blocks working, open screens, trading and workstations updated. */
    Machines,
    /*! Chunks sent to and unloaded from the clients, unneeded chunks dropped. */
    Streaming,
    /*! Entities, time, food, player state and debug data sent to the clients. */
    ClientSync,
    /*! Client messages handled since the previous tick (with the rate limits); not part of the
        tick. */
    Messages,
};

/*! \brief The number of the tick phases and their ids. */
namespace QcTickPhases {

/*! The number of phases, Messages included. */
inline constexpr int count = static_cast<int>(QcTickPhase::Messages) + 1;
/*! Phases inside a tick: all but Messages; their sum is the tick's duration. */
inline constexpr int inTick = count - 1;

/*! The id of each phase, in the order of the enum. */
inline constexpr std::array<const char *, count> ids{"players", "saving",   "chunkLoading",  "blockUpdates", "signal",
                                                     "liquids", "fire", "weather", "mobs", "projectiles", "items",         "plants",       "fallingBlocks",
                                                     "explosions", "minecarts", "machines", "streaming", "clientSync",  "messages"};

/*! Returns the id of \a phase. */
constexpr const char *id(QcTickPhase phase)
{
    return ids[static_cast<std::size_t>(phase)];
}

} // namespace QcTickPhases
