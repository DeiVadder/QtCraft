#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QTest>

#include <cmath>

// Monsters that spawn by themselves around one player in a generated world (decision E-836), at
// midnight and at noon: how many there are after each half minute, how many stand under the open
// sky, and what the creature tick costs. The ticks run as fast as they can (no real time). The
// default run is short; QTCRAFT_SPAWN_MEASURE=1 (Release build) runs three minutes of game time
// each, the recorded measurement. Uses only what the creature manager offered before E-836, so the
// same file measures the state before it.
class TestSpawnMeasure : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    quint32 m_nextId = 100;

    struct Count
    {
        int hostile = 0;
        int openSky = 0;
        int others = 0;
    };

    Count count(const QcServerWorld &world, const QcMobManager &mobs) const
    {
        Count result;
        for (const QcCreature &mob : mobs.mobs()) {
            const QcMobDefinition &definition = m_content->mobs()[mob.type];
            if (!definition.hostile) {
                result.others += 1;
                continue;
            }
            result.hostile += 1;
            const QcBlockPos feet{static_cast<int>(std::floor(mob.motion.position.x)), static_cast<int>(std::floor(mob.motion.position.y)),
                static_cast<int>(std::floor(mob.motion.position.z))};
            result.openSky += world.skyLight(feet) >= 15;
        }
        return result;
    }

    void measure(const char *name, double time, int seconds)
    {
        QcServerWorld world(m_content, std::make_unique<QcTerrainGenerator>(m_content, 42));
        for (int x = -8; x <= 8; ++x) {
            for (int z = -8; z <= 8; ++z) {
                world.loadChunk({x, z});
            }
        }
        const QcVec3 player{0.5, static_cast<double>(world.chunk({0, 0})->highestNonAir(0, 0) + 1), 0.5};
        QcMobManager mobs(m_content, 42);
        const QList<QcMobManager::PlayerView> players{{7, player, true}};

        QElapsedTimer timer;
        qint64 nanoseconds = 0;
        for (int tick = 1; tick <= seconds * 20; ++tick) {
            timer.start();
            mobs.tick(world, players, time, 0.05, [this] { return m_nextId++; });
            nanoseconds += timer.nsecsElapsed();
            if (tick % (20 * 30) == 0 || tick == seconds * 20) {
                const Count now = count(world, mobs);
                qInfo("MEASURE spawn %s %3d s: hostile %3d (open sky %3d), other creatures %3d, creature tick %.3f ms", name, tick / 20,
                      now.hostile, now.openSky, now.others, static_cast<double>(nanoseconds) / 1e6 / tick);
            }
        }
        QVERIFY(mobs.mobs().size() < 1000);
    }

private slots:
    void monstersAtNightAndByDay()
    {
        const int seconds = qEnvironmentVariableIntValue("QTCRAFT_SPAWN_MEASURE") == 1 ? 180 : 10;
        measure("midnight", QcDayCycle::dayLength * 0.75, seconds);
        measure("noon", QcDayCycle::dayLength * 0.25, seconds);
    }
};

QTEST_GUILESS_MAIN(TestSpawnMeasure)
#include "test_spawn_measure.moc"
