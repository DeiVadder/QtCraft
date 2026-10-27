#include "clientcore/light/QcWaterLook.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "QcTestWorld.h"

#include <QTest>

#include <cmath>
#include <limits>

// The pure parts of the realistic water (decision E-906): whether the eye is under water, the
// waves' time and the camera modulo the waves' period (exact far out, NaN-safe), the colour under
// water by daylight and the packing of the Water block (light, conventions, quality, NaN guards).
class TestWaterLook : public QObject
{
    Q_OBJECT

    const QcContentRegistry m_content = qcMakeTestContent();

    static QcWaterFrame frame()
    {
        QcWaterFrame result;
        result.viewProjection.perspective(70.0f, 1.6f, 0.05f, 300.0f);
        result.light.toLight = QVector3D(0.0f, 2.0f, 0.0f);
        result.light.strength = 0.75f;
        result.light.litTint = QVector3D(1.0f, 0.9f, 0.8f);
        result.camera = QcVec3{100.25, 70.5, -3.75};
        result.seconds = 12.5f;
        result.size = QSize(200, 100);
        result.water = QcRenderOptions{.mode = QcRenderOptions::Mode::Realistic, .quality = QcRenderOptions::Quality::High}.water();
        return result;
    }

private slots:
    void theEyeIsUnderWaterBelowTheSurface()
    {
        QcTestWorld world(m_content);
        const QcBlockId water = *m_content.blockId(QStringLiteral("test:water"));
        world.set({0, 60, 0}, water);
        const float surface = QcBlockRenderTable::liquidHeight(m_content, water);
        QVERIFY(surface > 0.5f && surface < 1.0f);
        QVERIFY(QcWaterLook::eyeUnderWater(world, {0.5, 60.0 + surface - 0.05, 0.5}));
        QVERIFY(!QcWaterLook::eyeUnderWater(world, {0.5, 60.0 + surface + 0.05, 0.5}));
        QVERIFY(!QcWaterLook::eyeUnderWater(world, {1.5, 60.5, 0.5}));
        // With water on top the whole cell is under water.
        world.set({0, 61, 0}, water);
        QVERIFY(QcWaterLook::eyeUnderWater(world, {0.5, 60.95, 0.5}));
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!QcWaterLook::eyeUnderWater(world, {0.5, nan, 0.5}));
        // Air and solid blocks have no surface of their own.
        QCOMPARE(QcBlockRenderTable::liquidHeight(m_content, qcAirBlockId), 1.0f);
    }

    void timeAndCameraWrapWithoutLosingPrecision()
    {
        QCOMPARE(QcWaterLook::waveSeconds(0.0), 0.0f);
        QCOMPARE(QcWaterLook::waveSeconds(200.0), 10.0f);
        const double loopTicks = QcWaterLook::waveLoopSeconds * 20.0;
        QCOMPARE(QcWaterLook::waveSeconds(3.0 * loopTicks + 40.0), 2.0f);
        QCOMPARE(QcWaterLook::waveSeconds(std::numeric_limits<double>::infinity()), 0.0f);

        // Far out, where a float cannot hold the camera's fraction, the modulo keeps it.
        const double far = 3.0e8;
        const std::array<float, 3> camera = QcWaterLook::periodicCamera({far + 0.375, -12.5, -far - 0.25});
        QCOMPARE(camera[0], static_cast<float>(std::fmod(far, QcWaterLook::wavePeriod) + 0.375));
        QCOMPARE(camera[1], static_cast<float>(QcWaterLook::wavePeriod - 12.5));
        QCOMPARE(camera[2], static_cast<float>(QcWaterLook::wavePeriod - std::fmod(far, QcWaterLook::wavePeriod) - 0.25));
        for (const float value : camera) {
            QVERIFY(value >= 0.0f && value <= QcWaterLook::wavePeriod);
        }
        const std::array<float, 3> broken = QcWaterLook::periodicCamera({std::numeric_limits<double>::quiet_NaN(), 1.0, 2.0});
        QCOMPARE(broken[0], 0.0f);
        QCOMPARE(broken[1], 1.0f);
    }

    void theWaterIsDarkerAtNight()
    {
        const QVector3D day = QcWaterLook::underwaterColour(1.0);
        const QVector3D night = QcWaterLook::underwaterColour(0.0);
        QCOMPARE(day, QVector3D(QcWaterLook::scatter[0], QcWaterLook::scatter[1], QcWaterLook::scatter[2]));
        QVERIFY(night.z() > 0.0f && night.z() < day.z());
        QCOMPARE(QcWaterLook::underwaterColour(std::numeric_limits<double>::quiet_NaN()), night);
        // Blue keeps most, red least.
        QVERIFY(day.z() > day.y() && day.y() > day.x());
        QVERIFY(QcWaterLook::extinction[0] > QcWaterLook::extinction[1] && QcWaterLook::extinction[1] > QcWaterLook::extinction[2]);
    }

    void uniformsCarryTheFrame()
    {
        QcWaterFrame input = frame();
        QcWaterUniforms uniforms = QcWaterLook::uniforms(input);
        QCOMPARE(uniforms.zenith[3], 12.5f);
        QCOMPARE(uniforms.horizon[3], 1.0f);
        QCOMPARE(uniforms.glow[3], -1.0f);
        QCOMPARE(uniforms.toLight[1], 1.0f);
        QCOMPARE(uniforms.toLight[3], 0.75f);
        QCOMPARE(uniforms.camera[0], 36.25f);
        QCOMPARE(uniforms.camera[2], 60.25f);
        QCOMPARE(uniforms.pixel[0], 1.0f / 200.0f);
        QCOMPARE(uniforms.pixel[1], 1.0f / 100.0f);
        QCOMPARE(uniforms.pixel[2], static_cast<float>(input.water.waveOctaves));
        QCOMPARE(uniforms.effects[0], static_cast<float>(input.water.reflectionSteps));
        QCOMPARE(uniforms.extinction[0], QcWaterLook::extinction[0]);
        QCOMPARE(uniforms.deep[2], QcWaterLook::underwaterColour(input.sky.daylight).z());
        const QMatrix4x4 inverse = input.viewProjection.inverted();
        QCOMPARE(uniforms.inverseViewProjection[5], inverse.constData()[5]);

        input.depthZeroToOne = false;
        input.textureYFollowsNdc = true;
        uniforms = QcWaterLook::uniforms(input);
        QCOMPARE(uniforms.horizon[3], 0.0f);
        QCOMPARE(uniforms.glow[3], 1.0f);
    }

    void brokenValuesGiveNoHighlight()
    {
        QcWaterFrame input = frame();
        input.light.toLight = QVector3D(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f);
        QcWaterUniforms uniforms = QcWaterLook::uniforms(input);
        QCOMPARE(uniforms.toLight[3], 0.0f);
        QCOMPARE(uniforms.toLight[1], 1.0f);

        input = frame();
        input.light.strength = std::numeric_limits<float>::infinity();
        QCOMPARE(QcWaterLook::uniforms(input).toLight[3], 0.0f);
        input.light.strength = 4.0f;
        QCOMPARE(QcWaterLook::uniforms(input).toLight[3], 1.0f);

        input = frame();
        input.size = QSize();
        input.sky.zenith = QVector3D(std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f);
        uniforms = QcWaterLook::uniforms(input);
        QCOMPARE(uniforms.pixel[0], 0.0f);
        QCOMPARE(uniforms.zenith[0], 0.0f);
        QCOMPARE(uniforms.zenith[1], 0.5f);

        // The classic mode has no water of its own.
        input.water = {};
        uniforms = QcWaterLook::uniforms(input);
        QCOMPARE(uniforms.pixel[2], 0.0f);
        QCOMPARE(uniforms.effects[1], 0.0f);
    }
};

QTEST_GUILESS_MAIN(TestWaterLook)
#include "test_water_look.moc"
