#include "QcChunkMesher.h"
#include "QcContentParser.h"
#include "QcContentRegistry.h"
#include "QcTextureAtlas.h"

#include <QJsonDocument>
#include <QTest>

#include <cmath>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

struct Fixture
{
    QcContentRegistry content;
    std::shared_ptr<const QcBlockRenderTable> table;

    Fixture()
    {
        for (const char *shape : {
                 R"({"name":"stairs","rotate":"facing","flip":"half","boxes":[[0,0,0,1,0.5,1],[0,0.5,0,1,1,0.5]]})",
                 R"({"name":"slab","parts":[{"when":{"type":"bottom"},"boxes":[[0,0,0,1,0.5,1]]},
                     {"when":{"type":"top"},"boxes":[[0,0.5,0,1,1,1]]},{"when":{"type":"double"},"boxes":[[0,0,0,1,1,1]]}]})",
                 R"({"name":"fence","parts":[{"boxes":[[0.375,0,0.375,0.625,1,0.625]]},
                     {"when":{"east":true},"boxes":[[0.625,0.375,0.4375,1,0.5625,0.5625]]},
                     {"when":{"west":true},"boxes":[[0,0.375,0.4375,0.375,0.5625,0.5625]]}]})",
                 R"({"name":"torch","boxes":[[0.4375,0,0.4375,0.5625,0.625,0.5625]],"uv":"box"})"}) {
            QVERIFY(content.registerShape(*QcContentParser::parseShape(json(shape), u"t"_s)).has_value());
        }
        for (const char *block : {
                 R"({"name":"stone","textures":{"all":"stone"}})",
                 R"({"name":"stairs","shape":"stairs","states":{"facing":["north","east","south","west"],"half":["bottom","top"]},"textures":{"all":"stone"}})",
                 R"({"name":"slab","shape":"slab","states":{"type":["bottom","top","double"]},"textures":{"all":"stone"}})",
                 R"({"name":"fence","shape":"fence","lightOpacity":0,"states":{"east":[false,true],"west":[false,true]},"textures":{"all":"stone"}})",
                 R"({"name":"torch","shape":"torch","solid":false,"render":"cutout","light":14,"lightOpacity":0,"textures":{"all":"stone"}})"}) {
            const auto parsed = QcContentParser::parseBlock(json(block), u"t"_s);
            if (!parsed || !content.registerBlock(*parsed)) {
                qFatal("cannot register %s", block);
            }
        }
        table = std::make_shared<const QcBlockRenderTable>(content, QcTextureAtlas::build(content));
    }

    QcBlockId id(const QString &state) const { return *content.stateId(state); }

    // The chunk at (0, 0) with empty neighbours (or the given ones), meshing `section`.
    QcMeshInput input(const std::shared_ptr<QcChunk> &centre, int section,
                      std::array<std::shared_ptr<QcChunk>, 9> chunks = {}) const
    {
        QcMeshInput in;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto index = std::size_t(dz + 1) * 3 + std::size_t(dx + 1);
                in.chunks[index] = chunks[index] ? chunks[index] : std::make_shared<QcChunk>(QcChunkPos{dx, dz});
            }
        }
        in.chunks[4] = centre;
        in.sectionIndex = section;
        in.renderTable = table;
        return in;
    }
};

struct Quad
{
    std::array<QcBlockVertex, 4> corners;

    // Axis-aligned normal from the winding: +1/-1 on one axis.
    std::array<int, 3> normal() const
    {
        const auto at = [this](int i) { return std::array<float, 3>{corners[i].x, corners[i].y, corners[i].z}; };
        const auto a = at(0);
        const auto b = at(1);
        const auto c = at(2);
        const std::array<float, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const std::array<float, 3> v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const std::array<float, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        std::array<int, 3> result{};
        for (std::size_t i = 0; i < 3; ++i) {
            result[i] = n[i] > 1e-6f ? 1 : (n[i] < -1e-6f ? -1 : 0);
        }
        return result;
    }
    double area() const
    {
        // Rectangles: product of the two non-zero extents.
        std::array<float, 3> low{corners[0].x, corners[0].y, corners[0].z};
        std::array<float, 3> high = low;
        for (const QcBlockVertex &v : corners) {
            low = {std::min(low[0], v.x), std::min(low[1], v.y), std::min(low[2], v.z)};
            high = {std::max(high[0], v.x), std::max(high[1], v.y), std::max(high[2], v.z)};
        }
        double result = 1.0;
        for (std::size_t i = 0; i < 3; ++i) {
            if (high[i] - low[i] > 1e-6f) {
                result *= double(high[i] - low[i]);
            }
        }
        return result;
    }
};

QList<Quad> quads(const QList<QcBlockVertex> &vertices)
{
    QList<Quad> result;
    for (qsizetype i = 0; i + 3 < vertices.size(); i += 4) {
        result.append(Quad{{vertices[i], vertices[i + 1], vertices[i + 2], vertices[i + 3]}});
    }
    return result;
}

double areaFacing(const QList<Quad> &list, std::array<int, 3> normal)
{
    double total = 0.0;
    for (const Quad &quad : list) {
        if (quad.normal() == normal) {
            total += quad.area();
        }
    }
    return total;
}

} // namespace

class TestMesherShapes : public QObject
{
    Q_OBJECT

private:
    Fixture m_f;

private slots:
    void slabsCullOnlyWhatIsCovered()
    {
        const QcBlockId slab = m_f.id(u"t:slab[type=bottom]"_s);
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 20, 5, slab);
        QCOMPARE(quads(QcChunkMesher::mesh(m_f.input(chunk, 1)).opaqueVertices).size(), 6);
        // Two slabs side by side hide their touching halves.
        chunk->setBlock(6, 20, 5, slab);
        QCOMPARE(quads(QcChunkMesher::mesh(m_f.input(chunk, 1)).opaqueVertices).size(), 10);
        // Next to stone: the slab's side is hidden, the stone's side stays (half of it is open).
        auto withStone = std::make_shared<QcChunk>();
        withStone->setBlock(5, 20, 5, slab);
        withStone->setBlock(6, 20, 5, m_f.id(u"t:stone"_s));
        QCOMPARE(quads(QcChunkMesher::mesh(m_f.input(withStone, 1)).opaqueVertices).size(), 5 + 6);
        // A top slab above a bottom slab: neither touches the other's faces, nothing is culled.
        auto stacked = std::make_shared<QcChunk>();
        stacked->setBlock(5, 20, 5, slab);
        stacked->setBlock(5, 21, 5, m_f.id(u"t:slab[type=top]"_s));
        QCOMPARE(quads(QcChunkMesher::mesh(m_f.input(stacked, 1)).opaqueVertices).size(), 12);
        // A stone block on a top slab hides the slab's top and the stone's bottom.
        stacked->setBlock(5, 22, 5, m_f.id(u"t:stone"_s));
        QCOMPARE(quads(QcChunkMesher::mesh(m_f.input(stacked, 1)).opaqueVertices).size(), 12 + 6 - 2);
        // A double slab is a full cube.
        auto doubled = std::make_shared<QcChunk>();
        doubled->setBlock(5, 20, 5, m_f.id(u"t:slab[type=double]"_s));
        doubled->setBlock(6, 20, 5, m_f.id(u"t:stone"_s));
        QCOMPARE(quads(QcChunkMesher::mesh(m_f.input(doubled, 1)).opaqueVertices).size(), 10);
    }

    void stairsAreClosedWithoutInnerFaces()
    {
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 20, 5, m_f.id(u"t:stairs[facing=north,half=bottom]"_s));
        const QList<Quad> list = quads(QcChunkMesher::mesh(m_f.input(chunk, 1)).opaqueVertices);
        // Lower box: 6 faces; upper box: 5 (its bottom lies on the lower box).
        QCOMPARE(list.size(), 11);
        // The surface is closed: every direction shows as much area as the opposite one hides.
        QCOMPARE(areaFacing(list, {0, 0, -1}), 1.0);
        QCOMPARE(areaFacing(list, {0, 0, 1}), 1.0);
        QCOMPARE(areaFacing(list, {1, 0, 0}), 0.75);
        QCOMPARE(areaFacing(list, {-1, 0, 0}), 0.75);
        QCOMPARE(areaFacing(list, {0, -1, 0}), 1.0);
        // Up: the step (drawn whole, half of it lies under the back box) and the back's top.
        QCOMPARE(areaFacing(list, {0, 1, 0}), 1.5);
        for (const Quad &quad : list) {
            const bool innerFloor = quad.normal() == std::array<int, 3>{0, -1, 0} && quad.corners[0].y == 20.5f;
            QVERIFY(!innerFloor);
        }
    }

    void connectedFencesShareNoFaces()
    {
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 20, 5, m_f.id(u"t:fence[east=true,west=false]"_s));
        chunk->setBlock(6, 20, 5, m_f.id(u"t:fence[east=false,west=true]"_s));
        const QList<Quad> list = quads(QcChunkMesher::mesh(m_f.input(chunk, 1)).opaqueVertices);
        for (const Quad &quad : list) {
            const bool onBorder = std::ranges::all_of(quad.corners, [](const QcBlockVertex &v) { return v.x == 6.0f; });
            QVERIFY(!onBorder);
        }
        // Each post: 6 faces; each arm: 5 (its end touches the other arm, its other end the post).
        QCOMPARE(list.size(), 6 + 6 + 4 + 4);
    }

    void torchLightCrossesTheChunkBorderAndGoesAway()
    {
        const auto levelAt = [](int x, int y, int z) {
            // The torch stands at world (-1, 17, 8) in the western neighbour chunk.
            return std::max(0, 14 - (std::abs(x + 1) + std::abs(y - 17) + std::abs(z - 8)));
        };
        auto centre = std::make_shared<QcChunk>();
        for (int x = 0; x < 4; ++x) {
            for (int z = 6; z <= 10; ++z) {
                centre->setBlock(x, 16, z, m_f.id(u"t:stone"_s));
            }
        }
        std::array<std::shared_ptr<QcChunk>, 9> chunks{};
        auto west = std::make_shared<QcChunk>(QcChunkPos{-1, 0});
        west->setBlock(15, 17, 8, *m_f.content.blockId(u"t:torch"_s));
        chunks[3] = west;
        const QList<Quad> lit = quads(QcChunkMesher::mesh(m_f.input(centre, 1, chunks)).opaqueVertices);
        int checked = 0;
        for (const Quad &quad : lit) {
            if (quad.normal() != std::array<int, 3>{0, 1, 0}) {
                continue;
            }
            for (const QcBlockVertex &v : quad.corners) {
                // A corner of a top face averages the four open cells around it.
                const int x = int(v.x);
                const int z = int(v.z);
                const double expected = (levelAt(x - 1, 17, z - 1) + levelAt(x, 17, z - 1) + levelAt(x - 1, 17, z) + levelAt(x, 17, z)) / 4.0 / 15.0;
                QVERIFY2(std::abs(v.blockLight - expected) < 1e-6, qPrintable(u"%1 %2: %3 vs %4"_s.arg(x).arg(z).arg(v.blockLight).arg(expected)));
                ++checked;
            }
        }
        QCOMPARE(checked, 4 * 4 * 5);
        // Without the torch the same mesh is dark.
        west->setBlock(15, 17, 8, qcAirBlockId);
        const QList<Quad> dark = quads(QcChunkMesher::mesh(m_f.input(centre, 1, chunks)).opaqueVertices);
        for (const Quad &quad : dark) {
            for (const QcBlockVertex &v : quad.corners) {
                QCOMPARE(v.blockLight, 0.0f);
            }
        }
    }

    void torchesAreLitInTheirOwnCell()
    {
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(5, 20, 5, *m_f.content.blockId(u"t:torch"_s));
        const QList<Quad> list = quads(QcChunkMesher::mesh(m_f.input(chunk, 1)).opaqueVertices);
        // A thin box: six faces. All but the bottom lie inside the cell and get the torch's own
        // light; the bottom lies on the floor of the cell and is lit by the cells below (level 13 and
        // less), interpolated to its small square.
        QCOMPARE(list.size(), 6);
        for (const Quad &quad : list) {
            const bool bottom = quad.normal() == std::array<int, 3>{0, -1, 0};
            for (const QcBlockVertex &v : quad.corners) {
                QVERIFY(v.x > 5.4f && v.x < 5.6f);
                if (bottom) {
                    QVERIFY2(v.blockLight > 11.0f / 15.0f && v.blockLight <= 13.0f / 15.0f, qPrintable(QString::number(v.blockLight)));
                } else {
                    QCOMPARE(v.blockLight, 14.0f / 15.0f);
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestMesherShapes)
#include "test_mesher_shapes.moc"
