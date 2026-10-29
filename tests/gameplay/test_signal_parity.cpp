// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcMachineFixture.h"

using namespace Qt::StringLiterals;

// Signal behaviour brought in line with the role model (decision E-887): levers, buttons, torches
// and dust stay on an extended piston (but drop from the first pushed and a pulled block), torches
// and dust reach pistons and dispensers with quasi-connectivity two blocks out, a dust stair over
// glass carries the signal up but not down, and a comparator's side reads the Signal Dust block
// as 15.
class TestSignalParity : public QcMachineFixture
{
    Q_OBJECT

private:
    // A piston at (5, 0, 5) facing north whose cell above a lever at (6, 1, 5) powers; flipped, the
    // piston is powered through quasi-connectivity but has not noticed it.
    void quasiPoweredPiston(const QString &piston)
    {
        put(5, 0, 5, piston + u"[facing=north]"_s);
        powerAbove();
        QVERIFY(!flag(5, 0, 5, u"extended"_s));
    }

    // A lever at (6, 1, 5) powers the cell above (5, 0, 5), not (5, 0, 5) itself.
    void powerAbove()
    {
        put(7, 1, 5, u"stone"_s);
        put(6, 1, 5, u"lever[attach=east]"_s);
        ticks(1);
        flip(6, 1, 5);
        ticks(5);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void leverOnThePistonStays_data()
    {
        QTest::addColumn<int>("x");
        QTest::addColumn<int>("y");
        QTest::addColumn<int>("z");
        QTest::addColumn<QString>("lever");
        QTest::addRow("top") << 5 << 1 << 5 << u"lever[attach=floor]"_s;
        QTest::addRow("side") << 6 << 0 << 5 << u"lever[attach=west]"_s;
        QTest::addRow("back") << 5 << 0 << 6 << u"lever[attach=north]"_s;
    }

    void leverOnThePistonStays()
    {
        QFETCH(int, x);
        QFETCH(int, y);
        QFETCH(int, z);
        QFETCH(QString, lever);
        buildArea();
        put(5, 0, 5, u"signal_piston[facing=north]"_s);
        put(x, y, z, lever);
        ticks(1);

        flip(x, y, z);
        ticks(1);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
        QVERIFY(isBlock(5, 0, 4, "signal_piston_head"));

        // The lever keeps hanging on the extended base, so the piston stays out.
        ticks(10);
        QVERIFY2(isBlock(x, y, z, "lever"), qPrintable(look(x, y, z)));
        QVERIFY(flag(x, y, z, u"powered"_s));
        QVERIFY(flag(5, 0, 5, u"extended"_s));
        QCOMPARE(droppedCount(item("qtcraft:lever")), 0);

        // Switched off, the piston retracts and the lever still hangs on it.
        flip(x, y, z);
        ticks(3);
        QVERIFY(!flag(5, 0, 5, u"extended"_s));
        QVERIFY(isBlock(x, y, z, "lever"));
    }

    void dustAndTorchStayOnTheExtendedPiston()
    {
        buildArea();
        put(5, 0, 5, u"signal_piston[facing=east]"_s);
        put(5, 1, 5, u"signal_dust"_s);
        put(5, 0, 6, u"signal_torch[attach=north]"_s);
        put(4, 0, 5, u"lever[attach=east]"_s);
        ticks(1);
        flip(4, 0, 5);
        ticks(5);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
        QVERIFY2(isBlock(5, 1, 5, "signal_dust"), qPrintable(look(5, 1, 5)));
        QVERIFY2(isBlock(5, 0, 6, "signal_torch"), qPrintable(look(5, 0, 6)));
        QVERIFY(isBlock(4, 0, 5, "lever"));
    }

    void ladderOnTheExtendedPistonDrops()
    {
        buildArea();
        // Only signal attachments hold on the short sides of an extended piston; a ladder needs a
        // full face.
        put(5, 0, 5, u"signal_piston[facing=north]"_s);
        put(6, 0, 5, u"ladder[attach=west]"_s);
        put(4, 0, 5, u"lever[attach=floor]"_s);
        ticks(1);
        flip(4, 0, 5);
        ticks(5);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
        QVERIFY2(look(6, 0, 5) == u"air"_s, qPrintable(look(6, 0, 5)));
        QCOMPARE(droppedCount(item("qtcraft:ladder")), 1);
        QVERIFY(isBlock(4, 0, 5, "lever"));
    }

    void leverOnABlockNextToThePiston()
    {
        buildArea();
        put(5, 0, 5, u"signal_piston[facing=north]"_s);
        put(6, 0, 5, u"stone"_s);
        put(7, 0, 5, u"lever[attach=west]"_s);
        ticks(1);
        flip(7, 0, 5);
        ticks(1);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
    }

    void leverTwoAboveWaitsForAnUpdate()
    {
        buildArea();
        // The lever powers the cell above the piston; it is no neighbour of the piston, neither is
        // its block, so the piston waits for an update next to it (quasi-connectivity).
        put(5, 0, 5, u"signal_piston[facing=north]"_s);
        put(6, 2, 5, u"stone"_s);
        put(5, 2, 5, u"lever[attach=east]"_s);
        ticks(1);
        flip(5, 2, 5);
        ticks(5);
        QVERIFY(!flag(5, 0, 5, u"extended"_s));
        put(4, 0, 5, u"stone"_s);
        ticks(1);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
    }

    void leverOnAPushedBlockDrops()
    {
        buildArea();
        put(4, 0, 5, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"signal_piston[facing=east]"_s);
        put(6, 0, 5, u"stone"_s);
        put(6, 1, 5, u"lever[attach=floor]"_s);
        ticks(1);

        flip(4, 0, 5);
        ticks(5);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
        QVERIFY(isBlock(7, 0, 5, "stone"));
        QVERIFY2(look(6, 1, 5) == u"air"_s, qPrintable(look(6, 1, 5)));
        QCOMPARE(droppedCount(item("qtcraft:lever")), 1);
    }

    // Documents the current behaviour (open point of decision E-887): only what hangs on the block
    // next to the head drops. Attachments of further pushed blocks stay where they are, held by the
    // block that moves into their holder's cell; the role model drops them.
    void attachmentsOnTheSecondPushedBlockCurrentlyStay()
    {
        buildArea();
        put(4, 0, 5, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"signal_piston[facing=east]"_s);
        put(6, 0, 5, u"stone"_s);
        put(7, 0, 5, u"stone"_s);
        put(7, 1, 5, u"lever[attach=floor]"_s);
        put(7, 0, 4, u"lever[attach=south]"_s);
        put(7, 0, 6, u"torch[attach=north]"_s);
        ticks(1);

        flip(4, 0, 5);
        ticks(5);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
        QVERIFY(isBlock(7, 0, 5, "stone"));
        QVERIFY(isBlock(8, 0, 5, "stone"));
        // Currently stay (the first block now holds them).
        QVERIFY2(isBlock(7, 1, 5, "lever"), qPrintable(look(7, 1, 5)));
        QVERIFY2(isBlock(7, 0, 4, "lever"), qPrintable(look(7, 0, 4)));
        QVERIFY2(isBlock(7, 0, 6, "torch"), qPrintable(look(7, 0, 6)));
        QCOMPARE(droppedCount(item("qtcraft:lever")), 0);
        QCOMPARE(droppedCount(item("qtcraft:torch")), 0);
    }

    void leverOnAPulledBlockDrops()
    {
        buildArea();
        put(4, 0, 5, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"sticky_signal_piston[facing=east]"_s);
        put(6, 0, 5, u"stone"_s);
        ticks(1);
        flip(4, 0, 5);
        ticks(5);
        QVERIFY(isBlock(7, 0, 5, "stone"));

        put(7, 1, 5, u"signal_dust"_s);
        put(7, 0, 4, u"lever[attach=south]"_s);
        ticks(1);
        flip(4, 0, 5);
        ticks(5);
        QVERIFY(!flag(5, 0, 5, u"extended"_s));
        QVERIFY(isBlock(6, 0, 5, "stone"));
        QVERIFY2(look(7, 1, 5) == u"air"_s, qPrintable(look(7, 1, 5)));
        QVERIFY2(look(7, 0, 4) == u"air"_s, qPrintable(look(7, 0, 4)));
        QCOMPARE(droppedCount(item("qtcraft:lever")), 1);
        QCOMPARE(droppedCount(item("qtcraft:signal_dust")), 1);
    }

    void torchTwoOutWakesAQuasiPoweredPiston_data()
    {
        QTest::addColumn<QString>("piston");
        QTest::addColumn<bool>("extends");
        QTest::addRow("quasi-connectivity") << u"signal_piston"_s << true;
        QTest::addRow("decoupled") << u"decoupled_signal_piston"_s << false;
    }

    void torchTwoOutWakesAQuasiPoweredPiston()
    {
        QFETCH(QString, piston);
        QFETCH(bool, extends);
        buildArea();
        put(5, 0, 6, u"stone"_s);
        quasiPoweredPiston(piston);

        // A torch placed on the far side of the stone: the piston is next to a neighbour of the
        // torch, not next to anything the torch powers.
        put(5, 0, 7, u"signal_torch[attach=north]"_s);
        ticks(1);
        QCOMPARE(flag(5, 0, 5, u"extended"_s), extends);
    }

    void dustTwoOutWakesAQuasiPoweredPiston_data()
    {
        QTest::addColumn<QString>("piston");
        QTest::addColumn<bool>("rule");
        QTest::addColumn<bool>("extends");
        QTest::addRow("quasi-connectivity") << u"signal_piston"_s << true << true;
        QTest::addRow("decoupled") << u"decoupled_signal_piston"_s << true << false;
        QTest::addRow("game rule off") << u"signal_piston"_s << false << false;
    }

    void dustTwoOutWakesAQuasiPoweredPiston()
    {
        QFETCH(QString, piston);
        QFETCH(bool, rule);
        QFETCH(bool, extends);
        buildArea();
        if (!rule) {
            chat(u"/gamerule quasiConnectivity false"_s);
        }
        put(5, 0, 6, u"glass"_s);
        // A straight wire along z = 7: it does not point at the glass next to the piston.
        put(2, 0, 7, u"lever[attach=floor]"_s);
        dustLine(3, 0, 7, 5);
        quasiPoweredPiston(piston);

        flip(2, 0, 7);
        ticks(1);
        QVERIFY(power(5, 0, 7) > 0);
        QCOMPARE(flag(5, 0, 5, u"extended"_s), extends);
    }

    void dustStrengthTwoOutWakesAQuasiPoweredPiston_data()
    {
        QTest::addColumn<QString>("piston");
        QTest::addColumn<bool>("extends");
        QTest::addRow("quasi-connectivity") << u"signal_piston"_s << true;
        QTest::addRow("decoupled") << u"decoupled_signal_piston"_s << false;
    }

    void dustStrengthTwoOutWakesAQuasiPoweredPiston()
    {
        QFETCH(QString, piston);
        QFETCH(bool, extends);
        buildArea();
        put(5, 0, 6, u"glass"_s);
        put(2, 0, 7, u"lever[attach=floor]"_s);
        dustLine(3, 0, 7, 4);
        put(7, 0, 7, u"lever[attach=floor]"_s);
        ticks(1);
        flip(2, 0, 7);
        ticks(2);
        QCOMPARE(power(5, 0, 7), 13);
        quasiPoweredPiston(piston);

        // The dust two out stays powered and only gets stronger.
        flip(7, 0, 7);
        ticks(1);
        QCOMPARE(power(5, 0, 7), 14);
        QCOMPARE(flag(5, 0, 5, u"extended"_s), extends);
    }

    void twoOutWakesAQuasiPoweredDispenser_data()
    {
        QTest::addColumn<QString>("block");
        QTest::addColumn<int>("thrown");
        QTest::addRow("dispenser") << u"signal_dispenser"_s << 1;
        QTest::addRow("dropper") << u"signal_dropper"_s << 1;
        QTest::addRow("decoupled dispenser") << u"decoupled_signal_dispenser"_s << 0;
        QTest::addRow("decoupled dropper") << u"decoupled_signal_dropper"_s << 0;
    }

    void twoOutWakesAQuasiPoweredDispenser()
    {
        QFETCH(QString, block);
        QFETCH(int, thrown);
        buildArea();
        put(5, 0, 6, u"stone"_s);
        put(5, 0, 5, block + u"[facing=north]"_s);
        fill(5, 0, 5, {stack("qtcraft:stick", 4)});
        powerAbove();
        ticks(10);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 0);

        put(5, 0, 7, u"signal_torch[attach=north]"_s);
        ticks(10);
        QCOMPARE(droppedCount(item("qtcraft:stick")), thrown);
    }

    void dustStairOverGlassCarriesUpOnly_data()
    {
        QTest::addColumn<QString>("step");
        QTest::addColumn<bool>("down");
        QTest::addRow("glass") << u"glass"_s << false;
        QTest::addRow("stone") << u"stone"_s << true;
    }

    void dustStairOverGlassCarriesUpOnly()
    {
        QFETCH(QString, step);
        QFETCH(bool, down);
        buildArea();
        // Up: from the floor onto the step and on along a block.
        put(1, 0, 5, u"lever[attach=floor]"_s);
        put(2, 0, 5, u"signal_dust"_s);
        put(3, 0, 5, step);
        put(3, 1, 5, u"signal_dust"_s);
        put(4, 0, 5, u"stone"_s);
        put(4, 1, 5, u"signal_dust"_s);
        // Down: from a block onto the step and down to the floor.
        put(6, 0, 9, u"stone"_s);
        put(6, 1, 9, u"lever[attach=floor]"_s);
        put(5, 0, 9, u"stone"_s);
        put(5, 1, 9, u"signal_dust"_s);
        put(4, 0, 9, step);
        put(4, 1, 9, u"signal_dust"_s);
        put(3, 0, 9, u"signal_dust"_s);
        ticks(1);

        flip(1, 0, 5);
        flip(6, 1, 9);
        ticks(2);
        QCOMPARE(power(2, 0, 5), 15);
        QCOMPARE(power(3, 1, 5), 14);
        QCOMPARE(power(4, 1, 5), 13);
        QCOMPARE(power(5, 1, 9), 15);
        QCOMPARE(power(4, 1, 9), 14);
        QCOMPARE(power(3, 0, 9), down ? 13 : 0);
        // The dust still looks connected down the step, as in the role model.
        QVERIFY(property(4, 1, 9, u"west"_s) != u"none"_s);
    }

    void dustOutsideTheRegionOnGlassFeedsUpOnly_data()
    {
        QTest::addColumn<QString>("step");
        QTest::addColumn<int>("below");
        QTest::addRow("glass") << u"glass"_s << 0;
        QTest::addRow("stone") << u"stone"_s << 13;
    }

    void dustOutsideTheRegionOnGlassFeedsUpOnly()
    {
        QFETCH(QString, step);
        QFETCH(int, below);
        buildArea();
        // Down from a block over the step onto the floor at (1, 0, 5).
        put(1, 0, 2, u"stone"_s);
        put(1, 1, 2, u"lever[attach=floor]"_s);
        put(1, 0, 3, u"stone"_s);
        put(1, 1, 3, u"signal_dust"_s);
        put(1, 0, 4, step);
        put(1, 1, 4, u"signal_dust"_s);
        ticks(1);
        flip(1, 1, 2);
        ticks(2);
        QCOMPARE(power(1, 1, 4), 14);

        // A floor wire 15 links long from (1, 0, 5) to (12, 0, 7), placed last: its region ends at
        // (1, 0, 5), and the dust on the step just outside it feeds the region from there.
        dustLine(1, 0, 5, 13);
        put(13, 0, 6, u"signal_dust"_s);
        put(13, 0, 7, u"signal_dust"_s);
        ticks(2);
        QCOMPARE(power(1, 0, 5), below);
        put(12, 0, 7, u"signal_dust"_s);
        ticks(2);
        QCOMPARE(power(1, 0, 5), below);
        QCOMPARE(power(2, 0, 5), std::max(below - 1, 0));
        QCOMPARE(power(12, 0, 7), 0);
    }

    void comparatorSideReadsTheDustBlock_data()
    {
        QTest::addColumn<QString>("side");
        QTest::addColumn<int>("output");
        QTest::addRow("dust block") << u"signal_dust_block"_s << 0;
        QTest::addRow("nothing") << u"air"_s << 15;
    }

    void comparatorSideReadsTheDustBlock()
    {
        QFETCH(QString, side);
        QFETCH(int, output);
        buildArea();
        put(4, 0, 5, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"signal_comparator[facing=east,mode=subtract]"_s);
        put(5, 0, 4, side);
        put(6, 0, 5, u"signal_dust"_s);
        ticks(1);
        flip(4, 0, 5);
        ticks(6);
        QCOMPARE(power(5, 0, 5), output);
        QCOMPARE(power(6, 0, 5), output);
    }
};

QTEST_GUILESS_MAIN(TestSignalParity)
#include "test_signal_parity.moc"
