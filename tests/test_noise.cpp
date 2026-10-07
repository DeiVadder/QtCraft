#include "QcNoise.h"

#include <QTest>

#include <cmath>

class TestNoise : public QObject
{
    Q_OBJECT

private slots:
    void sameSeedSameValues()
    {
        const QcNoise a(1234);
        const QcNoise b(1234);
        for (int i = 0; i < 100; ++i) {
            const double x = i * 0.37 - 11.0;
            const double z = i * 0.91 + 3.0;
            QCOMPARE(a.noise2(x, z), b.noise2(x, z));
            QCOMPARE(a.noise3(x, i * 0.13, z), b.noise3(x, i * 0.13, z));
        }
    }

    void differentSeedsDiffer()
    {
        const QcNoise a(1);
        const QcNoise b(2);
        int differences = 0;
        for (int i = 0; i < 50; ++i) {
            differences += a.noise2(i * 0.7 + 0.3, i * 0.4 + 0.2) != b.noise2(i * 0.7 + 0.3, i * 0.4 + 0.2);
        }
        QVERIFY(differences > 45);
    }

    void zeroAtLatticePoints()
    {
        // Gradient noise vanishes at integer coordinates by construction.
        const QcNoise noise(99);
        QCOMPARE(noise.noise2(3.0, -7.0), 0.0);
        QCOMPARE(noise.noise3(-2.0, 5.0, 11.0), 0.0);
    }

    void staysInRangeAndUsesIt()
    {
        const QcNoise noise(7);
        double low = 0.0;
        double high = 0.0;
        for (int i = 0; i < 20000; ++i) {
            const double v = noise.noise3(i * 0.173, i * 0.071, i * 0.0291);
            QVERIFY(v >= -1.0001 && v <= 1.0001);
            low = std::min(low, v);
            high = std::max(high, v);
        }
        QVERIFY(low < -0.5);
        QVERIFY(high > 0.5);
    }

    void isContinuous()
    {
        const QcNoise noise(5);
        for (int i = 0; i < 1000; ++i) {
            const double x = i * 0.0517;
            QVERIFY(std::abs(noise.noise2(x, 1.3) - noise.noise2(x + 1e-4, 1.3)) < 1e-3);
        }
    }

    void positionRandomIsUniformAndDeterministic()
    {
        int buckets[10] = {};
        for (int x = 0; x < 100; ++x) {
            for (int z = 0; z < 100; ++z) {
                const double r = qcPositionRandom(42, x, 0, z);
                QVERIFY(r >= 0.0 && r < 1.0);
                QCOMPARE(r, qcPositionRandom(42, x, 0, z));
                ++buckets[int(r * 10)];
            }
        }
        // 10000 samples: every decile within 15 % of the expected 1000.
        for (const int count : buckets) {
            QVERIFY2(count > 850 && count < 1150, qPrintable(QString::number(count)));
        }
    }
};

QTEST_GUILESS_MAIN(TestNoise)
#include "test_noise.moc"
