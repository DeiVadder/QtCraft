// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcShadowCascades.h"
#include "clientcore/light/QcSky.h"

#include <QTest>
#include <QVector4D>

#include <cmath>
#include <limits>

// The pure parts of the sun shadows (decision E-903): the radii of the cascades, the light's axes,
// the texel snapping that keeps a point of the world on the same texel grid while the camera
// moves, the matrix into the map's texture coordinates, and which light casts with what strength.
class TestShadowCascades : public QObject
{
    Q_OBJECT

    static QcShadowCascades::Basis noonBasis()
    {
        return QcShadowCascades::basis(QVector3D(0.3f, 0.9f, 0.1f)).value();
    }

    // Where a point of the world lands in the light's clip space of a cascade fitted at `camera`.
    static QVector3D clipOf(const QcShadowCascade &cascade, const QcVec3 &point, const QcVec3 &camera)
    {
        const QcVec3 relative = point - camera;
        const QVector4D clip =
            cascade.lightViewProjection.map(QVector4D(static_cast<float>(relative.x), static_cast<float>(relative.y), static_cast<float>(relative.z), 1.0f));
        return clip.toVector3D() / clip.w();
    }

private slots:
    void radiiGrowToTheDistance()
    {
        QCOMPARE(QcShadowCascades::radii(1, 40.0f), (QcShadowCascades::Radii{40.0f, 0.0f, 0.0f}));
        const QcShadowCascades::Radii three = QcShadowCascades::radii(3, 128.0f);
        QCOMPARE(three[2], 128.0f);
        QVERIFY(qFuzzyCompare(three[1], 128.0f * QcShadowCascades::splitRatio));
        QVERIFY(three[0] > 0.0f && three[0] < three[1]);
        QCOMPARE(QcShadowCascades::radii(0, 40.0f), QcShadowCascades::Radii{});
        QCOMPARE(QcShadowCascades::radii(4, 40.0f), QcShadowCascades::Radii{});
        QCOMPARE(QcShadowCascades::radii(2, -1.0f), QcShadowCascades::Radii{});
        QCOMPARE(QcShadowCascades::radii(2, std::numeric_limits<float>::quiet_NaN()), QcShadowCascades::Radii{});
    }

    void basisIsOrthonormalAndTurnsAboutZ()
    {
        const QcShadowCascades::Basis axes = noonBasis();
        QVERIFY(qFuzzyCompare(axes.toLight.length(), 1.0f));
        QVERIFY(qFuzzyCompare(axes.right.length(), 1.0f));
        QVERIFY(qFuzzyCompare(axes.up.length(), 1.0f));
        QVERIFY(std::abs(QVector3D::dotProduct(axes.right, axes.up)) < 1e-6f);
        QVERIFY(std::abs(QVector3D::dotProduct(axes.right, axes.toLight)) < 1e-6f);
        QVERIFY(std::abs(QVector3D::dotProduct(axes.up, axes.toLight)) < 1e-6f);
        // `right` stays in the plane the sun crosses, whatever its height.
        QVERIFY(std::abs(axes.right.z()) < 1e-6f);

        QVERIFY(!QcShadowCascades::basis(QVector3D()));
        QVERIFY(!QcShadowCascades::basis(QVector3D(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f)));
        QVERIFY(!QcShadowCascades::basis(QVector3D(0.0f, 0.0f, 1.0f)));
    }

    void snappingRoundsToTheStep()
    {
        QCOMPARE(QcShadowCascades::snapped(1.24, 0.5), 1.0);
        QCOMPARE(QcShadowCascades::snapped(1.26, 0.5), 1.5);
        QCOMPARE(QcShadowCascades::snapped(-1.26, 0.5), -1.5);
        QCOMPARE(QcShadowCascades::snapped(1e9 + 0.3, 0.25), 1e9 + 0.25);
    }

    // The light's view of a point near the camera: inside the box, the light's side in front.
    void cascadeHoldsTheCameraAndLooksFromTheLight()
    {
        const QcShadowCascades::Basis axes = noonBasis();
        const QcVec3 camera{100.5, 70.0, -20.25};
        const QcShadowCascade cascade = QcShadowCascades::fit(axes, camera, QVector3D(0.0f, 0.0f, -1.0f), 32.0f, 1024);
        QCOMPARE(cascade.radius, 32.0f);
        QVERIFY(qFuzzyCompare(cascade.texelSize, 64.0f / 1024.0f));

        const QVector3D atCamera = clipOf(cascade, camera, camera);
        QVERIFY(std::abs(atCamera.x()) < 1.0f && std::abs(atCamera.y()) < 1.0f && std::abs(atCamera.z()) < 1.0f);
        // A point towards the light lies nearer to it (smaller depth) than the camera.
        const QcVec3 above = camera + QcVec3{axes.toLight.x() * 10.0, axes.toLight.y() * 10.0, axes.toLight.z() * 10.0};
        QVERIFY(clipOf(cascade, above, camera).z() < atCamera.z());
        // The box reaches casterReach beyond its radius towards the light.
        const double far = 32.0 + QcShadowCascades::casterReach - 4.0;
        const QcVec3 high = camera + QcVec3{axes.toLight.x() * far, axes.toLight.y() * far, axes.toLight.z() * far};
        QVERIFY(clipOf(cascade, high, camera).z() > -1.0f);
        // The box lies ahead of the camera: farther ahead than behind.
        const QcVec3 ahead = camera + QcVec3{0.0, 0.0, -38.0};
        const QcVec3 behind = camera + QcVec3{0.0, 0.0, 38.0};
        const float aheadSide = std::max(std::abs(clipOf(cascade, ahead, camera).x()), std::abs(clipOf(cascade, ahead, camera).y()));
        const float behindSide = std::max(std::abs(clipOf(cascade, behind, camera).x()), std::abs(clipOf(cascade, behind, camera).y()));
        QVERIFY(aheadSide < behindSide);
    }

    // Texel snapping: as the camera moves by less than a texel (or by many, far from the origin),
    // a point of the world moves on the map by whole texels only, so shadow edges do not crawl.
    void pointsStayOnTheTexelGrid_data()
    {
        QTest::addColumn<double>("x");
        QTest::addColumn<double>("z");
        QTest::addRow("near the origin") << 10.0 << -5.0;
        QTest::addRow("far out") << 3.0e8 << -2.0e8;
    }

    void pointsStayOnTheTexelGrid()
    {
        QFETCH(double, x);
        QFETCH(double, z);
        const QcShadowCascades::Basis axes = noonBasis();
        constexpr int resolution = 1024;
        const QcVec3 first{x, 64.0, z};
        const QcVec3 point = first + QcVec3{3.0, -2.0, 4.0};
        const QVector3D forward(0.6f, -0.2f, -0.77f);
        const QcShadowCascade a = QcShadowCascades::fit(axes, first, forward, 24.0f, resolution);
        const QVector3D clipA = clipOf(a, point, first);
        for (const QcVec3 step : {QcVec3{0.013, 0.0, 0.0}, QcVec3{0.0, 0.021, -0.007}, QcVec3{1.37, -0.4, 2.9}}) {
            const QcVec3 second = first + step;
            const QcShadowCascade b = QcShadowCascades::fit(axes, second, forward, 24.0f, resolution);
            const QVector3D clipB = clipOf(b, point, second);
            // Clip x and y span two units over the resolution: half a texel is 1 / resolution.
            const double texelsX = (clipB.x() - clipA.x()) * resolution / 2.0;
            const double texelsY = (clipB.y() - clipA.y()) * resolution / 2.0;
            QVERIFY2(std::abs(texelsX - std::round(texelsX)) < 0.02, qPrintable(QString::number(texelsX)));
            QVERIFY2(std::abs(texelsY - std::round(texelsY)) < 0.02, qPrintable(QString::number(texelsY)));
        }
    }

    void textureMatrixMapsClipToTheTile()
    {
        // Clip x, y in -1..1 become 0..1; y mirrored where the texture runs against clip y; z 0..1.
        const QMatrix4x4 straight = QcShadowCascades::textureMatrix(false, true);
        QCOMPARE(straight.map(QVector3D(-1.0f, -1.0f, 0.25f)), QVector3D(0.0f, 0.0f, 0.25f));
        QCOMPARE(straight.map(QVector3D(1.0f, 1.0f, 1.0f)), QVector3D(1.0f, 1.0f, 1.0f));
        const QMatrix4x4 mirrored = QcShadowCascades::textureMatrix(true, true);
        QCOMPARE(mirrored.map(QVector3D(1.0f, 1.0f, 0.0f)), QVector3D(1.0f, 0.0f, 0.0f));
        const QMatrix4x4 openGl = QcShadowCascades::textureMatrix(false, false);
        QCOMPARE(openGl.map(QVector3D(0.0f, 0.0f, -1.0f)), QVector3D(0.5f, 0.5f, 0.0f));
        QCOMPARE(openGl.map(QVector3D(0.0f, 0.0f, 1.0f)), QVector3D(0.5f, 0.5f, 1.0f));
    }

    // The sun casts by day, the moon weaker by night; both fade out near the horizon and under clouds.
    void lightFollowsSunMoonAndClouds()
    {
        QcSkyState sky;
        sky.sunDirection = QVector3D(0.2f, 0.95f, 0.1f).normalized();
        sky.moonDirection = -sky.sunDirection;
        const QcShadowLight noon = QcShadowCascades::light(sky);
        QVERIFY(qFuzzyCompare(noon.toLight, sky.sunDirection));
        QCOMPARE(noon.strength, 1.0f);
        QVERIFY(qFuzzyCompare(noon.litTint, QVector3D(1.0f, 1.0f, 1.0f)));

        sky.sunDirection = QVector3D(0.99f, 0.1f, 0.1f).normalized();
        const QcShadowLight sunset = QcShadowCascades::light(sky);
        QVERIFY(sunset.strength > 0.0f && sunset.strength < 1.0f);
        QVERIFY(sunset.litTint.z() < sunset.litTint.x());

        sky.sunDirection = QVector3D(0.999f, 0.01f, 0.04f).normalized();
        QCOMPARE(QcShadowCascades::light(sky).strength, 0.0f);

        sky.sunDirection = QVector3D(-0.2f, -0.95f, 0.1f).normalized();
        sky.moonDirection = QVector3D(0.2f, 0.95f, -0.1f).normalized();
        const QcShadowLight night = QcShadowCascades::light(sky);
        QVERIFY(qFuzzyCompare(night.toLight, sky.moonDirection));
        QVERIFY(qFuzzyCompare(night.strength, QcShadowCascades::moonShare));

        sky.overcast = 1.0;
        QCOMPARE(QcShadowCascades::light(sky).strength, 0.0f);
        sky.overcast = std::numeric_limits<double>::quiet_NaN();
        QCOMPARE(QcShadowCascades::light(sky).strength, 0.0f);
        sky.overcast = 0.0;
        sky.moonDirection = QVector3D(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f);
        QCOMPARE(QcShadowCascades::light(sky).strength, 0.0f);
    }
};

QTEST_GUILESS_MAIN(TestShadowCascades)
#include "test_shadow_cascades.moc"
