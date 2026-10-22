#pragma once

#include "common/core/QcResult.h"
#include "common/protocol/QcMessages.h"

#include <QByteArray>

// Binary wire format: every frame is [quint16 type tag][payload] in QDataStream big-endian encoding.
// Transports deliver whole frames; framing on byte streams is the transport's job.
namespace QcProtocol {

// From this version on Hello starts with version, player name and client text, so a server of another
// version still reads who knocked (decision E-834). It is the number of the version that brought the
// client text: renumbering that change (a merge after another bump) changes this line, not `version`.
inline constexpr quint32 firstVersionWithClientText = 65;
// Bumped whenever a message layout changes: 66 sends the threads of families with the content
// (decision E-839).
inline constexpr quint32 version = 66;
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
