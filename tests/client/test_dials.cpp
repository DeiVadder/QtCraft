#include "clientcore/player/QcDials.h"
#include "common/world/QcDayCycle.h"

#include <QTest>

// The dials of held items (decision E-769): the compass needle towards the world spawn relative to
// the view, the clock hand with noon at the top, and the time it shows.
class TestDials : public QObject
{
    Q_OBJECT

private slots:
    void compassPointsToTheSpawnRelativeToTheView()
    {
        // The spawn column (10, -20) has its middle at (10.5, -19.5).
        const QcVec3 south{10.5, 64.0, 0.5};
        // North of the player: straight ahead when looking north (yaw 0), to the left when looking
        // east, behind when looking south.
        QCOMPARE(QcDials::compassAngle(south, 0.0, 10, -20), 0.0);
        QCOMPARE(QcDials::compassAngle(south, 90.0, 10, -20), -90.0);
        QCOMPARE(QcDials::compassAngle(south, 180.0, 10, -20), -180.0);
        QCOMPARE(QcDials::compassAngle(south, -90.0, 10, -20), 90.0);
        // East of the player while looking north: to the right.
        QCOMPARE(QcDials::compassAngle({0.5, 64.0, -19.5}, 0.0, 10, -20), 90.0);
        // Yaw beyond a turn counts the same.
        QCOMPARE(QcDials::compassAngle({0.5, 64.0, -19.5}, 720.0, 10, -20), 90.0);
        // On the spawn column: no direction.
        QCOMPARE(QcDials::compassAngle({10.6, 64.0, -19.4}, 37.0, 10, -20), 0.0);
    }

    void clockShowsNoonAtTheTop()
    {
        constexpr double day = QcDayCycle::dayLength;
        QCOMPARE(QcDials::clockAngle(day * 0.25), 0.0);
        QCOMPARE(QcDials::clockAngle(day * 0.75), 180.0);
        QCOMPARE(QcDials::clockAngle(0.0), 270.0);
        QCOMPARE(QcDials::clockAngle(day * 0.5), 90.0);
        QCOMPARE(QcDials::clockAngle(day * 3.25), 0.0);

        QCOMPARE(QcDials::clockTime(0.0).hours, 6);
        QCOMPARE(QcDials::clockTime(day * 0.25).hours, 12);
        QCOMPARE(QcDials::clockTime(day * 0.75).hours, 0);
        const QcDials::ClockTime late = QcDials::clockTime(day * (17.5 / 24.0) + 0.05);
        QCOMPARE(late.hours, 23);
        QCOMPARE(late.minutes, 30);
    }
};

QTEST_GUILESS_MAIN(TestDials)
#include "test_dials.moc"
