#include "QcBlockEntityDump.h"
#include "QcDumpDiff.h"
#include "QcMachineFixture.h"
#include "QcStoredWorld.h"
#include "server/blockentities/QcBlockEntityChecks.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <map>
#include <tuple>

using namespace Qt::StringLiterals;

// Comparison runs of the block-entity register (decisions E-357, E-374, E-416): a small station of
// block entities on the test terrain (chests, a hopper filling the chest beside it, a burning
// furnace emptied by a hopper across a chunk border, an observer clock across the border firing a
// dropper, a sapling, a repeater, a torch clock that burns out, a powered trapdoor, a hopper busy
// with 64 items across the border) that the player leaves and comes back to (its chunks unload and
// load again), then saved and opened again; and a piston chain across a chunk border in four
// rotations, saved in the middle of a move and opened again. The runs give a canonical dump of the block
// entities, the items, the changed blocks of every stored chunk and what the client was told; it
// must equal tests/data/block-entity-compare.txt. The station does not depend on the showcase world,
// so only changes of the block-entity rules, the station or the content it uses change the dump.
// A difference is printed line by line per section. To write the dump again after a deliberate
// change (and to check the difference before committing it):
//   QTCRAFT_COMPARE_DUMP=tests/data/block-entity-compare.txt build/debug/tests/test_block_entity_compare
// The deliberate changes of the register are checked apart (deliberateChangesOfTheRegister).
class TestBlockEntityCompare : public QcMachineFixture
{
    Q_OBJECT

public:
    // The chain runs keep changed chunks (they load what was stored, as a world was opened before
    // stage 31.3); the station run lets them leave like the game (stationRunAndReload). Chunks load
    // at once, so the runs do not depend on how fast the threads are.
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

    // The blocks of every stored chunk that differ from the generated terrain, by state: how many,
    // and where when they are few. The bytes of a region file depend on the order of its chunks,
    // which says nothing about the world.
    QStringList regionLines(const QString &world) const
    {
        const QcResult<QcStoredWorld> stored = QcStoredWorld::read(world, *m_content);
        if (!stored) {
            return {u"regions unreadable: "_s + stored.error()};
        }
        const QcTestTerrainGenerator terrain(m_content);
        QStringList lines;
        for (auto it = stored->chunks.constBegin(); it != stored->chunks.constEnd(); ++it) {
            const std::shared_ptr<QcChunk> generated = terrain.generate(it.key());
            const QString chunk = u"chunk %1 %2:"_s.arg(it.key().x).arg(it.key().z);
            const std::map<QString, QList<QcBlockPos>> changed = changedBlocks(*it.value(), *generated);
            if (changed.empty()) {
                lines.append(chunk + u" as generated"_s);
            }
            for (const auto &[state, cells] : changed) {
                QString line = u"%1 %2 x %3"_s.arg(chunk).arg(cells.size()).arg(state);
                if (cells.size() <= 4) {
                    for (const QcBlockPos &cell : cells) {
                        line += u" | "_s + posText(cell);
                    }
                }
                lines.append(line);
            }
        }
        return lines;
    }

    // The cells of `chunk` that differ from `generated`, by the state they hold.
    std::map<QString, QList<QcBlockPos>> changedBlocks(const QcChunk &chunk, const QcChunk &generated) const
    {
        std::map<QString, QList<QcBlockPos>> changed;
        const int x0 = chunk.pos().x * QcWorld::chunkSize;
        const int z0 = chunk.pos().z * QcWorld::chunkSize;
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    const QcBlockId block = chunk.block(x, y, z);
                    if (block != generated.block(x, y, z)) {
                        changed[m_content->stateName(block)].append({x0 + x, y, z0 + z});
                    }
                }
            }
        }
        return changed;
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
        case QcEntityKind::ExperienceOrb:
            return -7;
        case QcEntityKind::Projectile:
            return -8;
        case QcEntityKind::PrimedExplosive:
            return -9;
        case QcEntityKind::Hanging:
            return -10;
        }
        return 0;
    }

    QStringList clientLines() const
    {
        QStringList lines;
        // Per cell: how many updates the client got and the last state.
        std::map<QString, std::pair<int, QString>> updates;
        for (const QcMsg::BlockUpdate &update : std::as_const(m_client->blockUpdates)) {
            auto &[count, last] = updates[posText(update.pos)];
            ++count;
            last = m_content->stateName(update.block);
        }
        for (const auto &[pos, update] : updates) {
            lines.append(u"client updates at %1: %2, last %3"_s.arg(pos).arg(update.first).arg(update.second));
        }
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
        QCOMPARE(everywhere("qtcraft:raw_iron") + everywhere("qtcraft:iron_ingot"), 4);
        QCOMPARE(everywhere("qtcraft:dirt"), 3);
        QCOMPARE(everywhere("qtcraft:water_bucket"), 1);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 9);
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
        fill(5, 0, 6, {stack("qtcraft:raw_iron", 4), stack("qtcraft:coal", 2)});
        fill(6, 0, 6, {stack("qtcraft:dirt", 3)});
        fill(7, 0, 6, {stack("qtcraft:water_bucket", 1)});
        fill(8, 0, 6, {stack("qtcraft:cobblestone", 9)});
    }

    // The station (local cells; the chunk border runs between 6 and 7 along both axes): a chest with
    // contents; a hopper below a chest filling the chest east of it; a burning furnace whose
    // hopper carries the ingots into a chest in the east chunk; two observers watching each other
    // across the border, the eastern one firing a dropper with sticks; a sapling; a lever with a
    // slow repeater and a lamp; a torch clock that burns its torch out again and again; a lever
    // holding a trapdoor open (its powered memory); a hopper carrying 64 dirt from a chest across
    // the border into a chest in the east chunk, busy (on its cooldown) the whole run.
    void buildStation()
    {
        put(1, 0, 1, u"chest[facing=south]"_s);
        fill(1, 0, 1, {stack("qtcraft:diamond", 5), stack("qtcraft:gold_ingot", 12)});
        put(9, 1, 1, u"chest[facing=south]"_s);
        fill(9, 1, 1, {stack("qtcraft:cobblestone", 20)});
        put(9, 0, 1, u"signal_hopper[facing=east]"_s);
        put(10, 0, 1, u"chest[facing=south]"_s);

        put(6, 1, 1, u"furnace[facing=south]"_s);
        fill(6, 1, 1, {stack("qtcraft:raw_iron", 6), stack("qtcraft:coal", 2)});
        put(6, 0, 1, u"signal_hopper[facing=east]"_s);
        put(7, 0, 1, u"chest[facing=south]"_s);

        put(5, 0, 5, u"signal_lamp"_s);
        put(6, 0, 5, u"signal_observer[facing=east]"_s);
        put(7, 0, 5, u"signal_observer[facing=west]"_s);
        put(8, 0, 5, u"signal_dropper[facing=south]"_s);
        fill(8, 0, 5, {stack("qtcraft:stick", 6)});

        put(10, 0, 10, u"birch_sapling"_s);
        m_server->plants().saplingPlaced(m_server->blockEntities(), at(10, 0, 10), id("qtcraft:birch_sapling"));
        put(11, 0, 3, u"lever[attach=floor]"_s);
        put(12, 0, 3, u"signal_repeater[facing=east,delay=4]"_s);
        put(13, 0, 3, u"signal_lamp"_s);
        flip(11, 0, 3);

        // The torch's wire runs back into its block: it switches every two ticks and burns out.
        put(1, 0, 8, u"stone"_s);
        put(2, 0, 8, u"stone"_s);
        for (const auto &[x, y, z] : {std::tuple{2, 1, 8}, {3, 0, 8}, {3, 0, 9}, {3, 0, 10}, {2, 0, 10}, {1, 0, 10}, {1, 0, 9}}) {
            put(x, y, z, u"signal_dust"_s);
        }
        put(1, 1, 8, u"signal_torch[attach=floor]"_s);

        put(13, 0, 7, u"oak_trapdoor[facing=north]"_s);
        put(12, 0, 7, u"lever[attach=floor]"_s);
        flip(12, 0, 7);

        put(6, 1, 12, u"chest[facing=south]"_s);
        fill(6, 1, 12, {stack("qtcraft:dirt", 64)});
        put(6, 0, 12, u"signal_hopper[facing=east]"_s);
        put(7, 0, 12, u"chest[facing=south]"_s);
    }

    // The block entities hold a line starting with each of `kinds` (the dump covers them).
    void holdsLinesOf(const QStringList &kinds)
    {
        const QStringList lines = blockEntityLines();
        for (const QString &kind : kinds) {
            QVERIFY2(std::ranges::any_of(lines, [&kind](const QString &line) { return line.startsWith(kind); }), qPrintable(kind));
        }
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
        const QStringList expected = QString::fromUtf8(golden.readAll()).split(u'\n', Qt::SkipEmptyParts);
        const QStringList differences = QcDumpDiff::differences(expected, m_dump);
        for (const QString &line : differences) {
            qWarning().noquote() << line;
        }
        QVERIFY2(differences.isEmpty(), qPrintable(u"the dump differs from %1 in %2 lines (above); see the header of this test to write it again"_s
                                                       .arg(QStringLiteral(QTCRAFT_COMPARE_GOLDEN))
                                                       .arg(differences.size())));
    }

    // The station's chunks leave while the player is away and come back; then the world is saved
    // and opened again. Between the records the station keeps running.
    void stationRunAndReload()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = 1;
        m_keepChangedChunks = false;
        m_unloadChunks = true;
        buildArea(config);
        buildStation();
        // A furnace needs 200 ticks for an ingot.
        idle(240);
        QVERIFY(countIn(7, 0, 1, "qtcraft:iron_ingot") > 0);
        QVERIFY(countIn(10, 0, 1, "qtcraft:cobblestone") > 0);
        QVERIFY(droppedCount(item("qtcraft:stick")) > 0);
        QVERIFY(flag(13, 0, 7, u"open"_s));
        holdsLinesOf({u"cooldown"_s, u"burnt"_s, u"powered"_s});
        recordState(u"station running"_s);
        record(u"station client"_s, clientLines());

        const QList<QcChunkPos> area{at(0, 0, 0).chunk(), at(areaSize - 1, 0, areaSize - 1).chunk(), at(0, 0, areaSize - 1).chunk(),
                                     at(areaSize - 1, 0, 0).chunk()};
        chat(u"/tp %1 %2 %3"_s.arg(m_feet.x + 30 * QcWorld::chunkSize).arg(m_feet.y).arg(m_feet.z));
        idle(1);
        QVERIFY(std::ranges::none_of(area, [this](QcChunkPos chunk) { return m_server->world().chunk(chunk) != nullptr; }));
        idle(50);
        chat(u"/tp %1 %2 %3"_s.arg(m_feet.x).arg(m_feet.y).arg(m_feet.z));
        loadAround(at(areaSize / 2, 0, areaSize / 2), 2);
        idle(40);
        holdsLinesOf({u"cooldown"_s, u"powered"_s});
        recordState(u"station back"_s);

        reopen(config, nullptr);
        record(u"station regions"_s, regionLines(directory.path()));
        recordState(u"station reopened"_s);
        idle(60);
        recordState(u"station reopened and run"_s);
        record(u"station client after reopening"_s, clientLines());
        m_client.reset();
        m_server.reset();
        m_keepChangedChunks = true;
        m_unloadChunks = false;
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

    // A section whose title comes again is compared on its own, in order, not hidden by the first.
    void repeatedTitlesAreComparedInOrder()
    {
        const QStringList expected{u"== a"_s, u"one"_s, u"== a"_s, u"two"_s};
        QVERIFY(QcDumpDiff::differences(expected, expected).isEmpty());
        const QStringList changed{u"== a"_s, u"one"_s, u"== a"_s, u"three"_s};
        QCOMPARE(QcDumpDiff::differences(expected, changed), (QStringList{u"== a #2"_s, u"- two"_s, u"+ three"_s}));
        const QStringList longer{u"== a"_s, u"one"_s, u"== a"_s, u"two"_s, u"== a"_s, u"two"_s};
        QCOMPARE(QcDumpDiff::differences(expected, longer), (QStringList{u"== a #3"_s, u"+ two"_s}));
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
