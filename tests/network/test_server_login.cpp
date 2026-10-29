// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/auth/QcLoginCrypto.h"
#include "QcServerTestFixture.h"
#include "QcTestProtocol.h"
#include "QcTestUtil.h"
#include "server/login/QcIdentityStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The login of remote clients on the server (decisions E-252, E-253): password, identity (trust on
// first use), content, version, names, the player limit, the timeout, and operators and bans
// bound to the identity. The clients are in-process raw clients that count as remote.
class TestServerLogin : public QcServerTestFixture
{
    Q_OBJECT

private:
    struct Login
    {
        QString name;
        QByteArray key;
        QString password;
        std::optional<QList<QcPackFingerprint>> packs;
        qint32 viewDistance = 0;
        bool answer = true;
    };

    std::unique_ptr<RawClient> connect(const Login &login, QcServer::ClientOrigin origin = QcServer::ClientOrigin::Remote)
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), origin);
        auto client = std::make_unique<RawClient>(std::move(clientEnd));
        client->key = login.key;
        client->password = login.password;
        client->packs = login.packs;
        client->viewDistance = login.viewDistance;
        client->answerChallenge = login.answer;
        client->hello(login.name);
        QcTestUtil::pump();
        ticks(2);
        return client;
    }

    std::unique_ptr<RawClient> connect(const QString &name) { return connect(Login{name, {}, {}, {}, 0, true}); }

    // A server without a raw client of the fixture.
    void startServer(QcServerConfig config)
    {
        if (config.operators.isEmpty()) {
            config.operators = {u"Admin"_s};
        }
        config.viewDistance = std::max(config.viewDistance, 1);
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
    }

    static QcServerConfig savedWorld(const QTemporaryDir &directory)
    {
        QcServerConfig config;
        config.worldDirectory = directory.path();
        return config;
    }

    static QJsonObject jsonFile(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }

    static bool joined(const RawClient &client) { return client.entityId != 0 && client.disconnectReason.isEmpty(); }

    static QByteArray key(char fill) { return QByteArray(QcLoginCrypto::keyBytes, fill); }

    // The fingerprint the server sees for a key on the world of `server`.
    QString fingerprintOf(const QByteArray &installationKey) const
    {
        return QcLoginCrypto::fingerprint(QcLoginCrypto::identity(installationKey, m_identitiesSalt));
    }

    void rememberSalt(const RawClient &client)
    {
        QVERIFY(client.challenge.has_value());
        m_identitiesSalt = client.challenge->salt;
    }

    QByteArray m_identitiesSalt;

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void theRightPasswordLetsIn()
    {
        QcServerConfig config;
        config.password = u"hunter2"_s;
        startServer(config);

        const auto right = connect(Login{u"Ann"_s, key('a'), u"hunter2"_s, {}, 0, true});
        QVERIFY(joined(*right));
        QVERIFY(right->challenge->passwordRequired);
        QCOMPARE(right->challenge->serverName, u"QtCraft server"_s);

        const auto wrong = connect(Login{u"Bob"_s, key('b'), u"Hunter2"_s, {}, 0, true});
        QCOMPARE(wrong->disconnectKey, u"Wrong server password"_s);
        QCOMPARE(wrong->entityId, 0u);
        const auto none = connect(Login{u"Cid"_s, key('c'), {}, {}, 0, true});
        QCOMPARE(none->disconnectKey, u"Wrong server password"_s);
        QCOMPARE(m_server->clientCount(), 1);
    }

    void withoutAPasswordNoneIsAsked()
    {
        startServer({});
        const auto client = connect(Login{u"Ann"_s, key('a'), u"anything"_s, {}, 0, true});
        QVERIFY(joined(*client));
        QVERIFY(!client->challenge->passwordRequired);
    }

    void aNameBelongsToItsFirstIdentity()
    {
        QTemporaryDir directory;
        startServer(savedWorld(directory));

        auto first = connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true});
        QVERIFY(joined(*first));
        QVERIFY(!first->challenge->identityKnown);
        rememberSalt(*first);
        first.reset();
        ticks(2);

        // Another installation cannot take the name, also not in other case; the owner can.
        QCOMPARE(connect(Login{u"Ann"_s, key('x'), {}, {}, 0, true})->disconnectKey, u"The name %1 belongs to another player on this server"_s);
        QCOMPARE(connect(Login{u"ANN"_s, key('x'), {}, {}, 0, true})->disconnectKey, u"The name %1 belongs to another player on this server"_s);
        auto again = connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true});
        QVERIFY(joined(*again));
        QVERIFY(again->challenge->identityKnown);
        again.reset();
        ticks(2);

        // Kept with the world: the identity, not the key, and only readable by the owner.
        const QJsonObject file = jsonFile(directory.filePath(u"identities.json"_s));
        const QJsonArray players = file.value(u"players"_s).toArray();
        QCOMPARE(players.size(), 1);
        QCOMPARE(players.first().toObject().value(u"name"_s).toString(), u"Ann"_s);
        const QByteArray stored = QByteArray::fromBase64(players.first().toObject().value(u"identity"_s).toString().toLatin1());
        QCOMPARE(stored, QcLoginCrypto::identity(key('a'), m_identitiesSalt));
        QFile identities(directory.filePath(u"identities.json"_s));
        QVERIFY(identities.open(QIODevice::ReadOnly));
        QVERIFY(!identities.readAll().contains(key('a').toBase64()));
#ifndef Q_OS_WIN
        // Windows has no group and other bits: Qt reports only the read-only attribute there, and
        // the world folder in the user's profile keeps the file private.
        QCOMPARE(identities.permissions() & QFileDevice::ReadOther, QFileDevice::Permissions());
#endif
        identities.close();

        // After a restart as well.
        cleanup();
        startServer(savedWorld(directory));
        QCOMPARE(connect(Login{u"Ann"_s, key('x'), {}, {}, 0, true})->disconnectKey, u"The name %1 belongs to another player on this server"_s);
        QVERIFY(joined(*connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true})));
    }

    void registrationsAreWrittenInBatches()
    {
        QTemporaryDir directory;
        startServer(savedWorld(directory));
        const auto playersInFile = [&directory] { return jsonFile(directory.filePath(u"identities.json"_s)).value(u"players"_s).toArray().size(); };

        // The first registration goes out with the next tick.
        auto ann = connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true});
        QCOMPARE(playersInFile(), 1);

        // Those within the next second wait for one write, but count at once.
        auto bob = connect(Login{u"Bob"_s, key('b'), {}, {}, 0, true});
        auto cid = connect(Login{u"Cid"_s, key('c'), {}, {}, 0, true});
        QVERIFY(joined(*bob) && joined(*cid));
        QCOMPARE(playersInFile(), 1);
        QCOMPARE(connect(Login{u"Bob"_s, key('x'), {}, {}, 0, true})->disconnectKey, u"The name %1 belongs to another player on this server"_s);
        ticks(20);
        QCOMPARE(playersInFile(), 3);

        // A stop writes what is still waiting.
        auto dan = connect(Login{u"Dan"_s, key('d'), {}, {}, 0, true});
        QCOMPARE(playersInFile(), 3);
        QVERIFY(m_server->stop().has_value());
        QCOMPARE(playersInFile(), 4);
    }

    void aFullIdentityStoreRefusesNewcomers()
    {
        // A store with QcIdentityStore::maxEntries identities from an earlier run.
        QTemporaryDir directory;
        QJsonArray players;
        const QString identity = QString::fromLatin1(key('i').toBase64());
        for (qsizetype i = 0; i < QcIdentityStore::maxEntries; ++i) {
            players.append(QJsonObject{{u"name"_s, u"P%1"_s.arg(i)}, {u"identity"_s, identity}, {u"since"_s, u"2026-10-11T10:00:00Z"_s}});
        }
        const QByteArray salt(QcMsg::LoginChallenge::saltBytes, 's');
        QFile file(directory.filePath(u"identities.json"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{u"format"_s, 1}, {u"salt"_s, QString::fromLatin1(salt.toBase64())}, {u"players"_s, players}}).toJson(QJsonDocument::Compact));
        file.close();
        startServer(savedWorld(directory));

        // A newcomer is refused instead of playing under a name nobody owns; known players come in.
        const auto newcomer = connect(Login{u"Eve"_s, key('e'), {}, {}, 0, true});
        QCOMPARE(newcomer->disconnectKey, u"This server cannot register new players"_s);
        QCOMPARE(newcomer->entityId, 0u);
        auto known = connect(Login{u"P7"_s, {}, {}, {}, 0, false});
        QVERIFY(known->challenge.has_value() && known->challenge->identityKnown);
    }

    void aWrongProofOrMissingIdentityIsRefused()
    {
        startServer({});
        // Registering without an identity, and with an identity it cannot prove.
        for (const bool withIdentity : {false, true}) {
            auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
            m_server->addClient(std::move(serverEnd));
            RawClient client(std::move(clientEnd));
            client.answerChallenge = false;
            client.hello(u"Eve"_s);
            QcTestUtil::pump();
            QVERIFY(client.challenge.has_value());
            QcMsg::LoginResponse response = client.answer(*client.challenge);
            if (withIdentity) {
                response.identityProof[0] = static_cast<char>(response.identityProof[0] ^ 1);
            } else {
                response.identity.clear();
            }
            client.connection.send(response);
            QcTestUtil::pump();
            QCOMPARE(client.disconnectKey, u"Invalid player identity"_s);
            QCOMPARE(client.entityId, 0u);
        }
        // Nothing was registered: Eve's real key still gets the name.
        QVERIFY(joined(*connect(u"Eve"_s)));
    }

    void aSecondPlayerOfTheSameNameIsRefused()
    {
        startServer({});
        auto first = connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true});
        QVERIFY(joined(*first));
        // The same player from a second window: the name is taken while he plays.
        QCOMPARE(connect(Login{u"ann"_s, key('a'), {}, {}, 0, true})->disconnectKey, u"A player named %1 is already on this server"_s);
        QCOMPARE(m_server->clientCount(), 1);
        first.reset();
        ticks(2);
        QVERIFY(joined(*connect(Login{u"ann"_s, key('a'), {}, {}, 0, true})));
    }

    void otherContentIsRefusedWithTheDifference()
    {
        QcServerConfig config;
        config.contentPacks = {{u"qtcraft"_s, u"0.1.0"_s, u"aaaa"_s}, {u"rubies"_s, u"1.0"_s, u"bbbb"_s}};
        startServer(config);

        const auto same = connect(Login{u"Ann"_s, key('a'), {}, config.contentPacks, 0, true});
        QVERIFY(joined(*same));
        QCOMPARE(same->challenge->packs, config.contentPacks);

        const QList<QcPackFingerprint> without{{u"qtcraft"_s, u"0.1.0"_s, u"aaaa"_s}};
        const auto missing = connect(Login{u"Bob"_s, key('b'), {}, without, 0, true});
        QCOMPARE(missing->disconnectKey, u"Your game content differs from the server's: %1"_s);
        QVERIFY(missing->disconnectReason.contains(u"missing: rubies 1.0"_s));

        const QList<QcPackFingerprint> changed{{u"qtcraft"_s, u"0.1.0"_s, u"aaab"_s}, {u"rubies"_s, u"1.0"_s, u"bbbb"_s}};
        QVERIFY(connect(Login{u"Cid"_s, key('c'), {}, changed, 0, true})->disconnectReason.contains(u"qtcraft 0.1.0: different files"_s));
        // Sending no hash does not skip the comparison.
        const QList<QcPackFingerprint> unhashed{{u"qtcraft"_s, u"0.1.0"_s, {}}, {u"rubies"_s, u"1.0"_s, {}}};
        QVERIFY(connect(Login{u"Cid"_s, key('c'), {}, unhashed, 0, true})->disconnectReason.contains(u"rubies 1.0: different files"_s));
        // A refused client registers no name.
        QCOMPARE(m_server->clientCount(), 1);
        QVERIFY(joined(*connect(Login{u"Cid"_s, key('d'), {}, config.contentPacks, 0, true})));
    }

    void withoutConfiguredPacksTheLoadedContentCounts()
    {
        startServer({});
        const auto client = connect(u"Ann"_s);
        QVERIFY(joined(*client));
        QCOMPARE(client->challenge->packs.size(), m_content->packs().size());
        QCOMPARE(client->challenge->packs.first().id, m_content->packs().first().id);
        QVERIFY(client->challenge->packs.first().hash.isEmpty());
    }

    void namesWithMarkupCharactersAreRefused()
    {
        QTemporaryDir directory;
        startServer(savedWorld(directory));
        for (const QString &name : {u"<img src=http://example.invalid/x.png>"_s, u"Tom & Jerry"_s, u"a>b"_s}) {
            const auto client = connect(Login{name, key('m'), {}, {}, 0, true});
            QCOMPARE(client->disconnectKey, u"Player names may not contain <, > or &"_s);
            QCOMPARE(client->entityId, 0u);
            // Refused before the challenge: no name was registered.
            QVERIFY(!client->challenge.has_value());
        }
        QVERIFY(!QFile::exists(directory.filePath(u"identities.json"_s)));
        QVERIFY(joined(*connect(Login{u"Tom and Jerry"_s, key('m'), {}, {}, 0, true})));
    }

    void anotherProtocolVersionIsRefused()
    {
        startServer({});
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient old(std::move(clientEnd));
        old.hello(u"Old"_s, {}, QcProtocol::version - 1);
        QcTestUtil::pump();
        QCOMPARE(old.disconnectKey, u"Protocol version mismatch: server %1, client %2"_s);
        QVERIFY(!old.challenge.has_value());

        // A real client of the version before Hello carried the client text (decision E-834) hears
        // about the mismatch, not about a frame the server could not decode.
        auto [olderServerEnd, olderClientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(olderServerEnd));
        RawClient older(std::move(olderClientEnd));
        older.connection.transport().sendFrame(QcTestProtocol::helloWithoutClientText(u"Older"_s));
        QcTestUtil::pump();
        QCOMPARE(older.disconnectKey, u"Protocol version mismatch: server %1, client %2"_s);
        QVERIFY(!older.challenge.has_value());
    }

    void aSlowLoginTimesOut()
    {
        QcServerConfig config;
        config.loginTimeoutMs = 100;
        startServer(config);
        const auto silent = connect(Login{u"Slow"_s, key('s'), {}, {}, 0, false});
        QVERIFY(silent->challenge.has_value());
        QVERIFY(silent->disconnectKey.isEmpty());

        // No Hello at all is the same.
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient mute(std::move(clientEnd));
        QCOMPARE(m_server->clientCount(), 2);

        // The local player of the in-process host is not timed out before his Hello.
        auto [localEnd, localClientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(localEnd), QcServer::ClientOrigin::Local);
        RawClient host(std::move(localClientEnd));

        QTest::qWait(150);
        ticks(2);
        QCOMPARE(silent->disconnectKey, u"The login took too long"_s);
        QCOMPARE(mute.disconnectKey, u"The login took too long"_s);
        QCOMPARE(host.disconnectKey, QString());
        QCOMPARE(m_server->clientCount(), 1);
        host.hello(u"Host"_s);
        QcTestUtil::pump();
        ticks(1);
        QVERIFY(joined(host));
    }

    void onlyLoginMessagesBeforeTheLogin()
    {
        startServer({});
        const auto client = connect(Login{u"Eager"_s, key('e'), {}, {}, 0, false});
        client->connection.send(QcMsg::ChatInput{u"/give diamond"_s});
        QcTestUtil::pump();
        QCOMPARE(client->disconnectKey, u"Expected Hello"_s);
        QCOMPARE(client->entityId, 0u);

        // A second response or Hello after the login ends the connection (both are exempt from
        // the rate limit, so they must not be repeatable).
        const auto player = connect(u"Ann"_s);
        QVERIFY(joined(*player));
        player->connection.send(player->answer(*player->challenge));
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(player->disconnectKey, u"Unexpected login message"_s);
        QVERIFY(!player->connection.isOpen());

        const auto again = connect(u"Bea"_s);
        QVERIFY(joined(*again));
        again->hello(u"Bea"_s);
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(again->disconnectKey, u"Unexpected login message"_s);
        QCOMPARE(m_server->clientCount(), 0);
    }

    void garbageBeforeTheLoginClosesTheConnection()
    {
        startServer({});
        // Seeded random frames: pure noise, a real Hello cut short or with flipped bytes, and the
        // tag of a real message followed by noise. None may crash the server; each connection ends
        // (refused or undecodable) and leaves nothing behind.
        const QByteArray hello = QcProtocol::encode(QcMsg::Hello{QcProtocol::version, u"Fuzz"_s, {}, {}});
        const QByteArray response = QcProtocol::encode(QcMsg::LoginResponse{});
        QRandomGenerator random(2510);
        int closed = 0;
        // Connections left open by noise, a cut Hello or a tag with noise.
        int waiting = 0;
        constexpr int frames = 300;
        for (int i = 0; i < frames; ++i) {
            QByteArray frame;
            switch (i % 4) {
            case 0:
                frame.resize(1 + random.bounded(200));
                for (char &byte : frame) {
                    byte = static_cast<char>(random.bounded(256));
                }
                break;
            case 1:
                frame = hello.left(random.bounded(hello.size()));
                break;
            case 2:
                frame = hello;
                for (int flips = 0; flips < 3; ++flips) {
                    frame[random.bounded(frame.size())] = static_cast<char>(random.bounded(256));
                }
                break;
            default:
                frame = (i % 8 == 3 ? response : hello).left(4);
                for (int extra = random.bounded(64); extra > 0; --extra) {
                    frame.append(static_cast<char>(random.bounded(256)));
                }
                break;
            }

            auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
            m_server->addClient(std::move(serverEnd));
            clientEnd->sendFrame(frame);
            QcTestUtil::pump();
            ticks(1);
            // A flipped byte may leave a valid Hello (another name, appearance): it waits for its
            // login like any client and is then cut off by its Disconnect.
            if (clientEnd->isOpen()) {
                // Only a Hello with flipped bytes may still be a valid one.
                waiting += i % 4 == 2 ? 0 : 1;
                clientEnd->sendFrame(QcProtocol::encode(QcMsg::Disconnect{}));
                QcTestUtil::pump();
                ticks(1);
            }
            closed += clientEnd->isOpen() ? 0 : 1;
        }
        QCOMPARE(closed, frames);
        QCOMPARE(waiting, 0);
        QCOMPARE(m_server->clientCount(), 0);
        // The server still works.
        QVERIFY(joined(*connect(u"Ann"_s)));
    }

    void theServerHasRoomForItsPlayerLimit()
    {
        QcServerConfig config;
        config.maxPlayers = 2;
        startServer(config);
        const auto host = connect(Login{u"Host"_s, {}, {}, {}, 0, true}, QcServer::ClientOrigin::Local);
        const auto ann = connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true});
        QVERIFY(joined(*host) && joined(*ann));

        const auto bob = connect(Login{u"Bob"_s, key('b'), {}, {}, 0, true});
        QCOMPARE(bob->disconnectKey, u"The server is full (%1 players)"_s);
        QCOMPARE(bob->disconnectReason, u"The server is full (2 players)"_s);
        // An operator may still come in to make room.
        QVERIFY(joined(*connect(Login{u"Admin"_s, key('o'), {}, {}, 0, true})));
    }

    void theViewDistanceIsTheServersAtMost()
    {
        QcServerConfig config;
        config.viewDistance = 3;
        startServer(config);
        QCOMPARE(connect(Login{u"Near"_s, key('n'), {}, {}, 1, true})->viewDistanceGranted, 1);
        QCOMPARE(connect(Login{u"Far"_s, key('f'), {}, {}, 9, true})->viewDistanceGranted, 3);
        const auto any = connect(Login{u"Any"_s, key('y'), {}, {}, 0, true});
        QCOMPARE(any->viewDistanceGranted, 3);
        QCOMPARE(any->challenge->maxViewDistance, 3);

        // Streamed chunks stay within the granted radius.
        const auto near = connect(Login{u"Near2"_s, key('m'), {}, {}, 1, true});
        ticks(20);
        const QcChunkPos centre = QcChunkStreaming::chunkOf(near->motion.position);
        QVERIFY(!near->chunks.isEmpty());
        for (const QcChunkPos &pos : std::as_const(near->chunks)) {
            QVERIFY(std::abs(pos.x - centre.x) <= 1 && std::abs(pos.z - centre.z) <= 1);
        }
    }

    void everyoneGetsThePlayerList()
    {
        startServer({});
        auto ann = connect(u"Ann"_s);
        const auto admin = connect(u"Admin"_s);
        ticks(1);
        QCOMPARE(ann->playerList.size(), 2);
        QCOMPARE(admin->playerList, ann->playerList);
        const auto adminEntry = std::ranges::find(ann->playerList, u"Admin"_s, &QcMsg::PlayerList::Entry::name);
        QVERIFY(adminEntry != ann->playerList.end() && adminEntry->operatorRights && adminEntry->entityId == admin->entityId);

        ann.reset();
        ticks(2);
        QCOMPARE(admin->playerList.size(), 1);
        QCOMPARE(admin->playerList.first().name, u"Admin"_s);

        // A refused login or a connection that never logged in changes nobody's list.
        const int lists = admin->playerListMessages;
        QCOMPARE(connect(Login{u"Admin"_s, key('x'), {}, {}, 0, true})->disconnectKey, u"The name %1 belongs to another player on this server"_s);
        connect(Login{u"Quiet"_s, key('q'), {}, {}, 0, false}).reset();
        ticks(3);
        QCOMPARE(admin->playerListMessages, lists);
    }

    void operatorsAreBoundToTheIdentity()
    {
        QTemporaryDir directory;
        startServer(savedWorld(directory));
        auto admin = connect(Login{u"Admin"_s, key('o'), {}, {}, 0, true});
        auto ann = connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true});
        rememberSalt(*ann);
        admin->connection.send(QcMsg::ChatInput{u"/op Ann"_s});
        QcTestUtil::pump();

        // Ann's entry carries the fingerprint of her identity.
        const QJsonArray operators = jsonFile(directory.filePath(u"access.json"_s)).value(u"operators"_s).toArray();
        QCOMPARE(operators.size(), 1);
        QCOMPARE(operators.first().toObject().value(u"fingerprint"_s).toString(), fingerprintOf(key('a')));
        ann.reset();
        admin.reset();
        cleanup();

        // The identities are lost: somebody else registers "Ann" anew, but the operator entry
        // stays bound to the old identity and does not count for him.
        QVERIFY(QFile::remove(directory.filePath(u"identities.json"_s)));
        startServer(savedWorld(directory));
        auto impostor = connect(Login{u"Ann"_s, key('x'), {}, {}, 0, true});
        QVERIFY(joined(*impostor));
        impostor->connection.send(QcMsg::ChatInput{u"/time set 0"_s});
        QcTestUtil::pump();
        QVERIFY(impostor->chatKeys.constLast().key == u"Only operators can use /%1"_s);
        QVERIFY(!std::ranges::find(impostor->playerList, u"Ann"_s, &QcMsg::PlayerList::Entry::name)->operatorRights);
    }

    void anOperatorNamedBeforeHisFirstLoginIsBoundThen()
    {
        QTemporaryDir directory;
        startServer(savedWorld(directory));
        auto admin = connect(Login{u"Admin"_s, key('o'), {}, {}, 0, true});
        admin->connection.send(QcMsg::ChatInput{u"/op Carol"_s});
        QcTestUtil::pump();
        QCOMPARE(jsonFile(directory.filePath(u"access.json"_s)).value(u"operators"_s).toArray().first().toObject().value(u"fingerprint"_s).toString(),
                 QString());

        auto carol = connect(Login{u"Carol"_s, key('c'), {}, {}, 0, true});
        rememberSalt(*carol);
        QVERIFY(joined(*carol));
        QCOMPARE(jsonFile(directory.filePath(u"access.json"_s)).value(u"operators"_s).toArray().first().toObject().value(u"fingerprint"_s).toString(),
                 fingerprintOf(key('c')));
        carol->connection.send(QcMsg::ChatInput{u"/time set 0"_s});
        QcTestUtil::pump();
        QVERIFY(carol->chatKeys.constLast().key != u"Only operators can use /%1"_s);
    }

    void aConfiguredOperatorIsBoundAtHisFirstLogin()
    {
        QTemporaryDir directory;
        startServer(savedWorld(directory));
        auto admin = connect(Login{u"Admin"_s, key('o'), {}, {}, 0, true});
        rememberSalt(*admin);
        QVERIFY(joined(*admin));
        // The binding is kept in access.json, the name itself stays in the configuration.
        const QJsonObject access = jsonFile(directory.filePath(u"access.json"_s));
        QCOMPARE(access.value(u"operators"_s).toArray().size(), 0);
        const QJsonArray bound = access.value(u"configOperators"_s).toArray();
        QCOMPARE(bound.size(), 1);
        QCOMPARE(bound.first().toObject().value(u"name"_s).toString(), u"Admin"_s);
        QCOMPARE(bound.first().toObject().value(u"fingerprint"_s).toString(), fingerprintOf(key('o')));
        admin.reset();
        cleanup();

        // After a restart the same identity is still the operator.
        startServer(savedWorld(directory));
        admin = connect(Login{u"Admin"_s, key('o'), {}, {}, 0, true});
        QVERIFY(std::ranges::find(admin->playerList, u"Admin"_s, &QcMsg::PlayerList::Entry::name)->operatorRights);
        admin.reset();
        cleanup();

        // identities.json is lost: whoever registers "Admin" first is no operator.
        QVERIFY(QFile::remove(directory.filePath(u"identities.json"_s)));
        startServer(savedWorld(directory));
        auto impostor = connect(Login{u"Admin"_s, key('x'), {}, {}, 0, true});
        QVERIFY(joined(*impostor));
        QVERIFY(!std::ranges::find(impostor->playerList, u"Admin"_s, &QcMsg::PlayerList::Entry::name)->operatorRights);
        impostor->connection.send(QcMsg::ChatInput{u"/time set 0"_s});
        QcTestUtil::pump();
        QCOMPARE(impostor->chatKeys.constLast().key, u"Only operators can use /%1"_s);
        // The binding is not taken over by the newcomer.
        const QJsonArray kept = jsonFile(directory.filePath(u"access.json"_s)).value(u"configOperators"_s).toArray();
        QCOMPARE(kept.first().toObject().value(u"fingerprint"_s).toString(), fingerprintOf(key('o')));
    }

    void aBanFollowsTheIdentityUnderOtherNames()
    {
        startServer({});
        auto admin = connect(Login{u"Admin"_s, key('o'), {}, {}, 0, true});
        auto bob = connect(Login{u"Bob"_s, key('b'), {}, {}, 0, true});
        admin->connection.send(QcMsg::ChatInput{u"/ban Bob griefing"_s});
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(bob->disconnectKey, u"Banned by %1: %2"_s);
        QCOMPARE(bob->disconnectReason, u"Banned by Admin: griefing"_s);

        // The same installation under another name is still banned; another one is not.
        QCOMPARE(connect(Login{u"Bobby"_s, key('b'), {}, {}, 0, true})->disconnectReason, u"You are banned from this world: griefing"_s);
        QVERIFY(joined(*connect(Login{u"Robert"_s, key('r'), {}, {}, 0, true})));

        admin->connection.send(QcMsg::ChatInput{u"/unban Bob"_s});
        QcTestUtil::pump();
        QVERIFY(joined(*connect(Login{u"Bobby"_s, key('b'), {}, {}, 0, true})));
    }

    void kickReasonsAreTranslatable()
    {
        startServer({});
        auto admin = connect(Login{u"Admin"_s, key('o'), {}, {}, 0, true});
        auto ann = connect(Login{u"Ann"_s, key('a'), {}, {}, 0, true});
        admin->connection.send(QcMsg::ChatInput{u"/kick Ann"_s});
        QcTestUtil::pump();
        QCOMPARE(ann->disconnectKey, u"Kicked by %1"_s);
        QCOMPARE(ann->disconnectReason, u"Kicked by Admin"_s);
    }

    void theLocalPlayerNeedsNoLogin()
    {
        QcServerConfig config;
        config.password = u"secret"_s;
        startServer(config);
        const auto host = connect(Login{u"Host"_s, {}, {}, {}, 0, false}, QcServer::ClientOrigin::Local);
        QVERIFY(joined(*host));
        QVERIFY(!host->challenge.has_value());
    }
};

QTEST_GUILESS_MAIN(TestServerLogin)
#include "test_server_login.moc"
