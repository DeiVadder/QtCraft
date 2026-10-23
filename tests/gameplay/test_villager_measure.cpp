#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/villages/QcVillagerBreeding.h"
#include "server/villages/QcVillages.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QElapsedTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

// Cost of villagers on the server (decision E-411): the villages of seed 42 nearest to the origin,
// all their chunks loaded, a villager in every bed and nothing else alive; a whole day from sunrise
// of their routine (wandering, work, meeting, going home, sleeping), ticked as fast as the server can, then
// the same day on the same chunks with the villagers removed. One village, then ten. A measurement,
// not a check: set QTCRAFT_VILLAGER_MEASURE=1 and use a Release build. Without it the test runs one
// village for a few ticks. A check beside it: the villagers of the swamp village sleep at midnight.
// With `grow` every village has a free bed and every villager food all day (decision E-415): the
// cost of the children born and of the breeding checks.
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

    // The `count` villages nearest to the origin, ring by ring of regions; only those of `style`
    // unless it is empty.
    QList<std::shared_ptr<const QcVillagePlan>> villages(int count, const QString &style) const
    {
        const QcTerrainGenerator generator(m_content, seed);
        QList<std::shared_ptr<const QcVillagePlan>> plans;
        for (int ring = 0; ring <= 20 && plans.size() < count; ++ring) {
            for (int rz = -ring; rz <= ring && plans.size() < count; ++rz) {
                for (int rx = -ring; rx <= ring && plans.size() < count; ++rx) {
                    if (std::max(std::abs(rx), std::abs(rz)) != ring) {
                        continue;
                    }
                    std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz);
                    if (plan && (style.isEmpty() || plan->style == style)) {
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
            times.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
            QcTestUtil::pump();
            if (i == count * 3 / 4) {
                asleep = std::ranges::count_if(m_server->mobs().mobs(), [](const QcCreature &mob) { return mob.pose == QcPose::Lying; });
            }
        }
        std::ranges::sort(times);
        double sum = 0.0;
        for (const double time : std::as_const(times)) {
            sum += time;
        }
        return {sum / static_cast<double>(times.size()), times[static_cast<qsizetype>(static_cast<double>(times.size() - 1) * 0.99)], times.last(), asleep};
    }

    // A server on seed 42 with the `count` villages (of `style`, unless empty) nearest to the origin
    // loaded, only their villagers alive.
    QList<std::shared_ptr<const QcVillagePlan>> startWith(int count, const QString &style = {})
    {
        QcServerConfig config;
        config.seed = seed;
        config.keepChunksLoaded = true;
        join(config, std::make_unique<QcTerrainGenerator>(m_content, seed));
        chat(u"/gamerule weatherCycle false"_s);
        const QList<std::shared_ptr<const QcVillagePlan>> plans = villages(count, style);
        loadVillages(plans);
        chat(u"/gamerule mobSpawning false"_s);
        removeCreatures([](const QcCreature &mob) { return mob.isVillager(); });
        return plans;
    }

    // Frees one bed per village (its villager leaves) and gives every villager food for a child.
    void prepareGrowth(const QList<std::shared_ptr<const QcVillagePlan>> &plans)
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const auto it = std::ranges::find_if(m_server->mobs().mobs(), [&plan](const QcCreature &mob) { return mob.isVillager() && mob.villager->village == plan->id; });
            if (it != m_server->mobs().mobs().end()) {
                m_server->mobs().remove(it->id);
            }
        }
        for (QcCreature &mob : m_server->mobs().editableMobs()) {
            if (mob.isVillager()) {
                mob.villager->produce = QcVillagerRecord::maxProduce;
            }
        }
    }

    // Microseconds one breeding check of all villages takes, on a copy of the creatures.
    double breedingCheckMicros()
    {
        QcVillagerBreeding breeding(m_content);
        QList<QcCreature> copy = m_server->mobs().mobs();
        QcServerWorld &world = m_server->world();
        constexpr int rounds = 200;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < rounds; ++i) {
            breeding.check(copy, {world, world.villages(), QcDayCycle::dayLength * 10.1, {}, {}});
        }
        return static_cast<double>(timer.nsecsElapsed()) / 1e3 / rounds;
    }

    void measure(int villageCount, bool grow = false)
    {
        const QList<std::shared_ptr<const QcVillagePlan>> plans = startWith(villageCount);
        QCOMPARE(plans.size(), villageCount);
        if (grow) {
            prepareGrowth(plans);
        }
        const qsizetype villagers = m_server->mobs().mobs().size();
        QVERIFY(villagers > 0);
        const double check = breedingCheckMicros();
        const int dayTicks = full() ? static_cast<int>(QcDayCycle::dayLength * 20.0) : 40;
        const Ticks with = day(dayTicks);
        const qsizetype born = m_server->mobs().mobs().size() - villagers;

        QCOMPARE(removeCreatures([](const QcCreature &) { return false; }), 0);
        const Ticks without = day(dayTicks);
        qInfo("VILLAGERS villages=%d villagers=%lld chunks=%lld ticks=%d with: mean %.4f p99 %.4f max %.3f ms; without: mean %.4f "
              "p99 %.4f max %.3f ms; asleep at midnight %lld; born %lld; breeding check %.2f us",
              villageCount, static_cast<qint64>(villagers), static_cast<qint64>(m_server->world().loadedChunkCount()), dayTicks, with.mean, with.p99, with.max,
              without.mean, without.p99, without.max, static_cast<qint64>(with.asleep), static_cast<qint64>(born), check);
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
        // The swamp village nearest to the origin stands on boardwalks over a swamp: a whole day
        // from sunrise (wandering, work, the meeting at the bell, the evening home) to midnight;
        // none stays in the water it fell into, none is stuck at a door, all are in bed.
        const QList<std::shared_ptr<const QcVillagePlan>> plans = startWith(1, u"qtcraft:swamp"_s);
        QCOMPARE(plans.size(), 1);
        const qsizetype villagers = m_server->mobs().mobs().size();
        QVERIFY(villagers >= 10);

        m_server->setWorldTime(QcDayCycle::dayLength * 10.0);
        const int toMidnight = static_cast<int>(QcDayCycle::dayLength * 20.0 * 0.75);
        for (int i = 0; i < toMidnight; ++i) {
            m_server->tick();
            if (i % 20 == 0) {
                QcTestUtil::pump();
            }
        }
        QStringList awake;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (mob.pose != QcPose::Lying) {
                const auto cell = [](std::optional<QcBlockPos> pos) { return pos ? u"%1 %2 %3"_s.arg(pos->x).arg(pos->y).arg(pos->z) : u"none"_s; };
                awake.append(u"%1 at %2 %3 %4 home %5 target %6 water %7"_s.arg(mob.uid)
                                 .arg(mob.motion.position.x, 0, 'f', 1)
                                 .arg(mob.motion.position.y, 0, 'f', 1)
                                 .arg(mob.motion.position.z, 0, 'f', 1)
                                 .arg(cell(mob.villager->home), cell(mob.villager->target))
                                 .arg(mob.motion.inWater));
            }
        }
        const qsizetype asleep = villagers - awake.size();
        qInfo("swamp village: %lld of %lld villagers asleep at midnight", static_cast<qint64>(asleep), static_cast<qint64>(villagers));
        QVERIFY2(awake.isEmpty(), qPrintable(awake.join(u"; "_s)));
    }

    void tenVillages()
    {
        if (!full()) {
            QSKIP("QTCRAFT_VILLAGER_MEASURE not set");
        }
        measure(10);
    }

    void tenVillagesGrow()
    {
        if (!full()) {
            QSKIP("QTCRAFT_VILLAGER_MEASURE not set");
        }
        measure(10, true);
    }
};

QTEST_GUILESS_MAIN(TestVillagerMeasure)
#include "test_villager_measure.moc"
