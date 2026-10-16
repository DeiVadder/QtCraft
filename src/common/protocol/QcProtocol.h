#pragma once

#include "QcMessages.h"
#include "QcResult.h"

#include <QByteArray>

// Binary wire format: every frame is [quint16 type tag][payload] in QDataStream big-endian encoding.
// Transports deliver whole frames; framing on byte streams is the transport's job.
namespace QcProtocol {

// Bumped whenever a message layout changes.
inline constexpr quint32 version = 41;
inline constexpr int maxFrameBytes = 32 * 1024 * 1024;

enum class Peer { Client, Server };

// Whether `sender` may send the message with this variant index; decode() rejects the rest.
bool isSentBy(std::size_t messageIndex, Peer sender);

QByteArray encode(const QcMessage &message);
// Decodes a frame received from `sender`; message types that peer must not send are rejected
// before their payload is read.
[[nodiscard]] QcResult<QcMessage> decode(const QByteArray &frame, Peer sender);

// Chunk encoding (palette plus run-length encoded indices per section), shared with persistence.
void writeChunk(QDataStream &stream, const QcChunk &chunk);
[[nodiscard]] QcResult<std::shared_ptr<QcChunk>> readChunk(QDataStream &stream);

} // namespace QcProtocol
