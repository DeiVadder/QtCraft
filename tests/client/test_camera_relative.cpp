#include "clientcore/camera/QcCamera.h"
#include "clientcore/camera/QcFrustum.h"
#include "clientcore/camera/QcRenderOrigin.h"
#include "clientcore/meshing/QcEnchantingMesher.h"
#include "clientcore/meshing/QcEntityMesh.h"

#include <QTest>

#include <cmath>

// Camera-relative rendering (decision E-883): the arithmetic of the shaders reproduced in float on
// the CPU. A vertex is stored relative to its section's corner, the draw takes the corner's offset
// from the camera (subtracted in double), and the shader adds the two in float: the position
// relative to the camera must come out within a millimetre however far out the camera stands.
namespace {

// The block shader: the vertex relative to the camera, as the GPU adds it.
QVector3D shaderRelative(QVector3D local, QVector4D entry)
{
    return local + entry.toVector3D();
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
        QTest::newRow("1e3") << 1e3;
        QTest::newRow("1e6") << 1e6;
        QTest::newRow("1e8") << 1e8;
        QTest::newRow("1e9") << 1e9;
    }

    void vertexPositionsStayExactFarOut()
    {
        QFETCH(double, far);
        // A camera between whole blocks and sections up to 200 m around it, near and far in view.
        QcCamera camera;
        camera.position = {far + 0.371, 70.62, -far - 0.813};
        camera.yaw = 30.0f;
        camera.pitch = -20.0f;
        const QMatrix4x4 viewProjection = camera.projectionMatrix(16.0f / 9.0f, 512.0f) * camera.viewRotation();
        const QVector3D local(7.25f, 3.5f, 12.75f);
        for (const double away : {0.0, 3.0, 47.5, 199.0}) {
            const QcBlockPos corner{sectionCorner(camera.position.x + away), 64, sectionCorner(camera.position.z - away)};
            const QVector4D entry = QcRenderOrigin::entry(QcRenderOrigin::toVec3(corner), camera.position);
            const QVector3D relative = shaderRelative(local, entry);
            const QcVec3 exact = exactRelative(corner, local, camera.position);
            QVERIFY2(distance(relative, exact) < 1e-3, qPrintable(QString::number(distance(relative, exact))));

            // The projection works on small numbers: the same point on screen as the exact one.
            const QVector3D onScreen = viewProjection.map(relative);
            const QVector3D exactOnScreen = viewProjection.map(QVector3D(static_cast<float>(exact.x), static_cast<float>(exact.y), static_cast<float>(exact.z)));
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
        QcCamera camera;
        camera.position = {1e9 + 0.5, 70.0, -1e9 + 0.5};
        const QcFrustum::Planes planes = QcFrustum::planes(camera.projectionMatrix(16.0f / 9.0f, 256.0f) * camera.viewRotation());
        const auto visible = [&](QcBlockPos corner) {
            const QVector3D offset = QcRenderOrigin::entry(QcRenderOrigin::toVec3(corner), camera.position).toVector3D();
            return QcFrustum::intersectsBox(planes, offset, offset + QVector3D(16.0f, 16.0f, 16.0f));
        };
        // Yaw 0 looks north (-z).
        QVERIFY(visible({1'000'000'000, 64, -1'000'000'000 - 64}));
        QVERIFY(!visible({1'000'000'000, 64, -1'000'000'000 + 64}));
        QVERIFY(!visible({1'000'000'000 + 512, 64, -1'000'000'000 - 64}));
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
