#include "QcBlockEntityChecks.h"
#include "QcBlockEntityDump.h"
#include "QcMachineFixture.h"
#include "QcShowcaseGenerator.h"
#include "QcStoredWorld.h"
#include "QcWorldGenerators.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

// Comparison runs of the block-entity register (stage 31.1, decision E-357): the showcase world and a
// piston chain across a chunk border in four rotations, saved in the middle of a move and opened
// again. The runs give a canonical dump of the block entities, the items, the blocks of every
// stored chunk and what the client was told. Until stage 31.3 it had to equal the dump the code
// before the register wrote (tests/data/block-entity-compare-before-31-1.txt, from this test at
// eef0382); since chunks leave the world (stage 31.3) it is compared with
// tests/data/block-entity-compare-31-3.txt, whose only differences to that one are the deliberate
// ones of decision E-374 (the waits of a chunk stand still while it is away, a returning chunk is
// looked at along its borders); the dump lists what chunks that are not loaded keep, too. New
// content changes it: stage 32.1 (decision E-382) added two items, which the showcase's chests list,
// and the rails station, which moves the stations after it (their positions, and the timers and
// counts of circuits the run reaches at another moment); it was written again then, and again in
// stage 32.2 (decisions E-384 to E-387: five new items and the station of signal rails and container
// carts, whose chests and carts fill as the run passes, with the same kind of shifts). Checked in
// the review of stage 32: with both new stations built after all other areas and left out of the
// tour, the dump equals the one before stage 32 but for the new items in the item chests, so the
// changed furnace, burnout, sapling and stick values come from the tour reaching the later
// stations 80 ticks later (the stations stay where their order puts them). One difference remains
// in that run: the chest at 140 65 -26 holds the same 58 sticks, split 27/31 instead of 36/22
// between its slots; which change of stage 32.2 moves them is not found yet. With
// QTCRAFT_COMPARE_DUMP=<file> the dump is also written there. The deliberate changes of the register
// are checked apart (deliberateChangesOfTheRegister).
class TestBlockEntityCompare : public QcMachineFixture
{
    Q_OBJECT

public:
    // The showcase run unloads what the player leaves behind, as the game does (stage 31.3).
    // Changed chunks stay and the others leave, as in the code that wrote the dump (stage 31.3).
    // Chunks load at once, so the run does not depend on how fast the threads are.
    TestBlockEntityCompare()
    {
        m_keepChangedChunks = true;
        m_loadChunksAtOnce = true;
    }

private:
    QStringList m_dump;
    // Runs that add to the dump; it is compared only when all of them ran.
    int m_dumpRuns = 0;
    static constexpr int allDumpRuns = 5;

    static QString stackText(QcItemStack stack, const QcContentRegistry &content) { return QcBlockEntityDump::stackText(stack, content); }
    static QString posText(QcBlockPos pos) { return QcBlockEntityDump::posText(pos); }

    QStringList blockEntityLines() const { return QcBlockEntityDump::lines(*m_server, *m_content); }

    QStringList itemLines() const
    {
        QStringList lines;
        for (const QcItemEntity &dropped : m_server->items().items()) {
            const QcVec3 &p = dropped.motion.position;
            lines.append(u"item %1 at %2 %3 %4"_s.arg(stackText(dropped.stack, *m_content)).arg(p.x, 0, 'f', 3).arg(p.y, 0, 'f', 3).arg(p.z, 0, 'f', 3));
        }
        return lines;
    }

    // The blocks of every stored chunk by state name: the bytes of a region file depend on the order
    // of its chunks, which says nothing about the world.
    QStringList regionLines(const QString &world) const
    {
        QStringList lines;
        const QcResult<QcStoredWorld> stored = QcStoredWorld::read(world, *m_content);
        if (!stored) {
            return {u"regions unreadable: "_s + stored.error()};
        }
        const auto *chunks = &stored->chunks;
        QHash<QcBlockId, QByteArray> names;
        for (auto it = chunks->constBegin(); it != chunks->constEnd(); ++it) {
            QCryptographicHash hash(QCryptographicHash::Sha1);
            for (int y = 0; y < QcWorld::worldHeight; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        const QcBlockId block = it.value()->block(x, y, z);
                        auto name = names.find(block);
                        if (name == names.end()) {
                            name = names.insert(block, m_content->stateName(block).toUtf8() + ';');
                        }
                        hash.addData(*name);
                    }
                }
            }
            lines.append(u"chunk %1 %2 %3"_s.arg(it.key().x).arg(it.key().z).arg(QString::fromLatin1(hash.result().toHex())));
        }
        return lines;
    }

    static qint32 legacyCode(QcEntityKind kind, qint32 creatureType)
    {
        switch (kind) {
        case QcEntityKind::Creature:
            return creatureType;
        case QcEntityKind::Player:
            return -1;
        case QcEntityKind::Item:
            return -2;
        case QcEntityKind::FallingBlock:
            return -3;
        case QcEntityKind::ArmourStand:
            return -4;
        case QcEntityKind::MovingBlock:
            return -5;
        case QcEntityKind::Minecart:
            return -6;
        }
        return 0;
    }

    QStringList clientLines() const
    {
        QStringList lines;
        QCryptographicHash updates(QCryptographicHash::Sha1);
        for (const QcMsg::BlockUpdate &update : std::as_const(m_client->blockUpdates)) {
            updates.addData(posText(update.pos).toUtf8());
            updates.addData(m_content->stateName(update.block).toUtf8());
        }
        lines.append(u"client block updates %1 %2"_s.arg(m_client->blockUpdates.size()).arg(QString::fromLatin1(updates.result().toHex())));
        // Counted by the codes this dump has always used: a creature's type, -1 player, -2 item,
        // -3 falling block, -4 armour stand, -5 sliding block.
        QHash<qint32, int> kinds;
        for (auto it = m_client->entities.cbegin(); it != m_client->entities.cend(); ++it) {
            ++kinds[legacyCode(it.value(), m_client->spawns.value(it.key()).creatureType)];
        }
        QList<qint32> keys = kinds.keys();
        std::ranges::sort(keys);
        for (const qint32 kind : std::as_const(keys)) {
            lines.append(u"client entities of kind %1: %2"_s.arg(kind).arg(kinds.value(kind)));
        }
        return lines;
    }

    void record(const QString &title, const QStringList &lines)
    {
        QStringList sorted = lines;
        std::ranges::sort(sorted);
        m_dump.append(u"== "_s + title);
        m_dump.append(sorted);
    }

    // Records the state and checks the rules between blocks and block entities (decision E-357).
    void recordState(const QString &title)
    {
        record(title + u" block entities"_s, blockEntityLines());
        record(title + u" items"_s, itemLines());
        const QStringList problems = QcBlockEntityChecks::problems(m_server->world());
        QVERIFY2(problems.isEmpty(), qPrintable(title + u": "_s + problems.join(u"; "_s)));
    }

    // Nothing of the chain got lost or copied, wherever it is now.
    void chainIsWhole()
    {
        QCOMPARE(everywhere("qtcraft:diamond"), 5);
        QCOMPARE(everywhere("qtcraft:stick"), 7);
        QCOMPARE(everywhere("qtcraft:iron_ore") + everywhere("qtcraft:iron_ingot"), 4);
        QCOMPARE(everywhere("qtcraft:dirt"), 3);
        QCOMPARE(everywhere("qtcraft:water_bucket"), 1);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 9);
    }

    void visitAll(const QcShowcasePlan &plan)
    {
        for (const QcShowcaseAreaInfo &info : plan.areas) {
            const QcBlockPos middle = info.origin.offset(info.sizeX / 2, 1, info.sizeZ / 2);
            chat(u"/tp %1 %2 %3"_s.arg(middle.x).arg(middle.y + 6).arg(middle.z));
            // Loaded here, beyond the streaming range, so no chunk comes from the background generator
            // at a time that depends on the machine.
            for (int dx = -3; dx <= 3; ++dx) {
                for (int dz = -3; dz <= 3; ++dz) {
                    m_server->world().loadChunk({middle.chunk().x + dx, middle.chunk().z + dz});
                }
            }
            idle(40);
        }
    }

    void reopen(const QcServerConfig &config, std::unique_ptr<QcWorldGenerator> generator)
    {
        QVERIFY(m_server->save().has_value());
        m_client.reset();
        m_server.reset();
        join(config, std::move(generator));
        loadStored();
    }

    // A sticky piston pushing a chest, a furnace, a hopper, a dispenser and a dropper, all with
    // contents, over the chunk border in the middle of the area.
    void buildChain()
    {
        put(2, 0, 6, u"lever[attach=floor]"_s);
        put(3, 0, 6, u"sticky_signal_piston[facing=east]"_s);
        put(4, 0, 6, u"chest[facing=south]"_s);
        put(5, 0, 6, u"furnace[facing=south]"_s);
        put(6, 0, 6, u"signal_hopper[facing=down]"_s);
        put(7, 0, 6, u"signal_dispenser[facing=up]"_s);
        put(8, 0, 6, u"signal_dropper[facing=up]"_s);
        fill(4, 0, 6, {stack("qtcraft:diamond", 5), stack("qtcraft:stick", 7)});
        fill(5, 0, 6, {stack("qtcraft:iron_ore", 4), stack("qtcraft:coal", 2)});
        fill(6, 0, 6, {stack("qtcraft:dirt", 3)});
        fill(7, 0, 6, {stack("qtcraft:water_bucket", 1)});
        fill(8, 0, 6, {stack("qtcraft:cobblestone", 9)});
    }

private slots:
    void cleanupTestCase()
    {
        if (const QString path = qEnvironmentVariable("QTCRAFT_COMPARE_DUMP"); !path.isEmpty()) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(m_dump.join(u'\n').toUtf8() + '\n');
        }
        if (m_dumpRuns != allDumpRuns) {
            return;
        }
        QFile golden(QStringLiteral(QTCRAFT_COMPARE_GOLDEN));
        QVERIFY(golden.open(QIODevice::ReadOnly));
        const QStringList before = QString::fromUtf8(golden.readAll()).split(u'\n', Qt::SkipEmptyParts);
        QCOMPARE(m_dump.size(), before.size());
        for (qsizetype i = 0; i < m_dump.size(); ++i) {
            QCOMPARE(m_dump[i], before[i]);
        }
    }

    void showcaseRunAndReload()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.generator = u"showcase"_s;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = 1;
        join(config, QcWorldGenerators::create(u"showcase"_s, m_content, 0));
        const QcShowcasePlan plan = QcShowcaseGenerator(m_content).plan();
        visitAll(plan);
        idle(200);
        recordState(u"showcase after the tour"_s);
        record(u"showcase client"_s, clientLines());

        reopen(config, QcWorldGenerators::create(u"showcase"_s, m_content, 0));
        record(u"showcase regions"_s, regionLines(directory.path()));
        recordState(u"showcase reopened"_s);
        idle(100);
        recordState(u"showcase reopened and run"_s);
        // Deliberate: generated chests have their (empty) container since their chunk arrived; the
        // dump lists only containers with contents, as the code before the register kept none.
        const QList<QcBlockEntities::Container> containers = allContainers();
        QVERIFY(std::ranges::any_of(containers, [](const QcBlockEntities::Container &container) {
            return std::ranges::all_of(container.stacks, [](QcItemStack stack) { return stack.isEmpty(); });
        }));
        m_client.reset();
        m_server.reset();
        ++m_dumpRuns;
    }

    void pistonChainAcrossAChunkBorder_data()
    {
        QTest::addColumn<int>("turns");
        for (int turns = 0; turns < 4; ++turns) {
            QTest::addRow("turns %d", turns) << turns;
        }
    }

    void pistonChainAcrossAChunkBorder()
    {
        QFETCH(int, turns);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = 1;
        m_turns = turns;
        buildArea(config);
        // A chunk border runs between local 6 and 7 along both axes, so the chain crosses it in every turn.
        const QcBlockPos corner = m_feet.offset(-areaSize / 2, 12, -areaSize / 2);
        clearArea({qcFloorDiv(corner.x, QcWorld::chunkSize) * QcWorld::chunkSize + 9, corner.y,
                   qcFloorDiv(corner.z, QcWorld::chunkSize) * QcWorld::chunkSize + 9});
        buildChain();
        ticks(5);
        flip(2, 0, 6);
        ticks(2);
        // In the middle of the move: the blocks are already in their new cells, the clients see them slide.
        const QString title = u"chain turns %1"_s.arg(turns);
        m_dump.append(u"== %1 cells mid-move"_s.arg(title));
        for (int x = 2; x < 11; ++x) {
            m_dump.append(look(x, 0, 6));
        }
        recordState(title + u" mid-move"_s);
        chainIsWhole();
        QCOMPARE(countIn(5, 0, 6, "qtcraft:diamond"), 5);
        QCOMPARE(countIn(7, 0, 6, "qtcraft:dirt"), 3);
        QCOMPARE(countIn(9, 0, 6, "qtcraft:cobblestone"), 9);

        reopen(config, nullptr);
        m_turns = turns;
        recordState(title + u" reopened"_s);
        chainIsWhole();
        QCOMPARE(countIn(5, 0, 6, "qtcraft:diamond"), 5);
        QCOMPARE(countIn(8, 0, 6, "qtcraft:water_bucket"), 1);
        ticks(40);
        flip(2, 0, 6);
        ticks(40);
        m_dump.append(u"== %1 cells after pulling back"_s.arg(title));
        for (int x = 2; x < 11; ++x) {
            m_dump.append(look(x, 0, 6));
        }
        recordState(title + u" pulled back"_s);
        chainIsWhole();
        QCOMPARE(countIn(4, 0, 6, "qtcraft:diamond"), 5);
        m_client.reset();
        m_server.reset();
        ++m_dumpRuns;
    }

    void deliberateChangesOfTheRegister()
    {
        // Not in the dump, on purpose different from before the register: a pushed lamp and a pushed
        // observer take their scheduled tick along and switch off in the same tick as an unpushed
        // twin (before, they planned anew in their new cell and switched one or more ticks later).
        buildArea();
        put(1, 0, 5, u"lever[attach=floor]"_s);
        put(0, 0, 5, u"signal_lamp"_s);
        put(2, 0, 5, u"signal_lamp"_s);
        put(2, 0, 3, u"lever[attach=floor]"_s);
        put(2, 0, 4, u"signal_piston[facing=south]"_s);
        put(5, 0, 10, u"signal_observer[facing=north]"_s);
        put(9, 0, 10, u"signal_observer[facing=north]"_s);
        put(4, 0, 11, u"lever[attach=floor]"_s);
        put(4, 0, 10, u"signal_piston[facing=east]"_s);
        ticks(5);
        flip(1, 0, 5);
        ticks(3);
        flip(1, 0, 5);
        put(5, 0, 9, u"stone"_s);
        put(9, 0, 9, u"stone"_s);
        ticks(1);
        for (int tick = 0; tick < 2 && !flag(9, 0, 10, u"powered"_s); ++tick) {
            ticks(1);
        }
        QVERIFY(flag(9, 0, 10, u"powered"_s));
        flip(2, 0, 3);
        flip(4, 0, 11);
        ticks(1);
        QVERIFY(isBlock(2, 0, 6, "signal_lamp_lit"));
        QVERIFY(isBlock(6, 0, 10, "signal_observer"));
        for (int tick = 0; tick < 8; ++tick) {
            QCOMPARE(isBlock(2, 0, 6, "signal_lamp_lit"), isBlock(0, 0, 5, "signal_lamp_lit"));
            QCOMPARE(flag(6, 0, 10, u"powered"_s), flag(9, 0, 10, u"powered"_s));
            ticks(1);
        }
        QVERIFY(isBlock(2, 0, 6, "signal_lamp") && !flag(6, 0, 10, u"powered"_s));
        const QStringList problems = QcBlockEntityChecks::problems(m_server->world());
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u"; "_s)));
        m_client.reset();
        m_server.reset();
    }
};

QTEST_GUILESS_MAIN(TestBlockEntityCompare)
#include "test_block_entity_compare.moc"
