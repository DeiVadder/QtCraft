#include "QcMachineFixture.h"

using namespace Qt::StringLiterals;

// Signal Hoppers on a running server (decision E-344): transfer order and cooldown, furnace sides,
// the lock, pickup through blocks that do not fill their cell and none under a full block, sleeping
// hoppers that cost nothing, and no item lost or copied in chains, races and in every turn.
class TestSignalHoppers : public QcMachineFixture
{
    Q_OBJECT

private:
    // Every container of the area as text, turned back.
    QString contents()
    {
        QString text;
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < areaSize; ++z) {
                for (int x = 0; x < areaSize; ++x) {
                    const QList<QcItemStack> held = slotsAt(x, y, z);
                    for (const QcItemStack &slot : held) {
                        text += u"%1:%2,"_s.arg(slot.item).arg(slot.count);
                    }
                    text += u';';
                }
            }
        }
        return text;
    }

    // Two hoppers feed one chest from opposite sides and one from above; the chest has room for
    // fewer items than come, a hopper below the left feeder races it for its last item.
    void race()
    {
        put(5, 1, 5, u"chest[facing=south]"_s);
        QList<QcItemStack> almostFull(27, stack("qtcraft:cobblestone", 64));
        almostFull[26] = stack("qtcraft:cobblestone", 62);
        fill(5, 1, 5, almostFull);
        put(4, 1, 5, u"signal_hopper[facing=east]"_s);
        put(6, 1, 5, u"signal_hopper[facing=west]"_s);
        put(5, 2, 5, u"signal_hopper[facing=down]"_s);
        put(4, 0, 5, u"signal_hopper[facing=north]"_s);
        put(4, 0, 4, u"barrel[facing=north]"_s);
        fill(4, 1, 5, {stack("qtcraft:cobblestone", 1)});
        fill(6, 1, 5, {stack("qtcraft:cobblestone", 3)});
        fill(5, 2, 5, {stack("qtcraft:cobblestone", 2), stack("qtcraft:dirt", 1)});
    }

    // One unit of an item sorter at local x below a line of hoppers at z = line, y = 3 (the showcase's
    // design, decision E-344). The filter hopper under the line holds 18 of its item and four stacks
    // of 24 junk (reading 5); one more item makes it 6, which reaches through five dust a repeater.
    // Its 15 goes into the side of a comparator in subtract mode with a lever behind it: that one puts
    // out 15 and locks the hopper below the filter, and 0 while the filter holds one too many, so the
    // hopper takes one item into the chest. No torch, so no burnout at full hopper speed.
    void sorterUnit(int x, int line, const char *filter, const char *junk)
    {
        put(x, 0, line, u"chest[facing=south]"_s);
        put(x, 1, line, u"signal_hopper[facing=down,enabled=false]"_s);
        put(x, 2, line, u"signal_hopper[facing=south]"_s);
        fill(x, 2, line, {stack(filter, 18), stack(junk, 24), stack(junk, 24), stack(junk, 24), stack(junk, 24)});
        put(x + 1, 1, line, u"stone"_s);
        put(x + 1, 2, line, u"signal_comparator[facing=east]"_s);
        for (const auto &[dx, dz] : {std::pair{2, 0}, std::pair{3, 0}, std::pair{3, -1}}) {
            put(x + dx, 1, line + dz, u"glass"_s);
            put(x + dx, 2, line + dz, u"signal_dust"_s);
        }
        // Keeps the first dust from linking down to the last one.
        put(x + 2, 2, line - 1, u"stone"_s);
        for (const auto &[dx, dz] : {std::pair{3, -2}, std::pair{2, -2}, std::pair{2, -1}}) {
            put(x + dx, 0, line + dz, u"stone"_s);
            put(x + dx, 1, line + dz, u"signal_dust"_s);
        }
        put(x + 1, 0, line - 1, u"stone"_s);
        put(x + 1, 1, line - 1, u"signal_repeater[facing=west,delay=1]"_s);
        put(x, 0, line - 1, u"stone"_s);
        put(x, 1, line - 1, u"signal_comparator[facing=south,mode=subtract,powered=true,power=15]"_s);
        put(x, 0, line - 2, u"stone"_s);
        put(x, 1, line - 2, u"lever[attach=floor,powered=true]"_s);
    }

    // Two sorter units (five blocks apart: a unit's last dust must not touch the next lever) on a line of hoppers from an input chest to an overflow chest; each kind comes
    // at full hopper speed, one after the other.
    void sorter()
    {
        constexpr int line = 6;
        sorterUnit(2, line, "qtcraft:wheat", "qtcraft:string");
        sorterUnit(7, line, "qtcraft:cobblestone", "qtcraft:string");
        for (int x = 1; x <= 10; ++x) {
            put(x, 3, line, u"signal_hopper[facing=east]"_s);
        }
        put(1, 4, line, u"chest[facing=south]"_s);
        put(11, 3, line, u"chest[facing=south]"_s);
        fill(1, 4, line, {stack("qtcraft:wheat", 20), stack("qtcraft:cobblestone", 20), stack("qtcraft:stick", 5)});
    }

private slots:
    void sorterSortsItsItems()
    {
        QString reference;
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            sorter();
            ticks(900);
            // The last item of a unit waits in its locked hopper until the next one comes (as in the
            // reference: a hopper passes on what it took only at its next transfer).
            QCOMPARE(countIn(2, 0, 6, "qtcraft:wheat") + countIn(2, 1, 6, "qtcraft:wheat"), 20);
            QCOMPARE(countIn(7, 0, 6, "qtcraft:cobblestone") + countIn(7, 1, 6, "qtcraft:cobblestone"), 20);
            QCOMPARE(countIn(11, 3, 6, "qtcraft:stick"), 5);
            QCOMPARE(countIn(11, 3, 6, "qtcraft:wheat") + countIn(11, 3, 6, "qtcraft:cobblestone"), 0);
            QCOMPARE(countIn(2, 2, 6, "qtcraft:wheat"), 18);
            QCOMPARE(countIn(7, 2, 6, "qtcraft:cobblestone"), 18);
            QCOMPARE(everywhere("qtcraft:wheat"), 38);
            if (reference.isEmpty()) {
                reference = contents();
            }
            QCOMPARE(contents(), reference);
            cleanup();
        }
    }

    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void movesOneItemEveryEightTicks()
    {
        buildArea();
        put(2, 1, 2, u"chest[facing=south]"_s);
        fill(2, 1, 2, {stack("qtcraft:cobblestone", 10)});
        put(2, 0, 2, u"signal_hopper[facing=east]"_s);
        put(3, 0, 2, u"barrel[facing=north]"_s);

        // Pulls at once, then each 8 ticks pushes one on and pulls the next.
        ticks(1);
        QCOMPARE(countIn(2, 0, 2, "qtcraft:cobblestone"), 1);
        QCOMPARE(countIn(3, 0, 2, "qtcraft:cobblestone"), 0);
        ticks(7);
        QCOMPARE(countIn(3, 0, 2, "qtcraft:cobblestone"), 0);
        ticks(1);
        QCOMPARE(countIn(3, 0, 2, "qtcraft:cobblestone"), 1);
        QCOMPARE(countIn(2, 0, 2, "qtcraft:cobblestone"), 1);
        ticks(8 * 9);
        QCOMPARE(countIn(3, 0, 2, "qtcraft:cobblestone"), 10);
        QCOMPARE(countIn(2, 1, 2, "qtcraft:cobblestone"), 0);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 10);
    }

    void firstSlotThatFitsGoes()
    {
        buildArea();
        put(2, 0, 2, u"signal_hopper[facing=east]"_s);
        put(3, 0, 2, u"furnace[facing=south]"_s);
        // Dirt is no fuel: the coal behind it goes into the fuel slot from the side.
        fill(2, 0, 2, {stack("qtcraft:dirt", 4), stack("qtcraft:coal", 2)});
        ticks(1);
        QCOMPARE(slotsAt(3, 0, 2).value(QcMachineManager::fuelSlot), stack("qtcraft:coal", 1));
        QCOMPARE(slotsAt(3, 0, 2).value(QcMachineManager::inputSlot), QcItemStack{});
        QCOMPARE(countIn(2, 0, 2, "qtcraft:dirt"), 4);
    }

    void furnaceSides()
    {
        buildArea();
        put(4, 1, 4, u"furnace[facing=south]"_s);
        put(4, 2, 4, u"signal_hopper[facing=down]"_s);
        put(4, 0, 4, u"signal_hopper[facing=east]"_s);
        put(5, 0, 4, u"barrel[facing=north]"_s);
        fill(4, 2, 4, {stack("qtcraft:iron_ore", 2)});
        fill(4, 1, 4, {{}, stack("qtcraft:coal", 4), stack("qtcraft:iron_ingot", 3)});

        // From above into the input; from below out of the output only.
        ticks(1);
        QCOMPARE(slotsAt(4, 1, 4).value(QcMachineManager::inputSlot).item, item("qtcraft:iron_ore"));
        QCOMPARE(countIn(4, 0, 4, "qtcraft:iron_ingot"), 1);
        QCOMPARE(countIn(4, 0, 4, "qtcraft:coal"), 0);
        ticks(800);
        QCOMPARE(everywhere("qtcraft:iron_ore") + everywhere("qtcraft:iron_ingot"), 5);
        QCOMPARE(countIn(5, 0, 4, "qtcraft:iron_ingot"), 5);
    }

    void emptyBucketLeavesTheFuelSlot()
    {
        buildArea();
        put(4, 1, 4, u"furnace[facing=south]"_s);
        put(4, 0, 4, u"signal_hopper[facing=east]"_s);
        put(5, 0, 4, u"barrel[facing=north]"_s);
        // A burnt lava bucket leaves its bucket; coal stays in the fuel slot.
        fill(4, 1, 4, {{}, stack("qtcraft:bucket", 1), {}});
        ticks(20);
        QCOMPARE(countIn(5, 0, 4, "qtcraft:bucket"), 1);
        fill(4, 1, 4, {{}, stack("qtcraft:coal", 3), {}});
        ticks(20);
        QCOMPARE(countIn(4, 1, 4, "qtcraft:coal"), 3);
    }

    void receivingHopperDoesNotPullInTheSameTick()
    {
        buildArea();
        put(2, 0, 2, u"signal_hopper[facing=east]"_s);
        put(3, 0, 2, u"signal_hopper[facing=east,enabled=true]"_s);
        put(3, 1, 2, u"chest[facing=south]"_s);
        put(3, 0, 1, u"lever[attach=floor,powered=true]"_s);
        fill(3, 1, 2, {stack("qtcraft:dirt", 5)});
        ticks(3);
        // Locked while being set up, now free: the left one pushes into the empty right one.
        fill(2, 0, 2, {stack("qtcraft:stick", 1)});
        flip(3, 0, 1);
        ticks(1);
        ticks(1);
        QCOMPARE(countIn(3, 0, 2, "qtcraft:stick") + countIn(3, 0, 2, "qtcraft:dirt"), 1);
    }

    void signalLocksTheHopper()
    {
        buildArea();
        put(2, 1, 2, u"chest[facing=south]"_s);
        fill(2, 1, 2, {stack("qtcraft:cobblestone", 10)});
        put(2, 0, 2, u"signal_hopper[facing=east]"_s);
        put(3, 0, 2, u"barrel[facing=north]"_s);
        put(2, 0, 3, u"lever[attach=floor,powered=true]"_s);
        ticks(20);
        QVERIFY(!flag(2, 0, 2, u"enabled"_s));
        QCOMPARE(countIn(2, 0, 2, "qtcraft:cobblestone"), 0);

        flip(2, 0, 3);
        ticks(2);
        QVERIFY(flag(2, 0, 2, u"enabled"_s));
        QCOMPARE(countIn(2, 0, 2, "qtcraft:cobblestone"), 1);
    }

    void collectsThroughBlocksThatDoNotFillTheirCell_data()
    {
        QTest::addColumn<QString>("above");
        QTest::addColumn<double>("height");
        QTest::addColumn<bool>("collected");
        QTest::newRow("bowl") << u"air"_s << 0.6875 << true;
        QTest::newRow("slab") << u"stone_slab[type=bottom]"_s << 1.5 << true;
        QTest::newRow("carpet") << u"white_wool_carpet"_s << 1.0625 << true;
        QTest::newRow("farmland") << u"farmland"_s << 1.9375 << true;
        QTest::newRow("glass") << u"glass"_s << 2.0 << false;
        QTest::newRow("stone") << u"stone"_s << 2.0 << false;
    }

    void collectsThroughBlocksThatDoNotFillTheirCell()
    {
        QFETCH(QString, above);
        QFETCH(double, height);
        QFETCH(bool, collected);
        buildArea();
        put(2, 0, 2, u"signal_hopper[facing=east]"_s);
        put(2, 1, 2, above);
        ticks(2);

        drop(2, height, 2, stack("qtcraft:stick", 3));
        ticks(3);
        QCOMPARE(countIn(2, 0, 2, "qtcraft:stick"), collected ? 3 : 0);
        QCOMPARE(everywhere("qtcraft:stick"), 3);
    }

    void fullBlockAboveMeansNoSearch()
    {
        buildArea();
        for (int x = 0; x < 10; ++x) {
            put(x, 0, 2, u"signal_hopper[facing=east]"_s);
            put(x, 1, 2, u"stone"_s);
        }
        ticks(1);
        QCOMPARE(hopperStats().acting, 10);
        QCOMPARE(hopperStats().searches, 0);
        // Nothing to do: all asleep, nothing woken.
        ticks(1);
        QCOMPARE(hopperStats().woken, 0);
        QCOMPARE(hopperStats().acting, 0);
    }

    void idleHoppersSleep()
    {
        buildArea();
        for (int x = 0; x < 10; ++x) {
            put(x, 0, 4, u"signal_hopper[facing=down]"_s);
        }
        ticks(1);
        QCOMPARE(hopperStats().searches, 10);
        // Asleep: nothing is woken or searched in any of the following ticks.
        qsizetype woken = 0;
        qsizetype searches = 0;
        for (int tick = 0; tick < 20; ++tick) {
            ticks(1);
            woken += hopperStats().woken;
            searches += hopperStats().searches;
        }
        QCOMPARE(woken, 0);
        QCOMPARE(searches, 0);

        // An item falling in wakes only its hopper (each tick it moves); the two next to it wake once
        // when it lands in its hopper (a container next to them changed).
        drop(3, 2.0, 4, stack("qtcraft:stick", 1));
        qsizetype most = 0;
        for (int tick = 0; tick < 15; ++tick) {
            ticks(1);
            most = std::max(most, hopperStats().woken);
        }
        QCOMPARE(countIn(3, 0, 4, "qtcraft:stick"), 1);
        QVERIFY2(most <= 3, qPrintable(QString::number(most)));
    }

    void pistonsMoveMachinesWithTheirContents_data()
    {
        QTest::addColumn<QString>("machine");
        QTest::newRow("hopper") << u"signal_hopper[facing=down]"_s;
        QTest::newRow("dispenser") << u"signal_dispenser[facing=up]"_s;
        QTest::newRow("dropper") << u"signal_dropper[facing=up]"_s;
    }

    void pistonsMoveMachinesWithTheirContents()
    {
        QFETCH(QString, machine);
        buildArea();
        put(1, 0, 5, u"lever[attach=floor]"_s);
        put(2, 0, 5, u"signal_piston[facing=east]"_s);
        put(3, 0, 5, machine);
        fill(3, 0, 5, {stack("qtcraft:diamond", 5)});
        ticks(1);

        // A player looking into it: the screen closes when it moves.
        const QcBlockPos opened = at(3, 0, 5);
        const QcBlockPos stand = at(3, 0, 7);
        chat(u"/tp %1 %2 %3"_s.arg(stand.x + 0.5).arg(stand.y).arg(stand.z + 0.5));
        idle(2);
        send(QcMsg::UseItem{opened, QcBlockFace::South});
        QVERIFY(!m_client->machineClosed);
        QCOMPARE(m_client->machineStacks.value(0).count, 5);

        flip(1, 0, 5);
        idle(3);
        QVERIFY(m_client->machineClosed);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:diamond"), 5);
        QCOMPARE(everywhere("qtcraft:diamond"), 5);
    }

    void fullContainerTableLosesNothing()
    {
        buildArea();
        put(2, 0, 2, u"signal_hopper[facing=east]"_s);
        put(3, 0, 2, u"chest[facing=south]"_s);
        fill(2, 0, 2, {stack("qtcraft:cobblestone", 3)});
        put(2, 0, 5, u"signal_dropper[facing=east]"_s);
        put(3, 0, 5, u"chest[facing=south]"_s);
        put(2, 0, 6, u"lever[attach=floor]"_s);
        fill(2, 0, 5, {stack("qtcraft:stick", 2)});
        // Entries for chests nobody opened, far away, up to the limit: the chests in front have none.
        for (int i = 0; !m_server->containers().isFull(); ++i) {
            QVERIFY(m_server->containers().add({100000 + i, 70, 100000}, 1));
        }
        const QcBlockPos spare{100000, 70, 100000};
        ticks(20);
        flip(2, 0, 6);
        ticks(10);
        QCOMPARE(countIn(2, 0, 2, "qtcraft:cobblestone"), 3);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 3);
        // The dropper has no container in front: it throws instead.
        QCOMPARE(everywhere("qtcraft:stick"), 2);

        // Room again: the next push goes in.
        m_server->containers().remove(spare);
        put(2, 1, 2, u"stone"_s);
        ticks(20);
        QVERIFY(countIn(3, 0, 2, "qtcraft:cobblestone") >= 1);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 3);
    }

    void neverLosesOrCopies()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            race();
            for (int tick = 0; tick < 120; ++tick) {
                ticks(1);
                QCOMPARE(everywhere("qtcraft:cobblestone"), 26 * 64 + 62 + 6);
                QCOMPARE(everywhere("qtcraft:dirt"), 1);
            }
            cleanup();
        }
    }

    void sameInEveryTurnAndPlace()
    {
        QStringList reference;
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            for (const int shift : {0, 37}) {
                buildArea();
                clearArea(m_origin.offset(shift, 0, -shift));
                race();
                QStringList frames;
                for (int tick = 0; tick < 60; ++tick) {
                    ticks(1);
                    frames.append(contents());
                }
                if (reference.isEmpty()) {
                    reference = frames;
                }
                QCOMPARE(frames, reference);
                cleanup();
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestSignalHoppers)
#include "test_signal_hoppers.moc"
