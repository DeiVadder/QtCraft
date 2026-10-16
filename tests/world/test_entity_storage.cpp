#include "QcBaseContent.h"
#include "QcEntityCodec.h"
#include "QcStoredWorld.h"
#include "QcServer.h"
#include "QcTestTerrainGenerator.h"
#include "QcTestUtil.h"
#include "QcWorldLibrary.h"
#include "QcWorldStorage.h"

#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> baseContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

// The base content with a mod item, a mod creature and a mod block that falls.
std::shared_ptr<const QcContentRegistry> contentWithMod()
{
    QStringList warnings;
    std::shared_ptr<QcContentRegistry> content = *QcBaseContent::load(warnings);
    QcItemDefinition gem = content->item(*content->itemId(u"qtcraft:diamond"_s));
    gem.name = u"mod:gem"_s;
    QcMobDefinition beetle = content->mobs()[*content->mobIndex(u"qtcraft:pig"_s)];
    beetle.name = u"mod:beetle"_s;
    QcBlockDefinition sand = content->block(*content->blockId(u"qtcraft:sand"_s));
    sand.name = u"mod:sand"_s;
    if (!content->registerItem(gem) || !content->registerBlock(sand)) {
        qFatal("cannot register the mod content");
    }
    content->registerMob(beetle);
    return content;
}

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

// Every file below `directory` with its bytes, to prove nothing changed.
QStringList snapshot(const QString &directory)
{
    QStringList files;
    QDirIterator it(directory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (file.open(QIODevice::ReadOnly)) {
            files.append(file.fileName() + QString::fromLatin1(file.readAll().toHex()));
        }
    }
    files.sort();
    return files;
}

} // namespace

// Saving entities per chunk (stages 31.2 and 31.3, decisions E-366, E-373): a blob per chunk in the
// indexed region files of the entities, entries in their table's order, unknown content dropped with
// a warning once per world, damaged and implausible data refused, only changed regions written,
// worlds of the formats before refused.
class TestEntityStorage : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = baseContent();
    std::shared_ptr<const QcContentRegistry> m_mod = contentWithMod();

    QcItemStack stack(const QcContentRegistry &content, const QString &name, int count) const
    {
        return {*content.itemId(name), count, 0};
    }

    QcEntityRecord::Ordered<QcItemEntityManager::Drop> item(const QcContentRegistry &content, const QString &name, const QcVec3 &at,
                                                             quint32 order) const
    {
        return {{stack(content, name, 3), at, {0.5, -1.0, 0.25}, 0.75, 12.5}, order};
    }

    QcEntityRecord::Ordered<QcMobManager::Stored> creature(const QcContentRegistry &content, const QString &name, const QcVec3 &at,
                                                           quint32 order) const
    {
        return {{*content.mobIndex(name), at, 45.0f, 7.5, 120.0, 900.0, true, std::nullopt}, order};
    }

    QcEntityRecord::Ordered<QcFallingBlock> falling(const QcContentRegistry &content, const QString &name, const QcVec3 &at,
                                                    quint32 order) const
    {
        QcFallingBlock block;
        block.block = *content.blockId(name);
        block.motion.position = at;
        block.motion.velocity = {0.0, -4.5, 0.0};
        block.age = 0.6;
        return {block, order};
    }

    QcEntityRecord::Ordered<QcArmourStand> stand(const QcContentRegistry &content, const QcVec3 &at, quint32 order) const
    {
        QcArmourStand stand;
        stand.motion.position = at;
        stand.yaw = 90.0f;
        stand.name = u"Keeper"_s;
        stand.item = u"qtcraft:armor_stand"_s;
        stand.held = stack(content, u"qtcraft:stick"_s, 1);
        stand.equipment.at(QcEquipmentSlot::Chest) = stack(content, u"qtcraft:iron_chestplate"_s, 1);
        return {stand, order};
    }

    struct Records
    {
        QList<QcEntityRecord> records;
        QSet<QcChunkPos> populated;
    };

    // Two chunks in two regions and a populated chunk without entities in a third one.
    Records world(const QcContentRegistry &content) const
    {
        QcEntityRecord near;
        near.chunk = {0, 0};
        near.items = {item(content, u"qtcraft:stick"_s, {1.5, 64.0, 2.5}, 1), item(content, u"qtcraft:diamond"_s, {14.5, 70.25, 3.5}, 0)};
        near.creatures = {creature(content, u"qtcraft:pig"_s, {8.5, 65.0, 8.5}, 0)};
        near.fallingBlocks = {falling(content, u"qtcraft:sand"_s, {3.5, 71.25, 9.5}, 0)};
        QcEntityRecord far;
        far.chunk = {40, -3};
        far.items = {item(content, u"qtcraft:stick"_s, {645.5, 64.0, -40.5}, 2)};
        far.stands = {stand(content, {644.5, 63.0, -44.5}, 0)};
        return {{near, far}, {{0, 0}, {40, -3}, {-100, 5}}};
    }

    static QStringList regionFiles(const QString &directory)
    {
        return QDir(QDir(directory).filePath(u"entities"_s)).entryList({u"r.*.qen"_s}, QDir::Files, QDir::Name);
    }

    static const QcEntityRecord *recordOf(const QList<QcEntityRecord> &records, QcChunkPos chunk)
    {
        const auto it = std::ranges::find(records, chunk, &QcEntityRecord::chunk);
        return it == records.end() ? nullptr : &*it;
    }

    // Hands the records to a store of `directory` (with `populated`) and commits them.
    static QcStatus write(QcChunkStore &store, const QcContentRegistry &content, const Records &entities)
    {
        QSet<QcChunkPos> chunks = entities.populated;
        for (const QcEntityRecord &record : entities.records) {
            chunks.insert(record.chunk);
        }
        for (const QcChunkPos &chunk : std::as_const(chunks)) {
            QcEntityRecord record;
            if (const QcEntityRecord *found = recordOf(entities.records, chunk)) {
                record = *found;
            }
            record.chunk = chunk;
            record.populated = entities.populated.contains(chunk);
            store.put(QcChunkStore::Kind::Entities, chunk, QcEntityCodec::encode(record, content));
        }
        return store.commit({});
    }

    static QcStatus write(const QString &directory, const QcContentRegistry &content, const Records &entities)
    {
        QcChunkStore store(directory);
        if (const QcStatus opened = store.open(); !opened) {
            return opened;
        }
        return write(store, content, entities);
    }

    static QcResult<QcStoredWorld> read(const QString &directory, const QcContentRegistry &content)
    {
        return QcStoredWorld::read(directory, content);
    }

    // Writes `entities` and expects reading them back to fail.
    void expectRefused(const Records &entities, const char *what)
    {
        QTemporaryDir dir;
        QVERIFY(write(dir.path(), *m_content, entities).has_value());
        const bool read = QcStoredWorld::read(dir.path(), *m_content).has_value();
        QVERIFY2(!read, what);
    }

private slots:
    void recordsRoundTripPerRegionInTheirOrder()
    {
        QTemporaryDir dir;
        // Nothing saved yet: no records, no folder, no error.
        const auto empty = read(dir.path(), *m_content);
        QVERIFY(empty.has_value() && empty->entities.isEmpty() && empty->populated.isEmpty());
        QVERIFY(write(dir.path(), *m_content, {}).has_value());
        QVERIFY(!QDir(dir.filePath(u"entities"_s)).exists());

        const Records saved = world(*m_content);
        QVERIFY(write(dir.path(), *m_content, saved).has_value());
        QCOMPARE(regionFiles(dir.path()), (QStringList{u"r.-4.0.qen"_s, u"r.0.0.qen"_s, u"r.1.-1.qen"_s}));

        const auto loaded = read(dir.path(), *m_content);
        QVERIFY2(loaded.has_value(), qcErrorText(loaded).constData());
        QVERIFY2(loaded->warnings.isEmpty(), qPrintable(loaded->warnings.join(u'\n')));
        QCOMPARE(loaded->populated, saved.populated);
        QCOMPARE(loaded->entities.size(), 2);
        for (const QcEntityRecord &expected : saved.records) {
            const QcEntityRecord *record = recordOf(loaded->entities, expected.chunk);
            QVERIFY(record);
            QCOMPARE(record->items.size(), expected.items.size());
            for (qsizetype i = 0; i < expected.items.size(); ++i) {
                QVERIFY(record->items[i].entry == expected.items[i].entry);
                QCOMPARE(record->items[i].order, expected.items[i].order);
            }
            QCOMPARE(record->creatures.size(), expected.creatures.size());
            for (qsizetype i = 0; i < expected.creatures.size(); ++i) {
                QVERIFY(record->creatures[i].entry == expected.creatures[i].entry);
            }
            QCOMPARE(record->fallingBlocks.size(), expected.fallingBlocks.size());
            for (qsizetype i = 0; i < expected.fallingBlocks.size(); ++i) {
                QCOMPARE(record->fallingBlocks[i].entry.block, expected.fallingBlocks[i].entry.block);
                QCOMPARE(record->fallingBlocks[i].entry.motion, expected.fallingBlocks[i].entry.motion);
                QCOMPARE(record->fallingBlocks[i].entry.age, expected.fallingBlocks[i].entry.age);
            }
            QCOMPARE(record->stands.size(), expected.stands.size());
            for (qsizetype i = 0; i < expected.stands.size(); ++i) {
                QCOMPARE(record->stands[i].entry, expected.stands[i].entry);
            }
        }
    }

    void unknownContentIsDroppedWithAWarning()
    {
        // Saved with a mod, opened without it: its item, creature and falling block are left out,
        // each name reported once per world; the rest comes back.
        QTemporaryDir dir;
        Records saved = world(*m_mod);
        QcEntityRecord &near = saved.records.first();
        near.items.append(item(*m_mod, u"mod:gem"_s, {2.5, 64.0, 2.5}, 3));
        near.items.append(item(*m_mod, u"mod:gem"_s, {3.5, 64.0, 2.5}, 4));
        near.creatures.append(creature(*m_mod, u"mod:beetle"_s, {5.5, 65.0, 5.5}, 1));
        near.fallingBlocks.append(falling(*m_mod, u"mod:sand"_s, {6.5, 80.0, 6.5}, 1));
        // The same names in more chunks, of this region and of others, are not reported again.
        for (const QcChunkPos chunk : {QcChunkPos{1, 0}, QcChunkPos{40, 3}, QcChunkPos{-70, -70}}) {
            QcEntityRecord more;
            more.chunk = chunk;
            const QcVec3 corner(chunk.x * QcWorld::chunkSize + 2.5, 64.0, chunk.z * QcWorld::chunkSize + 2.5);
            more.items.append(item(*m_mod, u"mod:gem"_s, corner, 1));
            more.creatures.append(creature(*m_mod, u"mod:beetle"_s, corner + QcVec3(1.0, 1.0, 1.0), 1));
            saved.records.append(more);
        }
        QVERIFY(write(dir.path(), *m_mod, saved).has_value());

        const auto loaded = read(dir.path(), *m_content);
        QVERIFY2(loaded.has_value(), qcErrorText(loaded).constData());
        const QStringList warnings = loaded->warnings;
        const QcEntityRecord *record = recordOf(loaded->entities, {0, 0});
        QCOMPARE(record->items.size(), 2);
        QCOMPARE(record->creatures.size(), 1);
        QCOMPARE(record->fallingBlocks.size(), 1);
        QCOMPARE(warnings.size(), 3);
        QVERIFY(warnings.join(u'\n').contains(u"mod:gem"_s));
        QVERIFY(warnings.join(u'\n').contains(u"mod:beetle"_s));
        QVERIFY(warnings.join(u'\n').contains(u"mod:sand"_s));
    }

    void damagedAndImplausibleFilesAreRefused()
    {
        // A flipped byte in a chunk's data fails its checksum.
        {
            QTemporaryDir dir;
            QVERIFY(write(dir.path(), *m_content, world(*m_content)).has_value());
            QFile file(QDir(dir.filePath(u"entities"_s)).filePath(u"r.0.0.qen"_s));
            QVERIFY(file.open(QIODevice::ReadWrite));
            file.seek(file.size() - 3);
            file.write("\x7f");
            file.close();
            QVERIFY(!read(dir.path(), *m_content).has_value());
        }

        // An entity outside the chunk of its record.
        Records outside = world(*m_content);
        outside.records.first().items.first().entry.position.x = 17.5;
        expectRefused(outside, "item in another chunk");

        // Values that cannot be.
        Records notANumber = world(*m_content);
        notANumber.records.first().creatures.first().entry.position.y = std::numeric_limits<double>::quiet_NaN();
        expectRefused(notANumber, "creature at NaN");
        Records timer = world(*m_content);
        timer.records.first().creatures.first().entry.growUp = 1e9;
        expectRefused(timer, "growing up for 30 years");
        Records standBelow = world(*m_content);
        standBelow.records.last().stands.first().entry.motion.position.y = -500.0;
        expectRefused(standBelow, "stand far below the void");

        // A file under the name of another region: its entities lie outside the chunks they are read for.
        QTemporaryDir dir;
        QVERIFY(write(dir.path(), *m_content, world(*m_content)).has_value());
        const QDir folder(dir.filePath(u"entities"_s));
        QVERIFY(QFile::rename(folder.filePath(u"r.1.-1.qen"_s), folder.filePath(u"r.2.-1.qen"_s)));
        QVERIFY(!read(dir.path(), *m_content).has_value());

        // A damaged index makes the store refuse to open.
        QFile index(folder.filePath(u"r.0.0.qen"_s));
        QVERIFY(index.open(QIODevice::ReadWrite));
        index.seek(15);
        index.write("\x7f");
        index.close();
        QcChunkStore store(dir.path());
        QVERIFY(!store.open().has_value());
        QVERIFY(store.damaged().has_value());
        QCOMPARE(store.damaged()->file, u"entities/r.0.0.qen"_s);
    }

    void valuesBeyondTheirRangeAreClampedAndStacksSplit()
    {
        QTemporaryDir dir;
        Records saved = world(*m_content);
        QcItemEntityManager::Drop &drop = saved.records.first().items.first().entry;
        drop.stack.count = 150;
        drop.velocity = {1000.0, 0.0, -1000.0};
        drop.pickupDelay = 99.0;
        drop.age = 1e6;
        QVERIFY(write(dir.path(), *m_content, saved).has_value());

        const auto loaded = read(dir.path(), *m_content);
        QVERIFY2(loaded.has_value(), qcErrorText(loaded).constData());
        const QcEntityRecord *record = recordOf(loaded->entities, {0, 0});
        // 150 sticks come back as three stacks of at most 64, all with the order of the saved one.
        QCOMPARE(record->items.size(), 4);
        QCOMPARE(record->items[0].entry.stack.count, 64);
        QCOMPARE(record->items[1].entry.stack.count, 64);
        QCOMPARE(record->items[2].entry.stack.count, 22);
        QCOMPARE(record->items[2].order, record->items[0].order);
        QCOMPARE(record->items[0].entry.velocity, (QcVec3{60.0, 0.0, -60.0}));
        QCOMPARE(record->items[0].entry.pickupDelay, QcItemEntityManager::thrownPickupDelay);
        QCOMPARE(record->items[0].entry.age, QcItemEntityManager::despawnSeconds);
    }

    void onlyChangedRegionsAreWrittenAndEmptyOnesLoseTheirFile()
    {
        QTemporaryDir dir;
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        Records saved = world(*m_content);
        QVERIFY(write(store, *m_content, saved).has_value());

        // An unchanged region keeps its file as it is (here: garbage nobody reads), a changed one is
        // written again; unchanged chunks handed over again count as unchanged.
        const QDir folder(dir.filePath(u"entities"_s));
        QFile near(folder.filePath(u"r.0.0.qen"_s));
        QVERIFY(near.open(QIODevice::WriteOnly | QIODevice::Truncate));
        near.write("untouched");
        near.close();
        const QByteArray farBefore = [&folder] {
            QFile file(folder.filePath(u"r.1.-1.qen"_s));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        }();
        saved.records.last().stands.first().entry.yaw = 180.0f;
        store.put(QcChunkStore::Kind::Entities, {0, 0}, QcEntityCodec::encode([&] {
            QcEntityRecord near = saved.records.first();
            near.populated = true;
            return near;
        }(), *m_content));
        QVERIFY(write(store, *m_content, {{saved.records.last()}, {{40, -3}}}).has_value());
        QVERIFY(near.open(QIODevice::ReadOnly));
        QCOMPARE(near.readAll(), QByteArray("untouched"));
        near.close();
        QFile far(folder.filePath(u"r.1.-1.qen"_s));
        QVERIFY(far.open(QIODevice::ReadOnly));
        QVERIFY(far.readAll() != farBefore);
        far.close();

        // A region without entities and without populated chunks loses its file.
        store.put(QcChunkStore::Kind::Entities, {40, -3}, std::nullopt);
        QVERIFY(store.commit({}).has_value());
        QCOMPARE(regionFiles(dir.path()), (QStringList{u"r.-4.0.qen"_s, u"r.0.0.qen"_s}));
        QVERIFY(folder.entryList({u"*.next"_s}, QDir::Files).isEmpty());
    }

    void worldOfTheFormatBeforeChunkUnloadingIsRefused()
    {
        // A world of format 4 (stage 31.2: region files without an index, everything read at the
        // start) is refused before anything is read or written (decision E-373); the list shows it as
        // too old.
        QTemporaryDir root;
        const QString directory = root.filePath(u"Format4"_s);
        QVERIFY(QDir().mkpath(QDir(directory).filePath(u"regions"_s)));
        QFile info(QDir(directory).filePath(u"world.json"_s));
        QVERIFY(info.open(QIODevice::WriteOnly));
        info.write(R"({"formatVersion": 4, "name": "Format4", "seed": "42", "generator": {"type": "terrain"}})");
        info.close();
        QFile region(QDir(directory).filePath(u"regions/r.0.0.qcr"_s));
        QVERIFY(region.open(QIODevice::WriteOnly));
        region.write("QCRG old region data");
        region.close();
        const QStringList before = snapshot(directory);

        QcServerConfig config;
        config.worldDirectory = directory;
        config.seed = 42;
        {
            QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            const QcStatus opened = server.openWorld();
            QVERIFY(!opened.has_value());
            QVERIFY(server.worldTooOld());
            QVERIFY2(opened.error().contains(u"older development version"_s), qPrintable(opened.error()));
            QCOMPARE(server.world().loadedChunkCount(), 0);
        }
        QCOMPARE(snapshot(directory), before);
        const QList<QcWorldLibrary::Entry> entries = QcWorldLibrary(root.path()).list();
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().tooOld);
    }

    void worldOfTheFormatBeforeTheEntityModelIsRefused()
    {
        // A world saved by the code before stage 31.2 (tests/data/world-before-31-2, written by the
        // first version of test_entity_compare): entities in entities.qce, mobs.qce and stands.qce.
        // It is refused with a clear message, nothing of it is loaded and no file changes (decision
        // E-356), and the world list shows it as too old.
        QTemporaryDir root;
        const QString directory = root.filePath(u"Old"_s);
        QVERIFY(copyTree(QStringLiteral(QTCRAFT_OLD_WORLD), directory));
        const QStringList before = snapshot(directory);

        QcServerConfig config;
        config.worldDirectory = directory;
        {
            QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            const QcStatus opened = server.openWorld();
            QVERIFY(!opened.has_value());
            QVERIFY(server.worldTooOld());
            QVERIFY2(opened.error().contains(u"older development version"_s), qPrintable(opened.error()));
            QVERIFY(server.mobs().mobs().isEmpty());
            QVERIFY(server.items().items().isEmpty());
            QVERIFY(server.stands().stands().isEmpty());
            QCOMPARE(server.world().loadedChunkCount(), 0);
        }
        QCOMPARE(snapshot(directory), before);

        const QList<QcWorldLibrary::Entry> entries = QcWorldLibrary(root.path()).list();
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().tooOld);
        QVERIFY(!entries.first().error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestEntityStorage)
#include "test_entity_storage.moc"
