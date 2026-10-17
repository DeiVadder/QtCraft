#include "common/content/loader/QcBaseContent.h"
#include "common/physics/QcBodyFit.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/mobs/QcMobBreeding.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/villages/QcVillageGolems.h"
#include "server/villages/QcVillagePopulation.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageFixture.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QJsonObject>
#include <QTest>
#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Village life without a server (decision E-415), on a fixture village (decision E-424):
// villagers get a child that moves into a free bed when two willing adults meet by day in dry
// weather, none without a free bed, at night, in rain, without food or beyond the limits; farmers
// share their produce; the child has no profession, plays instead of working and takes a
// workstation once grown up. Villages spawn iron golems by their population with a cooldown and the
// world's limit, and not at all while the content has no golem creature; they count the golems of
// every village towards the world's limit, forget a last golem after the world time went back and
// place one on the ground where its whole body fits. A villager asleep under a low roof breathes.
// Children survive saving and resting. Neither children nor golems come before the village's claims
// are reconciled, which happens when the last chunk around the village arrives, also when that
// chunk lies in a neighbouring region (the one test on a generated world: regions come from the
// world generator).
class TestVillageLife : public QObject
{
    Q_OBJECT

private:
    // The seed of the creatures' random numbers and of the generated world of the one test that
    // needs regions (claimsAreReconciledWhenTheLastChunkLiesInANeighbouringRegion).
    static constexpr qint64 seed = 42;
    // A share of the day by work time (QcVillagerSettings::routine) and at night.
    static constexpr double workPhase = 0.1;
    static constexpr double nightPhase = 0.7;

    struct Village
    {
        std::shared_ptr<QcContentRegistry> content;
        std::unique_ptr<QcServerWorld> world;
        std::unique_ptr<QcMobManager> mobs;
        std::shared_ptr<const QcVillagePlan> plan;
        quint32 nextId = 1;
        double worldTime = QcDayCycle::dayLength * 10.0;

        QcVillage &state() const { return *world->villages().find(plan->id); }
        std::function<quint32()> ids()
        {
            return [this] { return nextId++; };
        }
    };

    static std::shared_ptr<QcContentRegistry> loadContent()
    {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }

    // The base content with a placeholder golem (a copy of the pig that never spawns by itself) named in the golem settings.
    static std::shared_ptr<QcContentRegistry> contentWithGolem(int minVillagers, int villagersPerGolem, double cooldown)
    {
        std::shared_ptr<QcContentRegistry> content = loadContent();
        QcMobDefinition golem = content->mobs()[*content->mobIndex(u"qtcraft:pig"_s)];
        golem.name = u"test:golem"_s;
        golem.spawnWeight = 0.0;
        golem.extraSpawns.clear();
        content->registerMob(golem);
        QcVillageSettings settings = content->villageSettings();
        settings.golem = {u"test:golem"_s, minVillagers, villagersPerGolem, cooldown, 8};
        content->setVillageSettings(settings);
        return content;
    }

    // Loads the chunks of the village's plan and `ring` chunks around them, then populates the plan's
    // chunks (which reconciles the claims once every chunk around the village is there).
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

    // A fixture village of the plains style (decision E-424) with its chunks and the ring around them
    // loaded and its villagers moved in.
    static Village village(const std::shared_ptr<QcContentRegistry> &content, QcVillagerHomes::Limits limits = {})
    {
        Village result;
        result.content = content;
        result.plan = QcVillageFixture::plan(*content, {});
        if (!result.plan) {
            return result;
        }
        result.world = std::make_unique<QcServerWorld>(content, terrain(result));
        result.mobs = std::make_unique<QcMobManager>(content, seed);
        result.mobs->population().setVillages(&result.world->villages());
        result.mobs->population().setVillagerLimits(limits, {});
        loadAround(result, 2);
        return result;
    }

    // The flat ground with the village's fixture.
    static std::unique_ptr<QcWorldGenerator> terrain(const Village &village)
    {
        return std::make_unique<QcVillageFixture::Terrain>(village.content, QList{village.plan});
    }

    static QList<QcCreature *> villagersOf(Village &village)
    {
        QList<QcCreature *> list;
        for (QcCreature &mob : village.mobs->editableMobs()) {
            if (village.content->mobs()[mob.type].villager) {
                list.append(&mob);
            }
        }
        return list;
    }

    // Sets the time to `phase` of a day (0.1: work time).
    static void setPhase(Village &village, double phase) { village.worldTime = QcDayCycle::dayLength * (10.0 + phase); }

    // One tick (the world time moves on), with the breeding timers the server runs (QcMobBreeding:
    // young ones grow up, parents rest).
    static void tick(Village &village)
    {
        village.mobs->tick(*village.world, {}, village.worldTime, QcPhysics::tickDuration, village.ids());
        QcMobBreeding(village.content).tick(*village.world, *village.mobs, QcPhysics::tickDuration);
        village.mobs->villagers().takeActions();
        village.worldTime += QcPhysics::tickDuration;
    }

    static void ticks(Village &village, int count)
    {
        for (int i = 0; i < count; ++i) {
            tick(village);
        }
    }

    // Frees a bed (its villager leaves the world) and returns the place it stood beside it.
    static QcVec3 freeABed(Village &village)
    {
        QcCreature *leaving = villagersOf(village).constLast();
        const QcVec3 beside = leaving->motion.position;
        village.mobs->remove(leaving->id);
        return beside;
    }

    // The first two villagers stand at `spot` (the second at `other`, if given) at `phase` of the day
    // holding `food` produce, for one check of village life (they stay there until they gave their
    // food); the villagers afterwards.
    static qsizetype meet(Village &village, const QcVec3 &spot, double phase, int food = 3, std::optional<QcVec3> other = {})
    {
        setPhase(village, phase);
        const QList<quint32> parents{villagersOf(village).at(0)->id, villagersOf(village).at(1)->id};
        for (const quint32 id : parents) {
            village.mobs->find(id)->villager->produce = food;
        }
        for (int i = 0; i <= QcVillagePopulation::villageLifeTicks; ++i) {
            for (const quint32 id : parents) {
                QcCreature *parent = village.mobs->find(id);
                if (parent->villager->produce == food) {
                    parent->motion.position = other && id == parents[1] ? *other : spot;
                    parent->motion.velocity = {};
                }
            }
            tick(village);
        }
        return villagersOf(village).size();
    }

    // Sets every block of the box from `low` to `high` to `block`.
    static void fill(Village &village, QcBlockPos low, QcBlockPos high, QcBlockId block)
    {
        for (int x = low.x; x <= high.x; ++x) {
            for (int y = low.y; y <= high.y; ++y) {
                for (int z = low.z; z <= high.z; ++z) {
                    village.world->setBlock({x, y, z}, block);
                }
            }
        }
    }

    static QcBlockId block(const Village &village, const QString &name) { return *village.content->blockId(name); }

    static qsizetype golemCount(const Village &village)
    {
        const int golemType = *village.content->mobIndex(u"test:golem"_s);
        return std::ranges::count_if(village.mobs->mobs(), [golemType](const QcCreature &mob) { return mob.type == golemType; });
    }

    // The village saved and loaded into a new world: its villages.json (unless it was lost) and its
    // persistent mobs, with the chunks `ring` around the village loaded.
    static Village reloaded(Village &old, bool villagesKept = true, int ring = 2)
    {
        Village result;
        result.content = old.content;
        result.plan = old.plan;
        result.worldTime = old.worldTime;
        result.nextId = old.nextId;
        result.world = std::make_unique<QcServerWorld>(old.content, terrain(result));
        if (villagesKept) {
            result.world->villages().restore(old.world->villages().villages());
        }
        result.mobs = std::make_unique<QcMobManager>(old.content, seed);
        result.mobs->population().setVillages(&result.world->villages());
        result.mobs->restore(old.mobs->persistentMobs(), old.mobs->populatedChunks(), result.ids());
        loadAround(result, ring);
        // The server wakes them once it sees their chunks (QcEntities).
        for (QcCreature &mob : result.mobs->editableMobs()) {
            mob.dormant = false;
        }
        return result;
    }

    // The village of the region nearest to the origin, within `rings` regions.
    static std::shared_ptr<const QcVillagePlan> nearestPlan(const QcTerrainGenerator &generator, int rings)
    {
        for (int ring = 0; ring <= rings; ++ring) {
            for (int rz = -ring; rz <= ring; ++rz) {
                for (int rx = -ring; rx <= ring; ++rx) {
                    if (std::max(std::abs(rx), std::abs(rz)) != ring) {
                        continue;
                    }
                    if (std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz)) {
                        return plan;
                    }
                }
            }
        }
        return nullptr;
    }

    // A chunk from `low` to `high` that lies in another region than `home`.
    static std::optional<QcChunkPos> chunkInAnotherRegion(QcChunkPos low, QcChunkPos high, QcChunkPos home, int spacing)
    {
        const auto regionOf = [spacing](QcChunkPos pos) {
            return std::pair{qFloor(double(pos.x * QcWorld::chunkSize) / spacing), qFloor(double(pos.z * QcWorld::chunkSize) / spacing)};
        };
        for (int z = low.z; z <= high.z; ++z) {
            for (int x = low.x; x <= high.x; ++x) {
                if (regionOf({x, z}) != regionOf(home)) {
                    return QcChunkPos{x, z};
                }
            }
        }
        return std::nullopt;
    }

    static const QcCreature *youngest(Village &village)
    {
        const QList<QcCreature *> all = villagersOf(village);
        const auto young = std::ranges::find_if(all, [](const QcCreature *mob) { return mob->isYoung(); });
        return young == all.end() ? nullptr : *young;
    }

private slots:
    void twoWillingVillagersGetAChildInAFreeBed()
    {
        Village built = village(loadContent());
        QVERIFY(built.plan);
        const QcVec3 spot = freeABed(built);
        const qsizetype before = villagersOf(built).size();
        QCOMPARE(built.state().freeBeds(), 1);

        const QList<quint32> parents{villagersOf(built).at(0)->id, villagersOf(built).at(1)->id};
        QCOMPARE(meet(built, spot, workPhase), before + 1);
        QCOMPARE(built.state().freeBeds(), 0);
        QCOMPARE(built.state().population, int(before + 1));
        const QcCreature *child = youngest(built);
        QVERIFY(child);
        QCOMPARE(child->villager->profession, (*built.content->villagerSettings(u"qtcraft:villager"_s)).unemployed);
        QVERIFY(child->villager->home);
        QCOMPARE(QcVillage::spotAt(built.state().beds, *child->villager->home)->claimedBy, child->uid);
        QCOMPARE(child->villager->style, built.state().style);
        // The parents gave their food and rest.
        for (const quint32 id : parents) {
            QCOMPARE(built.mobs->find(id)->villager->produce, 0);
            QVERIFY(built.mobs->find(id)->breedCooldown > 0.0);
        }
    }

    void noChildWithoutABedFoodDayOrDryWeather()
    {
        // No free bed.
        Village full = village(loadContent());
        QVERIFY(full.plan);
        const qsizetype everyone = villagersOf(full).size();
        QCOMPARE(meet(full, villagersOf(full).at(0)->motion.position, workPhase), everyone);

        // At night.
        Village night = village(loadContent());
        QcVec3 spot = freeABed(night);
        qsizetype before = villagersOf(night).size();
        QCOMPARE(meet(night, spot, nightPhase), before);

        // In rain at the meeting point.
        Village rain = village(loadContent());
        rain.mobs->villagers().setRainCheck([](QcBlockPos) { return true; });
        spot = freeABed(rain);
        before = villagersOf(rain).size();
        QCOMPARE(meet(rain, spot, workPhase), before);

        // Without food.
        Village hungry = village(loadContent());
        spot = freeABed(hungry);
        before = villagersOf(hungry).size();
        QCOMPARE(meet(hungry, spot, workPhase, 2), before);
    }

    void childrenKeepToTheLimits()
    {
        Village perVillage = village(loadContent());
        QVERIFY(perVillage.plan);
        const QcVec3 spot = freeABed(perVillage);
        const qsizetype before = villagersOf(perVillage).size();
        perVillage.mobs->population().setVillagerLimits({before, 1024}, {});
        QCOMPARE(meet(perVillage, spot, workPhase), before);

        Village world = village(loadContent());
        const QcVec3 there = freeABed(world);
        world.mobs->population().setVillagerLimits({32, villagersOf(world).size()}, {});
        QCOMPARE(meet(world, there, workPhase), villagersOf(world).size());
    }

    void farmersShareTheirProduce()
    {
        std::shared_ptr<QcContentRegistry> content = loadContent();
        QcVillagerBreeding breeding(content);
        QList<QcCreature> mobs(2);
        for (QcCreature &mob : mobs) {
            mob.type = *content->mobIndex(u"qtcraft:villager"_s);
            mob.villager.emplace();
            mob.villager->village = 7;
        }
        mobs[0].villager->profession = u"qtcraft:farmer"_s;
        mobs[0].villager->produce = 10;
        mobs[1].villager->profession = (*content->villagerSettings(u"qtcraft:villager"_s)).unemployed;
        mobs[1].motion.position = {1.0, 0.0, 0.0};

        // No village of id 7 is known: no child, but the farmer shares all the same.
        QcServerWorld world(content, std::make_unique<QcVillageFixture::Terrain>(content, QList<std::shared_ptr<const QcVillagePlan>>{}));
        QVERIFY(breeding.check(mobs, {world, world.villages(), 0.0, {}, {}}).isEmpty());
        QCOMPARE(mobs[1].villager->produce, 3);
        QCOMPARE(mobs[0].villager->produce, 7);
        QVERIFY(breeding.willing(mobs[1]));
        // It gives only what it holds above shareAbove (6).
        mobs[1].villager->produce = 0;
        QVERIFY(breeding.check(mobs, {world, world.villages(), 0.0, {}, {}}).isEmpty());
        QCOMPARE(mobs[1].villager->produce, 1);
        QCOMPARE(mobs[0].villager->produce, 6);
    }

    void aChildPlaysAndTakesAWorkstationOnceGrownUp()
    {
        Village built = village(loadContent());
        QVERIFY(built.plan);
        const QcVec3 spot = freeABed(built);
        meet(built, spot, workPhase);
        const QcCreature *child = youngest(built);
        QVERIFY(child);
        const quint32 childId = child->id;

        // Everybody else leaves: every workstation is free, yet the child takes none.
        const QList<QcCreature *> all = villagersOf(built);
        for (const QcCreature *mob : all) {
            if (mob->id != childId) {
                built.mobs->remove(mob->id);
            }
        }
        ticks(built, 3 * QcVillagerBehaviour::decideTicks);
        child = built.mobs->find(childId);
        QVERIFY(child->isYoung());
        QVERIFY(!child->villager->work);
        QCOMPARE(child->villager->profession, (*built.content->villagerSettings(u"qtcraft:villager"_s)).unemployed);
        QCOMPARE(child->villager->activity, QcVillagerActivity::Wander);

        // Grown up, it takes a workstation and its profession.
        built.mobs->find(childId)->growUp = 0.2;
        ticks(built, 3 * QcVillagerBehaviour::decideTicks);
        child = built.mobs->find(childId);
        QVERIFY(!child->isYoung());
        QVERIFY(child->villager->work);
        QVERIFY(child->villager->profession != (*built.content->villagerSettings(u"qtcraft:villager"_s)).unemployed);
        QCOMPARE(child->villager->activity, QcVillagerActivity::Work);
    }

    void aVillageSpawnsGolemsByItsPopulation()
    {
        // The threshold: one villager short, no golem.
        Village built = village(contentWithGolem(1, 1, 300.0));
        QVERIFY(built.plan);
        setPhase(built, workPhase);
        const int population = built.state().population;
        QcVillageSettings settings = built.content->villageSettings();
        settings.golem.minVillagers = population + 1;
        built.content->setVillageSettings(settings);
        const int golemType = *built.content->mobIndex(u"test:golem"_s);
        const auto golems = [&] {
            return std::ranges::count_if(built.mobs->mobs(), [golemType](const QcCreature &mob) { return mob.type == golemType; });
        };
        ticks(built, QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 0);

        // At it, one golem per `population` villagers: one, near the meeting point, counted.
        settings.golem.minVillagers = population;
        settings.golem.villagersPerGolem = population;
        built.content->setVillageSettings(settings);
        ticks(built, 3 * QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 1);
        QCOMPARE(built.state().golems, 1);
        QVERIFY(built.state().lastGolem >= 0.0);
        const QcCreature &golem = *std::ranges::find_if(built.mobs->mobs(), [golemType](const QcCreature &mob) { return mob.type == golemType; });
        const QcBlockPos centre = built.state().centre;
        QVERIFY(std::hypot(golem.motion.position.x - centre.x, golem.motion.position.z - centre.z) <= 9.5);
        QVERIFY(golem.persistent);

        // A golem gone leaves room for the next one, after the cooldown.
        built.mobs->remove(golem.id);
        ticks(built, QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 0);
        QCOMPARE(built.state().golems, 0);
        built.worldTime += QcDayCycle::dayLength;
        ticks(built, QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 1);
    }

    void golemsKeepToTheCooldownAndTheWorldLimit()
    {
        // One golem per villager, so the population allows many.
        Village built = village(contentWithGolem(1, 1, 300.0));
        QVERIFY(built.plan);
        setPhase(built, workPhase);
        const int golemType = *built.content->mobIndex(u"test:golem"_s);
        const auto golems = [&] {
            return std::ranges::count_if(built.mobs->mobs(), [golemType](const QcCreature &mob) { return mob.type == golemType; });
        };
        ticks(built, 5 * QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 1);

        built.worldTime += QcDayCycle::dayLength;
        ticks(built, QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 2);

        built.mobs->population().setGolemLimit(2);
        built.worldTime += QcDayCycle::dayLength;
        ticks(built, QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 2);

        // Natural spawning off: no golems.
        built.mobs->population().setGolemLimit(256);
        built.mobs->setNaturalSpawning(false);
        built.worldTime += QcDayCycle::dayLength;
        ticks(built, QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golems(), 2);
    }

    void withoutTheGolemCreatureTheRuleRests()
    {
        // The base game names "iron_golem", which arrives later.
        Village built = village(loadContent());
        QVERIFY(built.plan);
        QCOMPARE(built.content->golemOfStyle(built.state().style).mob, u"qtcraft:iron_golem"_s);
        QVERIFY(!built.mobs->population().golems().kind(built.state().style));
        QcVillageSettings settings = built.content->villageSettings();
        settings.golem.minVillagers = 1;
        settings.golem.villagersPerGolem = 1;
        built.content->setVillageSettings(settings);
        const qsizetype before = built.mobs->mobs().size();
        ticks(built, 2 * QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(built.mobs->mobs().size(), before);
        QCOMPARE(built.state().golems, 0);
    }

    void aSleepingVillagerBreathesUnderALowRoof()
    {
        // An igloo's bed has two blocks of air above it: a villager lying in it has its eyes inside
        // the bed's cells, not a standing height above it in the roof.
        std::shared_ptr<QcContentRegistry> content = loadContent();
        const QcMobDefinition &definition = content->mobs()[*content->mobIndex(u"qtcraft:villager"_s)];
        QcCreature mob;
        mob.motion.position = {10.5, 65.5625, 20.25};
        QVERIFY(QcEnvironmentDamage::eyesOf(mob, definition).y > 66.5);
        mob.pose = QcPose::Lying;
        const QcVec3 eyes = QcEnvironmentDamage::eyesOf(mob, definition);
        QVERIFY2(eyes.y < 66.0, qPrintable(QString::number(eyes.y)));
        QVERIFY(std::abs(eyes.x - 10.5) < 2.0 && std::abs(eyes.z - 20.25) < 2.0);
    }

    void villageStateKeepsTheLastGolem()
    {
        QcVillage village;
        village.id = 3;
        village.maxX = 10;
        village.maxY = 10;
        village.maxZ = 10;
        village.lastGolem = 1234.5;
        QCOMPARE(QcVillage::fromJson(village.toJson(), {})->lastGolem, 1234.5);
        village.lastGolem = -1.0;
        QVERIFY(!village.toJson().contains(u"lastGolem"_s));
        QJsonObject damaged = village.toJson();
        damaged.insert(u"lastGolem"_s, 1e300);
        QCOMPARE(QcVillage::fromJson(damaged, {})->lastGolem, -1.0);
    }

    void aVillagerWithoutABedKeepsTheFreeOneFromChildren()
    {
        // Two beds, one free: with two villagers the free bed belongs to the one whose bed was
        // broken, so no child comes; with one villager it does.
        std::shared_ptr<QcContentRegistry> content = loadContent();
        QcVillagerBreeding breeding(content);
        QcServerWorld world(content, std::make_unique<QcVillageFixture::Terrain>(content, QList<std::shared_ptr<const QcVillagePlan>>{}));
        QcVillage village;
        village.beds = {{{0, 64, 0}, {}, 1}, {{4, 64, 0}, {}, 0}};
        village.population = 2;
        const QcVillagerBreeding::Context context{world, world.villages(), QcDayCycle::dayLength * (10.0 + workPhase), {}, {}};
        QCOMPARE(village.freeBeds(), 1);
        QVERIFY(!breeding.breedsNow(village, context, 2));
        village.population = 1;
        QVERIFY(breeding.breedsNow(village, context, 1));
    }

    void noLifeBeforeTheClaimsAreReconciled()
    {
        // villages.json was lost: the village comes back from its plan without claims, and only the
        // chunks of its box are loaded, so villagers beyond them could still hold beds. No child and
        // no golem until every chunk around the village is there (decision E-416).
        Village built = village(contentWithGolem(1, 1, 300.0));
        QVERIFY(built.plan);
        const QcVec3 spot = freeABed(built);
        const qsizetype before = villagersOf(built).size();
        Village lost = reloaded(built, false, 0);
        QCOMPARE(villagersOf(lost).size(), before);
        QCOMPARE(meet(lost, spot, workPhase), before);
        ticks(lost, 2 * QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(villagersOf(lost).size(), before);
        QCOMPARE(golemCount(lost), 0);

        // The ring around it loads: the claims are reconciled, life goes on.
        loadAround(lost, 2);
        QCOMPARE(meet(lost, spot, workPhase), before + 1);
        ticks(lost, 3 * QcVillagePopulation::villageLifeTicks + 1);
        QVERIFY(golemCount(lost) >= 1);
    }

    void claimsAreReconciledWhenTheLastChunkLiesInANeighbouringRegion()
    {
        // Regions of 128 blocks: the chunks around a village reach into the neighbouring regions.
        // Loading the last of them (one in another region) reconciles the village's claims.
        std::shared_ptr<QcContentRegistry> content = loadContent();
        QcVillageSettings settings = content->villageSettings();
        settings.spacing = 128;
        settings.radius = 48;
        content->setVillageSettings(settings);
        auto generator = std::make_unique<QcTerrainGenerator>(content, seed);
        const int spacing = generator->villages().spacing();
        QCOMPARE(spacing, 128);
        const std::shared_ptr<const QcVillagePlan> found = nearestPlan(*generator, 14);
        QVERIFY(found);
        const QcVillagePlan &plan = *found;
        const int around = QcVillage::reach + QcVillagerHomes::strayReach;
        const QcChunkPos low = QcBlockPos{plan.minX - around, 0, plan.minZ - around}.chunk();
        const QcChunkPos high = QcBlockPos{plan.maxX + around, 0, plan.maxZ + around}.chunk();
        const std::optional<QcChunkPos> last = chunkInAnotherRegion(low, high, plan.centre.chunk(), spacing);
        QVERIFY(last);

        QcServerWorld world(content, std::move(generator));
        QcMobManager mobs(content, seed);
        mobs.population().setVillages(&world.villages());
        quint32 nextId = 1;
        const auto ids = [&nextId] { return nextId++; };
        for (int z = low.z; z <= high.z; ++z) {
            for (int x = low.x; x <= high.x; ++x) {
                if (QcChunkPos{x, z} != *last) {
                    world.loadChunk({x, z});
                    mobs.populateChunk(world, {x, z}, ids);
                }
            }
        }
        QVERIFY(world.villages().find(plan.id));
        QVERIFY(!mobs.population().claimsReconciled(plan.id));

        world.loadChunk(*last);
        mobs.populateChunk(world, *last, ids);
        QVERIFY(mobs.population().claimsReconciled(plan.id));
    }

    void noChildThroughAWall()
    {
        // High above the village, two blocks apart: they see each other and get a child; with a
        // wall between them they do not.
        for (const bool wall : {false, true}) {
            Village built = village(loadContent());
            QVERIFY(built.plan);
            freeABed(built);
            const qsizetype before = villagersOf(built).size();
            const QcBlockPos at = built.state().centre.offset(0, 40, 0);
            const QcVec3 spot{at.x + 0.5, double(at.y), at.z + 0.5};
            if (wall) {
                fill(built, at.offset(1, -1, -3), at.offset(1, 3, 3), block(built, u"qtcraft:stone"_s));
            }
            QCOMPARE(meet(built, spot, workPhase, 3, spot + QcVec3{2.0, 0.0, 0.0}), wall ? before : before + 1);
        }
    }

    void aChildSurvivesSavingAndResting()
    {
        Village built = village(loadContent());
        QVERIFY(built.plan);
        meet(built, freeABed(built), workPhase);
        const QcCreature *child = youngest(built);
        QVERIFY(child);
        const quint64 uid = child->uid;
        const QcBlockPos home = *child->villager->home;
        const double growUp = child->growUp;
        const int population = built.state().population;

        // Saved and loaded: still a child in its bed.
        Village back = reloaded(built);
        ticks(back, 2 * QcVillagePopulation::villageLifeTicks);
        child = youngest(back);
        QVERIFY(child);
        QCOMPARE(child->uid, uid);
        QCOMPARE(*child->villager->home, home);
        QVERIFY(child->growUp > growUp - 20.0 && child->growUp < growUp);
        QCOMPARE(QcVillage::spotAt(back.state().beds, home)->claimedBy, uid);
        QCOMPARE(back.state().population, population);

        // Resting while its chunk is not loaded: it neither grows up nor loses its bed.
        const quint32 id = child->id;
        back.mobs->find(id)->dormant = true;
        const double resting = back.mobs->find(id)->growUp;
        ticks(back, 2 * QcVillagePopulation::villageLifeTicks);
        QCOMPARE(back.mobs->find(id)->growUp, resting);
        QCOMPARE(QcVillage::spotAt(back.state().beds, home)->claimedBy, uid);
        back.mobs->find(id)->dormant = false;
        ticks(back, QcVillagerBehaviour::decideTicks);
        QVERIFY(back.mobs->find(id)->isYoung());
        QCOMPARE(QcVillage::spotAt(back.state().beds, home)->claimedBy, uid);
    }

    void golemsCountTowardsTheWorldInEveryVillage()
    {
        // A village not loaded holds three golems; one stands outside every village: four in the
        // world. Five loaded in the village's area count instead of its three.
        std::shared_ptr<QcContentRegistry> content = contentWithGolem(1, 1, 300.0);
        QcServerWorld world(content, std::make_unique<QcVillageFixture::Terrain>(content, QList<std::shared_ptr<const QcVillagePlan>>{}));
        QcVillage village;
        village.id = 3;
        village.maxX = 10;
        village.maxY = 80;
        village.maxZ = 10;
        village.golems = 3;
        world.villages().restore({village});
        const int type = *content->mobIndex(u"test:golem"_s);
        const QcVillageGolems golems(content);
        QList<QcCreature> mobs(1);
        mobs[0].type = type;
        mobs[0].motion.position = {500.5, 64.0, 500.5};
        QCOMPARE(golems.golemsInWorld(mobs, type, world.villages()), 4);
        for (int i = 0; i < 5; ++i) {
            QcCreature golem;
            golem.type = type;
            golem.motion.position = {5.5 + i, 64.0, 5.5};
            mobs.append(golem);
        }
        QCOMPARE(golems.golemsInWorld(mobs, type, world.villages()), 6);
    }

    void theGolemCooldownSurvivesTimeGoingBack()
    {
        Village built = village(contentWithGolem(1, 1, 300.0));
        QVERIFY(built.plan);
        setPhase(built, workPhase);
        const int population = built.state().population;
        QcVillageSettings settings = built.content->villageSettings();
        settings.golem.minVillagers = population;
        settings.golem.villagersPerGolem = population;
        built.content->setVillageSettings(settings);
        ticks(built, 3 * QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golemCount(built), 1);

        // /time set a day back: the last golem lies in the future, yet the next one comes.
        const int golemType = *built.content->mobIndex(u"test:golem"_s);
        built.mobs->remove(std::ranges::find_if(built.mobs->mobs(), [golemType](const QcCreature &mob) { return mob.type == golemType; })->id);
        built.worldTime -= QcDayCycle::dayLength;
        QcVillage future = built.state();
        future.lastGolem = built.worldTime + QcDayCycle::dayLength;
        QVERIFY(built.mobs->population().golems().due(future, 0, built.worldTime));
        ticks(built, QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(golemCount(built), 1);
        QVERIFY(built.state().lastGolem <= built.worldTime);
    }

    void aGolemStandsOnTheGroundWhereItsBodyFits()
    {
        // A wide golem (1.4 by 2.7) on an open stone floor around the meeting point.
        std::shared_ptr<QcContentRegistry> content = contentWithGolem(1, 1, 300.0);
        QcMobDefinition wide = content->mobs()[*content->mobIndex(u"test:golem"_s)];
        wide.width = 1.4;
        wide.height = 2.7;
        content->registerMob(wide);
        Village built = village(content);
        QVERIFY(built.plan);
        const int type = *content->mobIndex(u"test:golem"_s);
        const QcBlockPos centre = built.state().centre;
        const QcVillageGolems::Context context{*built.world, built.mobs->paths().cells(), built.worldTime, seed, 256};
        const QcBlockId stone = block(built, u"qtcraft:stone"_s);
        fill(built, centre.offset(-10, 0, -10), centre.offset(10, 10, 10), qcAirBlockId);
        fill(built, centre.offset(-10, -1, -10), centre.offset(10, -1, 10), stone);

        // Pillars on every other column, too high to stand on: each column between them is free,
        // but the body does not fit.
        for (int dx = -10; dx <= 10; ++dx) {
            for (int dz = -10; dz <= 10; ++dz) {
                if ((dx + dz) % 2 == 0) {
                    fill(built, centre.offset(dx, 0, dz), centre.offset(dx, 9, dz), stone);
                }
            }
        }
        QVERIFY(!built.mobs->population().golems().placeFor(built.state(), type, context));

        // Without them, under a roof: on the ground, not on the roof.
        fill(built, centre.offset(-10, 0, -10), centre.offset(10, 10, 10), qcAirBlockId);
        fill(built, centre.offset(-10, 4, -10), centre.offset(10, 4, 10), stone);
        const std::optional<QcVec3> at = built.mobs->population().golems().placeFor(built.state(), type, context);
        QVERIFY(at);
        QCOMPARE(at->y, double(centre.y));
        QVERIFY(!QcBodyFit::overlapsBlocks(*built.world, *at, {wide.width, wide.height}, QcBodyFit::Unloaded::Blocked));

        // A floor of magma hurts: no place.
        fill(built, centre.offset(-10, 0, -10), centre.offset(10, 10, 10), qcAirBlockId);
        fill(built, centre.offset(-10, -1, -10), centre.offset(10, -1, 10), block(built, u"qtcraft:magma_block"_s));
        QVERIFY(!built.mobs->population().golems().placeFor(built.state(), type, context));
    }
};

QTEST_GUILESS_MAIN(TestVillageLife)
#include "test_village_life.moc"
