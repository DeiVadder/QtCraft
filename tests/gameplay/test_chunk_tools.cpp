#include "QcMachineFixture.h"

#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

// What keeps chunks loaded besides the players (stage 31.3, decisions E-375 to E-377): load tickets
// that let a long wire reach a far door, groups that load a machine with the chunk of its player,
// /forceload, the spawn chunks and chunk loader blocks with their limits; the debug display counts
// all of it.
class TestChunkTools : public QcMachineFixture
{
    Q_OBJECT

public:
    TestChunkTools() { m_unloadChunks = true; }

private:
    void teleport(QcBlockPos to) { chat(u"/tp %1 %2 %3"_s.arg(to.x).arg(to.y).arg(to.z)); }
    QString lastLine() const { return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.constLast(); }

    // A wire `length` blocks east from a lever at local (1, 0, 6), a repeater every 15 blocks, an
    // iron door at its end; built with all of its chunks loaded, before any tick.
    QcBlockPos longLine(int length)
    {
        put(0, -1, 6, u"stone"_s);
        put(0, 0, 6, u"lever[attach=floor]"_s);
        for (int x = 1; x <= length; ++x) {
            const QcBlockPos cell = at(x, 0, 6);
            m_server->world().loadChunk(cell.chunk());
            putAt(cell.offset(0, -1, 0), id("qtcraft:stone"));
            putAt(cell, state(x % 15 == 0 ? u"signal_repeater[facing=east]"_s : u"signal_dust"_s));
        }
        const QcBlockPos door = at(length + 1, 0, 6);
        m_server->world().loadChunk(door.chunk());
        putAt(door.offset(0, -1, 0), id("qtcraft:stone"));
        putAt(door, state(u"iron_door[facing=west]"_s));
        putAt(door.offset(0, 1, 0), state(u"iron_door[facing=west,half=top]"_s));
        return door;
    }

    // No signal is on its way any more (none waits, changes or is scheduled) for some ticks: a door
    // still closed then will not open. Under load the chunks arrive late, so the tests wait for this
    // instead of a fixed number of ticks.
    bool signalIsGone()
    {
        const QcCircuitState &circuits = m_server->circuitState();
        const bool quiet = circuits.waitingOn.isEmpty() && circuits.changes.isEmpty() && m_server->blockEntities().schedule().size() == 0
                           && m_server->chunkLoading().numbers().waiting == 0;
        m_quietTicks = quiet ? m_quietTicks + 1 : 0;
        return m_quietTicks > 20;
    }

    int m_quietTicks = 0;

    bool isOpen(QcBlockPos door) const
    {
        const std::optional<QcBlockId> block = m_server->world().blockAt(door);
        return block && m_content->property(*block, u"open"_s) == u"true"_s;
    }

    // A lever in `chunk` (no player near), dust eastwards and at the chunk's last column a repeater of
    // the longest delay that powers an iron door in the next chunk; returns the door.
    QcBlockPos lineIntoNextChunk(QcChunkPos chunk)
    {
        const QcBlockPos lever{chunk.x * 16 + 8, m_origin.y, chunk.z * 16 + 8};
        const QcBlockPos door = lever.offset(8, 0, 0);
        m_server->world().loadChunk(chunk);
        m_server->world().loadChunk(door.chunk());
        for (int x = -1; x <= 9; ++x) {
            for (int z = -1; z <= 1; ++z) {
                putAt(lever.offset(x, -1, z), id("qtcraft:stone"));
                for (int y = 0; y < 3; ++y) {
                    putAt(lever.offset(x, y, z), qcAirBlockId);
                }
            }
        }
        putAt(lever, state(u"lever[attach=floor]"_s));
        for (int x = 1; x <= 7; ++x) {
            putAt(lever.offset(x, 0, 0), state(x == 7 ? u"signal_repeater[facing=east,delay=4]"_s : u"signal_dust"_s));
        }
        putAt(door, state(u"iron_door[facing=west]"_s));
        putAt(door.offset(0, 1, 0), state(u"iron_door[facing=west,half=top]"_s));
        return door;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    // A 1000 blocks long wire switches a door in terrain no player keeps loaded (decision E-376):
    // with groups too small to hold the wire, the signal asks for load tickets of the chunks ahead
    // of it, a few per tick; the door opens, and when the tickets run out those chunks leave again.
    void aLongWireSwitchesAFarDoor()
    {
        QcServerConfig config;
        config.limits.chunkGroupLimit = 4;
        config.limits.chunkTicketTicks = 60;
        buildArea(config);
        const QcBlockPos door = longLine(1000);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"connect more than 4 chunks"_s));
        // Building the wire is a change at the borders of chunks that leave: those ask for tickets
        // too; the line settles and its chunks leave once they run out.
        int settled = 0;
        while (settled < 2000 && (m_server->chunkLoading().tickets().count() > 0 || m_server->world().chunk(door.chunk()) || settled < 3)) {
            ticks(1);
            ++settled;
        }
        const qsizetype home = m_server->world().loadedChunkCount();
        QVERIFY(!m_server->world().chunk(door.chunk()));
        QVERIFY(!isOpen(door));

        flip(0, 0, 6);
        int openedAt = -1;
        qsizetype mostTickets = 0;
        m_quietTicks = 0;
        for (int tick = 0; tick < 30000 && openedAt < 0 && !signalIsGone(); ++tick) {
            ticks(1);
            mostTickets = std::max(mostTickets, m_server->chunkLoading().tickets().count());
            QVERIFY(m_server->chunkLoading().tickets().weight() <= qsizetype(config.limits.chunkTickets));
            if (isOpen(door)) {
                openedAt = tick;
            }
        }
        QVERIFY2(openedAt >= 0, "the door did not open");
        QVERIFY(mostTickets > 0);
        qInfo("door opened after %d ticks, at most %lld tickets", openedAt, static_cast<qlonglong>(mostTickets));

        // The tickets run out and their chunks leave; the door keeps its state. A ticket lasts while a
        // signal still waits or passes in its chunk (E-378), which under load may take longer.
        for (int tick = 0; tick < 3000 && m_server->chunkLoading().tickets().count() > 0; ++tick) {
            ticks(1);
        }
        ticks(5);
        QCOMPARE(m_server->chunkLoading().tickets().count(), 0);
        QVERIFY(m_server->world().loadedChunkCount() <= home + 2);
        m_server->world().loadChunk(door.chunk());
        QVERIFY(isOpen(door));
    }

    // A wish the limits refuse is asked for again while its signal waits, and the signal's chunk stays
    // meanwhile: with one ticket per tick a long wire still reaches its door (decision E-378).
    void aRefusedTicketIsAskedForAgain()
    {
        QcServerConfig config;
        config.limits.chunkGroupLimit = 4;
        config.limits.chunkTicketTicks = 30;
        config.limits.chunkTicketsPerTick = 1;
        buildArea(config);
        const QcBlockPos door = longLine(300);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"connect more than 4 chunks"_s));
        for (int tick = 0; tick < 2000 && (m_server->chunkLoading().tickets().count() > 0 || m_server->world().chunk(door.chunk())); ++tick) {
            ticks(1);
        }
        QVERIFY(!m_server->world().chunk(door.chunk()));

        flip(0, 0, 6);
        m_quietTicks = 0;
        for (int tick = 0; tick < 30000 && !isOpen(door) && !signalIsGone(); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->chunkLoading().tickets().refused() > 0);
        QVERIFY2(isOpen(door), "the door did not open");
    }

    // A chunk a waiting signal kept loaded, without a ticket of its own, stays while the signal runs
    // on once the chunk it waited for came (here 8 ticks in a repeater), and leaves after that
    // (decision E-410). Before, it left in the very tick the wait ended and took the signal along;
    // under load that made aRefusedTicketIsAskedForAgain fail now and then.
    void aChunkKeepsTheSignalThatStopsWaitingInIt()
    {
        QcServerConfig config;
        config.limits.chunkGroupLimit = 1;
        buildArea(config);
        const QcChunkPos chunk = at(400, 0, 6).chunk();
        const QcBlockPos door = lineIntoNextChunk(chunk);
        for (int tick = 0; tick < 2000 && (m_server->world().chunk(chunk) || m_server->world().chunk(door.chunk()) || m_server->chunkLoading().tickets().count() > 0); ++tick) {
            ticks(1);
        }
        QVERIFY(!m_server->world().chunk(chunk) && !m_server->world().chunk(door.chunk()));

        // Only the lever's chunk comes back; the dust at its edge waits for the door's chunk, which
        // gets a ticket and is read in the background.
        m_server->world().loadChunk(chunk);
        QVERIFY(m_server->systems().circuits.use(QcBlockPos{chunk.x * 16 + 8, m_origin.y, chunk.z * 16 + 8}));
        for (int tick = 0; tick < 2000 && !isOpen(door) && (m_server->world().chunk(chunk) || m_server->world().chunk(door.chunk())); ++tick) {
            ticks(1);
        }
        QVERIFY2(isOpen(door), "the signal left with its chunk");

        for (int tick = 0; tick < 2000 && m_server->world().chunk(chunk); ++tick) {
            ticks(1);
        }
        QVERIFY(!m_server->world().chunk(chunk));
    }

    // A ticket weighs the chunks of the group it loads, so the limit bounds the chunks tickets keep
    // loaded and not only the tickets (review of 31.3).
    void aTicketWeighsItsGroup()
    {
        QcChunkTickets tickets;
        tickets.setLimits({.perTick = 10, .total = 8, .ticks = 5});
        QVERIFY(tickets.request({0, 0}, 0, 6));
        QVERIFY(!tickets.request({5, 0}, 0, 3));
        QVERIFY(tickets.request({5, 0}, 0, 2));
        QCOMPARE(tickets.weight(), 8);
        QCOMPARE(tickets.refused(), quint64(1));

        // Running out gives the weight back.
        tickets.expire(10, [](QcChunkPos) { return true; });
        QCOMPARE(tickets.count(), 0);
        QCOMPARE(tickets.weight(), 0);
        QVERIFY(tickets.request({9, 0}, 10, 8));
    }

    // A ticket whose chunk holds a waiting signal stays; one a signal passes through lasts up to its
    // time once more; then it ends (decision E-378).
    void aTicketStaysWhileItsSignalWaitsOrPasses()
    {
        using Use = QcChunkTickets::Use;
        QcChunkTickets tickets;
        tickets.setLimits({.perTick = 10, .total = 10, .ticks = 5});
        QVERIFY(tickets.request({0, 0}, 0));
        const auto loaded = [](QcChunkPos) { return true; };
        Use use = Use::Waiting;
        const auto useOf = [&use](QcChunkPos) { return use; };

        tickets.expire(50, loaded, useOf);
        QVERIFY(tickets.contains({0, 0}));
        use = Use::Busy;
        tickets.expire(54, loaded, useOf);
        QVERIFY(tickets.contains({0, 0}));
        tickets.expire(55, loaded, useOf);
        QVERIFY(!tickets.contains({0, 0}));

        QVERIFY(tickets.request({1, 0}, 60));
        use = Use::Free;
        tickets.expire(65, loaded, useOf);
        QVERIFY(!tickets.contains({1, 0}));
    }

    // A machine within the group limit loads whole with the chunk of its player (decision E-375):
    // a 200 blocks long wire stays loaded while the player stands at its lever.
    void aGroupStaysLoadedWithItsPlayer()
    {
        buildArea();
        const QcBlockPos door = longLine(200);
        ticks(3);
        QVERIFY(m_server->world().chunk(door.chunk()));
        const QcChunkGroups::Group group = m_server->chunkLoading().groupOf(at(1, 0, 6).chunk());
        QVERIFY(!group.oversize);
        QVERIFY(group.chunks.contains(door.chunk()));
        flip(0, 0, 6);
        for (int tick = 0; tick < 200 && !isOpen(door); ++tick) {
            ticks(1);
        }
        QVERIFY(isOpen(door));
    }

    // A wire cut at a chunk border splits its group: the part no one needs leaves at once, although
    // the player stands still (the unloading pass is skipped only while nothing it depends on changes).
    void aCutWireLetsTheFarPartLeave()
    {
        // Without tickets: the cut is a change next to chunks that are not loaded.
        QcServerConfig config;
        config.limits.chunkTicketsPerTick = 0;
        buildArea(config);
        const QcBlockPos door = longLine(200);
        ticks(3);
        QVERIFY(m_server->world().chunk(door.chunk()));

        // The first border two chunks east of the player: its two cells on the near side go.
        const QcChunkPos home = m_feet.chunk();
        int cut = -1;
        for (int x = 1; x < 200 && cut < 0; ++x) {
            const QcChunkPos chunk = at(x, 0, 6).chunk();
            if (chunk.x >= home.x + 2 && at(x + 1, 0, 6).chunk() != chunk) {
                cut = x;
            }
        }
        QVERIFY(cut > 0);
        putAt(at(cut, 0, 6), qcAirBlockId);
        putAt(at(cut - 1, 0, 6), qcAirBlockId);
        ticks(3);
        QVERIFY(!m_server->world().chunk(door.chunk()));
    }

    // /forceload keeps chunks loaded without a player and the list survives a restart (decision E-377).
    void forceloadKeepsChunksLoaded()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        config.limits.forcedChunks = 4;
        join(config);
        const QcBlockPos far = m_feet.offset(400, 0, 0);
        chat(u"/forceload add %1 %2"_s.arg(far.x).arg(far.z));
        QCOMPARE(lastLine(), u"Chunk %1, %2 is forced"_s.arg(far.chunk().x).arg(far.chunk().z));
        for (int i = 0; i < 1000 && !m_server->world().chunk(far.chunk()); ++i) {
            ticks(1);
        }
        QVERIFY(m_server->world().chunk(far.chunk()));
        ticks(20);
        QVERIFY(m_server->world().chunk(far.chunk()));
        QCOMPARE(m_server->chunkLoading().numbers().forced, 1);

        // More than the limit is refused; an area counts every chunk in it.
        chat(u"/forceload add %1 %2 %3 %4"_s.arg(far.x).arg(far.z).arg(far.x + 64).arg(far.z));
        QVERIFY(lastLine().contains(u"At most 4 chunks"_s));
        chat(u"/forceload query %1 %2"_s.arg(far.x).arg(far.z));
        QVERIFY(lastLine().contains(u"is forced"_s));

        QVERIFY(m_server->save().has_value());
        cleanup();
        join(config);
        for (int i = 0; i < 1000 && !m_server->world().chunk(far.chunk()); ++i) {
            ticks(1);
        }
        QVERIFY(m_server->world().chunk(far.chunk()));
        chat(u"/forceload remove all"_s);
        ticks(2);
        QVERIFY(!m_server->world().chunk(far.chunk()));
    }

    // The game rule spawnChunkRadius keeps the chunks around the spawn loaded (decision E-377).
    void spawnChunksStayLoaded()
    {
        join();
        const QcChunkPos spawn = m_feet.chunk();
        teleport(m_feet.offset(600, 30, 0));
        ticks(3);
        QVERIFY(!m_server->world().chunk(spawn));
        chat(u"/gamerule spawnChunkRadius 2"_s);
        QCOMPARE(lastLine(), u"spawnChunkRadius is now 2"_s);
        const auto spawnChunksLoaded = [&] {
            return m_server->world().chunk(spawn) && m_server->world().chunk({spawn.x + 1, spawn.z + 1})
                   && m_server->world().chunk({spawn.x - 1, spawn.z + 1});
        };
        for (int i = 0; i < 1000 && !spawnChunksLoaded(); ++i) {
            ticks(1);
        }
        QVERIFY(m_server->world().chunk(spawn));
        QVERIFY(m_server->world().chunk({spawn.x - 1, spawn.z + 1}));
        QVERIFY(!m_server->world().chunk({spawn.x + 2, spawn.z}));
        chat(u"/gamerule spawnChunkRadius 17"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /gamerule"_s));
    }

    // A chunk loader keeps its chunk loaded; a player places only so many, the world lets only so
    // many work, and they are known after a restart also while their chunks are not loaded.
    void chunkLoadersKeepTheirChunksWithinTheirLimits()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        config.limits.chunkLoadersPerPlayer = 1;
        config.limits.chunkLoaders = 2;
        buildArea(config);
        QVERIFY(m_server->world().loaders().isLoader(id("qtcraft:chunk_loader")));
        chat(u"/give chunk_loader 2"_s);
        const int slot = slotOf(item("qtcraft:chunk_loader"));
        QVERIFY(slot >= 0);
        send(QcMsg::SelectHotbarSlot{slot});

        // The player's first one is his, the second is refused.
        const QcBlockPos mine = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{mine.offset(0, -1, 0), QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QCOMPARE(m_server->world().blockAt(mine), std::optional<QcBlockId>(id("qtcraft:chunk_loader")));
        QCOMPARE(m_server->world().loaders().countOf(u"Raw"_s), 1);
        send(QcMsg::UseItem{m_feet.offset(-2, -1, 0), QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QVERIFY(m_server->world().blockAt(m_feet.offset(-2, 0, 0)) != std::optional<QcBlockId>(id("qtcraft:chunk_loader")));
        QVERIFY(lastLine().contains(u"chunk loaders"_s));

        // Placed directly (like a command or a structure) they belong to no one; only the first two
        // of the world work.
        const QcBlockPos far = mine.offset(800, 0, 0);
        const QcBlockPos further = mine.offset(1600, 0, 0);
        const QcBlockPos furthest = mine.offset(2400, 0, 0);
        for (const QcBlockPos &pos : {far, further, furthest}) {
            m_server->world().loadChunk(pos.chunk());
            putAt(pos, id("qtcraft:chunk_loader"));
        }
        QCOMPARE(m_server->world().loaders().loaders().size(), 4);
        teleport(mine.offset(-800, 30, 0));
        ticks(5);
        QVERIFY(m_server->world().chunk(mine.chunk()));
        QVERIFY(m_server->world().chunk(far.chunk()));
        QVERIFY(!m_server->world().chunk(further.chunk()));
        QVERIFY(!m_server->world().chunk(furthest.chunk()));

        // Known after a restart, their chunks load without anyone near.
        QVERIFY(m_server->save().has_value());
        cleanup();
        join(config);
        QCOMPARE(m_server->world().loaders().loaders().size(), 4);
        for (int i = 0; i < 1000 && !m_server->world().chunk(far.chunk()); ++i) {
            ticks(1);
        }
        QVERIFY(m_server->world().chunk(far.chunk()));
        QCOMPARE(m_server->chunkLoading().numbers().loaders, 4);
    }

    // Many loaders (a /fill, a mod) are found and removed by position, keep the order they were
    // placed in and count their owners (review of 31.3).
    void manyLoadersKeepTheirOrderAndOwners()
    {
        QcChunkLoaders loaders(*m_content);
        const QcBlockId loader = id("qtcraft:chunk_loader");
        constexpr int count = 50000;
        for (int i = 0; i < count; ++i) {
            loaders.noteBlock({i, 10, 0}, qcAirBlockId, loader);
        }
        loaders.setOwner({3, 10, 0}, u"Ann"_s);
        loaders.setOwner({4, 10, 0}, u"Ann"_s);
        for (int i = 0; i < count; i += 2) {
            loaders.noteBlock({i, 10, 0}, loader, qcAirBlockId);
        }
        QCOMPARE(loaders.count(), count / 2);
        QCOMPARE(loaders.countOf(u"Ann"_s), 1);
        loaders.noteBlock({0, 10, 0}, qcAirBlockId, loader);

        // The oldest come first: (1..) then the one placed again; 16 blocks a chunk.
        const QList<QcChunkLoaders::Loader> all = loaders.loaders();
        QCOMPARE(all.constFirst().pos, (QcBlockPos{1, 10, 0}));
        QCOMPARE(all.at(1).owner, u"Ann"_s);
        QCOMPARE(all.constLast().pos, (QcBlockPos{0, 10, 0}));
        QCOMPARE(loaders.chunks(9), (QList<QcChunkPos>{{0, 0}, {1, 0}}));
    }
};

QTEST_GUILESS_MAIN(TestChunkTools)
#include "test_chunk_tools.moc"
