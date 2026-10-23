#include "common/physics/QcBodyFit.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/villages/QcVillageGolems.h"
#include "server/villages/QcVillagePopulation.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageFixture.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Villagers summon iron golems like in the reference game (decision E-470), without a server, on a
// fixture village (decision E-424), with ticks stepped by hand: villagers that panic at a zombie
// nearby summon one, three that slept and worked within the gossip time summon one by chance, two
// summon none; a golem near them, the village's cooldown and the world's limit hold the next one
// back; the golem stands on the ground where its whole body fits within the spawn box; villagers
// without a village summon too, with their pause per chunk; no golem comes before the village's
// claims are reconciled or while the content lacks the golem kind; golems of every village count
// towards the world's limit; a last golem in the future (the time went back) is forgotten.
class TestGolemSummons : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;
    // Work time (QcVillagerSettings::routine).
    static constexpr double workPhase = 0.1;

    struct Village
    {
        std::shared_ptr<QcContentRegistry> content;
        std::unique_ptr<QcServerWorld> world;
        std::unique_ptr<QcMobManager> mobs;
        std::shared_ptr<const QcVillagePlan> plan;
        quint32 nextId = 1;
        double worldTime = QcDayCycle::dayLength * (10.0 + workPhase);

        QcVillage &state() const { return *world->villages().find(plan->id); }
        std::function<quint32()> ids()
        {
            return [this] { return nextId++; };
        }
        int golemType() const { return *content->mobIndex(u"qtcraft:iron_golem"_s); }
    };

    // Loads the chunks of the plan and `ring` around them, then populates the plan's chunks (which
    // reconciles the claims once every chunk around the village is there).
    static void loadAround(Village &village, int ring)
    {
        const QList<QcChunkPos> around = QcVillageFixture::chunksOf(*village.plan, ring);
        for (const QcChunkPos &chunk : around) {
            village.world->loadChunk(chunk);
        }
        const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*village.plan);
        for (const QcChunkPos &chunk : chunks) {
            village.mobs->populateChunk(*village.world, chunk, village.ids());
        }
    }

    // A fixture village of the plains style with its villagers moved in and the chunks around it.
    static Village village(const std::shared_ptr<QcContentRegistry> &content = QcTestUtil::loadContent(), int ring = 2)
    {
        Village result;
        result.content = content;
        result.plan = QcVillageFixture::plan(*content, {});
        if (!result.plan) {
            return result;
        }
        result.world = std::make_unique<QcServerWorld>(content, std::make_unique<QcVillageFixture::Terrain>(content, QList{result.plan}));
        result.mobs = std::make_unique<QcMobManager>(content, seed);
        result.mobs->population().setVillages(&result.world->villages());
        loadAround(result, ring);
        return result;
    }

    static QList<QcCreature *> adults(Village &village)
    {
        QList<QcCreature *> list;
        for (QcCreature &mob : village.mobs->editableMobs()) {
            if (mob.isVillager() && !mob.isYoung()) {
                list.append(&mob);
            }
        }
        return list;
    }

    static qsizetype golems(const Village &village)
    {
        return std::ranges::count(village.mobs->mobs(), village.golemType(), &QcCreature::type);
    }

    static void tick(Village &village)
    {
        village.mobs->tick(*village.world, {}, village.worldTime, QcPhysics::tickDuration, village.ids());
        village.mobs->villagers().takeActions();
        village.worldTime += QcPhysics::tickDuration;
    }

    // A spot on the village road `along` blocks from its start, at the height of the road.
    static QcVec3 roadSpot(const Village &village, int along)
    {
        const QcBlockPos centre = village.plan->centre;
        return {centre.x + along + 0.5, static_cast<double>(QcVillageFixture::landFloor + 1), centre.z + 3.5};
    }

    // For one check of village life: the first `count` adults stand on the road a block apart and
    // `want` sets what they remember each tick (panic and sleep).
    static void gather(Village &village, int count, const std::function<void(QcVillager &)> &want)
    {
        const QList<QcCreature *> chosen = adults(village).mid(0, count);
        QList<quint32> ids;
        for (const QcCreature *mob : chosen) {
            ids.append(mob->id);
        }
        for (int i = 0; i <= QcVillagePopulation::villageLifeTicks; ++i) {
            for (int n = 0; n < ids.size(); ++n) {
                if (QcCreature *mob = village.mobs->find(ids[n])) {
                    mob->motion.position = roadSpot(village, n);
                    mob->motion.velocity = {};
                    want(*mob->villager);
                }
            }
            tick(village);
        }
    }

    static void panic(Village &village, int count)
    {
        gather(village, count, [&village](QcVillager &villager) {
            villager.sleptAt = village.worldTime - 100.0;
            villager.panickedAt = village.worldTime;
        });
    }

    static void gossip(Village &village, int count)
    {
        gather(village, count, [&village](QcVillager &villager) {
            villager.sleptAt = village.worldTime - 100.0;
        });
    }

    // Every villager forgets when it last panicked and slept.
    static void forget(Village &village)
    {
        const QList<QcCreature *> all = adults(village);
        for (QcCreature *mob : all) {
            mob->villager->panickedAt = -1.0;
            mob->villager->sleptAt = -1.0;
        }
    }

    static void setGolemSettings(Village &village, const std::function<void(QcVillageGolemSettings &)> &change)
    {
        QcVillageSettings settings = village.content->villageCatalog().settings();
        change(settings.golem);
        village.content->villageCatalog().setSettings(settings);
    }

    static void removeGolems(Village &village)
    {
        QList<quint32> ids;
        for (const QcCreature &mob : village.mobs->mobs()) {
            if (mob.type == village.golemType()) {
                ids.append(mob.id);
            }
        }
        for (const quint32 id : std::as_const(ids)) {
            village.mobs->remove(id);
        }
    }

private slots:
    void villagersPanickingAtAZombieSummonAGolem()
    {
        // A zombie that does not burn by day stands among three villagers on the road.
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcMobDefinition zombie = content->mobs()[*content->mobIndex(u"qtcraft:zombie"_s)];
        zombie.burnsInDaylight = false;
        content->registerMob(zombie);
        Village built = village(content);
        QVERIFY(built.plan);
        QCOMPARE(golems(built), 0);
        const quint32 zombieId = built.mobs->spawn(*content->mobIndex(u"qtcraft:zombie"_s), roadSpot(built, 6), built.nextId++);

        const QList<QcCreature *> three = adults(built).mid(0, 3);
        QCOMPARE(three.size(), 3);
        bool panicked = false;
        for (int i = 0; i <= 3 * QcVillagePopulation::villageLifeTicks && golems(built) == 0; ++i) {
            for (int n = 0; n < 3; ++n) {
                three[n]->motion.position = roadSpot(built, n);
                three[n]->motion.velocity = {};
                three[n]->villager->sleptAt = built.worldTime - 100.0;
            }
            built.mobs->find(zombieId)->motion.position = roadSpot(built, 6);
            tick(built);
            panicked = panicked || std::ranges::all_of(three, [&](const QcCreature *mob) { return mob->villager->panickedAt >= 0.0; });
        }
        QVERIFY(panicked);
        QCOMPARE(golems(built), 1);

        // Within the spawn box around one of them, persistent, a village golem; the village notes it.
        const QcCreature &golem = *std::ranges::find(built.mobs->mobs(), built.golemType(), &QcCreature::type);
        const QcVec3 middle = roadSpot(built, 1);
        QVERIFY(std::abs(golem.motion.position.x - middle.x) <= 10.0 && std::abs(golem.motion.position.z - middle.z) <= 9.0);
        QVERIFY(std::abs(golem.motion.position.y - middle.y) <= QcVillageGolemSettings::spawnHeight + 1);
        QVERIFY(golem.persistent && golem.golem && !golem.golem->playerBuilt);
        QVERIFY(built.state().lastGolem >= 0.0);
    }

    void gossipNeedsFiveAndItsChancePanicThreeThatSlept()
    {
        Village built = village();
        QVERIFY(built.plan);
        QVERIFY(adults(built).size() >= QcVillageGolemSettings{}.gossipVillagers);

        // Without the chance: none, however long they gossip.
        setGolemSettings(built, [](QcVillageGolemSettings &golem) { golem.gossipChance = 0.0; });
        gossip(built, 5);
        QCOMPARE(golems(built), 0);

        // With a sure chance four that slept are too few to gossip, and two panicking ones too few
        // to panic (the others forgot their sleep).
        setGolemSettings(built, [](QcVillageGolemSettings &golem) { golem.gossipChance = 1.0; });
        forget(built);
        gossip(built, 4);
        QCOMPARE(golems(built), 0);
        forget(built);
        panic(built, 2);
        QCOMPARE(golems(built), 0);

        // Panic without sleep: none (the reference's villagers must have slept within a day).
        forget(built);
        gather(built, 3, [&built](QcVillager &villager) { villager.panickedAt = built.worldTime; });
        QCOMPARE(golems(built), 0);

        // Five that slept, with a sure chance: one golem.
        gossip(built, 5);
        QCOMPARE(golems(built), 1);

        // Slept long ago: no gossip, so no next one (after the golem left and the cooldown).
        removeGolems(built);
        built.worldTime += 600.0;
        gather(built, 5, [&built](QcVillager &villager) { villager.sleptAt = built.worldTime - 2.0 * QcVillageGolemSettings{}.sleptSeconds; });
        QCOMPARE(golems(built), 0);
    }

    void aGolemNearbyTheCooldownAndTheWorldLimitHoldTheNextBack()
    {
        Village built = village();
        QVERIFY(built.plan);
        panic(built, 3);
        QCOMPARE(golems(built), 1);

        // The golem stays near them: no second one, however long they panic.
        QcCreature &first = *std::ranges::find(built.mobs->editableMobs(), built.golemType(), &QcCreature::type);
        const quint32 firstId = first.id;
        for (int check = 0; check < 3; ++check) {
            built.mobs->find(firstId)->motion.position = roadSpot(built, 4);
            panic(built, 3);
        }
        QCOMPARE(golems(built), 1);

        // Gone, but within the cooldown: none; after it, the next.
        setGolemSettings(built, [](QcVillageGolemSettings &golem) { golem.cooldownSeconds = 600.0; });
        removeGolems(built);
        built.state().lastGolem = built.worldTime;
        panic(built, 3);
        QCOMPARE(golems(built), 0);
        built.worldTime += 600.0;
        panic(built, 3);
        QCOMPARE(golems(built), 1);

        // At the world's limit: a golem far away is the only one allowed.
        setGolemSettings(built, [](QcVillageGolemSettings &golem) { golem.cooldownSeconds = 0.0; });
        removeGolems(built);
        built.mobs->spawn(built.golemType(), roadSpot(built, 40), built.nextId++, false, true);
        built.mobs->population().setGolemLimit(1);
        panic(built, 3);
        QCOMPARE(golems(built), 1);
        built.mobs->population().setGolemLimit(2);
        panic(built, 3);
        QCOMPARE(golems(built), 2);
        // A limit of 0: villagers summon none.
        removeGolems(built);
        built.mobs->population().setGolemLimit(0);
        panic(built, 3);
        QCOMPARE(golems(built), 0);
    }

    void villagersWithoutAVillageSummonWithAPausePerChunk()
    {
        // Two farms of three villagers on open ground far from the village, three chunks apart, with
        // no village of their own.
        Village built = village();
        QVERIFY(built.plan);
        const int villagerType = *built.content->mobIndex(u"qtcraft:villager"_s);
        const QcBlockPos far{built.plan->maxX + 3 * QcVillageFixture::villageGap, QcVillageFixture::landFloor + 1, built.plan->centre.z};
        const int apart = 48;
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 6; ++dz) {
                built.world->loadChunk({far.chunk().x + dx, far.chunk().z + dz});
            }
        }
        const auto spot = [&](int n) {
            const int farm = n / 3;
            return QcVec3{far.x + n % 3 + 0.5, static_cast<double>(far.y), far.z + farm * apart + 0.5};
        };
        QList<quint32> strays;
        for (int n = 0; n < 6; ++n) {
            strays.append(built.mobs->spawn(villagerType, spot(n), built.nextId++));
            QCOMPARE(built.mobs->find(strays.last())->villager->village, 0);
        }
        const auto panicStrays = [&] {
            for (int i = 0; i <= QcVillagePopulation::villageLifeTicks; ++i) {
                for (int n = 0; n < strays.size(); ++n) {
                    QcCreature *stray = built.mobs->find(strays[n]);
                    stray->motion.position = spot(n);
                    stray->villager->sleptAt = built.worldTime - 100.0;
                    stray->villager->panickedAt = built.worldTime;
                }
                tick(built);
            }
        };
        // Both farms summon in the same check: villagers without a village are no single group.
        panicStrays();
        QCOMPARE(golems(built), 2);

        // Gone at once: the pause of their chunks holds the next ones back until it is over.
        removeGolems(built);
        panicStrays();
        QCOMPARE(golems(built), 0);
        built.worldTime += QcVillageGolemSettings{}.cooldownSeconds;
        panicStrays();
        QCOMPARE(golems(built), 2);
    }

    void aGolemStandsOnTheGroundWhereItsBodyFits()
    {
        Village built = village();
        QVERIFY(built.plan);
        const int type = built.golemType();
        const QcMobDefinition &golem = built.content->mobs()[type];
        const QcBlockPos centre = built.plan->centre;
        const QcVillageGolems::Context context{*built.world, built.mobs->paths().cells(), built.worldTime, seed, 256};
        const QcVillageGolems &rule = built.mobs->population().golems();
        const QcBlockId stone = *built.content->blockId(u"qtcraft:stone"_s);
        const auto fill = [&](QcBlockPos low, QcBlockPos high, QcBlockId block) {
            for (int x = low.x; x <= high.x; ++x) {
                for (int y = low.y; y <= high.y; ++y) {
                    for (int z = low.z; z <= high.z; ++z) {
                        built.world->setBlock({x, y, z}, block);
                    }
                }
            }
        };
        const QcVec3 around{centre.x + 0.5, static_cast<double>(centre.y), centre.z + 0.5};
        fill(centre.offset(-10, 0, -10), centre.offset(10, 10, 10), qcAirBlockId);
        fill(centre.offset(-10, -1, -10), centre.offset(10, -1, 10), stone);

        // Pillars on every other column: each column between them is free, but the body does not fit.
        for (int dx = -10; dx <= 10; ++dx) {
            for (int dz = -10; dz <= 10; ++dz) {
                if ((dx + dz) % 2 == 0) {
                    fill(centre.offset(dx, 0, dz), centre.offset(dx, 9, dz), stone);
                }
            }
        }
        QVERIFY(!rule.placeNear(around, type, 8, context, 1));

        // Without them, under a glass roof: on the ground within the box, not on the glass.
        const QcBlockId glass = *built.content->blockId(u"qtcraft:glass"_s);
        fill(centre.offset(-10, 0, -10), centre.offset(10, 10, 10), qcAirBlockId);
        fill(centre.offset(-10, 4, -10), centre.offset(10, 4, 10), glass);
        for (quint64 draw = 1; draw <= 20; ++draw) {
            const std::optional<QcVec3> at = rule.placeNear(around, type, 8, context, draw);
            QVERIFY(at);
            QCOMPARE(at->y, double(centre.y));
            QVERIFY(std::abs(at->x - around.x) <= 8.5 && std::abs(at->z - around.z) <= 8.5);
            QVERIFY(!QcBodyFit::overlapsBlocks(*built.world, *at, {golem.width, golem.height}, QcBodyFit::Unloaded::Blocked));
        }

        // Under a stone roof within reach: on the roof, the highest place (from the top down like the
        // reference).
        fill(centre.offset(-10, 4, -10), centre.offset(10, 4, 10), stone);
        const std::optional<QcVec3> onRoof = rule.placeNear(around, type, 8, context, 1);
        QVERIFY(onRoof);
        QCOMPARE(onRoof->y, double(centre.y + 5));

        // Glass, leaves and ice are no floor for a golem, magma hurts, water is no floor: no place.
        for (const char *name : {"qtcraft:glass", "qtcraft:oak_leaves", "qtcraft:ice"}) {
            fill(centre.offset(-10, 0, -10), centre.offset(10, 10, 10), qcAirBlockId);
            fill(centre.offset(-10, -1, -10), centre.offset(10, -1, 10), *built.content->blockId(QString::fromLatin1(name)));
            QVERIFY2(!rule.placeNear(around, type, 8, context, 1), name);
        }
        fill(centre.offset(-10, 0, -10), centre.offset(10, 10, 10), qcAirBlockId);
        fill(centre.offset(-10, -1, -10), centre.offset(10, -1, 10), *built.content->blockId(u"qtcraft:magma_block"_s));
        QVERIFY(!rule.placeNear(around, type, 8, context, 1));
        fill(centre.offset(-10, -1, -10), centre.offset(10, -1, 10), *built.content->blockId(u"qtcraft:water"_s));
        QVERIFY(!rule.placeNear(around, type, 8, context, 1));
    }

    void noGolemBeforeTheClaimsAreReconciledOrWithoutTheKind()
    {
        // Only the chunks of the village's box: its claims are not reconciled yet.
        Village partial = village(QcTestUtil::loadContent(), 0);
        QVERIFY(partial.plan);
        panic(partial, 3);
        QCOMPARE(golems(partial), 0);
        loadAround(partial, 2);
        panic(partial, 3);
        QCOMPARE(golems(partial), 1);

        // A golem kind the content does not have: the rule rests.
        Village built = village();
        setGolemSettings(built, [](QcVillageGolemSettings &golem) { golem.mob = u"test:nobody"_s; });
        QVERIFY(!built.mobs->population().golems().kind(built.state().style));
        const qsizetype before = built.mobs->mobs().size();
        panic(built, 3);
        QCOMPARE(built.mobs->mobs().size(), before);
    }

    void golemsCountTowardsTheWorldInEveryVillage()
    {
        // A village not loaded holds three golems; one stands outside every village: four in the
        // world. Five loaded in the village's area count instead of its three.
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcServerWorld world(content, std::make_unique<QcVillageFixture::Terrain>(content, QList<std::shared_ptr<const QcVillagePlan>>{}));
        QcVillage stored;
        stored.id = 3;
        stored.maxX = 10;
        stored.maxY = 80;
        stored.maxZ = 10;
        stored.golems = 3;
        world.villages().restore({stored});
        const int type = *content->mobIndex(u"qtcraft:iron_golem"_s);
        const QcVillageGolems rule(content);
        QList<QcCreature> mobs(1);
        mobs[0].type = type;
        mobs[0].motion.position = {500.5, 64.0, 500.5};
        QCOMPARE(rule.golemsInWorld(mobs, type, world.villages()), 4);
        for (int i = 0; i < 5; ++i) {
            QcCreature golem;
            golem.type = type;
            golem.motion.position = {5.5 + i, 64.0, 5.5};
            mobs.append(golem);
        }
        QCOMPARE(rule.golemsInWorld(mobs, type, world.villages()), 6);
    }

    void aLastGolemInTheFutureIsForgotten()
    {
        Village built = village();
        QVERIFY(built.plan);
        setGolemSettings(built, [](QcVillageGolemSettings &golem) { golem.cooldownSeconds = 600.0; });
        // /time set back: the last golem lies a day ahead, yet the next one comes and the time is
        // put right.
        built.state().lastGolem = built.worldTime + QcDayCycle::dayLength;
        panic(built, 3);
        QCOMPARE(golems(built), 1);
        QVERIFY(built.state().lastGolem <= built.worldTime);
    }

    void wantsFollowsTheMemoryOfTheVillager()
    {
        const QcVillageGolemSettings settings;
        QcCreature villager;
        villager.villager.emplace();
        const double now = 5000.0;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::None);
        // Panic alone is not enough: it must have slept within sleptSeconds.
        villager.villager->panickedAt = now - settings.panicSeconds;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::None);
        villager.villager->sleptAt = now - settings.sleptSeconds;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::Panic);
        villager.villager->panickedAt = now - settings.panicSeconds - 1.0;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::Gossip);
        villager.villager->sleptAt = now - settings.sleptSeconds - 1.0;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::None);
        villager.villager->sleptAt = now - 10.0;
        // In the future (the time went back), asleep in a chunk not loaded, or a child: no.
        villager.villager->sleptAt = now + 10.0;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::None);
        villager.villager->sleptAt = now - 10.0;
        villager.villager->panickedAt = now;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::Panic);
        villager.dormant = true;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::None);
        villager.dormant = false;
        villager.growUp = 10.0;
        QCOMPARE(QcVillageGolems::wants(villager, settings, now), QcVillageGolems::Want::None);
    }
};

QTEST_GUILESS_MAIN(TestGolemSummons)
#include "test_golem_summons.moc"
