#include "QcServerTestFixture.h"

#include "QcProcessMemory.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Headless measurement of a large saved world (stage 31.3): every chunk of a square was changed and
// holds two animals and a filled chest; the time to save it, to open it again and until a player who
// joins it stands in loaded chunks, with the process memory before and after opening. Small in every
// test run (checks only that the world comes back); QTCRAFT_WORLD_OPEN_MEASURE=full measures the full
// size (radius 20, 1681 chunks) and prints MEASURE lines (Release build).
class TestWorldOpenMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    static int radius()
    {
        const int asked = qEnvironmentVariableIntValue("QTCRAFT_WORLD_OPEN_MEASURE_RADIUS");
        if (asked > 0) {
            return asked;
        }
        return qEnvironmentVariable("QTCRAFT_WORLD_OPEN_MEASURE") == u"full"_s ? 20 : 2;
    }

    quint32 m_nextId = 3'000'000;

    void furnishChunk(QcChunkPos chunk)
    {
        const std::shared_ptr<const QcChunk> loaded = m_server->world().loadChunk(chunk);
        const int x = chunk.x * QcWorld::chunkSize;
        const int z = chunk.z * QcWorld::chunkSize;
        const int ground = loaded->highestNonAir(4, 4) + 1;
        QVERIFY(m_server->world().setBlock({x + 4, ground + 20, z + 4}, id("qtcraft:stone")));
        const QcBlockPos chest{x + 8, ground + 21, z + 8};
        QVERIFY(m_server->world().setBlock(chest, id("qtcraft:chest")));
        QcBlockEntities::Container *container = m_server->blockEntities().container(chest);
        QVERIFY(container);
        container->stacks[0] = {item("qtcraft:diamond"), 3, 0};

        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        m_server->mobs().spawn(pig, {x + 4.5, double(ground), z + 4.5}, m_nextId++, false, true);
        m_server->mobs().spawn(pig, {x + 10.5, double(ground), z + 10.5}, m_nextId++, false, true);
    }

    static void report(const char *what, double value, const char *unit) { qInfo("MEASURE %s %.3f %s", what, value, unit); }

    // A player joins while a save of every chunk runs in the background: his join reads his file and
    // the spawn chunks without waiting for that save (decision E-378).
    void measureJoinDuringSave()
    {
        QElapsedTimer timer;
        m_server->saveInBackground();
        timer.start();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        RawClient other(std::move(clientEnd));
        other.hello(QStringLiteral("Other"));
        for (int i = 0; i < 200 && other.motion.position == QcVec3{}; ++i) {
            pump();
            ticks(1);
        }
        QVERIFY(other.motion.position != QcVec3{});
        report("join-during-save", double(timer.nsecsElapsed()) / 1e6, "ms");
        m_server->waitForStorage();
    }

private slots:
    void saveOpenAndJoin()
    {
        const int size = radius();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = 4;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        const QcChunkPos centre = m_feet.chunk();
        for (int dx = -size; dx <= size; ++dx) {
            for (int dz = -size; dz <= size; ++dz) {
                furnishChunk({centre.x + dx, centre.z + dz});
            }
        }
        ticks(2);

        measureJoinDuringSave();

        QElapsedTimer timer;
        timer.start();
        QVERIFY(m_server->save().has_value());
        report("save", double(timer.nsecsElapsed()) / 1e6, "ms");
        const int chunkCount = (2 * size + 1) * (2 * size + 1);
        qInfo("MEASURE chunks %d", chunkCount);

        m_client.reset();
        m_server.reset();
        const double before = QcProcessMemory::currentMb();
        timer.start();
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        report("open", double(timer.nsecsElapsed()) / 1e6, "ms");
        report("memory-after-open", QcProcessMemory::currentMb() - before, "MB");
        qInfo("MEASURE loaded-after-open %lld", qlonglong(m_server->world().loadedChunkCount()));

        timer.start();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(QStringLiteral("Raw"));
        pump();
        int waited = 0;
        const auto standsInLoaded = [this] {
            const QcChunkPos at = QcPhysics::blockContaining(m_client->motion.position).chunk();
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (!m_server->world().chunk({at.x + dx, at.z + dz})) {
                        return false;
                    }
                }
            }
            return true;
        };
        while (!standsInLoaded() && waited < 2000) {
            ticks(1);
            ++waited;
        }
        QVERIFY(standsInLoaded());
        report("open-to-playing", double(timer.nsecsElapsed()) / 1e6, "ms");
        qInfo("MEASURE ticks-to-playing %d", waited);

        // Everything comes back where the player can see it.
        const QcChunkPos home = m_feet.chunk();
        const QcBlockPos chest{home.x * QcWorld::chunkSize + 8, 0, home.z * QcWorld::chunkSize + 8};
        bool found = false;
        for (int y = 0; y < QcWorld::worldHeight && !found; ++y) {
            const QcBlockEntities::Container *container = m_server->blockEntities().container({chest.x, y, chest.z});
            found = container && container->stacks[0] == QcItemStack{item("qtcraft:diamond"), 3, 0};
        }
        QVERIFY(found);
    }
};

QTEST_GUILESS_MAIN(TestWorldOpenMeasure)
#include "test_world_open_measure.moc"
