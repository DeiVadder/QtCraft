// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Payload encoders and decoders of the admin channel (QcAdminCodec.cpp, decision E-848), beside
// those of the other messages in QcProtocolCodec.h. Internal to the protocol.

#include "common/protocol/QcProtocolCodec.h"

namespace QcProtocolCodec {

/*! Writes the payload of \a m (QcMsg::AdminSubscribe) to \a stream. */
void encode(QDataStream &stream, QcMsg::AdminSubscribe m);
/*! Writes the payload of \a m (QcMsg::AdminRequest) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminRequest &m);
/*! Writes the payload of \a m (QcMsg::AdminResult) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminResult &m);
/*! Writes the payload of \a m (QcMsg::AdminStatus) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminStatus &m);
/*! Writes the payload of \a m (QcMsg::AdminHistory) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminHistory &m);
/*! Writes the payload of \a m (QcMsg::AdminPlayers) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminPlayers &m);
/*! Writes the payload of \a m (QcMsg::AdminChat) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminChat &m);
/*! Writes the payload of \a m (QcMsg::AdminCatalogue) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminCatalogue &m);
/*! Writes the payload of \a m (QcMsg::AdminLists) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminLists &m);
/*! Writes the payload of \a m (QcMsg::AdminSettings) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminSettings &m);
/*! Writes the payload of \a m (QcMsg::AdminLog) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminLog &m);
/*! Returns the QcMsg::AdminSubscribe read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSubscribe>, QDataStream &stream);
/*! Returns the QcMsg::AdminRequest read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminRequest>, QDataStream &stream);
/*! Returns the QcMsg::AdminResult read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminResult>, QDataStream &stream);
/*! Returns the QcMsg::AdminStatus read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminStatus>, QDataStream &stream);
/*! Returns the QcMsg::AdminHistory read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminHistory>, QDataStream &stream);
/*! Returns the QcMsg::AdminPlayers read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminPlayers>, QDataStream &stream);
/*! Returns the QcMsg::AdminChat read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminChat>, QDataStream &stream);
/*! Returns the QcMsg::AdminCatalogue read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminCatalogue>, QDataStream &stream);
/*! Returns the QcMsg::AdminLists read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLists>, QDataStream &stream);
/*! Returns the QcMsg::AdminSettings read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSettings>, QDataStream &stream);
/*! Returns the QcMsg::AdminLog read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLog>, QDataStream &stream);

} // namespace QcProtocolCodec
