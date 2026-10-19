#pragma once

#include <QtGlobal>

#include <array>

// The phases of a server tick in the order QcServer::tick runs them, plus the messages the server
// handles between two ticks (decision E-225). The ids name them in the BENCH line
// (tick_<id>_avg_ms), in JSON and in the control interface.
enum class QcTickPhase : quint8 {
    // Leaving players: their crafting grid returns, their file is written.
    Players,
    Saving,
    // Generated chunks taken over, waiting block updates of newly loaded chunks queued.
    ChunkLoading,
    BlockUpdates,
    // Signal Dust circuits: plates, dust networks, components, pistons (decision E-335).
    Signal,
    Liquids,
    // Fire taking its steps from its own queue (decision E-392).
    Fire,
    // The weather's course, lightning and the weather sent to the players (decision E-395).
    Weather,
    // Players' health and hunger, creature behaviour, spawning and despawning, contact damage.
    Mobs,
    // Projectiles flying, hitting, sticking and picked up (decision E-425).
    Projectiles,
    Items,
    Plants,
    FallingBlocks,
    // Lit explosives counting down, explosions and the blocks they break (decision E-602).
    Explosions,
    // Minecarts rolling along rails and their riders (decision E-382).
    Minecarts,
    Machines,
    // Chunks sent to and unloaded from the clients, unneeded chunks dropped.
    Streaming,
    // Entities, time, food, player state and debug data sent to the clients.
    ClientSync,
    // Client messages handled since the previous tick (with the rate limits); not part of the tick.
    Messages,
};

namespace QcTickPhases {

inline constexpr int count = int(QcTickPhase::Messages) + 1;
// Phases inside a tick: all but Messages; their sum is the tick's duration.
inline constexpr int inTick = count - 1;

inline constexpr std::array<const char *, count> ids{"players", "saving",   "chunkLoading",  "blockUpdates", "signal",
                                                     "liquids", "fire", "weather", "mobs", "projectiles", "items",         "plants",       "fallingBlocks",
                                                     "explosions", "minecarts", "machines", "streaming", "clientSync",  "messages"};

constexpr const char *id(QcTickPhase phase)
{
    return ids[std::size_t(phase)];
}

} // namespace QcTickPhases
