// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/entitymeshes/QcSpriteShape.h"
#include "clientcore/entitymeshes/QcSpriteShapes.h"
#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype quadVertices = 4;
const QRectF spriteRect{0.25, 0.5, 0.125, 0.0625};

// A hook of 8 texels: a stem with a bend at the top, so it has corners on every side.
std::shared_ptr<const QcSpriteShape> hook()
{
    return std::make_shared<const QcSpriteShape>(QcSpriteShape::fromRows({
        u"........"_s,
        u"..####.."_s,
        u"..#..#.."_s,
        u".....#.."_s,
        u".....#.."_s,
        u".....#.."_s,
        u".....#.."_s,
        u"........"_s,
    }));
}

QcEntityMesher::ItemLook sprite(std::shared_ptr<const QcSpriteShape> shape)
{
    QcEntityMesher::ItemLook look;
    look.sprite = u"t:hook"_s;
    look.shape = std::move(shape);
    return look;
}

QcEntityMesher::ItemLook cube()
{
    QcEntityMesher::ItemLook look;
    look.cube = true;
    look.faces.fill(u"t:stone"_s);
    look.sprite = u"t:stone_item"_s;
    return look;
}

QRectF uvOf(const QString &texture)
{
    return texture == u"t:hook"_s ? spriteRect : QRectF(0.0, 0.0, 1.0, 1.0);
}

QcClientEntity drop(int count = 1)
{
    QcClientEntity entity;
    entity.id = 7;
    entity.item = {1, count, 0};
    entity.position = {10.0, 64.0, -3.0};
    entity.previousPosition = entity.position;
    return entity;
}

QcEntityMesh dropped(const QcEntityMesher::ItemLook &look, int count = 1, double seconds = 0.0)
{
    QcEntityMesh mesh;
    QcEntityMesher::appendItem(mesh, drop(count), look, {1.0, seconds, 1.0f}, uvOf);
    return mesh;
}

QcVec3 at(const QcEntityVertex &vertex)
{
    return {vertex.x, vertex.y, vertex.z};
}

// The unit normal of the quad starting at `vertex`.
QcVec3 normalOf(const QcEntityMesh &mesh, qsizetype vertex)
{
    const QcVec3 a = at(mesh.vertices[vertex + 1]) - at(mesh.vertices[vertex]);
    const QcVec3 b = at(mesh.vertices[vertex + 2]) - at(mesh.vertices[vertex]);
    const QcVec3 normal{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return normal * (1.0 / normal.length());
}

// How far the mesh reaches along `direction` around the turning axis of the drop.
double reach(const QcEntityMesh &mesh, const QcVec3 &direction)
{
    const QcVec3 axis = drop().position;
    double furthest = 0.0;
    for (const QcEntityVertex &vertex : mesh.vertices) {
        furthest = std::max(furthest, std::abs((at(vertex) - axis).dot(direction)));
    }
    return furthest;
}

} // namespace

// Dropped items on the ground (decision E-991): one solid turning item, not two crossed pictures;
// blocks keep their block form and experience orbs stay crossed.
class TestDroppedItem : public QObject
{
    Q_OBJECT

    std::shared_ptr<QcContentRegistry> m_content = std::make_shared<QcContentRegistry>();
    std::shared_ptr<const QcTextureAtlas> m_atlas;

    [[nodiscard]] QcItemId itemNamed(const QString &name) const { return m_content->itemId(name).value_or(qcNoItem); }

private slots:
    void initTestCase()
    {
        QVERIFY(QcBaseContent::loadInto(*m_content));
        QVERIFY(m_content->finish().isEmpty());
        m_atlas = std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(*m_content));
    }

    void aDroppedToolOfTheRealAtlasIsSolid()
    {
        QcSpriteShapes shapes;
        const QcEntityMesher::ItemLook look = QcFigureLook::dropped(*m_content, itemNamed(u"qtcraft:iron_pickaxe"_s), shapes, m_atlas);
        QVERIFY(!look.cube);
        QVERIFY(look.shape && !look.shape->isEmpty());

        QcEntityMesh mesh;
        QcEntityMesher::appendItem(mesh, drop(), look, {1.0, 0.0, 1.0f}, [this](const QString &texture) { return m_atlas->uv(texture); });
        QVERIFY(look.shape->form().size() > 2);
        QCOMPARE(mesh.vertices.size(), look.shape->form().size() * quadVertices);
    }

    void aDroppedBowShowsItsRestingSprite()
    {
        const QcItemId bow = itemNamed(u"qtcraft:bow"_s);
        QcSpriteShapes shapes;
        const QcEntityMesher::ItemLook look = QcFigureLook::dropped(*m_content, bow, shapes, m_atlas);
        QCOMPARE(look.sprite, m_content->item(bow).texture);
        QCOMPARE(look.shape, shapes.shape(m_atlas, m_content->item(bow).texture));
    }

    void aDroppedBlockOfTheRealAtlasHasNoShape()
    {
        QcSpriteShapes shapes;
        const QcEntityMesher::ItemLook look = QcFigureLook::dropped(*m_content, itemNamed(u"qtcraft:stone"_s), shapes, m_atlas);
        QVERIFY(look.cube);
        QVERIFY(!look.shape);
    }

    void withoutAnAtlasADropIsFlat()
    {
        QcSpriteShapes shapes;
        const QcEntityMesher::ItemLook look = QcFigureLook::dropped(*m_content, itemNamed(u"qtcraft:iron_pickaxe"_s), shapes, nullptr);
        QVERIFY(!look.shape);
    }

    void aSpriteIsOneSolidItem()
    {
        const auto shape = hook();
        const QcEntityMesh mesh = dropped(sprite(shape));

        // The solid form and nothing else: a front, a back and quads on the edges.
        QVERIFY(shape->form().size() > 2);
        QCOMPARE(mesh.vertices.size(), shape->form().size() * quadVertices);
        QCOMPARE(mesh.indices.size(), shape->form().size() * 6);

        // All of it lies in one upright slab one texel thick; crossed pictures reach far out of it.
        const QcVec3 normal = normalOf(mesh, 0);
        QVERIFY(std::abs(normal.y) < 1e-5);
        const double texel = QcEntityMesher::itemSpriteSize / shape->size();
        QVERIFY(std::abs(reach(mesh, normal) - texel * 0.5) < 1e-4);
    }

    void itsFrontAndBackShowThePicture()
    {
        const QcEntityMesh mesh = dropped(sprite(hook()));
        const qsizetype faces = 2 * quadVertices;
        for (qsizetype i = 0; i < faces; ++i) {
            const QcEntityVertex &vertex = mesh.vertices[i];
            const bool cornerU = qFuzzyCompare(vertex.u, static_cast<float>(spriteRect.left())) || qFuzzyCompare(vertex.u, static_cast<float>(spriteRect.right()));
            const bool cornerV = qFuzzyCompare(vertex.v, static_cast<float>(spriteRect.top())) || qFuzzyCompare(vertex.v, static_cast<float>(spriteRect.bottom()));
            QVERIFY(cornerU && cornerV);
        }
        QVERIFY(normalOf(mesh, 0).dot(normalOf(mesh, quadVertices)) < -0.999);

        // The sides take colours from inside the picture.
        for (const QcEntityVertex &vertex : mesh.vertices) {
            QVERIFY(spriteRect.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(QPointF(vertex.u, vertex.v)));
        }
    }

    void aStraightEdgeIsOneQuadInTheColoursOfItsTexels()
    {
        // A bar: its four edges are four quads however many texels they run along.
        const QcSpriteShape bar = QcSpriteShape::fromRows({u"...."_s, u"####"_s, u"...."_s, u"...."_s});
        QCOMPARE(bar.sides().size(), 10);
        QCOMPARE(bar.form().size(), 6);

        // The top runs through the middle of its texels' row in the picture, end to end.
        const auto top = std::ranges::find_if(bar.form(), [](const QcSpriteShape::FormQuad &quad) { return quad.shade == 1.0f; });
        QVERIFY(top != bar.form().end());
        double left = 1.0;
        double right = 0.0;
        for (const QcSpriteShape::FormCorner &corner : top->corners) {
            QCOMPARE(corner.texture.y(), 1.5 / 4.0);
            QCOMPARE(corner.point.y, 0.75);
            left = std::min(left, corner.texture.x());
            right = std::max(right, corner.texture.x());
        }
        QCOMPARE(left, 0.0);
        QCOMPARE(right, 1.0);

        // Every edge of a crooked shape stays covered: the quads add up to one texel per side.
        const auto shape = hook();
        double covered = 0.0;
        const QList<QcSpriteShape::FormQuad> edges = shape->form().sliced(2);
        for (const QcSpriteShape::FormQuad &quad : edges) {
            covered += (quad.corners[1].point - quad.corners[0].point).length() + (quad.corners[2].point - quad.corners[1].point).length();
            covered -= 1.0 / shape->size();
        }
        QCOMPARE(qRound(covered * shape->size()), shape->sides().size());
    }

    void itTurnsAboutTheVerticalAxisAndBobs()
    {
        const auto look = sprite(hook());
        const QcEntityMesh start = dropped(look, 1, 0.0);
        const QcEntityMesh later = dropped(look, 1, 1.0);
        QCOMPARE(later.vertices.size(), start.vertices.size());

        // One second is 60 degrees for every vertex, whatever its distance from the axis.
        const QcVec3 axis = drop().position;
        const double rise = later.vertices[0].y - start.vertices[0].y;
        QVERIFY(std::abs(rise) > 1e-3);
        for (qsizetype i = 0; i < start.vertices.size(); ++i) {
            const QcVec3 a = at(start.vertices[i]) - axis;
            const QcVec3 b = at(later.vertices[i]) - axis;
            QVERIFY(std::abs(b.y - a.y - rise) < 1e-4);
            const double radius = std::hypot(a.x, a.z);
            QVERIFY(std::abs(std::hypot(b.x, b.z) - radius) < 1e-4);
            if (radius > 0.01) {
                QVERIFY(std::abs((a.x * b.x + a.z * b.z) / (radius * radius) - 0.5) < 1e-3);
            }
        }
    }

    void aSpriteWithoutAShapeIsOneFlatPicture()
    {
        for (const auto &shape : {std::shared_ptr<const QcSpriteShape>(), std::make_shared<const QcSpriteShape>()}) {
            const QcEntityMesh mesh = dropped(sprite(shape));
            QCOMPARE(mesh.vertices.size(), 2 * quadVertices);
            QVERIFY(reach(mesh, normalOf(mesh, 0)) < 1e-4);
        }
    }

    void aBlockKeepsItsForm()
    {
        // A shape on a block's look changes nothing: six faces of a cube of the item size.
        QcEntityMesher::ItemLook look = cube();
        const QcEntityMesh plain = dropped(look);
        look.shape = hook();
        const QcEntityMesh withShape = dropped(look);
        QCOMPARE(plain.vertices.size(), 6 * quadVertices);
        QCOMPARE(withShape.vertices.size(), plain.vertices.size());

        const QcVec3 across = normalOf(plain, 0);
        QVERIFY(std::abs(reach(plain, across) - QcEntityMesher::itemCubeSize * 0.5) < 1e-4);
        for (qsizetype i = 0; i < plain.vertices.size(); ++i) {
            QCOMPARE(withShape.vertices[i].x, plain.vertices[i].x);
            QCOMPARE(withShape.vertices[i].y, plain.vertices[i].y);
            QCOMPARE(withShape.vertices[i].z, plain.vertices[i].z);
        }
    }

    void stacksShowCopies_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("copies");
        QTest::newRow("one") << 1 << 1;
        QTest::newRow("two") << 2 << 2;
        QTest::newRow("fifteen") << 15 << 2;
        QTest::newRow("sixteen") << 16 << 3;
        QTest::newRow("full") << 64 << 3;
    }

    void stacksShowCopies()
    {
        QFETCH(int, count);
        QFETCH(int, copies);
        for (const QcEntityMesher::ItemLook &look : {sprite(hook()), cube()}) {
            const QcEntityMesh one = dropped(look);
            const QcEntityMesh stack = dropped(look, count);
            QCOMPARE(stack.vertices.size(), one.vertices.size() * copies);

            // Each copy is the same item moved a little to the side and up.
            for (int copy = 0; copy < copies; ++copy) {
                const QcVec3 shift{copy * 0.07, copy * 0.05, copy * -0.06};
                for (qsizetype i = 0; i < one.vertices.size(); ++i) {
                    const QcVec3 moved = at(one.vertices[i]) + shift;
                    QVERIFY((at(stack.vertices[copy * one.vertices.size() + i]) - moved).length() < 1e-4);
                }
            }
        }
    }

    void itGlowsLikeTheFlatItemDid()
    {
        QcEntityMesher::ItemLook look = sprite(hook());
        look.glow = 0.75f;
        QcEntityMesh mesh;
        QcEntityMesher::appendItem(mesh, drop(), look, {1.0, 0.0, QcEntityLight(0.25f, 0.5f)}, uvOf);
        QVERIFY(std::ranges::all_of(mesh.vertices, [](const QcEntityVertex &vertex) { return vertex.blockLight == 0.75f && vertex.skyLight == 0.25f; }));
    }

    void orbsStayCrossed()
    {
        QcClientEntity orb = drop();
        orb.health = 1.0;
        QcEntityMesh mesh;
        QcEntityMesher::appendOrb(mesh, orb, {1.0, 0.0, 1.0f}, uvOf);

        // Two pictures, each with a front and a back, at right angles.
        QCOMPARE(mesh.vertices.size(), 4 * quadVertices);
        const QcVec3 first = normalOf(mesh, 0);
        const QcVec3 second = normalOf(mesh, 2 * quadVertices);
        QVERIFY(std::abs(first.dot(second)) < 1e-4);
        QVERIFY(reach(mesh, first) > 0.05);
        QVERIFY(reach(mesh, second) > 0.05);
    }
};

QTEST_GUILESS_MAIN(TestDroppedItem)
#include "test_dropped_item.moc"
