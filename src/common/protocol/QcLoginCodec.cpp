// Payloads of the login messages (QcLoginMessages.h): challenge, response and the player list.

#include "common/protocol/QcProtocolCodec.h"

#include "common/protocol/QcStreamLimits.h"

using namespace Qt::StringLiterals;

namespace {

void writePacks(QDataStream &stream, const QList<QcPackFingerprint> &packs)
{
    stream << quint32(packs.size());
    for (const QcPackFingerprint &pack : packs) {
        stream << pack.id << pack.version << pack.hash;
    }
}

[[nodiscard]] std::optional<QList<QcPackFingerprint>> readPacks(QDataStream &stream)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, quint32(QcMsg::LoginChallenge::maxPacks));
    if (!countRead) {
        return std::nullopt;
    }
    const quint32 count = *countRead;

    QList<QcPackFingerprint> packs;
    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        QcPackFingerprint pack;
        stream >> pack.id >> pack.version >> pack.hash;
        const qsizetype limit = QcPackFingerprint::maxTextLength;
        if (pack.id.size() > limit || pack.version.size() > limit || pack.hash.size() > limit) {
            return std::nullopt;
        }
        packs.append(pack);
    }
    return stream.status() == QDataStream::Ok ? std::optional(std::move(packs)) : std::nullopt;
}

bool withinProofSize(const QByteArray &bytes)
{
    return bytes.size() <= QcMsg::LoginResponse::maxProofBytes;
}

} // namespace

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::LoginChallenge &m)
{
    stream << m.serverName << m.motd << m.nonce << m.salt << m.passwordRequired << m.identityKnown << m.maxViewDistance;
    writePacks(stream, m.packs);
}

void encode(QDataStream &stream, const QcMsg::LoginResponse &m)
{
    stream << m.passwordProof << m.identityProof << m.identity << m.viewDistance;
    writePacks(stream, m.packs);
}

void encode(QDataStream &stream, const QcMsg::PlayerList &m)
{
    stream << quint32(m.players.size());
    for (const QcMsg::PlayerList::Entry &entry : m.players) {
        stream << entry.entityId << entry.name << entry.operatorRights << entry.spectator;
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LoginChallenge>, QDataStream &stream)
{
    QcMsg::LoginChallenge m;
    stream >> m.serverName >> m.motd >> m.nonce >> m.salt >> m.passwordRequired >> m.identityKnown >> m.maxViewDistance;
    const qsizetype text = QcMsg::LoginChallenge::maxTextLength;
    if (m.serverName.size() > text || m.motd.size() > text) {
        return qcError(u"Server name or message too long"_s);
    }
    if (m.nonce.size() != QcMsg::LoginChallenge::nonceBytes || m.salt.size() != QcMsg::LoginChallenge::saltBytes) {
        return qcError(u"Invalid login challenge"_s);
    }
    if (m.maxViewDistance < 1 || m.maxViewDistance > QcMsg::Welcome::maxViewDistance) {
        return qcError(u"Invalid view distance"_s);
    }
    std::optional<QList<QcPackFingerprint>> packs = readPacks(stream);
    if (!packs) {
        return qcError(u"Invalid content pack list"_s);
    }
    m.packs = std::move(*packs);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LoginResponse>, QDataStream &stream)
{
    QcMsg::LoginResponse m;
    stream >> m.passwordProof >> m.identityProof >> m.identity >> m.viewDistance;
    if (!withinProofSize(m.passwordProof) || !withinProofSize(m.identityProof) || !withinProofSize(m.identity)) {
        return qcError(u"Login proof too long"_s);
    }
    if (m.viewDistance < 0 || m.viewDistance > QcMsg::Welcome::maxViewDistance) {
        return qcError(u"Invalid view distance"_s);
    }
    std::optional<QList<QcPackFingerprint>> packs = readPacks(stream);
    if (!packs) {
        return qcError(u"Invalid content pack list"_s);
    }
    m.packs = std::move(*packs);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerList>, QDataStream &stream)
{
    QcMsg::PlayerList m;
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, quint32(QcMsg::PlayerList::maxPlayers));
    if (!countRead) {
        return qcError(u"Too many players"_s);
    }
    const quint32 count = *countRead;

    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        QcMsg::PlayerList::Entry entry;
        stream >> entry.entityId >> entry.name >> entry.operatorRights >> entry.spectator;
        if (entry.name.size() > QcMsg::Hello::maxNameLength) {
            return qcError(u"Player name too long"_s);
        }
        m.players.append(entry);
    }
    return m;
}

} // namespace QcProtocolCodec
