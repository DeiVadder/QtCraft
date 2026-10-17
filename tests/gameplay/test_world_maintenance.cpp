#include "QcServerTestFixture.h"
#include "server/commands/QcWorldBackup.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// Repair and maintenance commands (decisions E-245, E-246): regenerating chunks of every world type,
// removing entities, backups and the world's numbers.
class TestWorldMaintenance : public QcServerTestFixture
{
    Q_OBJECT

private:
    QString lastLine() const { return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.constLast(); }

    // /backup copies in the background (decision E-256): ticks until the copy reported back.
    void backup(const QString &command)
    {
        chat(command);
        for (int i = 0; i < 400 && lastLine().startsWith(u"Backup of "_s); ++i) {
            QTest::qWait(5);
            ticks(1);
        }
    }

    // Whether the loaded chunk holds exactly what `generator` makes of it.
    bool asGenerated(QcChunkPos pos, const QcWorldGenerator &generator) const
    {
        const std::shared_ptr<const QcChunk> loaded = m_server->world().chunk(pos);
        const std::shared_ptr<QcChunk> fresh = generator.generate(pos);
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    if (loaded->block(x, y, z) != fresh->block(x, y, z)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    QcServerConfig typed(const QString &type, const QString &directory = {}) const
    {
        QcServerConfig config;
        config.generator = type;
        config.seed = 7;
        config.worldDirectory = directory;
        return config;
    }

    std::unique_ptr<QcWorldGenerator> generatorOf(const QString &type) const { return QcWorldGenerators::create(type, m_content, 7); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void regeneratingRestoresTheGeneratedChunk_data()
    {
        QTest::addColumn<QString>("type");
        for (const QString &type : {u"terrain"_s, u"flat"_s, u"void"_s, u"showcase"_s}) {
            QTest::newRow(qPrintable(type)) << type;
        }
    }

    void regeneratingRestoresTheGeneratedChunk()
    {
        QFETCH(QString, type);
        QTemporaryDir directory;
        join(typed(type, directory.path()), generatorOf(type));
        const QcChunkPos chunk = m_feet.chunk();
        const std::unique_ptr<QcWorldGenerator> reference = generatorOf(type);
        // A changed block and a chest that holds items.
        QVERIFY(m_server->world().setBlock(m_feet.offset(2, 0, 0), id("qtcraft:bedrock")));
        QVERIFY(!asGenerated(chunk, *reference));
        chat(u"/setblock ~1 ~ ~ chest"_s);
        QVERIFY(ensureEntity(m_feet.offset(1, 0, 0)));

        chat(u"/regenerate"_s);
        QVERIFY(!asGenerated(chunk, *reference));
        chat(u"/confirm"_s);
        QCOMPARE(lastLine(), u"Regenerated 1 chunks (0 were not loaded and unchanged)"_s);
        QVERIFY(asGenerated(chunk, *reference));
        QVERIFY(m_server->blockEntities().container(m_feet.offset(1, 0, 0)) == nullptr);

        // The client gets the chunk again, and the saved world keeps the generated blocks.
        m_client->chunks.clear();
        ticks(2);
        QVERIFY(m_client->chunks.contains(chunk));
        QVERIFY(m_server->save().has_value());
        cleanup();
        m_server = std::make_unique<QcServer>(typed(type, directory.path()), m_content, generatorOf(type));
        QVERIFY(m_server->openWorld().has_value());
        QVERIFY(m_server->world().loadChunk(chunk) != nullptr);
        QVERIFY(asGenerated(chunk, *reference));
    }

    void regeneratingAnAreaHasALimit()
    {
        join();
        chat(u"/regenerate 0 0 200 200"_s);
        QCOMPARE(lastLine(), u"At most 64 chunks at once"_s);
        chat(u"/regenerate 0"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /regenerate"_s));

        // Chunks that are not loaded are as generated anyway.
        const int x = m_feet.x;
        chat(u"/regenerate %1 %2 %3 %4"_s.arg(x - 16).arg(m_feet.z).arg(x + 16 * 4).arg(m_feet.z));
        chat(u"/confirm"_s);
        QVERIFY(lastLine().startsWith(u"Regenerated"_s));
        QVERIFY(lastLine().contains(u"were not loaded"_s));
    }

    void aPlayerInsideRegeneratedBlocksIsLiftedOut()
    {
        join();
        // Five blocks down, where the generator puts ground, inside the hole the stage dug.
        QVERIFY(m_server->world().setBlock(m_feet.offset(0, -1, 0), qcAirBlockId));
        chat(u"/tp ~0.5 ~-5 ~0.5"_s);
        ticks(1);
        QVERIFY(m_client->motion.position.y < m_feet.y - 4);
        chat(u"/regenerate"_s);
        chat(u"/confirm"_s);
        ticks(1);
        const QcBlockPos feet{int(std::floor(m_client->motion.position.x)), int(std::floor(m_client->motion.position.y)),
                              int(std::floor(m_client->motion.position.z))};
        QCOMPARE(*m_server->world().blockAt(feet), qcAirBlockId);
        QCOMPARE(*m_server->world().blockAt(feet.offset(0, 1, 0)), qcAirBlockId);
    }

    void regeneratingDropsWhatBelongedToTheOldBlocks()
    {
        join();
        // Water with its flow and neighbour updates, a falling block and two zombies: one buried
        // where the generator puts ground, one in the air above it.
        const QcBlockPos water = m_feet.offset(2, 0, 2);
        QVERIFY(m_server->world().setBlock(water, id("qtcraft:water")));
        QVERIFY(m_server->liquids().schedule(m_server->world(), water, m_server->tickCount()));
        chat(u"/setblock ~-2 ~ ~2 glass"_s);
        m_server->fallingBlocks().start(m_feet.offset(-2, 6, -2), id("qtcraft:sand"), 900010);
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        const QcVec3 here = m_client->motion.position;
        const quint32 buried = m_server->mobs().spawn(zombie, here + QcVec3{1.5, -8.0, 1.5}, 900011);
        const quint32 flying = m_server->mobs().spawn(zombie, here + QcVec3{-1.5, 12.0, -1.5}, 900012);
        QVERIFY(m_server->liquids().plannedCount() > 0);
        QVERIFY(m_server->pendingBlockUpdates() > 0);
        QCOMPARE(m_server->fallingBlocks().all().size(), 1);

        chat(u"/regenerate ~-8 ~-8 ~8 ~8"_s);
        chat(u"/confirm"_s);
        QVERIFY2(lastLine().startsWith(u"Regenerated 4 chunks"_s), qPrintable(lastLine()));
        QCOMPARE(m_server->liquids().plannedCount(), 0);
        QCOMPARE(m_server->pendingBlockUpdates(), 0);
        QCOMPARE(m_server->waitingBlockUpdates(), 0);
        QVERIFY(m_server->fallingBlocks().all().isEmpty());
        QVERIFY(m_server->mobs().find(buried) == nullptr);
        QVERIFY(m_server->mobs().find(flying) != nullptr);

        // Nothing of the old blocks comes back with the next ticks.
        ticks(40);
        QVERIFY(m_server->fallingBlocks().all().isEmpty());
        QCOMPARE(m_server->liquids().plannedCount(), 0);
    }

    void removeTakesEntitiesByKindAndRadius()
    {
        join();
        const QcVec3 here = m_client->motion.position;
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        m_server->mobs().spawn(zombie, here + QcVec3{2.0, 0.0, 0.0}, 900001);
        m_server->mobs().spawn(pig, here + QcVec3{-2.0, 0.0, 0.0}, 900002);
        m_server->mobs().spawn(zombie, here + QcVec3{20.0, 0.0, 0.0}, 900003);
        m_server->items().spawnScattered({item("qtcraft:dirt"), 3, 0}, here + QcVec3{1.0, 0.0, 1.0}, 900004);
        m_server->items().spawnScattered({item("qtcraft:stone"), 3, 0}, here + QcVec3{1.0, 0.0, -1.0}, 900005);

        chat(u"/remove hostile 10"_s);
        QCOMPARE(lastLine(), u"Removed 1 creatures, 0 item stacks and 0 armour stands"_s);
        QVERIFY(m_server->mobs().find(900001) == nullptr);
        QVERIFY(m_server->mobs().find(900003) != nullptr);
        chat(u"/remove item:dirt"_s);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 0);
        QCOMPARE(droppedCount(item("qtcraft:stone")), 3);
        chat(u"/remove pig"_s);
        QVERIFY(m_server->mobs().find(900002) == nullptr);

        // Unknown kinds, too large radii and stands without confirmation change nothing.
        for (const QString &bad : {u"/remove dragons"_s, u"/remove mobs 257"_s, u"/remove mobs 0"_s, u"/remove item:nothing"_s}) {
            chat(bad);
            QVERIFY2(lastLine().startsWith(u"Usage: /remove"_s), qPrintable(bad));
        }
        chat(u"/summon armor_stand"_s);
        QCOMPARE(m_server->stands().stands().size(), 1);
        chat(u"/remove stands"_s);
        QCOMPARE(m_server->stands().stands().size(), 1);
        chat(u"/confirm"_s);
        QCOMPARE(m_server->stands().stands().size(), 0);

        // The whole world always asks.
        chat(u"/remove all world"_s);
        QVERIFY(m_server->mobs().find(900003) != nullptr);
        chat(u"/confirm"_s);
        QVERIFY(m_server->mobs().find(900003) == nullptr);
        QVERIFY(m_server->items().items().isEmpty());
    }

    void manyEntitiesAskBeforeTheyGo()
    {
        join();
        chat(u"/summon pig 16"_s);
        chat(u"/summon zombie 4"_s);
        chat(u"/remove mobs"_s);
        QCOMPARE(lastLine(), u"Type /confirm within 30 s to go ahead or /cancel to drop it"_s);
        QVERIFY(m_server->mobs().awakeCount() >= 20);
        chat(u"/confirm"_s);
        QCOMPARE(m_server->mobs().awakeCount(), 0);
    }

    void backupsAreConsistentCopies()
    {
        QTemporaryDir data;
        const QString worldDirectory = QDir(data.path()).filePath(u"worlds/mine"_s);
        QVERIFY(QDir().mkpath(worldDirectory));
        QcServerConfig config;
        config.worldDirectory = worldDirectory;
        join(config);
        chat(u"/setblock ~5 ~ ~ glass"_s);
        chat(u"/op Friend"_s);

        backup(u"/backup before-update"_s);
        QVERIFY2(lastLine().startsWith(u"Backup with "_s), qPrintable(lastLine()));
        const QStringList backups = QDir(QDir(data.path()).filePath(u"backups"_s)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(backups.size(), 1);
        QVERIFY(backups.first().startsWith(u"mine-"_s) && backups.first().endsWith(u"-before-update"_s));
        const QString copy = QDir(data.path()).filePath(u"backups/"_s + backups.first());
        QVERIFY(!QFile::exists(QDir(copy).filePath(u"session.lock"_s)));
        QVERIFY(QFile::exists(QDir(copy).filePath(u"access.json"_s)));

        // A second backup in the same second gets its own folder.
        backup(u"/backup before-update"_s);
        QCOMPARE(QDir(QDir(data.path()).filePath(u"backups"_s)).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 2);

        // The copy opens like the world, with the change of this session.
        const QcBlockPos glass = m_feet.offset(5, 0, 0);
        cleanup();
        config.worldDirectory = copy;
        join(config);
        QCOMPARE(*m_server->world().blockAt(glass), id("qtcraft:glass"));
    }

    void backupLabelsCannotLeaveTheFolder()
    {
        QTemporaryDir data;
        QcServerConfig config;
        config.worldDirectory = QDir(data.path()).filePath(u"worlds/mine"_s);
        QVERIFY(QDir().mkpath(config.worldDirectory));
        join(config);
        for (const QString &bad : {u"/backup ../../evil"_s, u"/backup a/b"_s, u"/backup %1"_s.arg(QString(33, u'x')), u"/backup a b"_s}) {
            chat(bad);
            QCOMPARE(lastLine(), u"Usage: /backup [label]"_s);
        }
        QVERIFY(!QFile::exists(QDir(data.path()).filePath(u"backups"_s)));
        QVERIFY(!QcWorldBackup::isValidLabel(u"..\\x"_s));
        QCOMPARE(QcWorldBackup::defaultRoot(u"/data/worlds/mine"_s), u"/data/backups"_s);

        // A world without a folder has nothing to copy.
        cleanup();
        join();
        chat(u"/backup"_s);
        QCOMPARE(lastLine(), u"This world is not saved, there is nothing to back up"_s);
    }

    void backupsStayOutsideTheWorldAndAreNeverHalfDone()
    {
        QTemporaryDir data;
        const QString worldDirectory = QDir(data.path()).filePath(u"worlds/mine"_s);
        QVERIFY(QDir().mkpath(worldDirectory));
        QcServerConfig config;
        config.worldDirectory = worldDirectory;
        config.backupDirectory = QDir(worldDirectory).filePath(u"backups"_s);
        join(config);
        chat(u"/backup"_s);
        QVERIFY2(lastLine().startsWith(u"Backup failed: The backup folder "_s), qPrintable(lastLine()));
        QVERIFY(!QFile::exists(config.backupDirectory));
        QVERIFY(!QcWorldBackup::copy(worldDirectory, worldDirectory, {}, QDateTime::currentDateTime()).has_value());
        QVERIFY(!QcWorldBackup::copy(worldDirectory, worldDirectory + u"/../mine/x"_s, {}, QDateTime::currentDateTime()).has_value());
        QVERIFY(!QFile::exists(QDir(worldDirectory).filePath(u"x"_s)));

        // A file that cannot be read stops the copy; the incomplete folder goes again.
        cleanup();
        config.backupDirectory = QDir(data.path()).filePath(u"backups"_s);
        join(config);
        const QString unreadable = QDir(worldDirectory).filePath(u"notes.txt"_s);
        {
            QFile file(unreadable);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("private");
        }
        QVERIFY(QFile::setPermissions(unreadable, QFileDevice::WriteOwner));
        backup(u"/backup"_s);
        QFile::setPermissions(unreadable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(lastLine(), u"Backup failed: Cannot copy notes.txt, the incomplete backup was removed"_s);
        QCOMPARE(QDir(config.backupDirectory).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden), QStringList());

        backup(u"/backup"_s);
        QVERIFY2(lastLine().startsWith(u"Backup with "_s), qPrintable(lastLine()));
        QCOMPARE(QDir(config.backupDirectory).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);
    }

    void worldInfoReportsTheServersNumbers()
    {
        join();
        ticks(5);
        chat(u"/worldinfo"_s);
        const QStringList lines = m_client->chatLines.mid(m_client->chatLines.size() - 6);
        QVERIFY(lines[0].startsWith(u"World World (terrain, seed 0), tick "_s));
        QVERIFY(lines[1].startsWith(u"Chunks: "_s));
        QVERIFY(lines[2].startsWith(u"Entities: "_s));
        // The block entities and the central check of their rules (decision E-357).
        QVERIFY2(lines[3].startsWith(u"Block entities: "_s) && lines[3].endsWith(u"; 0 broken rules"_s), qPrintable(lines[3]));
        QVERIFY(lines[4].startsWith(u"Tick: "_s));
        QVERIFY(lines[5].startsWith(u"Memory: "_s));
    }
};

QTEST_GUILESS_MAIN(TestWorldMaintenance)
#include "test_world_maintenance.moc"
