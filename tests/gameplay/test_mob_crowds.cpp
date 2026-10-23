#include "common/world/QcDayCycle.h"
#include "QcServerTestFixture.h"
#include "server/mobs/QcMobBehaviour.h"
#include "server/mobs/QcServerCreatures.h"
#include "server/pathing/QcPathPlanner.h"
#include "worldgen/generators/QcFlatGenerator.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = 0.05;
constexpr double midnight = QcDayCycle::dayLength * 0.75;
// The flat world's ground is grass at y 4; feet stand at y 5.
constexpr int ground = 5;

} // namespace

// Crowds of creatures (stage 37.1, decision E-401), ticked by hand: the AI tiers by density (a
// thousand in one cell think not at all and search no paths), the push of the cell grid spreads a
// small group, throttled and idle creatures still strike an adjacent player, the rest of unloaded
// chunks and the density decide together, hunters need a line of sight to pick a target and remember
// it for a few seconds, and the game rule maxEntityCramming hurts crowded cells (0: never). The
// showcase station of path finding works as shown: the zombie finds its way through the maze to the
// window, the pigs stay on their plateau.
class TestMobCrowds : public QcServerTestFixture
{
    Q_OBJECT

private:
    quint32 m_nextId = 100;

    std::unique_ptr<QcServerWorld> makeWorld() const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,grass_block"_s));
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    void fill(QcServerWorld &world, QcBlockPos from, QcBlockPos to, const char *name) const
    {
        const QcBlockId block = name ? id(name) : qcAirBlockId;
        for (int x = from.x; x <= to.x; ++x) {
            for (int y = from.y; y <= to.y; ++y) {
                for (int z = from.z; z <= to.z; ++z) {
                    world.setBlock({x, y, z}, block);
                }
            }
        }
    }

    // Stone walls three high around the cell x 0, z 0: a spawner cell nobody leaves or sees into.
    void shaft(QcServerWorld &world) const
    {
        fill(world, {-1, ground, -1}, {1, ground + 2, 1}, "qtcraft:stone");
        fill(world, {0, ground, 0}, {0, ground + 2, 0}, nullptr);
    }

    std::unique_ptr<QcMobManager> makeMobs() const
    {
        auto mobs = std::make_unique<QcMobManager>(m_content, 3);
        mobs->setNaturalSpawning(false);
        return mobs;
    }

    int type(const char *name) const { return *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)); }

    void spawnMany(QcMobManager &mobs, const char *kind, int count, const QcVec3 &at)
    {
        for (int i = 0; i < count; ++i) {
            mobs.spawn(type(kind), at, m_nextId++, false, true);
        }
    }

    // Ticks the creatures; returns the number of blows they dealt.
    int run(QcMobManager &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players, int ticks)
    {
        int hits = 0;
        for (int tick = 0; tick < ticks; ++tick) {
            hits += static_cast<int>(mobs.tick(world, players, midnight, dt, [this] { return m_nextId++; }).size());
        }
        return hits;
    }

    static QcVec3 at(double x, double z, double y = ground) { return {x, y, z}; }

    qsizetype pigsIn(QcBlockPos cell) const
    {
        return std::ranges::count_if(m_server->mobs().mobs(), [cell](const QcCreature &mob) {
            return QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.01, 0.0}) == cell;
        });
    }

private slots:
    void thousandInOneCellThinkNotAtAll()
    {
        auto world = makeWorld();
        shaft(*world);
        auto mobs = makeMobs();
        spawnMany(*mobs, "zombie", 1000, at(0.5, 0.5));
        const QList<QcMobManager::PlayerView> players{{7, at(4.5, 0.5), true}};
        run(*mobs, *world, players, 100);

        const QcMobBehaviour::TierCounts tiers = mobs->behaviour().tiersLastTick();
        QCOMPARE(tiers.idle, 1000);
        QCOMPARE(tiers.full + tiers.throttled, 0);
        QCOMPARE(mobs->behaviour().crowd().largestCell(), 1000);
        // No searches, no targets: the cost is the cell grid and the physics.
        QCOMPARE(mobs->paths().searches(), qint64(0));
        for (const QcCreature &mob : mobs->mobs()) {
            QCOMPARE(mob.chasing, quint32(0));
            QVERIFY(mob.motion.position.x > 0.0 && mob.motion.position.x < 1.0);
        }
    }

    void tiersFollowTheDensity()
    {
        auto world = makeWorld();
        shaft(*world);
        auto mobs = makeMobs();
        spawnMany(*mobs, "pig", 4, at(0.5, 0.5));
        run(*mobs, *world, {}, 1);
        QCOMPARE(mobs->behaviour().tiersLastTick().full, 4);

        // Ten in the cell: throttled, and without path searches.
        // (Searches asked for before run at the start of the next tick.)
        spawnMany(*mobs, "pig", 6, at(0.5, 0.5));
        run(*mobs, *world, {}, 1);
        const qint64 searches = mobs->paths().searches();
        run(*mobs, *world, {}, 20 * 10);
        QCOMPARE(mobs->behaviour().tiersLastTick().throttled, 10);
        QCOMPARE(mobs->paths().searches(), searches + 0);

        // The limits move the thresholds.
        mobs->setCrowdLimits(4, 10);
        run(*mobs, *world, {}, 1);
        QCOMPARE(mobs->behaviour().tiersLastTick().idle, 10);
    }

    void pushSpreadsASmallGroup()
    {
        auto world = makeWorld();
        auto mobs = makeMobs();
        spawnMany(*mobs, "pig", 12, at(0.5, 0.5));
        run(*mobs, *world, {}, 1);
        QCOMPARE(mobs->behaviour().tiersLastTick().throttled, 12);
        run(*mobs, *world, {}, 60);

        QVERIFY2(mobs->behaviour().crowd().largestCell() < 8, qPrintable(QString::number(mobs->behaviour().crowd().largestCell())));
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

    void crowdsStillStrikeAnAdjacentPlayer()
    {
        // Every zombie of a throttled (10) and of an idle (30) knot strikes the player next to it in
        // the first tick, before any of them looked for a target.
        for (const int count : {10, 30}) {
            auto world = makeWorld();
            auto mobs = makeMobs();
            spawnMany(*mobs, "zombie", count, at(0.5, 0.5));
            QCOMPARE(run(*mobs, *world, {{7, at(1.6, 0.5), true}}, 1), count);
            const QcMobBehaviour::TierCounts tiers = mobs->behaviour().tiersLastTick();
            QCOMPARE(count == 10 ? tiers.throttled : tiers.idle, count);
        }
    }

    void crowdsStrikeTheNearestPlayerTheyReach()
    {
        // A glass pane shields the nearest player; the knot strikes the other one, in reach beside it.
        auto world = makeWorld();
        const QcBlockId pane = *m_content->stateId(u"qtcraft:glass_pane[north=true,south=true]"_s);
        for (int z = -3; z <= 3; ++z) {
            for (int y = ground; y <= ground + 2; ++y) {
                world->setBlock({1, y, z}, pane);
            }
        }
        auto mobs = makeMobs();
        spawnMany(*mobs, "zombie", 30, at(0.5, 0.5));
        const QList<QcMobManager::PlayerView> players{{7, at(1.9, 0.5), true}, {8, at(-0.95, 0.5), true}};
        const QList<QcMobManager::PlayerHit> hits = mobs->tick(*world, players, midnight, dt, [this] { return m_nextId++; });
        QCOMPARE(hits.size(), qsizetype(30));
        QVERIFY(std::ranges::all_of(hits, [](const QcMobManager::PlayerHit &hit) { return hit.playerEntityId == 8; }));
    }

    void restAndDensityDecideTogether()
    {
        auto world = makeWorld();
        shaft(*world);
        auto mobs = makeMobs();
        spawnMany(*mobs, "zombie", 30, at(0.5, 0.5));
        // Twenty of them in a chunk that is not loaded (QcEntities::settle): they rest and do not count.
        for (int i = 0; i < 20; ++i) {
            mobs->editableMobs()[i].dormant = true;
        }
        run(*mobs, *world, {}, 20);

        const QcMobBehaviour::TierCounts tiers = mobs->behaviour().tiersLastTick();
        QCOMPARE(tiers.throttled, 10);
        QCOMPARE(tiers.full + tiers.idle, 0);
        QCOMPARE(mobs->crammed(24).size(), qsizetype(0));
        QCOMPARE(mobs->crammed(8).size(), qsizetype(10));
        QCOMPARE(mobs->crammed(8).first().count, 10);
        for (int i = 0; i < 20; ++i) {
            QCOMPARE(mobs->mobs()[i].motion.position, at(0.5, 0.5));
        }
    }

    void targetNeedsALineOfSightAndIsRemembered()
    {
        auto world = makeWorld();
        // The player in a closed glass room; the zombie outside stands until it sees them.
        fill(*world, {-1, ground, -1}, {2, ground + 2, 1}, "qtcraft:glass");
        fill(*world, {0, ground, 0}, {1, ground + 1, 0}, nullptr);
        auto mobs = makeMobs();
        mobs->spawn(type("zombie"), at(6.5, 0.5), 1);
        mobs->editableMobs().first().wanderTimer = 1e9;
        const QList<QcMobManager::PlayerView> players{{7, at(0.5, 0.5), true}};
        run(*mobs, *world, players, 40);
        QCOMPARE(mobs->find(1)->chasing, quint32(0));

        // A window at eye height: it sees the player within a look (QcMobSteering::sightCheckTicks).
        fill(*world, {2, ground + 1, 0}, {2, ground + 1, 0}, nullptr);
        run(*mobs, *world, players, 10);
        QCOMPARE(mobs->find(1)->chasing, quint32(7));

        // Closed again: it remembers the player for a few seconds, then forgets them.
        fill(*world, {2, ground + 1, 0}, {2, ground + 1, 0}, "qtcraft:glass");
        run(*mobs, *world, players, 80);
        QCOMPARE(mobs->find(1)->chasing, quint32(7));
        run(*mobs, *world, players, 40);
        QCOMPARE(mobs->find(1)->chasing, quint32(0));
    }

    void showcaseStationShowsPaths()
    {
        auto generator = std::make_unique<QcShowcaseGenerator>(m_content);
        const QcShowcasePlan &plan = generator->plan();
        const auto station = std::ranges::find(plan.areas, u"demo-path_station"_s, &QcShowcaseAreaInfo::name);
        QVERIFY(station != plan.areas.end());
        const QcBlockPos origin = station->origin;
        const QcWorldSetup setup = generator->setup();

        QcServerWorld world(m_content, std::move(generator));
        for (int x = -1; x <= station->sizeX / 16 + 1; ++x) {
            for (int z = -1; z <= station->sizeZ / 16 + 1; ++z) {
                world.loadChunk({origin.chunk().x + x, origin.chunk().z + z});
            }
        }
        auto mobs = makeMobs();
        quint32 zombie = 0;
        for (const QcWorldSetup::Mob &placed : setup.mobs) {
            const QcVec3 local = placed.at - QcVec3{static_cast<double>(origin.x), static_cast<double>(origin.y), static_cast<double>(origin.z)};
            if (local.x >= 0.0 && local.x < station->sizeX && local.z >= 0.0 && local.z < station->sizeZ) {
                const quint32 id = mobs->spawn(*m_content->mobIndex(placed.mob), placed.at, m_nextId++, false, true);
                zombie = placed.mob.endsWith(u"zombie"_s) ? id : zombie;
            }
        }
        QCOMPARE(mobs->mobs().size(), qsizetype(3));

        // A visitor in front of the window: the zombie sees them and walks the maze to the corridor
        // behind the window, out of reach.
        const QList<QcMobManager::PlayerView> players{{7, QcVec3{origin.x + 4.5, origin.y + 1.0, origin.z + 16.5}, true}};
        int hits = 0;
        for (int tick = 0; tick < 20 * 30 && mobs->find(zombie)->motion.position.z < origin.z + 8.0; ++tick) {
            hits += run(*mobs, world, players, 1);
        }
        QCOMPARE(mobs->find(zombie)->chasing, quint32(7));
        QVERIFY2(mobs->find(zombie)->motion.position.z > origin.z + 8.0, qPrintable(QString::number(mobs->find(zombie)->motion.position.z - origin.z)));
        hits += run(*mobs, world, players, 20 * 10);
        QCOMPARE(hits, 0);

        // A minute of wandering: the pigs never step off the plateau, four blocks high.
        for (int tick = 0; tick < 20 * 60; ++tick) {
            run(*mobs, world, players, 1);
            for (const QcCreature &mob : mobs->mobs()) {
                QVERIFY(mob.id == zombie || mob.motion.position.y > origin.y + 4.9);
            }
        }
    }

    void crammingHurtsCrowdedCells()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);

        // Thirty pigs in a cell walled in by stone: with the default rule (24) they die until about
        // 24 are left, and leave their loot.
        const QcBlockPos cell = m_feet.offset(2, 0, 2);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = 0; dy <= 2; ++dy) {
                    m_server->world().setBlock(cell.offset(dx, dy, dz), (dx == 0 && dz == 0) ? qcAirBlockId : id("qtcraft:stone"));
                }
            }
        }
        const QcVec3 centre{cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
        for (int i = 0; i < 30; ++i) {
            m_server->mobs().spawn(type("pig"), centre, m_server->entities().newId(), false, true);
        }
        ticks(20 * 10);
        const qsizetype left = pigsIn(cell);
        QVERIFY2(left <= 24 && left >= 18, qPrintable(QString::number(left)));
        QVERIFY(!m_server->items().items().isEmpty());

        // Two hundred more: their hurt and death sounds stay within the cap of each tick.
        for (int i = 0; i < 200; ++i) {
            m_server->mobs().spawn(type("pig"), centre, m_server->entities().newId(), false, true);
        }
        int crammingSounds = 0;
        for (int tick = 0; tick < 20; ++tick) {
            const qsizetype heard = m_client->sounds.size();
            ticks(1);
            const qsizetype pains = std::count_if(m_client->sounds.cbegin() + heard, m_client->sounds.cend(), [](const QString &key) {
                return key == u"qtcraft:mob.pig.hurt"_s || key == u"qtcraft:mob.pig.death"_s;
            });
            QVERIFY2(pains <= QcServerCreatures::crammingSoundsPerTick, qPrintable(QString::number(pains)));
            crammingSounds += static_cast<int>(pains);
        }
        QVERIFY(crammingSounds > 20);
        QVERIFY(pigsIn(cell) < left + 200);

        // Off: nobody is hurt.
        chat(u"/gamerule maxEntityCramming 0"_s);
        QCOMPARE(m_client->chatLines.constLast(), u"maxEntityCramming is now 0"_s);
        for (int i = 0; i < 20; ++i) {
            m_server->mobs().spawn(type("pig"), centre, m_server->entities().newId(), false, true);
        }
        const auto totalHealth = [this] {
            double total = 0.0;
            for (const QcCreature &pig : m_server->mobs().mobs()) {
                total += pig.health;
            }
            return total;
        };
        const double health = totalHealth();
        ticks(20 * 10);
        QCOMPARE(pigsIn(cell), left + 20);
        QCOMPARE(totalHealth(), health);

        // The rule is saved with the world.
        QVERIFY(m_server->save().has_value());
        QFile file(directory.filePath(u"world.json"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject rules = QJsonDocument::fromJson(file.readAll()).object().value(u"gameRules"_s).toObject();
        QCOMPARE(rules.value(u"maxEntityCramming"_s).toInt(-1), 0);
    }
};

QTEST_GUILESS_MAIN(TestMobCrowds)
#include "test_mob_crowds.moc"
