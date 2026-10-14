#include "QcBaseContent.h"
#include "QcClientSession.h"
#include "QcLocalServerHost.h"
#include "QcPhysics.h"
#include "QcTestTerrainGenerator.h"
#include "QcTestUtil.h"
#include "QcTransport.h"
#include "QcWorldStorage.h"

#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 20000;
constexpr qint64 seed = 99;

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

} // namespace

// Saving and loading a world through the real server: blocks, inventory and position survive a restart.
class TestPersistence : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

    std::unique_ptr<QcLocalServerHost> host(const QString &directory)
    {
        QcServerConfig config;
        config.worldName = u"Saved"_s;
        config.seed = seed;
        config.viewDistance = 2;
        config.worldDirectory = directory;
        return std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
    }

    // The saved creatures of every chunk, in their saved order.
    static QList<QcMobManager::Stored> creaturesOf(const QcWorldEntityRecords &saved)
    {
        QList<QcEntityRecord::Ordered<QcMobManager::Stored>> all;
        for (const QcEntityRecord &record : saved.records) {
            all.append(record.creatures);
        }
        std::ranges::sort(all, {}, &QcEntityRecord::Ordered<QcMobManager::Stored>::order);
        QList<QcMobManager::Stored> creatures;
        for (const auto &creature : std::as_const(all)) {
            creatures.append(creature.entry);
        }
        return creatures;
    }

private slots:
    void aWorldOpensInOneServerAtATime()
    {
        QTemporaryDir dir;
        auto first = host(dir.path());
        QVERIFY(first->startError().isEmpty());
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(u"session.lock"_s)));

        // A second window or process gets a clear refusal instead of overwriting the first one's saves.
        auto second = host(dir.path());
        QVERIFY(second->worldInUse());
        QVERIFY2(second->startError().contains(u"already open"_s), qPrintable(second->startError()));
        second.reset();

        // Stopping the first server releases the world.
        QVERIFY(first->shutdown().has_value());
        first.reset();
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(u"session.lock"_s)));
        auto third = host(dir.path());
        QVERIFY2(third->startError().isEmpty(), qPrintable(third->startError()));
        QVERIFY(!third->worldInUse());
    }

    void aLockOfACrashedProcessDoesNotBlockTheWorld()
    {
        QTemporaryDir dir;
        // QLockFile's format: process id, application name, host name; no process has this id.
        QFile lock(QDir(dir.path()).filePath(u"session.lock"_s));
        QVERIFY(lock.open(QIODevice::WriteOnly));
        lock.write("999999\nqtcraft\n" + QSysInfo::machineHostName().toUtf8() + "\n");
        lock.close();

        auto server = host(dir.path());
        QVERIFY2(server->startError().isEmpty(), qPrintable(server->startError()));
    }

    void blocksInventoryAndPositionSurviveARestart()
    {
        QTemporaryDir dir;
        QcBlockPos mined;
        QcVec3 position;

        {
            auto server = host(dir.path());
            QVERIFY(server->startError().isEmpty());
            QcClientSession session(server->connectClient(), u"Saver"_s);
            session.start();
            QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing && session.playerMotion().onGround,
                                     timeoutMs);

            session.sendChat(u"/give glass 7"_s);
            QTRY_COMPARE_WITH_TIMEOUT(session.inventory().slot(0).count, 7, timeoutMs);

            // Dig the block under the feet: its chunk becomes modified and the drop lands in slot 1.
            QcPlayerInput down;
            down.pitch = -90.0f;
            session.setIntent(down);
            QVERIFY(session.target().has_value());
            mined = session.target()->block;
            session.setMining(true);
            QTRY_VERIFY_WITH_TIMEOUT(session.world()->blockAt(mined) == std::optional<QcBlockId>(qcAirBlockId), timeoutMs);
            session.setMining(false);
            QTRY_VERIFY_WITH_TIMEOUT(session.inventory().slot(1).count == 1, timeoutMs);

            // Wait until the player has fallen into the hole and the server agrees.
            QTRY_VERIFY_WITH_TIMEOUT(session.playerMotion().onGround && session.pendingInputCount() <= 1
                                         && session.playerMotion().position.y < mined.y + 0.5,
                                     timeoutMs);
            position = session.playerMotion().position;
            QVERIFY(server->shutdown().has_value());
        }

        QVERIFY(QFile::exists(QDir(dir.path()).filePath(u"world.json"_s)));
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Saver"_s))));

        auto server = host(dir.path());
        QVERIFY2(server->startError().isEmpty(), qPrintable(server->startError()));
        QcClientSession session(server->connectClient(), u"Saver"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, timeoutMs);

        QTRY_VERIFY_WITH_TIMEOUT(session.world()->blockAt(mined).has_value(), timeoutMs);
        QCOMPARE(session.world()->blockAt(mined), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(session.inventory().slot(0).item, *m_content->itemId(u"qtcraft:glass"_s));
        QCOMPARE(session.inventory().slot(0).count, 7);
        QCOMPARE(session.inventory().slot(1).item, *m_content->itemId(u"qtcraft:dirt"_s));
        QCOMPARE(session.playerMotion().position, position);
        QCOMPARE(session.worldName(), u"Saved"_s);
        QVERIFY(server->shutdown().has_value());
    }

    void animalsAndPopulatedChunksSurviveARestart()
    {
        QTemporaryDir dir;

        {
            auto server = host(dir.path());
            QVERIFY(server->startError().isEmpty());
            QcClientSession session(server->connectClient(), u"Herder"_s);
            session.start();
            QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing && session.playerMotion().onGround,
                                     timeoutMs);

            QVERIFY(server->shutdown().has_value());
        }

        QStringList warnings;
        const QcResult<QcWorldEntityRecords> saved = QcWorldStorage(dir.path(), m_content).readEntities(warnings);
        QVERIFY2(saved.has_value(), qcErrorText(saved).constData());
        const QList<QcMobManager::Stored> animals = creaturesOf(*saved);

        // The chunks around the spawn were seen and some of them got animals.
        QVERIFY(saved->populated.contains({0, 0}));
        QVERIFY(!animals.isEmpty());

        QSet<QString> kinds;
        for (const QcMobManager::Stored &mob : animals) {
            kinds.insert(m_content->mobs()[mob.type].name);
            QVERIFY(!m_content->mobs()[mob.type].hostile);
        }

        // Keep a single marked animal: after the restart exactly it must come back. Regenerated
        // animals would show up as more creatures with full health.
        QcMobManager::Stored marked = animals.first();
        marked.health = 3.5;
        QcEntityRecord record;
        record.chunk = QcPhysics::blockContaining(marked.position).chunk();
        record.creatures.append({marked, 0});
        QVERIFY(QcWorldStorage(dir.path(), m_content).writeEntities({{record}, saved->populated}).has_value());

        auto server = host(dir.path());
        QVERIFY2(server->startError().isEmpty(), qPrintable(server->startError()));
        QcClientSession session(server->connectClient(), u"Herder"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing && session.playerMotion().onGround,
                                 timeoutMs);

        QVERIFY(server->shutdown().has_value());

        const QcResult<QcWorldEntityRecords> again = QcWorldStorage(dir.path(), m_content).readEntities(warnings);
        QVERIFY(again.has_value());
        QVERIFY(again->populated.contains(saved->populated));
        const QList<QcMobManager::Stored> after = creaturesOf(*again);
        QCOMPARE(after.size(), qsizetype(1));
        QCOMPARE(after.first().type, marked.type);
        QCOMPARE(after.first().health, 3.5);
        QVERIFY(kinds.contains(m_content->mobs()[after.first().type].name));
    }

    void damagedWorldInfoRefusesToStart()
    {
        QTemporaryDir dir;
        QVERIFY(host(dir.path())->shutdown().has_value());

        QFile info(QDir(dir.path()).filePath(u"world.json"_s));
        QVERIFY(info.open(QIODevice::WriteOnly | QIODevice::Truncate));
        info.write("{ broken");
        info.close();

        auto server = host(dir.path());
        QVERIFY(server->startError().contains(u"world.json"_s));

        // A host that failed to start hands out closed transports instead of hanging clients.
        QcClientSession session(server->connectClient(), u"Late"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Disconnected, timeoutMs);
    }

    void seedMismatchIsReported()
    {
        QTemporaryDir dir;
        QVERIFY(host(dir.path())->shutdown().has_value());
        QcServerConfig config;
        config.seed = seed + 1;
        config.worldDirectory = dir.path();
        QcLocalServerHost other(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(other.startError().contains(u"seed"_s));
    }
};

QTEST_GUILESS_MAIN(TestPersistence)
#include "test_persistence.moc"
