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

} // namespace QcMsg
