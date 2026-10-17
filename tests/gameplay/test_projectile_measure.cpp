#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"

#include <QElapsedTimer>

#include <algorithm>
#include <numbers>

using namespace Qt::StringLiterals;

// Headless measurement of projectiles (stage 52.1, decision E-425), ticked by hand as fast as the
// server goes: arrows shot in a fan from high above the stage, flying among pigs, then the same
// number stuck in a wall. Prints the time of the projectile phase and of the whole tick per tick.
// Small in every test run (32 arrows, checks that all fly and later stick);
// QTCRAFT_PROJECTILE_MEASURE=full measures 500 arrows among 100 pigs over 60 ticks each (Release).
class TestProjectileMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    struct Size
    {
        int arrows = 32;
        int pigs = 8;
        int ticks = 20;
    };

    struct Times
    {
        double phaseMeanMs = 0.0;
        double phaseMaxMs = 0.0;
        double tickMeanMs = 0.0;
    };

    static bool full() { return qEnvironmentVariable("QTCRAFT_PROJECTILE_MEASURE") == u"full"_s; }
    static Size size() { return full() ? Size{500, 100, 60} : Size{}; }

    Times measure(int ticks)
    {
        Times times;
        for (int i = 0; i < ticks; ++i) {
            QElapsedTimer timer;
            timer.start();
            m_server->tick();
            const double tickMs = double(timer.nsecsElapsed()) / 1e6;
            const double phaseMs = double(m_server->tickPhases().lastTick()[std::size_t(QcTickPhase::Projectiles)]) / 1e6;
            times.phaseMeanMs += phaseMs / ticks;
            times.phaseMaxMs = std::max(times.phaseMaxMs, phaseMs);
            times.tickMeanMs += tickMs / ticks;
        }
        return times;
    }

    static void report(const char *scenario, int arrows, const Times &times)
    {
        qInfo("MEASURE projectiles %s arrows=%d phase_mean_ms=%.4f phase_max_ms=%.4f tick_mean_ms=%.4f", scenario, arrows, times.phaseMeanMs,
              times.phaseMaxMs, times.tickMeanMs);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void arrowsInFlightAndStuck()
    {
        const Size measured = size();
        QcServerConfig config;
        config.limits.projectiles = 4096;
        config.limits.projectilesPerChunk = 4096;
        join(config);

        // Pigs on the stage among which the arrows fall.
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        for (int i = 0; i < measured.pigs; ++i) {
            const QcVec3 at{m_feet.x - 3.0 + (i % 7), double(m_feet.y), m_feet.z - 3.0 + (i / 7) % 7};
            m_server->entities().creatures().spawn(pig, at, m_server->entities().newId());
        }

        // A fan of arrows steeply upwards from 40 m above the stage: they fly for seconds, all within
        // loaded chunks (none waits at the edge of the loaded world).
        loadAround(m_feet, 5);
        const QcVec3 from{m_feet.x + 0.5, m_feet.y + 40.0, m_feet.z + 0.5};
        for (int i = 0; i < measured.arrows; ++i) {
            const double yaw = 360.0 * i / measured.arrows;
            const double pitch = 55.0 + 30.0 * (i % 5) / 4.0;
            QVERIFY(m_server->projectiles().launch({0, u"qtcraft:arrow"_s, from, QcProjectileFlight::direction(yaw, pitch) * 30.0}));
        }
        const Times flying = measure(measured.ticks);
        QCOMPARE(m_server->entities().projectiles().all().size(), measured.arrows);
        QVERIFY(std::ranges::none_of(m_server->entities().projectiles().all(), &QcProjectile::stuck));
        report("flying", measured.arrows, flying);

        // The same number stuck: a stone ceiling above the stage catches arrows shot upwards.
        m_server->entities().projectiles().clear();
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, 6, dz), id("qtcraft:stone"));
            }
        }
        for (int i = 0; i < measured.arrows; ++i) {
            const QcVec3 below{m_feet.x - 2.5 + (i % 6), m_feet.y + 4.0, m_feet.z - 2.5 + (i / 6) % 6};
            QVERIFY(m_server->projectiles().launch({0, u"qtcraft:arrow"_s, below, {0.0, 30.0, 0.0}}));
        }
        ticks(5);
        QVERIFY(std::ranges::all_of(m_server->entities().projectiles().all(), &QcProjectile::stuck));
        report("stuck", measured.arrows, measure(measured.ticks));
    }

    // Skeletons in a ring 7–11 m around a player at night on a flat arena, all seeing him (decision
    // E-426): 8 in every test run, QTCRAFT_PROJECTILE_MEASURE=full 50 over 200 ticks. The player
    // is healed each tick so the shooting goes on.
    void skeletonsShootAtAPlayer()
    {
        const int skeletons = full() ? 50 : 8;
        const int count = full() ? 200 : 60;
        join();
        m_server->setWorldTime(QcDayCycle::dayLength * 10.75);
        loadAround(m_feet, 2);
        for (int dx = -14; dx <= 14; ++dx) {
            for (int dz = -14; dz <= 14; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 6; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        const int skeleton = *m_content->mobIndex(u"qtcraft:skeleton"_s);
        for (int i = 0; i < skeletons; ++i) {
            const double angle = 2.0 * std::numbers::pi * i / skeletons;
            const double radius = 7.0 + 4.0 * (i % 3) / 2.0;
            const QcVec3 at{m_feet.x + 0.5 + radius * std::cos(angle), double(m_feet.y), m_feet.z + 0.5 + radius * std::sin(angle)};
            m_server->entities().creatures().spawn(skeleton, at, m_server->entities().newId());
        }

        QcServerClient &player = *m_server->entities().clients().front();
        double tickMs = 0.0;
        double mobsMs = 0.0;
        double projectilesMs = 0.0;
        double maxTickMs = 0.0;
        QSet<quint32> shots;
        for (int i = 0; i < count; ++i) {
            player.health = QcServerClient::maxHealth;
            QElapsedTimer timer;
            timer.start();
            m_server->tick();
            const double ms = double(timer.nsecsElapsed()) / 1e6;
            tickMs += ms / count;
            maxTickMs = std::max(maxTickMs, ms);
            mobsMs += double(m_server->tickPhases().lastTick()[std::size_t(QcTickPhase::Mobs)]) / 1e6 / count;
            projectilesMs += double(m_server->tickPhases().lastTick()[std::size_t(QcTickPhase::Projectiles)]) / 1e6 / count;
            for (const QcProjectile &projectile : m_server->entities().projectiles().all()) {
                shots.insert(projectile.id);
            }
        }
        QVERIFY(!player.dead);
        // Every skeleton shoots about once in two seconds once it has seen the player.
        QVERIFY2(shots.size() >= skeletons, qPrintable(QString::number(shots.size())));
        qInfo("MEASURE skeletons=%d ticks=%d shots=%lld tick_mean_ms=%.4f tick_max_ms=%.4f mobs_mean_ms=%.4f projectiles_mean_ms=%.4f",
              skeletons, count, qint64(shots.size()), tickMs, maxTickMs, mobsMs, projectilesMs);
    }
};

QTEST_GUILESS_MAIN(TestProjectileMeasure)
#include "test_projectile_measure.moc"
