#include "clientcore/camera/QcCamera.h"
#include "clientcore/camera/QcMouseLook.h"

#include <QTest>

namespace {

bool fuzzyEqual(QVector3D a, QVector3D b)
{
    return (a - b).length() < 1e-5f;
}

} // namespace

class TestCamera : public QObject
{
    Q_OBJECT

private slots:
    void forwardFollowsYawAndPitchConvention_data()
    {
        QTest::addColumn<float>("yaw");
        QTest::addColumn<float>("pitch");
        QTest::addColumn<QVector3D>("expected");
        QTest::newRow("yaw 0 looks north (-Z)") << 0.0f << 0.0f << QVector3D(0, 0, -1);
        QTest::newRow("yaw 90 looks east (+X)") << 90.0f << 0.0f << QVector3D(1, 0, 0);
        QTest::newRow("yaw 180 looks south (+Z)") << 180.0f << 0.0f << QVector3D(0, 0, 1);
        QTest::newRow("yaw -90 looks west (-X)") << -90.0f << 0.0f << QVector3D(-1, 0, 0);
        QTest::newRow("pitch 90 looks up") << 0.0f << 90.0f << QVector3D(0, 1, 0);
        QTest::newRow("pitch -45 at yaw 90") << 90.0f << -45.0f << QVector3D(0.70710678f, -0.70710678f, 0);
    }

    void forwardFollowsYawAndPitchConvention()
    {
        QFETCH(float, yaw);
        QFETCH(float, pitch);
        QFETCH(QVector3D, expected);
        QcCamera camera;
        camera.yaw = yaw;
        camera.pitch = pitch;
        QVERIFY2(fuzzyEqual(camera.forward(), expected), qPrintable(QString::number(camera.forward().x()) + u' '
                                                                    + QString::number(camera.forward().y()) + u' '
                                                                    + QString::number(camera.forward().z())));
    }

    void viewRotationMapsForwardPointOntoNegativeViewZ()
    {
        QcCamera camera;
        camera.position = {10.0, 70.0, -4.0};
        camera.yaw = 90.0f;
        // A point 5 m east of an east-facing camera lies straight ahead: view space (0, 0, -5).
        const QVector3D inView = camera.viewRotation().map(camera.relative({15.0, 70.0, -4.0}));
        QVERIFY(fuzzyEqual(inView, QVector3D(0, 0, -5)));
        // A point 1 m above the camera is up in view space.
        QVERIFY(fuzzyEqual(camera.viewRotation().map(camera.relative({10.0, 71.0, -4.0})), QVector3D(0, 1, 0)));
    }

    void relativePositionsStayExactFarFromTheOrigin()
    {
        // Subtracted in double (decision E-883): a float holds 1e9 only to 64 m.
        QcCamera camera;
        camera.position = {1e9 + 0.25, 70.5, -1e9 - 0.75};
        QVERIFY(fuzzyEqual(camera.relative({1e9 + 3.0, 71.0, -1e9 - 2.0}), QVector3D(2.75f, 0.5f, -1.25f)));
    }

    void projectionPlacesNearAndFarPlanesAtClipDepthLimits()
    {
        QcCamera camera;
        const QMatrix4x4 projection = camera.projectionMatrix(16.0f / 9.0f, 100.0f);
        const QVector3D nearPoint = projection.map(QVector3D(0, 0, -QcCamera::nearPlane));
        const QVector3D farPoint = projection.map(QVector3D(0, 0, -100.0f));
        // OpenGL-style projection: near maps to -1, far to +1 (QRhi corrects per backend).
        QVERIFY(qAbs(nearPoint.z() + 1.0f) < 1e-4f);
        QVERIFY(qAbs(farPoint.z() - 1.0f) < 1e-4f);
    }

    void mouseMotionTurnsTheView()
    {
        QcPlayerInput start;
        start.yaw = 350.0f;
        start.pitch = 10.0f;

        // 40 points right and 20 points down at 0.25 degrees per point: +10 yaw (wrapping), -5 pitch.
        const QcPlayerInput turned = QcMouseLook::apply(start, {40.0, 20.0}, 0.25, false);
        QCOMPARE(turned.yaw, 0.0f);
        QCOMPARE(turned.pitch, 5.0f);
        QCOMPARE(QcMouseLook::apply(start, {0.0, 20.0}, 0.25, true).pitch, 15.0f);

        // Left past zero wraps to the top of the range; pitch stops short of straight up or down.
        QCOMPARE(QcMouseLook::apply(start, {-1440.0, 0.0}, 0.25, false).yaw, 350.0f);
        QCOMPARE(QcMouseLook::apply(start, {-200.0, 0.0}, 0.25, false).yaw, 300.0f);
        QCOMPARE(QcMouseLook::apply(start, {0.0, -1000.0}, 0.25, false).pitch, 89.9f);
        QCOMPARE(QcMouseLook::apply(start, {0.0, 1000.0}, 0.25, false).pitch, -89.9f);

        // A turn of a tiny fraction of a degree left from 0 would round to exactly 360; it stays in [0, 360).
        QcPlayerInput north;
        const float nudged = QcMouseLook::apply(north, {-1e-7, 0.0}, 1.0, false).yaw;
        QVERIFY(nudged >= 0.0f && nudged < 360.0f);

        // Broken input leaves the view alone.
        QCOMPARE(QcMouseLook::apply(start, {qQNaN(), 1.0}, 0.25, false).yaw, 350.0f);
    }
};

QTEST_GUILESS_MAIN(TestCamera)
#include "test_camera.moc"
