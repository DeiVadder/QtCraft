#include "common/light/QcLightEngine.h"
#include "common/world/QcLightVolume.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// Headless measurement of the server's light (decision E-389) without real time: what computing the
// light costs when a chunk arrives and leaves, what a block change costs, how much memory the
// sections take, and what a light query costs against the flood fill on demand it replaced. Chunks of
// the terrain generator (seed 42) are generated first, untimed; every step is timed on its own.
// QTCRAFT_LIGHT_MEASURE=full measures 17 x 17 chunks and 4000 changes (the recorded sizes), else
// 9 x 9 and 400 (every test run). Prints "MEASURE key=value ..." lines.
class TestLightMeasure : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content;

    struct Times
    {
        double totalMs = 0.0;
        double maxMs = 0.0;
        int count = 0;

        void add(qint64 nanoseconds)
        {
            const double ms = static_cast<double>(nanoseconds) / 1e6;
            totalMs += ms;
            maxMs = std::max(maxMs, ms);
            ++count;
        }
        QString text(const QString &key) const
        {
            return u"%1_avg_ms=%2 %1_max_ms=%3"_s.arg(key).arg(count ? totalMs / count : 0.0, 0, 'f', 4).arg(maxMs, 0, 'f', 3);
        }
    };

    static bool full() { return qEnvironmentVariable("QTCRAFT_LIGHT_MEASURE") == u"full"_s; }

    std::vector<std::shared_ptr<QcChunk>> generate(int radius) const
    {
        const QcTerrainGenerator generator(m_content, 42);
        std::vector<std::shared_ptr<QcChunk>> chunks;
        for (int cx = -radius; cx <= radius; ++cx) {
            for (int cz = -radius; cz <= radius; ++cz) {
                chunks.push_back(generator.generate({cx, cz}));
            }
        }
        return chunks;
    }

private slots:
    void initTestCase()
    {
        m_content = QcTestUtil::loadContent();
    }

    // Light of arriving and leaving chunks, block light alone (what the client computes) and with sky
    // light (the server), and the memory of the sections.
    void chunkArrivalAndMemory()
    {
        const int radius = full() ? 8 : 4;
        const std::vector<std::shared_ptr<QcChunk>> chunks = generate(radius);
        const auto table = std::make_shared<const QcLightTable>(*m_content);

        // Block light only, both channels, both with the chunk's own light prepared beforehand (the
        // server does that in the background: only joining the borders is timed).
        for (const int mode : {0, 1, 2}) {
            const bool sky = mode > 0;
            QcLightEngine engine(table, {.sky = sky});
            Times add;
            Times prepare;
            QElapsedTimer timer;
            for (const std::shared_ptr<QcChunk> &chunk : chunks) {
                std::shared_ptr<QcLightEngine::Prepared> prepared;
                if (mode == 2) {
                    timer.start();
                    prepared = engine.prepare(*chunk);
                    prepare.add(timer.nsecsElapsed());
                }
                timer.start();
                engine.addChunk(*chunk, prepared);
                add.add(timer.nsecsElapsed());
            }

            const QcLightEngine::Memory memory = engine.memory();
            const qsizetype sections = memory.columns * QcWorld::sectionCount;
            Times remove;
            for (const std::shared_ptr<QcChunk> &chunk : chunks) {
                if (std::abs(chunk->pos().x) + std::abs(chunk->pos().z) <= radius) {
                    timer.start();
                    engine.removeChunk(chunk->pos());
                    remove.add(timer.nsecsElapsed());
                }
            }

            const QString channels = mode == 2 ? u"block+sky,prepared %1"_s.arg(prepare.text(u"prepare"_s))
                                               : (sky ? u"block+sky"_s : u"block"_s);
            qInfo().noquote() << u"MEASURE channels=%1 chunks=%2 %3 %4 bytes_per_chunk=%5 block_sections=%6 sky_sections=%7 of=%8"_s
                                     .arg(channels)
                                     .arg(chunks.size())
                                     .arg(add.text(u"add"_s), remove.text(u"remove"_s))
                                     .arg(memory.bytes / std::max<qsizetype>(1, memory.columns))
                                     .arg(memory.sections[0])
                                     .arg(memory.sections[1])
                                     .arg(sections);
            QVERIFY(add.count > 0);
        }
    }

    // Block changes in the server world as the game makes them (torches set and taken away, blocks
    // placed and dug on the surface and below), timed as a whole (QcServerWorld::setBlock with its
    // light update), and a light query against the flood fill on demand (they must agree).
    void blockChangesAndQueries()
    {
        const int radius = full() ? 8 : 4;
        QcServerWorld world(m_content, std::make_unique<QcTerrainGenerator>(m_content, 42));
        for (int cx = -radius; cx <= radius; ++cx) {
            for (int cz = -radius; cz <= radius; ++cz) {
                world.loadChunk({cx, cz});
            }
        }

        QRandomGenerator random(7);
        const QList<QcBlockId> blocks = {qcAirBlockId, *m_content->blockId(u"qtcraft:torch"_s), *m_content->blockId(u"qtcraft:stone"_s),
                                         *m_content->blockId(u"qtcraft:glass"_s), *m_content->blockId(u"qtcraft:oak_leaves"_s)};
        const int changes = full() ? 4000 : 400;
        Times change;
        Times flood;
        Times query;
        QElapsedTimer timer;
        const int span = radius * 16;
        for (int i = 0; i < changes; ++i) {
            const QcBlockPos column{random.bounded(-span, span + 16), 0, random.bounded(-span, span + 16)};
            const int surface = world.chunk(column.chunk())->highestNonAir(column.localX(), column.localZ());
            const QcBlockPos pos{column.x, std::clamp(surface - 6 + random.bounded(10), 1, 254), column.z};
            timer.start();
            QVERIFY(world.setBlock(pos, blocks[qsizetype(random.bounded(quint32(blocks.size())))]));
            change.add(timer.nsecsElapsed());

            const QcBlockPos probe = pos.offset(1, 1, 0);
            timer.start();
            const int engineLight = world.blockLight(probe);
            query.add(timer.nsecsElapsed());
            timer.start();
            const int floodLight = QcLightVolume::blockLightAt(world, probe);
            flood.add(timer.nsecsElapsed());
            QCOMPARE(engineLight, floodLight);
        }

        qInfo().noquote() << u"MEASURE chunks=%1 changes=%2 %3 %4 %5 visited_cells=%6"_s.arg(world.loadedChunkCount())
                                 .arg(changes)
                                 .arg(change.text(u"set_block"_s), query.text(u"engine_query"_s), flood.text(u"flood_query"_s))
                                 .arg(world.light().visitedCells());
    }
};

QTEST_GUILESS_MAIN(TestLightMeasure)
#include "test_light_measure.moc"
