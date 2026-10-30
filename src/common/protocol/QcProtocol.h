// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/protocol/QcMessages.h"

#include <QByteArray>

/*! \brief Binary wire format: every frame is [quint16 type tag][payload] in QDataStream big-endian
    encoding.

    Transports deliver whole frames; framing on byte streams is the transport's job. */
namespace QcProtocol {

/*! From this version on Hello starts with version, player name and client text, so a server of
    another version still reads who knocked (decision E-834). */
inline constexpr quint32 firstVersionWithClientText = 65;
/*! Bumped whenever a message layout changes (history: decision E-368): 66 sends the threads of
    families with the content (decision E-839), 67 brings the admin channel (decision E-848), 68
    sends the speed of status effects with the content (decision E-838), 69 numbers the predicted
    block actions and acknowledges them (decision E-853), 70 brings the enchanting table and
    enchantments on stacks (decision E-864), 71 the anvil and the prior work on stacks (decision
    E-866), 72 the names of chests and machines (OpenMachine), content keys of chat lines
    (ChatMessage) and items that store their enchantments in the content (decision E-873), 73 the
    building part of a block (\c {partOf}) in the content (decision E-874), 74 the pause of the
    server (PauseRequest, ServerPause; decision E-877), 75 runs every network connection over TLS
    (decision E-872; the messages are unchanged), 76 the band-pass, volume variation and drive of
    sounds in the content (decision E-879), 77 map centres and debug goals over the whole
    technical bound of the world (decision E-883), 78 the rune seed of the enchanting screen
    (EnchantingState::runeSeed, decision E-884), 79 whether a block is a bed in the content
    (decision E-885), 80 the latency probes (LatencyProbe, LatencyEcho) and the round trip of
    each player in the PlayerList (decision E-886), 81 the group and the names of each command in
    the AdminCatalogue (decision E-911), 82 boats: the entity kind Boat, the boat item's
    \c {boatBlock} in the content and the driven boat's state in PlayerState, the Rowing bit of a
    boat whose driver paddles (decision E-920), 83 the advancements: their definitions in the
    content and the player's progress (AdvancementProgress, decision E-923). */
inline constexpr quint32 version = 83;
/*! The largest frame decode() accepts. */
inline constexpr int maxFrameBytes = 32 * 1024 * 1024;

/*! The two ends of a connection. */
enum class Peer {
    /*! The client. */
    Client,
    /*! The server. */
    Server
};

/*! Returns whether \a sender may send the message with the variant index \a messageIndex;
    decode() rejects the rest. */
bool isSentBy(std::size_t messageIndex, Peer sender);
/*! Returns the number of the client's block prediction \a message carries if it is a FinishMining
    or UseItem (decision E-853); 0 for other messages and for actions the client predicted nothing
    for. */
quint32 predictionSequence(const QcMessage &message);

/*! Returns the frame of \a message: its type tag and payload. */
QByteArray encode(const QcMessage &message);
/*! Returns the message of \a frame received from \a sender, or why it is invalid: too large,
    empty, of an unknown type or one that peer must not send (rejected before its payload is
    read), undecodable, truncated or with trailing bytes. */
[[nodiscard]] QcResult<QcMessage> decode(const QByteArray &frame, Peer sender);

/*! Writes \a chunk to \a stream: its position, then per section a palette plus run-length
    encoded indices; shared with persistence. */
void writeChunk(QDataStream &stream, const QcChunk &chunk);
/*! Returns the chunk read from \a stream in the encoding of writeChunk(), or why its data is
    corrupt. */
[[nodiscard]] QcResult<std::shared_ptr<QcChunk>> readChunk(QDataStream &stream);

} // namespace QcProtocol
