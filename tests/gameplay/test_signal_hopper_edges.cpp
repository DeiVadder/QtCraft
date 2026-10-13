#include "QcMachineFixture.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Signal Hoppers at the edges (decision E-344, review): busy hoppers cost the circuits nothing
// without comparators, more ready hoppers than the per-tick bound are all served in turn, a player
// and a hopper race for one item, items drift in on water and fall in when a piston takes the block
// above away, hoppers survive their chunk unloading and a save in the middle of a cooldown.
class TestSignalHopperEdges : public QcMachineFixture
{
    Q_OBJECT

private:
    // A chain of hoppers from local x = 1 eastwards into a chest, the first one filled.
    void chain(int z, int length, int items)
    {
        for (int x = 1; x <= length; ++x) {
            put(x, 0, z, u"signal_hopper[facing=east]"_s);
        }
        put(length + 1, 0, z, u"chest[facing=south]"_s);
        fill(1, 0, z, {stack("qtcraft:cobblestone", items)});
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void busyHoppersCostTheCircuitsNothing()
    {
        buildArea();
        for (int z = 0; z < 10; ++z) {
            chain(z, 8, 20);
        }
        ticks(2);
        qsizetype work = 0;
        for (int tick = 0; tick < 100; ++tick) {
            ticks(1);
            work += m_server->circuits().lastTickWork();
            QVERIFY(m_server->circuitState().changes.isEmpty());
        }
        QCOMPARE(work, 0);
        QVERIFY(countIn(9, 0, 0, "qtcraft:cobblestone") > 0);

        // A comparator reading one chest: only it looks again, once per tick however often it changes.
        put(10, 0, 0, u"signal_comparator[facing=east]"_s);
        ticks(3);
        for (int tick = 0; tick < 40; ++tick) {
            ticks(1);
            // Noted at the end of a tick, looked at once at the start of the next.
            QVERIFY(m_server->circuitState().readers.size() <= 1);
            QVERIFY2(m_server->circuits().lastTickWork() <= 8, qPrintable(QString::number(m_server->circuits().lastTickWork())));
        }
        QVERIFY(power(10, 0, 0) > 0);
    }

    void moreReadyHoppersThanTheBoundAreAllServed()
    {
        buildArea();
        // 5000 hoppers, each over a chest, each with one item, all ready at once.
        constexpr int count = 5000;
        const QcBlockPos base = m_origin.offset(2000, 0, 2000);
        const QcBlockId hopper = *m_content->stateId(u"qtcraft:signal_hopper[facing=down]"_s);
        const QcBlockId chest = *m_content->stateId(u"qtcraft:chest[facing=south]"_s);
        QList<QcBlockPos> hoppers;
        for (int i = 0; i < count; ++i) {
            const QcBlockPos pos = base.offset(i % 100, 0, (i / 100) * 2);
            m_server->world().loadChunk(pos.chunk());
            QVERIFY(m_server->world().setBlock(pos, hopper));
            QVERIFY(m_server->world().setBlock(pos.offset(0, -1, 0), chest));
            QVERIFY(m_server->world().setBlock(pos.offset(0, 1, 0), *m_content->stateId(u"qtcraft:stone"_s)));
            QVERIFY(m_server->containers().add(pos, 5));
            m_server->containers().find(pos)->stacks[0] = stack("qtcraft:diamond", 1);
            hoppers.append(pos);
        }
        for (int dx = -32; dx <= 132; dx += 16) {
            for (int dz = -32; dz <= 132; dz += 16) {
                m_server->world().loadChunk(base.offset(dx, 0, dz).chunk());
            }
        }

        // Within ceil(5000 / 4096) = 2 ticks every hopper has moved its item.
        const int bound = int((count + QcServerLimits{}.hopperBudget - 1) / QcServerLimits{}.hopperBudget);
        ticks(bound);
        int served = 0;
        for (const QcBlockPos &pos : std::as_const(hoppers)) {
            const QcContainerManager::Container *below = m_server->containers().find(pos.offset(0, -1, 0));
            served += below && below->stacks.value(0).count == 1 ? 1 : 0;
        }
        QCOMPARE(served, count);
        QCOMPARE(everywhere("qtcraft:diamond"), count);
    }

    void playerAndHopperRaceForOneItem()
    {
        join();
        const QcBlockPos below = m_feet.offset(0, -1, 0);
        putAt(below, *m_content->stateId(u"qtcraft:signal_hopper[facing=down]"_s));
        putAt(below.offset(0, -1, 0), *m_content->stateId(u"qtcraft:chest[facing=south]"_s));
        ticks(2);
        QcItemEntityManager::Drop drop;
        drop.stack = stack("qtcraft:diamond", 3);
        drop.position = {m_feet.x + 0.5, double(m_feet.y), m_feet.z + 0.5};
        drop.pickupDelay = 0.0;
        QVERIFY(m_server->items().spawn(drop, 950001) != 0);
        for (int tick = 0; tick < 40; ++tick) {
            ticks(1);
            QCOMPARE(everywhere("qtcraft:diamond"), 3);
        }
        QCOMPARE(droppedCount(item("qtcraft:diamond")), 0);
    }

    void itemDriftsInOnWater()
    {
        buildArea();
        // A channel on the floor: water from x = 1 flows east over a hopper in the floor at x = 5.
        for (int x = 0; x <= 7; ++x) {
            put(x, 0, 4, u"glass"_s);
            put(x, 0, 6, u"glass"_s);
        }
        put(7, 0, 5, u"glass"_s);
        // Closed to the west too: water runs to the nearest drop (E-103).
        put(0, 0, 5, u"glass"_s);
        put(5, -1, 5, u"signal_hopper[facing=down]"_s);
        put(5, -2, 5, u"chest[facing=south]"_s);
        const QcBlockPos source = at(1, 0, 5);
        chat(u"/setblock %1 %2 %3 water"_s.arg(source.x).arg(source.y).arg(source.z));
        idle(80);
        QCOMPARE(m_content->block(*m_server->world().blockAt(at(4, 0, 5))).name, u"qtcraft:water"_s);
        drop(2, 0.4, 5, stack("qtcraft:stick", 2));
        idle(200);
        QCOMPARE(countIn(5, -1, 5, "qtcraft:stick") + countIn(5, -2, 5, "qtcraft:stick"), 2);
    }

    void itemFallsInWhenAPistonTakesTheBlockAway()
    {
        buildArea();
        // A sticky piston holds a stone over the hopper with its head; the lever keeps it out.
        put(5, 0, 5, u"signal_hopper[facing=down]"_s);
        put(5, 1, 5, u"stone"_s);
        put(6, 0, 5, u"stone"_s);
        put(6, 1, 5, u"signal_piston_head[facing=west,sticky=true]"_s);
        put(7, 0, 5, u"stone"_s);
        put(7, 1, 5, u"sticky_signal_piston[facing=west,extended=true]"_s);
        put(8, 0, 5, u"stone"_s);
        put(8, 1, 5, u"lever[attach=floor,powered=true]"_s);
        ticks(2);
        drop(5, 2.0, 5, stack("qtcraft:stick", 1));
        ticks(20);
        QCOMPARE(countIn(5, 0, 5, "qtcraft:stick"), 0);
        QCOMPARE(hopperStats().searches, 0);

        // Off: the piston pulls the stone away, the stick falls into the hopper.
        flip(8, 1, 5);
        ticks(30);
        QCOMPARE(countIn(5, 0, 5, "qtcraft:stick"), 1);
    }

    void hoppersGoOnAfterTheirChunkReloads()
    {
        buildArea();
        // Far from the player, in chunks only the test loads.
        clearArea(m_origin.offset(4000, 0, 4000));
        chain(5, 3, 6);
        ticks(3);
        const QcChunkPos chunk = at(2, 0, 5).chunk();
        std::shared_ptr<QcChunk> taken = m_server->world().takeChunk(chunk);
        QVERIFY(taken);
        ticks(40);
        m_server->world().adoptStoredChunks({{chunk, taken}});
        ticks(200);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:cobblestone"), 6);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 6);
    }

    void saveInTheMiddleOfACooldown()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        buildArea(config);
        chain(5, 2, 4);
        // The first hopper moved an item and rests: three ticks of its cooldown are gone.
        ticks(1);
        ticks(3);
        const QcBlockPos first = at(1, 0, 5);
        const quint64 left = m_server->circuitState().hoppers.readyAt.value(first) - m_server->tickCount();
        QVERIFY(left > 0 && left < 8);
        QVERIFY(m_server->save().has_value());
        cleanup();

        // Read right after opening the world, before any tick.
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        const quint64 loadedLeft = m_server->circuitState().hoppers.readyAt.value(first) - m_server->tickCount();
        QCOMPARE(loadedLeft, left);
        cleanup();
        join(config);
        ticks(200);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 4);
        QCOMPARE(countIn(3, 0, 5, "qtcraft:cobblestone"), 4);
    }
};

QTEST_GUILESS_MAIN(TestSignalHopperEdges)
#include "test_signal_hopper_edges.moc"
