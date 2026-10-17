#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "QcServerTestFixture.h"
#include "server/commands/QcVillageCommands.h"
#include "server/mobs/QcMobManager.h"
#include "server/storage/QcWorldStorage.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

// The test terrain with one village around the spawn: no pieces, only its area, so the tests place
// beds and workstations themselves.
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
    std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const override
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return m_plan;
    }

private:
    std::shared_ptr<const QcVillagePlan> m_plan;
};

} // namespace

// The village state of the server (decision E-403): what generation built is counted, players'
// beds and workstations update it, it is saved with the world and kept within its limits.
class TestVillages : public QcServerTestFixture
{
    Q_OBJECT

private:
    const QcVillage *village() const { return m_server->world().villages().find(77); }

    void hold(const char *name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(QString::fromLatin1(name)).arg(count));
        ticks(1);
        send(QcMsg::SelectHotbarSlot{slotOf(item(name))});
    }

    void face(float yaw)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = yaw;
        send(QcMsg::PlayerInput{input});
    }

    // Places a bed with its foot at `foot`, the head one block north (the player looks north).
    void placeBed(QcBlockPos foot)
    {
        face(0.0f);
        hold("qtcraft:bed");
        send(QcMsg::UseItem{foot.offset(0, -1, 0), QcBlockFace::Up, {0.5, 1.0, 0.5}});
    }

    QStringList console(const QString &line) const
    {
        QStringList lines;
        m_server->runConsoleCommand(line, [&lines](const QcMsg::ChatMessage &reply) { lines.append(reply.text); });
        return lines;
    }

    QString answer(const QString &line)
    {
        m_client->chatLines.clear();
        chat(line);
        ticks(2);
        return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.back();
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void generatedVillagesCountTheirBedsAndWorkstations()
    {
        // A real village, every chunk of it loaded: the state lists what was built.
        auto generator = std::make_unique<QcTerrainGenerator>(m_content, 42);
        std::shared_ptr<const QcVillagePlan> plan;
        for (int r = 0; r < 6 && !plan; ++r) {
            plan = generator->villages().regionPlan(r, 0);
        }
        QVERIFY(plan);
        const auto expectedBeds = plan->beds.size();
        const auto expectedStations = plan->workstations.size();
        QcServerWorld world(m_content, std::move(generator));
        const QcChunkPos low = QcBlockPos{plan->minX, 0, plan->minZ}.chunk();
        const QcChunkPos high = QcBlockPos{plan->maxX, 0, plan->maxZ}.chunk();
        world.loadChunk(plan->centre.chunk());
        QCOMPARE(world.villages().count(), 1);
        for (int cz = low.z; cz <= high.z; ++cz) {
            for (int cx = low.x; cx <= high.x; ++cx) {
                world.loadChunk({cx, cz});
            }
        }

        const QcVillage *village = world.villages().find(plan->id);
        QVERIFY(village);
        QCOMPARE(village->beds.size(), expectedBeds);
        QCOMPARE(village->workstations.size(), expectedStations);
        QCOMPARE(village->centre, plan->centre);
        QCOMPARE(village->population, 0);
        for (const QcVillage::Spot &bed : village->beds) {
            QVERIFY(world.villages().blocks().isBedHead(*world.blockAt(bed.at)));
        }
    }

    void penAnimalsSpawnOnceWithTheirChunk()
    {
        // The pair of a village's animal pen (decision E-404) appears, persistent, when its chunk is
        // seen for the first time, and not again.
        auto generator = std::make_unique<QcTerrainGenerator>(m_content, 42);
        std::shared_ptr<const QcVillagePlan> plan;
        for (int r = 0; r < 64 && !plan; ++r) {
            std::shared_ptr<const QcVillagePlan> candidate = generator->villages().regionPlan(r % 8 - 4, r / 8 - 4);
            plan = candidate && !candidate->mobs.isEmpty() ? candidate : nullptr;
        }
        QVERIFY(plan);
        QCOMPARE(plan->mobs.size(), 2);
        QcServerWorld world(m_content, std::move(generator));
        const QcChunkPos chunk = QcBlockPos{int(plan->mobs[0].at.x), 0, int(plan->mobs[0].at.z)}.chunk();
        world.loadChunk(chunk);

        QcMobManager mobs(m_content, 42);
        quint32 next = 1;
        const auto populate = [&] { mobs.populateChunk(world, chunk, [&next] { return next++; }); };
        populate();
        const auto atSpot = [&](const QcVillagePlan::Mob &spot) {
            return std::ranges::count_if(mobs.mobs(), [&](const QcCreature &mob) {
                return mob.persistent && m_content->mobs()[mob.type].name == spot.mob && mob.motion.position.x == spot.at.x
                       && mob.motion.position.z == spot.at.z;
            });
        };
        for (const QcVillagePlan::Mob &spot : plan->mobs) {
            if (QcBlockPos{int(spot.at.x), 0, int(spot.at.z)}.chunk() == chunk) {
                QCOMPARE(atSpot(spot), 1);
            }
        }
        const qsizetype count = mobs.mobs().size();
        populate();
        QCOMPARE(mobs.mobs().size(), count);
    }

    void playersPlaceAndBreakBeds()
    {
        join({}, std::make_unique<VillageTerrain>(m_content));
        QVERIFY(village());
        QCOMPARE(village()->beds.size(), 0);

        const QcBlockPos foot = m_feet.offset(1, 0, -1);
        placeBed(foot);
        QCOMPARE(m_content->property(*m_server->world().blockAt(foot), u"part"_s), u"foot"_s);
        QCOMPARE(m_content->property(*m_server->world().blockAt(foot.offset(0, 0, -1)), u"part"_s), u"head"_s);
        QCOMPARE(village()->beds.size(), 1);
        QCOMPARE(village()->beds.front().at, foot.offset(0, 0, -1));
        QCOMPARE(village()->freeBeds(), 1);

        // A workstation set by a command counts by its kind.
        chat(u"/setblock %1 %2 %3 composter"_s.arg(m_feet.x - 2).arg(m_feet.y).arg(m_feet.z));
        ticks(1);
        QCOMPARE(village()->workstations.size(), 1);
        QCOMPARE(village()->workstations.front().kind, u"qtcraft:farmer"_s);

        // Breaking the foot takes the head with it and the bed out of the village.
        chat(u"/setblock %1 %2 %3 air"_s.arg(foot.x).arg(foot.y).arg(foot.z));
        ticks(2);
        QCOMPARE(m_server->world().blockAt(foot.offset(0, 0, -1)), qcAirBlockId);
        QCOMPARE(village()->beds.size(), 0);
        chat(u"/setblock %1 %2 %3 air"_s.arg(m_feet.x - 2).arg(m_feet.y).arg(m_feet.z));
        ticks(1);
        QCOMPARE(village()->workstations.size(), 0);
    }

    void aMinedBedDropsOneBed()
    {
        join({}, std::make_unique<VillageTerrain>(m_content));
        const QcBlockPos foot = m_feet.offset(1, 0, -1);
        placeBed(foot);
        const QcBlockPos head = foot.offset(0, 0, -1);
        send(QcMsg::StartMining{head});
        ticks(int(std::ceil(QcMining::breakSeconds(m_content->block(*m_server->world().blockAt(head)), nullptr) * QcPhysics::tickRate)));
        send(QcMsg::FinishMining{head});
        ticks(2);
        QCOMPARE(m_server->world().blockAt(head), qcAirBlockId);
        QCOMPARE(m_server->world().blockAt(foot), qcAirBlockId);
        QCOMPARE(droppedCount(item("qtcraft:bed")), 1);
        QCOMPARE(village()->beds.size(), 0);
    }

    void bedsAndWorkstationsKeepToTheLimits()
    {
        QcServerConfig config;
        config.limits.villageBeds = 2;
        config.limits.villageWorkstations = 1;
        config.limitsFromServer = true;
        join(config, std::make_unique<VillageTerrain>(m_content));
        for (int i = 0; i < 3; ++i) {
            placeBed(m_feet.offset(-2 + 2 * i, 0, -2));
        }
        QCOMPARE(village()->beds.size(), 2);
        for (int i = 0; i < 2; ++i) {
            m_server->world().setBlock(m_feet.offset(-2 + 2 * i, 0, 2), id("qtcraft:furnace"));
        }
        QCOMPARE(village()->workstations.size(), 1);
    }

    void villagesAreSavedWithTheWorld()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config, std::make_unique<VillageTerrain>(m_content));
        placeBed(m_feet.offset(1, 0, -1));
        QCOMPARE(village()->beds.size(), 1);
        QVERIFY(m_server->save().has_value());
        const QList<QcVillage> saved = m_server->world().villages().villages();
        cleanup();

        const QcResult<QList<QcVillage>> read = QcWorldStorage(directory.path(), m_content).readVillages({});
        QVERIFY(read);
        QCOMPARE(*read, saved);

        // Reopened, the village is known before any of its chunks is loaded again.
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<VillageTerrain>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        QVERIFY(village());
        QCOMPARE(village()->beds.size(), 1);
    }

    void damagedVillageEntriesAreLeftOut()
    {
        QcVillage village;
        village.id = 5;
        village.centre = {10, 70, 10};
        village.minX = 0;
        village.maxX = 20;
        village.minY = 60;
        village.maxY = 80;
        village.minZ = 0;
        village.maxZ = 20;
        village.beds = {{{5, 70, 5}, {}, u"villager-1"_s}, {{500, 70, 5}, {}, {}}};
        const std::optional<QcVillage> back = QcVillage::fromJson(village.toJson(), {});
        QVERIFY(back);
        // The bed far outside the village is dropped, the claim kept.
        QCOMPARE(back->beds.size(), 1);
        QCOMPARE(back->beds.front().claimedBy, u"villager-1"_s);

        QJsonObject wide = village.toJson();
        wide.insert(u"max"_s, QJsonArray{5000, 80, 20});
        QVERIFY(!QcVillage::fromJson(wide, {}));
        // No wider than a region (a plan never leaves its own).
        QJsonObject regionWide = village.toJson();
        regionWide.insert(u"max"_s, QJsonArray{384, 80, 20});
        QVERIFY(!QcVillage::fromJson(regionWide, {384, 64, 64}));
        QVERIFY(QcVillage::fromJson(regionWide, {385, 64, 64}));
        QJsonObject noId = village.toJson();
        noId.remove(u"id"_s);
        QVERIFY(!QcVillage::fromJson(noId, {}));
    }

    void aDamagedVillagesFileIsKeptAside()
    {
        // The state follows from the plans again (decision E-409): the world opens, the file is
        // kept aside and the village comes back when its chunk loads.
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config, std::make_unique<VillageTerrain>(m_content));
        QVERIFY(m_server->save().has_value());
        cleanup();
        const QString path = QDir(directory.path()).filePath(u"villages.json"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(file.size() / 2));
        file.close();

        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<VillageTerrain>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        QVERIFY(QFile::exists(path + u".damaged"_s));
        QVERIFY(!QFile::exists(path));
        QVERIFY(!village());
        QVERIFY(std::ranges::any_of(m_server->openWarnings(), [](const QString &w) { return w.contains(u"villages.json.damaged"_s); }));
        m_server->world().loadChunk(VillageTerrain(m_content).spawnPoint().chunk());
        QVERIFY(village());

        // The next save writes a readable file again.
        QVERIFY(m_server->save().has_value());
        QVERIFY(QcWorldStorage(directory.path(), m_content).readVillages({}).has_value());
        QVERIFY(QFile::exists(path));
    }

    void unchangedVillagesAreNotWrittenAgain()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config, std::make_unique<VillageTerrain>(m_content));
        QVERIFY(m_server->save().has_value());
        const QString path = QDir(directory.path()).filePath(u"villages.json"_s);
        QVERIFY(QFile::remove(path));

        QVERIFY(m_server->save().has_value());
        QVERIFY(!QFile::exists(path));
        placeBed(m_feet.offset(1, 0, -1));
        QCOMPARE(village()->beds.size(), 1);
        QVERIFY(m_server->save().has_value());
        QVERIFY(QFile::exists(path));
    }

    void savedSpotsKeepToTheirOwnLimits()
    {
        // Each list is cut to its own limit, not both to the larger one.
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.limits.villageBeds = 8;
        config.limits.villageWorkstations = 64;
        config.limitsFromServer = true;
        join(config, std::make_unique<VillageTerrain>(m_content));
        QVERIFY(m_server->save().has_value());
        cleanup();

        QcVillage saved;
        saved.id = 77;
        saved.centre = {10, 70, 10};
        saved.minX = 0;
        saved.maxX = 40;
        saved.minY = 60;
        saved.maxY = 80;
        saved.minZ = 0;
        saved.maxZ = 40;
        for (int i = 0; i < 20; ++i) {
            saved.beds.append(QcVillage::Spot{{i, 70, 5}, {}, {}});
            saved.workstations.append(QcVillage::Spot{{i, 70, 9}, u"farmer"_s, {}});
        }
        QFile file(QDir(directory.path()).filePath(u"villages.json"_s));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QcWorldStorage::villagesBytes({saved}));
        file.close();

        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<VillageTerrain>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        QVERIFY(village());
        QCOMPARE(village()->beds.size(), 8);
        QCOMPARE(village()->workstations.size(), 20);
    }

    void commandsFindTheNearestVillage()
    {
        join({}, std::make_unique<VillageTerrain>(m_content));
        placeBed(m_feet.offset(1, 0, -1));

        const QJsonObject state = QJsonDocument::fromJson(answer(u"/village"_s).toUtf8()).object();
        QCOMPARE(state.value(u"id"_s).toString(), u"77"_s);
        QCOMPARE(state.value(u"style"_s).toString(), u"qtcraft:plains"_s);
        QCOMPARE(state.value(u"known"_s).toBool(), true);
        QCOMPARE(state.value(u"beds"_s).toArray().size(), 1);
        QCOMPARE(state.value(u"freeBeds"_s).toInt(), 1);
        QCOMPARE(state.value(u"population"_s).toInt(), 0);
        QCOMPARE(state.value(u"layout"_s).toString(), u"cross"_s);
        QCOMPARE(state.value(u"winding"_s).toBool(), false);

        const QString located = answer(u"/locate village"_s);
        QVERIFY2(located.startsWith(u"Village (qtcraft:plains, cross) at %1 "_s.arg(m_feet.x)), qPrintable(located));

        // The console names a position; the real generator plans villages it has not built yet.
        const QStringList lines = console(u"village 0 0"_s);
        QCOMPARE(lines.size(), 1);
        QVERIFY(lines.front().contains(u"\"id\":\"77\""_s));
    }

    void plannedVillagesAreFoundBeforeTheyAreBuilt()
    {
        join({}, std::make_unique<QcTerrainGenerator>(m_content, 42));
        const QStringList lines = console(u"village 0 0"_s);
        QCOMPARE(lines.size(), 1);
        const QJsonObject state = QJsonDocument::fromJson(lines.front().toUtf8()).object();
        QCOMPARE(state.value(u"known"_s).toBool(), false);
        QVERIFY(state.value(u"beds"_s).toArray().size() >= 1);
        const QJsonArray centre = state.value(u"centre"_s).toArray();
        QVERIFY(std::hypot(centre[0].toDouble(), centre[2].toDouble()) <= QcVillageCommands::searchRadius);
        QVERIFY(QStringList({u"qtcraft:plains"_s, u"qtcraft:desert"_s, u"qtcraft:taiga"_s, u"qtcraft:swamp"_s}).contains(state.value(u"style"_s).toString()));
        QVERIFY(QStringList({u"cross"_s, u"street"_s, u"t"_s, u"l"_s, u"plaza"_s, u"tree"_s}).contains(state.value(u"layout"_s).toString()));
    }
};

QTEST_GUILESS_MAIN(TestVillages)
#include "test_villages.moc"
