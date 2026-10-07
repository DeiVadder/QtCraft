#include "QcCamera.h"

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

    void viewMatrixMapsForwardPointOntoNegativeViewZ()
    {
        QcCamera camera;
        camera.position = QVector3D(10, 70, -4);
        camera.yaw = 90.0f;
        // A point 5 m east of an east-facing camera lies straight ahead: view space (0, 0, -5).
        const QVector3D inView = camera.viewMatrix().map(QVector3D(15, 70, -4));
        QVERIFY(fuzzyEqual(inView, QVector3D(0, 0, -5)));
        // A point 1 m above the camera is up in view space.
        QVERIFY(fuzzyEqual(camera.viewMatrix().map(QVector3D(10, 71, -4)), QVector3D(0, 1, 0)));
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
};

QTEST_GUILESS_MAIN(TestCamera)
#include "test_camera.moc"
