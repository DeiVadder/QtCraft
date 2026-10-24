// Shapes of signal blocks with the base content (decision E-874): extended piston heads are closed
// and drawn with opaque texels, and the arrow on the top of repeaters and comparators points
// towards their output for every facing.
#include "clientcore/meshing/QcChunkMesher.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"
#include "QcWorldMesh.h"

#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

struct Facing
{
    QString name;
    QcBlockFace face;
};

const std::array<Facing, 6> allFacings{{
    {u"north"_s, QcBlockFace::North},
    {u"east"_s, QcBlockFace::East},
    {u"south"_s, QcBlockFace::South},
    {u"west"_s, QcBlockFace::West},
    {u"up"_s, QcBlockFace::Up},
    {u"down"_s, QcBlockFace::Down},
}};

std::array<int, 3> offsetOf(QcBlockFace face)
{
    const QcFaceOffset offset = qcFaceOffset(face);
    return {offset.dx, offset.dy, offset.dz};
}

// A quad of the mesh: its corners in world space.
struct MeshQuad
{
    std::array<QcBlockVertex, 4> corners;

    [[nodiscard]] std::array<float, 3> low() const
    {
        std::array<float, 3> result{corners[0].x, corners[0].y, corners[0].z};
        for (const QcBlockVertex &v : corners) {
            result = {std::min(result[0], v.x), std::min(result[1], v.y), std::min(result[2], v.z)};
        }
        return result;
    }

    [[nodiscard]] std::array<float, 3> high() const
    {
        std::array<float, 3> result{corners[0].x, corners[0].y, corners[0].z};
        for (const QcBlockVertex &v : corners) {
            result = {std::max(result[0], v.x), std::max(result[1], v.y), std::max(result[2], v.z)};
        }
        return result;
    }

    // Axis-aligned normal from the winding: +1/-1 on one axis.
    [[nodiscard]] std::array<int, 3> normal() const
    {
        const std::array<float, 3> u{corners[1].x - corners[0].x, corners[1].y - corners[0].y, corners[1].z - corners[0].z};
        const std::array<float, 3> v{corners[2].x - corners[0].x, corners[2].y - corners[0].y, corners[2].z - corners[0].z};
        const std::array<float, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        std::array<int, 3> result{};
        for (std::size_t i = 0; i < 3; ++i) {
            result[i] = n[i] > 1e-6f ? 1 : (n[i] < -1e-6f ? -1 : 0);
        }
        return result;
    }

    [[nodiscard]] double area() const
    {
        const std::array<float, 3> a = low();
        const std::array<float, 3> b = high();
        double result = 1.0;
        for (std::size_t i = 0; i < 3; ++i) {
            if (b[i] - a[i] > 1e-6f) {
                result *= static_cast<double>(b[i] - a[i]);
            }
        }
        return result;
    }

    // Whether the quad belongs to the block in `cell`: its centre, moved a little against its normal
    // (a face on a cell side lies on the boundary of both cells), lies in that cell.
    [[nodiscard]] bool inCell(std::array<int, 3> cell) const
    {
        const std::array<float, 3> a = low();
        const std::array<float, 3> b = high();
        const std::array<int, 3> n = normal();
        for (std::size_t i = 0; i < 3; ++i) {
            const float inside = 0.5f * (a[i] + b[i]) - 0.001f * static_cast<float>(n[i]);
            if (static_cast<int>(std::floor(inside)) != cell[i]) {
                return false;
            }
        }
        return true;
    }
};

QList<MeshQuad> quadsOf(const QList<QcBlockVertex> &vertices)
{
    QList<MeshQuad> result;
    for (qsizetype i = 0; i + 3 < vertices.size(); i += 4) {
        result.append(MeshQuad{{vertices[i], vertices[i + 1], vertices[i + 2], vertices[i + 3]}});
    }
    return result;
}

// Area of the faces of the block in `cell` that face `normal`.
double areaFacing(const QList<MeshQuad> &quads, std::array<int, 3> cell, std::array<int, 3> normal)
{
    double total = 0.0;
    for (const MeshQuad &quad : quads) {
        if (quad.inCell(cell) && quad.normal() == normal) {
            total += quad.area();
        }
    }
    return total;
}

// Texels of the atlas under the texture rectangle of `quad` that are not fully opaque.
int transparentTexels(const QImage &atlas, const MeshQuad &quad)
{
    float u0 = quad.corners[0].u;
    float u1 = u0;
    float v0 = quad.corners[0].v;
    float v1 = v0;
    for (const QcBlockVertex &corner : quad.corners) {
        u0 = std::min(u0, corner.u);
        u1 = std::max(u1, corner.u);
        v0 = std::min(v0, corner.v);
        v1 = std::max(v1, corner.v);
    }

    int count = 0;
    const int firstX = static_cast<int>(std::floor(u0 * static_cast<float>(atlas.width())));
    const int lastX = static_cast<int>(std::ceil(u1 * static_cast<float>(atlas.width()))) - 1;
    const int firstY = static_cast<int>(std::floor(v0 * static_cast<float>(atlas.height())));
    const int lastY = static_cast<int>(std::ceil(v1 * static_cast<float>(atlas.height()))) - 1;
    for (int y = firstY; y <= lastY; ++y) {
        for (int x = firstX; x <= lastX; ++x) {
            count += atlas.pixelColor(x, y).alpha() < 255 ? 1 : 0;
        }
    }
    return count;
}

// The world direction (axis signs) in which the texture's top edge lies on the largest face of
// `quads` facing `normal`: from the middle of its bottom edge to the middle of its top edge (in the
// atlas the top edge has the smaller v).
std::array<int, 3> textureUp(const QList<MeshQuad> &quads, std::array<int, 3> normal)
{
    const MeshQuad *largest = nullptr;
    for (const MeshQuad &quad : quads) {
        if (quad.normal() == normal && (!largest || quad.area() > largest->area())) {
            largest = &quad;
        }
    }
    if (!largest) {
        return {};
    }

    float top = largest->corners[0].v;
    float bottom = top;
    for (const QcBlockVertex &corner : largest->corners) {
        top = std::min(top, corner.v);
        bottom = std::max(bottom, corner.v);
    }
    std::array<float, 3> direction{};
    for (const QcBlockVertex &corner : largest->corners) {
        const float weight = corner.v == top ? 1.0f : (corner.v == bottom ? -1.0f : 0.0f);
        direction = {direction[0] + weight * corner.x, direction[1] + weight * corner.y, direction[2] + weight * corner.z};
    }
    const auto sign = [](float value) { return value > 1e-4f ? 1 : (value < -1e-4f ? -1 : 0); };
    return {sign(direction[0]), sign(direction[1]), sign(direction[2])};
}

std::array<int, 3> negated(std::array<int, 3> v)
{
    return {-v[0], -v[1], -v[2]};
}

} // namespace

class TestMesherSignalShapes : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content;
    QcTextureAtlas m_atlas;
    std::shared_ptr<const QcBlockRenderTable> m_table;

    [[nodiscard]] QcBlockId id(const QString &state) const
    {
        const std::optional<QcBlockId> found = m_content.stateId(state);
        if (!found) {
            qFatal("unknown state %s", qPrintable(state));
        }
        return *found;
    }

    // The quads of `state` alone at (8, 20, 8), all layers, with `support` (if any) in the cell
    // `supportOffset` away.
    [[nodiscard]] QList<MeshQuad> meshAlone(const QString &state, const QString &support = {}, std::array<int, 3> supportOffset = {}) const
    {
        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(8, 20, 8, id(state));
        if (!support.isEmpty()) {
            chunk->setBlock(8 + supportOffset[0], 20 + supportOffset[1], 8 + supportOffset[2], id(support));
        }
        const QcChunkMesh mesh = qcWorldMesh(input(chunk));
        QList<MeshQuad> quads = quadsOf(mesh.opaqueVertices);
        quads.append(quadsOf(mesh.cutoutVertices));
        quads.append(quadsOf(mesh.translucentVertices));
        quads.removeIf([](const MeshQuad &quad) { return !quad.inCell({8, 20, 8}); });
        return quads;
    }

    [[nodiscard]] QcMeshInput input(const std::shared_ptr<QcChunk> &centre) const
    {
        QcMeshInput in;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const auto index = static_cast<std::size_t>(dz + 1) * 3 + static_cast<std::size_t>(dx + 1);
                in.chunks[index] = std::make_shared<QcChunk>(QcChunkPos{dx, dz});
            }
        }
        in.chunks[4] = centre;
        in.sectionIndex = 1;
        in.renderTable = m_table;
        return in;
    }

    // The slab's top of a repeater or comparator state: the direction from the cell's centre to the
    // middle of the texture's top edge (the arrow's tip), as (dx, dz) signs.
    [[nodiscard]] std::array<int, 2> arrowOf(const QString &state) const
    {
        const QcBlockRenderTable::Entry &entry = m_table->entry(id(state));
        for (const QcBlockRenderTable::Quad &quad : entry.quads) {
            const bool slabTop = quad.face == QcBlockFace::Up && quad.corners[0][1] == 0.125f && quad.rect.contains({});
            if (!slabTop) {
                continue;
            }
            // The texture's top edge has the smallest v in the atlas (images run downwards).
            const float top = std::min({quad.uvs[0][1], quad.uvs[1][1], quad.uvs[2][1], quad.uvs[3][1]});
            float x = 0.0f;
            float z = 0.0f;
            for (std::size_t corner = 0; corner < 4; ++corner) {
                if (quad.uvs[corner][1] == top) {
                    x += 0.5f * (quad.corners[corner][0] - 0.5f);
                    z += 0.5f * (quad.corners[corner][2] - 0.5f);
                }
            }
            const auto sign = [](float value) { return value > 1e-4f ? 1 : (value < -1e-4f ? -1 : 0); };
            return {sign(x), sign(z)};
        }
        return {0, 0};
    }

private slots:
    void initTestCase()
    {
        // With the generated building parts and without warnings.
        m_content = *QcTestUtil::loadContent();
        m_atlas = QcTextureAtlas::build(m_content);
        m_table = std::make_shared<const QcBlockRenderTable>(m_content, m_atlas);
    }

    // An extended head shows its plate's front, back and sides and its arm's sides with opaque texels
    // for every facing, sticky or not; none of its faces is hidden against its own base.
    void extendedPistonHeadsAreClosed_data()
    {
        QTest::addColumn<QString>("piston");
        QTest::addColumn<bool>("sticky");
        QTest::addColumn<int>("facing");
        for (const bool sticky : {false, true}) {
            for (std::size_t f = 0; f < allFacings.size(); ++f) {
                const QString piston = sticky ? u"qtcraft:sticky_signal_piston"_s : u"qtcraft:signal_piston"_s;
                QTest::addRow("%s %s", qPrintable(piston), qPrintable(allFacings[f].name)) << piston << sticky << static_cast<int>(f);
            }
        }
    }

    void extendedPistonHeadsAreClosed()
    {
        QFETCH(QString, piston);
        QFETCH(bool, sticky);
        QFETCH(int, facing);
        const Facing &which = allFacings[static_cast<std::size_t>(facing)];
        const std::array<int, 3> out = offsetOf(which.face);
        const std::array<int, 3> base{8, 20, 8};
        const std::array<int, 3> head{base[0] + out[0], base[1] + out[1], base[2] + out[2]};

        auto chunk = std::make_shared<QcChunk>();
        chunk->setBlock(base[0], base[1], base[2], id(u"%1[facing=%2,extended=true]"_s.arg(piston, which.name)));
        chunk->setBlock(head[0], head[1], head[2],
                        id(u"qtcraft:signal_piston_head[facing=%1,sticky=%2]"_s.arg(which.name, sticky ? u"true"_s : u"false"_s)));
        const QcChunkMesh mesh = qcWorldMesh(input(chunk));
        QVERIFY(mesh.opaqueVertices.isEmpty());
        const QList<MeshQuad> quads = quadsOf(mesh.cutoutVertices);

        // The plate's front; its back plus the arm's end at the base.
        const std::array<int, 3> back{-out[0], -out[1], -out[2]};
        QCOMPARE(areaFacing(quads, head, out), 1.0);
        QCOMPARE(areaFacing(quads, head, back), 1.0 + 0.0625);
        // Each side: the plate's (a quarter deep) and the arm's (a quarter wide, three quarters long).
        for (const QcBlockFace side : qcAllBlockFaces) {
            const std::array<int, 3> normal = offsetOf(side);
            if (normal == out || normal == back) {
                continue;
            }
            QCOMPARE(areaFacing(quads, head, normal), 0.25 + 0.1875);
        }

        for (const MeshQuad &quad : quads) {
            if (quad.inCell(head)) {
                QCOMPARE(transparentTexels(m_atlas.image(), quad), 0);
            }
        }
    }

    // The arrow on the top of a repeater, and the single torch of a comparator's top, point where
    // the block sends its signal: towards its facing (QcSignalPower: output on the facing side).
    void diodeTopsPointToTheirOutput_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<int>("facing");
        for (std::size_t f = 0; f < 4; ++f) {
            const QString name = allFacings[f].name;
            QTest::addRow("repeater %s", qPrintable(name)) << u"qtcraft:signal_repeater[facing=%1]"_s.arg(name) << static_cast<int>(f);
            QTest::addRow("comparator %s", qPrintable(name)) << u"qtcraft:signal_comparator[facing=%1]"_s.arg(name) << static_cast<int>(f);
        }
    }

    void diodeTopsPointToTheirOutput()
    {
        QFETCH(QString, state);
        QFETCH(int, facing);
        const std::array<int, 3> out = offsetOf(allFacings[static_cast<std::size_t>(facing)].face);
        QCOMPARE(arrowOf(state), (std::array<int, 2>{out[0], out[2]}));
    }

    // An observer shows the same textures the same way round whether it is powered or not (its
    // powered shape is two boxes, the unpowered one a full cube), for all six facings, and the arrows
    // of its top and bottom textures point from its face to its back, where its pulse leaves.
    void observerArrowsKeepTheirWayWhenPowered_data()
    {
        QTest::addColumn<int>("facing");
        for (std::size_t f = 0; f < allFacings.size(); ++f) {
            QTest::addRow("%s", qPrintable(allFacings[f].name)) << static_cast<int>(f);
        }
    }

    void observerArrowsKeepTheirWayWhenPowered()
    {
        QFETCH(int, facing);
        const Facing &which = allFacings[static_cast<std::size_t>(facing)];
        const QList<MeshQuad> resting = meshAlone(u"qtcraft:signal_observer[facing=%1,powered=false]"_s.arg(which.name));
        const QList<MeshQuad> powered = meshAlone(u"qtcraft:signal_observer[facing=%1,powered=true]"_s.arg(which.name));
        for (const QcBlockFace face : qcAllBlockFaces) {
            const std::array<int, 3> normal = offsetOf(face);
            QVERIFY(textureUp(resting, normal) != (std::array<int, 3>{}));
            QCOMPARE(textureUp(powered, normal), textureUp(resting, normal));
        }

        // The faces with the arrows: the top texture's arrow points to its bottom edge, the bottom
        // texture's to its top edge. Upright observers carry them on their north and south sides.
        const std::array<int, 3> back = negated(offsetOf(which.face));
        const bool upright = which.face == QcBlockFace::Up || which.face == QcBlockFace::Down;
        const std::array<int, 3> topFace = upright ? (which.face == QcBlockFace::Up ? offsetOf(QcBlockFace::South) : offsetOf(QcBlockFace::North))
                                                   : offsetOf(QcBlockFace::Up);
        const std::array<int, 3> bottomFace = negated(topFace);
        for (const QList<MeshQuad> *quads : {&resting, &powered}) {
            QCOMPARE(negated(textureUp(*quads, topFace)), back);
            QCOMPARE(textureUp(*quads, bottomFace), back);
        }
    }

    // A signal indicator light (on the floor, the ceiling and the four walls, every colour, lit and
    // unlit) is closed: frame and lens show five faces each (the frame's back lies on its support,
    // the lens's back on the frame), all with opaque texels.
    void indicatorLightsAreClosed_data()
    {
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("attach");
        QTest::addColumn<int>("support");
        static const std::array<std::pair<const char *, QcBlockFace>, 6> attachments{{
            {"floor", QcBlockFace::Down}, {"ceiling", QcBlockFace::Up}, {"north", QcBlockFace::North},
            {"east", QcBlockFace::East}, {"south", QcBlockFace::South}, {"west", QcBlockFace::West},
        }};
        for (const char *block : {"qtcraft:signal_indicator_light", "qtcraft:signal_indicator_light_lit"}) {
            for (const auto &[attach, support] : attachments) {
                QTest::addRow("%s %s", block, attach) << QString::fromLatin1(block) << QString::fromLatin1(attach) << static_cast<int>(support);
            }
        }
    }

    void indicatorLightsAreClosed()
    {
        QFETCH(QString, block);
        QFETCH(QString, attach);
        QFETCH(int, support);
        const QcBlockProperty *colours = m_content.block(id(block)).property(u"color"_s);
        QVERIFY(colours);
        for (const QString &colour : colours->values) {
            const QString state = u"%1[attach=%2,color=%3]"_s.arg(block, attach, colour);
            const QList<MeshQuad> quads = meshAlone(state, u"qtcraft:stone"_s, offsetOf(static_cast<QcBlockFace>(support)));
            QVERIFY2(quads.size() == 10, qPrintable(u"%1: %2 faces"_s.arg(state).arg(quads.size())));
            for (const MeshQuad &quad : quads) {
                QVERIFY2(transparentTexels(m_atlas.image(), quad) == 0, qPrintable(state));
            }
        }
    }

    // Building parts keep their textures as on the world's faces (lockTextures, decision E-874):
    // the boards on top of an oak stair run as on the oak planks beside it, whatever its facing.
    void lockedPartsKeepWorldTextures()
    {
        const std::array<int, 3> up = offsetOf(QcBlockFace::Up);
        const std::array<int, 3> planks = textureUp(meshAlone(u"qtcraft:oak_planks"_s), up);
        QVERIFY(planks != (std::array<int, 3>{}));
        for (const Facing &facing : allFacings) {
            if (facing.face == QcBlockFace::Up || facing.face == QcBlockFace::Down) {
                continue;
            }
            const QString state = u"qtcraft:oak_stairs[facing=%1,half=bottom]"_s.arg(facing.name);
            QVERIFY2(textureUp(meshAlone(state), up) == planks, qPrintable(state));
        }
    }

    // The arrow of the observer's top points to the texture's bottom edge (there the back lies on
    // the top face), the one of its bottom to the top edge: most of the dark arrow pixels (the head)
    // lie in that half.
    void observerTopArrowPointsToItsBottomEdge()
    {
        const auto darkInHalves = [this](const QString &texture) {
            const QImage tile = m_atlas.tile(texture);
            std::array<int, 2> count{};
            for (int y = 0; y < tile.height(); ++y) {
                for (int x = 0; x < tile.width(); ++x) {
                    count[y < tile.height() / 2 ? 0 : 1] += qGray(tile.pixel(x, y)) < 106 ? 1 : 0;
                }
            }
            return count;
        };
        const std::array<int, 2> top = darkInHalves(u"qtcraft:signal_observer_top"_s);
        QVERIFY2(top[1] > 2 * top[0], qPrintable(u"top: %1 above, %2 below"_s.arg(top[0]).arg(top[1])));
        const std::array<int, 2> bottom = darkInHalves(u"qtcraft:signal_observer_bottom"_s);
        QVERIFY2(bottom[0] > 2 * bottom[1], qPrintable(u"bottom: %1 above, %2 below"_s.arg(bottom[0]).arg(bottom[1])));
    }
};

QTEST_GUILESS_MAIN(TestMesherSignalShapes)
#include "test_mesher_signal_shapes.moc"
