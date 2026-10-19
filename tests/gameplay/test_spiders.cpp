#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/mobs/QcMobSteering.h"

#include <QElapsedTimer>

#include <numbers>

using namespace Qt::StringLiterals;

// Spiders and cave spiders (decision E-451): wall climbing in the shared physics and on paths,
// neutral by day and hostile at night, leaping, the cave spider's poison and its size. Ticks are
// stepped by hand.
class TestSpiders : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double noon = QcDayCycle::dayLength * 10.25;
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    static constexpr int arena = 12;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }

    void joinArena(double time)
    {
        join();
        chat(u"/gamemode survival"_s);
        m_server->setWorldTime(time);
        loadAround(m_feet, 1);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 10; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        player().hunger.restore(10, 0.0);
    }

    quint32 spawn(const char *name, int dx, int dy, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, double(m_feet.y + dy), m_feet.z + dz + 0.5};
        return m_server->entities().creatures().spawn(type(name), at, m_server->entities().newId());
    }

    // One tick of a spider's body against the wall world.
    static QcPhysics::MoveResult step(QcEntityMotion &motion, const QcVec3 &wish, bool jump, const QcBlockAccess &world)
    {
        const QcPhysics::Steering steering{.wish = wish, .speed = 3.0, .jump = jump, .climbsWalls = true};
        return motion.step(steering, {1.3, 0.9}, world, QcPhysics::tickDuration);
    }

private slots:
    void spidersComeFromData()
    {
        const QcMobDefinition &spider = m_content->mobs()[type("spider")];
        const QcMobDefinition &cave = m_content->mobs()[type("cave_spider")];
        QCOMPARE(spider.abilities.climbs, QcMobAbilities::Climbing::Walls);
        QVERIFY(spider.neutralByDay && !cave.neutralByDay);
        QVERIFY(spider.immuneTo.contains(u"qtcraft:poison"_s));
        QCOMPARE(cave.blowEffects, (QList<QcStatusEffect>{{u"qtcraft:poison"_s, 1, 140}}));
        // A cave spider fits through a gap one block high, a spider does not fit through one block wide.
        QVERIFY(cave.height < 1.0 && cave.width < 1.0 && spider.width > 1.0);
        QVERIFY(QcPathRules::of(spider).climbsWalls && !QcPathRules::of(m_content->mobs()[type("zombie")]).climbsWalls);
    }

    void climbsUpAWallAndSlidesDownSlowly()
    {
        const QcContentRegistry content = qcMakeTestContent();
        QcTestWorld world(content);
        const QcBlockId stone = *content.blockId(u"test:stone"_s);
        world.fillBox({-4, 0, -3}, {6, 0, 3}, stone);
        world.fillBox({1, 1, -3}, {1, 5, 3}, stone);

        // Against the wall, pushing into it: up the wall and over the top.
        QcEntityMotion motion;
        motion.position = {0.35, 1.0, 0.5};
        for (int i = 0; i < 80 && motion.position.x < 1.5; ++i) {
            step(motion, {1.0, 0.0, 0.0}, false, world);
        }
        QVERIFY2(motion.position.x > 1.0 && motion.position.y >= 6.0 - 1e-6, qPrintable(u"%1 %2"_s.arg(motion.position.x).arg(motion.position.y)));

        // Without wall climbing the same body stays at the foot of the wall.
        QcEntityMotion walker;
        walker.position = {0.35, 1.0, 0.5};
        for (int i = 0; i < 80; ++i) {
            walker.step({.wish = {1.0, 0.0, 0.0}, .speed = 3.0}, {1.3, 0.9}, world, QcPhysics::tickDuration);
        }
        QVERIFY(walker.position.y < 2.0);

        // Hanging at the wall it slides down no faster than on a ladder.
        QcEntityMotion hanging;
        hanging.position = {0.35, 5.0, 0.5};
        int ticks = 0;
        while (!hanging.onGround && ticks < 200) {
            step(hanging, {}, false, world);
            QVERIFY(hanging.velocity.y >= -QcPhysics::climbDownSpeed - 1e-9);
            ++ticks;
        }
        QCOMPARE(hanging.position.y, 1.0);
        QVERIFY(ticks >= int(4.0 / QcPhysics::climbDownSpeed / QcPhysics::tickDuration) - 1);
    }

    void clingsBelowAndBesideAnOverhang()
    {
        const QcContentRegistry content = qcMakeTestContent();
        QcTestWorld world(content);
        const QcBlockId stone = *content.blockId(u"test:stone"_s);
        world.fillBox({1, 1, -3}, {1, 5, 3}, stone);
        world.fillBox({0, 5, -3}, {0, 5, 3}, stone);

        // A small body under the overhang holds on to it, beside its face too.
        const QcAabb under = QcPhysics::boxAt({0.5, 4.5, 0.5}, 0.7, 0.5);
        const QcAabb beside = QcPhysics::boxAt({-0.36, 4.2, 0.5}, 0.7, 0.5);
        const QcAabb away = QcPhysics::boxAt({-2.0, 4.2, 0.5}, 0.7, 0.5);
        QVERIFY(QcPhysics::clingsToWall(world, under));
        QVERIFY(QcPhysics::clingsToWall(world, beside));
        QVERIFY(!QcPhysics::clingsToWall(world, away));
    }

    void leavesYouAloneByDayButNotAtNight()
    {
        joinArena(noon);
        const quint32 spider = spawn("spider", 2, 0, 0);
        ticks(100);
        QCOMPARE(player().health, 20.0);
        QVERIFY(mob(spider)->calm);

        // A hit makes it hostile by day too.
        m_server->entities().creatures().damage({.creature = spider, .damage = 1.0, .from = player().motion.position, .attacker = player().id});
        ticks(1);
        QVERIFY(!QcMobSteering::calm(*mob(spider), m_content->mobs()[type("spider")], noon));
        ticks(60);
        QVERIFY(player().health < 20.0);

        joinArena(midnight);
        spawn("spider", 3, 0, 0);
        ticks(100);
        QVERIFY(player().health < 20.0);
    }

    // Damage without a player (a fall, fire, lava, lightning) makes no spider angry (decision E-451).
    void damageWithoutAPlayerProvokesNobody()
    {
        joinArena(noon);
        const quint32 spider = spawn("spider", 2, 0, 0);
        m_server->entities().creatures().damage({.creature = spider, .damage = 1.0, .from = mob(spider)->motion.position});
        ticks(60);
        QVERIFY(mob(spider));
        QCOMPARE(mob(spider)->provokedBy, 0u);
        QVERIFY(mob(spider)->calm);
        QCOMPARE(player().health, 20.0);
    }

    // By day a spider hurt by one player hunts exactly that player and leaves another one alone
    // standing right beside it (decision E-451).
    void aHitByDaySetsItOnThatPlayerOnly()
    {
        joinArena(noon);
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        QcServerClient &bystander = *m_server->entities().clients().back();
        QVERIFY(&bystander != &player());
        bystander.motion.position = QcVec3{m_feet.x + 3.5, double(m_feet.y), m_feet.z + 0.5};
        const QcVec3 far{m_feet.x - 8.5, double(m_feet.y), m_feet.z + 0.5};
        player().motion.position = far;
        const quint32 spider = spawn("spider", 2, 0, 0);
        ticks(40);
        QCOMPARE(bystander.health, 20.0);

        m_server->entities().creatures().damage({.creature = spider, .damage = 1.0, .from = far, .attacker = player().id});
        ticks(1);
        QCOMPARE(mob(spider)->provokedBy, player().id);
        QCOMPARE(mob(spider)->chasing, player().id);
        // It walks off to the attacker; the bystander stays unharmed although the spider began next to it.
        for (int i = 0; i < 200 && player().health >= 20.0; ++i) {
            bystander.motion.position = QcVec3{m_feet.x + 3.5, double(m_feet.y), m_feet.z + 0.5};
            ticks(1);
        }
        QVERIFY(player().health < 20.0);
        QCOMPARE(bystander.health, 20.0);
    }

    void climbsAWallToReachYou()
    {
        joinArena(midnight);
        // The player stands on a block tower four high, a wall of stone in front of the spider.
        for (int dy = 0; dy < 4; ++dy) {
            for (int dz = -3; dz <= 3; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), id("qtcraft:stone"));
                }
            }
        }
        player().motion.position.y = m_feet.y + 4.0;
        player().stateDirty = true;
        const quint32 spider = spawn("cave_spider", 6, 0, 0);
        double highest = 0.0;
        for (int i = 0; i < 400 && player().health >= 20.0; ++i) {
            ticks(1);
            highest = std::max(highest, mob(spider)->motion.position.y - m_feet.y);
        }
        QVERIFY2(highest >= 3.9, qPrintable(QString::number(highest)));
        QVERIFY(player().health < 20.0);
    }

    void caveSpidersPoison()
    {
        joinArena(noon);
        spawn("cave_spider", 1, 0, 0);
        for (int i = 0; i < 200 && !player().effects.find(u"qtcraft:poison"_s); ++i) {
            ticks(1);
        }
        const QcStatusEffect *poison = player().effects.find(u"qtcraft:poison"_s);
        QVERIFY(poison);
        QVERIFY(poison->ticksLeft > 100);
    }

    // Headless measurement (decision E-451), ticked by hand as fast as the server goes: creatures in
    // a ring 6-10 m around a player on a 3 x 3 stone tower four blocks high at night. Spiders climb
    // the tower and chase him; zombies, which cannot climb, gather at its foot (the comparison).
    // The player is healed each tick. Small in every run (8 each over 100 ticks);
    // QTCRAFT_SPIDER_MEASURE=full: 50 each over 400 ticks. Prints the mean tick and mobs phase.
    void climbingSpidersMeasure()
    {
        const bool full = qEnvironmentVariable("QTCRAFT_SPIDER_MEASURE") == u"full"_s;
        const int count = full ? 50 : 8;
        const int ticksToRun = full ? 400 : 100;
        for (const char *kind : {"spider", "zombie"}) {
            joinArena(midnight);
            for (int dy = 0; dy < 4; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        m_server->world().setBlock(m_feet.offset(dx, dy, dz), id("qtcraft:stone"));
                    }
                }
            }
            player().motion.position.y = m_feet.y + 4.0;
            player().stateDirty = true;
            QList<quint32> ids;
            for (int i = 0; i < count; ++i) {
                const double angle = 2.0 * std::numbers::pi * i / count;
                const double radius = 6.0 + 4.0 * (i % 3) / 2.0;
                const QcVec3 at{m_feet.x + 0.5 + radius * std::cos(angle), double(m_feet.y), m_feet.z + 0.5 + radius * std::sin(angle)};
                ids.append(m_server->entities().creatures().spawn(type(kind), at, m_server->entities().newId()));
            }

            double tickMs = 0.0;
            double mobsMs = 0.0;
            double maxTickMs = 0.0;
            for (int i = 0; i < ticksToRun; ++i) {
                player().health = QcServerClient::maxHealth;
                QElapsedTimer timer;
                timer.start();
                m_server->tick();
                const double ms = double(timer.nsecsElapsed()) / 1e6;
                tickMs += ms / ticksToRun;
                maxTickMs = std::max(maxTickMs, ms);
                mobsMs += double(m_server->tickPhases().lastTick()[std::size_t(QcTickPhase::Mobs)]) / 1e6 / ticksToRun;
            }
            const auto onTop = std::ranges::count_if(ids, [&](quint32 id) { return mob(id) && mob(id)->motion.position.y >= m_feet.y + 3.9; });
            if (qstrcmp(kind, "spider") == 0) {
                QVERIFY2(onTop >= 1, qPrintable(QString::number(onTop)));
            } else {
                QCOMPARE(onTop, 0);
            }
            qInfo("MEASURE climbing kind=%s count=%d ticks=%d on_top=%lld tick_mean_ms=%.4f tick_max_ms=%.4f mobs_mean_ms=%.4f", kind, count,
                  ticksToRun, qint64(onTop), tickMs, maxTickMs, mobsMs);
        }
    }
};

QTEST_MAIN(TestSpiders)
#include "test_spiders.moc"
