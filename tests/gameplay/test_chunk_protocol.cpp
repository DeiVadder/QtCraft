#include "common/core/QcWorldConstants.h"
#include "common/protocol/QcProtocol.h"

#include <QDataStream>
#include <QIODevice>
#include <QRandomGenerator>
#include <QTest>

// The chunk data of the protocol: palettes and run-length coded block indices per section, their
// exact byte layout, compact terrain, and chunk data that is cut, overflows a section or points
// outside its palette.
namespace {

// The bytes of `chunk` in the run-length layout, written value by value in byte `order`: its
// position, then per section the palette and, unless the section is uniform, the runs of palette
// indices.
QByteArray runLengthBytes(const QcChunk &chunk, QDataStream::ByteOrder order)
{
    QByteArray expected;
    QDataStream stream(&expected, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    stream.setByteOrder(order);
    stream << qint32(chunk.pos().x) << qint32(chunk.pos().z);
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const QcChunkSection &section = chunk.section(index);
        stream << quint32(section.palette().size());
        for (const QcBlockId id : section.palette()) {
            stream << id;
        }
        if (section.isUniform()) {
            continue;
        }
        QList<std::pair<quint16, quint16>> runs;
        const QList<quint16> indices = section.unpackedIndices();
        for (const quint16 value : indices) {
            if (!runs.isEmpty() && runs.last().second == value) {
                ++runs.last().first;
            } else {
                runs.append({quint16(1), value});
            }
        }
        stream << quint32(runs.size());
        for (const auto &[length, value] : std::as_const(runs)) {
            stream << length << value;
        }
    }
    return expected;
}

} // namespace

class TestChunkProtocol : public QObject
{
    Q_OBJECT

private slots:
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

    void chunkBytesFollowTheRunLengthLayout()
    {
        // Scattered blocks give sections with many short runs; the expected bytes are written value
        // by value, in both byte orders.
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{3, -4});
        QRandomGenerator random(14);
        for (int i = 0; i < 3000; ++i) {
            chunk->setBlock(random.bounded(16), random.bounded(70), random.bounded(16), QcBlockId(1 + random.bounded(5)));
        }

        for (const QDataStream::ByteOrder order : {QDataStream::BigEndian, QDataStream::LittleEndian}) {
            const QByteArray expected = runLengthBytes(*chunk, order);

            QByteArray written;
            {
                QDataStream stream(&written, QIODevice::WriteOnly);
                stream.setVersion(QDataStream::Qt_6_8);
                stream.setByteOrder(order);
                QcProtocol::writeChunk(stream, *chunk);
            }
            QCOMPARE(written, expected);

            QDataStream in(written);
            in.setVersion(QDataStream::Qt_6_8);
            in.setByteOrder(order);
            const QcResult<std::shared_ptr<QcChunk>> copy = QcProtocol::readChunk(in);
            QVERIFY(copy.has_value());
            QVERIFY(in.atEnd());
            for (int index = 0; index < QcWorld::sectionCount; ++index) {
                QCOMPARE((*copy)->section(index).palette(), chunk->section(index).palette());
                QCOMPARE((*copy)->section(index).unpackedIndices(), chunk->section(index).unpackedIndices());
            }

            // Cut off in the middle (inside the runs of a section): rejected.
            QDataStream cut(written.left(written.size() / 2));
            cut.setVersion(QDataStream::Qt_6_8);
            cut.setByteOrder(order);
            QVERIFY(!QcProtocol::readChunk(cut).has_value());
        }
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

QTEST_GUILESS_MAIN(TestChunkProtocol)
#include "test_chunk_protocol.moc"
