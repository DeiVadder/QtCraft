#include "QcBaseContent.h"
#include "QcDayCycle.h"
#include "QcFlatGenerator.h"
#include "QcMobBehaviour.h"
#include "QcMobManager.h"
#include "QcMobNavigator.h"
#include "QcPathPlanner.h"
#include "QcServerWorld.h"

#include <QSet>
#include <QTest>

#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = 0.05;
constexpr double midnight = QcDayCycle::dayLength * 0.75;
// The flat world's ground is grass at y 4; feet stand at y 5.
constexpr int ground = 5;

} // namespace

// Creatures walking their paths on the server (decision E-400), ticked by hand: chasing around
// walls, melee only with a line of sight, budgets, reusing and renewing paths, wandering and fleeing
// to reachable places, keeping apart.
class TestMobNavigation : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = [] {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }();
    quint32 m_nextId = 100;

    std::unique_ptr<QcServerWorld> makeWorld(int radius = 2)
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,grass_block"_s));
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    void fill(QcServerWorld &world, QcBlockPos from, QcBlockPos to, const QString &name) const
    {
        const QcBlockId id = name.isEmpty() ? qcAirBlockId : *m_content->stateId(u"qtcraft:"_s + name);
        for (int x = from.x; x <= to.x; ++x) {
            for (int y = from.y; y <= to.y; ++y) {
                for (int z = from.z; z <= to.z; ++z) {
                    world.setBlock({x, y, z}, id);
                }
            }
        }
    }

    std::unique_ptr<QcMobManager> makeMobs(quint64 seed = 1) const
    {
        auto mobs = std::make_unique<QcMobManager>(m_content, seed);
        mobs->setNaturalSpawning(false);
        return mobs;
    }

    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }

    static QcVec3 at(double x, double z, double y = ground) { return {x, y, z}; }

    // Ticks the creatures; returns the number of blows they dealt.
    int run(QcMobManager &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players, int ticks)
    {
        int hits = 0;
        for (int tick = 0; tick < ticks; ++tick) {
            hits += int(mobs.tick(world, players, midnight, dt, [this] { return m_nextId++; }).size());
        }
        return hits;
    }

    // A glass room around the cells x 0..1, z 0 (feet at ground level), with a roof; with `window`
    // an opening of one cell at eye height in its east wall, too low for a creature to pass, through
    // which creatures east of it see a player inside (decision E-401).
    void glassRoom(QcServerWorld &world, bool window = false) const
    {
        fill(world, {-1, ground, -1}, {2, ground + 2, 1}, u"glass"_s);
        fill(world, {0, ground, 0}, {1, ground + 1, 0}, {});
        if (window) {
            fill(world, {2, ground + 1, 0}, {2, ground + 1, 0}, {});
        }
    }

    // Lets every creature remember the player `playerId` for good, as if it had seen them: hunters
    // chase without a line of sight (decision E-401).
    static void remember(QcMobManager &mobs, quint32 playerId)
    {
        for (QcCreature &mob : mobs.editableMobs()) {
            mob.chasing = playerId;
            mob.targetMemory = 1e9;
        }
    }

private slots:
    void chasesAroundAWallAndStrikes()
    {
        auto world = makeWorld();
        // The zombie sees the player through a window too low to pass, loses sight of them on its
        // way around and remembers them until it sees them again past the wall's end.
        fill(*world, {0, ground, -8}, {0, ground + 2, 8}, u"stone"_s);
        fill(*world, {0, ground + 1, 0}, {0, ground + 1, 0}, {});
        auto mobs = makeMobs();
        mobs->spawn(type("zombie"), at(-5.5, 0.5), 1);

        int ticks = 0;
        int hits = 0;
        while (hits == 0 && ticks < 20 * 15) {
            hits += run(*mobs, *world, {{7, at(6.5, 0.5), true}}, 1);
            ++ticks;
        }
        QVERIFY2(hits > 0, "the zombie never reached the player");
        QVERIFY(mobs->find(1)->motion.position.x > 0.5);
    }

    void meleeNeedsALineOfSight()
    {
        auto world = makeWorld();
        glassRoom(*world, true);
        auto mobs = makeMobs();
        // The player stands at the room's east wall, the zombie right outside it, in reach: through
        // the window it sees the player and its blows land.
        const QList<QcMobManager::PlayerView> players{{7, at(1.7, 0.5), true}};
        mobs->spawn(type("zombie"), at(3.3, 0.5), 1);
        // It stands until it first looks for players instead of wandering off.
        mobs->editableMobs().first().wanderTimer = 1e9;
        QVERIFY(run(*mobs, *world, players, 20 * 5) > 0);

        // Glass in the window: it still remembers the player but strikes no more.
        fill(*world, {2, ground + 1, 0}, {2, ground + 1, 0}, u"glass"_s);
        QCOMPARE(run(*mobs, *world, players, 20 * 4), 0);
        QCOMPARE(mobs->find(1)->chasing, quint32(7));
    }

    void unreachablePreyIsNotSearchedEveryTick()
    {
        auto world = makeWorld();
        // Seen through the window, out of reach behind it.
        glassRoom(*world, true);
        auto mobs = makeMobs();
        mobs->spawn(type("zombie"), at(9.5, 0.5), 1);
        QCOMPARE(run(*mobs, *world, {{7, at(0.5, 0.5), true}}, 20 * 20), 0);

        // The back-off doubles up to eight seconds: a handful of searches in 20 s, not 400.
        const qint64 searches = mobs->paths().searches();
        QVERIFY2(searches >= 2 && searches <= 10, qPrintable(QString::number(searches)));
        // It waits next to the room, as close as it gets.
        QVERIFY((mobs->find(1)->motion.position - at(0.5, 0.5)).length() < 4.0);
    }

    void unreachableRunningPreyIsNotSearchedEveryTenTicks()
    {
        auto world = makeWorld();
        // The player runs to and fro on a walled platform six blocks up, out of the zombie's reach.
        fill(*world, {-12, ground, -2}, {12, ground + 5, 2}, u"stone"_s);
        auto mobs = makeMobs();
        mobs->spawn(type("zombie"), at(0.5, 8.5), 1);
        remember(*mobs, 7);
        int hits = 0;
        for (int tick = 0; tick < 20 * 20; ++tick) {
            // 4 m/s along x between -10 and 10.
            const double phase = std::fmod(tick * 0.2, 40.0);
            const double x = phase < 20.0 ? -10.0 + phase : 30.0 - phase;
            hits += run(*mobs, *world, {{7, at(x, 0.5, ground + 6), true}}, 1);
        }
        QCOMPARE(hits, 0);

        // Waiting after each failed search, it searches anew only when the player moved far.
        const qint64 searches = mobs->paths().searches();
        QVERIFY2(searches >= 2 && searches <= 14, qPrintable(QString::number(searches)));
    }

    void reachableRunningPreyIsCaught()
    {
        auto world = makeWorld();
        auto mobs = makeMobs();
        mobs->spawn(type("zombie"), at(-9.5, 0.5), 1);
        remember(*mobs, 7);
        int hits = 0;
        int tick = 0;
        for (; tick < 20 * 20 && hits == 0; ++tick) {
            // The player walks in a circle of six blocks at 2 m/s.
            const double angle = tick * 0.05 * 2.0 / 6.0;
            hits += run(*mobs, *world, {{7, at(6.0 * std::cos(angle), 6.0 * std::sin(angle)), true}}, 1);
        }
        QVERIFY2(hits > 0, "the zombie never caught the player");
        QVERIFY2(tick < 20 * 12, qPrintable(QString::number(tick)));
    }

    void aGoalOfAnotherPurposeStartsWithoutAPath()
    {
        QcCreature mob;
        QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Wander, at(5.5, 0.5), 0);
        mob.path.planned = true;
        mob.path.cells = {{1, ground, 0}, {2, ground, 0}};
        mob.path.failures = 3;
        mob.path.waitTicks = 50;

        // The same purpose keeps the path, another drops it with its failures.
        QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Wander, at(6.5, 0.5), 0);
        QVERIFY(mob.path.planned);
        QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Chase, at(6.5, 0.5), 1);
        QVERIFY(!mob.path.planned);
        QVERIFY(mob.path.cells.empty());
        QCOMPARE(mob.path.failures, 0);
        QCOMPARE(mob.path.waitTicks, 0);
        QCOMPARE(mob.path.purpose, QcMobPath::Purpose::Chase);
    }

    void throttledCreaturesCoastButNotOffAnEdge()
    {
        auto world = makeWorld();
        // A plateau five blocks high; two in a cell already throttle, so they mostly coast.
        fill(*world, {-2, ground, -2}, {2, ground + 4, 2}, u"stone"_s);
        auto mobs = makeMobs(7);
        mobs->setCrowdLimits(2, 1000);
        for (quint32 id = 1; id <= 8; ++id) {
            mobs->spawn(type("pig"), at(-1.5 + (id % 3), -1.5 + (id % 4), ground + 5), id, false, true);
        }
        int throttled = 0;
        for (int tick = 0; tick < 20 * 60; ++tick) {
            run(*mobs, *world, {}, 1);
            throttled += mobs->behaviour().tiersLastTick().throttled;
            for (const QcCreature &pig : mobs->mobs()) {
                QVERIFY2(pig.motion.position.y > ground + 4.9, qPrintable(QString::number(tick)));
            }
        }
        QVERIFY(throttled > 20 * 60);
    }

    void resumesAfterRestingInAnUnloadedChunk()
    {
        auto world = makeWorld();
        fill(*world, {0, ground, -8}, {0, ground + 2, 8}, u"stone"_s);
        auto mobs = makeMobs();
        mobs->spawn(type("zombie"), at(-6.5, 0.5), 1);
        remember(*mobs, 7);
        run(*mobs, *world, {{7, at(6.5, 0.5), true}}, 20);
        QVERIFY(mobs->find(1)->path.planned);

        // Its chunk unloads while it waits for a search, the player moves round the wall's other end.
        mobs->find(1)->path.age = 1000;
        mobs->find(1)->dormant = true;
        run(*mobs, *world, {{7, at(6.5, -12.5), true}}, 100);
        QVERIFY(!mobs->find(1)->path.queued);

        // Awake again it searches for the new place and gets there.
        mobs->find(1)->dormant = false;
        const qint64 before = mobs->paths().searches();
        int hits = 0;
        for (int tick = 0; tick < 20 * 15 && hits == 0; ++tick) {
            hits += run(*mobs, *world, {{7, at(6.5, -12.5), true}}, 1);
        }
        QVERIFY(hits > 0);
        QVERIFY(mobs->paths().searches() > before);
    }

    void staticPreyIsNotSearchedAgain()
    {
        auto world = makeWorld();
        auto mobs = makeMobs();
        mobs->spawn(type("zombie"), at(-14.5, 0.5), 1);
        // Until it first looks for players (QcMobSteering::sightCheckTicks) it may wander.
        const QList<QcMobManager::PlayerView> players{{7, at(0.5, 0.5), true}};
        for (int tick = 0; tick < 20 && mobs->find(1)->chasing == 0; ++tick) {
            run(*mobs, *world, players, 1);
        }
        QCOMPARE(mobs->find(1)->chasing, quint32(7));
        const qint64 before = mobs->paths().searches();
        run(*mobs, *world, players, 70);
        QVERIFY2(mobs->paths().searches() - before <= 1, qPrintable(QString::number(mobs->paths().searches() - before)));
        QVERIFY(mobs->find(1)->motion.position.x > -6.0);
    }

    void searchesAgainWhenABlockChanges()
    {
        auto world = makeWorld();
        auto mobs = makeMobs();
        const QList<QcMobManager::PlayerView> players{{7, at(9.5, 0.5), true}};
        mobs->spawn(type("zombie"), at(-9.5, 0.5), 1);
        // It may wander until it first looks for players; its chase starts with a search of its own.
        run(*mobs, *world, players, 11);
        QCOMPARE(mobs->find(1)->chasing, quint32(7));
        QVERIFY(mobs->find(1)->path.planned);
        const qint64 before = mobs->paths().searches();
        run(*mobs, *world, players, 10);
        QCOMPARE(mobs->paths().searches(), before);

        // A wall across its way: the next waypoints fail the check and it searches anew.
        fill(*world, {0, ground, -5}, {0, ground + 2, 5}, u"stone"_s);
        int hits = run(*mobs, *world, players, 30);
        QVERIFY(mobs->paths().searches() > before);
        for (int tick = 0; tick < 20 * 15 && hits == 0; ++tick) {
            hits += run(*mobs, *world, players, 1);
        }
        QVERIFY(hits > 0);
    }

    void nodeBudgetHoldsFor200Searchers()
    {
        auto world = makeWorld();
        // Pillars every third cell make the searches work.
        for (int x = -24; x <= 24; x += 3) {
            for (int z = -24; z <= 24; z += 3) {
                if (std::abs(x) > 2 || std::abs(z) > 2) {
                    fill(*world, {x, ground, z}, {x, ground + 2, z}, u"stone"_s);
                }
            }
        }
        auto mobs = makeMobs();
        mobs->setPathLimits(1024, 256, 64);
        for (int i = 0; i < 200; ++i) {
            // On the cells between the pillars, 8 to 16 cells from the player.
            const double angle = i * 2.0 * std::numbers::pi / 200.0;
            const double radius = 8.0 + (i % 9);
            const int x = int(std::lround(std::cos(angle) * radius / 3.0)) * 3 + 1;
            const int z = int(std::lround(std::sin(angle) * radius / 3.0)) * 3 + 1;
            mobs->spawn(type("zombie"), at(x + 0.5, z + 0.5), m_nextId++);
        }
        // The pillars hide the player from most of them.
        remember(*mobs, 7);

        QSet<quint32> planned;
        const QList<QcMobManager::PlayerView> players{{7, at(0.5, 0.5), true}};
        for (int tick = 0; tick < 100; ++tick) {
            run(*mobs, *world, players, 1);
            QVERIFY2(mobs->paths().nodesLastTick() <= 1024, qPrintable(QString::number(mobs->paths().nodesLastTick())));
            for (const QcCreature &mob : mobs->mobs()) {
                if (mob.path.planned) {
                    planned.insert(mob.id);
                }
            }
        }
        // Every creature got its turn (the queue is served first come, first served).
        QCOMPARE(planned.size(), qsizetype(200));
    }

    void neverLoadsChunks()
    {
        auto world = makeWorld(1);
        const qsizetype loaded = world->loadedChunks().size();
        auto mobs = makeMobs();
        // The player stands in a chunk that is not loaded; the zombie walks to the edge and waits.
        mobs->spawn(type("zombie"), at(25.5, 0.5), 1);
        run(*mobs, *world, {{7, at(40.5, 0.5), true}}, 20 * 10);
        QCOMPARE(world->loadedChunks().size(), loaded);
        QVERIFY(mobs->find(1)->motion.position.x < 32.0);
        QVERIFY(mobs->paths().searches() <= 8);
    }

    void wanderersStayWhereTheyCanWalk()
    {
        auto world = makeWorld();
        // A plateau five blocks high: every way down is a drop of five.
        fill(*world, {-3, ground, -3}, {3, ground + 4, 3}, u"stone"_s);
        auto mobs = makeMobs(5);
        mobs->spawn(type("pig"), at(-0.5, -0.5, ground + 5), 1, false, true);
        mobs->spawn(type("pig"), at(1.5, 1.5, ground + 5), 2, false, true);
        for (int tick = 0; tick < 20 * 60; ++tick) {
            run(*mobs, *world, {}, 1);
            for (const QcCreature &pig : mobs->mobs()) {
                QVERIFY2(pig.motion.position.y > ground + 4.9, qPrintable(QString::number(tick)));
            }
        }
        // They did walk about.
        QVERIFY(mobs->paths().searches() > 4);
    }

    void fleeingPicksAPlaceAwayItCanReach()
    {
        auto world = makeWorld();
        fill(*world, {-3, ground, -3}, {6, ground + 4, 3}, u"stone"_s);
        auto mobs = makeMobs(5);
        mobs->spawn(type("pig"), at(-1.5, 0.5, ground + 5), 1, false, true);
        QVERIFY(!mobs->damage(1, 1.0, at(-6.0, 0.5, ground + 5)).has_value());
        run(*mobs, *world, {}, 20 * 4);

        const QcCreature *pig = mobs->find(1);
        QVERIFY(pig->motion.position.x > 2.0);
        QVERIFY(pig->motion.position.y > ground + 4.9);
    }

    void stackedCreaturesMoveApart()
    {
        auto world = makeWorld();
        auto mobs = makeMobs(3);
        for (quint32 id = 1; id <= 6; ++id) {
            mobs->spawn(type("pig"), at(0.5, 0.5), id, false, true);
        }
        run(*mobs, *world, {}, 60);

        double closest = 100.0;
        for (const QcCreature &a : mobs->mobs()) {
            for (const QcCreature &b : mobs->mobs()) {
                if (a.id < b.id) {
                    closest = std::min(closest, std::hypot(a.motion.position.x - b.motion.position.x, a.motion.position.z - b.motion.position.z));
                }
            }
        }
        QVERIFY2(closest > 0.3, qPrintable(QString::number(closest)));
    }

    void withoutPathFindingCreaturesWalkStraight()
    {
        auto world = makeWorld();
        fill(*world, {0, ground, -8}, {0, ground + 2, 8}, u"stone"_s);
        auto mobs = makeMobs();
        mobs->setPathLimits(0, 512, 64);
        mobs->spawn(type("zombie"), at(-5.5, 0.5), 1);
        QCOMPARE(run(*mobs, *world, {{7, at(6.5, 0.5), true}}, 20 * 10), 0);
        QCOMPARE(mobs->paths().searches(), qint64(0));
        QVERIFY(mobs->find(1)->motion.position.x < 0.0);
    }
};

QTEST_GUILESS_MAIN(TestMobNavigation)
#include "test_mob_navigation.moc"
