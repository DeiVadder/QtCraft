#pragma once

#include "common/core/QcResult.h"
#include "common/protocol/QcMessages.h"

#include <QByteArray>

// Binary wire format: every frame is [quint16 type tag][payload] in QDataStream big-endian encoding.
// Transports deliver whole frames; framing on byte streams is the transport's job.
namespace QcProtocol {

// From this version on Hello starts with version, player name and client text, so a server of another
// version still reads who knocked (decision E-834).
inline constexpr quint32 firstVersionWithClientText = 65;
// Bumped whenever a message layout changes (history: decision E-368): 66 sends the threads of
// families with the content (decision E-839), 67 brings the admin channel (decision E-848), 68
// sends the speed of status effects with the content (decision E-838), 69 numbers the predicted
// block actions and acknowledges them (decision E-853), 70 brings the enchanting table and
// enchantments on stacks (decision E-864), 71 the anvil and the prior work on stacks (decision E-866),
// 72 the names of chests and machines (OpenMachine), content keys of chat lines (ChatMessage) and
// items that store their enchantments in the content (decision E-873), 73 the building part of a
// block (`partOf`) in the content (decision E-874), 74 the pause of the server (PauseRequest,
// ServerPause; decision E-877).
inline constexpr quint32 version = 74;
inline constexpr int maxFrameBytes = 32 * 1024 * 1024;

enum class Peer { Client, Server };

// Whether `sender` may send the message with this variant index; decode() rejects the rest.
bool isSentBy(std::size_t messageIndex, Peer sender);
// The number of the client's block prediction a FinishMining or UseItem carries (decision E-853);
// 0 for other messages and for actions the client predicted nothing for.
quint32 predictionSequence(const QcMessage &message);

QByteArray encode(const QcMessage &message);
// Decodes a frame received from `sender`; message types that peer must not send are rejected
// before their payload is read.
[[nodiscard]] QcResult<QcMessage> decode(const QByteArray &frame, Peer sender);

// Chunk encoding (palette plus run-length encoded indices per section), shared with persistence.
void writeChunk(QDataStream &stream, const QcChunk &chunk);
[[nodiscard]] QcResult<std::shared_ptr<QcChunk>> readChunk(QDataStream &stream);

} // namespace QcProtocol
