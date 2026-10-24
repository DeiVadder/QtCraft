// Payloads of the session messages (QcSessionMessages.h): handshake, content, chat and world time.

#include "common/protocol/QcProtocolCodec.h"

#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcStreamLimits.h"

#include <QIODevice>

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::Hello &m)
{
    stream << m.protocolVersion << m.playerName << m.client << m.appearance;
}

void encode(QDataStream &stream, const QcMsg::Welcome &m)
{
    stream << m.protocolVersion << m.playerEntityId << m.worldName << m.seed << m.yaw << m.pitch << m.restoredView << m.viewDistance << m.spawnX << m.spawnZ;
}

void encode(QDataStream &stream, const QcMsg::ContentSync &m)
{
    Q_ASSERT(m.content);
    m.content->writeTo(stream);
}

void encode(QDataStream &stream, const QcMsg::Disconnect &m)
{
    stream << m.reason << m.key << static_cast<quint32>(m.arguments.size());
    for (const QString &argument : m.arguments) {
        stream << argument;
    }
}

void encode(QDataStream &stream, const QcMsg::ChatInput &m)
{
    stream << m.text;
}

void encode(QDataStream &stream, const QcMsg::ChatMessage &m)
{
    stream << m.text << m.key << static_cast<quint32>(m.arguments.size());
    for (const QString &argument : m.arguments) {
        stream << argument;
    }
    stream << static_cast<quint32>(m.contentKeys.size());
    for (const QString &key : m.contentKeys) {
        stream << key;
    }
}

void encode(QDataStream &stream, const QcMsg::WorldTime &m)
{
    stream << m.time << m.dayLength << m.running << m.speed;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Hello>, QDataStream &stream)
{
    QcMsg::Hello m;
    stream >> m.protocolVersion >> m.playerName;
    if (m.playerName.size() > QcMsg::Hello::maxNameLength) {
        return qcError(u"Player name too long"_s);
    }
    if (m.protocolVersion >= QcProtocol::firstVersionWithClientText) {
        stream >> m.client;
        if (m.client.size() > QcMsg::Hello::maxClientLength) {
            return qcError(u"Client text too long"_s);
        }
    }

    // Other versions may lay out the rest differently: skip it so the server can report the version
    // mismatch instead of a decode error.
    if (m.protocolVersion != QcProtocol::version) {
        stream.skipRawData(static_cast<int>(stream.device()->bytesAvailable()));
        return m;
    }

    std::optional<QcAppearance> appearance = readAppearance(stream);
    if (!appearance) {
        return qcError(u"Invalid appearance"_s);
    }
    m.appearance = std::move(*appearance);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Welcome>, QDataStream &stream)
{
    QcMsg::Welcome m;
    stream >> m.protocolVersion >> m.playerEntityId >> m.worldName >> m.seed >> m.yaw >> m.pitch
           >> m.restoredView >> m.viewDistance >> m.spawnX >> m.spawnZ;
    if (!std::isfinite(m.yaw) || !std::isfinite(m.pitch)) {
        return qcError(u"Non-finite view direction"_s);
    }
    if (m.viewDistance < 0 || m.viewDistance > QcMsg::Welcome::maxViewDistance) {
        return qcError(u"Invalid view distance"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ContentSync>, QDataStream &stream)
{
    QcResult<QcContentRegistry> content = QcContentRegistry::readFrom(stream);
    if (!content) {
        return qcError(content.error());
    }
    return QcMsg::ContentSync{std::make_shared<const QcContentRegistry>(std::move(*content))};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Disconnect>, QDataStream &stream)
{
    QcMsg::Disconnect m;
    stream >> m.reason >> m.key;
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, QcMsg::Disconnect::maxArguments);
    if (!countRead) {
        return qcError(u"Too many disconnect arguments"_s);
    }
    const quint32 count = *countRead;

    for (quint32 i = 0; i < count; ++i) {
        QString argument;
        stream >> argument;
        m.arguments.append(argument);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChatInput>, QDataStream &stream)
{
    QcMsg::ChatInput m;
    stream >> m.text;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChatMessage>, QDataStream &stream)
{
    QcMsg::ChatMessage m;
    stream >> m.text >> m.key;
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, QcMsg::ChatMessage::maxArguments);
    if (!countRead) {
        return qcError(u"Too many chat arguments"_s);
    }
    const quint32 count = *countRead;

    for (quint32 i = 0; i < count; ++i) {
        QString argument;
        stream >> argument;
        m.arguments.append(argument);
    }

    const std::optional<quint32> keysRead = QcStreamLimits::readCount(stream, QcMsg::ChatMessage::maxArguments);
    if (!keysRead || (*keysRead != 0 && *keysRead != count)) {
        return qcError(u"Invalid chat content keys"_s);
    }
    for (quint32 i = 0; i < *keysRead; ++i) {
        QString key;
        stream >> key;
        if (key.size() > QcMsg::ChatMessage::maxContentKey) {
            return qcError(u"Invalid chat content keys"_s);
        }
        m.contentKeys.append(key);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::WorldTime>, QDataStream &stream)
{
    QcMsg::WorldTime m;
    stream >> m.time >> m.dayLength >> m.running >> m.speed;
    if (!std::isfinite(m.time) || !std::isfinite(m.dayLength) || m.dayLength <= 0.0 || !std::isfinite(m.speed) || m.speed < 0.0
        || m.speed > QcMsg::WorldTime::maxSpeed) {
        return qcError(u"Invalid world time"_s);
    }
    return m;
}

void encode(QDataStream &stream, QcMsg::Ping m)
{
    stream << m.nonce;
}

void encode(QDataStream &stream, QcMsg::Pong m)
{
    stream << m.nonce;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Ping>, QDataStream &stream)
{
    QcMsg::Ping m;
    stream >> m.nonce;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Pong>, QDataStream &stream)
{
    QcMsg::Pong m;
    stream >> m.nonce;
    return m;
}

void encode(QDataStream &stream, QcMsg::PauseRequest m)
{
    stream << m.paused;
}

void encode(QDataStream &stream, QcMsg::ServerPause m)
{
    stream << m.paused;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PauseRequest>, QDataStream &stream)
{
    QcMsg::PauseRequest m;
    stream >> m.paused;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ServerPause>, QDataStream &stream)
{
    QcMsg::ServerPause m;
    stream >> m.paused;
    return m;
}

} // namespace QcProtocolCodec
