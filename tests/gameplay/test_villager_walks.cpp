#include "common/content/loader/QcBaseContent.h"
#include "common/content/parser/QcVillagerParser.h"
#include "common/world/QcDayCycle.h"
#include "server/blocks/QcBlockRules.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/storage/QcVillagerCodec.h"
#include "server/villages/QcVillagerLooks.h"
#include "server/villages/QcVillages.h"
#include "world/QcVillageTestUtil.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Villagers of generated villages without a server (decision E-411): one per bed when the village's
// chunks are seen, the limits, claims freed by death, long walks in legs over a swamp village's
// boardwalks, a walk going on after the villager's chunk was unloaded and loaded again, the record
// in the chunk data, the looks and the settings in data.
class TestVillagerWalks : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;

    static std::shared_ptr<const QcContentRegistry> loadContent()
    {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }

    // A world of the seed with every chunk of a village loaded, and its creatures.
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

    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

    // The first village of the seed whose style `accepts`, searched ring by ring around the origin.
    template<typename Accepts>
    std::shared_ptr<const QcVillagePlan> findPlan(const QcTerrainGenerator &generator, Accepts accepts) const
    {
        for (int ring = 0; ring <= 14; ++ring) {
            for (int rz = -ring; rz <= ring; ++rz) {
                for (int rx = -ring; rx <= ring; ++rx) {
                    if (std::max(std::abs(rx), std::abs(rz)) != ring) {
                        continue;
                    }
                    const std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz);
                    if (plan && accepts(*plan)) {
                        return plan;
                    }
                }
            }
        }
        return nullptr;
    }

    // Loads the village's chunks; with `populate` its villagers move in.
    template<typename Accepts>
    Village village(Accepts accepts, bool populate, QcVillagerHomes::Limits limits = {})
    {
        auto generator = std::make_unique<QcTerrainGenerator>(m_content, seed);
        Village result;
        result.plan = findPlan(*generator, accepts);
        if (!result.plan) {
            return result;
        }
        result.world = std::make_unique<QcServerWorld>(m_content, std::move(generator));
        result.mobs = std::make_unique<QcMobManager>(m_content, seed);
        result.mobs->setVillages(&result.world->villages());
        result.mobs->setVillagerLimits(limits, {});
        const QList<QcChunkPos> chunks = QcVillageTestUtil::chunksOf(*result.plan);
        for (const QcChunkPos &chunk : chunks) {
            result.world->loadChunk(chunk);
        }
        // The ring of chunks around it too, so walks along its edge stay in loaded chunks.
        for (const QcChunkPos &chunk : chunks) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    result.world->loadChunk({chunk.x + dx, chunk.z + dz});
                }
            }
        }
        if (populate) {
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

    static bool any(const QcVillagePlan &) { return true; }

    static QString where(const QcCreature &mob)
    {
        const auto cell = [](std::optional<QcBlockPos> pos) {
            return pos ? u"%1 %2 %3"_s.arg(pos->x).arg(pos->y).arg(pos->z) : u"none"_s;
        };
        return u"%1 at %2 %3 %4, home %5, work %6, target %7"_s.arg(mob.villager.profession)
            .arg(mob.motion.position.x)
            .arg(mob.motion.position.y)
            .arg(mob.motion.position.z)
            .arg(cell(mob.villager.home), cell(mob.villager.work), cell(mob.villager.target));
    }

private slots:
    void aVillagerMovesIntoEveryBed()
    {
        Village built = village(any, true);
        QVERIFY(built.plan);
        const QcVillage &state = built.state();
        QVERIFY(state.beds.size() >= 2);
        QCOMPARE(villagers(built).size(), state.beds.size());
        QCOMPARE(state.population, int(state.beds.size()));

        // Each bed has its own villager, who knows it as home, and stands beside it.
        QSet<QString> names;
        const QList<const QcCreature *> settled = villagers(built);
        for (const QcCreature *villager : settled) {
            names.insert(villager->villager.name);
            QVERIFY(villager->villager.home);
            QCOMPARE(QcVillage::spotAt(built.state().beds, *villager->villager.home)->claimedBy, villager->villager.name);
            QVERIFY(horizontalDistance(villager->motion.position, *villager->villager.home) < 3.0);
            QCOMPARE(villager->villager.style, state.style);
            QVERIFY(villager->persistent);
        }
        QCOMPARE(names.size(), state.beds.size());

        // A chunk seen again brings nobody new.
        const qsizetype count = built.mobs->mobs().size();
        const QList<QcChunkPos> chunks = QcVillageTestUtil::chunksOf(*built.plan);
        for (const QcChunkPos &chunk : chunks) {
            built.mobs->populateChunk(*built.world, chunk, built.ids());
        }
        QCOMPARE(built.mobs->mobs().size(), count);
    }

    void villagersKeepToTheLimits()
    {
        Village perVillage = village(any, true, {2, 1024});
        QCOMPARE(villagers(perVillage).size(), 2);
        QCOMPARE(perVillage.state().population, 2);
        QCOMPARE(perVillage.state().freeBeds(), perVillage.state().beds.size() - 2);

        Village world = village(any, true, {32, 1});
        QCOMPARE(villagers(world).size(), 1);
    }

    void deathAndRemovalFreeTheClaims()
    {
        Village built = village(any, true);
        QVERIFY(villagers(built).size() >= 2);
        const QList<const QcCreature *> all = villagers(built);
        const QcCreature first = *all.constFirst();
        const QcCreature second = *all.constLast();
        const int population = built.state().population;

        QVERIFY(built.mobs->wound(first.id, 1000.0).has_value());
        QVERIFY(QcVillage::spotAt(built.state().beds, *first.villager.home)->claimedBy.isEmpty());
        QCOMPARE(built.state().population, population - 1);
        QVERIFY(built.mobs->remove(second.id));
        QVERIFY(QcVillage::spotAt(built.state().beds, *second.villager.home)->claimedBy.isEmpty());
        QCOMPARE(built.state().population, population - 2);
    }

    // villages.json and the chunks may come from different saves (a crash between them): a claim
    // whose villager is not in the world lapses once every chunk it could stand in is loaded, and a
    // villager saved with its chunk keeps its name to itself.
    void lostClaimsLapseAndNamesStayUnique()
    {
        Village built = village(any, false);
        QVERIFY(built.plan);
        QcVillage &state = built.state();
        QVERIFY(state.beds.size() >= 2);
        // villages.json knew a villager whose chunk was never written.
        state.beds[0].claimedBy = u"villager-40"_s;
        state.population = 1;
        // A chunk knew villager-5, villages.json did not.
        QcMobManager::Stored stored;
        stored.type = *m_content->mobIndex(m_content->villagerSettings().mob);
        stored.position = {state.beds[1].at.x + 0.5, double(state.beds[1].at.y), state.beds[1].at.z + 0.5};
        stored.health = 20.0;
        QcVillagerRecord record;
        record.village = state.id;
        record.name = u"villager-5"_s;
        record.profession = m_content->villagerSettings().unemployed;
        record.style = state.style;
        stored.villager = record;
        // ... and was settled once more under that name before the crash (it is renamed).
        built.mobs->restore({stored, stored}, {}, built.ids());
        QCOMPARE(state.nextVillager, 6);

        // Not every chunk around the village is loaded yet: the claim stays.
        const QList<QcChunkPos> chunks = QcVillageTestUtil::chunksOf(*built.plan);
        built.mobs->populateChunk(*built.world, chunks.constFirst(), built.ids());
        QCOMPARE(state.beds[0].claimedBy, u"villager-40"_s);

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
        QSet<QString> names;
        const QList<const QcCreature *> settled = villagers(built);
        for (const QcCreature *villager : settled) {
            QVERIFY2(!names.contains(villager->villager.name), qPrintable(villager->villager.name));
            names.insert(villager->villager.name);
        }
        QVERIFY(names.contains(u"villager-5"_s));
        QVERIFY(!names.contains(u"villager-40"_s));
        QVERIFY(std::ranges::none_of(state.beds, [](const QcVillage::Spot &bed) { return bed.claimedBy == u"villager-40"_s; }));
        QCOMPARE(names.size(), settled.size());
        // Every bed has its villager again, villager-40's too; villager-5 has none yet.
        QVERIFY(std::ranges::all_of(state.beds, [&names](const QcVillage::Spot &bed) { return names.contains(bed.claimedBy); }));
        QCOMPARE(state.population, int(settled.size()));
        QCOMPARE(settled.size(), state.beds.size() + 2);
    }

    // Names and claims in the village state alone.
    void namesStayUniqueAndLostClaimsLapse()
    {
        QcVillage village;
        village.beds.append({{0, 64, 0}, {}, u"villager-3"_s});
        village.nextVillager = 3;
        QCOMPARE(QcVillagerHomes::freshName(village), u"villager-4"_s);
        QVERIFY(QcVillagerHomes::noteName(village, u"villager-9"_s));
        QCOMPARE(village.nextVillager, 10);
        QVERIFY(!QcVillagerHomes::noteName(village, u"villager-2"_s));
        QVERIFY(!QcVillagerHomes::noteName(village, u"someone"_s));
        QCOMPARE(QcVillagerHomes::numberOf(u"villager-x"_s), 0);
        QCOMPARE(QcVillagerHomes::dropLostClaims(village, {u"villager-1"_s}), 1);
        QVERIFY(village.beds[0].claimedBy.isEmpty());
    }

    void longWalksGoInLegsOverTheBoardwalks()
    {
        Village built = village([](const QcVillagePlan &plan) { return plan.style == u"qtcraft:swamp"_s && plan.beds.size() >= 4; }, false);
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
        walker->villager = QcVillagerHomes::settle(built.state(), to, m_content->villagerSettings(), 1);
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
        Village built = village([](const QcVillagePlan &plan) { return plan.style != u"qtcraft:swamp"_s && !plan.workstations.isEmpty(); }, true);
        QVERIFY(built.plan);
        const auto worker = std::ranges::find_if(built.mobs->mobs(), [](const QcCreature &mob) { return !mob.villager.profession.isEmpty(); });
        QVERIFY(worker != built.mobs->mobs().end());
        const quint32 id = worker->id;
        for (int tick = 0; tick < 40; ++tick) {
            this->tick(built, 0.1);
        }
        QVERIFY(built.mobs->find(id)->villager.work);

        // Unloaded (what its chunk keeps) and loaded again: the same villager, deciding anew.
        const QcMobManager::Stored stored = QcMobManager::storedOf(*built.mobs->find(id));
        QVERIFY(stored.villager);
        const QcVillagerRecord record = *stored.villager;
        built.mobs->editableMobs().removeIf([id](const QcCreature &mob) { return mob.id == id; });
        // ... and was settled once more under that name before the crash (it is renamed).
        built.mobs->restore({stored, stored}, {}, built.ids());
        QcCreature &back = built.mobs->editableMobs().last();
        back.dormant = false;
        QCOMPARE(static_cast<const QcVillagerRecord &>(back.villager), record);
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
            asleep = built.mobs->find(again)->villager.sleeping;
        }
        QVERIFY2(asleep, qPrintable(where(*built.mobs->find(again))));
        QVERIFY(horizontalDistance(built.mobs->find(again)->motion.position, *record.home) < 1.5);
    }

    void theRecordIsSavedWithItsChunk()
    {
        QcVillagerRecord record;
        record.village = (qint64(5) << 32) | 7;
        record.name = u"villager-3"_s;
        record.home = QcBlockPos{100, 70, -20};
        record.work = QcBlockPos{104, 70, -18};
        record.profession = u"qtcraft:smith"_s;
        record.level = 2;
        record.experience = 15;
        record.look = 0xdeadbeef;
        record.style = u"qtcraft:desert"_s;
        record.produce = 3;
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, record);
        }
        QDataStream in(bytes);
        QCOMPARE(QcVillagerCodec::read(in), record);

        // A level out of range is damaged data.
        record.level = 9;
        QByteArray damaged;
        {
            QDataStream out(&damaged, QIODevice::WriteOnly);
            QcVillagerCodec::write(out, record);
        }
        QDataStream bad(damaged);
        QVERIFY(!QcVillagerCodec::read(bad));

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
        QCOMPARE(QcVillagerCodec::read(littleIn), record);

        // A name claiming 2 GB is refused before it is read, as is one just over the limit.
        for (const quint32 length : {0x7ffffffeu, 2u * 129u}) {
            QByteArray huge;
            {
                QDataStream out(&huge, QIODevice::WriteOnly);
                out << record.village << length;
                out.writeRawData(QByteArray(int(length < 4096 ? length : 64), 'a').constData(), int(length < 4096 ? length : 64));
            }
            QDataStream hugeIn(huge);
            QVERIFY(!QcVillagerCodec::read(hugeIn));
            QVERIFY(hugeIn.status() != QDataStream::Ok);
        }
    }

    void villagersDressInTheirStyleAndProfession()
    {
        QcVillagerRecord farmer;
        farmer.style = u"qtcraft:desert"_s;
        farmer.profession = u"qtcraft:farmer"_s;
        const QcAppearance look = QcVillagerLooks::appearanceOf(*m_content, farmer);
        QCOMPARE(look.outfit, u"qtcraft:villager_robe@qtcraft:farmer"_s);
        QCOMPARE(look.hair, u"qtcraft:villager_straw_hat"_s);
        const QcAppearanceDefinition *outfit = m_content->appearance(look.outfit);
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
            const QcAppearance taiga = QcVillagerLooks::appearanceOf(*m_content, unemployed);
            QCOMPARE(taiga.outfit, u"qtcraft:villager_coat"_s);
            skins.insert(taiga.skinColor);
        }
        QVERIFY(skins.size() >= 2);
        QcVillagerRecord stranger;
        stranger.profession = u"qtcraft:smith"_s;
        stranger.style = u"mod:moon"_s;
        QCOMPARE(QcVillagerLooks::appearanceOf(*m_content, stranger).outfit, u"qtcraft:villager_tunic@qtcraft:smith"_s);
    }

    void theSettingsComeFromData()
    {
        const QcVillagerSettings &settings = m_content->villagerSettings();
        QCOMPARE(settings.mob, u"qtcraft:villager"_s);
        QCOMPARE(settings.forWorkstation(u"qtcraft:fisher"_s)->name, u"qtcraft:fisher"_s);
        QVERIFY(settings.profession(u"qtcraft:farmer"_s)->tendsCrops);
        QCOMPARE(settings.activityAt(0.75), QcVillagerActivity::Sleep);
        QCOMPARE(settings.activityAt(0.995), QcVillagerActivity::Home);

        const auto parse = [](const char *json) {
            return QcVillagerParser::parseVillagerSettings(QJsonDocument::fromJson(json).object(), u"mod"_s);
        };
        const char *valid = R"({"mob": "v", "unemployed": "none", "professions": [{"name": "none"}],
            "routine": [{"from": 0.5, "do": "sleep"}, {"from": 0.0, "do": "work"}], "look": {"outfit": "o", "skin": ["#102030"]}})";
        const QcResult<QcVillagerSettings> parsed = parse(valid);
        QVERIFY2(parsed, qPrintable(parsed ? QString() : parsed.error()));
        QCOMPARE(parsed->routine.front().activity, QcVillagerActivity::Work);
        QCOMPARE(parsed->look.skin, QList<quint32>{0x102030});
        QVERIFY(!parse(R"({"mob": "v", "unemployed": "none", "professions": [{"name": "none"}],
            "routine": [{"from": 0.0, "do": "dance"}], "look": {"outfit": "o"}})"));
        QVERIFY(!parse(R"({"mob": "v", "unemployed": "none", "professions": [{"name": "none"}],
            "routine": [{"from": 0.0, "do": "work"}], "look": {"outfit": "o", "skin": ["red"]}})"));
        QVERIFY(!parse(R"({"mob": "v", "unemployed": "farmer", "professions": [{"name": "farmer", "workstation": "farmer"}],
            "routine": [{"from": 0.0, "do": "work"}], "look": {"outfit": "o"}})"));
    }
};

QTEST_GUILESS_MAIN(TestVillagerWalks)
#include "test_villager_walks.moc"
