// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcChunkMesher.h"
#include "common/world/QcLightPassage.h"
#include "fixtures/QcMesherFixture.h"

#include <QList>
#include <QMap>
#include <QRectF>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

using namespace Qt::StringLiterals;

// The faces where translucent blocks (ice, glass) meet translucent water (decisions E-868, E-959):
// one face per boundary above and below, both sides at a shore, none between equal blocks.
namespace {

// A bottom corner (0 or 1) of a block's side face and the top corner above it (3 or 2).
struct Corner
{
    QcBlockVertex bottom;
    QcBlockVertex top;
};

// The bottom corners of the translucent side faces towards `face` that start above their cell's
// bottom.
QList<Corner> raisedCorners(const QcChunkMesh &mesh, QcBlockFace face)
{
    QList<Corner> corners;
    const QList<QcBlockVertex> &vertices = mesh.translucentVertices;
    for (qsizetype quad = 0; quad + 3 < vertices.size(); quad += 4) {
        if (vertices[quad].normal() != std::optional(face)) {
            continue;
        }
        for (qsizetype k = 0; k < 2; ++k) {
            const Corner corner{vertices[quad + k], vertices[quad + 3 - k]};
            if (corner.bottom.y > corner.top.y - 1.0f) {
                corners.append(corner);
            }
        }
    }
    return corners;
}

// Every raised corner of `block`'s sides towards `face` sits on a top corner of the water's side
// facing back, and shows the texture from that height up: along v, or along u where the texture
// is turned a quarter; the other coordinate stays.
void checkShore(const QcMesherFixture &f, const QcChunkMesh &mesh, QcBlockId block, QcBlockFace face)
{
    const QcBlockFace back = QcLightPassage::opposite(face);
    // Top corner heights by position (x, z) and cell bottom (the bottom corners of a water side).
    QMap<std::tuple<float, float, float>, float> waterTop;
    const QList<QcBlockVertex> &water = mesh.waterVertices;
    for (qsizetype i = 0; i + 3 < water.size(); i += 4) {
        for (qsizetype k = 2; k < 4 && water[i].normal() == std::optional(back); ++k) {
            waterTop.insert({water[i + k].x, water[i + k].z, water[i].y}, water[i + k].y);
        }
    }
    const QcBlockRenderTable::Entry &entry = f.table->entry(block);
    const QRectF uv = entry.faceUv[static_cast<std::size_t>(face)];
    const bool turned = entry.faceUvTurns[static_cast<std::size_t>(face)] % 2 == 1;
    const QList<Corner> corners = raisedCorners(mesh, face);
    for (const Corner &corner : corners) {
        const std::tuple<float, float, float> at{corner.bottom.x, corner.bottom.z, corner.top.y - 1.0f};
        QVERIFY(waterTop.contains(at));
        QCOMPARE(corner.bottom.y, waterTop.value(at));
        const float shown = corner.top.y - corner.bottom.y;
        QCOMPARE(turned ? corner.bottom.v : corner.bottom.u, turned ? corner.top.v : corner.top.u);
        const float moved = turned ? std::abs(corner.bottom.u - corner.top.u) : std::abs(corner.bottom.v - corner.top.v);
        QCOMPARE(moved, static_cast<float>(shown * (turned ? uv.width() : uv.height())));
    }
}

} // namespace

class TestMesherShores : public QObject
{
    Q_OBJECT

private slots:
    // Ice or glass on water: the water surface alone stands for the boundary, so no two unsorted
    // translucent faces blend in mesh order (decision E-868); the side faces stay.
    void translucentBlockOnWaterLeavesTheBoundaryToTheWater()
    {
        const QcMesherFixture f;
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
    // shore both sides stay and the block's starts at the water's surface (E-959).
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
        QVERIFY(!hides(source, QcBlockFace::East, ice));
        QVERIFY(!hides(flowing, QcBlockFace::East, ice));
        QVERIFY(QcBlockRenderTable::isShore(table.entry(ice), table.entry(flowing)));
        QVERIFY(!QcBlockRenderTable::isShore(table.entry(ice), table.entry(lava)));
        QVERIFY(!QcBlockRenderTable::isShore(table.entry(source), table.entry(ice)));
        QVERIFY(!hides(source, QcBlockFace::East, leaves));
        QVERIFY(!hides(lava, QcBlockFace::East, ice));
    }

    // Water beside glass stays visible through it (decision E-959): the water keeps its side up to
    // its surface, the glass side towards it starts exactly there with the upper part of its
    // texture; under more water the glass side goes.
    void waterBesideGlassKeepsItsSide()
    {
        const QcMesherFixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 2, 5, f.water);
        chunk->setBlock(6, 2, 5, f.glass);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        QCOMPARE(raisedCorners(mesh, QcBlockFace::West).size(), 2);
        checkShore(f, mesh, f.glass, QcBlockFace::West);

        chunk->setBlock(5, 3, 5, f.water);
        QCOMPARE(facesTowards(QcChunkMesher::mesh(f.input(chunk)).translucentVertices, QcBlockFace::West), 0);
    }

    // Water whose surface is full at one corner of the shared edge (water stands on its northern
    // neighbour): the glass side stays as a triangle up to the other corner, no hole.
    void aShoreOnASlopedEdgeKeepsATriangle()
    {
        const QcMesherFixture f;
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 2, 5, f.water);
        chunk->setBlock(5, 2, 4, f.water);
        chunk->setBlock(5, 3, 4, f.water);
        chunk->setBlock(6, 2, 5, f.glass);
        const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
        const QList<Corner> corners = raisedCorners(mesh, QcBlockFace::West);
        QCOMPARE(corners.size(), 2);
        QVERIFY(std::min(corners.at(0).bottom.y, corners.at(1).bottom.y) < 3.0f);
        QCOMPARE(std::max(corners.at(0).bottom.y, corners.at(1).bottom.y), 3.0f);
        checkShore(f, mesh, f.glass, QcBlockFace::West);
    }

    // The same towards north and south, and on a block whose side texture is turned: the crop
    // follows the texture's upward direction (u there instead of v).
    void shoresTowardsNorthAndSouthAndTurnedTextures()
    {
        const QcMesherFixture f;
        QVERIFY(f.table->entry(f.glassLogAlongX).fullCube);
        QVERIFY(f.table->entry(f.glassLogAlongX).faceUvTurns[static_cast<std::size_t>(QcBlockFace::South)] != 0);
        for (const QcBlockId block : {f.glass, f.glassLogAlongX}) {
            auto chunk = std::make_shared<QcChunk>();
            chunk->setBlock(5, 2, 5, f.water);
            chunk->setBlock(5, 2, 4, block);
            chunk->setBlock(5, 2, 6, block);
            const QcChunkMesh mesh = QcChunkMesher::mesh(f.input(chunk));
            QCOMPARE(raisedCorners(mesh, QcBlockFace::South).size(), 2);
            QCOMPARE(raisedCorners(mesh, QcBlockFace::North).size(), 2);
            checkShore(f, mesh, block, QcBlockFace::South);
            checkShore(f, mesh, block, QcBlockFace::North);
        }
    }

    // The same across the border of two sections: water at y = 15, ice at y = 16.
    void translucentBlockOnWaterAcrossASectionBorder()
    {
        const QcMesherFixture f;
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
        const QcMesherFixture f;
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
};

QTEST_GUILESS_MAIN(TestMesherShores)
#include "test_mesher_shores.moc"
