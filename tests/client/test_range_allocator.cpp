// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The ranges of a section page (QcRangeAllocator, decision E-820): first fit from the lowest free
// range, and freed ranges merge with free neighbours so a page can take a large section again.

#include "app/rendering/QcRangeAllocator.h"

#include <QLoggingCategory>
#include <QTest>

using namespace Qt::StringLiterals;

class TestRangeAllocator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // A range given back twice is reported; that message is expected here.
        QLoggingCategory::setFilterRules(u"qtcraft.renderer=false"_s);
    }

    void handsOutRangesFromTheStart()
    {
        QcRangeAllocator ranges(100);
        QCOMPARE(ranges.allocate(30), std::optional<quint32>(0));
        QCOMPARE(ranges.allocate(50), std::optional<quint32>(30));
        QCOMPARE(ranges.used(), 80u);
        // 20 units are left.
        QCOMPARE(ranges.allocate(21), std::nullopt);
        QCOMPARE(ranges.allocate(20), std::optional<quint32>(80));
        QCOMPARE(ranges.allocate(1), std::nullopt);
        QCOMPARE(ranges.allocate(0), std::nullopt);
    }

    void freedRangesAreReusedFirstFit()
    {
        QcRangeAllocator ranges(100);
        const quint32 a = ranges.allocate(10).value();
        const quint32 b = ranges.allocate(10).value();
        QVERIFY(ranges.allocate(10).has_value());
        ranges.release(a, 10);
        ranges.release(b, 10);
        // a and b merged: a range of 20 at the start fits again.
        QCOMPARE(ranges.allocate(20), std::optional<quint32>(0));
        QCOMPARE(ranges.used(), 30u);
    }

    void freedNeighboursMergeOnBothSides()
    {
        QcRangeAllocator ranges(30);
        const quint32 a = ranges.allocate(10).value();
        const quint32 b = ranges.allocate(10).value();
        const quint32 c = ranges.allocate(10).value();
        ranges.release(a, 10);
        ranges.release(c, 10);
        QCOMPARE(ranges.allocate(15), std::nullopt);
        // b joins its free neighbours before and after it into one range of 30.
        ranges.release(b, 10);
        QVERIFY(ranges.isEmpty());
        QCOMPARE(ranges.allocate(30), std::optional<quint32>(0));
    }

    void foreignRangesAreIgnored()
    {
        QcRangeAllocator ranges(10);
        QVERIFY(ranges.allocate(5).has_value());
        ranges.release(8, 5);
        ranges.release(0, 6);
        QCOMPARE(ranges.used(), 5u);
        ranges.release(0, 5);
        QVERIFY(ranges.isEmpty());
    }

    void doubleAndPartlyFreeReleasesAreRefused()
    {
        QcRangeAllocator ranges(30);
        const quint32 a = ranges.allocate(10).value();
        const quint32 b = ranges.allocate(10).value();
        QVERIFY(ranges.allocate(10).has_value());
        ranges.release(a, 10);
        QCOMPARE(ranges.used(), 20u);
        // Given back twice: it is already free.
        ranges.release(a, 10);
        QCOMPARE(ranges.used(), 20u);
        // Reaching into the free range before it, and into the free range after it.
        ranges.release(5, 10);
        ranges.release(b, 10);
        ranges.release(15, 10);
        QCOMPARE(ranges.used(), 10u);
        // The free list is still whole: the first 20 units are one range.
        QCOMPARE(ranges.allocate(20), std::optional<quint32>(0));
    }
};

QTEST_MAIN(TestRangeAllocator)
#include "test_range_allocator.moc"
