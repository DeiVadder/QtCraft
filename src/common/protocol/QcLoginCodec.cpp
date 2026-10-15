// Payloads of the login messages (QcLoginMessages.h): challenge, response and the player list.

#include "QcProtocolCodec.h"

#include "QcStreamLimits.h"

using namespace Qt::StringLiterals;

namespace {

void writePacks(QDataStream &stream, const QList<QcPackFingerprint> &packs)
{
    stream << quint32(packs.size());
    for (const QcPackFingerprint &pack : packs) {
        stream << pack.id << pack.version << pack.hash;
    }
}

bool readPacks(QDataStream &stream, QList<QcPackFingerprint> &packs)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, quint32(QcMsg::LoginChallenge::maxPacks), count)) {
        return false;
    }

    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        QcPackFingerprint pack;
        stream >> pack.id >> pack.version >> pack.hash;
        const qsizetype limit = QcPackFingerprint::maxTextLength;
        if (pack.id.size() > limit || pack.version.size() > limit || pack.hash.size() > limit) {
            return false;
        }
        packs.append(pack);
    }
    return stream.status() == QDataStream::Ok;
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
        stream << entry.entityId << entry.name << entry.operatorRights;
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
    if (!readPacks(stream, m.packs)) {
        return qcError(u"Invalid content pack list"_s);
    }
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
    if (!readPacks(stream, m.packs)) {
        return qcError(u"Invalid content pack list"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerList>, QDataStream &stream)
{
    QcMsg::PlayerList m;
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, quint32(QcMsg::PlayerList::maxPlayers), count)) {
        return qcError(u"Too many players"_s);
    }

    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        QcMsg::PlayerList::Entry entry;
        stream >> entry.entityId >> entry.name >> entry.operatorRights;
        if (entry.name.size() > QcMsg::Hello::maxNameLength) {
            return qcError(u"Player name too long"_s);
        }
        m.players.append(entry);
    }
    return m;
}

} // namespace QcProtocolCodec
