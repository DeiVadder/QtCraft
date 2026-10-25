#include "clientcore/meshing/QcChunkMesher.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcWorldMesh.h"

#include <QMap>
#include <QTest>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <optional>

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
    QcBlockId glass = 0;
    QcBlockId sapling = 0;
    QcBlockId ice = 0;
    std::shared_ptr<const QcBlockRenderTable> table;

    Fixture()
    {
        stone = *content.registerBlock(makeBlock(u"t:stone"_s, QcRenderLayer::Opaque, true));
        water = *content.registerBlock(makeBlock(u"t:water"_s, QcRenderLayer::Translucent, false, true));
        leaves = *content.registerBlock(makeBlock(u"t:leaves"_s, QcRenderLayer::Cutout, false));
        glass = *content.registerBlock(makeBlock(u"t:glass"_s, QcRenderLayer::Translucent, false));
        QcBlockDefinition cross = makeBlock(u"t:sapling"_s, QcRenderLayer::Cutout, false, true);
        cross.liquid = false;
        cross.shape.parts = {[] {
            QcShapePart part;
            part.crosses = {{{0.15, 0.0, 0.15}, {0.85, 1.0, 0.85}}};
            return part;
        }()};
        sapling = *content.registerBlock(cross);
        ice = *content.registerBlock(makeBlock(u"t:ice"_s, QcRenderLayer::Translucent, false));
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
                    in.chunks[static_cast<std::size_t>(index)] = std::make_shared<QcChunk>(QcChunkPos{dx, dz});
                }
            }
        }
        in.renderTable = table;
        return in;
    }
};

int quadCount(const QList<quint32> &indices)
{
    return static_cast<int>(indices.size() / 6);
}

QVector3D position(const QcBlockVertex &v)
{
    return {v.x, v.y, v.z};
}

// Quads of a vertex list that face `face`.
int facesTowards(const QList<QcBlockVertex> &vertices, QcBlockFace face)
{
    int count = 0;
    for (qsizetype i = 0; i < vertices.size(); i += 4) {
        count += vertices[i].normal() == std::optional<QcBlockFace>(face) ? 1 : 0;
    }
    return count;
}

} // namespace

class TestMesher : public QObject
{
    Q_OBJECT

private slots:
    void vertexBytesKeepLightLevelsExact()
    {
        // Light levels k / 15 survive the byte packing exactly (17 k of 255); other values within
        // half a step; out-of-range values are clamped.
        for (int level = 0; level <= 15; ++level) {
            QcBlockVertex vertex;
            vertex.setSkyLight(static_cast<float>(level) / 15.0f);
            vertex.setBlockLight(static_cast<float>(level) / 15.0f);
            QCOMPARE(vertex.light[QcBlockVertex::skyLightIndex], quint8(17 * level));
            QCOMPARE(vertex.skyLight(), float(level) / 15.0f);
            QCOMPARE(vertex.blockLight(), float(level) / 15.0f);
        }

        QcBlockVertex vertex;
        QCOMPARE(vertex.shade(), 1.0f);
        QCOMPARE(vertex.skyLight(), 1.0f);
        QCOMPARE(vertex.blockLight(), 0.0f);

        vertex.setShade(0.65f * 0.45f);
        QCOMPARE(vertex.light[QcBlockVertex::shadeIndex], quint8(75)); // 0.2925 × 255 = 74.6
        vertex.setShade(1.5f);
        QCOMPARE(vertex.shade(), 1.0f);
        vertex.setBlockLight(-0.2f);
        QCOMPARE(vertex.blockLight(), 0.0f);
    }

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
        // Relative to the section's corner at y 16 (decision E-883).
        QVERIFY(mesh.origin == (QcBlockPos{0, 16, 0}));
        for (const QcBlockVertex &v : mesh.opaqueVertices) {
            QVERIFY(v.x == 3.0f || v.x == 4.0f);
            QVERIFY(v.y == 4.0f || v.y == 5.0f);
            QVERIFY(v.z == 5.0f || v.z == 6.0f);
        }
    }

    void farSectionsKeepSmallExactPositions()
    {
        // At the bound of the world a float holds a block coordinate only to 128 m; the vertices
        // are relative to the section's corner, which the mesh carries (decision E-883).
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{QcWorld::maxHorizontalChunk, -QcWorld::maxHorizontalChunk});
        chunk->setBlock(3, 20, 5, f.stone);
        QcMeshInput in = f.input(chunk);
        in.sectionIndex = 1;
        const QcChunkMesh mesh = QcChunkMesher::mesh(in);
        QVERIFY(mesh.origin == (QcBlockPos{QcWorld::maxHorizontalBlock, 16, -QcWorld::maxHorizontalBlock}));
        QCOMPARE(quadCount(mesh.opaqueIndices), 6);
        for (const QcBlockVertex &v : mesh.opaqueVertices) {
            QVERIFY(v.x == 3.0f || v.x == 4.0f);
            QVERIFY(v.y == 4.0f || v.y == 5.0f);
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

        // Four quads of the plant in the cut-out list, all six faces of the stone below in the
        // solid one: the plant hides nothing.
        QCOMPARE(quadCount(mesh.opaqueIndices), 6);
        QCOMPARE(quadCount(mesh.cutoutIndices), 4);
        int plantVertices = 0;
        for (const QcBlockVertex &v : mesh.cutoutVertices) {
            // Stone corners lie on whole coordinates, the inset plant corners strictly inside.
            if (v.x > 3.0f && v.x < 4.0f) {
                ++plantVertices;
                QVERIFY(v.z > 5.0f && v.z < 6.0f);
                QVERIFY(v.y == 4.0f || v.y == 5.0f);
            }
        }
        QCOMPARE(plantVertices, 16);

        // Each diagonal is drawn once from each side: the triangles of a pair face opposite ways.
        const auto normal = [&mesh](int quad) {
            const quint32 *index = mesh.cutoutIndices.constData() + static_cast<qsizetype>(quad) * 6;
            const QVector3D a = position(mesh.cutoutVertices[index[0]]);
            const QVector3D b = position(mesh.cutoutVertices[index[1]]);
            const QVector3D c = position(mesh.cutoutVertices[index[2]]);
            return QVector3D::crossProduct(b - a, c - a).normalized();
        };

        int pairs = 0;
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
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

    void bottomLayerHasItsUndersideForAViewFromTheVoid()
    {
        // Decision E-288: below the world is air, so a block at y = 0 shows all six faces,
        // including the one facing down at y = 0. That one is kept in the underside lists, which
        // the renderer draws only for a camera below the world.
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(3, 0, 5, f.stone);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        QCOMPARE(quadCount(mesh.opaqueIndices), 5);
        QCOMPARE(quadCount(mesh.undersideIndices), 1);
        QVERIFY(!mesh.isEmpty());

        int bottomCorners = 0;
        for (const QcBlockVertex &v : mesh.opaqueVertices) {
            bottomCorners += v.y == 0.0f ? 1 : 0;
        }
        // Four side faces with two bottom corners each.
        QCOMPARE(bottomCorners, 4 * 2);
        QCOMPARE(mesh.undersideVertices.size(), 4);
        for (const QcBlockVertex &v : mesh.undersideVertices) {
            QCOMPARE(v.y, 0.0f);
        }
    }

    void buriedBottomSectionHasOnlyItsUnderside()
    {
        // A section filled up completely (bedrock and stone of a normal world, the lower layers of a
        // flat world) shows no face from above the world: only the underside towards the void.
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        for (int y = 0; y < QcWorld::sectionHeight + 1; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    chunk->setBlock(x, y, z, f.stone);
                }
            }
        }
        // The same full chunk all around: no border faces either.
        QcMeshInput in = f.input(chunk);
        for (auto &neighbour : in.chunks) {
            neighbour = chunk;
        }
        const QcChunkMesh mesh = QcChunkMesher::mesh(in);
        QVERIFY(mesh.opaqueIndices.isEmpty());
        QVERIFY(mesh.translucentIndices.isEmpty());
        QCOMPARE(quadCount(mesh.undersideIndices), QcWorld::chunkSize * QcWorld::chunkSize);
    }

    void undersideHoldsOnlyNonTranslucentFacesLookingIntoTheVoid()
    {
        // Decision E-330, per render layer at y = 0. Downward-facing quads of a list.
        const auto facesDown = [](const QList<QcBlockVertex> &vertices) {
            int count = 0;
            for (qsizetype i = 0; i + 3 < vertices.size(); i += 4) {
                const QVector3D normal = QVector3D::crossProduct(position(vertices[i + 1]) - position(vertices[i]),
                                                                 position(vertices[i + 2]) - position(vertices[i]));
                count += normal.y() < -0.5f && vertices[i].y == 0.0f ? 1 : 0;
            }
            return count;
        };

        // A cross plant has no down face: nothing goes to the underside, its four quads stay.
        const Fixture f;
        auto plant = std::make_shared<QcChunk>();
        plant->setBlock(3, 0, 5, f.sapling);
        const QcChunkMesh plantMesh = QcChunkMesher::mesh(f.input(plant));
        QVERIFY(plantMesh.undersideIndices.isEmpty());
        QCOMPARE(quadCount(plantMesh.cutoutIndices), 4);

        // Leaves (cutout) keep their down face in the cut-out list: the underside is drawn without
        // alpha test (decision E-756).
        auto leaves = std::make_shared<QcChunk>();
        leaves->setBlock(3, 0, 5, f.leaves);
        const QcChunkMesh leavesMesh = QcChunkMesher::mesh(f.input(leaves));
        QVERIFY(leavesMesh.undersideIndices.isEmpty());
        QVERIFY(leavesMesh.opaqueIndices.isEmpty());
        QCOMPARE(quadCount(leavesMesh.cutoutIndices), 6);
        QCOMPARE(facesDown(leavesMesh.cutoutVertices), 1);

        // Glass (translucent) keeps its down face in the translucent list, sorted with the rest.
        auto glass = std::make_shared<QcChunk>();
        glass->setBlock(3, 0, 5, f.glass);
        const QcChunkMesh glassMesh = QcChunkMesher::mesh(f.input(glass));
        QVERIFY(glassMesh.undersideIndices.isEmpty());
        QVERIFY(glassMesh.opaqueIndices.isEmpty());
        QCOMPARE(quadCount(glassMesh.translucentIndices), 6);
        QCOMPARE(facesDown(glassMesh.translucentVertices), 1);
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

        // Two water blocks: 12 faces, minus 2 shared, minus the bottom of the block above stone. Water
        // has lists of its own, apart from the other translucent faces (decision E-390).
        QCOMPARE(quadCount(mesh.waterIndices), 9);
        QVERIFY(mesh.translucentIndices.isEmpty());
        float highest = 0.0f;
        for (const QcBlockVertex &v : mesh.waterVertices) {
            highest = std::max(highest, v.y);
        }
        // In the open the surface also slopes down towards the air beside it (decision E-885): the
        // highest corner is the one the two sources share, with air on its north side (both sources
        // weigh ten times, the two air cells once).
        const float full = 1.0f - QcChunkMesher::liquidSurfaceDrop;
        QCOMPARE(highest, 2.0f + 20.0f * full / 22.0f);
    }

    // Two sources on either side of a chunk border: each chunk's mesh reads the other's water, so
    // their surfaces meet on the border at the same height (decision E-885).
    void waterSurfacesMeetAcrossAChunkBorder()
    {
        const Fixture f;
        auto west = std::make_shared<QcChunk>();
        west->setBlock(15, 2, 5, f.water);
        auto east = std::make_shared<QcChunk>(QcChunkPos{1, 0});
        east->setBlock(0, 2, 5, f.water);
        QcMeshInput westInput = f.input(west);
        westInput.chunks[5] = east;
        QcMeshInput eastInput = f.input(east);
        eastInput.chunks[3] = west;

        const float full = 1.0f - QcChunkMesher::liquidSurfaceDrop;
        const float shared = 2.0f + 20.0f * full / 22.0f;
        for (const QcChunkMesh &mesh : {QcChunkMesher::mesh(westInput), QcChunkMesher::mesh(eastInput)}) {
            // No side face between the two: top, bottom, north, south and the outer side.
            QCOMPARE(quadCount(mesh.waterIndices), 5);
            float low = 99.0f;
            float high = -99.0f;
            for (const QcBlockVertex &v : mesh.waterVertices) {
                low = std::min(low, v.x);
                high = std::max(high, v.x);
            }
            // The border is the side of the cell towards the other chunk: x = 16 in the west chunk,
            // the cell's low side in the east chunk.
            const float border = high - low == 1.0f && low == 15.0f ? high : low;
            int corners = 0;
            for (const QcBlockVertex &v : mesh.waterVertices) {
                if (v.x == border && v.y > 2.0f) {
                    QCOMPARE(v.y, shared);
                    ++corners;
                }
            }
            QVERIFY(corners >= 2);
        }
    }

    // The cells a liquid slopes down into (QcBlockRenderTable::Entry::liquidMayEnter): air and blocks
    // without collision; not solid blocks (also invisible ones, a barrier), liquids or unknown ids.
    void liquidsSlopeOnlyIntoOpenCells()
    {
        QcContentRegistry content;
        QcBlockDefinition torch = makeBlock(u"t:torch"_s, QcRenderLayer::Cutout, false);
        torch.solid = false;
        const QcBlockId torchId = *content.registerBlock(torch);
        const QcBlockId barrier = *content.registerBlock(makeBlock(u"t:barrier"_s, QcRenderLayer::Invisible, false));
        const QcBlockId stone = *content.registerBlock(makeBlock(u"t:stone"_s, QcRenderLayer::Opaque, true));
        const QcBlockId water = *content.registerBlock(makeBlock(u"t:water"_s, QcRenderLayer::Translucent, false, true));
        const QcBlockRenderTable table(content, QcTextureAtlas::build(content));
        QVERIFY(table.entry(qcAirBlockId).liquidMayEnter);
        QVERIFY(table.entry(torchId).liquidMayEnter);
        QVERIFY(!table.entry(barrier).liquidMayEnter);
        QVERIFY(!table.entry(stone).liquidMayEnter);
        QVERIFY(!table.entry(water).liquidMayEnter);
        QVERIFY(!table.entry(static_cast<QcBlockId>(table.size()) + 5).liquidMayEnter);
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
            in.chunks[static_cast<std::size_t>(i)] = std::make_shared<QcChunk>(QcChunkPos{i % 3 - 1, i / 3 - 1});
        }
        in.chunks[4] = chunk;
        in.renderTable = table;
        const QcChunkMesh mesh = QcChunkMesher::mesh(in);

        // Source: top, north, south and west; flow: top, north, south and east. Between them no side:
        // the surfaces meet at the corners they share (decision E-885).
        QCOMPARE(quadCount(mesh.waterIndices), 8);
        std::optional<float> shared;
        bool sharedAgrees = true;
        float far = 3.0f;
        for (const QcBlockVertex &v : mesh.waterVertices) {
            QVERIFY(v.y <= 2.0f + full);
            if (v.y <= 2.0f) {
                continue;
            }
            if (v.x == 6.0f && v.z == 5.0f) {
                sharedAgrees = sharedAgrees && (!shared || *shared == v.y);
                shared = v.y;
            } else if (v.x == 7.0f && v.z == 5.0f) {
                far = std::min(far, v.y);
            }
        }
        QVERIFY(shared.has_value());
        QVERIFY(sharedAgrees);
        // The surface falls from the source's side towards the flow's far edge: the shared corner
        // averages the source (ten times), the flow and two air cells, the far one the flow and two
        // air cells.
        const float flow = table->entry(flowing).liquidHeight;
        QCOMPARE(*shared, 2.0f + (10.0f * full + flow) / 13.0f);
        QCOMPARE(far, 2.0f + flow / 3.0f);

        // Under falling water the source is filled to the top and shows no surface of its own.
        chunk->setBlock(5, 3, 5, falling);
        const QcChunkMesh covered = QcChunkMesher::mesh(in);
        float highest = 0.0f;
        for (const QcBlockVertex &v : covered.waterVertices) {
            highest = std::max(highest, v.y);
        }
        // The falling water on top slopes towards the air around it: ten times its own height and
        // two air cells per corner.
        QCOMPARE(highest, 3.0f + 10.0f * full / 12.0f);
    }

    // Water next to glass: each in its own list; every vertex names its face and its material
    // (decision E-390), crosses have no face.
    void verticesCarryNormalAndMaterial()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 2, 5, f.water);
        chunk->setBlock(6, 2, 5, f.glass);
        chunk->setBlock(8, 2, 5, f.stone);
        chunk->setBlock(10, 2, 5, f.leaves);
        chunk->setBlock(12, 2, 5, f.sapling);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));

        // Glass and water: the glass side alone stands for their shared side (E-868).
        QCOMPARE(quadCount(mesh.waterIndices), 5);
        QCOMPARE(quadCount(mesh.translucentIndices), 6);
        for (const QcBlockVertex &v : mesh.waterVertices) {
            QCOMPARE(v.material(), QcSurfaceMaterial::Water);
            QVERIFY(v.normal().has_value());
        }
        for (const QcBlockVertex &v : mesh.translucentVertices) {
            QCOMPARE(v.material(), QcSurfaceMaterial::Translucent);
        }

        // Solid faces in the opaque list, cut-out faces and plants in the cut-out list (E-756).
        QMap<QcSurfaceMaterial, int> materials;
        for (const QList<QcBlockVertex> *vertices : {&mesh.opaqueVertices, &mesh.cutoutVertices}) {
            for (qsizetype i = 0; i < vertices->size(); i += 4) {
                const QcBlockVertex &first = (*vertices)[i];
                ++materials[first.material()];
                if (first.material() == QcSurfaceMaterial::Plant) {
                    QVERIFY(!first.normal());
                    continue;
                }
                // The normal is the face's direction: all four corners lie in its plane.
                const QcBlockFace face = *first.normal();
                const QcFaceOffset offset = qcFaceOffset(face);
                for (qsizetype k = i; k < i + 4; ++k) {
                    QCOMPARE((*vertices)[k].normal(), std::optional<QcBlockFace>(face));
                    QCOMPARE((*vertices)[k].x * float(offset.dx), first.x * float(offset.dx));
                    QCOMPARE((*vertices)[k].y * float(offset.dy), first.y * float(offset.dy));
                    QCOMPARE((*vertices)[k].z * float(offset.dz), first.z * float(offset.dz));
                }
            }
        }
        QCOMPARE(mesh.opaqueVertices.size(), 24);
        QCOMPARE(materials.value(QcSurfaceMaterial::Solid), 6);
        QCOMPARE(materials.value(QcSurfaceMaterial::Cutout), 6);
        QCOMPARE(materials.value(QcSurfaceMaterial::Plant), 4);

        // The surface byte does not disturb the light bytes.
        QcBlockVertex vertex;
        vertex.setSkyLight(0.4f);
        vertex.setSurface(QcBlockFace::North, QcSurfaceMaterial::Emissive);
        QCOMPARE(vertex.normal(), std::optional<QcBlockFace>(QcBlockFace::North));
        QCOMPARE(vertex.material(), QcSurfaceMaterial::Emissive);
        QCOMPARE(vertex.light[QcBlockVertex::skyLightIndex], QcBlockVertex::pack(0.4f));
        QCOMPARE(QcBlockVertex().normal(), std::nullopt);
    }

    // Water, glass and water again in cell order: the runs keep that order for drawing water and
    // glass in turn (decision E-390); all water before all glass needs no runs.
    void translucentRunsKeepTheMeshersOrder()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(2, 2, 5, f.water);
        chunk->setBlock(4, 2, 5, f.glass);
        chunk->setBlock(6, 2, 5, f.water);
        const QcChunkMesh mixed = QcChunkMesher::mesh(f.input(chunk));
        QCOMPARE(mixed.translucentRuns, (QList<quint32>{36, 36, 36}));
        QCOMPARE(mixed.waterIndices.size(), 72);

        chunk->setBlock(2, 2, 5, f.glass);
        chunk->setBlock(6, 2, 5, f.glass);
        chunk->setBlock(4, 2, 5, f.water);
        QCOMPARE(QcChunkMesher::mesh(f.input(chunk)).translucentRuns, (QList<quint32>{0, 36, 36, 36}));

        chunk->setBlock(6, 2, 5, qcAirBlockId);
        chunk->setBlock(2, 2, 5, qcAirBlockId);
        chunk->setBlock(4, 3, 5, f.glass);
        QVERIFY(QcChunkMesher::mesh(f.input(chunk)).translucentRuns.isEmpty());
    }

    // Ice or glass on water: the water surface alone stands for the boundary, so no two unsorted
    // translucent faces blend in mesh order (decision E-868); the side faces stay.
    void translucentBlockOnWaterLeavesTheBoundaryToTheWater()
    {
        const Fixture f;
        for (const QcBlockId block : {f.glass, f.ice}) {
            auto chunk = std::make_shared<QcChunk>();
            chunk->setBlock(5, 2, 5, f.water);
            chunk->setBlock(5, 3, 5, block);
            chunk->setBlock(8, 3, 5, f.water);
            chunk->setBlock(8, 2, 5, block);
            const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
            QCOMPARE(facesTowards(mesh.translucentVertices, QcBlockFace::Down), 1);
            QCOMPARE(facesTowards(mesh.translucentVertices, QcBlockFace::Up), 1);
            QCOMPARE(facesTowards(mesh.translucentVertices, QcBlockFace::East), 2);
            QCOMPARE(facesTowards(mesh.waterVertices, QcBlockFace::Up), 2);
            QCOMPARE(facesTowards(mesh.waterVertices, QcBlockFace::Down), 2);
        }
    }

    // Which faces next to liquids drop, by render layer and water level (E-868): only translucent
    // blocks give way to water, only to translucent water, below only to a full surface; at the
    // shore the block's side stands for the water's.
    void facesNextToLiquidsFollowLayerAndLevel()
    {
        QcContentRegistry content;
        const auto add = [&content](const QString &name, QcRenderLayer layer, bool opaque, bool liquid = false) {
            return *content.registerBlock(makeBlock(name, layer, opaque, liquid));
        };
        const QcBlockId stone = add(u"t:stone"_s, QcRenderLayer::Opaque, true);
        const QcBlockId leaves = add(u"t:leaves"_s, QcRenderLayer::Cutout, false);
        const QcBlockId ice = add(u"t:ice"_s, QcRenderLayer::Translucent, false);
        const QcBlockId lava = add(u"t:lava"_s, QcRenderLayer::Cutout, false, true);
        QcBlockDefinition water = makeBlock(u"t:water"_s, QcRenderLayer::Translucent, false, true);
        water.properties = {QcBlockProperty{u"level"_s, {u"0"_s, u"1"_s, u"2"_s, u"3"_s}}};
        water.flow.distance = 3;
        const QcBlockId source = *content.registerBlock(water);
        const QcBlockId flowing = content.withProperty(source, u"level"_s, u"2"_s);
        const QcBlockRenderTable table(content, QcTextureAtlas::build(content));
        const auto hides = [&table](QcBlockId self, QcBlockFace face, QcBlockId neighbour) {
            return QcBlockRenderTable::hides(table.entry(self), {}, face, table.entry(neighbour));
        };

        QVERIFY(!hides(stone, QcBlockFace::Up, source));
        QVERIFY(!hides(leaves, QcBlockFace::Up, source));
        QVERIFY(!hides(ice, QcBlockFace::Up, lava));
        QVERIFY(!hides(ice, QcBlockFace::Down, lava));
        QVERIFY(hides(ice, QcBlockFace::Up, flowing));
        QVERIFY(hides(ice, QcBlockFace::Down, source));
        QVERIFY(!hides(ice, QcBlockFace::Down, flowing));

        QVERIFY(!hides(ice, QcBlockFace::West, source));
        QVERIFY(hides(source, QcBlockFace::East, ice));
        QVERIFY(hides(flowing, QcBlockFace::East, ice));
        QVERIFY(!hides(source, QcBlockFace::East, leaves));
        QVERIFY(!hides(lava, QcBlockFace::East, ice));
    }

    // The same across the border of two sections: water at y = 15, ice at y = 16.
    void translucentBlockOnWaterAcrossASectionBorder()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 15, 5, f.water);
        chunk->setBlock(5, 16, 5, f.ice);
        QcMeshInput upper = f.input(chunk);
        upper.sectionIndex = 1;
        const QcChunkMesh ice = QcChunkMesher::mesh(upper);
        QCOMPARE(quadCount(ice.translucentIndices), 5);
        QCOMPARE(facesTowards(ice.translucentVertices, QcBlockFace::Down), 0);
        const QcChunkMesh water = QcChunkMesher::mesh(f.input(chunk));
        QCOMPARE(facesTowards(water.waterVertices, QcBlockFace::Up), 1);
    }

    // Ice next to ice keeps no inner faces, also across chunk and section borders.
    void iceNextToIceAcrossBordersHasNoInnerFaces()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(15, 2, 5, f.ice);
        QcMeshInput in = f.input(chunk);
        auto east = std::make_shared<QcChunk>(QcChunkPos{1, 0});
        east->setBlock(0, 2, 5, f.ice);
        in.chunks[5] = east;
        const QcChunkMesh acrossChunks = QcChunkMesher::mesh(in);
        QCOMPARE(quadCount(acrossChunks.translucentIndices), 5);
        QCOMPARE(facesTowards(acrossChunks.translucentVertices, QcBlockFace::East), 0);

        auto column = std::make_shared<QcChunk>();
        column->setBlock(5, 15, 5, f.ice);
        column->setBlock(5, 16, 5, f.ice);
        const QcChunkMesh lower = QcChunkMesher::mesh(f.input(column));
        QCOMPARE(quadCount(lower.translucentIndices), 5);
        QCOMPARE(facesTowards(lower.translucentVertices, QcBlockFace::Up), 0);
        QcMeshInput upper = f.input(column);
        upper.sectionIndex = 1;
        QCOMPARE(facesTowards(QcChunkMesher::mesh(upper).translucentVertices, QcBlockFace::Down), 0);
    }

    void cutoutBlocksKeepFacesBetweenEachOther()
    {
        const Fixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 2, 5, f.leaves);
        chunk->setBlock(6, 2, 5, f.leaves);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        QCOMPARE(quadCount(mesh.cutoutIndices), 12);
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
                const float light = mesh.opaqueVertices[i].skyLight();
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
                QCOMPARE(v.skyLight(), 0.0f);
                sawInside = true;
            }
            if (v.y == 1.0f && v.x == 12.0f && v.z == 8.0f) {
                // Three blocks in from the open side at x = 9: level 15 - 3 steps, minus the light lost
                // in averaging, but clearly between dark and full sky.
                QVERIFY2(v.skyLight() > 0.5f && v.skyLight() < 1.0f, qPrintable(QString::number(v.skyLight())));
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
                in.chunks[static_cast<std::size_t>(index)] = chunk;
            }
        }

        in.renderTable = f.table;
        in.sectionIndex = 1;
        // In world coordinates: the checks below name world heights.
        const QcChunkMesh mesh = qcWorldMesh(in);

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
                const int cornerX = static_cast<int>(v.x);
                const double expected = (expectedLevel(cornerX - 1) + expectedLevel(cornerX)) / 2.0 / 15.0;
                // Light is stored as a byte (value × 255), exact to half a step.
                QVERIFY2(std::abs(v.skyLight() - expected) <= 0.5 / 255.0 + 1e-6,
                         qPrintable(u"x %1: %2 vs %3"_s.arg(v.x).arg(v.skyLight()).arg(expected)));
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
                    // Stored as a byte (value × 255), exact to half a step.
                    QVERIFY2(std::abs(v.shade() - 0.82f) <= 0.5f / 255.0f + 1e-6f, qPrintable(QString::number(v.shade())));
                    ++occluded;
                } else if (v.z == 2.0f) {
                    QCOMPARE(v.shade(), 1.0f);
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
