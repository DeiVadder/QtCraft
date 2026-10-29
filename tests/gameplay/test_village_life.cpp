// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcBodyFit.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/mobs/QcMobBreeding.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathPlanner.h"
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
// workstation once grown up (iron golems: test_golem_summons). A village keeps its last golem's time.
// A villager asleep under a low roof breathes.
// Children survive saving and resting. No children come before the village's claims
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
            return std::pair{qFloor(static_cast<double>(pos.x * QcWorld::chunkSize) / spacing),
                qFloor(static_cast<double>(pos.z * QcWorld::chunkSize) / spacing)};
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
        Village built = village(QcTestUtil::loadContent());
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
        QCOMPARE(child->villager->profession, (*built.content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)).unemployed);
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
        Village full = village(QcTestUtil::loadContent());
        QVERIFY(full.plan);
        const qsizetype everyone = villagersOf(full).size();
        QCOMPARE(meet(full, villagersOf(full).at(0)->motion.position, workPhase), everyone);

        // At night.
        Village night = village(QcTestUtil::loadContent());
        QcVec3 spot = freeABed(night);
        qsizetype before = villagersOf(night).size();
        QCOMPARE(meet(night, spot, nightPhase), before);

        // In rain at the meeting point.
        Village rain = village(QcTestUtil::loadContent());
        rain.mobs->villagers().setRainCheck([](QcBlockPos) { return true; });
        spot = freeABed(rain);
        before = villagersOf(rain).size();
        QCOMPARE(meet(rain, spot, workPhase), before);

        // Without food.
        Village hungry = village(QcTestUtil::loadContent());
        spot = freeABed(hungry);
        before = villagersOf(hungry).size();
        QCOMPARE(meet(hungry, spot, workPhase, 2), before);
    }

    void childrenKeepToTheLimits()
    {
        Village perVillage = village(QcTestUtil::loadContent());
        QVERIFY(perVillage.plan);
        const QcVec3 spot = freeABed(perVillage);
        const qsizetype before = villagersOf(perVillage).size();
        perVillage.mobs->population().setVillagerLimits({before, 1024}, {});
        QCOMPARE(meet(perVillage, spot, workPhase), before);

        Village world = village(QcTestUtil::loadContent());
        const QcVec3 there = freeABed(world);
        world.mobs->population().setVillagerLimits({32, villagersOf(world).size()}, {});
        QCOMPARE(meet(world, there, workPhase), villagersOf(world).size());
    }

    void farmersShareTheirProduce()
    {
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcVillagerBreeding breeding(content);
        QList<QcCreature> mobs(2);
        for (QcCreature &mob : mobs) {
            mob.type = *content->mobIndex(u"qtcraft:villager"_s);
            mob.villager.emplace();
            mob.villager->village = 7;
        }
        mobs[0].villager->profession = u"qtcraft:farmer"_s;
        mobs[0].villager->produce = 10;
        mobs[1].villager->profession = (*content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)).unemployed;
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
        Village built = village(QcTestUtil::loadContent());
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
        QCOMPARE(child->villager->profession, (*built.content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)).unemployed);
        QCOMPARE(child->villager->activity, QcVillagerActivity::Wander);

        // Grown up, it takes a workstation and its profession.
        built.mobs->find(childId)->growUp = 0.2;
        ticks(built, 3 * QcVillagerBehaviour::decideTicks);
        child = built.mobs->find(childId);
        QVERIFY(!child->isYoung());
        QVERIFY(child->villager->work);
        QVERIFY(child->villager->profession != (*built.content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)).unemployed);
        QCOMPARE(child->villager->activity, QcVillagerActivity::Work);
    }

    void aSleepingVillagerBreathesUnderALowRoof()
    {
        // An igloo's bed has two blocks of air above it: a villager lying in it has its eyes inside
        // the bed's cells, not a standing height above it in the roof.
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
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
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
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
        // chunks of its box are loaded, so villagers beyond them could still hold beds. No child
        // until every chunk around the village is there (decision E-416; golems: test_golem_summons).
        Village built = village(QcTestUtil::loadContent());
        QVERIFY(built.plan);
        const QcVec3 spot = freeABed(built);
        const qsizetype before = villagersOf(built).size();
        Village lost = reloaded(built, false, 0);
        QCOMPARE(villagersOf(lost).size(), before);
        QCOMPARE(meet(lost, spot, workPhase), before);
        ticks(lost, 2 * QcVillagePopulation::villageLifeTicks + 1);
        QCOMPARE(villagersOf(lost).size(), before);

        // The ring around it loads: the claims are reconciled, life goes on.
        loadAround(lost, 2);
        QCOMPARE(meet(lost, spot, workPhase), before + 1);
    }

    void claimsAreReconciledWhenTheLastChunkLiesInANeighbouringRegion()
    {
        // Regions of 128 blocks: the chunks around a village reach into the neighbouring regions.
        // Loading the last of them (one in another region) reconciles the village's claims.
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcVillageSettings settings = content->villageCatalog().settings();
        settings.spacing = 128;
        settings.radius = 48;
        content->villageCatalog().setSettings(settings);
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
            Village built = village(QcTestUtil::loadContent());
            QVERIFY(built.plan);
            freeABed(built);
            const qsizetype before = villagersOf(built).size();
            const QcBlockPos at = built.state().centre.offset(0, 40, 0);
            const QcVec3 spot{at.x + 0.5, static_cast<double>(at.y), at.z + 0.5};
            if (wall) {
                fill(built, at.offset(1, -1, -3), at.offset(1, 3, 3), block(built, u"qtcraft:stone"_s));
            }
            QCOMPARE(meet(built, spot, workPhase, 3, spot + QcVec3{2.0, 0.0, 0.0}), wall ? before : before + 1);
        }
    }

    void aChildSurvivesSavingAndResting()
    {
        Village built = village(QcTestUtil::loadContent());
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

};

QTEST_GUILESS_MAIN(TestVillageLife)
#include "test_village_life.moc"
