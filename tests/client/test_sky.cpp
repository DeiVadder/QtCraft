#include "clientcore/light/QcSky.h"
#include "common/core/QcWorldConstants.h"
#include "common/world/QcDayCycle.h"

#include <QTest>

#include <cmath>

namespace {

constexpr double day = QcDayCycle::dayLength;
// Phase 0 is sunrise, 0.25 noon, 0.5 sunset, 0.75 midnight.
constexpr double noon = day * 0.25;
constexpr double sunset = day * 0.5;
constexpr double midnight = day * 0.75;

float brightness(QVector3D color)
{
    return color.x() + color.y() + color.z();
}

} // namespace

class TestSky : public QObject
{
    Q_OBJECT

private slots:
    void sunAndMoonFollowTheDay()
    {
        QVERIFY(QcSky::at(noon).sunDirection.y() > 0.9f);
        QVERIFY(QcSky::at(midnight).sunDirection.y() < -0.9f);
        QVERIFY(QcSky::at(midnight).moonDirection.y() > 0.9f);
        // Rises in the east (+x), sets in the west.
        QVERIFY(QcSky::at(day * 0.02).sunDirection.x() > 0.9f);
        QVERIFY(QcSky::at(sunset - 1.0).sunDirection.x() < -0.9f);
        QVERIFY(std::abs(QcSky::at(noon).sunDirection.length() - 1.0f) < 1e-4f);
    }

    void skyFadesToBlackBelowTheWorld()
    {
        // Decision E-288: unchanged down to the bottom of the world, half as bright half-way to
        // the void limit, black from there on; sun and moon keep their places.
        const QcSkyState noonSky = QcSky::at(noon);
        const QcSkyState surface = QcSky::seenFrom(noonSky, 0.0);
        QCOMPARE(surface.horizon, noonSky.horizon);
        QCOMPARE(surface.zenith, noonSky.zenith);

        const QcSkyState halfway = QcSky::seenFrom(noonSky, QcWorld::voidLimitY / 2.0);
        QVERIFY(std::abs(brightness(halfway.fogColor()) - brightness(noonSky.fogColor()) * 0.5f) < 1e-5f);
        QCOMPARE(halfway.sunDirection, noonSky.sunDirection);

        const QcSkyState night = QcSky::seenFrom(QcSky::at(midnight), QcWorld::voidLimitY - 100.0);
        for (const QVector3D colour : {night.zenith, night.horizon, night.glow, night.cloudColor}) {
            QCOMPARE(brightness(colour), 0.0f);
        }
        QCOMPARE(night.stars, 0.0);
    }

    void dayIsBlueAndBrighterThanTheNight()
    {
        const QcSkyState noonSky = QcSky::at(noon);
        const QcSkyState night = QcSky::at(midnight);
        QVERIFY(noonSky.zenith.z() > noonSky.zenith.x());
        // The zenith is a deeper blue than the horizon.
        QVERIFY(brightness(noonSky.zenith) < brightness(noonSky.horizon));
        QVERIFY(brightness(night.horizon) < brightness(noonSky.horizon) * 0.2f);
        QVERIFY(brightness(night.zenith) < brightness(noonSky.zenith) * 0.2f);
        QCOMPARE(noonSky.stars, 0.0);
        QCOMPARE(night.stars, 1.0);
        QCOMPARE(noonSky.daylight, QcDayCycle::daylight(noon));
    }

    void sunsetIsWarmAndFogMatchesTheHorizon()
    {
        const QcSkyState noonSky = QcSky::at(noon);
        const QcSkyState dusk = QcSky::at(sunset);
        QVERIFY(dusk.horizon.x() > dusk.horizon.z());
        QVERIFY(noonSky.horizon.x() < noonSky.horizon.z());
        QVERIFY(dusk.glow.length() > 0.5f);
        QCOMPARE(noonSky.glow.length(), 0.0f);
        QCOMPARE(dusk.fogColor(), dusk.horizon);
        QCOMPARE(noonSky.fogColor(), noonSky.horizon);
    }

    // No jumps over the whole day: half a second never changes a colour or the stars by more than a little.
    void transitionsAreSmooth()
    {
        QcSkyState previous = QcSky::at(0.0);
        for (int halfSeconds = 1; halfSeconds <= int(day) * 2; ++halfSeconds) {
            const double time = halfSeconds * 0.5;
            const QcSkyState sky = QcSky::at(time);
            QVERIFY2((sky.horizon - previous.horizon).length() < 0.02f, qPrintable(QString::number(time)));
            QVERIFY2((sky.zenith - previous.zenith).length() < 0.02f, qPrintable(QString::number(time)));
            QVERIFY(std::abs(sky.stars - previous.stars) < 0.04);
            previous = sky;
        }
    }

    // One phase per day through a lunar month; the first night has a full moon and a night keeps
    // its phase from dusk to dawn.
    void moonGoesThroughItsPhases()
    {
        QCOMPARE(QcSky::at(midnight).moonPhase, 0.5);
        QCOMPARE(QcSky::at(sunset + 1.0).moonPhase, QcSky::at(day - 1.0).moonPhase);
        QList<double> phases;
        for (int night = 0; night < QcSky::lunarDays; ++night) {
            const double phase = QcSky::at(night * day + midnight).moonPhase;
            QVERIFY(phase >= 0.0 && phase < 1.0);
            QVERIFY(!phases.contains(phase));
            phases.append(phase);
        }
        QVERIFY(phases.contains(0.0));
        QCOMPARE(QcSky::at(QcSky::lunarDays * day + midnight).moonPhase, 0.5);
    }

    void cloudsDrift()
    {
        const QcSkyState early = QcSky::at(100.0);
        const QcSkyState later = QcSky::at(110.0);
        QVERIFY(std::abs(later.cloudOffsetX - early.cloudOffsetX - 10.0 * QcSky::cloudSpeed) < 1e-9);
        QVERIFY(later.cloudOffsetZ > early.cloudOffsetZ);
        QVERIFY(brightness(QcSky::at(midnight).cloudColor) < brightness(QcSky::at(noon).cloudColor) * 0.3f);
    }
};

QTEST_GUILESS_MAIN(TestSky)
#include "test_sky.moc"
