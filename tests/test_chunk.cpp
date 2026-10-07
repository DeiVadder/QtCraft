#include "QcChunk.h"

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
