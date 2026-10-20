#include "common/content/parser/QcVillagerParser.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/blocks/QcBlockRules.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/storage/QcVillagerCodec.h"
#include "server/villages/QcVillagePopulation.h"
#include "server/villages/QcVillagerLooks.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageFixture.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Villagers of a village without a server (decision E-411), on fixture villages (decision E-424):
// one per bed when the village's chunks are seen, the limits, claims freed by death, every bed and
// workstation of every style reachable from the road and back, a villager getting out of the well,
// two villagers taking turns at a door, long walks in legs over a swamp village's boardwalks, a walk
// going on after the villager's chunk was unloaded and loaded again, the record in the chunk data,
// the looks and the settings in data.
class TestVillagerWalks : public QObject
{
    Q_OBJECT

private:
    // The seed of the creatures' random numbers.
    static constexpr qint64 seed = 42;

    // A world with one fixture village (decision E-424), every chunk of it loaded, and its creatures.
    struct Village
    {
        std::unique_ptr<QcServerWorld> world;
        std::unique_ptr<QcMobManager> mobs;
        std::shared_ptr<const QcVillagePlan> plan;
        quint32 nextId = 1;

        QcVillage &state() const { return *world->villages().find(plan->id); }
        std::function<quint32()> ids()
        {
            return [this] { return nextId++; };
        }
    };

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // Loads the chunks of a fixture village of `style` (its buildings turned `turns`,
    // QcVillageFixture::Options::turns) and the ring around them; with `populate` its villagers move in.
    Village village(const QString &style, bool populate, QcVillagerHomes::Limits limits = {}, int turns = 0)
    {
        Village result;
        QcVillageFixture::Options options;
        options.style = style;
        options.turns = turns;
        result.plan = QcVillageFixture::plan(*m_content, options);
        if (!result.plan) {
            return result;
        }
        result.world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcVillageFixture::Terrain>(m_content, QList{result.plan}));
        result.mobs = std::make_unique<QcMobManager>(m_content, seed);
        result.mobs->population().setVillages(&result.world->villages());
        result.mobs->population().setVillagerLimits(limits, {});
        // The ring of chunks around it too, so walks along its edge stay in loaded chunks.
        const QList<QcChunkPos> around = QcVillageFixture::chunksOf(*result.plan, 1);
        for (const QcChunkPos &chunk : around) {
            result.world->loadChunk(chunk);
        }
        if (populate) {
            const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*result.plan);
            for (const QcChunkPos &chunk : chunks) {
                result.mobs->populateChunk(*result.world, chunk, result.ids());
            }
        }
        return result;
    }

    QList<const QcCreature *> villagers(const Village &village) const
    {
        QList<const QcCreature *> list;
        for (const QcCreature &mob : village.mobs->mobs()) {
            if (m_content->mobs()[mob.type].villager) {
                list.append(&mob);
            }
        }
        return list;
    }

    // One tick of the creatures at `phase`, with the doors the villagers open and close flipped as
    // the server would (QcServerVillagers).
    void tick(Village &village, double phase) const
    {
        village.mobs->tick(*village.world, {}, QcDayCycle::dayLength * (10.0 + phase), QcPhysics::tickDuration, village.ids());
        const QList<QcVillagerBehaviour::Action> actions = village.mobs->villagers().takeActions();
        for (const QcVillagerBehaviour::Action &action : actions) {
            if (action.kind == QcVillagerBehaviour::Action::Kind::Harvest) {
                continue;
            }
            const QcBlockId door = *village.world->blockAt(action.pos);
            const QString open = action.kind == QcVillagerBehaviour::Action::Kind::OpenDoor ? u"true"_s : u"false"_s;
            const QcBlockPos other = QcBlockRules::otherHalf(*village.world, action.pos, door);
            village.world->setBlock(other, m_content->withProperty(*village.world->blockAt(other), u"open"_s, open));
            village.world->setBlock(action.pos, m_content->withProperty(door, u"open"_s, open));
        }
    }

    static double horizontalDistance(const QcVec3 &a, QcBlockPos cell) { return std::hypot(a.x - cell.x - 0.5, a.z - cell.z - 0.5); }

    static inline const QString plains = QStringLiteral("qtcraft:plains");

    // The four ways the fixture turns its buildings (QcVillageFixture::Options::turns): together
    // every building faces every direction once.
    static void turnsData()
    {
        QTest::addColumn<int>("turns");
        for (int turns = 0; turns < 4; ++turns) {
            QTest::addRow("%d", turns * 90) << turns;
        }
    }

    static QString where(const QcCreature &mob)
    {
        const auto cell = [](std::optional<QcBlockPos> pos) {
            return pos ? u"%1 %2 %3"_s.arg(pos->x).arg(pos->y).arg(pos->z) : u"none"_s;
        };
        return u"%1 at %2 %3 %4, home %5, work %6, target %7"_s.arg(mob.villager->profession)
            .arg(mob.motion.position.x)
            .arg(mob.motion.position.y)
            .arg(mob.motion.position.z)
            .arg(cell(mob.villager->home), cell(mob.villager->work), cell(mob.villager->target));
    }

private slots:
    // Every bed of every style's buildings can be left for the road and reached from it, and every
    // workstation from a cell beside it (diagonals too: a smith's furnace may stand in a corner) (fixture villages, decision E-424): no door blocked by a bed's
    // foot or a block in front of it, the ladders to beds upstairs climbable both ways; with the
    // buildings turned all four ways.
    void everyBedAndWorkstationIsReachable_data() { turnsData(); }
    void everyBedAndWorkstationIsReachable()
    {
        QFETCH(int, turns);
        const QcPathRules rules = QcPathRules::of(m_content->mobs()[*m_content->mobIndex(u"qtcraft:villager"_s)]);
        QcPathFinder finder;
        QStringList unreachable;
        for (const QcVillageStyleDefinition &definition : m_content->villageCatalog().styles()) {
            Village built = village(definition.name, false, {}, turns);
            QVERIFY(built.plan);
            // The well's side turned `turns`, the other half a turn more.
            QCOMPARE(built.plan->pieces.front().turns(), turns);
            QVERIFY(std::ranges::any_of(built.plan->pieces, [turns](const QcStructurePlacement &piece) { return piece.turns() == (turns + 2) % 4; }));
            QVERIFY(std::ranges::all_of(built.plan->pieces, [turns](const QcStructurePlacement &piece) { return piece.turns() % 2 == turns % 2; }));
            const int roadY = built.plan->roads.isEmpty() ? built.plan->decks.front().at.y + 1 : built.plan->roads.front().y + 1;
            const auto both = [&](QcBlockPos from, const QcVillagePlan::Spot &spot) {
                // The road's middle line (x or z 0) in front of the spot (within one search window).
                const QcBlockPos road = turns % 2 == 0 ? QcBlockPos{spot.at.x, roadY, 0} : QcBlockPos{0, roadY, spot.at.z};
                const QcPathRequest out{from, road, 0, 8192, 512};
                const QcPathRequest in{road, from, 0, 8192, 512};
                return finder.find(*built.world, built.mobs->paths().cells(), rules, out).reachesGoal
                       && finder.find(*built.world, built.mobs->paths().cells(), rules, in).reachesGoal;
            };
            for (const QcVillagePlan::Spot &bed : built.plan->beds) {
                const std::optional<QcVec3> beside = QcVillagerHomes::besideBed(*built.world, built.mobs->paths().cells(), bed.at, rules);
                if (!beside || !both(QcPhysics::blockContaining(*beside), bed)) {
                    unreachable.append(u"%1 bed %2 %3 %4"_s.arg(definition.name).arg(bed.at.x).arg(bed.at.y).arg(bed.at.z));
                }
            }
            QcPathDirectCells cells(*built.world, built.mobs->paths().cells());
            for (const QcVillagePlan::Spot &station : built.plan->workstations) {
                const bool reached = std::ranges::any_of(std::array{QcBlockPos{1, 0, 0}, QcBlockPos{-1, 0, 0}, QcBlockPos{0, 0, 1}, QcBlockPos{0, 0, -1}, QcBlockPos{1, 0, 1}, QcBlockPos{1, 0, -1}, QcBlockPos{-1, 0, 1}, QcBlockPos{-1, 0, -1}}, [&](QcBlockPos side) {
                    const QcBlockPos cell = station.at.offset(side.x, 0, side.z);
                    return QcPathTerrain::standAt(cells, cell, rules).ok && both(cell, station);
                });
                if (!reached) {
                    unreachable.append(u"%1 workstation %2 at %3 %4 %5"_s.arg(definition.name, station.kind).arg(station.at.x).arg(station.at.y).arg(station.at.z));
                }
            }
        }
        QVERIFY2(unreachable.isEmpty(), qPrintable(unreachable.join(u"; "_s)));
    }

    // A villager that fell into the well of a style gets out of the water (the rim is a step from its
    // surface) and goes home; the well turned all four ways.
    void aVillagerGetsOutOfTheWell_data() { turnsData(); }
    void aVillagerGetsOutOfTheWell()
    {
        QFETCH(int, turns);
        for (const QString &style : {u"qtcraft:plains"_s, u"qtcraft:desert"_s}) {
            Village built = village(style, true, {}, turns);
            QVERIFY(built.plan);
            const QcStructurePlacement &well = built.plan->pieces.front();
            const QcBlockPos water = well.toWorld(QcBlockPos{3, 0, 3});
            const auto swimmer = std::ranges::find_if(built.mobs->editableMobs(), [](const QcCreature &mob) { return mob.isVillager(); });
            QVERIFY(swimmer != built.mobs->editableMobs().end());
            const quint32 id = swimmer->id;
            swimmer->motion.position = {water.x + 0.5, double(water.y) + 0.5, water.z + 0.5};
            bool out = false;
            for (int tick = 0; tick < 1200 && !out; ++tick) {
                this->tick(built, 0.7);
                const QcCreature *mob = built.mobs->find(id);
                out = !mob->motion.inWater && mob->motion.position.y >= water.y + 1.0;
            }
            QVERIFY2(out, qPrintable(style + u": "_s + where(*built.mobs->find(id))));
        }
    }

    // Two villagers meet in the door of a swamp house in the evening, one in the doorway on its way
    // out, one on the porch on its way in: they take turns and both get home (decision E-411); the
    // house turned all four ways.
    void twoVillagersTakeTurnsAtADoor_data() { turnsData(); }
    void twoVillagersTakeTurnsAtADoor()
    {
        QFETCH(int, turns);
        Village built = village(u"qtcraft:swamp"_s, false, {}, turns);
        QVERIFY(built.plan);
        const auto house = std::ranges::find_if(built.plan->pieces, [](const QcStructurePlacement &piece) { return piece.structure().name() == u"qtcraft:village_swamp_house_large"_s; });
        QVERIFY(house != built.plan->pieces.end());
        const QcBlockPos door = house->toWorld(QcBlockPos{4, 1, 5});
        const QcBlockPos porch = house->toWorld(QcBlockPos{4, 1, 6});
        const auto inHouse = [&house](QcBlockPos at) {
            return at.x >= house->origin().x && at.x < house->origin().x + house->sizeX() && at.z >= house->origin().z && at.z < house->origin().z + house->sizeZ();
        };
        const auto homeInside = std::ranges::find_if(built.plan->beds, [&](const QcVillagePlan::Spot &bed) { return inHouse(bed.at); });
        const auto homeElsewhere = std::ranges::find_if(built.plan->beds, [&](const QcVillagePlan::Spot &bed) { return !inHouse(bed.at); });
        QVERIFY(homeInside != built.plan->beds.end() && homeElsewhere != built.plan->beds.end());
        const int type = *m_content->mobIndex(u"qtcraft:villager"_s);
        const QcVillagerSettings &settings = *m_content->villageCatalog().villagerSettings(u"qtcraft:villager"_s);
        const quint32 out = built.mobs->spawn(type, {door.x + 0.5, double(door.y), door.z + 0.5}, built.nextId++);
        *built.mobs->find(out)->villager = QcVillagerHomes::settle(built.state(), homeElsewhere->at, built.mobs->find(out)->uid, settings, 1);
        // On the porch, a little off its middle line and away from the door.
        const QcVec3 outwards{double(porch.x - door.x), 0.0, double(porch.z - door.z)};
        const QcVec3 aside{outwards.z, 0.0, outwards.x};
        const QcVec3 onPorch = QcVec3{porch.x + 0.5, double(porch.y), porch.z + 0.5} + outwards * 0.2 - aside * 0.2;
        const quint32 in = built.mobs->spawn(type, onPorch, built.nextId++);
        *built.mobs->find(in)->villager = QcVillagerHomes::settle(built.state(), homeInside->at, built.mobs->find(in)->uid, settings, 2);
        bool home = false;
        for (int tick = 0; tick < 2400 && !home; ++tick) {
            this->tick(built, 0.7);
            home = built.mobs->find(out)->pose == QcPose::Lying && built.mobs->find(in)->pose == QcPose::Lying;
        }
        QVERIFY2(home, qPrintable(where(*built.mobs->find(out)) + u"; "_s + where(*built.mobs->find(in))));
    }

    void aVillagerMovesIntoEveryBed()
    {
        Village built = village(plains, true);
        QVERIFY(built.plan);
        const QcVillage &state = built.state();
        QVERIFY(state.beds.size() >= 2);
        QCOMPARE(villagers(built).size(), state.beds.size());
        QCOMPARE(state.population, int(state.beds.size()));

        // Each bed has its own villager, who knows it as home, and stands beside it.
        QSet<quint64> ids;
        const QList<const QcCreature *> settled = villagers(built);
        for (const QcCreature *villager : settled) {
            ids.insert(villager->uid);
            QVERIFY(villager->villager->home);
            QCOMPARE(QcVillage::spotAt(built.state().beds, *villager->villager->home)->claimedBy, villager->uid);
            QVERIFY(horizontalDistance(villager->motion.position, *villager->villager->home) < 3.0);
            QCOMPARE(villager->villager->style, state.style);
            QVERIFY(villager->persistent);
        }
        QCOMPARE(ids.size(), state.beds.size());
        QVERIFY(!ids.contains(0));

        // A chunk seen again brings nobody new.
        const qsizetype count = built.mobs->mobs().size();
        const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*built.plan);
        for (const QcChunkPos &chunk : chunks) {
            built.mobs->populateChunk(*built.world, chunk, built.ids());
        }
        QCOMPARE(built.mobs->mobs().size(), count);
    }

    void villagersKeepToTheLimits()
    {
        Village perVillage = village(plains, true, {2, 1024});
        QCOMPARE(villagers(perVillage).size(), 2);
        QCOMPARE(perVillage.state().population, 2);
        QCOMPARE(perVillage.state().freeBeds(), perVillage.state().beds.size() - 2);

        Village world = village(plains, true, {32, 1});
        QCOMPARE(villagers(world).size(), 1);
    }

    void deathAndRemovalFreeTheClaims()
    {
        Village built = village(plains, true);
        QVERIFY(villagers(built).size() >= 2);
        const QList<const QcCreature *> all = villagers(built);
        const QcCreature first = *all.constFirst();
        const QcCreature second = *all.constLast();
        const int population = built.state().population;

        QVERIFY(built.mobs->wound(first.id, 1000.0).has_value());
        QCOMPARE(QcVillage::spotAt(built.state().beds, *first.villager->home)->claimedBy, 0u);
        QCOMPARE(built.state().population, population - 1);
        QVERIFY(built.mobs->remove(second.id));
        QCOMPARE(QcVillage::spotAt(built.state().beds, *second.villager->home)->claimedBy, 0u);
        QCOMPARE(built.state().population, population - 2);
    }

    // villages.json and the chunks may disagree (villages.json put back from another save): a claim
    // whose villager is not in the world lapses once every chunk it could stand in is loaded, and new
    // villagers get persistent ids no loaded one has (decision E-420).
    void lostClaimsLapseAndIdsStayUnique()
    {
        Village built = village(plains, false);
        QVERIFY(built.plan);
        QcVillage &state = built.state();
        QVERIFY(state.beds.size() >= 2);
        // villages.json knew a villager whose chunk was never written.
        state.beds[0].claimedBy = 40;
        state.population = 1;
        // A chunk knew villager 5, villages.json did not.
        QcMobManager::Stored stored;
        stored.uid = 5;
        stored.type = *m_content->mobIndex(u"qtcraft:villager"_s);
        stored.position = {state.beds[1].at.x + 0.5, double(state.beds[1].at.y), state.beds[1].at.z + 0.5};
        stored.health = 20.0;
        QcVillagerRecord record;
        record.village = state.id;
        record.profession = (*m_content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)).unemployed;
        record.style = state.style;
        stored.villager = record;
        built.mobs->restore({stored}, {}, built.ids());
        QCOMPARE(built.mobs->nextUid(), 6u);

        // Not every chunk around the village is loaded yet: the claim stays. The chunk seen early
        // is not the one of villager 40's bed: each chunk is settled once, so a bed whose claim
        // lapses after its chunk was settled waits for a child.
        const QList<QcChunkPos> chunks = QcVillageFixture::chunksOf(*built.plan);
        const QcChunkPos lostBed = state.beds[0].at.chunk();
        const auto early = std::ranges::find_if(chunks, [lostBed](QcChunkPos chunk) { return chunk != lostBed; });
        QVERIFY(early != chunks.end());
        built.mobs->populateChunk(*built.world, *early, built.ids());
        QCOMPARE(state.beds[0].claimedBy, 40u);

        const int around = QcVillage::reach + QcVillagerHomes::strayReach;
        const QcChunkPos low = QcBlockPos{state.minX - around, 0, state.minZ - around}.chunk();
        const QcChunkPos high = QcBlockPos{state.maxX + around, 0, state.maxZ + around}.chunk();
        for (int x = low.x; x <= high.x; ++x) {
            for (int z = low.z; z <= high.z; ++z) {
                built.world->loadChunk({x, z});
            }
        }
        for (const QcChunkPos &chunk : chunks) {
            built.mobs->populateChunk(*built.world, chunk, built.ids());
        }
        QSet<quint64> ids;
        const QList<const QcCreature *> settled = villagers(built);
        for (const QcCreature *villager : settled) {
            QVERIFY2(!ids.contains(villager->uid), qPrintable(QString::number(villager->uid)));
            ids.insert(villager->uid);
        }
        QVERIFY(ids.contains(5));
        QVERIFY(!ids.contains(40));
        QVERIFY(std::ranges::none_of(state.beds, [](const QcVillage::Spot &bed) { return bed.claimedBy == 40; }));
        // Every bed has its villager again, villager 40's too; villager 5 has none yet.
        QVERIFY(std::ranges::all_of(state.beds, [&ids](const QcVillage::Spot &bed) { return ids.contains(bed.claimedBy); }));
        QCOMPARE(state.population, int(settled.size()));
        QCOMPARE(settled.size(), state.beds.size() + 1);
    }

    // Claims in the village state alone: those of villagers not in the world lapse.
    void lostClaimsLapseById()
    {
        QcVillage village;
        village.beds.append({{0, 64, 0}, {}, 3});
        village.beds.append({{2, 64, 0}, {}, 1});
        village.workstations.append({{4, 64, 0}, u"qtcraft:farmer"_s, 3});
        QCOMPARE(QcVillagerHomes::dropLostClaims(village, {1}), 1);
        QCOMPARE(village.beds[0].claimedBy, 0u);
        QCOMPARE(village.beds[1].claimedBy, 1u);
        QCOMPARE(village.workstations[0].claimedBy, 0u);
    }

    void longWalksGoInLegsOverTheBoardwalks()
    {
        Village built = village(u"qtcraft:swamp"_s, false);
        QVERIFY(built.plan);

        // The two beds farthest apart, well beyond one search window (64 cells).
        QcBlockPos from;
        QcBlockPos to;
        int farthest = 0;
        for (const QcVillagePlan::Spot &a : built.plan->beds) {
            for (const QcVillagePlan::Spot &b : built.plan->beds) {
                const int distance = std::max(std::abs(a.at.x - b.at.x), std::abs(a.at.z - b.at.z));
                if (distance > farthest) {
                    farthest = distance;
                    from = a.at;
                    to = b.at;
                }
            }
        }
        QVERIFY2(farthest > QcVillagerRoads::legReach, qPrintable(QString::number(farthest)));

        // A villager beside the one bed whose home is the other walks there in the evening.
        const QcPathRules rules = QcPathRules::of(m_content->mobs()[*m_content->mobIndex(u"qtcraft:villager"_s)]);
        const std::optional<QcVec3> start = QcVillagerHomes::besideBed(*built.world, built.mobs->paths().cells(), from, rules);
        QVERIFY(start);
        const quint32 id = built.mobs->spawn(*m_content->mobIndex(u"qtcraft:villager"_s), *start, built.nextId++);
        QcCreature *walker = built.mobs->find(id);
        *walker->villager = QcVillagerHomes::settle(built.state(), to, walker->uid, (*m_content->villageCatalog().villagerSettings(u"qtcraft:villager"_s)), 1);
        bool arrived = false;
        for (int tick = 0; tick < 4800 && !arrived; ++tick) {
            this->tick(built, 0.5);
            arrived = horizontalDistance(built.mobs->find(id)->motion.position, to) <= 2.0;
        }
        QVERIFY2(arrived, qPrintable(u"%1 %2 -> %3 %4, now %5 %6"_s.arg(from.x).arg(from.z).arg(to.x).arg(to.z)
                                         .arg(built.mobs->find(id)->motion.position.x)
                                         .arg(built.mobs->find(id)->motion.position.z)));
        QVERIFY(built.mobs->villagers().roads().planned() > 0);
    }

    void aWalkGoesOnAfterTheChunkWasUnloaded()
    {
        Village built = village(plains, true);
        QVERIFY(built.plan);
        const auto worker = std::ranges::find_if(built.mobs->mobs(), [](const QcCreature &mob) { return mob.isVillager(); });
        QVERIFY(worker != built.mobs->mobs().end());
        const quint32 id = worker->id;
        for (int tick = 0; tick < 40; ++tick) {
            this->tick(built, 0.1);
        }
        QVERIFY(built.mobs->find(id)->villager->work);

        // Unloaded (what its chunk keeps) and loaded again: the same villager, deciding anew.
        const QcMobManager::Stored stored = QcMobManager::storedOf(*built.mobs->find(id));
        QVERIFY(stored.villager);
        const QcVillagerRecord record = *stored.villager;
        built.mobs->editableMobs().removeIf([id](const QcCreature &mob) { return mob.id == id; });
        // A copy of it comes along (a copied chunk): the copy gets a new id, the original goes on.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"persistent id %1 of another one"_s.arg(stored.uid)));
        built.mobs->restore({stored, stored}, {}, built.ids());
        QcCreature &back = built.mobs->editableMobs()[built.mobs->mobs().size() - 2];
        QCOMPARE(back.uid, stored.uid);
        QVERIFY(built.mobs->mobs().last().uid != stored.uid);
        back.dormant = false;
        QCOMPARE(static_cast<const QcVillagerRecord &>(*back.villager), record);
        const quint32 again = back.id;
        bool there = false;
        for (int tick = 0; tick < 2400 && !there; ++tick) {
            this->tick(built, 0.1);
            there = horizontalDistance(built.mobs->find(again)->motion.position, *record.work) <= 2.0;
        }
        QVERIFY2(there, qPrintable(where(*built.mobs->find(again))));

        // At night it goes home to bed (up a ladder when the bed is upstairs) and lies down.
        bool asleep = false;
        for (int tick = 0; tick < 2400 && !asleep; ++tick) {
            this->tick(built, 0.7);
            asleep = built.mobs->find(again)->pose == QcPose::Lying;
        }
        QVERIFY2(asleep, qPrintable(where(*built.mobs->find(again))));
        QVERIFY(horizontalDistance(built.mobs->find(again)->motion.position, *record.home) < 1.5);
    }

    // Version 2 knew no sleep (decision E-470): the rest of the record reads as before, it never
    // slept.
    void aRecordOfVersionTwoNeverSlept()
    {
        QcVillagerRecord record;
        record.produce = 3;
        record.sleptAt = 1234.5;
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, record);
        }
        bytes.chop(8);
        QDataStream previous(bytes);
        const std::optional<QcVillagerRecord> before = QcVillagerCodec::read(previous, 2);
        QVERIFY(before.has_value());
        QCOMPARE(before->sleptAt, -1.0);
        QCOMPARE(before->produce, record.produce);
        QVERIFY(previous.atEnd());
    }

    void theRecordIsSavedWithItsChunk()
    {
        QcVillagerRecord record;
        record.village = (qint64(5) << 32) | 7;
        record.home = QcBlockPos{100, 70, -20};
        record.work = QcBlockPos{104, 70, -18};
        record.profession = u"qtcraft:smith"_s;
        record.level = 2;
        record.experience = 15;
        record.look = 0xdeadbeef;
        record.style = u"qtcraft:desert"_s;
        record.produce = 3;
        record.sleptAt = 1234.5;
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, record);
        }
        QDataStream in(bytes);
        QCOMPARE(QcVillagerCodec::read(in, QcVillagerCodec::version), record);

        // A level out of range is damaged data.
        record.level = 9;
        QByteArray damaged;
        {
            QDataStream out(&damaged, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, record);
        }
        QDataStream bad(damaged);
        QVERIFY(!QcVillagerCodec::read(bad, QcVillagerCodec::version));

        // In little endian as well.
        record.level = 2;
        QByteArray little;
        {
            QDataStream out(&little, QIODevice::WriteOnly);
            out.setByteOrder(QDataStream::LittleEndian);
            QcVillagerCodec::write(out, record);
        }
        QDataStream littleIn(little);
        littleIn.setByteOrder(QDataStream::LittleEndian);
        QCOMPARE(QcVillagerCodec::read(littleIn, QcVillagerCodec::version), record);

        // Version 1 has no offers: read with none and no restock.
        record.offers = {{u"x"_s, 1, 0}};
        record.lastRestock = 50.0;
        QByteArray older;
        {
            QDataStream out(&older, QIODevice::WriteOnly);
            QcVillagerRecord first = record;
            first.offers.clear();
            QcVillagerCodec::write(out, first);
        }
        // The offers (a count of none), the restock time and count are the fields of version 2, the
        // time it slept that of version 3.
        older.chop(4 + 8 + 4 + 8);
        QDataStream olderIn(older);
        QcVillagerRecord defaults = record;
        defaults.offers.clear();
        defaults.lastRestock = -1.0;
        defaults.restocks = 0;
        defaults.sleptAt = -1.0;
        QCOMPARE(QcVillagerCodec::read(olderIn, 1), defaults);
        QVERIFY(olderIn.atEnd());

        // A profession claiming 2 GB is refused before it is read, as is one just over the limit.
        for (const quint32 length : {0x7ffffffeu, 2u * 129u}) {
            QByteArray huge;
            {
                QDataStream out(&huge, QIODevice::WriteOnly);
                out << record.village << false << false << length;
                out.writeRawData(QByteArray(int(length < 4096 ? length : 64), 'a').constData(), int(length < 4096 ? length : 64));
            }
            QDataStream hugeIn(huge);
            QVERIFY(!QcVillagerCodec::read(hugeIn, QcVillagerCodec::version));
            QVERIFY(hugeIn.status() != QDataStream::Ok);
        }
    }

    void villagersDressInTheirStyleAndProfession()
    {
        QcVillagerRecord farmer;
        farmer.style = u"qtcraft:desert"_s;
        farmer.profession = u"qtcraft:farmer"_s;
        const QcAppearance look = QcVillagerLooks::appearanceOf(*m_content, *m_content->mobIndex(u"qtcraft:villager"_s), farmer);
        QCOMPARE(look.outfit, u"qtcraft:villager_robe@qtcraft:farmer"_s);
        QCOMPARE(look.hair, u"qtcraft:villager_straw_hat"_s);
        const QcAppearanceDefinition *outfit = m_content->assets().appearance(look.outfit);
        QVERIFY(outfit);
        QCOMPARE(outfit->layer(u"torso"_s)->texture, u"qtcraft:villager_farmer_apron"_s);
        QCOMPARE(outfit->layer(u"legs"_s)->texture, u"qtcraft:villager_robe"_s);

        // Colours and hair come from the seed, from the style's lists; an unknown style dresses
        // like the default look.
        QSet<quint32> skins;
        for (quint32 seed = 0; seed < 64; ++seed) {
            QcVillagerRecord unemployed;
            unemployed.profession = u"qtcraft:unemployed"_s;
            unemployed.look = seed * 0x01010101u;
            unemployed.style = u"qtcraft:taiga"_s;
            const QcAppearance taiga = QcVillagerLooks::appearanceOf(*m_content, *m_content->mobIndex(u"qtcraft:villager"_s), unemployed);
            QCOMPARE(taiga.outfit, u"qtcraft:villager_coat"_s);
            skins.insert(taiga.skinColor);
        }
        QVERIFY(skins.size() >= 2);
        QcVillagerRecord stranger;
        stranger.profession = u"qtcraft:smith"_s;
        stranger.style = u"mod:moon"_s;
        QCOMPARE(QcVillagerLooks::appearanceOf(*m_content, *m_content->mobIndex(u"qtcraft:villager"_s), stranger).outfit, u"qtcraft:villager_tunic@qtcraft:smith"_s);
    }

    void theSettingsComeFromData()
    {
        // The base content in the files of decision E-421: the villager names its settings, the
        // professions their workstation blocks, which become the village settings' workstations.
        const int villager = *m_content->mobIndex(u"qtcraft:villager"_s);
        QCOMPARE(m_content->mobs()[villager].villagerSettings, u"qtcraft:villager"_s);
        const QcVillagerSettings &settings = m_content->villagerSettingsOf(villager);
        QCOMPARE(settings.name, u"qtcraft:villager"_s);
        QCOMPARE(settings.unemployed, u"qtcraft:unemployed"_s);
        QCOMPARE(settings.professions.size(), 4);
        QCOMPARE(settings.forWorkstation(u"qtcraft:fisher"_s)->name, u"qtcraft:fisher"_s);
        QVERIFY(settings.profession(u"qtcraft:farmer"_s)->tendsCrops);
        QCOMPARE(m_content->villageCatalog().settings().workstations.value(u"qtcraft:composter"_s), u"qtcraft:farmer"_s);
        QCOMPARE(m_content->villageCatalog().settings().workstations.value(u"qtcraft:furnace"_s), u"qtcraft:smith"_s);
        QCOMPARE(m_content->villageCatalog().settings().workstations.value(u"qtcraft:barrel"_s), u"qtcraft:fisher"_s);
        QCOMPARE(m_content->villageCatalog().settings().workstations.size(), 3);
        QCOMPARE(m_content->villagerOfStyle(u"qtcraft:plains"_s), villager);
        QCOMPARE(m_content->villageCatalog().golemOfStyle(u"qtcraft:plains"_s).mob, u"qtcraft:iron_golem"_s);
        QCOMPARE(settings.activityAt(0.75), QcVillagerActivity::Sleep);
        QCOMPARE(settings.activityAt(0.995), QcVillagerActivity::Home);

        const auto parse = [](const char *json) {
            return QcVillagerParser::parseVillagerSettings(QJsonDocument::fromJson(json).object(), u"mod"_s);
        };
        const char *valid = R"({"name": "v", "unemployed": "none",
            "routine": [{"from": 0.5, "do": "sleep"}, {"from": 0.0, "do": "work"}], "look": {"outfit": "o", "skin": ["#102030"]}})";
        const QcResult<QcVillagerSettings> parsed = parse(valid);
        QVERIFY2(parsed, qPrintable(parsed ? QString() : parsed.error()));
        QCOMPARE(parsed->name, u"mod:v"_s);
        QCOMPARE(parsed->routine.front().activity, QcVillagerActivity::Work);
        QCOMPARE(parsed->look.skin, QList<quint32>{0x102030});
        QVERIFY(!parse(R"({"name": "v", "unemployed": "none", "routine": [{"from": 0.0, "do": "dance"}], "look": {"outfit": "o"}})"));
        QVERIFY(!parse(R"({"name": "v", "unemployed": "none", "routine": [{"from": 0.0, "do": "work"}], "look": {"outfit": "o", "skin": ["red"]}})"));
        // Fields of the earlier single villagers.json are refused with where they went.
        const QcResult<QcVillagerSettings> old = parse(R"({"name": "v", "mob": "v", "unemployed": "none", "professions": [{"name": "none"}],
            "routine": [{"from": 0.0, "do": "work"}], "look": {"outfit": "o"}})");
        QVERIFY(!old);
        QVERIFY2(old.error().contains(u"moved to"_s), qPrintable(old.error()));
    }
};

QTEST_GUILESS_MAIN(TestVillagerWalks)
#include "test_villager_walks.moc"
