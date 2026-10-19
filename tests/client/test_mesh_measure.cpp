#include "clientcore/light/QcBlockLightEngine.h"
#include "clientcore/meshing/QcChunkMesher.h"
#include "clientcore/meshing/QcIndexPacking.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <unordered_map>

using namespace Qt::StringLiterals;

// Headless measurement of the section meshes without a window and without real time: how long the
// mesher takes per section and how many bytes the vertex and index buffers of every list take as
// the renderer uploads them (QcWorldRenderer: 16-bit indices where they fit, decision E-069). Chunks
// of the terrain generator (seed 42) and their block light (QcBlockLightEngine, as the client has
// it) are prepared first, untimed; every section of the inner chunks is meshed and timed on its own.
// With QTCRAFT_SECTION_LIGHT=1 (as in the game) the meshes carry the per-cell light of their sections,
// and the measure counts what it would take in the GPU pool (decision E-390); without it, as today's
// frame plan, they do not and the light counts are 0.
// Two regions: around the origin (land) and around the first chunk under the sea on a line from it
// (coast, with water). QTCRAFT_MESH_MEASURE=full meshes 15 x 15 chunks per region (the recorded size),
// else 5 x 5 (every test run).
// Prints "MEASURE key=value ..." lines.
class TestMeshMeasure : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content;
    std::shared_ptr<const QcBlockRenderTable> m_table;

    struct Bytes
    {
        qint64 vertices = 0;
        qint64 indices = 0;
        qint64 vertexCount = 0;

        void add(const QList<QcBlockVertex> &list, const QList<quint32> &indexList)
        {
            if (indexList.isEmpty()) {
                return;
            }
            vertexCount += list.size();
            vertices += list.size() * qint64(sizeof(QcBlockVertex));
            const qint64 indexSize = QcIndexPacking::fitsShortIndices(list.size()) ? 2 : 4;
            indices += indexList.size() * indexSize;
        }
        QString text(const QString &key) const
        {
            return u"%1_vertices=%2 %1_kb=%3"_s.arg(key).arg(vertexCount).arg(double(vertices + indices) / 1024.0, 0, 'f', 1);
        }
    };

    // What the meshes of a region add up to.
    struct Totals
    {
        Bytes opaque;
        Bytes translucent;
        Bytes water;
        Bytes underside;
        // Sections whose light needs a slot of the GPU pool (a channel with differing cells), sky
        // light copies the mesher makes, and sections drawn in runs of water and other faces.
        int lightSlots = 0;
        int skyCopies = 0;
        int runSections = 0;
        qint64 meshNs = 0;
        qint64 maxNs = 0;
        int sections = 0;
        int meshed = 0;

        void add(const QcChunkMesh &mesh, qint64 ns)
        {
            ++sections;
            if (mesh.isEmpty()) {
                return;
            }

            ++meshed;
            meshNs += ns;
            maxNs = std::max(maxNs, ns);
            opaque.add(mesh.opaqueVertices, mesh.opaqueIndices);
            translucent.add(mesh.translucentVertices, mesh.translucentIndices);
            water.add(mesh.waterVertices, mesh.waterIndices);
            underside.add(mesh.undersideVertices, mesh.undersideIndices);
            lightSlots += mesh.light.isUniform() ? 0 : 1;
            skyCopies += mesh.light.sky ? 1 : 0;
            runSections += mesh.translucentRuns.isEmpty() ? 0 : 1;
        }

        void report(const QString &region) const
        {
            const qint64 totalBytes = opaque.vertices + opaque.indices + translucent.vertices + translucent.indices + water.vertices
                                      + water.indices + underside.vertices + underside.indices;
            qInfo().noquote() << u"MEASURE mesh region=%7 sections=%1 meshed=%2 mesh_avg_ms=%3 mesh_max_ms=%4 buffers_mb=%5 bytes_per_meshed=%6"_s
                                     .arg(sections)
                                     .arg(meshed)
                                     .arg(double(meshNs) / 1e6 / std::max(1, meshed), 0, 'f', 4)
                                     .arg(double(maxNs) / 1e6, 0, 'f', 3)
                                     .arg(double(totalBytes) / 1048576.0, 0, 'f', 2)
                                     .arg(totalBytes / std::max(1, meshed))
                                     .arg(region);
            qInfo().noquote() << u"MEASURE lists %1 %2 %3 %4 run_sections=%5"_s
                                     .arg(opaque.text(u"opaque"_s), translucent.text(u"translucent"_s), water.text(u"water"_s),
                                          underside.text(u"underside"_s))
                                     .arg(runSections);
            // The GPU pool takes 8 KiB per slot (RG8, 16^3); a sky copy holds 2 KiB until the upload.
            qInfo().noquote()
                << u"MEASURE light slots=%1 of=%2 pool_kb=%3 sky_copies=%4"_s.arg(lightSlots).arg(meshed).arg(lightSlots * 8).arg(skyCopies);
        }
    };

    // Generated chunks of a region and their block light as the client has it.
    struct Region
    {
        explicit Region(const std::shared_ptr<const QcBlockRenderTable> &table)
            : light(table)
        {
        }

        static qint64 key(int x, int z) { return (qint64(x) << 32) ^ qint64(quint32(z)); }

        QcMeshInput input(int cx, int cz, const std::shared_ptr<const QcBlockRenderTable> &table) const
        {
            QcMeshInput result;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const std::size_t index = std::size_t(dz + 1) * 3 + std::size_t(dx + 1);
                    result.chunks[index] = chunks.at(key(cx + dx, cz + dz));
                    result.blockLight[index] = std::make_shared<const QcChunkLight>(light.snapshot({cx + dx, cz + dz}));
                }
            }
            result.renderTable = table;
            result.hasBlockLight = true;
            result.sectionLight = qEnvironmentVariableIntValue("QTCRAFT_SECTION_LIGHT") == 1;
            return result;
        }

        QcBlockLightEngine light;
        std::unordered_map<qint64, std::shared_ptr<const QcChunk>> chunks;
    };

    static bool full() { return qEnvironmentVariable("QTCRAFT_MESH_MEASURE") == u"full"_s; }

    // The first chunk on a line from the origin whose centre column lies under the sea, so the
    // second region has water.
    QcChunkPos coast(const QcTerrainGenerator &generator) const
    {
        const QcBlockId water = *m_content->blockId(u"qtcraft:water"_s);
        for (int step = 1; step < 64; ++step) {
            const QcChunkPos pos{step * 4, step * 2};
            const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
            if (chunk->block(8, 58, 8) == water) {
                return pos;
            }
        }
        return {0, 0};
    }

private slots:
    void initTestCase()
    {
        m_content = QcTestUtil::loadContent();
        m_table = std::make_shared<const QcBlockRenderTable>(*m_content, QcTextureAtlas::build(*m_content));
    }

    void sectionMeshes_data()
    {
        QTest::addColumn<bool>("atCoast");
        QTest::newRow("land") << false;
        QTest::newRow("coast") << true;
    }

    // Meshes every section of the inner chunks and sums time and buffer sizes per list.
    void sectionMeshes()
    {
        QFETCH(bool, atCoast);
        const int radius = full() ? 7 : 2;
        const QcTerrainGenerator generator(m_content, 42);
        const QcChunkPos centre = atCoast ? coast(generator) : QcChunkPos{0, 0};
        Region region(m_table);
        for (int cx = centre.x - radius - 1; cx <= centre.x + radius + 1; ++cx) {
            for (int cz = centre.z - radius - 1; cz <= centre.z + radius + 1; ++cz) {
                std::shared_ptr<const QcChunk> chunk = generator.generate({cx, cz});
                region.light.addChunk(chunk);
                region.chunks.emplace(Region::key(cx, cz), std::move(chunk));
            }
        }
        region.light.takeChanges();

        Totals totals;
        QElapsedTimer timer;
        for (int cx = centre.x - radius; cx <= centre.x + radius; ++cx) {
            for (int cz = centre.z - radius; cz <= centre.z + radius; ++cz) {
                QcMeshInput input = region.input(cx, cz, m_table);
                for (int section = 0; section < QcWorld::sectionCount; ++section) {
                    input.sectionIndex = section;
                    timer.start();
                    const QcChunkMesh mesh = QcChunkMesher::mesh(input);
                    totals.add(mesh, timer.nsecsElapsed());
                }
            }
        }

        totals.report(atCoast ? u"coast(%1,%2)"_s.arg(centre.x).arg(centre.z) : u"land"_s);
        QVERIFY(totals.meshed > 0);
        QVERIFY(totals.opaque.vertexCount > 0);
    }
};

QTEST_GUILESS_MAIN(TestMeshMeasure)
#include "test_mesh_measure.moc"
