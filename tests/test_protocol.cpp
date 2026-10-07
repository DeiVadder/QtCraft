#include "QcProtocol.h"

#include <QDataStream>
#include <QIODevice>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

template<typename T>
T roundTrip(const T &message)
{
    const QcMessage wrapped(message);
    const QcProtocol::Peer sender =
        QcProtocol::isSentBy(wrapped.index(), QcProtocol::Peer::Client) ? QcProtocol::Peer::Client : QcProtocol::Peer::Server;
    const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(wrapped), sender);
    if (!decoded) {
        qFatal("decode failed: %s", qPrintable(decoded.error()));
    }
    if (!std::holds_alternative<T>(*decoded)) {
        qFatal("decoded wrong alternative");
    }
    return std::get<T>(*decoded);
}

QByteArray frameWithTag(quint16 tag, const QByteArray &payload)
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream << tag;
    frame.append(payload);
    return frame;
}

} // namespace

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void simpleMessagesRoundTrip()
    {
        const QcMsg::Hello hello = roundTrip(QcMsg::Hello{QcProtocol::version, u"Jö"_s});
        QCOMPARE(hello.protocolVersion, QcProtocol::version);
        QCOMPARE(hello.playerName, u"Jö"_s);

        const QcMsg::Welcome welcome = roundTrip(QcMsg::Welcome{1, 42, u"World"_s, -1234567890123});
        QCOMPARE(welcome.playerEntityId, 42u);
        QCOMPARE(welcome.worldName, u"World"_s);
        QCOMPARE(welcome.seed, -1234567890123);

        QCOMPARE(roundTrip(QcMsg::ChunkUnload{{-5, 9}}).pos, (QcChunkPos{-5, 9}));
        const QcMsg::BlockUpdate update = roundTrip(QcMsg::BlockUpdate{{-1, 200, 33}, 17});
        QCOMPARE(update.pos, (QcBlockPos{-1, 200, 33}));
        QCOMPARE(update.block, 17u);
        QCOMPARE(roundTrip(QcMsg::Disconnect{u"bye"_s}).reason, u"bye"_s);
    }

    void movementMessagesRoundTripExactly()
    {
        QcPlayerInput input;
        input.sequence = 99;
        input.forward = 1.0f;
        input.strafe = -0.5f;
        input.jump = true;
        input.sprint = true;
        input.yaw = 123.25f;
        input.pitch = -12.5f;
        QCOMPARE(roundTrip(QcMsg::PlayerInput{input}).input, input);

        QcMsg::PlayerState state;
        state.lastInputSequence = 99;
        state.motion.position = {1.5, 64.0, -3.25};
        state.motion.velocity = {0.1, -9.5, 0.0};
        state.motion.onGround = true;
        const QcMsg::PlayerState copy = roundTrip(state);
        QCOMPARE(copy.lastInputSequence, 99u);
        QCOMPARE(copy.motion, state.motion);
    }

    void chunkRoundTripPreservesEveryBlock()
    {
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{-7, 12});
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < 60; ++y) {
                    chunk->setBlock(x, y, z, QcBlockId(1 + (x + y + z) % 3));
                }
            }
        }
        chunk->section(10).fill(5);
        const QcMsg::ChunkData copy = roundTrip(QcMsg::ChunkData{chunk});
        QCOMPARE(copy.chunk->pos(), (QcChunkPos{-7, 12}));
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    QCOMPARE(copy.chunk->block(x, y, z), chunk->block(x, y, z));
                }
            }
        }
        QVERIFY(copy.chunk->section(10).isUniform());
        QVERIFY(copy.chunk->section(12).isEmpty());
    }

    void contentSyncRoundTrip()
    {
        auto content = std::make_shared<QcContentRegistry>();
        QcBlockDefinition stone;
        stone.name = u"a:stone"_s;
        stone.faceTextures.fill(u"a:stone"_s);
        QVERIFY(content->registerBlock(stone).has_value());
        const QcMsg::ContentSync copy = roundTrip(QcMsg::ContentSync{content});
        QCOMPARE(copy.content->blockCount(), 2);
        QCOMPARE(copy.content->blockId(u"a:stone"_s), std::optional<QcBlockId>(1));
    }

    void rejectsMalformedFrames()
    {
        QVERIFY(!QcProtocol::decode(QByteArray(), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(frameWithTag(999, {}), QcProtocol::Peer::Server).has_value());

        const QByteArray hello = QcProtocol::encode(QcMsg::Hello{1, u"player"_s});
        QVERIFY(!QcProtocol::decode(hello.left(hello.size() - 3), QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(hello + QByteArray(1, 'x'), QcProtocol::Peer::Client).has_value());

        // A truncated chunk and a palette size of zero (byte 13 is the low byte of section 0's size).
        const QByteArray chunkFrame = QcProtocol::encode(QcMsg::ChunkData{std::make_shared<QcChunk>()});
        QVERIFY(!QcProtocol::decode(chunkFrame.left(chunkFrame.size() - 1), QcProtocol::Peer::Server).has_value());
        QByteArray zeroPalette = chunkFrame;
        zeroPalette[13] = 0;
        QVERIFY(!QcProtocol::decode(zeroPalette, QcProtocol::Peer::Server).has_value());
    }

    void messagesAreOnlyAcceptedFromTheirSender()
    {
        const QByteArray chunk = QcProtocol::encode(QcMsg::ChunkData{std::make_shared<QcChunk>()});
        QVERIFY(QcProtocol::decode(chunk, QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(chunk, QcProtocol::Peer::Client).has_value());
        const QByteArray input = QcProtocol::encode(QcMsg::PlayerInput{});
        QVERIFY(QcProtocol::decode(input, QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(input, QcProtocol::Peer::Server).has_value());
        const QByteArray bye = QcProtocol::encode(QcMsg::Disconnect{u"x"_s});
        QVERIFY(QcProtocol::decode(bye, QcProtocol::Peer::Client).has_value());
        QVERIFY(QcProtocol::decode(bye, QcProtocol::Peer::Server).has_value());
    }

    void runLengthDataCannotOverflowASection()
    {
        // Two runs of 4000 blocks claim 8000 entries in a section of 4096.
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0);
            stream << quint32(2) << QcBlockId(0) << QcBlockId(1);
            stream << quint32(2) << quint16(4000) << quint16(0) << quint16(4000) << quint16(1);
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }

    void runLengthDataMustFillTheSection()
    {
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0);
            stream << quint32(2) << QcBlockId(0) << QcBlockId(1);
            stream << quint32(1) << quint16(100) << quint16(1);
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }

    void terrainChunksEncodeCompactly()
    {
        // 64 layers of alternating stone and dirt: 4 sections with long runs, 12 empty sections.
        auto chunk = std::make_shared<QcChunk>();
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    chunk->setBlock(x, y, z, QcBlockId(1 + y % 2));
                }
            }
        }
        const QByteArray frame = QcProtocol::encode(QcMsg::ChunkData{chunk});
        // Each section: palette (4 + 3 * 4 bytes) + 16 runs of 256 (4 + 16 * 4 bytes). Raw indices would be 8 kB.
        QVERIFY2(frame.size() < 1000, qPrintable(QString::number(frame.size())));
    }

    void rejectsNonFiniteMovement()
    {
        QcPlayerInput input;
        input.forward = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerInput{input}), QcProtocol::Peer::Client).has_value());
        input.forward = 0.0f;
        input.yaw = std::numeric_limits<float>::infinity();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerInput{input}), QcProtocol::Peer::Client).has_value());
        QcMsg::PlayerState state;
        state.motion.position.y = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server).has_value());
    }

    void entityAndStatusMessagesRoundTrip()
    {
        const QcMsg::EntitySpawn spawn = roundTrip(QcMsg::EntitySpawn{42, 3, u"Alex"_s, {1.5, 70.0, -2.25}, 90.0f, 17.5});
        QCOMPARE(spawn.id, 42u);
        QCOMPARE(spawn.mobType, 3);
        QCOMPARE(spawn.name, u"Alex"_s);
        QCOMPARE(spawn.position, (QcVec3{1.5, 70.0, -2.25}));
        QCOMPARE(spawn.yaw, 90.0f);
        QCOMPARE(spawn.health, 17.5);

        QcMsg::EntityMoves moves;
        moves.moves = {{1, {0.0, 64.0, 0.0}, 10.0f, 5.0, true}, {2, {3.0, 65.0, 4.0}, -45.0f, 9.0, false}};
        const QcMsg::EntityMoves copy = roundTrip(moves);
        QCOMPARE(copy.moves.size(), 2);
        QCOMPARE(copy.moves.at(0).hurt, true);
        QCOMPARE(copy.moves.at(1).position, (QcVec3{3.0, 65.0, 4.0}));

        QCOMPARE(roundTrip(QcMsg::EntityRemove{77}).id, 77u);
        const QcMsg::PlayerHealth health = roundTrip(QcMsg::PlayerHealth{3.0, 20.0, true});
        QCOMPARE(health.health, 3.0);
        QCOMPARE(health.dead, true);
        QCOMPARE(roundTrip(QcMsg::WorldTime{1234.5, 600.0}).time, 1234.5);
        QCOMPARE(roundTrip(QcMsg::AttackEntity{9}).id, 9u);
        roundTrip(QcMsg::Respawn{});
    }

    void rejectsBadEntityAndTimeData()
    {
        QcMsg::EntitySpawn spawn;
        spawn.position.x = std::numeric_limits<double>::infinity();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(spawn), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::WorldTime{0.0, 0.0}), QcProtocol::Peer::Server).has_value());
        // Combat messages only come from clients.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::AttackEntity{1}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerHealth{}), QcProtocol::Peer::Client).has_value());
    }

    void rejectsPaletteIndexOutsidePalette()
    {
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0);
            // Section 0: palette of two entries but an index pointing at entry 5.
            stream << quint32(2) << QcBlockId(0) << QcBlockId(1);
            for (int i = 0; i < QcWorld::blocksPerSection; ++i) {
                stream << quint16(i == 100 ? 5 : 0);
            }
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }

    void rejectsOversizedPalette()
    {
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0) << quint32(0x7FFFFFFF);
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
