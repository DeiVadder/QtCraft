// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Payload encoders and decoders of the beacon's messages (QcBeaconCodec.cpp, decisions E-1142,
// E-1143), beside those of the other messages in QcProtocolCodec.h. Internal to the protocol.

#include "common/protocol/QcProtocolCodec.h"

namespace QcProtocolCodec {

/*! Writes the payload of \a m (QcMsg::OpenBeacon) to \a stream. */
void encode(QDataStream &stream, QcMsg::OpenBeacon m);
/*! Writes the payload of \a m (QcMsg::BeaconState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::BeaconState &m);
/*! Writes the payload of \a m (QcMsg::ChooseBeaconEffects) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ChooseBeaconEffects &m);
/*! Writes the payload of \a m (QcMsg::CloseBeacon) to \a stream. */
void encode(QDataStream &stream, QcMsg::CloseBeacon m);
/*! Writes the payload of \a m (QcMsg::BeaconBeam) to \a stream. */
void encode(QDataStream &stream, const QcMsg::BeaconBeam &m);
/*! Returns the QcMsg::OpenBeacon read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenBeacon>, QDataStream &stream);
/*! Returns the QcMsg::BeaconState read from \a stream, or why it is invalid (a level beyond
    QcBeacon::maxLevel, effect names longer than a content name). */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BeaconState>, QDataStream &stream);
/*! Returns the QcMsg::ChooseBeaconEffects read from \a stream, or why it is invalid (effect
    names longer than a content name). */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChooseBeaconEffects>, QDataStream &stream);
/*! Returns the QcMsg::CloseBeacon read from \a stream. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseBeacon>, QDataStream &stream);
/*! Returns the QcMsg::BeaconBeam read from \a stream, or why it is invalid (more than
    QcMsg::BeaconBeam::maxSections sections, a section without height). */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BeaconBeam>, QDataStream &stream);

} // namespace QcProtocolCodec
