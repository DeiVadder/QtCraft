#include "QcChunkMesher.h"
#include "QcClientWorld.h"
#include "QcMeshScheduler.h"
#include "QcSectionKey.h"
#include "QcTextureAtlas.h"
#include "QcWorkerPool.h"

#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <condition_variable>
#include <mutex>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 10000;

std::shared_ptr<QcContentRegistry> makeContent()
{
    auto content = std::make_shared<QcContentRegistry>();
    QcBlockDefinition stone;
    stone.name = u"t:stone"_s;
    stone.faceTextures.fill(stone.name);
    if (!content->registerBlock(stone)) {
        qFatal("cannot register test block");
    }
    // Id 2: looks different, same light properties as stone.
    stone.name = u"t:granite"_s;
    stone.faceTextures.fill(stone.name);
    // Id 3: a torch.
    QcBlockDefinition torch;
    torch.name = u"t:torch"_s;
    torch.solid = false;
    torch.opaque = false;
    torch.renderLayer = QcRenderLayer::Cutout;
    torch.lightOpacity = 0;
    torch.lightLevel = 14;
    torch.faceTextures.fill(torch.name);
    if (!content->registerBlock(stone) || !content->registerBlock(torch)) {
        qFatal("cannot register test blocks");
    }
    return content;
}

std::shared_ptr<QcChunk> flatChunk(QcChunkPos pos, int height)
{
    auto chunk = std::make_shared<QcChunk>(pos);
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < height; ++y) {
                chunk->setBlock(x, y, z, 1);
            }
        }
    }
    return chunk;
}

} // namespace

class TestMeshScheduler : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<QcContentRegistry> m_content = makeContent();
    std::shared_ptr<const QcBlockRenderTable> m_table =
        std::make_shared<const QcBlockRenderTable>(*m_content, QcTextureAtlas::build(*m_content));

    // Loads a 3 x 3 square of flat chunks around the origin.
    void loadSquare(QcClientWorld &world, QcMeshScheduler &scheduler, int height)
    {
        for (int x = -1; x <= 1; ++x) {
            for (int z = -1; z <= 1; ++z) {
                world.setChunk(flatChunk({x, z}, height));
                scheduler.chunkLoaded(world, {x, z});
            }
        }
    }

    void pumpUntilIdle(QcMeshScheduler &scheduler, const QcClientWorld &world)
    {
        QVERIFY(QTest::qWaitFor(
            [&] {
                scheduler.pump(world);
                return scheduler.dirtyCount() == 0 && scheduler.jobsInFlight() == 0 && scheduler.lightJobsPending() == 0;
            },
            timeoutMs));
    }

private slots:
    void onlyChunksWithAllSideNeighboursGetMeshes()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QHash<quint64, std::shared_ptr<const QcChunkMesh>> meshes;
        connect(&scheduler, &QcMeshScheduler::meshReady, this,
                [&meshes](quint64 key, const std::shared_ptr<const QcChunkMesh> &mesh) { meshes.insert(key, mesh); });
        loadSquare(world, scheduler, 20);
        pumpUntilIdle(scheduler, world);
        // Only the centre chunk has all four side neighbours. Of its two non-empty sections only
        // section 1 has visible faces (the surface at y = 20); section 0 is buried completely.
        QCOMPARE(meshes.size(), 1);
        QVERIFY(meshes.contains(qcSectionKey({0, 0}, 1)));
    }

    void scheduledMeshEqualsDirectMeshing()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        std::shared_ptr<const QcChunkMesh> scheduled;
        connect(&scheduler, &QcMeshScheduler::meshReady, this, [&scheduled](quint64 key, const std::shared_ptr<const QcChunkMesh> &mesh) {
            if (key == qcSectionKey({0, 0}, 1)) {
                scheduled = mesh;
            }
        });
        loadSquare(world, scheduler, 20);
        pumpUntilIdle(scheduler, world);
        QVERIFY(scheduled);

        QcMeshInput input;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int index = (dz + 1) * 3 + (dx + 1);
                input.chunks[std::size_t(index)] = world.chunk({dx, dz});
            }
        }
        input.sectionIndex = 1;
        input.renderTable = m_table;
        const QcChunkMesh direct = QcChunkMesher::mesh(input);
        QCOMPARE(scheduled->opaqueIndices, direct.opaqueIndices);
        QCOMPARE(scheduled->opaqueVertices.size(), direct.opaqueVertices.size());
        // A flat surface at y = 20 inside a fully surrounded chunk: exactly the 256 top faces.
        QCOMPARE(direct.opaqueIndices.size(), qsizetype(256) * 6);
    }

    void blockChangeRemeshesAffectedSections()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QHash<quint64, std::shared_ptr<const QcChunkMesh>> meshes;
        connect(&scheduler, &QcMeshScheduler::meshReady, this,
                [&meshes](quint64 key, const std::shared_ptr<const QcChunkMesh> &mesh) { meshes.insert(key, mesh); });
        loadSquare(world, scheduler, 16);
        pumpUntilIdle(scheduler, world);
        QCOMPARE(meshes.size(), 1);
        const qsizetype before = meshes.value(qcSectionKey({0, 0}, 0))->opaqueIndices.size();

        // Dig a hole at the top of section 0: the pit adds 4 side faces and a floor face.
        QVERIFY(world.setBlock({5, 15, 5}, qcAirBlockId));
        scheduler.blockChanged(world, {5, 15, 5}, 1);
        pumpUntilIdle(scheduler, world);
        QCOMPARE(meshes.value(qcSectionKey({0, 0}, 0))->opaqueIndices.size(), before + qsizetype(4) * 6);
    }

    void blockChangeAlsoRemeshesSectionsReachedByLight()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QSet<quint64> meshed;
        connect(&scheduler, &QcMeshScheduler::meshReady, this,
                [&meshed](quint64 key, const std::shared_ptr<const QcChunkMesh> &) { meshed.insert(key); });
        connect(&scheduler, &QcMeshScheduler::meshRemoved, this, [&meshed](quint64 key) { meshed.insert(key); });
        loadSquare(world, scheduler, 64);
        pumpUntilIdle(scheduler, world);
        meshed.clear();

        // A shaft from y = 40 up to y = 62, still closed at the top: no light reaches it.
        for (int y = 40; y <= 62; ++y) {
            QVERIFY(world.setBlock({15, y, 15}, qcAirBlockId));
        }
        // Opening it lets sky light down to y = 40, into the light volume of section 1 (y 0..47 with
        // its margin); those sections are meshed again, section 0 (up to y = 31) is not (neighbour
        // chunks are marked too but lack side neighbours here).
        QVERIFY(world.setBlock({15, 63, 15}, qcAirBlockId));
        scheduler.blockChanged(world, {15, 63, 15}, 1);
        // Only the centre chunk has all side neighbours, so only its sections produce results.
        pumpUntilIdle(scheduler, world);
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 1)));
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 3)));
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 4)));
        QVERIFY(!meshed.contains(qcSectionKey({0, 0}, 0)));
        QVERIFY(!meshed.contains(qcSectionKey({0, 0}, 5)));

        // A block put into the shaft at y = 50 shades y = 40..49 again: sections 1 to 4.
        meshed.clear();
        QVERIFY(world.setBlock({15, 50, 15}, 1));
        scheduler.blockChanged(world, {15, 50, 15}, qcAirBlockId);
        pumpUntilIdle(scheduler, world);
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 1)));
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 3)));
        QVERIFY(!meshed.contains(qcSectionKey({0, 0}, 0)));
        QVERIFY(!meshed.contains(qcSectionKey({0, 0}, 5)));
    }

    void blockChangesRemeshOnlyWhatTheyReach()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QSet<quint64> meshed;
        connect(&scheduler, &QcMeshScheduler::meshReady, this,
                [&meshed](quint64 key, const std::shared_ptr<const QcChunkMesh> &) { meshed.insert(key); });
        connect(&scheduler, &QcMeshScheduler::meshRemoved, this, [&meshed](quint64 key) { meshed.insert(key); });
        loadSquare(world, scheduler, 64);
        pumpUntilIdle(scheduler, world);

        // Granite for stone in the middle of section 3: same light, so only that section.
        meshed.clear();
        QVERIFY(world.setBlock({8, 56, 8}, 2));
        scheduler.blockChanged(world, {8, 56, 8}, 1);
        pumpUntilIdle(scheduler, world);
        QCOMPARE(meshed, (QSet<quint64>{qcSectionKey({0, 0}, 3)}));
        // On the corner of a section the neighbouring sections read its faces and light too (only the
        // centre chunk has all side neighbours, so only its sections are meshed here).
        meshed.clear();
        QVERIFY(world.setBlock({0, 48, 15}, 2));
        scheduler.blockChanged(world, {0, 48, 15}, 1);
        pumpUntilIdle(scheduler, world);
        QCOMPARE(meshed, (QSet<quint64>{qcSectionKey({0, 0}, 2), qcSectionKey({0, 0}, 3)}));

        // A torch on the surface: no sky light changes and the block light reaches up to y = 77, so
        // sections 3 (y 48..63) and 4 (y 64..79) change, not those below or above.
        meshed.clear();
        QVERIFY(world.setBlock({8, 64, 8}, 3));
        scheduler.blockChanged(world, {8, 64, 8}, qcAirBlockId);
        QCOMPARE(scheduler.dirtyCount(), 0);
        QCOMPARE(scheduler.lightJobsPending(), 1);
        pumpUntilIdle(scheduler, world);
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 3)));
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 4)));
        QVERIFY(!meshed.contains(qcSectionKey({0, 0}, 2)));
        QVERIFY(!meshed.contains(qcSectionKey({0, 0}, 5)));
        QCOMPARE(scheduler.blockLight({8, 64, 8}), 14);
        QCOMPARE(scheduler.blockLight({8, 66, 8}), 12);
        QCOMPARE(scheduler.blockLight({8, 63, 8}), 0);
        QCOMPARE(scheduler.blockLight({-3, 64, 8}), 14 - 11);
        // Every change was counted with its time to the screen.
        const QcMeshScheduler::Stats stats = scheduler.stats();
        QVERIFY(stats.latencySamples >= 5);
        QVERIFY(stats.latencyWorstMs > 0.0);
        QVERIFY(stats.lightJobs >= 1);
    }

    void remeshedSectionsKeepEveryMeshCurrent()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QHash<quint64, std::shared_ptr<const QcChunkMesh>> meshes;
        connect(&scheduler, &QcMeshScheduler::meshReady, this,
                [&meshes](quint64 key, const std::shared_ptr<const QcChunkMesh> &mesh) { meshes.insert(key, mesh); });
        connect(&scheduler, &QcMeshScheduler::meshRemoved, this, [&meshes](quint64 key) { meshes.remove(key); });
        loadSquare(world, scheduler, 64);
        pumpUntilIdle(scheduler, world);
        const auto change = [&](QcBlockPos pos, QcBlockId block) {
            const QcBlockId previous = *world.blockAt(pos);
            QVERIFY(world.setBlock(pos, block));
            scheduler.blockChanged(world, pos, previous);
        };
        // A shaft dug from the top down to y = 30 and closed at the top; a second one dug below a
        // closed top and then opened; a pit next to the chunk border; then random changes around
        // the surface and in the shafts.
        // Meshing in between, so that every step has to bring the meshes up to date by itself.
        for (int y = 63; y >= 30; --y) {
            change({3, y, 3}, qcAirBlockId);
        }
        pumpUntilIdle(scheduler, world);
        change({3, 63, 3}, 1);
        pumpUntilIdle(scheduler, world);
        for (int y = 62; y >= 30; --y) {
            change({12, y, 12}, qcAirBlockId);
        }
        pumpUntilIdle(scheduler, world);
        change({12, 63, 12}, qcAirBlockId);
        pumpUntilIdle(scheduler, world);
        for (int y = 63; y >= 50; --y) {
            change({15, y, 7}, qcAirBlockId);
        }
        // Every section of the centre chunk looks exactly as if meshed from scratch.
        const auto verifyCurrent = [&] {
            pumpUntilIdle(scheduler, world);
            for (int section = 0; section < QcWorld::sectionCount; ++section) {
                QcMeshInput input;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        input.chunks[std::size_t(dz + 1) * 3 + std::size_t(dx + 1)] = world.chunk({dx, dz});
                    }
                }
                input.sectionIndex = section;
                input.renderTable = m_table;
                const QcChunkMesh direct = QcChunkMesher::mesh(input);
                const std::shared_ptr<const QcChunkMesh> scheduled = meshes.value(qcSectionKey({0, 0}, section));
                QCOMPARE(bool(scheduled), !direct.isEmpty());
                if (!scheduled) {
                    continue;
                }
                QCOMPARE(scheduled->opaqueIndices, direct.opaqueIndices);
                QCOMPARE(scheduled->opaqueVertices.size(), direct.opaqueVertices.size());
                for (qsizetype i = 0; i < direct.opaqueVertices.size(); ++i) {
                    QCOMPARE(scheduled->opaqueVertices[i].skyLight, direct.opaqueVertices[i].skyLight);
                }
            }
        };
        verifyCurrent();
        QRandomGenerator random(14);
        for (int i = 0; i < 120; ++i) {
            const QcBlockPos pos{random.bounded(-6, 22), random.bounded(28, 72), random.bounded(-6, 22)};
            change(pos, *world.blockAt(pos) == qcAirBlockId ? QcBlockId(1) : qcAirBlockId);
            if (i % 10 == 9) {
                pumpUntilIdle(scheduler, world);
            }
        }
        verifyCurrent();
    }

    void meshesWithEngineLightEqualTheSelfLitMesher()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QHash<quint64, std::shared_ptr<const QcChunkMesh>> meshes;
        connect(&scheduler, &QcMeshScheduler::meshReady, this,
                [&meshes](quint64 key, const std::shared_ptr<const QcChunkMesh> &mesh) { meshes.insert(key, mesh); });
        loadSquare(world, scheduler, 20);
        pumpUntilIdle(scheduler, world);
        // Torches in the neighbour chunks and next to the borders light the centre.
        for (const QcBlockPos pos : {QcBlockPos{-1, 20, 4}, QcBlockPos{16, 20, 15}, QcBlockPos{7, 20, 7}, QcBlockPos{3, 19, 3}}) {
            const QcBlockId previous = *world.blockAt(pos);
            QVERIFY(world.setBlock(pos, 3));
            scheduler.blockChanged(world, pos, previous);
        }
        pumpUntilIdle(scheduler, world);
        QcMeshInput input;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                input.chunks[std::size_t(dz + 1) * 3 + std::size_t(dx + 1)] = world.chunk({dx, dz});
            }
        }
        input.sectionIndex = 1;
        input.renderTable = m_table;
        const QcChunkMesh direct = QcChunkMesher::mesh(input);
        const std::shared_ptr<const QcChunkMesh> scheduled = meshes.value(qcSectionKey({0, 0}, 1));
        QVERIFY(scheduled);
        QCOMPARE(scheduled->opaqueVertices.size(), direct.opaqueVertices.size());
        float brightest = 0.0f;
        for (qsizetype i = 0; i < direct.opaqueVertices.size(); ++i) {
            QCOMPARE(scheduled->opaqueVertices[i].blockLight, direct.opaqueVertices[i].blockLight);
            brightest = std::max(brightest, direct.opaqueVertices[i].blockLight);
        }
        QVERIFY(brightest > 0.8f);
    }

    void poolDiscardsQueuedJobsButFinishesRunningOnes()
    {
        QcWorkerPool pool(1);
        std::mutex mutex;
        std::condition_variable released;
        bool release = false;
        std::atomic<bool> started = false;
        std::atomic<int> ran = 0;
        pool.submit([&] {
            started = true;
            std::unique_lock lock(mutex);
            released.wait(lock, [&] { return release; });
            ++ran;
        });
        QVERIFY(QTest::qWaitFor([&] { return started.load(); }, timeoutMs));
        for (int i = 0; i < 25; ++i) {
            pool.submit([&] { ++ran; });
        }
        // The blocked job runs; the 25 behind it never start.
        QCOMPARE(pool.discardQueued(), qsizetype(25));
        {
            const std::scoped_lock lock(mutex);
            release = true;
        }
        released.notify_all();
        pool.waitForIdle();
        QCOMPARE(ran.load(), 1);
        QCOMPARE(pool.discardQueued(), qsizetype(0));
    }

    void clearDropsQueuedWorkAndMeshesAgain()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QHash<quint64, std::shared_ptr<const QcChunkMesh>> meshes;
        connect(&scheduler, &QcMeshScheduler::meshReady, this,
                [&meshes](quint64 key, const std::shared_ptr<const QcChunkMesh> &mesh) { meshes.insert(key, mesh); });
        loadSquare(world, scheduler, 20);
        scheduler.pump(world);
        // Queued light and mesh jobs of the old world are dropped, running ones are discarded on arrival.
        scheduler.clear();
        QCOMPARE(scheduler.lightJobsPending(), 0);
        QCOMPARE(scheduler.jobsInFlight(), qsizetype(0));
        scheduler.waitForJobs();
        QCoreApplication::processEvents();
        meshes.clear();
        // Nothing of the old world is stuck: the same chunks mesh again from scratch.
        loadSquare(world, scheduler, 20);
        pumpUntilIdle(scheduler, world);
        QCOMPARE(meshes.size(), 1);
        QVERIFY(meshes.contains(qcSectionKey({0, 0}, 1)));
    }

    void unloadRemovesMeshesAndDropsRunningResults()
    {
        QcClientWorld world(m_content);
        QcMeshScheduler scheduler;
        scheduler.setRenderTable(m_table);
        QSignalSpy ready(&scheduler, &QcMeshScheduler::meshReady);
        QSignalSpy removed(&scheduler, &QcMeshScheduler::meshRemoved);
        loadSquare(world, scheduler, 20);
        scheduler.pump(world);
        // Unload while jobs may still be running: their results must be discarded.
        world.removeChunk({0, 0});
        scheduler.chunkUnloaded({0, 0});
        scheduler.waitForJobs();
        pumpUntilIdle(scheduler, world);
        QCOMPARE(ready.count(), 0);
        QVERIFY(removed.count() >= QcWorld::sectionCount);
    }
};

QTEST_GUILESS_MAIN(TestMeshScheduler)
#include "test_mesh_scheduler.moc"
