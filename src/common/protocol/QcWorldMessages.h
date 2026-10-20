#pragma once

// Protocol messages about the world: chunks, block changes, mining, using items on blocks and sounds.
// Part of the QcMessage variant (QcMessages.h).

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/world/QcChunk.h"
#include "common/world/QcWeather.h"
#include "common/world/QcWorldLabel.h"

#include <QString>

#include <memory>

namespace QcMsg {

struct ChunkData
{
    std::shared_ptr<const QcChunk> chunk;
};

struct ChunkUnload
{
    QcChunkPos pos;
};

// Server -> client: the labels floating in a chunk (decision E-222), sent right after its ChunkData
// when there are any; they go with the chunk's ChunkUnload.
struct ChunkLabels
{
    QcChunkPos pos;
    QList<QcWorldLabel> labels;
};

// Server -> client: the creature kind shown spinning in the monster spawner at `pos` (decision
// E-520), sent after the ChunkData of its chunk and whenever it changes; `mob` is the kind's index
// in the content, -1 for none (an empty cage, or the spawner is gone).
struct SpawnerDisplay
{
    QcBlockPos pos;
    qint32 mob = -1;
};

// Server -> client: the items lying on the campfire at `pos` (decision E-772), sent after the
// ChunkData of its chunk and whenever they change; one content item id per slot, 0 for an empty one.
// An empty list: nothing lies on it (or it is gone).
struct CampfireDisplay
{
    static constexpr qsizetype maxItems = 4;

    QcBlockPos pos;
    QList<quint32> items;
};

struct BlockUpdate
{
    QcBlockPos pos;
    QcBlockId block = qcAirBlockId;
};

// Client -> server: the player starts hitting a block.
struct StartMining
{
    QcBlockPos pos;
};

// Client -> server: the player stopped hitting before the block broke.
struct CancelMining
{
};

// Client -> server: the client's timer says the block is broken; the server checks the time itself.
struct FinishMining
{
    QcBlockPos pos;
};

// Client -> server: use the held item on a block face (places blocks, opens doors). `hit` is the
// point the player aimed at inside the target block (0..1 per axis); it picks slab halves and the
// like.
struct UseItem
{
    QcBlockPos target;
    QcBlockFace face = QcBlockFace::Up;
    QcVec3 hit{0.5, 0.5, 0.5};
};

// Server -> client: a sound at a world position (block broken, creature hurt, furnace crackling).
// The client picks the loudness and the side from its own distance and view direction.
struct SoundEvent
{
    static constexpr qsizetype maxKeyLength = 256;
    static constexpr float maxVolume = 4.0f;
    static constexpr float minPitch = 0.25f;
    static constexpr float maxPitch = 4.0f;

    QString key;
    QcVec3 position;
    // Loudness relative to the sound's own (louder sounds also carry further) and playback speed.
    float volume = 1.0f;
    float pitch = 1.0f;
};

// Server -> client: the weather as this player sees it (decision E-395). `rain` and `thunder` are
// the strengths in 0..1, fading in and out; `climate` is where the player stands: dry, or rain
// below `climate.snowFrom` and snow from there up. Sent on joining, while the strengths change and
// when the player walks into another climate.
struct Weather
{
    float rain = 0.0f;
    float thunder = 0.0f;
    QcWeather::Climate climate;

    friend bool operator==(const Weather &, const Weather &) = default;
};

// Server -> client: lightning struck at `position` (the top of the column it hit). The client lights
// up the sky and plays the thunder after the time the sound takes to reach it.
struct Lightning
{
    QcVec3 position;
};

// Server -> client: blocks set together in one tick (an explosion's, decision E-604). The client
// applies them as one change, so each section is relit and meshed once.
struct BlockBatch
{
    // At most maxUpdates.
    QList<BlockUpdate> updates;

    static constexpr quint32 maxUpdates = 1u << 16;
};

// Server -> client: an explosion of `power` (QcBlastDefinition) at `centre` (decision E-604); the
// client shows its flash and smoke, the sound comes as a sound event.
struct Explosion
{
    QcVec3 centre;
    float power = 0.0f;
};

} // namespace QcMsg
