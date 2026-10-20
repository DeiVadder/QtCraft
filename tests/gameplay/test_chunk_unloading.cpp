#include "common/core/QcIntMath.h"
#include "QcBlockEntityDump.h"
#include "QcMachineFixture.h"
#include "server/blockentities/QcBlockEntityChecks.h"
#include "server/entities/QcEntityChecks.h"
#include "server/storage/QcSaveFile.h"

#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

// Chunks leaving and coming back (stage 31.3, decisions E-373 to E-375): everything attached to a
// chunk goes with it and comes back unchanged, clocks and machines stand still while their chunks
// are away and go on exactly where they stopped, connected chunks come and go together, groups over
// the limit fall back to waiting at their edge, and the memory of a long exploration stays bounded.
class TestChunkUnloading : public QcMachineFixture
{
    Q_OBJECT

public:
    TestChunkUnloading() { m_unloadChunks = true; }

private:
    quint32 m_nextId = 5'000'000;

    void teleport(QcBlockPos to) { chat(u"/tp %1 %2 %3"_s.arg(to.x).arg(to.y).arg(to.z)); }

    // Where the player goes to leave the area: far enough that none of its chunks is needed.
    QcBlockPos away() const { return m_feet.offset(30 * QcWorld::chunkSize, 0, 0); }

    // The local x and z of the first cells of the chunks east and south of the area's origin chunk.
    QcBlockPos border() const
    {
        return {QcWorld::chunkSize - qcFloorMod(m_origin.x, QcWorld::chunkSize), 0, QcWorld::chunkSize - qcFloorMod(m_origin.z, QcWorld::chunkSize)};
    }

    // Two observers watching each other across the chunk border between local x - 1 and x, a lamp
    // behind each: a clock that changes every few ticks and spans both chunks.
    void observerClock(int x, int z)
    {
        put(x - 2, 0, z, u"signal_lamp"_s);
        put(x + 1, 0, z, u"signal_lamp"_s);
        put(x - 1, 0, z, u"signal_observer[facing=east]"_s);
        put(x, 0, z, u"signal_observer[facing=west]"_s);
    }
    // The same across the border between local z - 1 and z.
    void observerClockSouth(int x, int z)
    {
        put(x, 0, z - 2, u"signal_lamp"_s);
        put(x, 0, z + 1, u"signal_lamp"_s);
        put(x, 0, z - 1, u"signal_observer[facing=south]"_s);
        put(x, 0, z, u"signal_observer[facing=north]"_s);
    }

    // The states of the cells as one line; empty while any of them is not loaded.
    QString states(const QList<QcBlockPos> &cells) const
    {
        QStringList parts;
        for (const QcBlockPos &cell : cells) {
            const std::optional<QcBlockId> block = m_server->world().blockAt(cell);
            if (!block) {
                return {};
            }
            parts.append(m_content->stateName(*block));
        }
        return parts.join(u' ');
    }

    // Runs the clock for `count` observed ticks; with `leaveAt` the player leaves before that tick
    // and comes back `awayTicks` later. Only ticks after which all cells are loaded are observed.
    QStringList runClock(const QList<QcBlockPos> &cells, int count, int leaveAt, int awayTicks)
    {
        QStringList seen;
        const QcBlockPos home = m_feet;
        int tick = 0;
        int guard = 0;
        while (seen.size() < count && guard++ < count + awayTicks + 400) {
            if (tick == leaveAt) {
                teleport(away());
            }
            if (leaveAt >= 0 && tick == leaveAt + awayTicks) {
                teleport(home);
            }
            ticks(1);
            ++tick;
            if (const QString line = states(cells); !line.isEmpty()) {
                seen.append(line);
            }
        }
        return seen;
    }

    // The chunks of the cells.
    static QSet<QcChunkPos> chunksOf(const QList<QcBlockPos> &cells)
    {
        QSet<QcChunkPos> chunks;
        for (const QcBlockPos &cell : cells) {
            chunks.insert(cell.chunk());
        }
        return chunks;
    }

    qsizetype loadedOf(const QSet<QcChunkPos> &chunks) const
    {
        return std::ranges::count_if(chunks, [this](QcChunkPos chunk) { return m_server->world().chunk(chunk) != nullptr; });
    }

    // Entities as lines without ids, sorted.
    QStringList entityLines() const
    {
        QStringList lines;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            lines.append(u"creature %1 %2 %3 %4 %5"_s.arg(m_content->mobs()[mob.type].name)
                             .arg(mob.motion.position.x)
                             .arg(mob.motion.position.y)
                             .arg(mob.motion.position.z)
                             .arg(mob.health));
        }
        for (const QcItemEntity &item : m_server->items().items()) {
            lines.append(u"item %1 %2 %3 %4 age %5"_s.arg(QcBlockEntityDump::stackText(item.stack, *m_content))
                             .arg(item.motion.position.x)
                             .arg(item.motion.position.y)
                             .arg(item.motion.position.z)
                             .arg(item.age));
        }
        for (const QcFallingBlock &falling : m_server->fallingBlocks().all()) {
            lines.append(u"falling %1 %2 %3 %4"_s.arg(m_content->stateName(falling.block))
                             .arg(falling.motion.position.x)
                             .arg(falling.motion.position.y)
                             .arg(falling.motion.position.z));
        }
        for (const QcArmourStand &stand : m_server->stands().stands()) {
            lines.append(u"stand %1 %2 %3 %4"_s.arg(stand.name).arg(stand.motion.position.x).arg(stand.motion.position.y).arg(stand.motion.position.z));
        }
        lines.sort();
        return lines;
    }

    void furnish()
    {
        put(2, 0, 2, u"chest[facing=south]"_s);
        fill(2, 0, 2, {stack("qtcraft:diamond", 5), stack("qtcraft:stick", 7)});
        put(4, 0, 2, u"furnace[facing=south]"_s);
        fill(4, 0, 2, {stack("qtcraft:iron_ore", 6), stack("qtcraft:coal", 2)});
        put(6, 1, 2, u"chest[facing=south]"_s);
        fill(6, 1, 2, {stack("qtcraft:cobblestone", 9)});
        put(6, 0, 2, u"signal_hopper[facing=down]"_s);
        put(9, 0, 9, u"birch_sapling"_s);
        m_server->plants().saplingPlaced(m_server->blockEntities(), at(9, 0, 9), id("qtcraft:birch_sapling"));
        put(11, 0, 3, u"lever[attach=floor]"_s);
        put(12, 0, 3, u"signal_repeater[facing=east,delay=4]"_s);
        put(13, 0, 3, u"signal_lamp"_s);
        flip(11, 0, 3);

        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        m_server->mobs().spawn(pig, QcVec3{at(3, 0, 8).x + 0.5, double(at(3, 0, 8).y), at(3, 0, 8).z + 0.5}, m_nextId++, false, true);
        QcItemEntityManager::Drop drop;
        drop.stack = stack("qtcraft:diamond", 3);
        drop.position = {at(8, 0, 5).x + 0.5, double(at(8, 0, 5).y), at(8, 0, 5).z + 0.5};
        m_server->items().spawn(drop, m_nextId++);
        QcArmourStand stand;
        stand.id = m_nextId++;
        stand.name = u"Keeper"_s;
        stand.motion.position = {at(10, 0, 11).x + 0.5, double(at(10, 0, 11).y), at(10, 0, 11).z + 0.5};
        stand.item = u"qtcraft:armor_stand"_s;
        QVERIFY(m_server->stands().add(stand));
        chat(u"/setblock %1 %2 %3 qtcraft:sand"_s.arg(at(5, 6, 6).x).arg(at(5, 6, 6).y).arg(at(5, 6, 6).z));
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    // Everything of a chunk leaves with it and comes back as it was (decision E-374): containers with
    // their contents and a hopper's cooldown, a burning furnace, a sapling, a repeater's scheduled
    // tick, a creature, an item, a stand and a block in mid-air, with every wait where it stopped.
    // Compared with the same area in a world nobody left, after as many simulated ticks: the tick in
    // which the chunks leave simulates them, the next one simulates them again.
    void unloadingAndLoadingGiveTheSameState()
    {
        QStringList blockEntities;
        QStringList entities;
        {
            buildArea();
            furnish();
            ticks(5);
            blockEntities = QcBlockEntityDump::lines(*m_server, *m_content);
            entities = entityLines();
            QVERIFY(blockEntities.join(u'\n').contains(u"scheduled"_s));
            QVERIFY(blockEntities.join(u'\n').contains(u"cooldown"_s));
            QVERIFY(blockEntities.join(u'\n').contains(u"machine"_s));
            QVERIFY(!m_server->fallingBlocks().all().isEmpty());
            cleanup();
        }

        buildArea();
        furnish();
        ticks(3);
        const QSet<QcChunkPos> area = chunksOf({at(0, 0, 0), at(areaSize - 1, 0, areaSize - 1)});
        teleport(away());
        ticks(1);
        QCOMPARE(loadedOf(area), 0);
        QVERIFY(m_server->mobs().mobs().isEmpty());
        QVERIFY(m_server->stands().stands().isEmpty());
        QVERIFY(m_server->fallingBlocks().all().isEmpty());
        QVERIFY(std::ranges::none_of(m_server->blockEntities().chunks(), [&](QcChunkPos chunk) { return area.contains(chunk); }));
        ticks(50);

        teleport(m_feet);
        loadAround(at(areaSize / 2, 0, areaSize / 2));
        QCOMPARE(loadedOf(area), area.size());
        ticks(1);
        QCOMPARE(QcBlockEntityDump::lines(*m_server, *m_content), blockEntities);
        // The creature walks by chance; it is compared by kind and health.
        const auto calm = [](QStringList lines) {
            for (QString &line : lines) {
                if (line.startsWith(u"creature"_s)) {
                    line = line.section(u' ', 0, 1) + u' ' + line.section(u' ', 5);
                }
            }
            lines.sort();
            return lines;
        };
        QCOMPARE(calm(entityLines()), calm(entities));
        QVERIFY2(QcBlockEntityChecks::problems(m_server->world()).isEmpty(), qPrintable(QcBlockEntityChecks::problems(m_server->world()).join(u'\n')));
        QVERIFY(QcEntityChecks::problems(m_server->entities()).isEmpty());
    }

    // A clock across a chunk border stops when the player walks away and goes on where it stopped
    // when he comes back: the states it goes through are those of a clock nobody left, without the
    // tick in which its chunks left (decision E-374).
    void aClockGoesOnWhereItStopped()
    {
        const int count = 60;
        const int leaveAt = 23;
        QStringList reference;
        QList<QcBlockPos> cells;
        {
            buildArea();
            const int x = border().x;
            // Three cells or more from the other chunk borders: the clock never waits for chunks
            // outside its group, whatever order the chunks around it come back in.
            const int z = border().z - 4;
            QVERIFY(z >= 0);
            observerClock(x, z);
            cells = {at(x - 2, 0, z), at(x - 1, 0, z), at(x, 0, z), at(x + 1, 0, z)};
            reference = runClock(cells, count, -1, 0);
            cleanup();
        }
        buildArea();
        observerClock(border().x, border().z - 4);
        const QStringList returned = runClock(cells, count - 1, leaveAt, 80);
        QStringList expected = reference;
        expected.removeAt(leaveAt);
        expected.resize(count - 1);
        QCOMPARE(returned, expected);
        // The clock does change: a frozen line would prove nothing.
        QVERIFY(QSet<QString>(reference.begin(), reference.end()).size() > 1);
    }

    // A machine over four chunks (clocks across both borders of a chunk corner) is one group: its
    // chunks leave in the same tick and come back in the same tick, and it goes on where it stopped.
    void aMachineOverFourChunksFreezesAndResumes()
    {
        const int count = 60;
        const int leaveAt = 17;
        QStringList reference;
        QList<QcBlockPos> cells;
        const auto build = [&] {
            const QcBlockPos corner = border();
            QVERIFY(corner.x >= 3 && corner.x <= areaSize - 3 && corner.z >= 3 && corner.z <= areaSize - 3);
            observerClock(corner.x, corner.z - 3);
            observerClock(corner.x, corner.z + 2);
            observerClockSouth(corner.x - 3, corner.z);
            cells = {at(corner.x - 1, 0, corner.z - 3), at(corner.x, 0, corner.z - 3), at(corner.x - 1, 0, corner.z + 2), at(corner.x, 0, corner.z + 2),
                     at(corner.x - 3, 0, corner.z - 1), at(corner.x - 3, 0, corner.z)};
        };
        {
            buildArea();
            build();
            reference = runClock(cells, count, -1, 0);
            cleanup();
        }
        buildArea();
        build();
        const QSet<QcChunkPos> machine = chunksOf(cells);
        QCOMPARE(machine.size(), 4);
        QCOMPARE(m_server->chunkLoading().groupOf(*machine.cbegin()).chunks.size(), 4);

        // Leaving and coming back by hand, to watch the chunks: all or none of them, every tick.
        QStringList seen;
        int tick = 0;
        for (int guard = 0; seen.size() < count - 1 && guard < 400; ++guard) {
            if (tick == leaveAt) {
                teleport(away());
            }
            if (tick == leaveAt + 60) {
                teleport(m_feet);
            }
            ticks(1);
            ++tick;
            const qsizetype loaded = loadedOf(machine);
            QVERIFY2(loaded == 0 || loaded == 4, qPrintable(u"%1 of 4 chunks loaded at tick %2"_s.arg(loaded).arg(tick)));
            if (const QString line = states(cells); !line.isEmpty()) {
                seen.append(line);
            }
        }
        QStringList expected = reference;
        expected.removeAt(leaveAt);
        expected.resize(count - 1);
        QCOMPARE(seen, expected);
    }

    // A group larger than chunkGroupLimit is not kept whole (decision E-375): its chunks come and go
    // one by one, the server warns once and counts it, and a wire of it waits at the loaded edge.
    void anOversizeGroupFallsBackToWaitingAtItsEdge()
    {
        QcServerConfig config;
        config.limits.chunkGroupLimit = 2;
        buildArea(config);
        const QcBlockPos corner = border();
        observerClock(corner.x, 4);
        observerClockSouth(corner.x - 3, corner.z);
        observerClock(corner.x, corner.z + 2);
        ticks(2);
        QcChunkGroups::Group group = m_server->chunkLoading().groupOf(at(corner.x, 0, 4).chunk());
        QVERIFY(group.oversize);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"connect more than 2 chunks"_s));
        teleport(away());
        ticks(2);
        QVERIFY(m_server->chunkLoading().numbers().oversizeGroups > 0);
        QCOMPARE(loadedOf(chunksOf({at(0, 0, 0), at(areaSize - 1, 0, areaSize - 1)})), 0);
    }

    // Pistons moving every kind of block entity across a chunk border while the chunks leave in the
    // middle of the move: nothing is lost and nothing exists twice (decision E-374).
    void pistonsMovingAcrossABorderWhileTheChunksLeave()
    {
        buildArea();
        const int x = border().x - 3;
        put(x - 2, 0, 6, u"lever[attach=floor]"_s);
        put(x - 1, 0, 6, u"sticky_signal_piston[facing=east]"_s);
        put(x, 0, 6, u"chest[facing=south]"_s);
        put(x + 1, 0, 6, u"furnace[facing=south]"_s);
        put(x + 2, 0, 6, u"signal_hopper[facing=down]"_s);
        put(x + 3, 0, 6, u"signal_dispenser[facing=up]"_s);
        put(x + 4, 0, 6, u"signal_dropper[facing=up]"_s);
        fill(x, 0, 6, {stack("qtcraft:diamond", 5), stack("qtcraft:stick", 7)});
        fill(x + 1, 0, 6, {stack("qtcraft:iron_ore", 4), stack("qtcraft:coal", 2)});
        fill(x + 2, 0, 6, {stack("qtcraft:gold_ingot", 3)});
        fill(x + 3, 0, 6, {stack("qtcraft:cobblestone", 6)});
        fill(x + 4, 0, 6, {stack("qtcraft:wheat_seeds", 8)});
        ticks(2);
        const auto total = [this] {
            qsizetype count = 0;
            const QStringList lines = QcBlockEntityDump::lines(*m_server, *m_content);
            for (const QString &line : lines) {
                if (line.startsWith(u"container"_s) || line.startsWith(u"machine"_s)) {
                    count += line.count(u'x');
                }
            }
            return count;
        };
        const qsizetype before = total();
        flip(x - 2, 0, 6);
        ticks(1);
        // Gone while the blocks slide.
        teleport(away());
        ticks(3);
        QCOMPARE(loadedOf(chunksOf({at(x, 0, 6), at(x + 5, 0, 6)})), 0);
        teleport(m_feet);
        for (int i = 0; i < 1000 && loadedOf(chunksOf({at(x, 0, 6), at(x + 5, 0, 6)})) < 2; ++i) {
            ticks(1);
        }
        ticks(5);
        QVERIFY(isBlock(x + 1, 0, 6, "chest"));
        QVERIFY(isBlock(x + 5, 0, 6, "signal_dropper"));
        QCOMPARE(total(), before);
        QVERIFY(m_server->items().items().isEmpty());
        QVERIFY(QcBlockEntityChecks::problems(m_server->world()).isEmpty());
    }

    // An item thrown into a chunk that is not loaded brings its chunk in and goes to storage with it
    // (decision E-374): it is kept, not lost, and comes back with the chunk.
    void anEntityInAChunkThatIsNotLoadedGoesWithIt()
    {
        buildArea();
        const QcChunkPos far{m_feet.chunk().x + 6, m_feet.chunk().z};
        QVERIFY(!m_server->world().chunk(far));
        QcItemEntityManager::Drop drop;
        drop.stack = stack("qtcraft:diamond", 2);
        drop.position = {far.x * QcWorld::chunkSize + 8.5, 200.0, far.z * QcWorld::chunkSize + 8.5};
        m_server->items().spawn(drop, m_nextId++);
        for (int i = 0; i < 200 && !m_server->items().items().isEmpty(); ++i) {
            ticks(1);
        }
        QVERIFY(m_server->items().items().isEmpty());
        QVERIFY(m_server->world().saves().stored(far).has_value());
        m_server->world().loadChunk(far);
        QCOMPARE(m_server->items().items().size(), 1);
    }

    // Experience orbs leave with their chunk and come back with it during the session; beyond the
    // limit the returning orbs join the nearest one, so no point is lost (decisions E-398, E-399).
    void orbsLeaveWithTheirChunkAndJoinBeyondTheLimit()
    {
        buildArea();
        const QList<std::pair<QcBlockPos, int>> placed{{at(2, 0, 2), 4}, {at(6, 0, 2), 5}, {at(2, 0, 6), 6}};
        for (const auto &[cell, value] : placed) {
            QcExperienceOrb orb;
            orb.id = m_nextId++;
            orb.value = value;
            orb.motion.position = {cell.x + 0.5, double(cell.y), cell.z + 0.5};
            QVERIFY(m_server->orbs().add(orb));
        }
        ticks(20);
        QCOMPARE(m_server->orbs().totalPoints(), 15);

        teleport(away());
        ticks(1);
        QVERIFY(m_server->orbs().orbs().isEmpty());
        m_server->orbs().setLimit(1);
        ticks(10);

        teleport(m_feet);
        loadAround(at(areaSize / 2, 0, areaSize / 2));
        ticks(1);
        QCOMPARE(m_server->orbs().orbs().size(), 1);
        QCOMPARE(m_server->orbs().totalPoints(), 15);
        QVERIFY(QcEntityChecks::problems(m_server->entities()).isEmpty());
    }

    // /regenerate reaches a changed chunk that is not loaded: it is read, generated again and the
    // stored copy is replaced (decision E-374).
    void regeneratingAStoredChunkThatIsNotLoaded()
    {
        buildArea();
        const QcBlockPos far = m_feet.offset(10 * QcWorld::chunkSize, 0, 0);
        m_server->world().loadChunk(far.chunk());
        const QcBlockPos marker{far.x, QcWorld::worldHeight - 2, far.z};
        QVERIFY(m_server->world().setBlock(marker, id("qtcraft:glass")));
        ticks(2);
        QVERIFY(!m_server->world().chunk(far.chunk()));
        chat(u"/regenerate %1 %2"_s.arg(far.x).arg(far.z));
        chat(u"/confirm"_s);
        QVERIFY(m_client->chatLines.join(u'\n').contains(u"Regenerated 1 chunks"_s));
        ticks(2);
        m_server->world().loadChunk(far.chunk());
        QCOMPARE(m_server->world().blockAt(marker), std::optional<QcBlockId>(qcAirBlockId));
    }

    // Circuit work kept with a chunk keeps whether it was a signal (review of 31.3, decision E-376): a
    // wire whose recomputation waited at the view edge only because its chunk had just come back does
    // not come back as a signal and asks for no load ticket, however often the player leaves and returns.
    void looksAtTheEdgeAskForNoTickets()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        const QcChunkPos home = m_feet.chunk();
        const int y = m_feet.y + 20;
        // Along the south border of the chunk east of the player's, up to its east border: the
        // chunk south of it is in view, the one east of it is not. When either of the two comes back
        // while the other is there, the wire on their border is looked at and waits for the chunk
        // beyond the view.
        const int base = (home.x + 1) * QcWorld::chunkSize;
        const int z = home.z * QcWorld::chunkSize + QcWorld::chunkSize - 1;
        const auto at = [&](int x) { return QcBlockPos{base + x, y, z}; };
        m_server->world().loadChunk({home.x + 1, home.z});
        m_server->world().loadChunk({home.x + 1, home.z + 1});
        for (int x = 4; x < QcWorld::chunkSize; ++x) {
            putAt(at(x).offset(0, -1, 0), id("qtcraft:stone"));
            putAt(at(x), x == 4 ? state(u"lever[attach=floor,powered=true]"_s) : id("qtcraft:signal_dust"));
        }
        ticks(200);
        const quint64 given = m_server->chunkLoading().tickets().given();
        const quint64 refused = m_server->chunkLoading().tickets().refused();
        for (int round = 0; round < 4; ++round) {
            teleport(m_feet.offset(30 * QcWorld::chunkSize, 30, 0));
            ticks(5);
            QVERIFY(!m_server->world().chunk(home));
            teleport(m_feet);
            for (int i = 0; i < 400 && !m_server->world().chunk({home.x + 1, home.z}); ++i) {
                ticks(1);
            }
            ticks(20);
            QVERIFY(m_server->world().chunk({home.x + 1, home.z}));
        }
        QCOMPARE(m_server->chunkLoading().tickets().given(), given);
        QCOMPARE(m_server->chunkLoading().tickets().refused(), refused);
    }

    // A save that cannot be written is reported once and not tried again every tick (review of 31.3):
    // one save runs at a time, and after a failure only the normal interval tries again, even while
    // more unloaded chunks wait than unsavedChunks allows.
    void aFailingSaveIsReportedOnceAndNotRepeatedEveryTick()
    {
        QTemporaryDir directory;
        // A file where the folder of the blocks belongs: no save can write it.
        QFile blocker(directory.filePath(u"regions"_s));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 600;
        config.limits.unsavedChunks = 16;
        join(config);
        for (int i = 0; i < 40; ++i) {
            const QcBlockPos far = m_feet.offset(400 + 16 * i, 0, 0);
            m_server->world().loadChunk(far.chunk());
            QVERIFY(m_server->world().setBlock({far.x, QcWorld::worldHeight - 2, far.z}, id("qtcraft:glass")));
        }
        ticks(2);
        m_server->waitForStorage();
        const quint64 before = m_server->autosaveCount();
        ticks(100);
        m_server->waitForStorage();
        ticks(2);
        QVERIFY2(m_server->autosaveCount() - before <= 1, qPrintable(QString::number(m_server->autosaveCount() - before)));
        const qsizetype reports = std::ranges::count_if(m_client->chatLines, [](const QString &line) { return line.startsWith(u"Autosave failed"_s); });
        QCOMPARE(reports, 1);
    }

    // The players' files are written with the world in one save (review of 31.3, decision E-378): a
    // save that fails writes neither, so an item between a chest and an inventory is never in both.
    void playersAreSavedWithTheWorld()
    {
        QTemporaryDir directory;
        QFile blocker(directory.filePath(u"regions"_s));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        QVERIFY(m_server->world().setBlock(m_feet.offset(2, 0, 0), id("qtcraft:glass")));
        const QString player = directory.filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Raw"_s));
        QVERIFY(!m_server->save().has_value());
        QVERIFY(!QFile::exists(player));
        QVERIFY(QFile::remove(blocker.fileName()));
        QVERIFY(m_server->save().has_value());
        QVERIFY(QFile::exists(player));
        QVERIFY(QFile::exists(directory.filePath(u"regions"_s)));
    }

    // A save that broke off and cannot be finished (its journal names a staged file that is gone)
    // refuses the world with the way out; without the journal the world opens as saved before.
    void aSaveThatCannotBeFinishedNamesTheWayOut()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        QVERIFY(m_server->save().has_value());
        cleanup();

        QByteArray steps;
        QDataStream stream(&steps, QIODevice::WriteOnly);
        stream.setVersion(QcSaveFile::streamVersion);
        stream << quint32(1) << u"world.json"_s << false << qint64(1) << quint32(0);
        QVERIFY(QcSaveFile::write(directory.filePath(u"save.journal"_s), "QCSJ", steps, 2).has_value());
        {
            QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            const QcStatus opened = server.openWorld();
            QVERIFY(!opened.has_value());
            QVERIFY2(opened.error().contains(u"removing save.journal and every file ending in .next"_s), qPrintable(opened.error()));
            const bool journal = server.damagedFile().part == QcServerPersistence::DamagedPart::Journal;
            QVERIFY(journal);
        }

        QVERIFY(QFile::remove(directory.filePath(u"save.journal"_s)));
        join(config);
        QVERIFY(m_server->world().chunk(m_feet.chunk()));
    }

    // A long exploration keeps the server's memory bounded (decision E-374): only the chunks around
    // the player stay loaded, what they keep goes to storage, unloaded chunks waiting in memory are
    // saved once there are more than unsavedChunks, and everything is there when the player comes back.
    void memoryStaysBoundedDuringALongExploration()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        config.limits.unsavedChunks = 16;
        m_viewDistance = 2;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        const QcBlockPos start = m_feet;
        qsizetype mostLoaded = 0;
        qsizetype mostEntities = 0;
        const int legs = 120;
        for (int leg = 1; leg <= legs; ++leg) {
            const QcBlockPos here = start.offset(leg * QcWorld::chunkSize, 0, 0);
            teleport(here.offset(0, 60, 0));
            for (int i = 0; i < 400 && !m_server->world().chunk(here.chunk()); ++i) {
                ticks(1);
            }
            QVERIFY2(m_server->world().chunk(here.chunk()), qPrintable(u"leg %1: player at %2 %3, %4 loaded, %5 under way"_s.arg(leg)
                                                                         .arg(m_client->motion.position.x)
                                                                         .arg(m_client->motion.position.z)
                                                                         .arg(m_server->world().loadedChunkCount())
                                                                         .arg(m_server->world().pendingGenerationCount())));
            // Something to keep in every chunk passed: a changed block and an item.
            QVERIFY(m_server->world().setBlock(here.offset(0, 40, 0), id("qtcraft:glass")));
            QcItemEntityManager::Drop drop;
            drop.stack = stack("qtcraft:stick", 1 + leg % 60);
            drop.position = {here.x + 0.5, double(here.y + 41), here.z + 0.5};
            m_server->items().spawn(drop, m_nextId++);
            // Within the chat rate of a player (2 lines a second): one teleport per leg.
            ticks(10);
            mostLoaded = std::max(mostLoaded, m_server->world().loadedChunkCount());
            mostEntities = std::max(mostEntities, m_server->items().items().size());
            QVERIFY(m_server->chunkLoading().numbers().unsaved <= 2 * qsizetype(config.limits.unsavedChunks) + 25);
        }
        const qsizetype square = qsizetype(2 * m_viewDistance + 3) * qsizetype(2 * m_viewDistance + 3);
        QVERIFY2(mostLoaded <= 2 * square, qPrintable(QString::number(mostLoaded)));
        QVERIFY2(mostEntities <= 30, qPrintable(QString::number(mostEntities)));
        QVERIFY(m_server->world().saves().storedChunkCount() >= legs);

        // Back at a chunk passed long ago: its block and its item are there.
        const QcBlockPos early = start.offset(5 * QcWorld::chunkSize, 0, 0);
        teleport(early.offset(0, 60, 0));
        for (int i = 0; i < 1000 && !m_server->world().chunk(early.chunk()); ++i) {
            ticks(1);
        }
        QCOMPARE(m_server->world().blockAt(early.offset(0, 40, 0)), std::optional<QcBlockId>(id("qtcraft:glass")));
        QVERIFY(std::ranges::any_of(m_server->items().items(), [](const QcItemEntity &item) { return item.stack.count == 6; }));
        QVERIFY(m_server->save().has_value());
    }
};

QTEST_GUILESS_MAIN(TestChunkUnloading)
#include "test_chunk_unloading.moc"
