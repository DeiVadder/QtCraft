#include "QcBaseContent.h"
#include "QcClientSession.h"
#include "QcLocalServerHost.h"
#include "QcTestTerrainGenerator.h"
#include "QcTestUtil.h"
#include "QcTransport.h"
#include "QcWorldStorage.h"

#include <QDir>
#include <QFile>
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

private slots:
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
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(u"players/Saver.qcp"_s)));

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
        const QcResult<QcWorldMobs> saved = QcWorldStorage(dir.path(), m_content).readMobs(warnings);
        QVERIFY2(saved.has_value(), qcErrorText(saved).constData());

        // The chunks around the spawn were seen and some of them got animals.
        QVERIFY(saved->populatedChunks.contains({0, 0}));
        QVERIFY(!saved->mobs.isEmpty());

        QSet<QString> kinds;
        for (const QcMobManager::Stored &mob : saved->mobs) {
            kinds.insert(m_content->mobs()[mob.type].name);
            QVERIFY(!m_content->mobs()[mob.type].hostile);
        }

        // Keep a single marked animal: after the restart exactly it must come back. Regenerated
        // animals would show up as more creatures with full health.
        QcWorldMobs marked;
        marked.populatedChunks = saved->populatedChunks;
        marked.mobs.append(saved->mobs.first());
        marked.mobs.first().health = 3.5;
        QVERIFY(QcWorldStorage(dir.path(), m_content).writeMobs(marked).has_value());

        auto server = host(dir.path());
        QVERIFY2(server->startError().isEmpty(), qPrintable(server->startError()));
        QcClientSession session(server->connectClient(), u"Herder"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing && session.playerMotion().onGround,
                                 timeoutMs);

        QVERIFY(server->shutdown().has_value());

        const QcResult<QcWorldMobs> again = QcWorldStorage(dir.path(), m_content).readMobs(warnings);
        QVERIFY(again.has_value());
        QVERIFY(again->populatedChunks.contains(saved->populatedChunks));
        QCOMPARE(again->mobs.size(), qsizetype(1));
        QCOMPARE(again->mobs.first().type, marked.mobs.first().type);
        QCOMPARE(again->mobs.first().health, 3.5);
        QVERIFY(kinds.contains(m_content->mobs()[again->mobs.first().type].name));
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
