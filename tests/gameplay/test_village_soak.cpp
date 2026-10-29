// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"
#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageFixture.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

// Village life over whole days on a server (decision E-424): four fixture villages (plains, desert,
// swamp on stilts, snowy; turned 0°, 90°, 180° and 270°) live through `days` in-game days, ticked by hand as fast as the server
// can, and keep their invariants: each village's population is the number of villagers with its id
// (children included), no villager holds two beds or two workstations and every claim belongs to a
// villager in the world, nobody is stuck (no progress towards a far target for two minutes of game
// time) or treading water for long, nobody dies or leaves without a cause, at least 95 % lie in bed
// at midnight, and the claims and records come back unchanged after saving and reopening the world
// in the middle of the run. Before the first day one villager per village leaves and everyone gets
// food, so children are born into the free beds.
//
// Without QTCRAFT_VILLAGE_SOAK_DAYS the test runs one day (about 12 000 ticks, the CI variant); with
// it that many days (3 for the soak recorded in docs/Performance-Verlauf.md), best in Release.
class TestVillageSoak : public QcServerTestFixture
{
    Q_OBJECT

private:
    // The share of villagers that must lie in bed at midnight. The rest may still be on the way: a
    // villager pushed aside at a shared door or behind a slow one on a ladder is late, not lost.
    static constexpr double asleepAtMidnight = 0.95;
    // Ticks between two samples of every villager's position (20 s of game time).
    static constexpr int sampleTicks = 400;
    // Samples without progress (two minutes) that make a villager with a far target stuck, and
    // samples in water in a row (one minute) that make it a swimmer that does not get out.
    static constexpr int stuckSamples = 6;
    static constexpr int wetSamples = 3;
    // Phase of the day of the midnight check (QcVillagerSettings::routine: asleep since 0.6) and of
    // the save and reopen (work and meeting time, villagers on their way).
    static constexpr double midnight = 0.75;
    static constexpr double reopenPhase = 0.4;

    static int days()
    {
        bool ok = false;
        const int set = qEnvironmentVariableIntValue("QTCRAFT_VILLAGE_SOAK_DAYS", &ok);
        return ok && set > 0 ? set : 1;
    }

    // What the test keeps of a villager between samples.
    struct Sample
    {
        QcVec3 position;
        double distance = 0.0;
        int still = 0;
        int wet = 0;
    };

    QTemporaryDir m_directory;
    QcServerConfig m_config;
    QList<std::shared_ptr<const QcVillagePlan>> m_plans;
    QHash<quint64, Sample> m_samples;
    QStringList m_problems;
    // Villagers removed by the test on purpose (their departure is no problem).
    QSet<quint64> m_removed;
    // The beds of the villagers removed by the test: children take them.
    QList<std::pair<qint64, QcBlockPos>> m_freedBeds;
    std::optional<QcMobManager::DepartureToken> m_departures;
    qsizetype m_born = 0;
    qint64 m_ticks = 0;

    QList<const QcCreature *> villagers() const
    {
        QList<const QcCreature *> list;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (mob.isVillager()) {
                list.append(&mob);
            }
        }
        return list;
    }

    static QString describe(const QcCreature &mob)
    {
        const auto cell = [](std::optional<QcBlockPos> pos) { return pos ? u"%1 %2 %3"_s.arg(pos->x).arg(pos->y).arg(pos->z) : u"none"_s; };
        return u"villager %1 of village %2 at %3 %4 %5, home %6, target %7, water %8"_s.arg(mob.uid)
            .arg(mob.villager->village)
            .arg(mob.motion.position.x, 0, 'f', 1)
            .arg(mob.motion.position.y, 0, 'f', 1)
            .arg(mob.motion.position.z, 0, 'f', 1)
            .arg(cell(mob.villager->home), cell(mob.villager->target))
            .arg(mob.motion.inWater)
            + u" path: purpose %1 goal %2 %3 %4 planned %5 reaches %6 cells %7 next %8 failures %9"_s.arg(static_cast<int>(mob.path.purpose))
                  .arg(mob.path.goal.x).arg(mob.path.goal.y).arg(mob.path.goal.z).arg(mob.path.planned).arg(mob.path.reachesGoal)
                  .arg(static_cast<qint64>(mob.path.cells.size())).arg(static_cast<qint64>(mob.path.next)).arg(mob.path.failures)
            + u" door %1 leg %2"_s.arg(cell(mob.villager->openedDoor), cell(mob.villager->legs.leg));
    }

    // Opens the world (new or saved) with the fixture villages, every chunk around them loaded.
    void open()
    {
        join(m_config, std::make_unique<QcVillageFixture::Terrain>(m_content, m_plans));
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule weatherCycle false"_s);
        // Every chunk a villager of the village may stray into, so the claims are reconciled.
        const int ring = (QcVillage::reach + QcVillagerHomes::strayReach + QcWorld::chunkSize - 1) / QcWorld::chunkSize;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*plan, ring);
            for (const QcChunkPos &chunk : chunks) {
                m_server->world().loadChunk(chunk);
            }
        }
        ticks(2);
        m_departures = m_server->mobs().onDeparture([this](const QcCreature &mob, QcMobManager::Departure why) {
            if (mob.isVillager() && !m_removed.contains(mob.uid) && why != QcMobManager::Departure::Unloaded) {
                m_problems.append(u"departed (%1): %2"_s.arg(static_cast<int>(why)).arg(describe(mob)));
            }
        });
        m_samples.clear();
    }

    // One villager per village leaves and everyone gets food: children come into the free beds.
    void makeRoomForChildren()
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const QList<const QcCreature *> all = villagers();
            const auto leaving = std::ranges::find_if(all, [&plan](const QcCreature *mob) { return mob->villager->village == plan->id; });
            QVERIFY(leaving != all.end());
            m_removed.insert((*leaving)->uid);
            QVERIFY((*leaving)->villager->home.has_value());
            m_freedBeds.append({plan->id, *(*leaving)->villager->home});
            QVERIFY(m_server->mobs().remove((*leaving)->id));
        }
        for (QcCreature &mob : m_server->mobs().editableMobs()) {
            if (mob.isVillager()) {
                mob.villager->produce = QcVillagerRecord::maxProduce;
            }
        }
    }

    // Population, claims and homes of every village against the villagers in the world.
    void checkClaims(const QString &when)
    {
        QHash<qint64, int> counted;
        QHash<quint64, const QcCreature *> byUid;
        const QList<const QcCreature *> living = villagers();
        for (const QcCreature *mob : living) {
            ++counted[mob->villager->village];
            byUid.insert(mob->uid, mob);
        }
        const QList<QcVillage> villages = m_server->world().villages().villages();
        for (const QcVillage &village : villages) {
            if (village.population != counted.value(village.id)) {
                m_problems.append(u"%1: village %2 counts %3, %4 live there"_s.arg(when).arg(village.id).arg(village.population).arg(counted.value(village.id)));
            }
            checkSpots(when, village, village.beds, byUid);
            checkSpots(when, village, village.workstations, byUid);
        }
    }

    // Each claimed spot of the list belongs to a villager of the village in the world, and no
    // villager holds two of them.
    void checkSpots(const QString &when, const QcVillage &village, const QList<QcVillage::Spot> &spots, const QHash<quint64, const QcCreature *> &byUid)
    {
        QSet<quint64> holders;
        for (const QcVillage::Spot &spot : spots) {
            if (spot.claimedBy == 0) {
                continue;
            }
            const QcCreature *holder = byUid.value(spot.claimedBy);
            if (!holder || holder->villager->village != village.id) {
                m_problems.append(u"%1: spot %2 %3 %4 of village %5 claimed by %6, not in it"_s.arg(when).arg(spot.at.x).arg(spot.at.y).arg(spot.at.z).arg(village.id).arg(spot.claimedBy));
            } else if (holders.contains(spot.claimedBy)) {
                m_problems.append(u"%1: %2 holds two spots"_s.arg(when, describe(*holder)));
            } else if (spot.kind.isEmpty() ? holder->villager->home != spot.at : holder->villager->work != spot.at) {
                m_problems.append(u"%1: spot %2 %3 %4 claimed by %5, who does not know it"_s.arg(when).arg(spot.at.x).arg(spot.at.y).arg(spot.at.z).arg(describe(*holder)));
            }
            holders.insert(spot.claimedBy);
        }
    }

    // Progress of every villager since the last sample.
    void sample()
    {
        QHash<quint64, Sample> next;
        const QList<const QcCreature *> living = villagers();
        for (const QcCreature *mob : living) {
            Sample now{mob->motion.position, 0.0, 0, 0};
            if (mob->villager->target) {
                const QcBlockPos &target = *mob->villager->target;
                now.distance = std::hypot(now.position.x - target.x - 0.5, now.position.z - target.z - 0.5);
            }
            const auto before = m_samples.constFind(mob->uid);
            if (before != m_samples.cend()) {
                const double moved = std::hypot(now.position.x - before->position.x, now.position.z - before->position.z);
                const bool far = mob->villager->target && now.distance > 3.0 && mob->pose != QcPose::Lying;
                now.still = far && moved < 0.5 && now.distance >= before->distance - 0.5 ? before->still + 1 : 0;
                now.wet = mob->motion.inWater ? before->wet + 1 : 0;
                if (now.still == stuckSamples) {
                    m_problems.append(u"stuck: %1"_s.arg(describe(*mob)));
                }
                if (now.wet == wetSamples) {
                    m_problems.append(u"in the water: %1"_s.arg(describe(*mob)));
                }
            }
            next.insert(mob->uid, now);
        }
        m_samples = std::move(next);
    }

    // Ticks on to `phase` of day `day` (counted from the first sunrise), sampling as it goes.
    void tickTo(int day, double phase)
    {
        const double until = QcDayCycle::dayLength * (10.0 + day + phase);
        const qsizetype before = villagers().size();
        while (m_server->worldTime() < until) {
            m_server->tick();
            ++m_ticks;
            if (m_ticks % sampleTicks == 0) {
                QcTestUtil::pump();
                sample();
            }
        }
        m_born += std::max<qsizetype>(0, villagers().size() - before);
    }

    // At least asleepAtMidnight of the villagers lie in bed.
    void checkSleep(int day)
    {
        const QList<const QcCreature *> all = villagers();
        QStringList awake;
        for (const QcCreature *mob : all) {
            if (mob->pose != QcPose::Lying) {
                awake.append(describe(*mob));
            }
        }
        const double share = static_cast<double>(all.size() - awake.size()) / static_cast<double>(std::max<qsizetype>(1, all.size()));
        qInfo("day %d: %lld of %lld villagers asleep at midnight", day, static_cast<qint64>(all.size() - awake.size()), static_cast<qint64>(all.size()));
        if (share < asleepAtMidnight) {
            m_problems.append(u"day %1: only %2 % asleep: %3"_s.arg(day).arg(share * 100.0, 0, 'f', 1).arg(awake.join(u"; "_s)));
        }
    }

    // The villages with their beds and workstations in the order of their cells: the order of the
    // spots follows the order their chunks were loaded in (QcVillages::rescan).
    static QList<QcVillage> inOrder(QList<QcVillage> villages)
    {
        const auto byCell = [](const QcVillage::Spot &spot) { return std::tuple(spot.at.x, spot.at.y, spot.at.z); };
        for (QcVillage &village : villages) {
            std::ranges::sort(village.beds, {}, byCell);
            std::ranges::sort(village.workstations, {}, byCell);
        }
        return villages;
    }

    // Saves, closes and reopens the world: villages and villager records come back as they were.
    void saveAndReopen()
    {
        const QList<QcVillage> villages = m_server->world().villages().villages();
        QHash<quint64, QcVillagerRecord> records;
        const QList<const QcCreature *> living = villagers();
        for (const QcCreature *mob : living) {
            records.insert(mob->uid, static_cast<const QcVillagerRecord &>(*mob->villager));
        }
        const double time = m_server->worldTime();
        QVERIFY(m_server->save().has_value());
        close();

        open();
        // Opening ticks the server a few times.
        QVERIFY(std::abs(m_server->worldTime() - time) < 1.0);
        QCOMPARE(inOrder(m_server->world().villages().villages()), inOrder(villages));
        QHash<quint64, QcVillagerRecord> back;
        const QList<const QcCreature *> reopened = villagers();
        for (const QcCreature *mob : reopened) {
            back.insert(mob->uid, static_cast<const QcVillagerRecord &>(*mob->villager));
        }
        QCOMPARE(back.size(), records.size());
        for (auto it = records.cbegin(); it != records.cend(); ++it) {
            QVERIFY2(back.contains(it.key()), qPrintable(QString::number(it.key())));
            QCOMPARE(back.value(it.key()).home, it->home);
            QCOMPARE(back.value(it.key()).work, it->work);
            QCOMPARE(back.value(it.key()).village, it->village);
        }
        checkClaims(u"after reopening"_s);
    }

    // Closes the world; the departures of its creatures from here on are no problem.
    void close()
    {
        if (m_server && m_departures) {
            m_server->mobs().removeDepartureListener(*m_departures);
        }
        m_departures.reset();
        m_client.reset();
        m_server.reset();
    }

    // Every bed the test freed is claimed again (by a child, or a resident moving in on reopening).
    void checkFreedBedsReclaimed()
    {
        const QList<QcVillage> villages = m_server->world().villages().villages();
        for (const auto &[village, bed] : std::as_const(m_freedBeds)) {
            const auto it = std::ranges::find(villages, village, &QcVillage::id);
            QVERIFY(it != villages.end());
            const auto spot = std::ranges::find(it->beds, bed, &QcVillage::Spot::at);
            QVERIFY(spot != it->beds.end());
            QVERIFY2(spot->claimedBy != 0, qPrintable(u"bed %1 %2 %3 of village %4 is still free"_s.arg(bed.x).arg(bed.y).arg(bed.z).arg(village)));
            QVERIFY(!m_removed.contains(spot->claimedBy));
        }
    }

private slots:
    void cleanup()
    {
        close();
    }

    void villagesLiveForDays();
};

void TestVillageSoak::villagesLiveForDays()
{
    QVERIFY(m_directory.isValid());
    QcVillageFixture::Options options;
    options.alwaysLivedIn = true;
    options.turnEach = true;
    m_plans = QcVillageFixture::spread(*m_content, {u"qtcraft:plains"_s, u"qtcraft:desert"_s, u"qtcraft:swamp"_s, u"qtcraft:snowy"_s}, 2, options);
    QCOMPARE(m_plans.size(), 4);
    m_config.worldDirectory = m_directory.path();
    m_config.keepChunksLoaded = true;
    open();
    m_server->setWorldTime(QcDayCycle::dayLength * 10.0);
    const qsizetype settled = villagers().size();
    QVERIFY(settled >= qsizetype(4 * 8));
    checkClaims(u"at the start"_s);
    makeRoomForChildren();
    QVERIFY2(m_problems.isEmpty(), qPrintable(m_problems.join(u"\n"_s)));

    QElapsedTimer timer;
    timer.start();
    const int count = days();
    for (int day = 0; day < count; ++day) {
        if (day == count / 2) {
            tickTo(day, reopenPhase);
            saveAndReopen();
        }
        tickTo(day, midnight);
        checkSleep(day);
        checkClaims(u"day %1 at midnight"_s.arg(day));
        tickTo(day + 1, 0.0);
        QVERIFY2(m_problems.isEmpty(), qPrintable(m_problems.join(u"\n"_s)));
    }
    const qsizetype children = std::ranges::count_if(villagers(), [](const QcCreature *mob) { return mob->isYoung(); });
    qInfo("SOAK days=%d villages=%lld villagers=%lld (settled %lld) born=%lld young=%lld ticks=%lld wall=%.1f s mean tick %.3f ms", count,
          static_cast<qint64>(m_plans.size()), static_cast<qint64>(villagers().size()), static_cast<qint64>(settled),
          static_cast<qint64>(m_born), static_cast<qint64>(children), m_ticks,
          static_cast<double>(timer.elapsed()) / 1000.0, static_cast<double>(timer.nsecsElapsed()) / 1e6 / static_cast<double>(std::max<qint64>(1, m_ticks)));
    QVERIFY(villagers().size() >= settled - m_plans.size());
    // Children were born into the freed beds.
    QVERIFY(m_born > 0);
    checkFreedBedsReclaimed();
}

QTEST_GUILESS_MAIN(TestVillageSoak)
#include "test_village_soak.moc"
