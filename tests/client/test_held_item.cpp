#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/meshing/QcFigureLook.h"
#include "clientcore/meshing/QcHeldItemMesher.h"
#include "clientcore/meshing/QcSpriteShape.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QImage>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

// Vertices of a quad and of the arm's one box.
constexpr qsizetype quadVertices = 4;
constexpr qsizetype armVertices = 6 * quadVertices;

QcModelDefinition rightArm()
{
    QcModelDefinition model;
    model.name = u"t:arm"_s;
    model.bones = {{u"right_arm"_s, QcBoneRole::RightArm, {0.35, 1.3, 0.0}, {{{0.25, 0.7, -0.1}, {0.45, 1.35, 0.1}, u"arm"_s}}}};
    return model;
}

QcAppearanceDefinition skin()
{
    QcAppearanceDefinition definition;
    definition.name = u"t:skin"_s;
    definition.layers = {{u"arm"_s, u"t:skin"_s, QcTint::Skin}};
    return definition;
}

QRectF anyUv(const QString &)
{
    return {0.0, 0.0, 1.0, 1.0};
}

// A pickaxe of 16 texels: the handle runs from the lower left (texel 1, 14) up to the head, an arc
// across the top right.
std::shared_ptr<const QcSpriteShape> pickaxe()
{
    return std::make_shared<const QcSpriteShape>(QcSpriteShape::fromRows({
        u"................"_s, u".....#######...."_s, u"....#.....###..."_s, u"..........####.."_s,
        u".........##.###."_s, u"........##...##."_s, u".......##....##."_s, u"......##......#."_s,
        u".....##.......#."_s, u"....##.........."_s, u"...##..........."_s, u"..##............"_s,
        u".##............."_s, u"##.............."_s, u"#..............."_s, u"................"_s,
    }));
}

QcVec3 at(const QcEntityVertex &vertex)
{
    return {vertex.x, vertex.y, vertex.z};
}

// Where a camera at the origin looking north (-Z) with the default view (70 degrees high, 16:10)
// shows a point: -1..1 across and up the screen.
QPointF screen(const QcVec3 &p)
{
    const double tanHalf = std::tan(qDegreesToRadians(35.0));
    return {p.x / (-p.z * tanHalf * 1.6), p.y / (-p.z * tanHalf)};
}

// The screen rectangle around vertices [from, to) of a mesh, y up.
QRectF screenBounds(const QcEntityMesh &mesh, qsizetype from, qsizetype to)
{
    double left = 1e9;
    double right = -1e9;
    double bottom = 1e9;
    double top = -1e9;
    for (qsizetype i = from; i < to; ++i) {
        const QPointF p = screen(at(mesh.vertices[i]));
        left = std::min(left, p.x());
        right = std::max(right, p.x());
        bottom = std::min(bottom, p.y());
        top = std::max(top, p.y());
    }
    return QRectF(QPointF(left, bottom), QPointF(right, top));
}

// The outward normal of quad `quad` (counter-clockwise seen from outside).
QcVec3 normalOf(const QcEntityMesh &mesh, qsizetype quad)
{
    const QcVec3 a = at(mesh.vertices[quad * 4 + 1]) - at(mesh.vertices[quad * 4]);
    const QcVec3 b = at(mesh.vertices[quad * 4 + 2]) - at(mesh.vertices[quad * 4]);
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

QcVec3 quadCentre(const QcEntityMesh &mesh, qsizetype quad)
{
    QcVec3 sum;
    for (qsizetype i = 0; i < 4; ++i) {
        sum += at(mesh.vertices[quad * 4 + i]);
    }
    return sum * 0.25;
}

// How squarely the front of a held sprite (the first quad after the arm) faces the camera at the
// origin: 1 seen face-on, 0 edge-on.
double facing(const QcEntityMesh &mesh)
{
    const qsizetype front = armVertices / quadVertices;
    const QcVec3 normal = normalOf(mesh, front);
    const QcVec3 sight = quadCentre(mesh, front);
    return std::abs(normal.dot(sight)) / (normal.length() * sight.length());
}

// How far the end of a held pickaxe's handle (the middle of its texel 0, 14, found from the
// corners of the front by their texture coordinates) lies from the nearest corner of the arm.
double handleToArm(const QcEntityMesh &mesh)
{
    const auto front = mesh.vertices.mid(armVertices, quadVertices);
    const auto corner = [&](float u, float v) {
        const auto found = std::ranges::find_if(front, [&](const QcEntityVertex &vertex) { return vertex.u == u && vertex.v == v; });
        return found == front.end() ? QcVec3{1e9, 1e9, 1e9} : at(*found);
    };
    const QcVec3 lowerLeft = corner(0.0f, 1.0f);
    const QcVec3 end = lowerLeft + (corner(1.0f, 1.0f) - lowerLeft) * (0.5 / 16.0) + (corner(0.0f, 0.0f) - lowerLeft) * (1.5 / 16.0);
    double nearest = 1e9;
    for (qsizetype i = 0; i < armVertices; ++i) {
        nearest = std::min(nearest, (at(mesh.vertices[i]) - end).length());
    }
    return nearest;
}

} // namespace

// The item in a hand (decision E-831): sprite shapes, extruded sprites, the first-person grip and
// the item in a figure's fist.
class TestHeldItem : public QObject
{
    Q_OBJECT

    const QcModelDefinition m_model = rightArm();
    const QcAppearanceDefinition m_skin = skin();
    const QcEntityMesher::ModelLook m_look{&m_model, &m_skin, nullptr, {}, 1.0};

    // The base content: the sizes and turns below are those of content/qtcraft/held.json, so a
    // change there that makes items tiny or turns them away fails here.
    QcContentRegistry m_content;

    QcEntityMesher::ItemLook baseLook(const QString &item) const
    {
        return QcFigureLook::item(m_content, m_content.itemId(item).value_or(qcNoItem));
    }

    // The iron pickaxe as the base content holds it, with the pickaxe shape below.
    QcEntityMesher::ItemLook tool() const
    {
        QcEntityMesher::ItemLook look = baseLook(u"qtcraft:iron_pickaxe"_s);
        look.sprite = u"t:pickaxe"_s;
        look.shape = pickaxe();
        return look;
    }

    // Stone, a full block, as the base content holds it.
    QcEntityMesher::ItemLook block() const { return baseLook(u"qtcraft:stone"_s); }

private slots:
    void initTestCase()
    {
        const QcResult<QcLoadedPack> base = QcBaseContent::loadInto(m_content);
        QVERIFY(base);
        QVERIFY(m_content.finish().isEmpty());
        QCOMPARE(tool().grip, QcHoldGrip::Handle);
        QVERIFY(block().cube && block().boxes.isEmpty());
    }

    void spriteShapeFindsTheHandle()
    {
        const QcSpriteShape shape = *pickaxe();
        QCOMPARE(shape.size(), 16);
        QCOMPARE(shape.bounds(), QRect(0, 1, 15, 14));
        QVERIFY(shape.opaque(0, 14));
        QVERIFY(!shape.opaque(-1, 14));
        QVERIFY(!shape.opaque(0, 16));
        // From the end of the handle two texels up and to the right along it.
        QCOMPARE(shape.handle(), QPoint(2, 12));

        // A stick standing upright is held two texels above its end.
        QCOMPARE(QcSpriteShape::fromRows({u".#."_s, u".#."_s, u".#."_s}).handle(), QPoint(1, 0));

        QImage tile(4, 4, QImage::Format_ARGB32);
        tile.fill(Qt::transparent);
        // Opaque from the alpha the entity shader still draws (0.1), clear below it.
        tile.setPixel(1, 2, qRgba(10, 20, 30, 200));
        tile.setPixel(2, 2, qRgba(10, 20, 30, QcSpriteShape::minOpaqueAlpha));
        tile.setPixel(3, 2, qRgba(10, 20, 30, QcSpriteShape::minOpaqueAlpha - 1));
        QVERIFY(QcSpriteShape::minOpaqueAlpha / 255.0 >= 0.1 && (QcSpriteShape::minOpaqueAlpha - 1) / 255.0 < 0.1);
        const QcSpriteShape cut = QcSpriteShape::fromImage(tile);
        QVERIFY(cut.opaque(1, 2));
        QVERIFY(cut.opaque(2, 2));
        QVERIFY(!cut.opaque(3, 2));
        QCOMPARE(cut.bounds(), QRect(1, 2, 2, 1));
        // Two texels side by side: their middle, and six sides (not between them).
        QCOMPARE(cut.centroid(), QPointF(2.0, 2.5));
        QCOMPARE(cut.sides().size(), 6);
        QVERIFY(QcSpriteShape::fromImage(QImage(4, 2, QImage::Format_ARGB32)).isEmpty());
    }

    void extrudedSpritesHaveSidesOnTheirEdges()
    {
        const auto identity = [](const QcVec3 &p) { return p; };

        // One texel: front, back and four sides, every face turned outwards.
        const QcSpriteShape texel = QcSpriteShape::fromRows({u"#"_s});
        QcEntityMesh cube;
        QcEntityMesher::appendExtrudedSprite(cube, {texel, anyUv({}), 1.0, 1.0f}, identity);
        QCOMPARE(cube.vertices.size(), 6 * quadVertices);
        QCOMPARE(cube.indices.size(), 6 * 6);
        const QcVec3 middle{0.0, 0.5, 0.0};
        for (qsizetype quad = 0; quad < 6; ++quad) {
            QVERIFY2(normalOf(cube, quad).dot(quadCentre(cube, quad) - middle) > 0.0, qPrintable(QString::number(quad)));
        }
        // One texel thick: the faces lie half a texel in front of and behind the middle.
        QCOMPARE(std::abs(quadCentre(cube, 0).z), 0.5);

        // Three texels in an L: two faces and eight edges to clear texels or the outside.
        const QcSpriteShape corner = QcSpriteShape::fromRows({u"#."_s, u"##"_s});
        QcEntityMesh ell;
        QcEntityMesher::appendExtrudedSprite(ell, {corner, anyUv({}), 1.0, 1.0f}, identity);
        QCOMPARE(ell.vertices.size(), (2 + 8) * 4);

        // A side takes the colour of its texel: all four corners at the texel's centre.
        const QcEntityVertex &side = ell.vertices[2 * quadVertices];
        QVERIFY(std::ranges::all_of(ell.vertices.mid(2 * quadVertices, 4), [&](const QcEntityVertex &v) { return v.u == side.u && v.v == side.v; }));

        QcEntityMesh none;
        QcEntityMesher::appendExtrudedSprite(none, {QcSpriteShape(), anyUv({}), 1.0, 1.0f}, identity);
        QVERIFY(none.vertices.isEmpty());
    }

    void firstPersonToolStandsInTheFist()
    {
        const QcEntityMesher::ItemLook pick = tool();
        QcEntityMesh mesh;
        QcEntityMesher::appendHand(mesh, {m_look, &pick}, {}, {{}, 0.0, 0.0, 1.0f}, anyUv);
        QVERIFY(mesh.vertices.size() > armVertices + 2 * quadVertices);

        // Large in the lower right, right of the crosshair: from the end of the handle, hidden in
        // the fist near the bottom of the screen, up to the head a little above the crosshair; the
        // part above the arm is about a third of the screen high.
        const QRectF item = screenBounds(mesh, armVertices, mesh.vertices.size());
        QVERIFY2(item.height() > 1.2 && item.height() < 1.7, qPrintable(QString::number(item.height())));
        QVERIFY2(item.center().x() > 0.15 && item.right() < 1.0, qPrintable(QString::number(item.center().x())));
        // The rectangle has y up: top() is its lowest point, bottom() its highest.
        QVERIFY2(item.bottom() > 0.1 && item.bottom() < 0.5 && item.top() > -1.3, qPrintable(QString::number(item.bottom())));

        // The arm comes up from below the screen to the fist.
        const QRectF arm = screenBounds(mesh, 0, armVertices);
        QVERIFY(arm.top() < -1.0);
        QVERIFY(arm.bottom() > -1.0);

        // The end of the handle lies inside the arm's end.
        QVERIFY2(handleToArm(mesh) < 0.15, qPrintable(QString::number(handleToArm(mesh))));

        // At rest the flat side faces the viewer, so the tool is readable: the front's normal
        // points mostly along the line of sight.
        QVERIFY2(facing(mesh) > 0.6, qPrintable(QString::number(facing(mesh))));
    }

    void swingTurnsThePointTowardsTheBlock()
    {
        const QcEntityMesher::ItemLook pick = tool();
        QVERIFY(pick.swing.rotate != pick.firstPerson.rotate);
        QcEntityPose swinging;
        const auto meshAt = [&](double progress) {
            swinging.swingProgress = progress;
            QcEntityMesh mesh;
            QcEntityMesher::appendHand(mesh, {m_look, &pick}, QcAnimator::animateHand(swinging, 0.0, -1.0, -1.0, -1.0), {{}, 0.0, 0.0, 1.0f}, anyUv);
            return mesh;
        };

        // The turn into the swing placement runs smoothly there and back: none at the start and
        // the end, all of it in the middle of the swing.
        QCOMPARE(QcAnimator::animateHand(swinging, 0.0, -1.0, -1.0, -1.0).swing, 0.0);
        const double rest = facing(meshAt(-1.0));
        QCOMPARE(facing(meshAt(0.0)), rest);
        QVERIFY(std::abs(facing(meshAt(1.0)) - rest) < 1e-6);

        // In the middle of the swing (pitching about the camera's x axis) the flat side faces
        // sideways (mostly along x), so the point of the head, not its face, comes down onto the
        // block.
        const QcEntityMesh middle = meshAt(0.5);
        const QcVec3 face = normalOf(middle, armVertices / quadVertices);
        QVERIFY2(std::abs(face.x) > 0.85 * face.length(), qPrintable(u"%1 %2 %3"_s.arg(face.x).arg(face.y).arg(face.z)));

        // The handle stays in the fist all the time.
        for (const double progress : {0.15, 0.3, 0.5, 0.7, 0.9}) {
            const QcEntityMesh mesh = meshAt(progress);
            QVERIFY2(handleToArm(mesh) < 0.15, qPrintable(u"%1: %2"_s.arg(progress).arg(handleToArm(mesh))));
        }
    }

    void swingMovesArmAndToolTogether()
    {
        const QcEntityMesher::ItemLook pick = tool();
        QcEntityPose swinging;
        const auto toolAt = [&](double progress) {
            swinging.swingProgress = progress;
            QcEntityMesh mesh;
            QcEntityMesher::appendHand(mesh, {m_look, &pick}, QcAnimator::animateHand(swinging, 0.0, -1.0, -1.0, -1.0), {{}, 0.0, 0.0, 1.0f}, anyUv);
            return screenBounds(mesh, armVertices, mesh.vertices.size());
        };

        const QRectF rest = toolAt(-1.0);
        const QRectF top = toolAt(QcAnimator::swingTop);
        const QRectF bottom = toolAt(QcAnimator::swingBottom);
        // Up first, then down and inwards, always on the screen.
        QVERIFY(top.top() > rest.top());
        QVERIFY(bottom.bottom() < rest.bottom());
        QVERIFY(bottom.center().x() < rest.center().x());
        for (const QRectF &moment : {rest, top, bottom}) {
            QVERIFY(moment.intersects(QRectF(-1.0, -1.0, 2.0, 2.0)));
        }
    }

    void blocksAndShapelessSpritesSitOnTheFist()
    {
        const QcEntityMesher::ItemLook cube = block();
        QcEntityMesh mesh;
        QcEntityMesher::appendHand(mesh, {m_look, &cube}, {}, {{}, 0.0, 0.0, 1.0f}, anyUv);
        QCOMPARE(mesh.vertices.size(), 2 * armVertices);
        // A cube as large as the block scale: its corners at most a space diagonal apart.
        double diagonal = 0.0;
        for (qsizetype i = armVertices; i < mesh.vertices.size(); ++i) {
            diagonal = std::max(diagonal, (at(mesh.vertices[i]) - at(mesh.vertices[armVertices])).length());
        }
        const double edge = cube.firstPerson.scale.x;
        QVERIFY2(std::abs(diagonal - edge * std::sqrt(3.0)) < 0.01, qPrintable(QString::number(diagonal)));
        const QRectF onScreen = screenBounds(mesh, armVertices, mesh.vertices.size());
        QVERIFY2(onScreen.height() > 0.25 && onScreen.height() < 0.6, qPrintable(QString::number(onScreen.height())));
        QVERIFY(onScreen.center().x() > 0.1);

        // Without a shape a sprite is flat: two quads, as large as one with a shape.
        QcEntityMesher::ItemLook flat = tool();
        flat.shape.reset();
        QcEntityMesh sprite;
        QcEntityMesher::appendHand(sprite, {m_look, &flat}, {}, {{}, 0.0, 0.0, 1.0f}, anyUv);
        QCOMPARE(sprite.vertices.size(), armVertices + 2 * quadVertices);
        QVERIFY(screenBounds(sprite, armVertices, sprite.vertices.size()).height() > 0.5);
    }

    void figuresHoldToolsByTheHandle()
    {
        const QcEntityMesher::ItemLook pick = tool();
        QcEntityMesh mesh;
        QcEntityMesher::appendModel(mesh, {m_look, &pick}, QcAnimator::animateBody({}, 0.0), {}, anyUv);
        QVERIFY(mesh.vertices.size() > armVertices + 2 * quadVertices);

        // The arm hangs at x 0.25..0.45, its end at y 0.7, the fist just in front of it (z -0.05).
        // The tool points forward out of the fist: the end of the handle, its rearmost point, stays
        // in the fist; the head is well in front of the arm and not far above the grip.
        const auto byZ = [](const QcEntityVertex &a, const QcEntityVertex &b) { return a.z < b.z; };
        const auto back = std::ranges::max_element(mesh.vertices.begin() + armVertices, mesh.vertices.end(), byZ);
        QVERIFY2(back->x > 0.2 && back->x < 0.5, qPrintable(QString::number(back->x)));
        QVERIFY2(back->y > 0.6 && back->y < 0.9, qPrintable(QString::number(back->y)));
        QVERIFY2(back->z > -0.1 && back->z < 0.15, qPrintable(QString::number(back->z)));
        const auto front = std::ranges::min_element(mesh.vertices.begin() + armVertices, mesh.vertices.end(), byZ);
        QVERIFY2(front->z < -0.5, qPrintable(QString::number(front->z)));
        QVERIFY2(front->y > 0.6 && front->y < 1.0, qPrintable(QString::number(front->y)));
    }
};

QTEST_GUILESS_MAIN(TestHeldItem)
#include "test_held_item.moc"
