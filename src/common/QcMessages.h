#pragma once

// Protocol messages. Plain data structs, grouped here on purpose: together they form the QcMessage
// variant whose alternative index is the wire type tag. New messages are appended at the end.

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcEntityMotion.h"
#include "QcPlayerInput.h"

#include <QString>

#include <memory>
#include <variant>

namespace QcMsg {

// Client -> server: first message on every connection.
struct Hello
{
    quint32 protocolVersion = 0;
    QString playerName;
};

// Server -> client: accepted connection.
struct Welcome
{
    quint32 protocolVersion = 0;
    quint32 playerEntityId = 0;
    QString worldName;
    qint64 seed = 0;
};

// Server -> client: all content definitions; runtime block ids are the indices in this registry.
struct ContentSync
{
    std::shared_ptr<const QcContentRegistry> content;
};

struct ChunkData
{
    std::shared_ptr<const QcChunk> chunk;
};

struct ChunkUnload
{
    QcChunkPos pos;
};

struct BlockUpdate
{
    QcBlockPos pos;
    QcBlockId block = qcAirBlockId;
};

// Client -> server: one tick of movement intent.
struct PlayerInput
{
    QcPlayerInput input;
};

// Server -> client: authoritative state of the receiving player.
struct PlayerState
{
    quint32 lastInputSequence = 0;
    QcEntityMotion motion;
};

struct Disconnect
{
    QString reason;
};

} // namespace QcMsg

using QcMessage = std::variant<QcMsg::Hello, QcMsg::Welcome, QcMsg::ContentSync, QcMsg::ChunkData, QcMsg::ChunkUnload,
                               QcMsg::BlockUpdate, QcMsg::PlayerInput, QcMsg::PlayerState, QcMsg::Disconnect>;
