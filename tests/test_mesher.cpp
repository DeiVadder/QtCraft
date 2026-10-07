#include "QcChunkMesher.h"
#include "QcContentRegistry.h"
#include "QcTextureAtlas.h"

#include <QTest>
#include <QVector3D>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QcBlockDefinition makeBlock(const QString &name, QcRenderLayer layer, bool opaque, bool liquid = false)
{
    QcBlockDefinition block;
    block.name = name;
    block.renderLayer = layer;
    block.opaque = opaque;
    block.solid = !liquid;
    block.liquid = liquid;
    block.faceTextures.fill(name);
    return block;
}

struct Fixture
{
    QcContentRegistry content;
    QcBlockId stone = 0;
    QcBlockId water = 0;
    QcBlockId leaves = 0;
    std::shared_ptr<const QcBlockRenderTable> table;

    Fixture()
    {
        stone = *content.registerBlock(makeBlock(u"t:stone"_s, QcRenderLayer::Opaque, true));
        water = *content.registerBlock(makeBlock(u"t:water"_s, QcRenderLayer::Translucent, false, true));
        leaves = *content.registerBlock(makeBlock(u"t:leaves"_s, QcRenderLayer::Cutout, false));
        table = std::make_shared<const QcBlockRenderTable>(content, QcTextureAtlas::build(content));
    }

    // Centre chunk at (0, 0) plus empty neighbours, unless `withNeighbours` is false.
    QcMeshInput input(const std::shared_ptr<QcChunk> &centre, bool withNeighbours = true) const
    {
        QcMeshInput in;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) {
                    in.chunks[4] = centre;
                } else if (withNeighbours) {
                    const int index = (dz + 1) * 3 + (dx + 1);
                    in.chunks[std::size_t(index)] = std::make_shared<QcChunk>(QcChunkPos{dx, dz});
                }
            }
        }
        in.renderTable = table;
        return in;
    }
};

int quadCount(const QList<quint32> &indices)
{
    return int(indices.size() / 6);
}

QVector3D position(const QcBlockVertex &v)
{
    return {v.x, v.y, v.z};
}

} // namespace

class TestMesher : public QObject
{
    Q_OBJECT

private slots:
    void singleBlockHasSixFacesInsideItsCube()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(3, 20, 5, f.stone);
        QcMeshInput in = f.input(chunk);
        in.sectionIndex = 1;
        const QcChunkMesh mesh = QcChunkMesher::mesh(in);
        QCOMPARE(quadCount(mesh.opaqueIndices), 6);
        QCOMPARE(mesh.opaqueVertices.size(), 24);
        QVERIFY(mesh.translucentIndices.isEmpty());
        for (const QcBlockVertex &v : mesh.opaqueVertices) {
            QVERIFY(v.x == 3.0f || v.x == 4.0f);
            QVERIFY(v.y == 20.0f || v.y == 21.0f);
            QVERIFY(v.z == 5.0f || v.z == 6.0f);
        }
    }

    void otherSectionsProduceNothing()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(3, 20, 5, f.stone);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        QVERIFY(mesh.isEmpty());
    }

    void sharedFacesBetweenOpaqueBlocksAreCulled()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(3, 2, 5, f.stone);
        chunk->setBlock(4, 2, 5, f.stone);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        QCOMPARE(quadCount(mesh.opaqueIndices), 10);
    }

    void trianglesFaceOutwards()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(8, 8, 8, f.stone);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        const QVector3D centre(8.5f, 8.5f, 8.5f);
        for (qsizetype i = 0; i < mesh.opaqueIndices.size(); i += 3) {
            const QVector3D a = position(mesh.opaqueVertices[mesh.opaqueIndices[i]]);
            const QVector3D b = position(mesh.opaqueVertices[mesh.opaqueIndices[i + 1]]);
            const QVector3D c = position(mesh.opaqueVertices[mesh.opaqueIndices[i + 2]]);
            const QVector3D normal = QVector3D::crossProduct(b - a, c - a);
            const QVector3D outward = (a + b + c) / 3.0f - centre;
            // Counter-clockwise seen from outside: the geometric normal points away from the cube centre.
            QVERIFY2(QVector3D::dotProduct(normal, outward) > 0.0f, qPrintable(QString::number(i)));
        }
    }

    void missingNeighbourChunkHidesBorderFaces()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(15, 2, 5, f.stone);
        const QcChunkMesh withNeighbours = QcChunkMesher::mesh(f.input(chunk));
        const QcChunkMesh without = QcChunkMesher::mesh(f.input(chunk, false));
        QCOMPARE(quadCount(withNeighbours.opaqueIndices), 6);
        QCOMPARE(quadCount(without.opaqueIndices), 5);
    }

    void blockInNeighbourChunkCullsBorderFace()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(15, 2, 5, f.stone);
        QcMeshInput in = f.input(chunk);
        auto east = std::make_shared<QcChunk>(QcChunkPos{1, 0});
        east->setBlock(0, 2, 5, f.stone);
        in.chunks[5] = east;
        QCOMPARE(quadCount(QcChunkMesher::mesh(in).opaqueIndices), 5);
    }

    void waterCullsAgainstWaterAndStoneAndLowersItsSurface()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 2, 5, f.water);
        chunk->setBlock(6, 2, 5, f.water);
        chunk->setBlock(5, 1, 5, f.stone);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        // Two water blocks: 12 faces, minus 2 shared, minus the bottom of the block above stone.
        QCOMPARE(quadCount(mesh.translucentIndices), 9);
        float highest = 0.0f;
        for (const QcBlockVertex &v : mesh.translucentVertices) {
            highest = std::max(highest, v.y);
        }
        QCOMPARE(highest, 3.0f - QcChunkMesher::liquidSurfaceDrop);
    }

    void cutoutBlocksKeepFacesBetweenEachOther()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 2, 5, f.leaves);
        chunk->setBlock(6, 2, 5, f.leaves);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        QCOMPARE(quadCount(mesh.opaqueIndices), 12);
    }

    void ambientOcclusionDarkensCornersNextToAWall()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        for (int x = 0; x < 3; ++x) {
            for (int z = 0; z < 3; ++z) {
                chunk->setBlock(x, 0, z, f.stone);
            }
        }
        // Wall block above and north of the floor block (1, 0, 1).
        chunk->setBlock(1, 1, 0, f.stone);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        int occluded = 0;
        int open = 0;
        // Vertices come in quads of four; floor tops are the quads lying entirely at y = 1.
        for (qsizetype quad = 0; quad < mesh.opaqueVertices.size(); quad += 4) {
            const bool floorTop = std::all_of(mesh.opaqueVertices.begin() + quad, mesh.opaqueVertices.begin() + quad + 4,
                                              [](const QcBlockVertex &v) { return v.y == 1.0f; });
            if (!floorTop) {
                continue;
            }
            for (qsizetype i = quad; i < quad + 4; ++i) {
                const QcBlockVertex &v = mesh.opaqueVertices[i];
                if (v.x < 1.0f || v.x > 2.0f) {
                    continue;
                }
                // Top faces have face shade 1.0. Corners touching the wall have one occluder: level 2 of 3.
                if (v.z == 1.0f) {
                    QCOMPARE(v.shade, 0.82f);
                    ++occluded;
                } else if (v.z == 2.0f) {
                    QCOMPARE(v.shade, 1.0f);
                    ++open;
                }
            }
        }
        QVERIFY(occluded >= 4);
        QVERIFY(open >= 4);
    }
};

QTEST_GUILESS_MAIN(TestMesher)
#include "test_mesher.moc"
