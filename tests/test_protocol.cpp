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

        const QByteArray chunkFrame = QcProtocol::encode(QcMsg::ChunkData{std::make_shared<QcChunk>()});
        QByteArray corrupt = chunkFrame;
        corrupt[corrupt.size() - 1] = char(corrupt.at(corrupt.size() - 1) ^ 0x5A);
        QVERIFY(!QcProtocol::decode(corrupt, QcProtocol::Peer::Server).has_value());
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

    void rejectsDecompressionBombs()
    {
        // 64 MB of zeros compress to a few kB; the announced size exceeds any real chunk.
        const QByteArray bomb = qCompress(QByteArray(qsizetype(64) * 1024 * 1024, '\0'), 9);
        QVERIFY(bomb.size() < qsizetype(1024) * 1024);
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream << bomb;
        const QByteArray frame = frameWithTag(quint16(QcMessage(QcMsg::ChunkData{}).index()), payload);
        const auto decoded = QcProtocol::decode(frame, QcProtocol::Peer::Server);
        QVERIFY(!decoded.has_value());
        QVERIFY(decoded.error().contains(u"implausible"_s));
    }

    void rejectsTrailingBytesInsideCompressedChunk()
    {
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            QcProtocol::writeChunk(stream, QcChunk());
            stream << quint32(0xDEADBEEF);
        }
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream << qCompress(raw);
        const QByteArray frame = frameWithTag(quint16(QcMessage(QcMsg::ChunkData{}).index()), payload);
        QVERIFY(!QcProtocol::decode(frame, QcProtocol::Peer::Server).has_value());
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
