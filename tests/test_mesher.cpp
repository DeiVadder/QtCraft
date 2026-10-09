#include "QcChunkMesher.h"
#include "QcContentRegistry.h"
#include "QcTextureAtlas.h"

#include <QTest>
#include <QVector3D>

#include <algorithm>
#include <cmath>

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
    QcBlockId sapling = 0;
    std::shared_ptr<const QcBlockRenderTable> table;

    Fixture()
    {
        stone = *content.registerBlock(makeBlock(u"t:stone"_s, QcRenderLayer::Opaque, true));
        water = *content.registerBlock(makeBlock(u"t:water"_s, QcRenderLayer::Translucent, false, true));
        leaves = *content.registerBlock(makeBlock(u"t:leaves"_s, QcRenderLayer::Cutout, false));
        QcBlockDefinition cross = makeBlock(u"t:sapling"_s, QcRenderLayer::Cutout, false, true);
        cross.liquid = false;
        cross.shape.parts = {[] {
            QcShapePart part;
            part.crosses = {{{0.15, 0.0, 0.15}, {0.85, 1.0, 0.85}}};
            return part;
        }()};
        sapling = *content.registerBlock(cross);
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

    void crossShapedBlocksAreTwoDoubleSidedQuads()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(3, 20, 5, f.sapling);
        chunk->setBlock(3, 19, 5, f.stone);
        QcMeshInput in = f.input(chunk);
        in.sectionIndex = 1;
        const QcChunkMesh mesh = QcChunkMesher::mesh(in);
        // Four quads of the plant plus all six faces of the stone below: the plant hides nothing.
        QCOMPARE(quadCount(mesh.opaqueIndices), 4 + 6);
        int plantVertices = 0;
        for (const QcBlockVertex &v : mesh.opaqueVertices) {
            // Stone corners lie on whole coordinates, the inset plant corners strictly inside.
            if (v.x > 3.0f && v.x < 4.0f) {
                ++plantVertices;
                QVERIFY(v.z > 5.0f && v.z < 6.0f);
                QVERIFY(v.y == 20.0f || v.y == 21.0f);
            }
        }
        QCOMPARE(plantVertices, 16);
        // Each diagonal is drawn once from each side: the triangles of a pair face opposite ways.
        const auto normal = [&mesh](int quad) {
            const quint32 *index = mesh.opaqueIndices.constData() + qsizetype(quad) * 6;
            const QVector3D a = position(mesh.opaqueVertices[index[0]]);
            const QVector3D b = position(mesh.opaqueVertices[index[1]]);
            const QVector3D c = position(mesh.opaqueVertices[index[2]]);
            return QVector3D::crossProduct(b - a, c - a).normalized();
        };
        int pairs = 0;
        for (int i = 0; i < 10; ++i) {
            for (int j = i + 1; j < 10; ++j) {
                if (std::abs(normal(i).y()) < 0.01f && (normal(i) + normal(j)).length() < 0.01f) {
                    ++pairs;
                }
            }
        }
        QVERIFY(pairs >= 2);
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

    void flowingLiquidSurfacesFollowTheirLevel()
    {
        // Water with levels 0..7 (source to farthest flow) and a falling flag, plus stone.
        QcContentRegistry content;
        const QcBlockId stone = *content.registerBlock(makeBlock(u"t:stone"_s, QcRenderLayer::Opaque, true));
        QcBlockDefinition water = makeBlock(u"t:water"_s, QcRenderLayer::Translucent, false, true);
        QcBlockProperty level;
        level.name = u"level"_s;
        for (int i = 0; i <= 7; ++i) {
            level.values.append(QString::number(i));
        }
        water.properties = {level, QcBlockProperty{u"falling"_s, {u"false"_s, u"true"_s}}};
        water.flow.distance = 7;
        const QcBlockId source = *content.registerBlock(water);
        const QcBlockId flowing = content.withProperty(source, u"level"_s, u"3"_s);
        const QcBlockId falling = content.withProperty(content.withProperty(source, u"level"_s, u"1"_s), u"falling"_s, u"true"_s);
        const auto table = std::make_shared<const QcBlockRenderTable>(content, QcTextureAtlas::build(content));
        const float full = 1.0f - QcChunkMesher::liquidSurfaceDrop;
        QCOMPARE(table->entry(source).liquidHeight, full);
        QCOMPARE(table->entry(flowing).liquidHeight, full * (1.0f - 3.0f / 8.0f));
        QCOMPARE(table->entry(falling).liquidHeight, full);

        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 1, 5, stone);
        chunk->setBlock(6, 1, 5, stone);
        chunk->setBlock(5, 2, 5, source);
        chunk->setBlock(6, 2, 5, flowing);
        QcMeshInput in;
        for (int i = 0; i < 9; ++i) {
            in.chunks[std::size_t(i)] = std::make_shared<QcChunk>(QcChunkPos{i % 3 - 1, i / 3 - 1});
        }
        in.chunks[4] = chunk;
        in.renderTable = table;
        const QcChunkMesh mesh = QcChunkMesher::mesh(in);
        // Source: top, north, south, west and the step above the lower flow on its east side;
        // flow: top, north, south, east (its west side is below the source's surface).
        QCOMPARE(quadCount(mesh.translucentIndices), 9);
        const float low = 2.0f + table->entry(flowing).liquidHeight;
        int stepCorners = 0;
        for (const QcBlockVertex &v : mesh.translucentVertices) {
            QVERIFY(v.y <= 2.0f + full);
            // The step quad lies in the plane x = 6 between the two surfaces.
            if (v.x == 6.0f && v.z >= 5.0f && v.z <= 6.0f && (v.y == low || v.y == 2.0f + full)) {
                ++stepCorners;
            }
        }
        QVERIFY(stepCorners >= 4);
        // Under falling water the source is filled to the top and shows no surface of its own.
        chunk->setBlock(5, 3, 5, falling);
        const QcChunkMesh covered = QcChunkMesher::mesh(in);
        float highest = 0.0f;
        for (const QcBlockVertex &v : covered.translucentVertices) {
            highest = std::max(highest, v.y);
        }
        QCOMPARE(highest, 3.0f + full);
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

    void openSkyIsFullyLit()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 10, 5, f.stone);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        for (qsizetype quad = 0; quad < mesh.opaqueVertices.size(); quad += 4) {
            const bool bottomFace = std::all_of(mesh.opaqueVertices.begin() + quad, mesh.opaqueVertices.begin() + quad + 4,
                                                [](const QcBlockVertex &v) { return v.y == 10.0f; });
            for (qsizetype i = quad; i < quad + 4; ++i) {
                // The cell below the block is in its shadow: level 14 from the side instead of 15.
                const float light = mesh.opaqueVertices[i].skyLight;
                if (bottomFace) {
                    QVERIFY(light < 1.0f && light > 0.9f);
                } else {
                    QCOMPARE(light, 1.0f);
                }
            }
        }
    }

    void closedRoomIsDarkAndOverhangIsDimmed()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        // Hollow 5 x 5 x 5 stone box: its inner floor gets no sky light at all.
        for (int x = 2; x <= 6; ++x) {
            for (int y = 0; y <= 4; ++y) {
                for (int z = 2; z <= 6; ++z) {
                    const bool shell = x == 2 || x == 6 || y == 0 || y == 4 || z == 2 || z == 6;
                    if (shell) {
                        chunk->setBlock(x, y, z, f.stone);
                    }
                }
            }
        }
        // A roof over open ground at x 10..14: light reaches underneath only sideways.
        for (int x = 9; x <= 15; ++x) {
            for (int z = 0; z <= 15; ++z) {
                chunk->setBlock(x, 0, z, f.stone);
                chunk->setBlock(x, 3, z, f.stone);
            }
        }
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        bool sawInside = false;
        bool sawOverhang = false;
        for (const QcBlockVertex &v : mesh.opaqueVertices) {
            if (v.y == 1.0f && v.x >= 3.0f && v.x <= 6.0f && v.z >= 3.0f && v.z <= 6.0f) {
                QCOMPARE(v.skyLight, 0.0f);
                sawInside = true;
            }
            if (v.y == 1.0f && v.x == 12.0f && v.z == 8.0f) {
                // Three blocks in from the open side at x = 9: level 15 - 3 steps, minus the light lost
                // in averaging, but clearly between dark and full sky.
                QVERIFY2(v.skyLight > 0.5f && v.skyLight < 1.0f, qPrintable(QString::number(v.skyLight)));
                sawOverhang = true;
            }
        }
        QVERIFY(sawInside);
        QVERIFY(sawOverhang);
    }

    void lightCrossesChunkBordersWithExactLevels()
    {
        const Fixture f;
        // Floor at y = 16 and roof at y = 20 over all nine chunks, except that the western column of
        // chunks has no roof for local x < 8. Under the roof light enters only from that open strip.
        QcMeshInput in;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto chunk = std::make_shared<QcChunk>(QcChunkPos{dx, dz});
                for (int x = 0; x < 16; ++x) {
                    for (int z = 0; z < 16; ++z) {
                        chunk->setBlock(x, 16, z, f.stone);
                        if (dx != -1 || x >= 8) {
                            chunk->setBlock(x, 20, z, f.stone);
                        }
                    }
                }
                const int index = (dz + 1) * 3 + (dx + 1);
                in.chunks[std::size_t(index)] = chunk;
            }
        }
        in.renderTable = f.table;
        in.sectionIndex = 1;
        const QcChunkMesh mesh = QcChunkMesher::mesh(in);
        // The floor top face at y = 17 under the roof; the open sky column nearest to the centre chunk
        // is west x = 7 (world x = -9). Light falls straight down there (level 15 at y = 17), then
        // spreads east: world x = -8 is 14, x = 0 is 6, x = 3 is 3.
        const auto expectedLevel = [](int worldX) { return std::max(0, 15 - (worldX + 9)); };
        int checked = 0;
        for (qsizetype quad = 0; quad < mesh.opaqueVertices.size(); quad += 4) {
            const QcBlockVertex &first = mesh.opaqueVertices[quad];
            bool floorTop = true;
            for (qsizetype i = quad; i < quad + 4; ++i) {
                floorTop = floorTop && mesh.opaqueVertices[i].y == 17.0f;
            }
            if (!floorTop || first.z < 4.0f || first.z > 12.0f) {
                continue;
            }
            // The smooth light of a corner averages the four cells around it; along a row of equal
            // z the two cells per x column give (level(x - 1) + level(x)) / 2 for the corner at x.
            for (qsizetype i = quad; i < quad + 4; ++i) {
                const QcBlockVertex &v = mesh.opaqueVertices[i];
                const int cornerX = int(v.x);
                const double expected = (expectedLevel(cornerX - 1) + expectedLevel(cornerX)) / 2.0 / 15.0;
                QVERIFY2(std::abs(v.skyLight - expected) < 1e-6,
                         qPrintable(u"x %1: %2 vs %3"_s.arg(v.x).arg(v.skyLight).arg(expected)));
                ++checked;
            }
        }
        QVERIFY(checked >= 16);
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
