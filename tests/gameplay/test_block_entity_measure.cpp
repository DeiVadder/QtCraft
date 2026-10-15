#include "QcBlockEntityDump.h"
#include "QcServerTestFixture.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Headless measurement of the block entities (stage 31.1, decision E-357): a world with chests,
// burning furnaces, hoppers under chests and saplings in every chunk of a square; the time of a
// server tick, of saving and of opening the world again. Small in every test run (checks only that
// the world comes back); QTCRAFT_BLOCK_ENTITY_MEASURE=full measures the full size and prints MEASURE
// lines (Release build).
class TestBlockEntityMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    struct Size
    {
        int radius = 1;
        int ticks = 20;
    };

    static Size size()
    {
        const int ticks = qEnvironmentVariableIntValue("QTCRAFT_BLOCK_ENTITY_MEASURE_TICKS");
        Size size = qEnvironmentVariable("QTCRAFT_BLOCK_ENTITY_MEASURE") == u"full"_s ? Size{10, 200} : Size{1, 20};
        size.ticks = ticks > 0 ? ticks : size.ticks;
        return size;
    }

    QcItemStack stackOf(const char *name, int count) const { return {item(name), count, 0}; }

    void setChest(QcBlockPos pos, const QList<QcItemStack> &stacks)
    {
        QVERIFY(m_server->world().setBlock(pos, id("qtcraft:chest")));
        QVERIFY(ensureEntity(pos));
        for (qsizetype i = 0; i < stacks.size(); ++i) {
            m_server->blockEntities().container(pos)->stacks[i] = stacks[i];
        }
    }

    // Per chunk: five chests, three burning furnaces, three hoppers under chests and three saplings.
    void furnishChunk(QcChunkPos chunk)
    {
        const QcBlockPos base{chunk.x * QcWorld::chunkSize, 90, chunk.z * QcWorld::chunkSize};
        for (int i = 0; i < 5; ++i) {
            setChest(base.offset(i * 2, 0, 0), {stackOf("qtcraft:diamond", 5), stackOf("qtcraft:stick", 30), stackOf("qtcraft:dirt", 64)});
        }
        for (int i = 0; i < 3; ++i) {
            const QcBlockPos furnace = base.offset(i * 2, 0, 4);
            QVERIFY(m_server->world().setBlock(furnace, id("qtcraft:furnace")));
            QVERIFY(ensureEntity(furnace));
            m_server->blockEntities().machine(furnace)->stacks = {stackOf("qtcraft:iron_ore", 64), stackOf("qtcraft:coal", 64), QcItemStack{}};

            const QcBlockPos hopper = base.offset(i * 2, 0, 8);
            setChest(hopper.offset(0, 1, 0), {stackOf("qtcraft:cobblestone", 64)});
            QVERIFY(m_server->world().setBlock(hopper, *m_content->stateId(u"qtcraft:signal_hopper[facing=down]"_s)));
            m_server->noteContents(hopper.offset(0, 1, 0));

            const QcBlockPos sapling = base.offset(i * 2, 0, 12);
            QVERIFY(m_server->world().setBlock(sapling.offset(0, -1, 0), id("qtcraft:dirt")));
            QVERIFY(m_server->world().setBlock(sapling, id("qtcraft:oak_sapling")));
            m_server->plants().saplingPlaced(m_server->blockEntities(), sapling, id("qtcraft:oak_sapling"));
        }
    }

    static void report(const char *what, double ms) { qInfo("MEASURE %s %.3f ms", what, ms); }

private slots:
    void tickSaveAndOpen()
    {
        const Size measure = size();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        const QcChunkPos centre = m_feet.chunk();
        for (int dx = -measure.radius; dx <= measure.radius; ++dx) {
            for (int dz = -measure.radius; dz <= measure.radius; ++dz) {
                m_server->world().loadChunk({centre.x + dx, centre.z + dz});
                furnishChunk({centre.x + dx, centre.z + dz});
            }
        }
        ticks(5);

        QElapsedTimer timer;
        qint64 tickNs = 0;
        std::array<qint64, QcTickPhases::count> phaseNs{};
        for (int i = 0; i < measure.ticks; ++i) {
            timer.start();
            m_server->tick();
            tickNs += timer.nsecsElapsed();
            for (int phase = 0; phase < QcTickPhases::inTick; ++phase) {
                phaseNs[std::size_t(phase)] += m_server->tickPhases().lastTick()[std::size_t(phase)];
            }
            pump();
        }
        report("tick", double(tickNs) / measure.ticks / 1e6);
        for (int phase = 0; phase < QcTickPhases::inTick; ++phase) {
            qInfo("MEASURE phase-%s %.3f ms", QcTickPhases::ids[std::size_t(phase)], double(phaseNs[std::size_t(phase)]) / measure.ticks / 1e6);
        }

        timer.start();
        QVERIFY(m_server->save().has_value());
        report("first-save", double(timer.nsecsElapsed()) / 1e6);
        ticks(1);
        timer.start();
        QVERIFY(m_server->save().has_value());
        report("second-save", double(timer.nsecsElapsed()) / 1e6);
        const QStringList lines = QcBlockEntityDump::lines(*m_server, *m_content);

        m_client.reset();
        m_server.reset();
        timer.start();
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        report("open", double(timer.nsecsElapsed()) / 1e6);
        // Chunks are read when needed (stage 31.3): reading all of them is measured on its own.
        timer.start();
        loadStored();
        report("load-all", double(timer.nsecsElapsed()) / 1e6);
        // The same block entities come back (ticks counted from now on both sides).
        QCOMPARE(QcBlockEntityDump::lines(*m_server, *m_content), lines);
    }
};

QTEST_GUILESS_MAIN(TestBlockEntityMeasure)
#include "test_block_entity_measure.moc"
