#include "QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "QcTerrainGenerator.h"
#include "QcVillageGenerator.h"
#include "QcVillages.h"

#include <QElapsedTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

// Cost of villagers on the server (decision E-411): the villages of seed 42 nearest to the origin,
// all their chunks loaded, a villager in every bed and nothing else alive; a whole day from sunrise
// of their routine (wandering, work, meeting, going home, sleeping), ticked as fast as the server can, then
// the same day on the same chunks with the villagers removed. One village, then ten. A measurement,
// not a check: set QTCRAFT_VILLAGER_MEASURE=1 and use a Release build. Without it the test runs one
// village for a few ticks. A check beside it: the villagers of the swamp village sleep at midnight.
class TestVillagerMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;

    static bool full() { return qEnvironmentVariableIsSet("QTCRAFT_VILLAGER_MEASURE"); }

    struct Ticks
    {
        double mean = 0.0;
        double p99 = 0.0;
        double max = 0.0;
        // Villagers in their beds at midnight.
        qsizetype asleep = 0;
    };

    // The `count` villages nearest to the origin, ring by ring of regions.
    QList<std::shared_ptr<const QcVillagePlan>> villages(int count) const
    {
        const QcTerrainGenerator generator(m_content, seed);
        QList<std::shared_ptr<const QcVillagePlan>> plans;
        for (int ring = 0; ring <= 20 && plans.size() < count; ++ring) {
            for (int rz = -ring; rz <= ring && plans.size() < count; ++rz) {
                for (int rx = -ring; rx <= ring && plans.size() < count; ++rx) {
                    if (std::max(std::abs(rx), std::abs(rz)) != ring) {
                        continue;
                    }
                    if (std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz)) {
                        plans.append(std::move(plan));
                    }
                }
            }
        }
        return plans;
    }

    // Loads every chunk of the villages; the villagers move in as the chunks arrive.
    void loadVillages(const QList<std::shared_ptr<const QcVillagePlan>> &plans)
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QcChunkPos low = QcBlockPos{plan->minX, 0, plan->minZ}.chunk();
            const QcChunkPos high = QcBlockPos{plan->maxX, 0, plan->maxZ}.chunk();
            for (int x = low.x; x <= high.x; ++x) {
                for (int z = low.z; z <= high.z; ++z) {
                    m_server->world().loadChunk({x, z});
                }
            }
        }
        ticks(2);
    }

    // Removes the creatures that `keep` does not hold on to; returns how many are left.
    template<typename Keep>
    qsizetype removeCreatures(Keep keep)
    {
        QList<quint32> gone;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (!keep(mob)) {
                gone.append(mob.id);
            }
        }
        for (const quint32 id : std::as_const(gone)) {
            m_server->mobs().remove(id);
        }
        return m_server->mobs().mobs().size();
    }

    // `count` ticks from sunrise on, as fast as they go.
    Ticks day(int count)
    {
        m_server->setWorldTime(QcDayCycle::dayLength * 10.0);
        QList<double> times;
        times.reserve(count);
        QElapsedTimer timer;
        qsizetype asleep = 0;
        for (int i = 0; i < count; ++i) {
            timer.start();
            m_server->tick();
            times.append(double(timer.nsecsElapsed()) / 1e6);
            pump();
            if (i == count * 3 / 4) {
                asleep = std::ranges::count_if(m_server->mobs().mobs(), [](const QcCreature &mob) { return mob.villager.sleeping; });
            }
        }
        std::ranges::sort(times);
        double sum = 0.0;
        for (const double time : std::as_const(times)) {
            sum += time;
        }
        return {sum / double(times.size()), times[qsizetype(double(times.size() - 1) * 0.99)], times.last(), asleep};
    }

    // A server on seed 42 with the `count` villages nearest to the origin loaded, only their
    // villagers alive.
    QList<std::shared_ptr<const QcVillagePlan>> startWith(int count)
    {
        QcServerConfig config;
        config.seed = seed;
        config.keepChunksLoaded = true;
        join(config, std::make_unique<QcTerrainGenerator>(m_content, seed));
        chat(u"/gamerule weatherCycle false"_s);
        const QList<std::shared_ptr<const QcVillagePlan>> plans = villages(count);
        loadVillages(plans);
        chat(u"/gamerule mobSpawning false"_s);
        removeCreatures([](const QcCreature &mob) { return !mob.villager.profession.isEmpty(); });
        return plans;
    }

    void measure(int villageCount)
    {
        QCOMPARE(startWith(villageCount).size(), villageCount);
        const qsizetype villagers = m_server->mobs().mobs().size();
        QVERIFY(villagers > 0);
        const int dayTicks = full() ? int(QcDayCycle::dayLength * 20.0) : 40;
        const Ticks with = day(dayTicks);

        QCOMPARE(removeCreatures([](const QcCreature &) { return false; }), 0);
        const Ticks without = day(dayTicks);
        qInfo("VILLAGERS villages=%d villagers=%lld chunks=%lld ticks=%d with: mean %.4f p99 %.4f max %.3f ms; without: mean %.4f "
              "p99 %.4f max %.3f ms; asleep at midnight %lld",
              villageCount, qint64(villagers), qint64(m_server->world().loadedChunkCount()), dayTicks, with.mean, with.p99, with.max,
              without.mean, without.p99, without.max, qint64(with.asleep));
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void oneVillage() { measure(1); }

    void theSwampVillageSleepsAtMidnight()
    {
        // The village nearest to the origin stands on boardwalks over a swamp (around 555, -214):
        // a whole day from sunrise (wandering, work, the meeting at the bell, the evening home) to
        // midnight; none stays in the water it fell into, none is stuck at a door, all are in bed.
        const QList<std::shared_ptr<const QcVillagePlan>> plans = startWith(1);
        QCOMPARE(plans.size(), 1);
        QCOMPARE(plans.front()->style, u"qtcraft:swamp"_s);
        const qsizetype villagers = m_server->mobs().mobs().size();
        QVERIFY(villagers >= 10);

        m_server->setWorldTime(QcDayCycle::dayLength * 10.0);
        const int toMidnight = int(QcDayCycle::dayLength * 20.0 * 0.75);
        for (int i = 0; i < toMidnight; ++i) {
            m_server->tick();
            if (i % 20 == 0) {
                pump();
            }
        }
        QStringList awake;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (!mob.villager.sleeping) {
                const auto cell = [](std::optional<QcBlockPos> pos) { return pos ? u"%1 %2 %3"_s.arg(pos->x).arg(pos->y).arg(pos->z) : u"none"_s; };
                awake.append(u"%1 at %2 %3 %4 home %5 target %6 water %7"_s.arg(mob.villager.name)
                                 .arg(mob.motion.position.x, 0, 'f', 1)
                                 .arg(mob.motion.position.y, 0, 'f', 1)
                                 .arg(mob.motion.position.z, 0, 'f', 1)
                                 .arg(cell(mob.villager.home), cell(mob.villager.target))
                                 .arg(mob.motion.inWater));
            }
        }
        const qsizetype asleep = villagers - awake.size();
        qInfo("swamp village: %lld of %lld villagers asleep at midnight", qint64(asleep), qint64(villagers));
        QVERIFY2(awake.isEmpty(), qPrintable(awake.join(u"; "_s)));
    }

    void tenVillages()
    {
        if (!full()) {
            QSKIP("QTCRAFT_VILLAGER_MEASURE not set");
        }
        measure(10);
    }
};

QTEST_GUILESS_MAIN(TestVillagerMeasure)
#include "test_villager_measure.moc"
