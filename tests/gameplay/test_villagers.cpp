#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/mobs/QcMobSteering.h"
#include "server/villages/QcServerVillagers.h"
#include "server/villages/QcVillagerHomes.h"
#include "server/weather/QcWeatherCycle.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <algorithm>
#include <limits>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

// The test terrain with one village (id 77) around the spawn: no pieces, only its area, so the tests
// build the beds, workstations, doors and fields themselves.
class VillageTerrain : public QcTestTerrainGenerator
{
public:
    explicit VillageTerrain(std::shared_ptr<const QcContentRegistry> content)
        : QcTestTerrainGenerator(std::move(content))
    {
        auto plan = std::make_shared<QcVillagePlan>();
        const QcBlockPos spawn = spawnPoint();
        plan->id = 77;
        plan->style = u"qtcraft:plains"_s;
        plan->layout = u"cross"_s;
        plan->centre = spawn;
        plan->minX = spawn.x - 24;
        plan->maxX = spawn.x + 24;
        plan->minZ = spawn.z - 24;
        plan->maxZ = spawn.z + 24;
        m_plan = std::move(plan);
    }

    std::shared_ptr<const QcVillagePlan> villageFor(QcChunkPos pos) const override
    {
        Q_UNUSED(pos);
        return m_plan;
    }
    // The plan, for the tests to add fields to.
    QcVillagePlan &plan() { return *m_plan; }

    std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const override
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return m_plan;
    }

private:
    std::shared_ptr<QcVillagePlan> m_plan;
};

} // namespace

// Villagers in a village (decision E-411): professions from workstations, the routine by the time of
// day, sleeping in their own beds, doors, fleeing, farming and saving, on a server with a village
// built by the test.
class TestVillagers : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcVillage *village() { return m_server->world().villages().find(77); }

    // The terrain of the server start() started (owned by the server).
    VillageTerrain *m_terrain = nullptr;
    // The world folder of a test that saves, kept until the next one replaces it: the test restarts
    // the server in it (cleanup() in between), and Windows refuses to delete files the server of
    // the test still holds open when the test function returns.
    std::optional<QTemporaryDir> m_directory;

    int villagerType() const { return *m_content->mobIndex(u"qtcraft:villager"_s); }

    QcBlockId state(const QString &name) const
    {
        const std::optional<QcBlockId> found = m_content->stateId(name);
        if (!found) {
            QcTestUtil::failCase(QStringLiteral("Test uses the state %1, which the content does not have").arg(name));
            return qcAirBlockId;
        }
        return *found;
    }

    // A server with the village, the time standing still at `phase` of the day, no creatures
    // spawning by themselves and a stone yard of `radius` around the player with room above it.
    void start(double phase, const QcServerConfig &config = {}, int radius = 12)
    {
        auto terrain = std::make_unique<VillageTerrain>(m_content);
        m_terrain = terrain.get();
        join(config, std::move(terrain));
        chat(u"/gamerule daylightCycle false"_s);
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule weatherCycle false"_s);
        loadAround(m_feet, 2);
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 4; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        setPhase(phase);
    }

    void setPhase(double phase) { m_server->setWorldTime(QcDayCycle::dayLength * (10.0 + phase)); }

    // A bed lying north (head at `foot` - z); returns the head.
    QcBlockPos bed(QcBlockPos foot)
    {
        m_server->world().setBlock(foot, state(u"qtcraft:bed[facing=north,part=foot]"_s));
        m_server->world().setBlock(foot.offset(0, 0, -1), state(u"qtcraft:bed[facing=north,part=head]"_s));
        return foot.offset(0, 0, -1);
    }

    // A villager of the village standing at `at`, living in the bed with its head at `head`.
    QcCreature &settle(QcBlockPos head, const QcVec3 &at, quint32 look = 7)
    {
        const quint32 entity = m_server->entities().newId();
        m_server->mobs().spawn(villagerType(), at, entity);
        QcCreature *mob = m_server->mobs().find(entity);
        *mob->villager = QcVillagerHomes::settle(*village(), head, mob->uid, (*m_content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)), look);
        return *mob;
    }

    QcCreature *villager(quint32 entity) { return m_server->mobs().find(entity); }

    // Ticks until `done` holds, at most `limit` ticks; whether it came to hold.
    template<typename Done>
    bool tickUntil(Done done, int limit)
    {
        for (int tick = 0; tick < limit; ++tick) {
            ticks(1);
            if (done()) {
                return true;
            }
        }
        return false;
    }

    static QcVec3 centreOf(QcBlockPos cell) { return {cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5}; }

    static double horizontalDistance(const QcVec3 &a, const QcVec3 &b) { return std::hypot(a.x - b.x, a.z - b.z); }

    bool isOpen(QcBlockPos door) const
    {
        return m_content->property(*m_server->world().blockAt(door), u"open"_s) == u"true"_s;
    }

    // A stone wall along z = `z` across the yard, three high, with the door `name` at x = m_feet.x.
    QcBlockPos wallWithDoor(int z, const QString &name)
    {
        for (int dx = -12; dx <= 12; ++dx) {
            for (int dy = 0; dy <= 2; ++dy) {
                m_server->world().setBlock({m_feet.x + dx, m_feet.y + dy, z}, id("qtcraft:stone"));
            }
        }
        const QcBlockPos door{m_feet.x, m_feet.y, z};
        m_server->world().setBlock(door, state(name + u"[facing=north,half=bottom,open=false,hinge=left]"_s));
        m_server->world().setBlock(door.offset(0, 1, 0), state(name + u"[facing=north,half=top,open=false,hinge=left]"_s));
        return door;
    }

    int ripeCrops(const QList<QcBlockPos> &crops) const
    {
        return static_cast<int>(std::ranges::count_if(crops, [this](QcBlockPos pos) {
            return m_content->property(*m_server->world().blockAt(pos), u"age"_s) == u"7"_s;
        }));
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aVillagerTakesTheProfessionOfAFreeWorkstation()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        const QcBlockPos composter = m_feet.offset(-5, 0, 4);
        m_server->world().setBlock(composter, id("qtcraft:composter"));
        const quint32 first = settle(head, centreOf(m_feet.offset(5, 0, 0))).id;
        const quint32 second = settle(head, centreOf(m_feet.offset(5, 0, -1))).id;
        QCOMPARE(villager(first)->villager->profession, u"qtcraft:unemployed"_s);
        QCOMPARE(village()->population, 2);
        ticks(30);

        // One workstation, one farmer; the bed belongs to the first, the second finds none.
        const quint64 farmer = villager(first)->villager->profession == u"qtcraft:farmer"_s ? villager(first)->uid : villager(second)->uid;
        QCOMPARE(village()->workstations.size(), 1);
        QCOMPARE(village()->workstations.front().claimedBy, farmer);
        QCOMPARE(int(std::ranges::count_if(m_server->mobs().mobs(), [](const QcCreature &mob) {
                     return mob.isVillager() && mob.villager->profession == u"qtcraft:farmer"_s;
                 })),
                 1);
        QCOMPARE(village()->beds.front().claimedBy, villager(first)->uid);
        QVERIFY(!villager(second)->villager->home);

        // Clients get the farmer's look: the style's outfit with the apron, the straw hat.
        const quint32 farmerId = villager(first)->uid == farmer ? first : second;
        ticks(2);
        QVERIFY(m_client->spawns.contains(farmerId));
        QCOMPARE(m_client->spawns[farmerId].appearance.outfit, u"qtcraft:villager_tunic@qtcraft:farmer"_s);
        QCOMPARE(m_client->spawns[farmerId].appearance.hair, u"qtcraft:villager_straw_hat"_s);

        // Without the workstation and without experience it is out of work again.
        m_server->world().setBlock(composter, qcAirBlockId);
        ticks(25);
        QCOMPARE(villager(farmerId)->villager->profession, u"qtcraft:unemployed"_s);
        QVERIFY(!villager(farmerId)->villager->work);
    }

    void theRoutineFollowsTheTimeOfDay()
    {
        start(0.02);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        m_server->world().setBlock(m_feet.offset(-5, 0, 4), id("qtcraft:furnace"));
        const quint32 entity = settle(head, centreOf(m_feet.offset(5, 0, 0))).id;
        const QList<std::pair<double, QcVillagerActivity>> steps{
            {0.02, QcVillagerActivity::Wander}, {0.1, QcVillagerActivity::Work},  {0.28, QcVillagerActivity::Meet},
            {0.35, QcVillagerActivity::Work},   {0.45, QcVillagerActivity::Wander}, {0.5, QcVillagerActivity::Home},
            {0.7, QcVillagerActivity::Sleep},   {0.99, QcVillagerActivity::Home}};
        for (const auto &[phase, activity] : steps) {
            setPhase(phase);
            ticks(QcVillagerBehaviour::decideTicks + 1);
            QCOMPARE(villager(entity)->villager->activity, activity);
        }
        QCOMPARE(villager(entity)->villager->profession, u"qtcraft:smith"_s);

        // Rain in the village sends it home in the middle of the working day.
        setPhase(0.1);
        chat(u"/weather rain"_s);
        ticks(QcWeatherCycle::fadeTicks + QcVillagerBehaviour::decideTicks + 1);
        QCOMPARE(villager(entity)->villager->activity, QcVillagerActivity::Home);
    }

    void villagersSleepInTheirOwnBedAtNight()
    {
        start(0.7);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        bed(m_feet.offset(-4, 0, 0));
        const quint32 entity = settle(head, centreOf(m_feet.offset(-2, 0, 2))).id;
        QVERIFY(tickUntil([&] { return villager(entity)->pose == QcPose::Lying; }, 1200));

        // In its own bed, on top of it, and the clients see it lying.
        const QcCreature &sleeper = *villager(entity);
        QVERIFY(horizontalDistance(sleeper.motion.position, centreOf(m_feet.offset(4, 0, 0))) < 0.5);
        QVERIFY(sleeper.motion.position.y > m_feet.y + 0.4 && sleeper.motion.position.y < m_feet.y + 0.7);
        // Its box lies in the bed, head end on the pillow, no higher than half a body's width.
        const std::optional<QcAabb> box = m_server->entities().boxOf(sleeper);
        QVERIFY(box.has_value());
        const QcBlockPos foot = m_feet.offset(4, 0, 0);
        constexpr double slack = 0.1;
        QVERIFY2(box->min.x >= foot.x - slack && box->max.x <= foot.x + 1 + slack && box->min.z >= head.z - slack
                     && box->max.z <= foot.z + 1 + slack,
                 qPrintable(u"%1 %2 / %3 %4"_s.arg(box->min.x).arg(box->min.z).arg(box->max.x).arg(box->max.z)));
        QVERIFY(box->max.z - box->min.z > 1.8);
        QVERIFY(box->max.y - box->min.y <= 0.31);
        ticks(2);
        QVERIFY((m_client->flags[entity] & QcMsg::Lying) != 0);

        // It stays there, gets up when hurt and lies down again.
        ticks(40);
        QVERIFY(villager(entity)->pose == QcPose::Lying);
        QVERIFY(m_server->mobs().damage({.creature = entity, .damage = 1.0, .from = villager(entity)->motion.position + QcVec3{2.0, 0.0, 0.0}}) == std::nullopt);
        ticks(1);
        QVERIFY(villager(entity)->pose != QcPose::Lying);
        QVERIFY(tickUntil([&] { return villager(entity)->pose == QcPose::Lying; }, 1200));

        // In the morning it gets up.
        setPhase(0.01);
        ticks(2);
        QVERIFY(villager(entity)->pose != QcPose::Lying);
    }

    // A hit wakes a sleeper for the push alone, whatever the game rule mobInvulnerabilityTicks
    // (decision E-429): with 0 it still wakes, with 200 it lies down again long before the window ends.
    void aHitWakesASleeperWhateverTheRule()
    {
        start(0.7);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        const quint32 entity = settle(head, centreOf(m_feet.offset(-2, 0, 2))).id;
        QVERIFY(tickUntil([&] { return villager(entity)->pose == QcPose::Lying; }, 1200));

        for (const int rule : {0, 200}) {
            chat(u"/gamerule mobInvulnerabilityTicks %1"_s.arg(rule));
            ticks(1);
            QVERIFY(villager(entity)->pose == QcPose::Lying);
            QVERIFY(m_server->mobs().damage({.creature = entity, .damage = 1.0, .from = villager(entity)->motion.position + QcVec3{2.0, 0.0, 0.0}}) == std::nullopt);
            ticks(1);
            QVERIFY2(villager(entity)->pose != QcPose::Lying, qPrintable(u"rule %1"_s.arg(rule)));
            QVERIFY(tickUntil([&] { return villager(entity)->pose == QcPose::Lying; }, 150));
            QVERIFY(m_server->mobs().find(entity)->hurt.active() == (rule == 200));
        }
    }

    // A sleeper keeps an eye out (decision E-470): a zombie beside its bed wakes it in a panic, and
    // it notes the panic (a golem summon may follow).
    void aZombieWakesASleeperInAPanic()
    {
        start(0.7);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        const quint32 entity = settle(head, centreOf(m_feet.offset(-2, 0, 2))).id;
        QVERIFY(tickUntil([&] { return villager(entity)->pose == QcPose::Lying; }, 1200));
        QVERIFY(villager(entity)->villager->sleptAt >= 0.0);
        QCOMPARE(villager(entity)->villager->panickedAt, -1.0);

        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:zombie"_s), villager(entity)->motion.position + QcVec3{0.0, 0.0, 3.0},
                               m_server->entities().newId());
        QVERIFY(tickUntil([&] { return villager(entity)->pose != QcPose::Lying; }, 2 * QcVillagerBehaviour::decideTicks + 1));
        QVERIFY(villager(entity)->villager->panickedAt >= 0.0);
        QVERIFY(villager(entity)->villager->threat.has_value());
    }

    // Thirty villagers meeting at the bell are a crowd that thinks in turns (decision E-401); they
    // still follow the routine when the meeting ends.
    void aCrowdAtTheBellStillFollowsTheRoutine()
    {
        start(0.28);
        // Bodies may crowd without harm here.
        chat(u"/gamerule maxEntityCramming 0"_s);
        QList<quint32> crowd;
        for (int i = 0; i < 30; ++i) {
            crowd.append(settle(m_feet.offset(30, 0, 30), centreOf(m_feet.offset(i % 3 - 1, 0, i / 3 % 3 - 1))).id);
        }
        ticks(100);
        QVERIFY(std::ranges::all_of(crowd, [this](quint32 entity) { return villager(entity)->villager->activity == QcVillagerActivity::Meet; }));

        setPhase(0.7);
        QVERIFY(tickUntil([&] {
            return std::ranges::all_of(crowd, [this](quint32 entity) { return villager(entity)->villager->activity == QcVillagerActivity::Sleep; });
        }, 400));
    }

    void villagersOpenWoodenDoorsAndCloseThemBehind()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 4));
        const QcBlockPos door = wallWithDoor(m_feet.z - 3, u"qtcraft:oak_door"_s);
        const QcBlockPos furnace = m_feet.offset(0, 0, -9);
        m_server->world().setBlock(furnace, id("qtcraft:furnace"));
        const quint32 entity = settle(head, centreOf(m_feet.offset(2, 0, 3))).id;

        // Through the door to the furnace, the door shut behind it.
        bool opened = false;
        const bool arrived = tickUntil([&] {
            opened = opened || isOpen(door);
            return opened && !isOpen(door) && horizontalDistance(villager(entity)->motion.position, centreOf(furnace)) <= 2.0;
        }, 600);
        QVERIFY(opened);
        QVERIFY(arrived);
        QVERIFY(horizontalDistance(villager(entity)->motion.position, centreOf(furnace)) <= 2.0);
        QVERIFY(!isOpen(door));
        QVERIFY(!isOpen(door.offset(0, 1, 0)));
    }

    void aDoorShutAgainIsOpenedAgain()
    {
        start(0.1);
        const QcBlockPos door = wallWithDoor(m_feet.z - 3, u"qtcraft:oak_door"_s);
        m_server->world().setBlock(m_feet.offset(-1, 0, -9), id("qtcraft:furnace"));
        m_server->world().setBlock(m_feet.offset(1, 0, -9), id("qtcraft:furnace"));
        const quint32 first = settle(bed(m_feet.offset(4, 0, 4)), centreOf(m_feet.offset(2, 0, 3))).id;
        const quint32 second = settle(bed(m_feet.offset(-4, 0, 9)), centreOf(m_feet.offset(-2, 0, 10))).id;

        // Two villagers, one after the other, through one door to their workstations; someone
        // shuts the door each time it was opened (another villager or a player), three times,
        // before both are through.
        const auto through = [&](quint32 entity) { return villager(entity)->motion.position.z < door.z - 1.0; };
        int shut = 0;
        const bool arrived = tickUntil([&] {
            if (shut < 3 && isOpen(door) && !through(first) && !through(second)) {
                m_server->world().setBlock(door, state(u"qtcraft:oak_door[facing=north,half=bottom,open=false,hinge=left]"_s));
                m_server->world().setBlock(door.offset(0, 1, 0), state(u"qtcraft:oak_door[facing=north,half=top,open=false,hinge=left]"_s));
                ++shut;
            }
            return through(first) && through(second);
        }, 1200);
        QCOMPARE(shut, 3);
        QVERIFY(arrived);
    }

    // Two villagers reaching one door at the same moment both get through, one after the other.
    void twoVillagersPassOneDoorAtOnce()
    {
        start(0.1);
        wallWithDoor(m_feet.z - 3, u"qtcraft:oak_door"_s);
        m_server->world().setBlock(m_feet.offset(1, 0, -9), id("qtcraft:furnace"));
        m_server->world().setBlock(m_feet.offset(-1, 0, -9), id("qtcraft:furnace"));
        const quint32 first = settle(bed(m_feet.offset(4, 0, 6)), centreOf(m_feet.offset(-1, 0, 0))).id;
        const quint32 second = settle(bed(m_feet.offset(-4, 0, 6)), centreOf(m_feet.offset(1, 0, 0))).id;

        const auto through = [&](quint32 entity) { return villager(entity)->motion.position.z < m_feet.z - 4.0; };
        const bool arrived = tickUntil([&] { return through(first) && through(second); }, 600);
        QVERIFY2(arrived, qPrintable(u"%1 %2 / %3 %4"_s.arg(villager(first)->motion.position.x).arg(villager(first)->motion.position.z)
                                         .arg(villager(second)->motion.position.x).arg(villager(second)->motion.position.z)));
    }

    // Two villagers meeting at one door from both sides both get through.
    void twoVillagersPassOneDoorFromBothSides()
    {
        start(0.1);
        wallWithDoor(m_feet.z - 3, u"qtcraft:oak_door"_s);
        m_server->world().setBlock(m_feet.offset(1, 0, -9), id("qtcraft:furnace"));
        m_server->world().setBlock(m_feet.offset(-1, 0, 4), id("qtcraft:furnace"));
        // Each lives beyond the door and works near home.
        const quint32 north = settle(bed(m_feet.offset(4, 0, -7)), centreOf(m_feet.offset(0, 0, 0))).id;
        const quint32 south = settle(bed(m_feet.offset(-4, 0, 6)), centreOf(m_feet.offset(0, 0, -6))).id;

        const bool arrived = tickUntil([&] {
            return villager(north)->motion.position.z < m_feet.z - 4.0 && villager(south)->motion.position.z > m_feet.z - 1.0;
        }, 800);
        QVERIFY2(arrived, qPrintable(u"%1 %2 / %3 %4"_s.arg(villager(north)->motion.position.x).arg(villager(north)->motion.position.z)
                                         .arg(villager(south)->motion.position.x).arg(villager(south)->motion.position.z)));
    }

    // A villager in a pool one block deep, its bank level with the water, climbs out and goes to bed.
    void aVillagerClimbsOutOfAPoolOntoItsBank()
    {
        start(0.7);
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -2, dz), id("qtcraft:stone"));
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:water"));
            }
        }
        const QcBlockPos head = bed(m_feet.offset(8, 0, 0));
        const quint32 entity = settle(head, centreOf(m_feet.offset(0, -1, 0))).id;
        QVERIFY(tickUntil([&] { return villager(entity)->pose == QcPose::Lying; }, 600));
    }

    void aVillagerWalksFromADirtPathThroughADoorWithoutJumping()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 4));
        const QcBlockPos door = wallWithDoor(m_feet.z - 3, u"qtcraft:oak_door"_s);
        m_server->world().setBlock(m_feet.offset(0, 0, -9), id("qtcraft:furnace"));
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -8; dz <= 8; ++dz) {
                if (m_feet.z + dz != door.z) {
                    m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:dirt_path"));
                }
            }
        }
        const quint32 entity = settle(head, centreOf(m_feet.offset(2, 0, 3))).id;

        // From the path (a sixteenth lower) onto the stone in the doorway it takes the low step
        // under the lintel: no jumping at the door frame, in the air or on landing.
        int jumps = 0;
        const bool through = tickUntil([&] {
            const QcCreature &mob = *villager(entity);
            jumps += mob.motion.velocity.y > 0.1 ? 1 : 0;
            return mob.motion.position.z < door.z - 1.0;
        }, 600);
        QVERIFY(through);
        QCOMPARE(jumps, 0);
    }

    // A villager coming at its door steeply from the side, starting off the line, keeps clear of the
    // open leaf (decision E-905): the leaf is 3/16 thick, the middle of the opening leaves a body 0.6
    // wide about 10.6 cm on each side.
    void aVillagerKeepsClearOfTheOpenLeaf()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 4));
        const QcBlockPos door = wallWithDoor(m_feet.z - 3, u"qtcraft:oak_door"_s);
        m_server->world().setBlock(m_feet.offset(0, 0, -9), id("qtcraft:furnace"));
        const quint32 entity = settle(head, QcVec3{door.x + 3.3, static_cast<double>(m_feet.y), door.z + 1.6}).id;

        double clearance = std::numeric_limits<double>::max();
        int jumps = 0;
        const bool through = tickUntil([&] {
            const QcCreature &mob = *villager(entity);
            jumps += mob.motion.velocity.y > 0.1 ? 1 : 0;
            const QcAabb body = QcPhysics::boxAt(mob.motion.position, 0.6, 1.95);
            if (isOpen(door) && body.max.z > door.z && body.min.z < door.z + 1) {
                const QcAabb leaf = m_content->state(*m_server->world().blockAt(door)).collision.first();
                clearance = std::min(clearance, std::max(door.x + leaf.min.x - body.max.x, body.min.x - (door.x + leaf.max.x)));
            }
            return mob.motion.position.z < door.z - 1.0;
        }, 600);
        QVERIFY(through);
        QCOMPARE(jumps, 0);
        QVERIFY2(clearance >= 0.08, qPrintable(QString::number(clearance)));
    }

    void aDoorIsClosedWhenItsVillagerDies()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 4));
        const QcBlockPos door = wallWithDoor(m_feet.z - 3, u"qtcraft:oak_door"_s);
        m_server->world().setBlock(m_feet.offset(0, 0, -9), id("qtcraft:furnace"));
        const quint32 entity = settle(head, centreOf(m_feet.offset(2, 0, 3))).id;
        QVERIFY(tickUntil([&] { return isOpen(door); }, 400));
        QVERIFY(m_server->mobs().wound(entity, 100.0).has_value());
        ticks(2);
        QVERIFY(!isOpen(door));
    }

    void ironDoorsStayShut()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 4));
        const QcBlockPos door = wallWithDoor(m_feet.z - 3, u"qtcraft:iron_door"_s);
        m_server->world().setBlock(m_feet.offset(0, 0, -9), id("qtcraft:furnace"));
        const quint32 entity = settle(head, centreOf(m_feet.offset(2, 0, 3))).id;
        for (int tick = 0; tick < 400; ++tick) {
            ticks(1);
            QVERIFY(!isOpen(door));
        }
        QVERIFY(villager(entity)->motion.position.z > door.z + 1.0);
    }

    void villagersRunHomeFromHostileCreatures()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(-10, 0, 0));
        const QcBlockPos furnace = m_feet.offset(10, 0, 0);
        m_server->world().setBlock(furnace, id("qtcraft:furnace"));
        const quint32 entity = settle(head, centreOf(m_feet.offset(8, 0, 1))).id;
        ticks(60);
        const double before = horizontalDistance(villager(entity)->motion.position, centreOf(head));
        QVERIFY(before > 12.0);

        // Underground creatures stay in daylight.
        m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:zombie"_s), villager(entity)->motion.position + QcVec3{3.0, 0.0, 0.0},
                               m_server->entities().newId(), true);
        ticks(60);
        QVERIFY(villager(entity)->villager->threat.has_value());
        QVERIFY(horizontalDistance(villager(entity)->motion.position, centreOf(head)) < before - 4.0);
    }

    void farmersHarvestAndReplantWithinTheBudget()
    {
        QcServerConfig config;
        config.limits.villagerHarvestsPerTick = 1;
        config.limitsFromServer = true;
        start(0.1, config);
        const QcBlockPos head = bed(m_feet.offset(-6, 0, -6));
        const QcBlockPos composter = m_feet.offset(0, 0, 4);
        m_server->world().setBlock(composter, id("qtcraft:composter"));
        QList<QcBlockPos> crops;
        for (int i = 0; i < 8; ++i) {
            const QcBlockPos crop = m_feet.offset(-4 + i, 0, 6 + (i % 2));
            m_server->world().setBlock(crop.offset(0, -1, 0), id("qtcraft:farmland"));
            m_server->world().setBlock(crop, state(u"qtcraft:wheat_crop[age=7]"_s));
            crops.append(crop);
            m_terrain->plan().fields.insert(crop);
        }
        // A player's field near the workstation is tended too (decision E-490); a crop beyond
        // cropRadius of the workstation is not.
        const QcBlockPos playerCrop = m_feet.offset(1, 0, 3);
        m_server->world().setBlock(playerCrop.offset(0, -1, 0), id("qtcraft:farmland"));
        m_server->world().setBlock(playerCrop, state(u"qtcraft:wheat_crop[age=7]"_s));
        crops.append(playerCrop);
        const QcBlockPos farCrop = composter.offset(-(QcVillagerBehaviour::cropRadius + 2), 0, 0);
        m_server->world().setBlock(farCrop.offset(0, -1, 0), id("qtcraft:farmland"));
        m_server->world().setBlock(farCrop, state(u"qtcraft:wheat_crop[age=7]"_s));
        const quint32 entity = settle(head, centreOf(m_feet.offset(-5, 0, -5))).id;

        int ripe = ripeCrops(crops);
        for (int tick = 0; tick < 2400 && ripe > 0; ++tick) {
            ticks(1);
            const int now = ripeCrops(crops);
            QVERIFY(ripe - now <= 1);
            ripe = now;
        }
        QCOMPARE(ripe, 0);
        QCOMPARE(villager(entity)->villager->profession, u"qtcraft:farmer"_s);
        QCOMPARE(villager(entity)->villager->produce, 9);
        ticks(200);
        QCOMPARE(ripeCrops({farCrop}), 1);
        QCOMPARE(villager(entity)->villager->produce, 9);
        for (const QcBlockPos &crop : std::as_const(crops)) {
            QVERIFY(m_content->block(*m_server->world().blockAt(crop)).crop.isCrop());
        }
    }

    void farmersLeaveCropsWithoutMobGriefing()
    {
        start(0.1);
        chat(u"/gamerule mobGriefing false"_s);
        const QcBlockPos head = bed(m_feet.offset(-6, 0, -6));
        const QcBlockPos composter = m_feet.offset(0, 0, 4);
        m_server->world().setBlock(composter, id("qtcraft:composter"));
        const QcBlockPos crop = m_feet.offset(1, 0, 6);
        m_server->world().setBlock(crop.offset(0, -1, 0), id("qtcraft:farmland"));
        m_server->world().setBlock(crop, state(u"qtcraft:wheat_crop[age=7]"_s));
        const quint32 entity = settle(head, centreOf(m_feet.offset(-5, 0, -5))).id;

        ticks(400);
        QCOMPARE(villager(entity)->villager->profession, u"qtcraft:farmer"_s);
        QCOMPARE(ripeCrops({crop}), 1);
        QCOMPARE(villager(entity)->villager->produce, 0);

        // With the rule on again a confined farmer still leaves it (decision E-510), a free one
        // harvests it.
        chat(u"/gamerule mobGriefing true"_s);
        villager(entity)->confined = true;
        ticks(400);
        QCOMPARE(ripeCrops({crop}), 1);
        villager(entity)->confined = false;
        QVERIFY(tickUntil([&] { return ripeCrops({crop}) == 0; }, 600));
    }

    void villagersPickUpFoodThrownToThem()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        const quint32 entity = settle(head, centreOf(m_feet.offset(5, 0, 0))).id;
        ticks(2);
        const QcVillagerBreedingSettings &breeding = m_content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)->breeding;
        QVERIFY(breeding.foods.value(u"qtcraft:bread"_s) > 0);
        const auto drop = [this](const char *name, int count, const QcVec3 &at) {
            QcItemEntityManager::Drop item;
            item.stack = {this->item(name), count, 0};
            item.position = at;
            item.pickupDelay = 0.0;
            return m_server->items().spawn(item, m_server->entities().newId());
        };
        // Food it does not eat stays; bread at its feet is taken until it holds what it wants.
        // Without the game rule mobGriefing nothing is picked up, like in the reference game.
        chat(u"/gamerule mobGriefing false"_s);
        const quint32 left = drop("qtcraft:bread", 16, villager(entity)->motion.position);
        ticks(2 * QcServerVillagers::foodTicks);
        QCOMPARE(villager(entity)->villager->produce, 0);
        QVERIFY(m_server->items().take(left, 16));
        chat(u"/gamerule mobGriefing true"_s);
        // Nor does a confined villager (decision E-510).
        villager(entity)->confined = true;
        const quint32 untouched = drop("qtcraft:bread", 16, villager(entity)->motion.position);
        ticks(2 * QcServerVillagers::foodTicks);
        QCOMPARE(villager(entity)->villager->produce, 0);
        QVERIFY(m_server->items().take(untouched, 16));
        villager(entity)->confined = false;

        const quint32 stone = drop("qtcraft:cobblestone", 1, villager(entity)->motion.position);
        const quint32 bread = drop("qtcraft:bread", 16, villager(entity)->motion.position);
        QVERIFY(stone != 0 && bread != 0);
        QVERIFY(tickUntil([&] { return villager(entity)->villager->produce > 0; }, 100));
        const int wants = breeding.shareAbove + breeding.food;
        const int taken = (wants + breeding.foods.value(u"qtcraft:bread"_s) - 1) / breeding.foods.value(u"qtcraft:bread"_s);
        QCOMPARE(villager(entity)->villager->produce, taken * breeding.foods.value(u"qtcraft:bread"_s));
        const auto countOf = [this](quint32 id) {
            const QList<QcItemEntity> &items = m_server->items().items();
            const auto it = std::ranges::find(items, id, &QcItemEntity::id);
            return it == items.end() ? 0 : it->stack.count;
        };
        QCOMPARE(countOf(bread), 16 - taken);
        QCOMPARE(countOf(stone), 1);
        ticks(40);
        QCOMPARE(countOf(bread), 16 - taken);
    }

    void villagersAreSavedWithTheirHomeAndWork()
    {
        const QTemporaryDir &directory = m_directory.emplace();
        QcServerConfig config;
        config.worldDirectory = directory.path();
        start(0.1, config);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        m_server->world().setBlock(m_feet.offset(-5, 0, 4), id("qtcraft:barrel"));
        const quint32 entity = settle(head, centreOf(m_feet.offset(5, 0, 0)), 4242).id;
        ticks(30);
        const QcVillagerRecord saved = static_cast<const QcVillagerRecord &>(*villager(entity)->villager);
        // Its persistent id stays across saving and loading (decision E-420), the entity id does not.
        const quint64 uid = villager(entity)->uid;
        QVERIFY(uid != 0);
        QCOMPARE(saved.profession, u"qtcraft:fisher"_s);
        QVERIFY(saved.work);
        const QList<QcVillage> villages = m_server->world().villages().villages();
        QVERIFY(m_server->save().has_value());
        cleanup();

        join(config, std::make_unique<VillageTerrain>(m_content));
        loadAround(m_feet, 2);
        ticks(2);
        const auto found = std::ranges::find_if(m_server->mobs().mobs(), [uid](const QcCreature &mob) { return mob.uid == uid; });
        QVERIFY(found != m_server->mobs().mobs().end());
        QCOMPARE(static_cast<const QcVillagerRecord &>(*found->villager), saved);
        QCOMPARE(village()->workstations.front().claimedBy, uid);
        QCOMPARE(m_server->world().villages().villages(), villages);
        QCOMPARE(village()->population, 1);
    }

    // The counter of persistent ids is saved with the chunks in one journal (decision E-420): after
    // a crash, and with villages.json put back from the save before, new villagers get ids no saved
    // one has, and every bed is claimed by a villager in the world.
    void persistentIdsStayUniqueAfterACrash()
    {
        const QTemporaryDir &directory = m_directory.emplace();
        QcServerConfig config;
        config.worldDirectory = directory.path();
        start(0.1, config);
        const quint64 first = settle(bed(m_feet.offset(4, 0, 0)), centreOf(m_feet.offset(5, 0, 0))).uid;
        QVERIFY(m_server->save().has_value());
        QFile villages(QDir(directory.path()).filePath(u"villages.json"_s));
        QVERIFY(villages.open(QIODevice::ReadOnly));
        const QByteArray before = villages.readAll();
        villages.close();
        const quint64 second = settle(bed(m_feet.offset(-4, 0, 0)), centreOf(m_feet.offset(-3, 0, 0))).uid;
        QVERIFY(second > first);
        QVERIFY(m_server->save().has_value());
        QVERIFY(villages.open(QIODevice::WriteOnly | QIODevice::Truncate));
        villages.write(before);
        villages.close();
        // Settled after the last save, then the crash: never saved.
        settle(bed(m_feet.offset(0, 0, 8)), centreOf(m_feet.offset(1, 0, 8)));
        cleanup();

        join(config, std::make_unique<VillageTerrain>(m_content));
        loadAround(m_feet, 2);
        ticks(30);
        const quint64 third = settle(bed(m_feet.offset(0, 0, -6)), centreOf(m_feet.offset(1, 0, -6))).uid;
        // The animals of the chunks have persistent ids too.
        QSet<quint64> ids;
        QSet<quint64> villagers;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            QVERIFY2(!ids.contains(mob.uid), qPrintable(QString::number(mob.uid)));
            ids.insert(mob.uid);
            if (mob.isVillager()) {
                villagers.insert(mob.uid);
            }
        }
        QVERIFY(!ids.contains(0));
        QCOMPARE(villagers, (QSet<quint64>{first, second, third}));
        QVERIFY(third > second);
        ticks(30);
        for (const QcVillage::Spot &spot : std::as_const(village()->beds)) {
            QVERIFY(spot.claimedBy == 0 || ids.contains(spot.claimedBy));
        }
    }

    // On its way home its chunk leaves the world as the player goes away: it is kept with the
    // chunk and, back with it, goes on to its bed with its claims (a real server, decision E-411).
    void aVillagerGoesOnHomeAfterItsChunkLeftAndCame()
    {
        m_unloadChunks = true;
        m_viewDistance = 2;
        const auto restore = qScopeGuard([this] {
            m_unloadChunks = false;
            m_viewDistance = 1;
        });
        const QTemporaryDir &directory = m_directory.emplace();
        QcServerConfig config;
        config.worldDirectory = directory.path();
        start(0.7, config, 24);
        const QcBlockPos head = bed(m_feet.offset(18, 0, 0));
        // Its persistent id stays across the unloading (decision E-420).
        const quint64 uid = settle(head, centreOf(m_feet.offset(-20, 0, 2))).uid;
        const auto find = [this, uid]() -> const QcCreature * {
            const auto found = std::ranges::find_if(m_server->mobs().mobs(), [uid](const QcCreature &mob) { return mob.uid == uid; });
            return found == m_server->mobs().mobs().end() || found->dormant ? nullptr : &*found;
        };

        // Halfway, the player goes far away and the villager's chunk leaves.
        QVERIFY(tickUntil([&] { return find() && find()->motion.position.x > m_feet.x - 10; }, 600));
        QVERIFY(find()->villager->activity == QcVillagerActivity::Sleep && find()->pose != QcPose::Lying);
        const QcChunkPos left = QcPhysics::blockContaining(find()->motion.position).chunk();
        chat(u"/tp %1 %2 %3"_s.arg(m_feet.x + 600).arg(m_feet.y + 40).arg(m_feet.z));
        QVERIFY(tickUntil([&] { return !m_server->world().chunk(left); }, 200));
        QVERIFY(!find());
        ticks(40);

        // Back again, it goes on to its bed and lies down, still the only one living there.
        chat(u"/tp %1 %2 %3"_s.arg(m_feet.x).arg(m_feet.y).arg(m_feet.z));
        loadAround(m_feet, 2);
        QVERIFY(tickUntil([&] { return find() != nullptr; }, 20));
        QVERIFY(tickUntil([&] { return find()->pose == QcPose::Lying; }, 1200));
        QCOMPARE(find()->villager->home, std::optional<QcBlockPos>(head));
        QVERIFY(horizontalDistance(find()->motion.position, centreOf(m_feet.offset(18, 0, 0))) < 0.5);
        QCOMPARE(village()->population, 1);
        QCOMPARE(std::ranges::count_if(m_server->mobs().mobs(), [uid](const QcCreature &mob) { return mob.uid == uid; }), 1);
        QCOMPARE(village()->beds.front().claimedBy, uid);
    }

    void aVillagerThatDiesFreesItsBedAndWorkstation()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        m_server->world().setBlock(m_feet.offset(-5, 0, 4), id("qtcraft:furnace"));
        const quint32 entity = settle(head, centreOf(m_feet.offset(5, 0, 0))).id;
        ticks(30);
        QCOMPARE(village()->workstations.front().claimedBy, villager(entity)->uid);

        QVERIFY(m_server->mobs().wound(entity, 100.0).has_value());
        QCOMPARE(village()->beds.front().claimedBy, 0u);
        QCOMPARE(village()->workstations.front().claimedBy, 0u);
        QCOMPARE(village()->population, 0);
    }

    void aVillagerTakenByTheVoidFreesItsBed()
    {
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        const quint32 entity = settle(head, centreOf(m_feet.offset(5, 0, 0))).id;
        QCOMPARE(village()->population, 1);
        villager(entity)->motion.position.y = -1000.0;
        ticks(1);
        QVERIFY(!villager(entity));
        QCOMPARE(village()->beds.front().claimedBy, 0u);
        QCOMPARE(village()->population, 0);
    }

    void usingAnUnemployedVillagerTurnsItTowardsThePlayer()
    {
        // Without offers there is no trading screen (decision E-414): it only looks up.
        start(0.1);
        const QcBlockPos head = bed(m_feet.offset(4, 0, 0));
        const quint32 entity = settle(head, centreOf(m_feet.offset(2, 0, 0))).id;
        send(QcMsg::UseEntity{entity});
        QCOMPARE(villager(entity)->villager->trader, 0u);
        const QcVec3 towards = m_client->motion.position - villager(entity)->motion.position;
        QCOMPARE(qRound(villager(entity)->yaw), qRound(QcMobSteering::yawTowards({towards.x, 0.0, towards.z})));
    }
};

QTEST_GUILESS_MAIN(TestVillagers)
#include "test_villagers.moc"
