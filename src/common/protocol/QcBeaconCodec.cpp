// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payload of the beacon's messages (QcBeaconMessages.h, decisions E-1142, E-1143).

#include "common/protocol/QcBeaconCodec.h"

#include "common/core/QcWorldConstants.h"

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

namespace {

// The longest effect name a message carries (a content name).
constexpr qsizetype maxEffectName = 128;

// Whether both effect names fit.
bool plausibleEffects(const QString &primary, const QString &secondary)
{
    return primary.size() <= maxEffectName && secondary.size() <= maxEffectName;
}

} // namespace

void encode(QDataStream &stream, QcMsg::OpenBeacon m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, const QcMsg::BeaconState &m)
{
    writeStack(stream, m.payment);
    stream << static_cast<quint8>(m.level) << m.primary << m.secondary;
}

void encode(QDataStream &stream, const QcMsg::ChooseBeaconEffects &m)
{
    stream << m.primary << m.secondary;
}

void encode(QDataStream &, QcMsg::CloseBeacon) {}

void encode(QDataStream &stream, const QcMsg::BeaconBeam &m)
{
    writePos(stream, m.pos);
    stream << static_cast<quint32>(m.sections.size());
    for (const QcBeamSection &section : m.sections) {
        stream << section.colour << static_cast<quint32>(section.height);
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenBeacon>, QDataStream &stream)
{
    QcMsg::OpenBeacon m;
    m.pos = readPos(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BeaconState>, QDataStream &stream)
{
    QcMsg::BeaconState m;
    m.payment = readStack(stream);
    quint8 level = 0;
    stream >> level >> m.primary >> m.secondary;
    if (level > QcBeacon::maxLevel || !plausibleEffects(m.primary, m.secondary)) {
        return qcError(u"Invalid beacon state"_s);
    }
    m.level = level;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChooseBeaconEffects>, QDataStream &stream)
{
    QcMsg::ChooseBeaconEffects m;
    stream >> m.primary >> m.secondary;
    if (!plausibleEffects(m.primary, m.secondary)) {
        return qcError(u"Invalid beacon choice"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseBeacon>, QDataStream &)
{
    return QcMsg::CloseBeacon{};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BeaconBeam>, QDataStream &stream)
{
    QcMsg::BeaconBeam m;
    m.pos = readPos(stream);
    quint32 count = 0;
    stream >> count;
    // The beacon stands in the world, and its sections reach no higher than the world's top.
    if (m.pos.y < 0 || m.pos.y >= QcWorld::worldHeight || count > static_cast<quint32>(QcMsg::BeaconBeam::maxSections)) {
        return qcError(u"Invalid beacon beam"_s);
    }
    qint64 reach = m.pos.y;
    for (quint32 i = 0; i < count; ++i) {
        quint32 colour = 0;
        quint32 height = 0;
        stream >> colour >> height;
        reach += height;
        if (height == 0 || reach > QcWorld::worldHeight) {
            return qcError(u"Invalid beacon beam"_s);
        }
        m.sections.append({colour & 0xFFFFFFu, static_cast<int>(height)});
    }
    return m;
}

} // namespace QcProtocolCodec
