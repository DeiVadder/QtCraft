#include "QcChunkMesher.h"
#include "QcClientWorld.h"
#include "QcMeshScheduler.h"
#include "QcSectionKey.h"
#include "QcTextureAtlas.h"

#include <QSignalSpy>
#include <QTest>

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
                return scheduler.dirtyCount() == 0 && scheduler.jobsInFlight() == 0;
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
        scheduler.blockChanged({5, 15, 5});
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

        // Opening a hole at the top of section 3 changes the light down to two sections below; those
        // sections are meshed again (neighbour chunks are marked too but lack side neighbours here).
        QVERIFY(world.setBlock({15, 63, 15}, qcAirBlockId));
        scheduler.blockChanged({15, 63, 15});
        // Only the centre chunk has all side neighbours, so only its sections produce results.
        pumpUntilIdle(scheduler, world);
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 1)));
        QVERIFY(meshed.contains(qcSectionKey({0, 0}, 3)));
        QVERIFY(!meshed.contains(qcSectionKey({0, 0}, 6)));
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
