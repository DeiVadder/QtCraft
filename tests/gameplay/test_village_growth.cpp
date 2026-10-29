// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/animals/QcMobBreeding.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcMobNavigator.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/villages/QcVillagePopulation.h"
#include "server/villages/QcVillagerRelocation.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageFixture.h"

#include <QTest>

#include <algorithm>
#include <tuple>

using namespace Qt::StringLiterals;

// Villages that grow and move (decision E-490), without a server, on fixture villages (decision
// E-424): a bed placed near a village joins it and widens its area, one placed away from every
// village founds a settlement, which homeless villagers move into and breed in like in any village
// and which is forgotten once it has neither spots nor villagers; breaking a claimed bed frees the
// claim. Villages growing towards each other stop before their areas meet, and a spot where
// generated areas overlap belongs to one village only. A villager carried away from its village
// moves into the nearest village with a free bed, a homeless one takes a free bed near it (a lapsed
// claim's too, also one that was away when its village was reconciled, which leaves the population
// only once), but not one of a village it stands beyond. The village of region (0, 0) is a village
// like any other. While other creatures wait for a path, villagers leave them a quarter of the path
// budget, but the first villager search of a tick always runs.
class TestVillageGrowth : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;
    static constexpr double workPhase = 0.1;

    struct World
    {
        std::shared_ptr<QcContentRegistry> content;
        QList<std::shared_ptr<const QcVillagePlan>> plans;
        std::unique_ptr<QcServerWorld> world;
        std::unique_ptr<QcMobManager> mobs;
        quint32 nextId = 1;
        double worldTime = QcDayCycle::dayLength * (10.0 + workPhase);

        std::function<quint32()> ids()
        {
            return [this] { return nextId++; };
        }
        QcVillages &villages() const { return world->villages(); }
        QcVillage &village(int index) const { return *villages().find(plans.at(index)->id); }
        int villagerType() const { return *content->mobIndex(u"qtcraft:villager"_s); }
    };

    // `count` plains fixture villages side by side (ids `firstId`, `firstId` + 1, …) with their
    // chunks and those around loaded and their villagers moved in.
    static World villages(int count, qint64 firstId = 1)
    {
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QStringList styles;
        for (int i = 0; i < count; ++i) {
            styles.append(u"qtcraft:plains"_s);
        }
        QcVillageFixture::Options base;
        base.id = firstId;
        return worldOf(content, QcVillageFixture::spread(*content, styles, count, base));
    }

    // A world of the fixture villages `plans` like villages().
    static World worldOf(const std::shared_ptr<QcContentRegistry> &content, const QList<std::shared_ptr<const QcVillagePlan>> &plans)
    {
        World result;
        result.content = content;
        result.plans = plans;
        result.world = std::make_unique<QcServerWorld>(result.content, std::make_unique<QcVillageFixture::Terrain>(result.content, result.plans));
        result.mobs = std::make_unique<QcMobManager>(result.content, seed);
        result.mobs->population().setVillages(&result.world->villages());
        result.mobs->population().setVillagerLimits({}, {});
        const int ring = (QcVillage::reach + QcVillagerHomes::strayReach + QcWorld::chunkSize - 1) / QcWorld::chunkSize;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(result.plans)) {
            const QList<QcChunkPos> around = QcVillageFixture::chunksOf(*plan, ring);
            for (const QcChunkPos &chunk : around) {
                result.world->loadChunk(chunk);
            }
        }
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(result.plans)) {
            const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*plan);
            for (const QcChunkPos &chunk : chunks) {
                result.mobs->populateChunk(*result.world, chunk, result.ids());
            }
        }
        return result;
    }

    static void tick(World &world)
    {
        world.mobs->tick(*world.world, {}, world.worldTime, QcPhysics::tickDuration, world.ids());
        QcMobBreeding(world.content).tick(*world.world, *world.mobs, QcPhysics::tickDuration);
        world.mobs->villagers().takeActions();
        world.worldTime += QcPhysics::tickDuration;
    }

    // One check of village life (relocation, children, golems) and a little more.
    static void lifeCheck(World &world)
    {
        for (int i = 0; i <= QcVillagePopulation::villageLifeTicks; ++i) {
            tick(world);
        }
    }

    static QcBlockId state(const World &world, const QString &name) { return *world.content->stateId(name); }

    // A bed lying north on the fixture's ground with its foot at (x, z); returns the head.
    static QcBlockPos bed(World &world, int x, int z)
    {
        const QcBlockPos foot{x, QcVillageFixture::landFloor + 1, z};
        world.world->setBlock(foot, state(world, u"qtcraft:bed[facing=north,part=foot]"_s));
        world.world->setBlock(foot.offset(0, 0, -1), state(world, u"qtcraft:bed[facing=north,part=head]"_s));
        return foot.offset(0, 0, -1);
    }

    static void breakBed(World &world, QcBlockPos head)
    {
        world.world->setBlock(head, qcAirBlockId);
        world.world->setBlock(head.offset(0, 0, 1), qcAirBlockId);
    }

    static void loadAround(World &world, QcBlockPos centre, int chunks)
    {
        for (int dx = -chunks; dx <= chunks; ++dx) {
            for (int dz = -chunks; dz <= chunks; ++dz) {
                world.world->loadChunk({centre.chunk().x + dx, centre.chunk().z + dz});
            }
        }
    }

    static QList<QcCreature *> villagersOf(World &world, qint64 village)
    {
        QList<QcCreature *> list;
        for (QcCreature &mob : world.mobs->editableMobs()) {
            if (mob.isVillager() && mob.villager->village == village) {
                list.append(&mob);
            }
        }
        return list;
    }

    // A villager without a village (summoned) standing at `at`.
    static QcCreature &homeless(World &world, const QcVec3 &at)
    {
        const quint32 id = world.nextId++;
        world.mobs->spawn(world.villagerType(), at, id);
        return *world.mobs->find(id);
    }

    // The server starts again (a new run: no village reconciled yet) with the villagers as saved,
    // except the one with the persistent id `away`, which stays in a chunk not loaded; returns its
    // record.
    static QcMobManager::Stored newRun(World &world, quint64 away)
    {
        QList<QcMobManager::Stored> stored;
        QcMobManager::Stored left;
        for (const QcCreature &mob : world.mobs->mobs()) {
            (mob.uid == away ? left : stored.emplaceBack()) = QcMobManager::storedOf(mob);
        }
        const QSet<QcChunkPos> populated = world.mobs->populatedChunks();
        const quint64 nextUid = world.mobs->nextUid();
        world.mobs = std::make_unique<QcMobManager>(world.content, seed);
        world.mobs->population().setVillages(&world.world->villages());
        world.mobs->population().setVillagerLimits({}, {});
        world.mobs->setNextUid(nextUid);
        world.mobs->restore(stored, populated, world.ids());
        // Their chunks are loaded (the server wakes them, QcEntities).
        for (QcCreature &mob : world.mobs->editableMobs()) {
            mob.dormant = false;
        }
        return left;
    }

    static QcVec3 above(QcBlockPos ground) { return {ground.x + 0.5, static_cast<double>(ground.y + 1), ground.z + 0.5}; }

    // A free spot on the fixture's ground well away from every village.
    static QcBlockPos farAway(const World &world)
    {
        int east = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : world.plans) {
            east = std::max(east, plan->maxX);
        }
        return {east + 160, QcVillageFixture::landFloor, world.plans.first()->centre.z};
    }

private slots:
    void aBedNearAVillageJoinsItAndWidensItsArea()
    {
        World world = villages(1);
        QcVillage &village = world.village(0);
        const qsizetype beds = village.beds.size();
        const int east = village.maxX;
        // Beyond the area (the box and its reach) but within joinDistance of the box.
        const int x = east + QcVillage::reach + 8;
        QVERIFY(x - east <= QcVillages::joinDistance);
        loadAround(world, {x, 0, village.centre.z}, 1);
        const QcBlockPos head = bed(world, x, village.centre.z);

        QCOMPARE(world.villages().count(), 1);
        QCOMPARE(village.beds.size(), beds + 1);
        QCOMPARE(village.maxX, x);
        QVERIFY(village.contains(head));
        QCOMPARE(world.villages().at(head), &village);
        // The generated plan stays the village's plan.
        QCOMPARE(world.villages().planOf(village.id), world.plans.first());

        // Its villagers breed into it: the new bed is free and more beds than villagers.
        QVERIFY(village.freeBeds() > 0);
        QVERIFY(village.population < village.beds.size());
    }

    void aBedAwayFromVillagesFoundsASettlementThatHomelessVillagersMoveInto()
    {
        World world = villages(1);
        const QcBlockPos ground = farAway(world);
        loadAround(world, ground, 2);
        const QList<QcBlockPos> heads{bed(world, ground.x, ground.z), bed(world, ground.x + 3, ground.z), bed(world, ground.x + 6, ground.z)};
        QCOMPARE(world.villages().count(), 2);
        const QcVillage *settlement = world.villages().at(heads.first());
        QVERIFY(settlement);
        QVERIFY(settlement->settlement());
        QCOMPARE(settlement->id, QcVillage::settlementId(heads.first()));
        QVERIFY(!QcVillage::isSettlement(world.plans.first()->id));
        QCOMPARE(settlement->beds.size(), 3);
        QCOMPARE(world.villages().planOf(settlement->id), nullptr);

        // Two homeless villagers nearby move in and claim beds.
        const quint64 first = homeless(world, above(ground.offset(2, 0, 3))).uid;
        const quint64 second = homeless(world, above(ground.offset(4, 0, 3))).uid;
        lifeCheck(world);
        const QList<QcCreature *> settlers = villagersOf(world, settlement->id);
        QCOMPARE(settlers.size(), 2);
        QCOMPARE(world.villages().find(settlement->id)->population, 2);
        for (const QcCreature *settler : settlers) {
            QVERIFY(settler->uid == first || settler->uid == second);
            QVERIFY(settler->villager->home.has_value());
        }

        // Fed, they get a child into the third bed like in any village.
        for (int i = 0; i <= QcVillagePopulation::villageLifeTicks * 2 && villagersOf(world, settlement->id).size() < 3; ++i) {
            const QList<QcCreature *> fed = villagersOf(world, settlement->id);
            for (QcCreature *settler : fed) {
                settler->villager->produce = std::max(settler->villager->produce, 3);
                settler->motion.position = above(ground.offset(3, 0, 3));
                settler->motion.velocity = {};
            }
            tick(world);
        }
        QCOMPARE(villagersOf(world, settlement->id).size(), 3);
        QCOMPARE(world.villages().find(settlement->id)->freeBeds(), 0);
    }

    void breakingAClaimedBedFreesTheClaimAndAnEmptySettlementIsForgotten()
    {
        World world = villages(1);
        const QcBlockPos ground = farAway(world);
        loadAround(world, ground, 2);
        const QcBlockPos head = bed(world, ground.x, ground.z);
        const qint64 id = QcVillage::settlementId(head);
        const quint32 settler = homeless(world, above(ground.offset(2, 0, 2))).id;
        lifeCheck(world);
        QCOMPARE(world.mobs->find(settler)->villager->home, std::optional(head));

        breakBed(world, head);
        QVERIFY(world.villages().find(id));
        QVERIFY(world.villages().find(id)->beds.isEmpty());
        for (int i = 0; i < QcVillagerBehaviour::decideTicks * 2; ++i) {
            tick(world);
        }
        QVERIFY(!world.mobs->find(settler)->villager->home.has_value());

        // Its last villager gone, the settlement without spots is forgotten.
        world.mobs->remove(settler);
        QVERIFY(!world.villages().find(id));
        QCOMPARE(world.villages().count(), 1);
    }

    void aVillagerCarriedAwayMovesIntoTheNearestVillageWithAFreeBed()
    {
        World world = villages(2);
        QcVillage &from = world.village(0);
        QcVillage &to = world.village(1);
        // A bed of the second village (the one farthest from the first) becomes free.
        QList<QcCreature *> others = villagersOf(world, to.id);
        std::ranges::sort(others, {}, [](const QcCreature *mob) { return mob->villager->home->x + mob->villager->home->z; });
        const QcVec3 freed = others.constLast()->motion.position;
        world.mobs->remove(others.constLast()->id);
        const int fromBefore = from.population;
        const int toBefore = to.population;
        QCOMPARE(to.freeBeds(), 1);

        QcCreature *moved = villagersOf(world, from.id).constFirst();
        const quint64 uid = moved->uid;
        const QcBlockPos oldHome = *moved->villager->home;
        moved->motion.position = freed;
        QVERIFY(QcVillagerRelocation::strayed(from, moved->motion.position));
        lifeCheck(world);

        const auto it = std::ranges::find(world.mobs->mobs(), uid, &QcCreature::uid);
        QVERIFY(it != world.mobs->mobs().end());
        QCOMPARE(it->villager->village, to.id);
        QVERIFY(it->villager->home.has_value());
        QVERIFY(to.contains(*it->villager->home));
        QCOMPARE(from.population, fromBefore - 1);
        QCOMPARE(to.population, toBefore + 1);
        QCOMPARE(to.freeBeds(), 0);
        QCOMPARE(QcVillage::spotAt(from.beds, oldHome)->claimedBy, quint64(0));
    }

    void aHomelessVillagerTakesTheFreeBedOfALapsedClaim()
    {
        World world = villages(1);
        const qint64 id = world.village(0).id;
        // A villager whose chunk never comes back holds its bed: in the next run its claim lapses
        // when the claims are reconciled, and nobody has food, so no child is due.
        const QcCreature *gone = villagersOf(world, id).constLast();
        const QcBlockPos bedOfGone = *gone->villager->home;
        const QcVec3 besideIt = gone->motion.position;
        newRun(world, gone->uid);
        QcVillage &village = *world.villages().find(id);
        QCOMPARE(village.freeBeds(), 0);
        QVERIFY(!world.mobs->population().claimsReconciled(id));

        // A villager without a village comes by.
        const quint64 uid = homeless(world, besideIt).uid;
        lifeCheck(world);
        QVERIFY(world.mobs->population().claimsReconciled(id));
        const auto it = std::ranges::find(world.mobs->mobs(), uid, &QcCreature::uid);
        QCOMPARE(it->villager->village, id);
        QCOMPARE(it->villager->home, std::optional(bedOfGone));
        QCOMPARE(village.freeBeds(), 0);
        QCOMPARE(village.population, int(villagersOf(world, id).size()));

        // One beyond searchRadius of any free bed stays homeless.
        world.mobs->remove(villagersOf(world, id).constFirst()->id);
        const QcBlockPos far = farAway(world);
        loadAround(world, far, 1);
        const quint64 stranger = homeless(world, above(far)).uid;
        lifeCheck(world);
        QCOMPARE(std::ranges::find(world.mobs->mobs(), stranger, &QcCreature::uid)->villager->village, 0);
    }

    void aStragglerAwayWhenItsVillageWasReconciledLeavesThePopulationOnce()
    {
        World world = villages(1);
        const qint64 id = world.village(0).id;
        const quint64 away = villagersOf(world, id).constLast()->uid;
        const QcMobManager::Stored straggler = newRun(world, away);
        lifeCheck(world);
        QcVillage &village = *world.villages().find(id);
        QVERIFY(world.mobs->population().claimsReconciled(id));
        const int counted = static_cast<int>(villagersOf(world, id).size());
        QCOMPARE(village.population, counted);

        // Its chunk comes back: its claims lapsed, the village does not count it, so it is homeless
        // and moves in again like any newcomer (its old bed is free).
        world.mobs->restore({straggler}, {}, world.ids());
        world.mobs->editableMobs().last().dormant = false;
        const quint32 entity = world.mobs->mobs().constLast().id;
        QCOMPARE(world.mobs->find(entity)->villager->village, id);
        QCOMPARE(village.population, counted);
        lifeCheck(world);
        QCOMPARE(world.mobs->find(entity)->villager->village, id);
        QCOMPARE(village.population, counted + 1);
        QCOMPARE(village.population, int(villagersOf(world, id).size()));

        // Dying, it leaves the population once.
        world.mobs->remove(entity);
        QCOMPARE(village.population, counted);
    }

    void aStragglerDyingBeforeItMovesAgainDoesNotLowerThePopulation()
    {
        World world = villages(1);
        const qint64 id = world.village(0).id;
        const quint64 away = villagersOf(world, id).constLast()->uid;
        const QcMobManager::Stored straggler = newRun(world, away);
        lifeCheck(world);
        QcVillage &village = *world.villages().find(id);
        const int counted = village.population;
        QCOMPARE(counted, int(villagersOf(world, id).size()));

        world.mobs->restore({straggler}, {}, world.ids());
        world.mobs->editableMobs().last().dormant = false;
        world.mobs->remove(world.mobs->mobs().constLast().id);
        QCOMPARE(village.population, counted);
    }

    void aVillagerStandingBeyondAVillageDoesNotMoveIntoIt()
    {
        World world = villages(1);
        QcVillage &village = world.village(0);
        // A free bed at the edge of the area (the box does not grow) and a homeless villager farther
        // out than strayReach, but within searchRadius of the bed: it would stray out again at once.
        const int edge = village.maxX + QcVillage::reach;
        loadAround(world, {edge, 0, village.centre.z}, 3);
        const QcBlockPos head = bed(world, edge, village.centre.z);
        QCOMPARE(village.maxX, edge - QcVillage::reach);
        QVERIFY(village.freeBeds() > 0);
        const QcBlockPos out{edge + QcVillagerHomes::strayReach + 12, QcVillageFixture::landFloor, head.z};
        QcCreature &stranger = homeless(world, above(out));
        const quint64 uid = stranger.uid;
        QVERIFY(QcVillagerRelocation::strayed(village, stranger.motion.position));
        QVERIFY(std::hypot(out.x - head.x, out.z - head.z) <= QcVillagerRelocation::searchRadius);

        const int population = village.population;
        lifeCheck(world);
        QCOMPARE(std::ranges::find(world.mobs->mobs(), uid, &QcCreature::uid)->villager->village, 0);
        QCOMPARE(village.population, population);
    }

    void aVillageInRegionZeroZeroIsAVillage()
    {
        // The generated village of region (0, 0) has an id other than 0 (no village) and other
        // than a settlement's.
        static_assert(QcVillagePlan::regionId(0, 0) != 0);
        static_assert(QcVillagePlan::regionX(QcVillagePlan::regionId(-3, 7)) == -3);
        static_assert(QcVillagePlan::regionZ(QcVillagePlan::regionId(-3, 7)) == 7);
        QVERIFY(!QcVillage::isSettlement(QcVillagePlan::regionId(0, 0)));
        QVERIFY(!QcVillage::isSettlement(QcVillagePlan::regionId(-1, -1)));
        QVERIFY(QcVillagePlan::regionId(-1, -1) > 0);

        World world = villages(2, QcVillagePlan::regionId(0, 0));
        QcVillage &home = world.village(0);
        QCOMPARE(home.id, QcVillagePlan::regionId(0, 0));
        const QList<QcCreature *> residents = villagersOf(world, home.id);
        const int population = home.population;
        QCOMPARE(population, int(residents.size()));

        // Village life keeps its villagers (they are not homeless) and counts them for golems.
        lifeCheck(world);
        QVERIFY(world.mobs->population().claimsReconciled(home.id));
        QCOMPARE(home.population, population);
        QCOMPARE(int(villagersOf(world, home.id).size()), population);

        // A villager carried away from the other village moves into it, and one carried away from
        // it moves out.
        QcCreature *moved = villagersOf(world, world.village(1).id).constFirst();
        const quint64 uid = moved->uid;
        const QcVec3 freed = residents.constLast()->motion.position;
        world.mobs->remove(residents.constLast()->id);
        moved = villagersOf(world, world.village(1).id).constFirst();
        QCOMPARE(moved->uid, uid);
        moved->motion.position = freed;
        lifeCheck(world);
        QCOMPARE(std::ranges::find(world.mobs->mobs(), uid, &QcCreature::uid)->villager->village, home.id);
        QCOMPARE(home.population, population);
    }

    void twoVillagesGrowingTowardsEachOtherNeverShareASpot()
    {
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcVillageFixture::Options west;
        const std::shared_ptr<QcVillagePlan> first = QcVillageFixture::plan(*content, west);
        QcVillageFixture::Options east;
        east.id = 2;
        east.x = first->maxX + 120 - first->minX;
        World world = worldOf(content, {first, QcVillageFixture::plan(*content, east)});
        QcVillage &a = world.village(0);
        QcVillage &b = world.village(1);
        const int z = a.centre.z;
        loadAround(world, {(a.maxX + b.minX) / 2, 0, z}, (b.minX - a.maxX) / QcWorld::chunkSize + 2);

        // Each side places a bed just beyond its area in turn, until neither may grow any more.
        QList<QcBlockPos> refused;
        for (int round = 0; round < 8; ++round) {
            for (QcVillage *village : {&a, &b}) {
                const bool eastwards = village == &a;
                const int x = eastwards ? village->maxX + QcVillage::reach + 4 : village->minX - QcVillage::reach - 4;
                if (x <= a.maxX || x >= b.minX) {
                    continue;
                }
                const QcBlockPos head = bed(world, x, z);
                if (!world.villages().at(head)) {
                    refused.append(head);
                }
            }
        }
        QVERIFY(!refused.isEmpty());
        QCOMPARE(world.villages().count(), 2);
        QVERIFY(a.maxX + QcVillage::reach < b.minX - QcVillage::reach);

        // Every spot belongs to one village, also after the chunks between them load again.
        const auto check = [&world, &a, &b] {
            for (const QcVillage *village : {&a, &b}) {
                const QcVillage *other = village == &a ? &b : &a;
                for (const QcVillage::Spot &spot : village->beds) {
                    QCOMPARE(world.villages().at(spot.at), village);
                    QVERIFY(std::ranges::none_of(other->beds, [&spot](const QcVillage::Spot &mine) { return mine.at == spot.at; }));
                }
            }
        };
        check();
        const auto sorted = [](QList<QcVillage::Spot> spots) {
            std::ranges::sort(spots, {}, [](const QcVillage::Spot &spot) { return std::tuple(spot.at.x, spot.at.y, spot.at.z); });
            return spots;
        };
        const QList<QcVillage::Spot> bedsA = sorted(a.beds);
        const QList<QcVillage::Spot> bedsB = sorted(b.beds);
        for (int x = a.minX - QcVillage::reach; x <= b.maxX + QcVillage::reach; x += QcWorld::chunkSize) {
            if (const auto chunk = world.world->chunk(QcBlockPos{x, 0, z}.chunk())) {
                world.villages().noteChunk(*chunk);
            }
        }
        QCOMPARE(sorted(a.beds), bedsA);
        QCOMPARE(sorted(b.beds), bedsB);
        check();
    }

    void aSpotWhereGeneratedAreasOverlapBelongsToTheNearerVillage()
    {
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcVillageFixture::Options west;
        const std::shared_ptr<QcVillagePlan> first = QcVillageFixture::plan(*content, west);
        QcVillageFixture::Options east;
        east.id = 2;
        east.x = first->maxX + 20 - first->minX;
        const std::shared_ptr<QcVillagePlan> second = QcVillageFixture::plan(*content, east);
        // Their areas overlap by a few blocks between them.
        const int gap = second->minX - first->maxX;
        QVERIFY(gap > 4 && gap < 2 * QcVillage::reach);
        World world = worldOf(content, {first, second});
        QcVillage &a = world.village(0);
        QcVillage &b = world.village(1);
        const QcBlockPos head = bed(world, a.maxX + gap / 2 - 1, a.centre.z);
        QVERIFY(a.contains(head) && b.contains(head));
        QCOMPARE(world.villages().at(head), &a);
        QVERIFY(QcVillage::spotAt(a.beds, head));
        QVERIFY(!QcVillage::spotAt(b.beds, head));

        world.villages().noteChunk(*world.world->chunk(head.chunk()));
        QVERIFY(QcVillage::spotAt(a.beds, head));
        QVERIFY(!QcVillage::spotAt(b.beds, head));
    }

    void villagersLeaveOtherCreaturesAShareOfThePathBudget()
    {
        World world = villages(1);
        QcPathPlanner planner(*world.content);
        constexpr int perSearch = 100;
        constexpr int perTick = 250;
        planner.setLimits(perTick, perSearch, 64);
        QList<QcCreature> mobs;
        const QList<QcCreature *> villagers = villagersOf(world, world.village(0).id);
        for (const QcCreature *villager : villagers) {
            mobs.append(*villager);
        }
        QVERIFY(mobs.size() >= 3);
        world.mobs->spawn(*world.content->mobIndex(u"qtcraft:zombie"_s), mobs.first().motion.position, world.nextId++);
        mobs.append(world.mobs->mobs().constLast());
        const quint32 zombie = mobs.constLast().id;
        // Goals inside the ground: every search spends its whole budget.
        for (QcCreature &mob : mobs) {
            mob.path = {};
            QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Errand, mob.motion.position + QcVec3{0.0, -6.0, 0.0}, 0);
            planner.request(mob);
        }
        const auto rulesOf = [&world](const QcCreature &mob) { return QcPathRules::of(world.content->mobs()[mob.type]); };
        planner.serve(*world.world, mobs, rulesOf);

        const auto served = std::ranges::find(mobs, zombie, &QcCreature::id);
        QVERIFY(served->path.planned);
        QVERIFY(planner.villagerNodesLastTick() <= perTick - perTick / QcPathPlanner::otherCreaturesShare);
        QCOMPARE(planner.searchesLastTick(), 2);
        QCOMPARE(planner.waiting(), mobs.size() - 2);

        // Without other creatures waiting, villagers use the whole budget.
        planner.serve(*world.world, mobs, rulesOf);
        QCOMPARE(planner.searchesLastTick(), 2);
        QVERIFY(planner.villagerNodesLastTick() > perTick - perTick / QcPathPlanner::otherCreaturesShare);
    }

    void aSearchLargerThanTheVillagersShareStillRunsOncePerTick()
    {
        World world = villages(1);
        QcPathPlanner planner(*world.content);
        // One search may spend the whole budget, more than the villagers' share.
        planner.setLimits(512, 512, 64);
        QList<QcCreature> mobs;
        const QList<QcCreature *> villagers = villagersOf(world, world.village(0).id);
        mobs.append(*villagers.at(0));
        mobs.append(*villagers.at(1));
        // Many other creatures keep waiting.
        for (int i = 0; i < 4; ++i) {
            world.mobs->spawn(*world.content->mobIndex(u"qtcraft:zombie"_s), mobs.first().motion.position, world.nextId++);
            mobs.append(world.mobs->mobs().constLast());
        }
        const auto rulesOf = [&world](const QcCreature &mob) { return QcPathRules::of(world.content->mobs()[mob.type]); };
        const auto ask = [&mobs, &planner] {
            for (QcCreature &mob : mobs) {
                if (!mob.path.queued) {
                    mob.path = {};
                    QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Errand, mob.motion.position + QcVec3{0.0, -6.0, 0.0}, 0);
                    planner.request(mob);
                }
            }
        };

        // Villagers asked first: in each tick the first villager search runs, whatever waits.
        int villagerSearches = 0;
        for (int tick = 0; tick < 4; ++tick) {
            ask();
            planner.serve(*world.world, mobs, rulesOf);
            QVERIFY(planner.searchesLastTick() >= 1);
            villagerSearches += planner.villagerNodesLastTick() > 0 ? 1 : 0;
        }
        QVERIFY(villagerSearches >= 2);
    }
};

QTEST_GUILESS_MAIN(TestVillageGrowth)
#include "test_village_growth.moc"
