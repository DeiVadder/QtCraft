// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcBlockPos.h"

#include <QTest>

class TestCoordinates : public QObject
{
    Q_OBJECT

private slots:
    void floorDivRoundsTowardsNegativeInfinity_data()
    {
        QTest::addColumn<int>("value");
        QTest::addColumn<int>("divisor");
        QTest::addColumn<int>("quotient");
        QTest::addColumn<int>("remainder");

        QTest::newRow("zero") << 0 << 16 << 0 << 0;
        QTest::newRow("positive inside") << 15 << 16 << 0 << 15;
        QTest::newRow("positive boundary") << 16 << 16 << 1 << 0;
        QTest::newRow("minus one") << -1 << 16 << -1 << 15;
        QTest::newRow("minus sixteen") << -16 << 16 << -1 << 0;
        QTest::newRow("minus seventeen") << -17 << 16 << -2 << 15;
        QTest::newRow("large negative") << -1000 << 16 << -63 << 8;
    }

    void floorDivRoundsTowardsNegativeInfinity()
    {
        QFETCH(int, value);
        QFETCH(int, divisor);
        QFETCH(int, quotient);
        QFETCH(int, remainder);
        QCOMPARE(qcFloorDiv(value, divisor), quotient);
        QCOMPARE(qcFloorMod(value, divisor), remainder);
    }

    void blockPositionMapsToChunkAndLocalCoordinates()
    {
        const QcBlockPos pos{-1, 70, 33};
        QCOMPARE(pos.chunk(), (QcChunkPos{-1, 2}));
        QCOMPARE(pos.localX(), 15);
        QCOMPARE(pos.localZ(), 1);
    }

    void worldHeightBounds()
    {
        QVERIFY(QcBlockPos(0, 0, 0).isInsideWorldHeight());
        QVERIFY(QcBlockPos(0, 255, 0).isInsideWorldHeight());
        QVERIFY(!QcBlockPos(0, 256, 0).isInsideWorldHeight());
        QVERIFY(!QcBlockPos(0, -1, 0).isInsideWorldHeight());
    }

    void chunkDistanceIsChebyshev()
    {
        QCOMPARE(qcChunkDistance({0, 0}, {3, -5}), 5);
        QCOMPARE(qcChunkDistance({-2, 4}, {-2, 4}), 0);
    }

    void hashDistinguishesAxes()
    {
        QVERIFY(qHash(QcChunkPos{1, 2}) != qHash(QcChunkPos{2, 1}));
        QVERIFY(qHash(QcBlockPos{1, 2, 3}) != qHash(QcBlockPos{3, 2, 1}));
    }
};

QTEST_GUILESS_MAIN(TestCoordinates)
#include "test_coordinates.moc"
