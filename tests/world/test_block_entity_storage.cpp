#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "QcTestWorldFiles.h"
#include "server/blockentities/QcBlockEntityChecks.h"
#include "server/signal/QcCircuitState.h"
#include "server/signal/QcHopperQueue.h"
#include "server/storage/QcBlockEntityRecords.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QDirIterator>
#include <QTemporaryDir>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

// The base content with a mod item and a small mod chest.
std::shared_ptr<const QcContentRegistry> contentWithMod()
{
    QStringList warnings;
    std::shared_ptr<QcContentRegistry> content = *QcBaseContent::load(warnings);
    QcItemDefinition gem = content->item(*content->itemId(u"qtcraft:diamond"_s));
    gem.name = u"mod:gem"_s;
    QcBlockDefinition crate = content->block(*content->blockId(u"qtcraft:barrel"_s));
    crate.name = u"mod:crate"_s;
    crate.containerSlots = 9;
    if (!content->registerItem(gem) || !content->registerBlock(crate)) {
        // Builds content for a member initialiser, outside any test case; no case can run without it.
        qFatal("cannot register the mod content");
    }
    return content;
}

// Copies a directory tree.
bool copyTree(const QString &from, const QString &to)
{
    QDirIterator files(from, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString source = files.next();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(source));
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(source, target)) {
            return false;
        }
    }
    return true;
}

} // namespace

// Saving block entities per chunk (stage 31.1, decision E-358): records with ticks counted from the
// save, one file per region written only when it changed, damaged and implausible files refused,
// worlds saved before the register refused (decision E-356), blocks of removed mods spilling their
// contents.
class TestBlockEntityStorage : public QcServerTestFixture
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_mod = contentWithMod();

    static QString regionFile(const QString &world, QcChunkPos region)
    {
        return QDir(world).filePath(u"blockentities/r.%1.%2.qcb"_s.arg(region.x).arg(region.z));
    }

    // A world of block entities at tick 100: two containers (one with a hopper's cooldown and a mod
    // item, one empty), a working and an idle machine, a sapling, memories, scheduled ticks and
    // positions the circuits wait for.
    void fillWorld(QcBlockEntities &entities, QcCircuitState &circuits) const
    {
        QcBlockEntities::Container chest{{4, 70, -9}, QList<QcItemStack>(27), 103};
        chest.stacks[0] = {*m_mod->itemId(u"qtcraft:stone"_s), 64, 0};
        chest.stacks[26] = {*m_mod->itemId(u"mod:gem"_s), 3, 0};
        QVERIFY(entities.putContainer(chest));
        QVERIFY(entities.putContainer({{5, 70, -9}, QList<QcItemStack>(5), 0}));

        QcBlockEntities::Machine furnace;
        furnace.pos = {-12, 64, 7};
        furnace.stacks = {QcItemStack{*m_mod->itemId(u"qtcraft:iron_ore"_s), 9, 0}, QcItemStack{*m_mod->itemId(u"qtcraft:coal"_s), 2, 0},
                          QcItemStack{}};
        furnace.burnRemaining = 12.5;
        furnace.burnSeconds = 80.0;
        furnace.progress = 3.25;
        QVERIFY(entities.putMachine(furnace));
        QcBlockEntities::Machine idle;
        idle.pos = {-12, 64, 9};
        QVERIFY(entities.putMachine(idle));

        QVERIFY(entities.putSapling({{7, 65, -9}, 33.5, 4}));
        entities.memoryAt({5, 66, 5}).burntOutUntil = 200;
        entities.memoryAt({6, 64, 6}).powered = true;
        entities.memoryAt({7, 64, 7}).extendingSince = 99;
        entities.schedule().add({1, 64, 2}, 105, QcSignalSchedule::Priority::Diode);
        entities.schedule().add({-3, 70, 9}, 260, QcSignalSchedule::Priority::Release);
        circuits.changes.append({{4, 65, -4}, qcAirBlockId, qcAirBlockId});
        circuits.waiting[QcChunkPos{100, 100}].append({1601, 66, 1602});
    }

private slots:
    void recordsKeepTicksLeftThroughAFile()
    {
        QTemporaryDir dir;
        QcBlockEntities saved(m_mod);
        QcCircuitState circuits;
        fillWorld(saved, circuits);
        const QcTestWorldFiles storage(dir.path(), m_mod);
        QVERIFY(storage.writeBlockEntities(QcBlockEntityRecords::collect(saved, circuits, 100)).has_value());
        // One file per region with records.
        for (const QcChunkPos region : {QcChunkPos{0, -1}, QcChunkPos{-1, 0}, QcChunkPos{0, 0}, QcChunkPos{-1, 0}, QcChunkPos{3, 3}}) {
            QVERIFY2(QFile::exists(regionFile(dir.path(), region)), qPrintable(regionFile(dir.path(), region)));
        }

        // Read without the mod at another tick: what waits keeps the ticks it still had.
        QStringList warnings;
        const QcTestWorldFiles withoutMod(dir.path(), m_content);
        const auto records = withoutMod.readBlockEntities(warnings);
        QVERIFY2(records.has_value(), qcErrorText(records).constData());
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(u"mod:gem"_s));
        QcBlockEntities loaded(m_content);
        QcCircuitState loadedCircuits;
        QcHopperQueue hoppers;
        QcBlockEntityRecords::apply(*records, loaded, loadedCircuits, hoppers, 1000);

        const QcBlockEntities::Container *chest = loaded.container({4, 70, -9});
        QVERIFY(chest);
        QCOMPARE(chest->stacks[0], (QcItemStack{*m_content->itemId(u"qtcraft:stone"_s), 64, 0}));
        QVERIFY(chest->stacks[26].isEmpty());
        QCOMPARE(chest->readyAt, quint64(1003));
        QCOMPARE(hoppers.timers.begin()->first, quint64(1003));
        // Empty containers and idle machines are not kept: they come back with their block.
        QVERIFY(!loaded.container({5, 70, -9}));
        QVERIFY(!loaded.machine({-12, 64, 9}));
        const QcBlockEntities::Machine *furnace = loaded.machine({-12, 64, 7});
        QVERIFY(furnace);
        QCOMPARE(furnace->burnRemaining, 12.5);
        QCOMPARE(furnace->burnSeconds, 80.0);
        QCOMPARE(furnace->progress, 3.25);
        QCOMPARE(furnace->stacks[1].count, 2);
        QCOMPARE(loaded.sapling({7, 65, -9})->remaining, 33.5);
        QCOMPARE(loaded.sapling({7, 65, -9})->order, quint64(4));
        QCOMPARE(loaded.memory({5, 66, 5})->burntOutUntil, quint64(1100));
        QVERIFY(loaded.memory({6, 64, 6})->powered);
        QCOMPARE(*loaded.memory({7, 64, 7})->extendingSince, quint64(999));
        const QList<QcSignalSchedule::Entry> entries = loaded.schedule().entries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].pos, (QcBlockPos{1, 64, 2}));
        QCOMPARE(entries[0].due, quint64(1005));
        QCOMPARE(entries[1].due, quint64(1160));
        QCOMPARE(entries[1].priority, QcSignalSchedule::Priority::Release);
        // The positions the circuits were going to look at are changes again (stage 31.3).
        QCOMPARE(loadedCircuits.changes.size(), 2);
        QVERIFY(std::ranges::any_of(loadedCircuits.changes, [](const QcCircuitState::Change &change) { return change.pos == QcBlockPos{4, 65, -4}; }));
        QVERIFY(std::ranges::any_of(loadedCircuits.changes, [](const QcCircuitState::Change &change) { return change.pos == QcBlockPos{1601, 66, 1602}; }));
    }

    void onlyChangedRegionsAreWrittenAndEmptyOnesGo()
    {
        QTemporaryDir dir;
        QcBlockEntities entities(m_mod);
        QcCircuitState circuits;
        fillWorld(entities, circuits);
        const QcTestWorldFiles storage(dir.path(), m_mod);
        QVERIFY(storage.writeBlockEntities(QcBlockEntityRecords::collect(entities, circuits, 100)).has_value());

        // Unchanged regions are not written again (the removed file stays away), a changed one is.
        const QString furnaces = regionFile(dir.path(), {-1, 0});
        const QString saplings = regionFile(dir.path(), {0, -1});
        QVERIFY(QFile::remove(furnaces));
        entities.setSaplingRemaining({7, 65, -9}, 20.0);
        QVERIFY(storage.writeBlockEntities(QcBlockEntityRecords::collect(entities, circuits, 100)).has_value());
        QVERIFY(!QFile::exists(furnaces));
        QVERIFY(QFile::exists(saplings));
        QStringList warnings;
        const auto read = QcTestWorldFiles(dir.path(), m_mod).readBlockEntities(warnings);
        QVERIFY(read.has_value());
        const auto sapling = std::ranges::find_if(*read, [](const QcBlockEntityRecord &record) { return !record.saplings.isEmpty(); });
        QCOMPARE(sapling->saplings.first().remaining, 20.0);

        // Without records a region's file goes.
        QVERIFY(storage.writeBlockEntities({}).has_value());
        QVERIFY(QDir(dir.filePath(u"blockentities"_s)).entryList(QDir::Files).isEmpty());
    }

    void damagedFilesFailWithAMessage_data()
    {
        QTest::addColumn<int>("cut");
        QTest::addColumn<int>("flipAt");
        QTest::newRow("truncated") << 6 << -1;
        QTest::newRow("only the header") << -1 << 14;
        QTest::newRow("bit flip") << 0 << 20;
    }

    void damagedFilesFailWithAMessage()
    {
        QFETCH(int, cut);
        QFETCH(int, flipAt);

        QTemporaryDir dir;
        QcBlockEntities entities(m_mod);
        QcCircuitState circuits;
        fillWorld(entities, circuits);
        QVERIFY(QcTestWorldFiles(dir.path(), m_mod).writeBlockEntities(QcBlockEntityRecords::collect(entities, circuits, 100)).has_value());

        const QString path = regionFile(dir.path(), {-1, 0});
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray data = file.readAll();
        if (cut > 0) {
            data.chop(cut);
        } else if (cut < 0) {
            data = data.left(flipAt);
        } else {
            data[flipAt] = char(data[flipAt] ^ 0x40);
        }
        file.resize(0);
        file.seek(0);
        file.write(data);
        file.close();

        QStringList warnings;
        const QcTestWorldFiles storage(dir.path(), m_mod);
        const auto read = storage.readBlockEntities(warnings);
        QVERIFY(!read.has_value());
        QVERIFY2(read.error().contains(path), qPrintable(read.error()));
        // Named relative to the world folder for the menu entry (decision E-366).
        QCOMPARE(storage.damagedFile(), QDir(dir.path()).relativeFilePath(path));
        QVERIFY(QFile::remove(path));
        QVERIFY(storage.readBlockEntities(warnings).has_value());
        QVERIFY(storage.damagedFile().isEmpty());
    }

    void implausibleRecordsAreRejected_data()
    {
        QTest::addColumn<int>("damage");
        QTest::newRow("two containers at one position") << 0;
        QTest::newRow("a machine outside its chunk") << 1;
        QTest::newRow("endless progress") << 2;
        QTest::newRow("a scheduled tick of an unknown priority") << 3;
    }

    void implausibleRecordsAreRejected()
    {
        QFETCH(int, damage);
        QTemporaryDir dir;
        QcBlockEntityRecord record;
        record.chunk = {0, 0};
        QcBlockEntities::Machine machine;
        machine.pos = {1, 64, 1};
        machine.stacks[0] = {*m_mod->itemId(u"qtcraft:stone"_s), 1, 0};
        switch (damage) {
        case 0:
            record.containers = {{{2, 64, 2}, QList<QcItemStack>(3), 0}, {{2, 64, 2}, QList<QcItemStack>(3), 0}};
            break;
        case 1:
            machine.pos = {17, 64, 1};
            break;
        case 2:
            machine.progress = std::numeric_limits<double>::infinity();
            break;
        default:
            record.scheduled = {{{3, 64, 3}, 5, QcSignalSchedule::Priority(7)}};
            break;
        }
        record.machines = {machine};
        const QcTestWorldFiles storage(dir.path(), m_mod);
        QVERIFY(storage.writeBlockEntities({record}).has_value());
        QStringList warnings;
        QVERIFY(!storage.readBlockEntities(warnings).has_value());
    }

    void outOfRangeValuesAreClamped()
    {
        QTemporaryDir dir;
        QcBlockEntityRecord record;
        record.chunk = {0, 0};
        QcBlockEntities::Machine machine;
        machine.pos = {1, 64, 1};
        machine.progress = -5.0;
        machine.burnSeconds = 10.0;
        machine.burnRemaining = 50.0;
        machine.stacks[0] = {*m_mod->itemId(u"qtcraft:stone"_s), 500, 0};
        record.machines = {machine};
        const QcTestWorldFiles storage(dir.path(), m_mod);
        QVERIFY(storage.writeBlockEntities({record}).has_value());
        QStringList warnings;
        const auto read = storage.readBlockEntities(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        const QcBlockEntities::Machine &loaded = read->first().machines.first();
        QCOMPARE(loaded.progress, 0.0);
        QCOMPARE(loaded.burnRemaining, 10.0);
        QCOMPARE(loaded.stacks[0].count, 64);
    }

    void damagedTicksAreCutAndHugeOrdersRefused()
    {
        // Waits beyond 2^20 ticks are damaged values and cut; a hopper's cooldown never exceeds the
        // hoppers' own; a planting order near the top of its range would let later orders wrap.
        QTemporaryDir dir;
        QcBlockEntityRecord record;
        record.chunk = {0, 0};
        record.containers = {{{1, 64, 1}, QList<QcItemStack>(5), 4'000'000'000u}};
        record.memories = {{{2, 64, 2}, 4'000'000'000u, false, 4'000'000'000u}};
        record.scheduled = {{{3, 64, 3}, 4'000'000'000u, QcSignalSchedule::Priority::Diode}};
        record.saplings = {{{4, 64, 4}, 10.0, 7}};
        const QcTestWorldFiles storage(dir.path(), m_mod);
        QVERIFY(storage.writeBlockEntities({record}).has_value());
        QStringList warnings;
        const auto read = storage.readBlockEntities(warnings);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        const QcBlockEntityRecord &loaded = read->first();
        QCOMPARE(loaded.containers.first().readyAt, quint64(1) << 20);
        QCOMPARE(loaded.memories.first().burntOutLeft, 1u << 20);
        QCOMPARE(*loaded.memories.first().extendingAge, 1u << 20);
        QCOMPARE(loaded.scheduled.first().ticksLeft, 1u << 20);

        QcBlockEntities entities(m_mod);
        QcCircuitState circuits;
        QcHopperQueue hoppers;
        hoppers.cooldownTicks = 8;
        QVERIFY(QcBlockEntityRecords::apply(*read, entities, circuits, hoppers, 100).isEmpty());
        QCOMPARE(entities.container({1, 64, 1})->readyAt, quint64(108));

        record.saplings = {{{4, 64, 4}, 10.0, quint64(1) << 63}};
        QVERIFY(storage.writeBlockEntities({record}).has_value());
        QVERIFY(!storage.readBlockEntities(warnings).has_value());
    }

    void aSaveThatBreaksOffKeepsThePreviousFiles()
    {
        // Changed regions are written under staging names first: when one of them cannot be
        // written, no region file is replaced (decision E-358).
        QTemporaryDir dir;
        QcBlockEntities entities(m_mod);
        QcCircuitState circuits;
        fillWorld(entities, circuits);
        const QcTestWorldFiles storage(dir.path(), m_mod);
        QVERIFY(storage.writeBlockEntities(QcBlockEntityRecords::collect(entities, circuits, 100)).has_value());
        QFile chests(regionFile(dir.path(), {0, -1}));
        QVERIFY(chests.open(QIODevice::ReadOnly));
        const QByteArray before = chests.readAll();
        chests.close();

        entities.container({4, 70, -9})->stacks[1] = {*m_mod->itemId(u"qtcraft:stick"_s), 3, 0};
        entities.machine({-12, 64, 7})->progress = 5.0;
        QVERIFY(QDir().mkpath(regionFile(dir.path(), {-1, 0}) + u".next"_s + u"/blocked"_s));
        // The stale staging directory is removed only if it is a file; here it blocks the write.
        const QcStatus failed = storage.writeBlockEntities(QcBlockEntityRecords::collect(entities, circuits, 100));
        QVERIFY(!failed.has_value());
        QVERIFY(chests.open(QIODevice::ReadOnly));
        QCOMPARE(chests.readAll(), before);
        chests.close();

        QVERIFY(QDir(regionFile(dir.path(), {-1, 0}) + u".next"_s).removeRecursively());
        QVERIFY(storage.writeBlockEntities(QcBlockEntityRecords::collect(entities, circuits, 100)).has_value());
        QVERIFY(chests.open(QIODevice::ReadOnly));
        QVERIFY(chests.readAll() != before);
        QVERIFY(QDir(dir.filePath(u"blockentities"_s)).entryList({u"*.next"_s}, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }

    void savedEntriesComeBackBeyondALoweredLimit()
    {
        // The limits refuse placing, never what a save holds (here: 20 filled chests, the limit 16).
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        for (int i = 0; i < 20; ++i) {
            const QcBlockPos chest = m_feet.offset(i - 10, 5, 3);
            QVERIFY(m_server->world().setBlock(chest, id("qtcraft:chest")));
            m_server->blockEntities().container(chest)->stacks[0] = {item("qtcraft:diamond"), i + 1, 0};
        }
        QVERIFY(m_server->save().has_value());
        m_client.reset();
        m_server.reset();

        // As a dedicated server's server.json would set it (a world's own limits come from world.json).
        config.limits.maxContainers = 16;
        config.limitsFromServer = true;
        join(config);
        QCOMPARE(m_server->blockEntities().limits().containers, 16);
        QVERIFY(m_server->blockEntities().isContainerTableFull());
        int diamonds = 0;
        for (int i = 0; i < 20; ++i) {
            const QcBlockEntities::Container *chest = m_server->blockEntities().container(m_feet.offset(i - 10, 5, 3));
            QVERIFY(chest);
            diamonds += chest->stacks[0].count;
        }
        QCOMPARE(diamonds, 20 * 21 / 2);
        QVERIFY(m_server->openWarnings().isEmpty());
        m_client.reset();
        m_server.reset();
    }

    void worldOfAnOlderVersionIsRefused()
    {
        // A flat world saved by the code before stage 31.1 (tests/data/world-before-31-1, written by
        // the first version of test_block_entity_compare): block entities in machines.qce,
        // containers.qce and signals.qce, saplings in entities.qce. It is refused with a clear
        // message, nothing of it is loaded and no file changes (decision E-356).
        QTemporaryDir dir;
        QVERIFY(copyTree(QStringLiteral(QTCRAFT_OLD_WORLD), dir.path()));
        QStringList before;
        QDirIterator files(dir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            QFile file(files.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            before.append(file.fileName() + QString::fromLatin1(file.readAll().toHex()));
        }
        before.sort();

        QcServerConfig config;
        config.generator = u"flat"_s;
        config.generatorOptions = u"bedrock,2*stone,grass_block"_s;
        config.seed = 7;
        config.worldDirectory = dir.path();
        {
            QcServer server(config, m_content, QcWorldGenerators::create(u"flat"_s, m_content, 7, nullptr, config.generatorOptions));
            const QcStatus opened = server.openWorld();
            QVERIFY(!opened.has_value());
            QVERIFY(server.worldTooOld());
            QVERIFY2(opened.error().contains(u"older development version"_s), qPrintable(opened.error()));
            QCOMPARE(server.blockEntities().containerCount(), 0);
            QCOMPARE(server.world().loadedChunkCount(), 0);
        }

        QStringList after;
        QDirIterator again(dir.path(), QDir::Files, QDirIterator::Subdirectories);
        while (again.hasNext()) {
            QFile file(again.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            after.append(file.fileName() + QString::fromLatin1(file.readAll().toHex()));
        }
        after.sort();
        QCOMPARE(after, before);
    }

    void blocksOfARemovedModSpillTheirContents()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_content = m_mod;
        join(config);
        const QcBlockPos crate = m_feet.offset(2, 0, 0);
        QVERIFY(m_server->world().setBlock(crate, *m_mod->blockId(u"mod:crate"_s)));
        QCOMPARE(m_server->blockEntities().container(crate)->stacks.size(), 9);
        m_server->blockEntities().container(crate)->stacks[4] = {*m_mod->itemId(u"qtcraft:stick"_s), 12, 0};
        QVERIFY(m_server->save().has_value());
        m_client.reset();
        m_server.reset();

        // Opened without the mod: the crate is air, its sticks lie where it stood.
        m_content = QcTestUtil::loadContent();
        join(config);
        QVERIFY(!m_server->blockEntities().container(crate));
        QCOMPARE(droppedCount(item("qtcraft:stick")), 12);
        const QStringList problems = QcBlockEntityChecks::problems(m_server->world());
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u"; "_s)));
        m_client.reset();
        m_server.reset();
    }
};

QTEST_GUILESS_MAIN(TestBlockEntityStorage)
#include "test_block_entity_storage.moc"
