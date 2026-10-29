// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcCamera.h"
#include "clientcore/camera/QcFrustum.h"
#include "clientcore/camera/QcRenderOrigin.h"
#include "clientcore/meshing/QcEnchantingMesher.h"
#include "clientcore/meshing/QcEntityMesh.h"

#include <QTest>

#include <cmath>

// Camera-relative rendering (decision E-883): the arithmetic of the shaders reproduced in float on
// the CPU. A vertex is stored relative to its section's corner, the draw takes the corner's offset
// from the render anchor (subtracted in double), the frame the camera's offset from the anchor, and
// the shader adds and subtracts them in float: the position relative to the camera must come out
// within a millimetre however far out the camera stands and wherever it is within the anchor's reach.
namespace {

// The block shader: the vertex relative to the camera, as the GPU computes it.
QVector3D shaderRelative(QVector3D local, QVector4D entry, QVector3D drawCamera)
{
    return local + entry.toVector3D() - drawCamera;
}

// The exact position relative to the camera, in double.
QcVec3 exactRelative(QcBlockPos corner, QVector3D local, const QcVec3 &camera)
{
    return QcRenderOrigin::toVec3(corner) + QcVec3{local.x(), local.y(), local.z()} - camera;
}

double distance(QVector3D a, const QcVec3 &b)
{
    return (QcVec3{a.x(), a.y(), a.z()} - b).length();
}

// The section corner of a block coordinate.
int sectionCorner(double coordinate)
{
    return static_cast<int>(std::floor(coordinate / 16.0)) * 16;
}

} // namespace

class TestCameraRelative : public QObject
{
    Q_OBJECT

private slots:
    void vertexPositionsStayExactFarOut_data()
    {
        QTest::addColumn<double>("far");
        // How far the camera has moved from where the anchor was placed: none, and almost the
        // anchor's reach, the largest offsets the shaders subtract.
        QTest::addColumn<bool>("atReach");
        for (const double far : {1e3, 1e6, 1e8, 1e9}) {
            const QByteArray name = QByteArray::number(far, 'g', 1);
            QTest::addRow("%s at the anchor", name.constData()) << far << false;
            QTest::addRow("%s at the reach", name.constData()) << far << true;
        }
    }

    void vertexPositionsStayExactFarOut()
    {
        QFETCH(double, far);
        QFETCH(bool, atReach);
        const QcVec3 drift = atReach ? QcVec3{127.55, -60.41, -127.87} : QcVec3{};
        // A camera between whole blocks and sections up to 200 m around it, near and far in view.
        QcCamera camera;
        const QcVec3 placed{far + 0.371, 70.62, -far - 0.813};
        const QcVec3 anchor = QcRenderOrigin::blockNear(placed);
        camera.position = placed + drift;
        QVERIFY(!QcRenderOrigin::outOfReach(anchor, camera.position));
        const QVector3D drawCamera = QcRenderOrigin::entry(camera.position, anchor).toVector3D();
        camera.yaw = 30.0f;
        camera.pitch = -20.0f;
        const QMatrix4x4 viewProjection = camera.projectionMatrix(16.0f / 9.0f, 512.0f) * camera.viewRotation();
        const QVector3D local(7.25f, 3.5f, 12.75f);
        for (const double away : {0.0, 3.0, 47.5, 199.0}) {
            const QcBlockPos corner{sectionCorner(camera.position.x + away), 64, sectionCorner(camera.position.z - away)};
            const QVector4D entry = QcRenderOrigin::entry(QcRenderOrigin::toVec3(corner), anchor);
            // The corner's offset from the anchor is a whole number, exact in a float.
            QCOMPARE(static_cast<double>(entry.x()), corner.x - anchor.x);
            const QVector3D relative = shaderRelative(local, entry, drawCamera);
            const QcVec3 exact = exactRelative(corner, local, camera.position);
            QVERIFY2(distance(relative, exact) < 1e-3, qPrintable(QString::number(distance(relative, exact))));
            // Near the camera, where an error would show most, within a tenth of a millimetre.
            if (away < 10.0) {
                QVERIFY2(distance(relative, exact) < 1e-4, qPrintable(QString::number(distance(relative, exact))));
            }

            // The projection works on small numbers: the same point on screen as the exact one.
            const QVector3D onScreen = viewProjection.map(relative);
            const QVector3D exactFloat(static_cast<float>(exact.x), static_cast<float>(exact.y), static_cast<float>(exact.z));
            const QVector3D exactOnScreen = viewProjection.map(exactFloat);
            QVERIFY((onScreen - exactOnScreen).length() < 1e-4f);
        }
    }

    void absoluteFloatsWouldMissFarOut()
    {
        // Why (decision E-883): the old absolute floats place a vertex 1e8 m out metres away.
        const double far = 1e8;
        const double world = far + 7.25;
        const auto stored = static_cast<float>(world);
        const auto eye = static_cast<float>(far + 0.371);
        QVERIFY(std::abs(static_cast<double>(stored - eye) - (world - (far + 0.371))) > 1.0);
    }

    void cullingWorksRelativeToTheCamera()
    {
        // As QcWorldRenderer::cull: the corner's entry minus the camera's offset from the anchor.
        QcCamera camera;
        camera.position = {1e9 + 0.5, 70.0, -1e9 + 0.5};
        const QcVec3 anchor = QcRenderOrigin::blockNear(camera.position + QcVec3{-100.25, 30.0, 90.75});
        const QVector3D drawCamera = QcRenderOrigin::entry(camera.position, anchor).toVector3D();
        const QcFrustum::Planes planes = QcFrustum::planes(camera.projectionMatrix(16.0f / 9.0f, 256.0f) * camera.viewRotation());
        const auto visible = [&](QcBlockPos corner) {
            const QVector3D offset = QcRenderOrigin::entry(QcRenderOrigin::toVec3(corner), anchor).toVector3D() - drawCamera;
            return QcFrustum::intersectsBox(planes, offset, offset + QVector3D(16.0f, 16.0f, 16.0f));
        };
        // Yaw 0 looks north (-z).
        QVERIFY(visible({1'000'000'000, 64, -1'000'000'000 - 64}));
        QVERIFY(!visible({1'000'000'000, 64, -1'000'000'000 + 64}));
        QVERIFY(!visible({1'000'000'000 + 512, 64, -1'000'000'000 - 64}));
    }

    // Decision E-910: a camera exactly on a whole block (here also on a section border, so the two
    // faces left and right of the centre belong to different sections and entries) shows no seam
    // in the middle of the picture. The anchor is a whole block (blockNear, floor()), placed at the
    // camera or left behind by up to anchorReach as QcDrawOrigins::follow keeps it: a whole-number
    // camera gives a whole-number drawCamera, and every term is exact in a float. So the edge the
    // faces share, in the vertical plane through the eye, comes out the same from both sections,
    // exactly in the centre column (x = 0 after the projection), and a camera a hair before the
    // whole block (anchor one block lower by floor()) moves every vertex only by that hair.
    // Scope: this guards the precision of QcRenderOrigin::entry and blockNear; that block.vert adds
    // in the order local + entry - drawCamera (shaderRelative) is a requirement it does not see.
    void wholeBlockCameraLeavesNoSeamInTheMiddle_data()
    {
        QTest::addColumn<double>("far");
        QTest::addColumn<float>("pitch");
        // Whether the anchor stayed behind (127, 3, -100 blocks away, within its reach) instead of
        // having just been placed at the camera.
        QTest::addColumn<bool>("anchorBehind");
        for (const double far : {0.0, 1024.0, 1048576.0, 1e9}) {
            for (const float pitch : {-30.0f, 0.0f, 25.0f}) {
                const QByteArray name = QByteArray::number(far, 'g', 7);
                const int degrees = static_cast<int>(pitch);
                QTest::addRow("%s pitch %d, anchor at the camera", name.constData(), degrees) << far << pitch << false;
                QTest::addRow("%s pitch %d, anchor behind", name.constData(), degrees) << far << pitch << true;
            }
        }
    }

    void wholeBlockCameraLeavesNoSeamInTheMiddle()
    {
        QFETCH(double, far);
        QFETCH(float, pitch);
        QFETCH(bool, anchorBehind);
        // The camera on the border of two sections at x, looking north (-z) along the border.
        QcCamera camera;
        camera.position = {far, 71.0, -far};
        camera.yaw = 0.0f;
        camera.pitch = pitch;
        const QMatrix4x4 viewProjection = camera.projectionMatrix(16.0f / 10.0f, 512.0f) * camera.viewRotation();
        const QcVec3 placedAt = anchorBehind ? camera.position - QcVec3{127.0, 3.0, -100.0} : camera.position;
        const QcVec3 anchor = QcRenderOrigin::blockNear(placedAt);
        QVERIFY(!QcRenderOrigin::outOfReach(anchor, camera.position));
        const QVector3D drawCamera = QcRenderOrigin::entry(camera.position, anchor).toVector3D();
        const QVector3D expectedDrawCamera = anchorBehind ? QVector3D(127.0f, 3.0f, -100.0f) : QVector3D();
        QCOMPARE(drawCamera, expectedDrawCamera);

        // The sections west and east of the border, and a point of their shared edge: x 16 in the
        // western section, x 0 in the eastern one, several blocks below and ahead of the camera.
        const int border = static_cast<int>(far);
        const int sectionZ = sectionCorner(-far - 40.0);
        const QcBlockPos west{border - 16, 64, sectionZ};
        const QcBlockPos east{border, 64, sectionZ};
        const QVector4D westEntry = QcRenderOrigin::entry(QcRenderOrigin::toVec3(west), anchor);
        const QVector4D eastEntry = QcRenderOrigin::entry(QcRenderOrigin::toVec3(east), anchor);
        for (const float along : {0.0f, 3.0f, 7.5f, 15.0f, 16.0f}) {
            const QVector3D fromWest = shaderRelative({16.0f, 2.0f, along}, westEntry, drawCamera);
            const QVector3D fromEast = shaderRelative({0.0f, 2.0f, along}, eastEntry, drawCamera);
            QCOMPARE(fromWest, fromEast);
            QCOMPARE(fromWest.x(), 0.0f);
            QCOMPARE((viewProjection * QVector4D(fromWest, 1.0f)).x(), 0.0f);
        }

        // A hair before the whole block, floor() puts a freshly placed anchor a block lower: the
        // vertices move by the hair only.
        const QcVec3 before = camera.position - QcVec3{1e-6, 1e-6, 1e-6};
        const QcVec3 lowerAnchor = QcRenderOrigin::blockNear(before);
        QVERIFY((lowerAnchor == camera.position - QcVec3{1.0, 1.0, 1.0}));
        const QVector3D drawBefore = QcRenderOrigin::entry(before, lowerAnchor).toVector3D();
        const QVector4D eastBefore = QcRenderOrigin::entry(QcRenderOrigin::toVec3(east), lowerAnchor);
        const QVector3D local(0.0f, 2.0f, 7.5f);
        const QVector3D moved = shaderRelative(local, eastBefore, drawBefore) - shaderRelative(local, eastEntry, drawCamera);
        QVERIFY2(moved.length() < 1e-4f, qPrintable(QString::number(moved.length())));
    }

    void anchorFollowsOnlyBeyondItsReach()
    {
        const QcVec3 anchor{1e9, 70.0, -1e9};
        QVERIFY(!QcRenderOrigin::outOfReach(anchor, anchor));
        QVERIFY(!QcRenderOrigin::outOfReach(anchor, anchor + QcVec3{QcRenderOrigin::anchorReach, -QcRenderOrigin::anchorReach, 0.0}));
        QVERIFY(QcRenderOrigin::outOfReach(anchor, anchor + QcVec3{QcRenderOrigin::anchorReach + 0.01, 0.0, 0.0}));
        QVERIFY(QcRenderOrigin::outOfReach(anchor, anchor + QcVec3{0.0, -QcRenderOrigin::anchorReach - 0.01, 0.0}));
        QVERIFY(QcRenderOrigin::outOfReach(anchor, anchor + QcVec3{0.0, 0.0, 1e6}));
    }

    void sectionCellsCarryTheCornerModuloThePeriod()
    {
        // Unpacked as block.vert does: x + 64 y + 4096 z in sixteens.
        static_assert(QcRenderOrigin::hashPeriod == 1024);
        const auto unpacked = [](float packed) {
            const float z = std::floor(packed / 4096.0f);
            const float y = std::floor((packed - z * 4096.0f) / 64.0f);
            const float x = packed - z * 4096.0f - y * 64.0f;
            return QVector3D(x, y, z) * 16.0f;
        };
        QCOMPARE(unpacked(QcRenderOrigin::sectionCell({0, 0, 0})), QVector3D(0.0f, 0.0f, 0.0f));
        QCOMPARE(unpacked(QcRenderOrigin::sectionCell({240, 48, -16})), QVector3D(240.0f, 48.0f, 1008.0f));
        QCOMPARE(unpacked(QcRenderOrigin::sectionCell({1008, 240, 1024 + 512})), QVector3D(1008.0f, 240.0f, 512.0f));
        QCOMPARE(unpacked(QcRenderOrigin::sectionCell({QcWorld::maxHorizontalBlock, 240, -QcWorld::maxHorizontalBlock})),
                 QVector3D(1008.0f, 240.0f, 16.0f));
        // The largest packed value stays a whole number a float holds exactly.
        const float largest = QcRenderOrigin::sectionCell({1008, 1008, 1008});
        QCOMPARE(largest, 63.0f + 64.0f * 63.0f + 4096.0f * 63.0f);
        QVERIFY(largest < static_cast<float>(1 << 24));

        // A corner shared across the wrap gets the same hash input on both sides.
        const auto hashInput = [&](int corner, float local) {
            return std::fmod(local + unpacked(QcRenderOrigin::sectionCell({corner, 0, 0})).x(), static_cast<float>(QcRenderOrigin::hashPeriod));
        };
        QCOMPARE(hashInput(1008, 16.0f), hashInput(1024, 0.0f));
        QCOMPARE(hashInput(240, 16.0f), hashInput(256, 0.0f));
        QCOMPARE(hashInput(-16, 16.0f), hashInput(0, 0.0f));
    }

    void entityVerticesAreRelativeToTheMeshOrigin()
    {
        QcEntityMesh mesh;
        mesh.origin = QcRenderOrigin::blockNear({1e9 + 0.7, 70.2, -1e9 - 0.2});
        QVERIFY(mesh.origin == (QcVec3{1e9, 70.0, -1e9 - 1.0}));
        const QcEntityVertex vertex = mesh.vertexAt({1e9 + 1.125, 71.5, -1e9 - 0.75});
        QCOMPARE(vertex.x, 1.125f);
        QCOMPARE(vertex.y, 1.5f);
        QCOMPARE(vertex.z, 0.25f);
    }

    // The enchanting table's book and runes (decision E-884) are written relative to the mesh origin
    // too: far out they lie within a millimetre of where they belong, and stay small floats.
    void enchantingBookAndRunesAreRelativeToTheMeshOrigin()
    {
        const QcVec3 table{1e8 + 0.5, 65.0, 1e8 + 0.5};
        QcEntityMesh mesh;
        mesh.origin = QcRenderOrigin::blockNear(table + QcVec3{0.0, 1.0, -3.0});
        const QcEntityMesher::UvLookup uv = [](const QString &) { return QRectF(0.0, 0.0, 0.25, 0.25); };
        const QcEnchantingAura::Book book{table + QcVec3{0.0, 0.8, 0.0}, 0.3, 0.7};
        QcEnchantingMesher::appendBook(mesh, book, QcEntityLight(1.0f, 0.0f), uv);
        const QcEnchantingAura::Rune rune{table + QcVec3{1.25, 0.5, -0.75}, 3, 0.2};
        QcEnchantingMesher::appendRune(mesh, rune, table + QcVec3{0.0, 1.0, -3.0}, uv);
        QCOMPARE(mesh.vertices.size(), 12);

        for (const QcEntityVertex &vertex : std::as_const(mesh.vertices)) {
            // Every corner lies within a metre of the book or the rune, relative to the origin.
            const QcVec3 world = mesh.origin + QcVec3{vertex.x, vertex.y, vertex.z};
            QVERIFY2(std::min((world - book.spine).length(), (world - rune.position).length()) < 1.0, qPrintable(QString::number(vertex.x)));
            QVERIFY(std::abs(vertex.x) < 8.0f && std::abs(vertex.y) < 8.0f && std::abs(vertex.z) < 8.0f);
        }
        // The rune's middle comes out exactly: corners are symmetric about it.
        QcVec3 middle;
        for (qsizetype i = 8; i < 12; ++i) {
            const QcEntityVertex &vertex = mesh.vertices.at(i);
            middle = middle + (mesh.origin + QcVec3{vertex.x, vertex.y, vertex.z}) * 0.25;
        }
        QVERIFY((middle - rune.position).length() < 1e-3);
    }
};

QTEST_GUILESS_MAIN(TestCameraRelative)
#include "test_camera_relative.moc"
