#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/core/QcServerWorld.h"
#include "server/items/QcItemEntityManager.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcMobNavigator.h"
#include "server/pathing/QcPathFinder.h"
#include "worldgen/generators/QcFlatGenerator.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = QcPhysics::tickDuration;
// The flat world's ground is grass at y 4; feet stand at y 5.
constexpr int ground = 5;

} // namespace

// Slippery blocks in the shared physics (decision E-430), ticked by hand: players, creatures and
// dropped items slide on ice and blue ice and stop at once on stone; a walker reaches its usual top
// speed on ice, only later; the value per tick is the original game's slipperiness times its drag;
// path searches go round ice where the way round is short and cross it otherwise, and a creature
// that walks to a point on ice comes to rest there instead of sliding to and fro.
class TestSlipperyGround : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcBlockId block(const QString &name) const
    {
        const std::optional<QcBlockId> id = m_content->stateId(u"qtcraft:"_s + name);
        if (!id) {
            QcTestUtil::failCase(QStringLiteral("unknown block %1").arg(name));
            return qcAirBlockId;
        }
        return *id;
    }

    // A floor of `name` at y 64 on stone, the start at (0.5, 65, 0.5).
    QcTestWorld floorWorld(const QString &name) const
    {
        QcTestWorld world(*m_content);
        world.fillBox({-40, 63, -40}, {40, 63, 40}, block(u"stone"_s));
        world.fillBox({-40, 64, -40}, {40, 64, 40}, block(name));
        return world;
    }

    struct Walk
    {
        double speed = 0.0;
        // How far the player slid after letting go of the key, until standing.
        double slide = 0.0;
    };

    // A player walking east for `ticks` ticks from a standstill, then letting go.
    static Walk walkAndLetGo(const QcBlockAccess &world, int ticks)
    {
        QcEntityMotion motion;
        motion.position = {0.5, 65.0, 0.5};
        motion.onGround = true;
        QcPlayerInput east;
        east.yaw = 90.0f;
        east.forward = 1.0f;
        for (int tick = 0; tick < ticks; ++tick) {
            motion.stepPlayer(east, world, dt, false);
        }

        Walk walk{motion.velocity.x, 0.0};
        const double released = motion.position.x;
        for (int tick = 0; tick < 200 && motion.velocity.length() > 1e-6; ++tick) {
            motion.stepPlayer({}, world, dt, false);
        }
        walk.slide = motion.position.x - released;
        return walk;
    }

    std::unique_ptr<QcServerWorld> flatWorld() const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,grass_block"_s));
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    void fill(QcServerWorld &world, QcBlockPos from, QcBlockPos to, const QString &name) const
    {
        for (int x = from.x; x <= to.x; ++x) {
            for (int y = from.y; y <= to.y; ++y) {
                for (int z = from.z; z <= to.z; ++z) {
                    world.setBlock({x, y, z}, block(name));
                }
            }
        }
    }

    std::unique_ptr<QcMobManager> makeMobs() const
    {
        auto mobs = std::make_unique<QcMobManager>(m_content, 1);
        mobs->setNaturalSpawning(false);
        return mobs;
    }

    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }

    void run(QcMobManager &mobs, QcServerWorld &world, int ticks) const
    {
        quint32 next = 1000;
        for (int tick = 0; tick < ticks; ++tick) {
            mobs.tick(world, {}, QcDayCycle::dayLength * 0.25, dt, [&next] { return next++; });
        }
    }

    // How far a pig pushed east at 4 m/s slides on a floor of `name` (the ground layer of the flat
    // world), without a will of its own.
    double pigSlide(const QString &name) const
    {
        auto world = flatWorld();
        fill(*world, {-20, ground - 1, -3}, {20, ground - 1, 3}, name);
        auto mobs = makeMobs();
        mobs->spawn(type("pig"), {0.5, double(ground), 0.5}, 1);
        QcCreature &pig = mobs->editableMobs().first();
        pig.wanderTimer = 1e9;
        pig.motion.onGround = true;
        pig.motion.velocity = {4.0, 0.0, 0.0};
        run(*mobs, *world, 100);
        return mobs->find(1)->motion.position.x - 0.5;
    }

    // How far a stack thrown east at 3 m/s along a floor of `name` slides.
    double itemSlide(const QString &name) const
    {
        const QcTestWorld world = floorWorld(name);
        QcItemEntityManager items(m_content, 1);
        QcItemEntityManager::Drop drop;
        drop.stack = {*m_content->itemId(u"qtcraft:cobblestone"_s), 1, 0};
        drop.position = {0.5, 65.0, 0.5};
        drop.velocity = {3.0, 0.0, 0.0};
        quint32 next = 1;
        const quint32 id = items.spawn(drop, next++);
        items.editableItems().first().motion.onGround = true;
        const QcItemEntityManager::TickCallbacks callbacks{[](const QcVec3 &) { return true; }, [](quint32, QcItemStack stack) { return stack.count; },
                                                           [&next] { return next++; }};
        for (int tick = 0; tick < 200; ++tick) {
            items.tick(world, {}, dt, callbacks);
        }
        const auto item = std::ranges::find(items.items(), id, &QcItemEntity::id);
        return item->motion.position.x - 0.5;
    }

    QcPathResult findPath(const QcTestWorld &world, QcBlockPos start, QcBlockPos goal) const
    {
        QcPathCellTable cells(*m_content);
        QcPathFinder finder;
        QcPathRules rules;
        rules.height = 1.95;
        return finder.find(world, cells, rules, QcPathRequest{start, goal, 0, 4096, 256});
    }

private slots:
    void slipperinessComesFromTheBlocks()
    {
        QCOMPARE(m_content->block(block(u"stone"_s)).slipperiness, 0.6);
        QCOMPARE(m_content->block(block(u"ice"_s)).slipperiness, 0.98);
        QCOMPARE(m_content->block(block(u"packed_ice"_s)).slipperiness, 0.98);
        QCOMPARE(m_content->block(block(u"blue_ice"_s)).slipperiness, 0.989);

        // The share kept per tick: none on ground that grips, slipperiness x drag on ice, the whole
        // slipperiness for a cart off the rails (also on stone), the same per second at other steps.
        QCOMPARE(QcPhysics::slideKeep(0.6, QcPhysics::footDrag, dt), 0.0);
        QCOMPARE(QcPhysics::slideKeep(0.6, QcPhysics::itemDrag, dt), 0.0);
        QVERIFY(std::abs(QcPhysics::slideKeep(0.98, QcPhysics::footDrag, dt) - 0.98 * 0.91) < 1e-12);
        QVERIFY(std::abs(QcPhysics::slideKeep(0.6, QcPhysics::coastDrag, dt) - 0.6) < 1e-12);
        QVERIFY(std::abs(QcPhysics::slideKeep(0.98, QcPhysics::coastDrag, dt * 2.0) - 0.98 * 0.98) < 1e-12);
    }

    void playersSlideOnIceAndStopAtOnceOnStone()
    {
        const QcTestWorld stone = floorWorld(u"stone"_s);
        const QcTestWorld ice = floorWorld(u"ice"_s);
        const QcTestWorld blue = floorWorld(u"blue_ice"_s);

        // On stone the walk takes its speed at once and stops at once.
        const Walk onStone = walkAndLetGo(stone, 40);
        QVERIFY(std::abs(onStone.speed - QcPhysics::walkSpeed) < 1e-9);
        QVERIFY2(onStone.slide < 1e-9, qPrintable(QString::number(onStone.slide)));

        // On ice the speed builds up slowly (89 % kept per tick), the top speed is the same, and the
        // player slides on: v k / (1 - k) x dt from the speed when letting go, 1.77 m from the top
        // speed.
        const Walk early = walkAndLetGo(ice, 5);
        QVERIFY2(early.speed < QcPhysics::walkSpeed * 0.5, qPrintable(QString::number(early.speed)));
        const Walk onIce = walkAndLetGo(ice, 100);
        QVERIFY2(std::abs(onIce.speed - QcPhysics::walkSpeed) < 0.01, qPrintable(QString::number(onIce.speed)));
        const double keep = 0.98 * QcPhysics::footDrag;
        const double expected = onIce.speed * keep / (1.0 - keep) * dt;
        QVERIFY2(std::abs(onIce.slide - expected) < 0.01, qPrintable(u"%1 vs %2"_s.arg(onIce.slide).arg(expected)));
        QVERIFY(onIce.slide > 1.7);

        // Blue ice is a little more slippery still.
        const Walk onBlue = walkAndLetGo(blue, 100);
        QVERIFY2(onBlue.slide > onIce.slide + 0.1, qPrintable(u"%1 vs %2"_s.arg(onBlue.slide).arg(onIce.slide)));
    }

    void creaturesAndItemsSlideOnIce()
    {
        const double pigOnGrass = pigSlide(u"grass_block"_s);
        const double pigOnIce = pigSlide(u"packed_ice"_s);
        QVERIFY2(pigOnGrass < 0.05, qPrintable(QString::number(pigOnGrass)));
        // 4 m/s x 0.89 / 0.11 x 0.05 s = 1.65 m.
        QVERIFY2(pigOnIce > 1.5 && pigOnIce < 1.8, qPrintable(QString::number(pigOnIce)));

        // Items keep 96 % per tick on ice: 3 m/s x 0.96 / 0.04 x 0.05 s = 3.6 m.
        const double itemOnStone = itemSlide(u"stone"_s);
        const double itemOnIce = itemSlide(u"ice"_s);
        QVERIFY2(itemOnStone < 0.05, qPrintable(QString::number(itemOnStone)));
        QVERIFY2(itemOnIce > 3.3 && itemOnIce < 3.7, qPrintable(QString::number(itemOnIce)));
    }

    void pathsGoRoundIceWhereTheWayRoundIsShort()
    {
        QcTestWorld world(*m_content);
        world.fillBox({-30, 0, -30}, {30, 0, 30}, block(u"stone"_s));
        // A strip of ice three wide across the way, ending two cells beside the straight line.
        world.fillBox({4, 0, -30}, {6, 0, 2}, block(u"ice"_s));
        const QcPathResult round = findPath(world, {0, 1, 0}, {10, 1, 0});
        QVERIFY(round.reachesGoal);
        QVERIFY(std::ranges::none_of(round.cells, [&](QcBlockPos cell) { return world.blockAt(cell.offset(0, -1, 0)) == block(u"ice"_s); }));

        // Ice all the way across: the path crosses it straight.
        world.fillBox({4, 0, 2}, {6, 0, 30}, block(u"ice"_s));
        const QcPathResult across = findPath(world, {0, 1, 0}, {10, 1, 0});
        QVERIFY(across.reachesGoal);
        QCOMPARE(across.cells.size(), std::size_t(10));
    }

    void aCreatureWalkingOntoIceComesToRest()
    {
        auto world = flatWorld();
        fill(*world, {-2, ground - 1, -8}, {16, ground - 1, 8}, u"ice"_s);
        auto mobs = makeMobs();
        mobs->spawn(type("pig"), {0.5, double(ground), 0.5}, 1);
        QcCreature &pig = mobs->editableMobs().first();
        pig.wanderTimer = 1e9;
        const QcVec3 goal{7.5, double(ground), 0.5};
        QcMobNavigator::setGoal(pig, QcMobPath::Purpose::Wander, goal, 0);

        // It gets there without sliding past the goal on the way, then stays: no sliding past the
        // goal and back again.
        double overshoot = 0.0;
        for (int tick = 0; tick < 20 * 15; ++tick) {
            run(*mobs, *world, 1);
            overshoot = std::max(overshoot, mobs->find(1)->motion.position.x - goal.x);
        }
        QVERIFY2(overshoot < 0.5, qPrintable(QString::number(overshoot)));
        const QcVec3 there = mobs->find(1)->motion.position;
        double fastest = 0.0;
        double farthest = 0.0;
        for (int tick = 0; tick < 40; ++tick) {
            run(*mobs, *world, 1);
            fastest = std::max(fastest, mobs->find(1)->motion.velocity.length());
            farthest = std::max(farthest, (mobs->find(1)->motion.position - there).length());
        }
        QVERIFY2((there - goal).length() < 1.0, qPrintable(u"%1, %2"_s.arg(there.x).arg(there.z)));
        QVERIFY2(fastest < 0.2, qPrintable(QString::number(fastest)));
        QVERIFY2(farthest < 0.3, qPrintable(QString::number(farthest)));
    }
};

QTEST_GUILESS_MAIN(TestSlipperyGround)
#include "test_slippery_ground.moc"
