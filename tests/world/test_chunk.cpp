#include "common/world/QcChunk.h"

#include <QTest>

#include <algorithm>

class TestChunk : public QObject
{
    Q_OBJECT

private slots:
    void newSectionIsEmptyAir()
    {
        const QcChunkSection section;
        QVERIFY(section.isEmpty());
        QVERIFY(section.isUniform());
        QCOMPARE(section.block(5, 6, 7), qcAirBlockId);
    }

    void setBlockSwitchesToPaletteAndKeepsOthersAir()
    {
        QcChunkSection section;
        section.setBlock(1, 2, 3, 7);
        QVERIFY(!section.isUniform());
        QCOMPARE(section.block(1, 2, 3), 7u);
        QCOMPARE(section.block(3, 2, 1), qcAirBlockId);
        QCOMPARE(section.palette(), (QList<QcBlockId>{0, 7}));
    }

    void compactRemovesUnusedEntriesAndCollapsesUniform()
    {
        QcChunkSection section;
        section.setBlock(0, 0, 0, 5);
        section.setBlock(0, 0, 0, 9);
        section.compact();

        QList<QcBlockId> palette = section.palette();
        std::ranges::sort(palette);
        QCOMPARE(palette, (QList<QcBlockId>{0, 9}));
        QCOMPARE(section.block(0, 0, 0), 9u);

        section.setBlock(0, 0, 0, 0);
        section.compact();
        QVERIFY(section.isEmpty());
    }

    void fillMakesUniform()
    {
        QcChunkSection section;
        section.setBlock(4, 4, 4, 2);
        section.fill(3);
        QVERIFY(section.isUniform());
        QCOMPARE(section.block(4, 4, 4), 3u);
        QCOMPARE(section.block(15, 15, 15), 3u);
    }

    void assignValidatesInput()
    {
        QcChunkSection section;
        QVERIFY(!section.assign({}, {}));
        QVERIFY(!section.assign({0, 1}, QList<quint16>(10, 0)));

        QList<quint16> indices(QcWorld::blocksPerSection, 0);
        indices[17] = 2;
        QVERIFY(!section.assign({0, 1}, indices));
        indices[17] = 1;
        QVERIFY(section.assign({0, 1}, indices));

        // index 17 = x 1, z 1, y 0
        QCOMPARE(section.block(1, 0, 1), 1u);
    }

    // Index widths (decision E-844): 4 bits from the first second entry, 8 bits from 17, 16 bits
    // from 257, every block kept across each repack.
    void growthWidensIndicesAndKeepsBlocks()
    {
        QcChunkSection section;
        QCOMPARE(section.bitsPerIndex(), 0);
        int next = 1;
        const auto blockOf = [](int cell) { return QcBlockId(cell % 300 + 1); };
        const auto growTo = [&](int entries) {
            for (; next < entries; ++next) {
                const int cell = next - 1;
                section.setBlock(cell % 16, cell / 256, cell / 16 % 16, blockOf(cell));
            }
        };

        growTo(2);
        QCOMPARE(section.palette().size(), 2);
        QCOMPARE(section.bitsPerIndex(), 4);
        QCOMPARE(section.heapBytes() - section.palette().capacity() * qsizetype(sizeof(QcBlockId)), 2048);
        growTo(16);
        QCOMPARE(section.bitsPerIndex(), 4);
        growTo(17);
        QCOMPARE(section.palette().size(), 17);
        QCOMPARE(section.bitsPerIndex(), 8);
        growTo(256);
        QCOMPARE(section.bitsPerIndex(), 8);
        growTo(257);
        QCOMPARE(section.palette().size(), 257);
        QCOMPARE(section.bitsPerIndex(), 16);
        growTo(301);

        for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
            const QcBlockId expected = cell < 300 ? blockOf(cell) : qcAirBlockId;
            QCOMPARE(section.block(cell % 16, cell / 256, cell / 16 % 16), expected);
        }
    }

    // assign() and compact() pick the narrowest width for the palette: 1, 2, 4, 8 or 16 bits.
    void roundTripAcrossWidths_data()
    {
        QTest::addColumn<int>("entries");
        QTest::addColumn<int>("bits");
        QTest::newRow("2") << 2 << 1;
        QTest::newRow("3") << 3 << 2;
        QTest::newRow("4") << 4 << 2;
        QTest::newRow("5") << 5 << 4;
        QTest::newRow("16") << 16 << 4;
        QTest::newRow("17") << 17 << 8;
        QTest::newRow("256") << 256 << 8;
        QTest::newRow("257") << 257 << 16;
        QTest::newRow("4096") << 4096 << 16;
    }

    void roundTripAcrossWidths()
    {
        QFETCH(int, entries);
        QFETCH(int, bits);

        QList<QcBlockId> palette;
        for (int i = 0; i < entries; ++i) {
            palette.append(QcBlockId(1000 + i));
        }
        QList<quint16> indices(QcWorld::blocksPerSection);
        for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
            // Long runs in the lower half (whole words alike), scattered indices above.
            indices[cell] = quint16(cell < 2048 ? cell / 300 % entries : (cell * 7 + cell / 13) % entries);
        }

        QcChunkSection section;
        QVERIFY(section.assign(palette, indices));
        QCOMPARE(section.bitsPerIndex(), bits);
        QCOMPARE(section.unpackedIndices(), indices);
        QCOMPARE(section.heapBytes() - section.palette().capacity() * qsizetype(sizeof(QcBlockId)),
                 qsizetype(QcWorld::blocksPerSection * bits / 8));
        for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
            QCOMPARE(section.paletteIndex(cell), indices[cell]);
            QCOMPARE(section.block(cell % 16, cell / 256, cell / 16 % 16), palette[indices[cell]]);
        }

        // The runs cover every cell in order, each maximal.
        QList<quint16> fromRuns;
        quint16 previous = 0xFFFF;
        section.forEachRun([&](int length, quint16 entry) {
            QVERIFY(length > 0);
            QVERIFY(entry != previous);
            previous = entry;
            fromRuns.insert(fromRuns.size(), length, entry);
        });
        QCOMPARE(fromRuns, indices);

        // Writing every cell again (a growing palette from uniform) and compacting gives the same width.
        QcChunkSection rebuilt;
        for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
            rebuilt.setBlock(cell % 16, cell / 256, cell / 16 % 16, palette[indices[cell]]);
        }
        rebuilt.compact();
        QCOMPARE(rebuilt.bitsPerIndex(), bits);
        for (int cell = 0; cell < QcWorld::blocksPerSection; cell += 37) {
            QCOMPARE(rebuilt.block(cell % 16, cell / 256, cell / 16 % 16), palette[indices[cell]]);
        }
    }

    // A narrow section from compact() widens again when its palette grows.
    void compactNarrowsAndWritesWidenAgain()
    {
        QcChunkSection section;
        section.setBlock(1, 1, 1, 5);
        section.compact();
        QCOMPARE(section.bitsPerIndex(), 1);
        section.setBlock(2, 2, 2, 6);
        QCOMPARE(section.bitsPerIndex(), 4);
        section.setBlock(3, 3, 3, 5);
        QCOMPARE(section.block(1, 1, 1), 5u);
        QCOMPARE(section.block(2, 2, 2), 6u);
        QCOMPARE(section.block(3, 3, 3), 5u);
        QCOMPARE(section.block(0, 0, 0), qcAirBlockId);

        // Unused entries go, the rest narrows to 2 bits (air, 5, 6).
        section.setBlock(4, 4, 4, 7);
        section.setBlock(4, 4, 4, qcAirBlockId);
        section.compact();
        QCOMPARE(section.palette().size(), 3);
        QCOMPARE(section.bitsPerIndex(), 2);
        QCOMPARE(section.block(2, 2, 2), 6u);
    }

    // A 2-bit section takes a fourth entry in place and widens to 4 bits for the fifth.
    void twoBitSectionGrowsToFifthEntry()
    {
        QcChunkSection section;
        section.setBlock(1, 0, 0, 5);
        section.setBlock(2, 0, 0, 6);
        section.compact();
        QCOMPARE(section.bitsPerIndex(), 2);
        section.setBlock(3, 0, 0, 7);
        QCOMPARE(section.palette().size(), 4);
        QCOMPARE(section.bitsPerIndex(), 2);
        section.setBlock(4, 9, 15, 8);
        QCOMPARE(section.palette().size(), 5);
        QCOMPARE(section.bitsPerIndex(), 4);

        QCOMPARE(section.block(0, 0, 0), qcAirBlockId);
        QCOMPARE(section.block(1, 0, 0), 5u);
        QCOMPARE(section.block(2, 0, 0), 6u);
        QCOMPARE(section.block(3, 0, 0), 7u);
        QCOMPARE(section.block(4, 9, 15), 8u);
        QCOMPARE(section.block(15, 15, 15), qcAirBlockId);
    }

    // Entries of the same block (unknown blocks loaded as air) merge into the first one.
    void compactMergesRepeatedEntries()
    {
        QList<quint16> indices(QcWorld::blocksPerSection);
        for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
            indices[cell] = quint16(cell % 4);
        }
        QcChunkSection section;
        QVERIFY(section.assign({3, 5, 3, 5}, indices));
        section.compact();
        QCOMPARE(section.palette(), (QList<QcBlockId>{3, 5}));
        QCOMPARE(section.bitsPerIndex(), 1);
        for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
            QCOMPARE(section.paletteIndex(cell), quint16(cell % 2));
        }

        // Two unknown blocks replaced by air leave a section of air only (also after an earlier
        // compact(): the new palette is compacted again).
        QcChunkSection loaded;
        loaded.setBlock(0, 0, 0, 7);
        loaded.setBlock(1, 0, 0, 8);
        loaded.compact();
        QVERIFY(loaded.replacePalette({qcAirBlockId, qcAirBlockId, qcAirBlockId}));
        loaded.compact();
        QVERIFY(loaded.isEmpty());
    }

    // A section that collapses to one block gives its index words back.
    void uniformSectionHoldsNoWords()
    {
        QcChunkSection section;
        for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
            section.setBlock(cell % 16, cell / 256, cell / 16 % 16, 4);
        }
        QVERIFY(section.heapBytes() > 2000);
        section.compact();
        QVERIFY(section.isUniform());
        QCOMPARE(section.block(7, 7, 7), 4u);
        QVERIFY2(section.heapBytes() <= 64, QByteArray::number(section.heapBytes()));

        section.setBlock(1, 1, 1, 2);
        section.fill(3);
        QVERIFY2(section.heapBytes() <= 64, QByteArray::number(section.heapBytes()));

        section.setBlock(1, 1, 1, 2);
        QVERIFY(section.assign({6}, {}));
        QVERIFY2(section.heapBytes() <= 64, QByteArray::number(section.heapBytes()));
    }

    // A copy shares the packed words until one side writes; the other keeps its blocks.
    void copiesStayIndependent()
    {
        QcChunkSection original;
        original.setBlock(0, 0, 0, 3);
        const QcChunkSection snapshot = original;
        for (int i = 0; i < 20; ++i) {
            original.setBlock(i % 16, 1, 0, QcBlockId(10 + i));
        }
        QCOMPARE(original.bitsPerIndex(), 8);
        QCOMPARE(snapshot.bitsPerIndex(), 4);
        QCOMPARE(snapshot.block(0, 0, 0), 3u);
        QCOMPARE(snapshot.block(5, 1, 0), qcAirBlockId);
        QCOMPARE(original.block(5, 1, 0), 15u);
    }

    void replacePaletteKeepsIndices()
    {
        QcChunkSection section;
        section.setBlock(1, 0, 0, 4);
        QVERIFY(!section.replacePalette({8}));
        QVERIFY(section.replacePalette({9, 8}));
        QCOMPARE(section.block(1, 0, 0), 8u);
        QCOMPARE(section.block(0, 0, 0), 9u);
    }

    void chunkAddressesSectionsByHeight()
    {
        QcChunk chunk({3, -2});
        chunk.setBlock(2, 0, 2, 1);
        chunk.setBlock(2, 17, 2, 2);
        chunk.setBlock(2, 255, 2, 3);

        QCOMPARE(chunk.block(2, 17, 2), 2u);
        QCOMPARE(chunk.section(1).block(2, 1, 2), 2u);
        QCOMPARE(chunk.section(15).block(2, 15, 2), 3u);
        QVERIFY(chunk.section(2).isEmpty());
        QCOMPARE(chunk.block(2, 256, 2), qcAirBlockId);
        QCOMPARE(chunk.block(2, -1, 2), qcAirBlockId);
        QCOMPARE(chunk.highestNonAir(2, 2), 255);
        QCOMPARE(chunk.highestNonAir(0, 0), -1);
    }
};

QTEST_GUILESS_MAIN(TestChunk)
#include "test_chunk.moc"
