#include "QcChatText.h"
#include "QcFrameReader.h"
#include "QcLoginCrypto.h"
#include "QcPackComparison.h"
#include "QcPlayerKey.h"
#include "QcProtocol.h"
#include "QcServerAddress.h"

#include <QDataStream>
#include <QFile>
#include <QIODevice>
#include <QPasswordDigestor>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

template<typename T>
QcResult<T> decoded(const T &message)
{
    const QcMessage wrapped(message);
    const QcProtocol::Peer sender =
        QcProtocol::isSentBy(wrapped.index(), QcProtocol::Peer::Client) ? QcProtocol::Peer::Client : QcProtocol::Peer::Server;
    const QcResult<QcMessage> result = QcProtocol::decode(QcProtocol::encode(wrapped), sender);
    if (!result) {
        return qcError(result.error());
    }
    return std::get<T>(*result);
}

QcMsg::LoginChallenge challenge()
{
    QcMsg::LoginChallenge m;
    m.serverName = u"Hütte"_s;
    m.motd = u"Welcome"_s;
    m.nonce = QByteArray(QcMsg::LoginChallenge::nonceBytes, 'n');
    m.salt = QByteArray(QcMsg::LoginChallenge::saltBytes, 's');
    m.passwordRequired = true;
    m.identityKnown = true;
    m.maxViewDistance = 12;
    m.packs = {{u"qtcraft"_s, u"0.1.0"_s, u"ab"_s}, {u"rubies"_s, u"1.2"_s, {}}};
    return m;
}

} // namespace

// The messages, framing and cryptography of the network login (decisions E-251, E-252).
class TestLoginProtocol : public QObject
{
    Q_OBJECT

private slots:
    void loginMessagesRoundTrip()
    {
        const QcMsg::LoginChallenge sent = challenge();
        const QcResult<QcMsg::LoginChallenge> got = decoded(sent);
        QVERIFY(got.has_value());
        QCOMPARE(got->serverName, sent.serverName);
        QCOMPARE(got->motd, sent.motd);
        QCOMPARE(got->nonce, sent.nonce);
        QCOMPARE(got->salt, sent.salt);
        QVERIFY(got->passwordRequired && got->identityKnown);
        QCOMPARE(got->maxViewDistance, 12);
        QCOMPARE(got->packs, sent.packs);

        QcMsg::LoginResponse response;
        response.passwordProof = QByteArray(32, 'p');
        response.identityProof = QByteArray(32, 'i');
        response.identity = QByteArray(32, 'k');
        response.viewDistance = 6;
        response.packs = sent.packs;
        const QcResult<QcMsg::LoginResponse> answer = decoded(response);
        QVERIFY(answer.has_value());
        QCOMPARE(answer->passwordProof, response.passwordProof);
        QCOMPARE(answer->identityProof, response.identityProof);
        QCOMPARE(answer->identity, response.identity);
        QCOMPARE(answer->viewDistance, 6);
        QCOMPARE(answer->packs, response.packs);

        const QcMsg::PlayerList list{{{7, u"Ann"_s, true}, {9, u"Bo"_s, false}}};
        QCOMPARE(decoded(list)->players, list.players);
    }

    void loginMessagesGoOneWayOnly()
    {
        // The server never accepts a challenge, the client never a response.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(challenge()), QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::LoginResponse{}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerList{}), QcProtocol::Peer::Client).has_value());
    }

    void loginMessagesAreBounded()
    {
        QcMsg::LoginChallenge shortNonce = challenge();
        shortNonce.nonce.chop(1);
        QVERIFY(!decoded(shortNonce).has_value());
        QcMsg::LoginChallenge longName = challenge();
        longName.serverName = QString(QcMsg::LoginChallenge::maxTextLength + 1, u'x');
        QVERIFY(!decoded(longName).has_value());
        QcMsg::LoginChallenge manyPacks = challenge();
        manyPacks.packs = QList<QcPackFingerprint>(QcMsg::LoginChallenge::maxPacks + 1, {u"p"_s, u"1"_s, {}});
        QVERIFY(!decoded(manyPacks).has_value());
        QcMsg::LoginChallenge noView = challenge();
        noView.maxViewDistance = 0;
        QVERIFY(!decoded(noView).has_value());

        QcMsg::LoginResponse longProof;
        longProof.identityProof = QByteArray(QcMsg::LoginResponse::maxProofBytes + 1, 'x');
        QVERIFY(!decoded(longProof).has_value());
        QcMsg::LoginResponse longPack;
        longPack.packs = {{QString(QcPackFingerprint::maxTextLength + 1, u'x'), u"1"_s, {}}};
        QVERIFY(!decoded(longPack).has_value());
        QcMsg::LoginResponse farView;
        farView.viewDistance = QcMsg::Welcome::maxViewDistance + 1;
        QVERIFY(!decoded(farView).has_value());
    }

    void helloNamesAreBoundedBeforeCleaning()
    {
        QVERIFY(decoded(QcMsg::Hello{QcProtocol::version, QString(QcMsg::Hello::maxNameLength, u'a'), {}}).has_value());
        QVERIFY(!decoded(QcMsg::Hello{QcProtocol::version, QString(QcMsg::Hello::maxNameLength + 1, u'a'), {}}).has_value());
        // A client of another version still gets its version checked, whatever follows the name.
        QVERIFY(decoded(QcMsg::Hello{QcProtocol::version + 1, u"Old"_s, {}}).has_value());
    }

    void disconnectReasonsAreTranslatable()
    {
        const QcMsg::Disconnect reason = QcChatText::disconnect("Kicked by %1: %2", {u"Ann"_s, u"%1 spam"_s});
        QCOMPARE(reason.reason, u"Kicked by Ann: %1 spam"_s);
        QCOMPARE(reason.key, u"Kicked by %1: %2"_s);
        QCOMPARE(QcChatText::display(reason), u"Kicked by Ann: %1 spam"_s);
        QcMsg::Disconnect tooMany = reason;
        tooMany.arguments = QStringList(int(QcMsg::Disconnect::maxArguments) + 1, u"x"_s);
        QVERIFY(!decoded(tooMany).has_value());
        // Without a key the English text is shown.
        QCOMPARE(QcChatText::display(QcMsg::Disconnect{u"Connection lost"_s, {}, {}}), u"Connection lost"_s);
    }

    void framesSurviveAnyFragmentation()
    {
        const QList<QByteArray> frames{QByteArray("a"), QByteArray(), QByteArray(70'000, 'x'), QByteArray("last")};
        QByteArray stream;
        for (const QByteArray &frame : frames) {
            stream += QcFrameReader::header(frame.size()) + frame;
        }

        // Byte by byte, in odd pieces and in one go: the same frames in the same order.
        for (const qsizetype piece : {qsizetype(1), qsizetype(3), qsizetype(4097), stream.size()}) {
            QcFrameReader reader(QcProtocol::maxFrameBytes);
            QList<QByteArray> got;
            for (qsizetype offset = 0; offset < stream.size(); offset += piece) {
                QVERIFY(reader.append(QByteArrayView(stream).mid(offset, piece)));
                while (std::optional<QByteArray> frame = reader.next()) {
                    got.append(*frame);
                }
            }
            QCOMPARE(got, frames);
            QCOMPARE(reader.buffered(), 0);
        }
    }

    void anOversizedFrameBreaksTheStream()
    {
        QcFrameReader reader(1000);
        // Refused as soon as the length is known, before any of its bytes arrive.
        QVERIFY(!reader.append(QcFrameReader::header(1001)));
        QVERIFY(reader.failed());
        QVERIFY(!reader.next().has_value());
        QVERIFY(!reader.append("more"));

        QcFrameReader exact(1000);
        QVERIFY(exact.append(QByteArray(QcFrameReader::header(1000) + QByteArray(1000, 'z'))));
        QCOMPARE(exact.next()->size(), 1000);
        QCOMPARE(QcFrameReader::header(258), QByteArray::fromHex("00000102"));
    }

    void pbkdf2MatchesTheStandard()
    {
        // RFC 7914, section 11: PBKDF2-HMAC-SHA256("passwd", "salt", 1), first block.
        QCOMPARE(QcLoginCrypto::pbkdf2("passwd", "salt", 1).toHex(),
                 QByteArray("55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"));
        // And Qt's implementation for more rounds.
        const QByteArray reference = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, "secret", "pepper", 2000, 32);
        QCOMPARE(QcLoginCrypto::pbkdf2("secret", "pepper", 2000), reference);
    }

    void proofsDependOnKeyNonceAndPurpose()
    {
        const QByteArray salt(QcMsg::LoginChallenge::saltBytes, 's');
        const QByteArray key(QcLoginCrypto::keyBytes, 'k');
        const QByteArray identity = QcLoginCrypto::identity(key, salt);
        QCOMPARE(identity.size(), QcLoginCrypto::keyBytes);
        // Another server (salt) sees another identity; the key itself never leaves the client.
        QVERIFY(QcLoginCrypto::identity(key, QByteArray(QcMsg::LoginChallenge::saltBytes, 't')) != identity);
        QVERIFY(!identity.contains(key));

        const QByteArray nonce(32, 'n');
        using Purpose = QcLoginCrypto::Purpose;
        const QByteArray proof = QcLoginCrypto::proof(identity, nonce, Purpose::Identity);
        QCOMPARE(QcLoginCrypto::proof(identity, nonce, Purpose::Identity), proof);
        QVERIFY(QcLoginCrypto::proof(identity, QByteArray(32, 'm'), Purpose::Identity) != proof);
        QVERIFY(QcLoginCrypto::proof(identity, nonce, Purpose::Password) != proof);

        QVERIFY(QcLoginCrypto::passwordKey(QString(), salt).isEmpty());
        QCOMPARE(QcLoginCrypto::passwordKey(u"pw"_s, salt), QcLoginCrypto::passwordKey(u"pw"_s, salt));
        QVERIFY(QcLoginCrypto::passwordKey(u"pw"_s, salt) != QcLoginCrypto::passwordKey(u"pW"_s, salt));
        QVERIFY(QcLoginCrypto::equal("abc", "abc"));
        QVERIFY(!QcLoginCrypto::equal("abc", "abd"));
        QVERIFY(!QcLoginCrypto::equal("abc", "ab"));

        const QString fingerprint = QcLoginCrypto::fingerprint(identity);
        QCOMPARE(fingerprint.size(), QcLoginCrypto::fingerprintBytes * 2);
        QCOMPARE(fingerprint, QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).first(16).toHex()));
    }

    void thePlayerKeyIsMadeOnceAndKept()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(u"sub/player.key"_s);
        const QcResult<QByteArray> first = QcPlayerKey::loadOrCreate(path);
        QVERIFY(first.has_value());
        QCOMPARE(first->size(), QcLoginCrypto::keyBytes);
        QCOMPARE(QcPlayerKey::loadOrCreate(path), first);
        QCOMPARE(QFile(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());

        // Another installation gets another key.
        QVERIFY(QcPlayerKey::loadOrCreate(directory.filePath(u"other.key"_s)) != first);

        // A damaged file is reported and kept.
        QFile damaged(directory.filePath(u"damaged.key"_s));
        QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write("not a key");
        damaged.close();
        QVERIFY(!QcPlayerKey::loadOrCreate(damaged.fileName()).has_value());
        QVERIFY(damaged.open(QIODevice::ReadOnly));
        QCOMPARE(damaged.readAll(), QByteArray("not a key"));
    }

    void packDifferencesAreNamed()
    {
        const QList<QcPackFingerprint> server{{u"qtcraft"_s, u"0.1.0"_s, u"aa"_s}, {u"rubies"_s, u"1.2"_s, u"bb"_s}, {u"gems"_s, u"1"_s, {}}};
        QVERIFY(QcPackComparison::differences(server, server).isEmpty());

        // A pack the server has no hash for counts by id and version; a client that sends no hash
        // for a pack the server hashed is refused (else an empty hash would skip the comparison).
        QList<QcPackFingerprint> client = server;
        client[2].hash = u"cc"_s;
        QVERIFY(QcPackComparison::differences(server, client).isEmpty());
        client[1].hash.clear();
        QCOMPARE(QcPackComparison::differences(server, client), QStringList{u"rubies 1.2: different files"_s});

        client = {{u"qtcraft"_s, u"0.1.0"_s, u"ff"_s}, {u"rubies"_s, u"1.1"_s, u"bb"_s}, {u"fun"_s, u"2"_s, {}}};
        const QStringList lines = QcPackComparison::differences(server, client);
        QCOMPARE(lines, (QStringList{u"qtcraft 0.1.0: different files"_s, u"rubies: server 1.2, client 1.1"_s, u"missing: gems 1"_s,
                                     u"extra: fun 2"_s}));
        QCOMPARE(QcPackComparison::summary(QStringList(7, u"x"_s)), u"x; x; x; x; x; and 2 more"_s);
    }

    void serverAddressesParse()
    {
        using Address = QcServerAddress;
        QCOMPARE(Address::parse(u"example.org"_s)->port, Address::defaultPort);
        QCOMPARE(Address::parse(u" 192.168.1.5:4000 "_s)->host, u"192.168.1.5"_s);
        QCOMPARE(Address::parse(u"192.168.1.5:4000"_s)->port, 4000);
        QCOMPARE(Address::parse(u"[::1]:4001"_s)->host, u"::1"_s);
        QCOMPARE(Address::parse(u"[::1]:4001"_s)->port, 4001);
        QCOMPARE(Address::parse(u"[fe80::1]"_s)->port, Address::defaultPort);
        QCOMPARE(Address::parse(u"2001:db8::7"_s)->host, u"2001:db8::7"_s);
        QCOMPARE(Address::parse(u"[::1]:4001"_s)->toString(), u"[::1]:4001"_s);
        QCOMPARE(Address::parse(u"host:12"_s)->toString(), u"host:12"_s);
        for (const QString &bad : {u""_s, u"host:"_s, u"host:0"_s, u"host:70000"_s, u"[::1"_s, u"[::1]x"_s, u"a b"_s, u"1:2:zz"_s}) {
            QVERIFY2(!Address::parse(bad).has_value(), qPrintable(bad));
        }
    }
};

QTEST_GUILESS_MAIN(TestLoginProtocol)
#include "test_login_protocol.moc"
