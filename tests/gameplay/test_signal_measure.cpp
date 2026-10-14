#include "QcServerHoppers.h"
#include "QcServerTestFixture.h"

#include <QElapsedTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

// Headless measurement of the signal phase (decisions E-335, E-344): work and milliseconds per
// server tick for a long dust line toggled every tick, a dust field, many repeater clocks, many
// pistons, hoppers idle, under full blocks and with items raining in, an item sorter and observer
// clocks. The sizes are small by default (a quick check that the scenarios work);
// QTCRAFT_SIGNAL_MEASURE=full runs the sizes of the performance record (4096 dust, 64 x 64, 1000
// clocks, 500 pistons, 1000 hoppers, 200 sorted items, 500 observers), meant for a Release build.
// Only the behaviour is checked, never the times.
class TestSignalMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcBlockPos m_base;
    bool m_full = qEnvironmentVariable("QTCRAFT_SIGNAL_MEASURE") == u"full"_s;

    QcBlockId state(const QString &text) const { return *m_content->stateId(u"qtcraft:"_s + text); }

    // Sets a block like a player's change; the chunk stays loaded (it is changed).
    void put(int x, int z, const QString &text, int y = 0)
    {
        const QcBlockPos pos = m_base.offset(x, y, z);
        m_server->world().loadChunk(pos.chunk());
        if (m_server->world().blockAt(pos.offset(0, -1, 0)) == qcAirBlockId) {
            m_server->world().setBlock(pos.offset(0, -1, 0), state(u"stone"_s));
        }
        const QcBlockId before = m_server->world().blockAt(pos).value_or(qcAirBlockId);
        m_server->world().setBlock(pos, state(text));
        m_server->circuitState().noteChange(m_server->world(), pos, before, state(text));
    }

    // Loads the chunks of a box (with a margin) and keeps them loaded.
    void prepare(int sizeX, int sizeZ)
    {
        for (int x = -16; x <= sizeX + 16; x += 8) {
            for (int z = -16; z <= sizeZ + 16; z += 8) {
                const QcBlockPos pos = m_base.offset(x, -2, z);
                m_server->world().loadChunk(pos.chunk());
                m_server->world().setBlock(pos, state(u"stone"_s));
            }
        }
    }

    struct Sample
    {
        double meanMs = 0.0;
        double maxMs = 0.0;
        double meanWork = 0.0;
        qsizetype maxWork = 0;
    };

    // Runs `count` ticks, calling `before` ahead of each, and measures the whole tick.
    template<typename Before>
    Sample measure(int count, Before before)
    {
        Sample sample;
        QElapsedTimer timer;
        for (int tick = 0; tick < count; ++tick) {
            before(tick);
            timer.start();
            m_server->tick();
            const double ms = double(timer.nsecsElapsed()) / 1e6;
            pump();
            const qsizetype work = m_server->circuits().lastTickWork();
            sample.meanMs += ms / count;
            sample.maxMs = std::max(sample.maxMs, ms);
            sample.meanWork += double(work) / count;
            sample.maxWork = std::max(sample.maxWork, work);
        }
        return sample;
    }

    struct HopperSample
    {
        Sample ticks;
        double meanActing = 0.0;
        double meanSearches = 0.0;
        double meanWoken = 0.0;
    };

    // The same for hoppers, with what they did per tick.
    template<typename Before>
    HopperSample measureHoppers(int count, Before before)
    {
        HopperSample sample;
        sample.ticks = measure(count, [&](int tick) {
            before(tick);
            if (tick > 0) {
                const QcServerHoppers::Stats &stats = m_server->circuits().hoppers().lastTick();
                sample.meanActing += double(stats.acting) / count;
                sample.meanSearches += double(stats.searches) / count;
                sample.meanWoken += double(stats.woken) / count;
            }
        });
        return sample;
    }

    static void reportHoppers(const char *scenario, int size, const HopperSample &sample)
    {
        report(scenario, size, sample.ticks);
        qInfo("SIGNAL-MEASURE %s hoppers acting=%.1f searches=%.1f woken=%.1f per tick", scenario, sample.meanActing, sample.meanSearches,
              sample.meanWoken);
    }

    // Chains of ten hoppers pointing east into a chest, `hoppers` in all; `above` on every hopper.
    QList<QcBlockPos> hopperChains(int hoppers, const QString &above, int gap = 2)
    {
        const int chains = (hoppers + 9) / 10;
        const int rows = int(std::ceil(std::sqrt(double(chains))));
        prepare(rows * 12, rows * gap);
        QList<QcBlockPos> placed;
        for (int chain = 0; chain < chains; ++chain) {
            const int x0 = (chain % rows) * 12;
            const int z = (chain / rows) * gap;
            for (int i = 0; i < 10; ++i) {
                put(x0 + i, z, u"signal_hopper[facing=east]"_s);
                if (!above.isEmpty()) {
                    put(x0 + i, z, above, 1);
                }
                placed.append(m_base.offset(x0 + i, 0, z));
            }
            put(x0 + 10, z, u"chest[facing=south]"_s);
        }
        return placed;
    }

    // Ten items a tick fall onto hoppers picked in a fixed pattern; returns how many appeared.
    int rain(const QList<QcBlockPos> &hoppers, int tick, quint32 &nextId, const char *name = "qtcraft:stick")
    {
        int dropped = 0;
        for (int i = 0; i < 10; ++i) {
            const QcBlockPos hopper = hoppers[(tick * 37 + i * 101) % hoppers.size()];
            QcItemEntityManager::Drop drop;
            drop.stack = {item(name), 1, 0};
            drop.position = {hopper.x + 0.5, hopper.y + 1.5, hopper.z + 0.5};
            drop.pickupDelay = 0.0;
            dropped += m_server->items().spawn(drop, nextId++) != 0 ? 1 : 0;
        }
        return dropped;
    }

    // The first hopper of every chain gets a stack: the chains carry it on.
    void feedChains(const QList<QcBlockPos> &hoppers)
    {
        for (qsizetype i = 0; i < hoppers.size(); i += 10) {
            QVERIFY(ensureEntity(hoppers[i]));
            m_server->blockEntities().container(hoppers[i])->stacks[0] = {item("qtcraft:cobblestone"), 64, 0};
            m_server->noteContents(hoppers[i]);
        }
    }

    static void report(const char *scenario, int size, const Sample &sample)
    {
        qInfo("SIGNAL-MEASURE %s size=%d tickMeanMs=%.3f tickMaxMs=%.3f workMean=%.0f workMax=%lld", scenario, size, sample.meanMs,
              sample.maxMs, sample.meanWork, qint64(sample.maxWork));
    }

    void toggleLever(QcBlockPos lever) { QVERIFY(m_server->circuits().use(lever)); }

    // One sorter unit (see test_signal_hoppers) at x below a hopper line at z = line, y = 3; the
    // filter holds 18 of `filter` and four stacks of 24 string.
    void sorterUnit(int x, int line, const char *filter)
    {
        put(x, line, u"chest[facing=south]"_s);
        put(x, line, u"signal_hopper[facing=down,enabled=false]"_s, 1);
        put(x, line, u"signal_hopper[facing=south]"_s, 2);
        QVERIFY(ensureEntity(m_base.offset(x, 2, line)));
        QList<QcItemStack> &stacks = m_server->blockEntities().container(m_base.offset(x, 2, line))->stacks;
        stacks = {{item(filter), 18, 0}};
        for (int i = 0; i < 4; ++i) {
            stacks.append({item("qtcraft:string"), 24, 0});
        }
        put(x + 1, line, u"stone"_s, 1);
        put(x + 1, line, u"signal_comparator[facing=east]"_s, 2);
        for (const auto &[dx, dz] : {std::pair{2, 0}, std::pair{3, 0}, std::pair{3, -1}}) {
            put(x + dx, line + dz, u"glass"_s, 1);
            put(x + dx, line + dz, u"signal_dust"_s, 2);
        }
        put(x + 2, line - 1, u"stone"_s, 2);
        for (const auto &[dx, dz] : {std::pair{3, -2}, std::pair{2, -2}, std::pair{2, -1}}) {
            put(x + dx, line + dz, u"signal_dust"_s, 1);
        }
        put(x + 1, line - 1, u"signal_repeater[facing=west,delay=1]"_s, 1);
        put(x, line - 1, u"signal_comparator[facing=south,mode=subtract,powered=true,power=15]"_s, 1);
        put(x, line - 2, u"lever[attach=floor,powered=true]"_s, 1);
    }

    // Ticks until the changes of building it are worked off.
    void settleBuild()
    {
        for (int tick = 0; tick < 400 && !m_server->circuitState().changes.isEmpty(); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->circuitState().changes.isEmpty());
        ticks(3);
    }

private slots:
    void init()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        m_base = m_feet.offset(3000, 40, 3000);
    }

    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void longDustLineToggledEveryTick()
    {
        const int length = m_full ? 4096 : 256;
        prepare(length, 1);
        put(0, 0, u"lever[attach=floor]"_s);
        for (int x = 1; x <= length; ++x) {
            put(x, 0, u"signal_dust"_s);
        }
        settleBuild();

        const QcBlockPos lever = m_base;
        const Sample sample = measure(40, [&](int) { toggleLever(lever); });
        report("line", length, sample);
        QVERIFY(m_content->property(*m_server->world().blockAt(m_base.offset(length, 0, 0)), u"power"_s) == u"0"_s);
    }

    void dustFieldToggledEveryTick()
    {
        const int side = m_full ? 64 : 16;
        prepare(side + 1, side);
        put(0, 0, u"lever[attach=floor]"_s);
        for (int x = 1; x <= side; ++x) {
            for (int z = 0; z < side; ++z) {
                put(x, z, u"signal_dust"_s);
            }
        }
        settleBuild();

        const QcBlockPos lever = m_base;
        const Sample sample = measure(40, [&](int) { toggleLever(lever); });
        report("field", side, sample);
    }

    void repeaterClocks()
    {
        // A torch on a block, its wire through a repeater (delay 4) back into the block: a clock of 20
        // ticks per period, slow enough not to burn out.
        const int clocks = m_full ? 1000 : 32;
        const int columns = int(std::ceil(std::sqrt(double(clocks))));
        prepare(columns * 4, columns * 4);
        for (int i = 0; i < clocks; ++i) {
            const int x = (i % columns) * 4;
            const int z = (i / columns) * 4;
            put(x, z, u"stone"_s);
            put(x + 1, z, u"signal_torch[attach=west]"_s);
            put(x + 1, z + 1, u"signal_dust"_s);
            put(x + 1, z + 2, u"signal_dust"_s);
            put(x, z + 2, u"signal_dust"_s);
            put(x, z + 1, u"signal_repeater[facing=north,delay=4]"_s);
        }
        settleBuild();
        ticks(40);

        int switches = 0;
        const QcBlockPos firstTorch = m_base.offset(1, 0, 0);
        QcBlockId last = *m_server->world().blockAt(firstTorch);
        const Sample sample = measure(100, [&](int) {
            const QcBlockId now = *m_server->world().blockAt(firstTorch);
            switches += now != last ? 1 : 0;
            last = now;
        });
        report("clocks", clocks, sample);
        QVERIFY2(switches >= 6, qPrintable(QString::number(switches)));
        const QList<QcChunkPos> chunks = m_server->blockEntities().chunks();
        for (const QcChunkPos &chunk : chunks) {
            const QHash<QcBlockPos, QcBlockEntities::Memory> memories = m_server->blockEntities().chunk(chunk)->memories;
            for (const QcBlockEntities::Memory &memory : memories) {
                QCOMPARE(memory.burntOutUntil, quint64(0));
            }
        }
    }

    void pistonsSwitchedTogether()
    {
        const int pistons = m_full ? 500 : 32;
        const int columns = int(std::ceil(std::sqrt(double(pistons))));
        prepare(columns * 4, columns * 3);
        QList<QcBlockPos> levers;
        for (int i = 0; i < pistons; ++i) {
            const int x = (i % columns) * 4;
            const int z = (i / columns) * 3;
            put(x, z, u"lever[attach=floor]"_s);
            put(x + 1, z, u"sticky_signal_piston[facing=east]"_s);
            put(x + 2, z, u"stone"_s);
            levers.append(m_base.offset(x, 0, z));
        }
        settleBuild();

        const Sample sample = measure(40, [&](int tick) {
            if (tick % 5 == 0) {
                for (const QcBlockPos &lever : std::as_const(levers)) {
                    toggleLever(lever);
                }
            }
        });
        report("pistons", pistons, sample);
        QCOMPARE(m_content->property(*m_server->world().blockAt(m_base.offset(1, 0, 0)), u"extended"_s), u"false"_s);
    }

    void idleHoppers()
    {
        const int count = m_full ? 1000 : 100;
        hopperChains(count, {});
        settleBuild();
        ticks(10);
        const HopperSample sample = measureHoppers(100, [](int) {});
        reportHoppers("hoppers-idle", count, sample);
        QCOMPARE(sample.meanSearches, 0.0);
    }

    void hoppersUnderFullBlocks()
    {
        const int count = m_full ? 1000 : 100;
        hopperChains(count, u"stone"_s);
        // The first tick looks at every new hopper once, none of them searches.
        ticks(1);
        QCOMPARE(m_server->circuits().hoppers().lastTick().searches, qsizetype(0));
        settleBuild();
        const HopperSample sample = measureHoppers(100, [](int) {});
        reportHoppers("hoppers-covered", count, sample);
        QCOMPARE(sample.meanSearches, 0.0);
    }

    void itemsRainIntoHoppers()
    {
        const int count = m_full ? 1000 : 100;
        const QList<QcBlockPos> hoppers = hopperChains(count, {});
        settleBuild();
        const QcItemId stick = item("qtcraft:stick");
        quint32 nextId = 700000;
        int dropped = 0;
        const HopperSample sample = measureHoppers(200, [&](int tick) { dropped += rain(hoppers, tick, nextId); });
        reportHoppers("hoppers-rain", count, sample);
        ticks(400);
        int stored = 0;
        const QList<QcBlockEntities::Container> containers = allContainers();
        for (const QcBlockEntities::Container &container : containers) {
            for (const QcItemStack &stack : container.stacks) {
                stored += stack.item == stick ? stack.count : 0;
            }
        }
        QCOMPARE(stored + droppedCount(stick), dropped);
        QVERIFY(stored > dropped / 2);
    }

    void itemSorter()
    {
        // Units every five blocks along a hopper line (the showcase's sorter, decision E-344), each
        // filtering one kind; the items come from a chest.
        const QList<const char *> kinds{"qtcraft:wheat", "qtcraft:cobblestone", "qtcraft:dirt", "qtcraft:sand"};
        const int items = m_full ? 200 : 40;
        prepare(kinds.size() * 5 + 4, 4);
        const int line = 2;
        for (qsizetype unit = 0; unit < kinds.size(); ++unit) {
            sorterUnit(1 + int(unit) * 5, line, kinds[unit]);
        }
        for (int x = 0; x <= int(kinds.size()) * 5; ++x) {
            put(x, line, u"signal_hopper[facing=east]"_s, 3);
        }
        put(0, line, u"chest[facing=south]"_s, 4);
        put(int(kinds.size()) * 5 + 1, line, u"chest[facing=south]"_s, 3);
        // Refilled with single items of the kinds in turn whenever it is empty (a stream that mixes
        // the kinds; one kind at full speed works too, see test_signal_hoppers).
        const QcBlockPos inputPos = m_base.offset(0, 4, line);
        int fed = 0;
        const auto refill = [&] {
            QcBlockEntities::Container *input = m_server->blockEntities().container(inputPos);
            if (!input || !std::ranges::all_of(input->stacks, &QcItemStack::isEmpty)) {
                return;
            }
            for (int slot = 0; slot < 24 && fed < items; ++slot, ++fed) {
                input->stacks[slot] = {item(kinds[fed % kinds.size()]), 1, 0};
            }
            m_server->noteContents(inputPos);
        };
        QVERIFY(ensureEntity(inputPos));
        refill();
        settleBuild();

        const HopperSample sample = measureHoppers(items * 8 + 600, [&](int) { refill(); });
        reportHoppers("sorter", items, sample);
        int sorted = 0;
        for (qsizetype unit = 0; unit < kinds.size(); ++unit) {
            for (const int y : {0, 1}) {
                const QcBlockEntities::Container *box = m_server->blockEntities().container(m_base.offset(1 + int(unit) * 5, y, line));
                for (const QcItemStack &stack : box->stacks) {
                    sorted += stack.item == item(kinds[unit]) ? stack.count : 0;
                }
            }
        }
        QCOMPARE(fed, items);
        QCOMPARE(sorted, items);
    }

    void observerClocks()
    {
        const int observers = m_full ? 500 : 40;
        const int columns = int(std::ceil(std::sqrt(observers / 2.0)));
        prepare(columns * 4, columns * 2);
        for (int i = 0; i < observers / 2; ++i) {
            const int x = (i % columns) * 4;
            const int z = (i / columns) * 2;
            put(x, z, u"signal_observer[facing=east]"_s);
            put(x + 1, z, u"signal_observer[facing=west]"_s);
        }
        settleBuild();
        ticks(10);
        const Sample sample = measure(100, [](int) {});
        report("observers", observers, sample);
        QVERIFY(sample.meanWork > observers / 4.0);
    }

    void busyHoppersWithoutComparators()
    {
        // Chains carrying items: no comparator anywhere, so the circuits have nothing to do.
        const int count = m_full ? 1000 : 100;
        const QList<QcBlockPos> hoppers = hopperChains(count, {});
        settleBuild();
        feedChains(hoppers);
        const HopperSample sample = measureHoppers(200, [](int) {});
        reportHoppers("hoppers-busy", count, sample);
        QVERIFY(sample.ticks.meanWork < 1.0);
        QVERIFY(sample.meanActing > 0.0);
    }

    void busyHoppersReadByComparators()
    {
        // The same with a comparator reading every hopper (rows three apart, each comparator south).
        const int count = m_full ? 1000 : 100;
        const QList<QcBlockPos> hoppers = hopperChains(count, {}, 3);
        for (const QcBlockPos &hopper : hoppers) {
            put(hopper.x - m_base.x, hopper.z - m_base.z + 1, u"signal_comparator[facing=south]"_s);
        }
        settleBuild();
        feedChains(hoppers);
        const HopperSample sample = measureHoppers(200, [](int) {});
        reportHoppers("hoppers-read", count, sample);
        QVERIFY(sample.ticks.meanWork > 0.0);
    }

    void fullHoppersUnderMovingItems()
    {
        // Full hoppers under items that keep moving: they wake every tick, search and find no room.
        const int count = m_full ? 1000 : 100;
        const QList<QcBlockPos> hoppers = hopperChains(count, {});
        for (const QcBlockPos &hopper : hoppers) {
            QVERIFY(ensureEntity(hopper));
            m_server->blockEntities().container(hopper)->stacks = QList<QcItemStack>(5, {item("qtcraft:dirt"), 64, 0});
        }
        settleBuild();
        quint32 nextId = 710000;
        const HopperSample sample = measureHoppers(200, [&](int tick) { rain(hoppers, tick, nextId); });
        reportHoppers("hoppers-full-rain", count, sample);
        QVERIFY(sample.meanSearches > 0.0);
    }

    void searchingAmongManyItems()
    {
        // 4096 items in the world, 512 of them awake, while items keep falling onto hoppers.
        const int count = m_full ? 1000 : 100;
        const QList<QcBlockPos> hoppers = hopperChains(count, {});
        settleBuild();
        QList<QcItemEntityManager::Drop> lying;
        for (int i = 0; i < QcItemEntityManager::totalLimit; ++i) {
            QcItemEntityManager::Drop drop;
            drop.stack = {item("qtcraft:diamond"), 1, 0};
            // On the floor between the chains, not on hoppers.
            drop.position = {m_base.x + 10.5 + (i % 2) * 0.2, double(m_base.y), m_base.z + 0.5 + (i / 2) % 40 * 2.0};
            lying.append(drop);
        }
        quint32 restored = 720000;
        m_server->items().restore(lying, [&restored] { return restored++; });
        ticks(2);
        quint32 nextId = 730000;
        const HopperSample sample = measureHoppers(200, [&](int tick) { rain(hoppers, tick, nextId); });
        reportHoppers("hoppers-many-items", count, sample);
        QVERIFY(m_server->items().items().size() >= QcItemEntityManager::activeLimit);
    }
};

QTEST_GUILESS_MAIN(TestSignalMeasure)
#include "test_signal_measure.moc"
