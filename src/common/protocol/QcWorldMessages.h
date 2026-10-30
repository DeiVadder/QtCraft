// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages about the world: chunks, block changes, mining, using items on blocks and sounds.
// Part of the QcMessage variant (QcMessages.h).

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "common/world/QcChunk.h"
#include "common/world/QcWeather.h"
#include "common/world/QcWorldLabel.h"

#include <QString>

#include <memory>
#include <optional>

/* QcMsg is documented in QcSessionMessages.h. */
namespace QcMsg {

/*! \brief Server -> client: a whole chunk, sent when it comes into the player's view. */
struct ChunkData
{
    /*! The chunk: its position and blocks. */
    std::shared_ptr<const QcChunk> chunk;
};

/*! \brief Server -> client: the client forgets the chunk at \c {pos}. */
struct ChunkUnload
{
    /*! The chunk's position. */
    QcChunkPos pos;
};

/*! \brief Server -> client: the labels floating in a chunk (decision E-222), sent right after
    its ChunkData when there are any; they go with the chunk's ChunkUnload. */
struct ChunkLabels
{
    /*! The chunk's position. */
    QcChunkPos pos;
    /*! Its labels. */
    QList<QcWorldLabel> labels;
};

/*! \brief Server -> client: the creature kind shown spinning in the monster spawner at \c {pos}
    (decision E-520), sent after the ChunkData of its chunk and whenever it changes; \c {mob} is
    the kind's index in the content, -1 for none (an empty cage, or the spawner is gone). */
struct SpawnerDisplay
{
    /*! The spawner's block. */
    QcBlockPos pos;
    /*! The creature kind shown (see the struct). */
    qint32 mob = -1;
};

/*! \brief Server -> client: the items lying on the campfire at \c {pos} (decision E-772), sent
    after the ChunkData of its chunk and whenever they change; one content item id per slot, 0 for
    an empty one.

    An empty list: nothing lies on it (or it is gone). */
struct CampfireDisplay
{
    /*! Slots of a campfire; the decoder refuses more items. */
    static constexpr qsizetype maxItems = 4;

    /*! The campfire's block. */
    QcBlockPos pos;
    /*! The item id per slot (see the struct). */
    QList<quint32> items;
};

/*! \brief Server -> client: the block at \c {pos} is now \c {block}. */
struct BlockUpdate
{
    /*! The block's position. */
    QcBlockPos pos;
    /*! The new block. */
    QcBlockId block = qcAirBlockId;
};

/*! \brief Client -> server: the player starts hitting a block. */
struct StartMining
{
    /*! The block hit. */
    QcBlockPos pos;
};

/*! \brief Client -> server: the player stopped hitting before the block broke. */
struct CancelMining
{
};

/*! \brief Client -> server: the client's timer says the block is broken; the server checks the time
    itself.

    \c {sequence} numbers the client's prediction of the break (decision E-853), 0 when it
    predicted nothing; the server answers it with a BlockAck. */
struct FinishMining
{
    /*! The block broken. */
    QcBlockPos pos;
    /*! The number of the client's prediction (see the struct). */
    quint32 sequence = 0;
};

/*! \brief Client -> server: use the held item on a block face (places blocks, opens doors).

    \c {hit} is the point the player aimed at inside the target block (0..1 per axis); it picks
    slab halves and the like. \c {sequence} numbers the client's prediction of the placed block
    (decision E-853), 0 when it predicted nothing; the server answers it with a BlockAck.
    \c {view} is the view direction the client predicted the placement with; the server places
    with it (facing, stairs), without it with the view of the player's latest input. */
struct UseItem
{
    /*! \brief Degrees; the server wraps yaw into [0, 360) and clamps pitch to [-90, 90]
        (positive is up). */
    struct View
    {
        /*! Horizontal view angle. */
        float yaw = 0.0F;
        /*! Vertical view angle. */
        float pitch = 0.0F;
    };

    /*! The block the item is used on. */
    QcBlockPos target;
    /*! The face of the target that was clicked. */
    QcBlockFace face = QcBlockFace::Up;
    /*! The point aimed at inside the target (see the struct). */
    QcVec3 hit{0.5, 0.5, 0.5};
    /*! The number of the client's prediction (see the struct). */
    quint32 sequence = 0;
    /*! The view direction of the prediction (see the struct). */
    std::optional<View> view = std::nullopt;
};

/*! \brief Server -> client: a sound at a world position (block broken, creature hurt, furnace
    crackling).

    The client picks the loudness and the side from its own distance and view direction. */
struct SoundEvent
{
    /*! Bounds the decoder checks: the key's length, the volume (from 0) and the pitch. \qcshared */
    static constexpr qsizetype maxKeyLength = 256;
    static constexpr float maxVolume = 4.0f;
    static constexpr float minPitch = 0.25f;
    static constexpr float maxPitch = 4.0f;

    /*! The sound's key in the content; not empty. */
    QString key;
    /*! Where it sounds. */
    QcVec3 position;
    /*! Loudness relative to the sound's own (louder sounds also carry further) and playback
        speed. \qcshared */
    float volume = 1.0f;
    float pitch = 1.0f;
};

/*! \brief Server -> client: the weather as this player sees it (decision E-395).

    \c {rain} and \c {thunder} are the strengths in 0..1, fading in and out; \c {climate} is
    where the player stands: dry, or rain below \c {climate.snowFrom} and snow from there up.
    Sent on joining, while the strengths change and when the player walks into another climate. */
struct Weather
{
    /*! Strengths of rain and thunder (see the struct). \qcshared */
    float rain = 0.0f;
    float thunder = 0.0f;
    /*! The climate where the player stands. */
    QcWeather::Climate climate;

    friend bool operator==(const Weather &, const Weather &) = default;
};

/*! \brief Server -> client: lightning struck at \c {position} (the top of the column it hit).

    The client lights up the sky and plays the thunder after the time the sound takes to reach
    it. */
struct Lightning
{
    /*! Where it struck. */
    QcVec3 position;
};

/*! \brief Server -> client: blocks set together in one tick (an explosion's, decision E-604).

    The client applies them as one change, so each section is relit and meshed once. */
struct BlockBatch
{
    /*! At most maxUpdates. */
    QList<BlockUpdate> updates;

    /*! The most updates in one batch; the decoder refuses more. */
    static constexpr quint32 maxUpdates = 1u << 16;
};

/*! \brief Server -> client: the player's action numbered \c {sequence} (FinishMining, UseItem) is
    handled, together with every earlier one (decision E-853).

    It follows the world sync of the next tick, so the blocks and the entities (drops) the server
    sent before it are its answer: the client drops its predictions up to the number and shows
    the server's blocks there. */
struct BlockAck
{
    /*! The number of the latest action handled. */
    quint32 sequence = 0;
};

/*! \brief Server -> client: an explosion of \c {power} (QcBlastDefinition) at \c {centre}
    (decision E-604); the client shows its flash and smoke, the sound comes as a sound event. */
struct Explosion
{
    /*! Where it exploded. */
    QcVec3 centre;
    /*! Its power. */
    float power = 0.0f;
};

} // namespace QcMsg
