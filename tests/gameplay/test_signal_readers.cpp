#include "common/content/registry/QcContentSerializer.h"
#include "QcMachineFixture.h"
#include "QcTestUtil.h"

using namespace Qt::StringLiterals;

// Signal Comparators and Observers on a running server (decision E-343): the reference formula for
// container fullness, reading through one block, readable block states from data, compare and
// subtract with side inputs, the 2-tick delay; observers pulsing 2 ticks after the block in front
// changed for 2 ticks, chained and as a clock, the same in every turn.
class TestSignalReaders : public QcMachineFixture
{
    Q_OBJECT

private:
    // Ticks until `done` holds (at most `limit`), or -1.
    template<typename Done>
    int ticksUntil(Done done, int limit = 40)
    {
        for (int tick = 1; tick <= limit; ++tick) {
            ticks(1);
            if (done()) {
                return tick;
            }
        }
        return -1;
    }

    // A comparator at (5, 0, 5) facing east reading what stands at (4, 0, 5).
    void comparatorBehind(const QString &container)
    {
        put(4, 0, 5, container);
        put(5, 0, 5, u"signal_comparator[facing=east]"_s);
        put(6, 0, 5, u"signal_lamp"_s);
    }

    // Two observers looking at each other: a clock once something starts it.
    void observerClock()
    {
        put(4, 0, 4, u"signal_observer[facing=east]"_s);
        put(5, 0, 4, u"signal_observer[facing=west]"_s);
        put(3, 0, 4, u"signal_lamp"_s);
        put(6, 0, 4, u"signal_lamp"_s);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void fullnessFormula_data()
    {
        QTest::addColumn<QString>("container");
        QTest::addColumn<QList<QcItemStack>>("stacks");
        QTest::addColumn<int>("strength");
        const QcItemStack stone{*QcTestUtil::loadContent()->itemId(u"qtcraft:cobblestone"_s), 64, 0};
        const QcItemStack one{stone.item, 1, 0};
        const QcItemStack bucket{*QcTestUtil::loadContent()->itemId(u"qtcraft:water_bucket"_s), 1, 0};
        const QcItemStack coal{*QcTestUtil::loadContent()->itemId(u"qtcraft:coal"_s), 64, 0};
        QTest::newRow("empty chest") << u"chest[facing=south]"_s << QList<QcItemStack>{} << 0;
        QTest::newRow("one item") << u"chest[facing=south]"_s << QList<QcItemStack>{one} << 1;
        QTest::newRow("one stack") << u"chest[facing=south]"_s << QList<QcItemStack>{stone} << 1;
        QTest::newRow("two stacks") << u"chest[facing=south]"_s << QList<QcItemStack>{stone, stone} << 2;
        QTest::newRow("half chest") << u"chest[facing=south]"_s << QList<QcItemStack>(14, stone) << 8;
        QTest::newRow("full chest") << u"chest[facing=south]"_s << QList<QcItemStack>(27, stone) << 15;
        QTest::newRow("hopper 22") << u"signal_hopper[facing=down,enabled=false]"_s << QList<QcItemStack>{{stone.item, 22, 0}} << 1;
        QTest::newRow("hopper 23") << u"signal_hopper[facing=down,enabled=false]"_s << QList<QcItemStack>{{stone.item, 23, 0}} << 2;
        QTest::newRow("hopper bucket") << u"signal_hopper[facing=down,enabled=false]"_s << QList<QcItemStack>{bucket} << 3;
        QTest::newRow("furnace fuel") << u"furnace[facing=south]"_s << QList<QcItemStack>{{}, coal} << 5;
        QTest::newRow("full dropper") << u"signal_dropper[facing=up]"_s << QList<QcItemStack>(9, stone) << 15;
    }

    void fullnessFormula()
    {
        QFETCH(QString, container);
        QFETCH(QList<QcItemStack>, stacks);
        QFETCH(int, strength);
        buildArea();
        // A hopper above the furnace would pull: the furnace stands alone, lit by nothing.
        comparatorBehind(container);
        fill(4, 0, 5, stacks);
        ticks(4);
        QCOMPARE(power(5, 0, 5), strength);
        QCOMPARE(flag(5, 0, 5, u"powered"_s), strength > 0);
        QCOMPARE(isBlock(6, 0, 5, "signal_lamp_lit"), strength > 0);
    }

    void readsThroughOneBlockOnly()
    {
        buildArea();
        put(2, 0, 5, u"chest[facing=south]"_s);
        put(3, 0, 5, u"stone"_s);
        put(4, 0, 5, u"signal_comparator[facing=east]"_s);
        fill(2, 0, 5, QList<QcItemStack>(27, stack("qtcraft:cobblestone", 64)));
        ticks(4);
        QCOMPARE(power(4, 0, 5), 15);

        // Two blocks between: nothing.
        put(1, 0, 5, u"chest[facing=south]"_s);
        put(2, 0, 5, u"stone"_s);
        fill(1, 0, 5, QList<QcItemStack>(27, stack("qtcraft:cobblestone", 64)));
        ticks(4);
        QCOMPARE(power(4, 0, 5), 0);
    }

    void readsBlockStatesFromData()
    {
        buildArea();
        put(4, -1, 5, u"farmland"_s);
        put(4, 0, 5, u"wheat_crop[age=3]"_s);
        put(5, 0, 5, u"signal_comparator[facing=east]"_s);
        ticks(4);
        QCOMPARE(power(5, 0, 5), 3 * 15 / 7);
        put(4, 0, 5, u"wheat_crop[age=7]"_s);
        ticks(4);
        QCOMPARE(power(5, 0, 5), 15);
    }

    void compareAndSubtract()
    {
        buildArea();
        comparatorBehind(u"chest[facing=south]"_s);
        fill(4, 0, 5, QList<QcItemStack>(27, stack("qtcraft:cobblestone", 64)));
        // A side input of 13: lever, then three dust towards the comparator's north side.
        put(5, 0, 1, u"lever[attach=floor,powered=true]"_s);
        dustLine(5, 0, 2, 1);
        put(5, 0, 3, u"signal_dust"_s);
        put(5, 0, 4, u"signal_dust"_s);
        ticks(6);
        QCOMPARE(power(5, 0, 4), 13);
        QCOMPARE(power(5, 0, 5), 15);

        QVERIFY(m_server->systems().circuits.use(at(5, 0, 5)));
        ticks(4);
        QCOMPARE(property(5, 0, 5, u"mode"_s), u"subtract"_s);
        QCOMPARE(power(5, 0, 5), 2);

        // Compare mode with a stronger side: off.
        QVERIFY(m_server->systems().circuits.use(at(5, 0, 5)));
        QList<QcItemStack> half(27);
        std::fill_n(half.begin(), 14, stack("qtcraft:cobblestone", 64));
        fill(4, 0, 5, half);
        ticks(4);
        QCOMPARE(power(5, 0, 5), 0);
        // The lamp goes dark after its delay.
        ticks(6);
        QVERIFY(isBlock(6, 0, 5, "signal_lamp"));
    }

    void switchesTwoTicksAfterItsInput()
    {
        buildArea();
        comparatorBehind(u"chest[facing=south]"_s);
        ticks(4);
        fill(4, 0, 5, {stack("qtcraft:cobblestone", 1)});
        // The change is seen in the next tick; the comparator switches 2 ticks later.
        QCOMPARE(ticksUntil([this] { return power(5, 0, 5) == 1; }), 3);
    }

    void followsAHopperEmptyingAChest()
    {
        buildArea();
        comparatorBehind(u"chest[facing=south]"_s);
        put(4, -1, 5, u"signal_hopper[facing=east]"_s);
        put(5, -1, 5, u"barrel[facing=north]"_s);
        fill(4, 0, 5, {stack("qtcraft:cobblestone", 2)});
        ticks(3);
        QCOMPARE(power(5, 0, 5), 1);
        ticks(30);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:cobblestone"), 0);
        QCOMPARE(power(5, 0, 5), 0);
    }

    void observerPulsesTwoTicksAfterAChange()
    {
        buildArea();
        put(5, 0, 5, u"signal_observer[facing=west]"_s);
        put(6, 0, 5, u"signal_lamp"_s);
        ticks(4);
        QVERIFY(!flag(5, 0, 5, u"powered"_s));

        put(4, 0, 5, u"stone"_s);
        QCOMPARE(ticksUntil([this] { return flag(5, 0, 5, u"powered"_s); }), 3);
        QVERIFY(isBlock(6, 0, 5, "signal_lamp_lit"));
        QCOMPARE(ticksUntil([this] { return !flag(5, 0, 5, u"powered"_s); }), 2);
        ticks(20);
        QVERIFY(!flag(5, 0, 5, u"powered"_s));
    }

    void observerSeesStatesNotContents()
    {
        buildArea();
        put(4, 0, 5, u"chest[facing=south]"_s);
        put(5, 0, 5, u"signal_observer[facing=west]"_s);
        ticks(4);
        fill(4, 0, 5, {stack("qtcraft:cobblestone", 5)});
        QCOMPARE(ticksUntil([this] { return flag(5, 0, 5, u"powered"_s); }, 10), -1);

        // Dust changing its strength is a new state.
        put(4, 0, 5, u"signal_dust"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        ticks(10);
        flip(4, 0, 6);
        QVERIFY(ticksUntil([this] { return flag(5, 0, 5, u"powered"_s); }, 6) > 0);
    }

    void observersChain()
    {
        buildArea();
        put(5, 0, 5, u"signal_observer[facing=west]"_s);
        put(6, 0, 5, u"signal_observer[facing=west]"_s);
        ticks(4);
        put(4, 0, 5, u"stone"_s);
        const int first = ticksUntil([this] { return flag(5, 0, 5, u"powered"_s); });
        const int second = ticksUntil([this] { return flag(6, 0, 5, u"powered"_s); });
        QCOMPARE(first, 3);
        QCOMPARE(second, 2);
    }

    void observerClockRunsTheSameInEveryTurn()
    {
        QList<int> reference;
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            observerClock();
            QList<int> onTicks;
            for (int tick = 0; tick < 60; ++tick) {
                const bool before = flag(4, 0, 4, u"powered"_s);
                ticks(1);
                if (!before && flag(4, 0, 4, u"powered"_s)) {
                    onTicks.append(tick);
                }
            }
            // It keeps running.
            QVERIFY2(onTicks.size() >= 8, qPrintable(QString::number(onTicks.size())));
            QVERIFY(onTicks.constLast() >= 50);
            if (reference.isEmpty()) {
                reference = onTicks;
            }
            QCOMPARE(onTicks, reference);
            cleanup();
        }
    }

    void clientsLearnKindsColumnsAndFacing()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> copy = QcContentSerializer::read(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        const QcBlockDefinition &hopper = copy->block(*copy->blockId(u"qtcraft:signal_hopper"_s));
        QCOMPARE(hopper.signal.kind, QcSignalKind::Hopper);
        QCOMPARE(hopper.containerColumns, 5);
        QCOMPARE(hopper.facingRule, QcFacingRule::Into);
        QCOMPARE(copy->block(*copy->blockId(u"qtcraft:signal_dispenser"_s)).containerColumns, 3);
        QVERIFY(copy->block(*copy->blockId(u"qtcraft:signal_comparator"_s)).signal.isUsable());
        QCOMPARE(copy->block(*copy->blockId(u"qtcraft:signal_observer"_s)).signal.kind, QcSignalKind::Observer);
    }
};

QTEST_GUILESS_MAIN(TestSignalReaders)
#include "test_signal_readers.moc"
