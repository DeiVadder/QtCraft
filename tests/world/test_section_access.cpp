// Micro-benchmarks of QcChunkSection's hot paths (decision E-844): reading every cell (mesher,
// light, server scans), writing cells, copy on write, filling a fresh section as the generator
// does, forEachCell and compact() of a written copy, for palettes of 8, 40 and 300 entries (4-,
// 8- and 16-bit indices). Under ctest each benchmark runs once as a smoke test; for numbers run
// the binary of a release build with `-minimumvalue 200` (or `-iterations <n>`). Only uses the
// section API that stage 58 had, so the same file measures the state before E-844.

#include "common/world/QcChunk.h"

#include <QTest>

#include <array>

namespace {

// A section with `entries` palette entries in a terrain-like layout: layers of a few blocks with
// scattered others (every 7th cell cycles through the rest of the palette).
QcChunkSection sectionWith(int entries)
{
    QcChunkSection section;
    for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
        const int y = cell / 256;
        QcBlockId block = QcBlockId(1 + y / 4 % 4);
        if (cell % 7 == 0 && entries > 5) {
            block = QcBlockId(5 + cell / 7 % (entries - 4));
        }
        section.setBlock(cell % 16, y, cell / 16 % 16, block);
    }
    section.compact();
    return section;
}

// A fixed pseudo-random order of all cells, as (x, y, z).
std::array<std::array<quint8, 3>, QcWorld::blocksPerSection> shuffledCells()
{
    std::array<std::array<quint8, 3>, QcWorld::blocksPerSection> cells{};
    quint32 state = 12345;
    for (int i = 0; i < QcWorld::blocksPerSection; ++i) {
        state = state * 1664525u + 1013904223u;
        const int cell = int((state >> 8) % quint32(QcWorld::blocksPerSection));
        cells[std::size_t(i)] = {quint8(cell % 16), quint8(cell / 256), quint8(cell / 16 % 16)};
    }
    return cells;
}

void addPalettes()
{
    QTest::addColumn<int>("entries");
    QTest::newRow("palette 8") << 8;
    QTest::newRow("palette 40") << 40;
    QTest::newRow("palette 300") << 300;
}

} // namespace

class TestSectionAccess : public QObject
{
    Q_OBJECT

private slots:
    void readInOrder_data() { addPalettes(); }
    void readInOrder()
    {
        QFETCH(int, entries);
        const QcChunkSection section = sectionWith(entries);
        QCOMPARE(section.palette().size(), entries);
        quint64 sum = 0;
        QBENCHMARK {
            for (int y = 0; y < QcWorld::sectionHeight; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        sum += section.block(x, y, z);
                    }
                }
            }
        }
        QVERIFY(sum > 0);
    }

    void readShuffled_data() { addPalettes(); }
    void readShuffled()
    {
        QFETCH(int, entries);
        const QcChunkSection section = sectionWith(entries);
        const auto cells = shuffledCells();
        quint64 sum = 0;
        QBENCHMARK {
            for (const auto &[x, y, z] : cells) {
                sum += section.block(x, y, z);
            }
        }
        QVERIFY(sum > 0);
    }

    // Reads through QcChunk::block with the y of the world, as the mesher's neighbour lookups do.
    void readThroughChunk_data() { addPalettes(); }
    void readThroughChunk()
    {
        QFETCH(int, entries);
        QcChunk chunk;
        chunk.section(4) = sectionWith(entries);
        quint64 sum = 0;
        QBENCHMARK {
            for (int y = 64; y < 80; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        sum += chunk.block(x, y, z);
                    }
                }
            }
        }
        QVERIFY(sum > 0);
    }

    // Writes blocks already in the palette to shuffled cells.
    void writeShuffled_data() { addPalettes(); }
    void writeShuffled()
    {
        QFETCH(int, entries);
        QcChunkSection section = sectionWith(entries);
        const auto cells = shuffledCells();
        QBENCHMARK {
            QcBlockId block = 1;
            for (const auto &[x, y, z] : cells) {
                section.setBlock(x, y, z, block);
                block = block % QcBlockId(entries - 1) + 1;
            }
        }
        QVERIFY(!section.isUniform());
    }

    // A fresh section filled cell by cell and compacted, as the terrain generator does.
    void fillFresh_data() { addPalettes(); }
    void fillFresh()
    {
        QFETCH(int, entries);
        QBENCHMARK {
            const QcChunkSection section = sectionWith(entries);
            QVERIFY(!section.isUniform());
        }
    }

    // Copy on write: a snapshot copy, then one block changed (a block update on the client).
    void copyAndWrite_data() { addPalettes(); }
    void copyAndWrite()
    {
        QFETCH(int, entries);
        const QcChunkSection original = sectionWith(entries);
        QBENCHMARK {
            QcChunkSection copy = original;
            copy.setBlock(3, 4, 5, 2);
            QVERIFY(copy.block(3, 4, 5) == 2);
        }
    }

    // compact() of a copy with one block written again (the save encoder on a section written
    // during play; unwritten sections return at once).
    void compactWrittenCopy_data() { addPalettes(); }
    void compactWrittenCopy()
    {
        QFETCH(int, entries);
        const QcChunkSection original = sectionWith(entries);
        QBENCHMARK {
            QcChunkSection copy = original;
            copy.setBlock(3, 4, 5, original.block(3, 4, 5));
            copy.compact();
            QVERIFY(copy.palette().size() == entries);
        }
    }

    void forEachCell_data() { addPalettes(); }
    void forEachCell()
    {
        QFETCH(int, entries);
        const QcChunkSection section = sectionWith(entries);
        int found = 0;
        QBENCHMARK {
            section.forEachCell({0, 0, 0}, [](QcBlockId id) { return id >= 5; }, [&found](QcBlockPos) { ++found; });
        }
        QVERIFY(found > 0);
    }
};

QTEST_GUILESS_MAIN(TestSectionAccess)
#include "test_section_access.moc"
