#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/meshing/QcFigureLook.h"
#include "clientcore/meshing/QcHandSwing.h"
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

// The first point along the edges of a hand's quads that lies on the crosshair or on the hotbar's
// slots (the middle 76 % of the width, below -0.77 in y), as the default view shows them; empty
// when there is none. A held sprite's front and back (the two quads after the arm) span its whole
// square, transparent corners included; its sides outline the opaque texels and stand for it
// (`sprite`; a block's faces all count).
QString hudOverlap(const QcEntityMesh &mesh, bool sprite)
{
    const QRectF hotbar(QPointF(-0.38, -1.0), QPointF(0.38, -0.77));
    for (qsizetype quad = 0; quad + 3 < mesh.vertices.size(); quad += 4) {
        if (sprite && (quad == armVertices || quad == armVertices + quadVertices)) {
            continue;
        }
        for (int corner = 0; corner < 4; ++corner) {
            const QcVec3 a = at(mesh.vertices[quad + corner]);
            const QcVec3 b = at(mesh.vertices[quad + (corner + 1) % 4]);
            for (int i = 0; i <= 8; ++i) {
                const QPointF p = screen(a + (b - a) * (i / 8.0));
                if (hotbar.contains(p) || std::hypot(p.x(), p.y()) < 0.1) {
                    return u"%1 %2 (%3)"_s.arg(p.x()).arg(p.y()).arg(quad < armVertices ? u"arm"_s : u"item"_s);
                }
            }
        }
    }
    return {};
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

    // The first-person hand holding `look` at `progress` of a swing (negative: at rest), of the
    // mining loop or a click's.
    QcEntityMesh handMesh(const QcEntityMesher::ItemLook &look, double progress, bool mining) const
    {
        QcEntityPose pose;
        pose.swingProgress = progress;
        QcEntityMesh mesh;
        QcEntityMesher::appendHand(mesh, {m_look, &look}, QcAnimator::animateHand(pose, 0.0, {.mining = mining}), {{}, 0.0, 0.0, 1.0f}, anyUv);
        return mesh;
    }

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

        // In the lower right corner like the reference (decision E-881): the head a little above
        // the crosshair's height but well right of it, the handle running down out of the screen
        // (handKeepsClearOfCrosshairAndHotbar checks the crosshair itself).
        const QRectF item = screenBounds(mesh, armVertices, mesh.vertices.size());
        QVERIFY2(item.left() > 0.15 && item.right() > 0.8, qPrintable(u"%1 %2"_s.arg(item.left()).arg(item.right())));
        // The rectangle has y up: top() is its lowest point, bottom() its highest.
        QVERIFY2(item.bottom() < 0.45 && item.bottom() > -0.3 && item.top() < -0.6, qPrintable(u"%1 %2"_s.arg(item.bottom()).arg(item.top())));
        QVERIFY2(item.height() > 0.5, qPrintable(QString::number(item.height())));

        // The arm comes up from below the screen to the fist.
        const QRectF arm = screenBounds(mesh, 0, armVertices);
        QVERIFY(arm.top() < -1.0);
        QVERIFY(arm.bottom() > -1.0);

        // The end of the handle lies at the arm's end (near one of its corners; the larger tool's
        // grip sits a little farther up the handle).
        QVERIFY2(handleToArm(mesh) < 0.2, qPrintable(QString::number(handleToArm(mesh))));

        // At rest the flat side faces the viewer nearly squarely, like the reference (decision
        // E-881), so the tool reads clearly.
        QVERIFY2(facing(mesh) > 0.6, qPrintable(QString::number(facing(mesh))));
    }

    // Decision E-881: arm and tool swing as one rigid body. Every distance between two vertices of
    // the hand (arm and tool alike) stays what it is at rest over the whole swing, for the mining
    // loop and for a click; nothing bends, shears or squashes.
    void swingIsRigid()
    {
        const QcEntityMesher::ItemLook pick = tool();
        const QcEntityMesh rest = handMesh(pick, -1.0, true);
        // Every fifth vertex, so arm, front, back and sides are all sampled.
        QList<qsizetype> sample;
        for (qsizetype i = 0; i < rest.vertices.size(); i += 5) {
            sample.append(i);
        }
        QVERIFY(sample.size() > 20);

        for (const bool mining : {true, false}) {
            for (int step = 0; step <= 20; ++step) {
                const double progress = step / 20.0;
                const QcEntityMesh moved = handMesh(pick, progress, mining);
                QCOMPARE(moved.vertices.size(), rest.vertices.size());
                double worst = 0.0;
                for (const qsizetype a : sample) {
                    for (const qsizetype b : sample) {
                        const double before = (at(rest.vertices[a]) - at(rest.vertices[b])).length();
                        const double after = (at(moved.vertices[a]) - at(moved.vertices[b])).length();
                        worst = std::max(worst, std::abs(after - before));
                    }
                }
                QVERIFY2(worst < 1e-4, qPrintable(u"mining %1, progress %2: %3 m"_s.arg(mining).arg(progress).arg(worst)));
                // The handle never leaves the fist.
                QVERIFY2(handleToArm(moved) < 0.2, qPrintable(QString::number(progress)));
            }
        }
    }

    // Decision E-881: from the corner the strike travels a good part of the way towards the middle
    // of the screen (the head's left end moves by more than a tenth of the screen's width) after a
    // lift, and back.
    void swingStrikesTowardsTheMiddle()
    {
        const QcEntityMesher::ItemLook pick = tool();
        const auto toolAt = [&](double progress) {
            const QcEntityMesh mesh = handMesh(pick, progress, true);
            return screenBounds(mesh, armVertices, mesh.vertices.size());
        };
        const QcHandSwing::Shape &shape = QcHandSwing::shapeOf(QcHandSwing::Style::Mining);

        const QRectF rest = toolAt(-1.0);
        const QRectF top = toolAt(shape.windUpEnd);
        const QRectF bottom = toolAt(shape.strikeEnd);
        // bottom() is the highest point (y up).
        QVERIFY(top.bottom() > rest.bottom());
        QVERIFY2(rest.left() - bottom.left() > 0.2, qPrintable(u"%1 %2"_s.arg(rest.left()).arg(bottom.left())));
        QVERIFY(std::abs(toolAt(1.0).left() - rest.left()) < 1e-6);
    }

    // Decision E-881: at no moment of a mining swing or a click do arm or held item reach the
    // crosshair or cover the hotbar, as the default view shows them.
    void handKeepsClearOfCrosshairAndHotbar()
    {
        for (const QString &name : {u"pickaxe"_s, u"qtcraft:iron_sword"_s, u"qtcraft:iron_axe"_s, u"qtcraft:stone"_s}) {
            const QcEntityMesher::ItemLook look = name == u"pickaxe"_s ? tool() : baseLook(name);
            for (const bool mining : {true, false}) {
                for (int step = -1; step <= 20; ++step) {
                    const QString overlap = hudOverlap(handMesh(look, step / 20.0, mining), !look.cube);
                    QVERIFY2(overlap.isEmpty(), qPrintable(u"%1 mining %2 step %3: %4"_s.arg(name).arg(mining).arg(step).arg(overlap)));
                }
            }
        }
    }

    // A mod's swing placement (ItemLook::swing, decisions E-831 and E-881; the base content sets
    // none): over a swing the item turns about the grip from its rest placement towards it, not at
    // all where the swing starts and ends, fully in its middle, the handle in the fist throughout.
    void modSwingPlacementTurnsInTheFist()
    {
        const QcEntityMesher::ItemLook plain = tool();
        QCOMPARE(plain.swing.rotate, plain.firstPerson.rotate);
        QcEntityMesher::ItemLook turning = plain;
        turning.swing.rotate = plain.firstPerson.rotate + QcVec3{0.0, 40.0, -30.0};
        // The same item resting in the swing placement: what the turning one shows halfway.
        QcEntityMesher::ItemLook turned = turning;
        turned.firstPerson = turning.swing;
        turned.firstPerson.scale = plain.firstPerson.scale;
        turned.swing = turned.firstPerson;

        // The largest distance between the same vertex of two meshes.
        const auto apart = [](const QcEntityMesh &a, const QcEntityMesh &b) {
            double worst = 0.0;
            for (qsizetype i = 0; i < a.vertices.size(); ++i) {
                worst = std::max(worst, (at(a.vertices[i]) - at(b.vertices[i])).length());
            }
            return worst;
        };
        for (const double progress : {0.0, 1.0}) {
            QVERIFY2(apart(handMesh(turning, progress, true), handMesh(plain, progress, true)) < 1e-6, qPrintable(QString::number(progress)));
        }
        QVERIFY(apart(handMesh(turning, 0.5, true), handMesh(turned, 0.5, true)) < 1e-6);
        QVERIFY(apart(handMesh(turning, 0.5, true), handMesh(plain, 0.5, true)) > 0.05);
        for (const double progress : {0.25, 0.5, 0.75}) {
            QVERIFY2(handleToArm(handMesh(turning, progress, true)) < 0.2, qPrintable(QString::number(progress)));
        }
    }

    // Decision E-881: tools are held larger than before (rest scale 0.45), so the head reads
    // clearly, and leaning to the right.
    void toolsAreHeldLargeAndLeaningRight()
    {
        constexpr double previousScale = 0.45;
        for (const QString &item : {u"qtcraft:iron_pickaxe"_s, u"qtcraft:iron_sword"_s, u"qtcraft:iron_axe"_s, u"qtcraft:iron_shovel"_s,
                                    u"qtcraft:iron_hoe"_s}) {
            const QcEntityMesher::ItemLook look = baseLook(item);
            const double factor = look.firstPerson.scale.x / previousScale;
            QVERIFY2(factor >= 1.2 && factor <= 1.6, qPrintable(u"%1: %2"_s.arg(item).arg(factor)));
        }

        // From the end of the handle to the sprite's upper right corner (the end of the head's
        // right prong): up, to the right and forward (-z), 15 to 40 degrees off the vertical, so
        // the tool leans to the right like the reference.
        const QcEntityMesh mesh = handMesh(tool(), -1.0, true);
        const auto front = mesh.vertices.mid(armVertices, quadVertices);
        const auto corner = [&](float u, float v) { return at(*std::ranges::find_if(front, [&](const QcEntityVertex &w) { return w.u == u && w.v == v; })); };
        const QcVec3 lowerLeft = corner(0.0f, 1.0f);
        const QcVec3 handle = lowerLeft + (corner(1.0f, 1.0f) - lowerLeft) * (0.5 / 16.0) + (corner(0.0f, 0.0f) - lowerLeft) * (1.5 / 16.0);
        const QcVec3 upperRight = corner(1.0f, 0.0f);
        const QcVec3 along = upperRight - handle;
        const double offVertical = qRadiansToDegrees(std::acos(along.y / along.length()));
        QVERIFY2(along.y > 0.0 && along.x > 0.0 && along.z < 0.0, qPrintable(u"%1 %2 %3"_s.arg(along.x).arg(along.y).arg(along.z)));
        QVERIFY2(offVertical > 15.0 && offVertical < 40.0, qPrintable(QString::number(offVertical)));

        // A sword shows enough of its flat side that the blade reads as a blade, not as an edge.
        const double sword = facing(handMesh(baseLook(u"qtcraft:iron_sword"_s), -1.0, true));
        QVERIFY2(sword > 0.3, qPrintable(QString::number(sword)));
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
