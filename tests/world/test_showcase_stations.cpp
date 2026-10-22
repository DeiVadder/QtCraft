#include "common/core/QcAabb.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcInProcessTransport.h"
#include "QcRawClient.h"
#include "QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/storage/QcWorldStorage.h"
#include "world/QcShowcaseTestUtil.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The stations and pens of the showcase world as the server runs them: the adventure, dungeon,
// desert temple, jungle temple and witch hut stations and the creature pens of stage 52.
class TestShowcaseStations : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // A pen of the showcase (a structure) with the kinds of creatures it holds, how many, and the
    // box it takes in the world.
    struct ShowcasePen
    {
        QString structure;
        QStringList kinds;
        qsizetype creatures = 0;
        QcAabb box;
    };

    // `pens` with the box of each one's placement in the showcase plan; loads the chunks around them.
    QList<ShowcasePen> placePens(QcServer &server, const QcShowcaseGenerator &generator, QList<ShowcasePen> pens) const
    {
        for (ShowcasePen &pen : pens) {
            const auto placement = std::ranges::find_if(generator.plan().placements, [&](const QcStructurePlacement &placed) {
                return placed.structure().name() == pen.structure;
            });
            if (placement == generator.plan().placements.end()) {
                QTest::qFail(qPrintable(u"no placement of "_s + pen.structure), __FILE__, __LINE__);
                return {};
            }
            const QcStructure &structure = placement->structure();
            const QcBlockPos a = placement->toWorld(QcBlockPos{0, 0, 0});
            const QcBlockPos b = placement->toWorld(QcBlockPos{structure.sizeX() - 1, structure.sizeY() - 1, structure.sizeZ() - 1});
            pen.box = {{double(std::min(a.x, b.x)), double(a.y), double(std::min(a.z, b.z))},
                       {double(std::max(a.x, b.x) + 1), double(b.y + 1), double(std::max(a.z, b.z) + 1)}};
            for (int x = int(pen.box.min.x) - 16; x <= int(pen.box.max.x) + 16; x += 16) {
                for (int z = int(pen.box.min.z) - 16; z <= int(pen.box.max.z) + 16; z += 16) {
                    server.world().loadChunk(QcBlockPos{x, 0, z}.chunk());
                }
            }
        }
        return pens;
    }

    // Names a pen whose count is off: its box, every creature of its kinds with health and position,
    // and every departure of such a creature so far (with its reason and last position).
    QString penReport(QcServer &server, const ShowcasePen &pen, const QStringList &departures) const
    {
        QString report = u"%1 box (%2 %3 %4)-(%5 %6 %7):"_s.arg(pen.structure)
                             .arg(pen.box.min.x).arg(pen.box.min.y).arg(pen.box.min.z)
                             .arg(pen.box.max.x).arg(pen.box.max.y).arg(pen.box.max.z);
        for (const QcCreature &mob : server.mobs().mobs()) {
            const QString &kind = m_content->mobs()[mob.type].name;
            if (pen.kinds.contains(kind)) {
                const QcVec3 &at = mob.motion.position;
                report += u" [%1 #%2 health %3 at %4 %5 %6]"_s.arg(kind).arg(mob.id).arg(mob.health).arg(at.x).arg(at.y).arg(at.z);
            }
        }
        for (const QString &departure : departures) {
            if (std::ranges::any_of(pen.kinds, [&](const QString &kind) { return departure.startsWith(kind); })) {
                report += u" departed: "_s + departure;
            }
        }
        return report;
    }

    // Water creatures whose middle is not in a liquid.
    qsizetype fishOnLand(QcServer &server) const
    {
        return std::ranges::count_if(server.mobs().mobs(), [&](const QcCreature &mob) {
            const QcMobDefinition &kind = m_content->mobs()[mob.type];
            const QcVec3 centre = mob.motion.position + QcVec3{0.0, kind.height * 0.5, 0.0};
            return kind.aquatic && !QcPhysics::isLiquidAt(server.world(), QcPhysics::blockContaining(centre));
        });
    }

private slots:
    // The adventure station (decision E-735) brings its allow-list entries into the showcase world,
    // which keeps them in world.json.
    void theAdventureStationAllowsItsBlocks()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = QcShowcaseTestUtil::makeServer(m_content, directory.path(), u"showcase"_s, 0);
        QVERIFY(server->openWorld());
        QVERIFY(server->stop());

        const QcResult<QcWorldInfo> info = QcWorldStorage(directory.path(), nullptr).readInfo();
        QVERIFY(info);
        using Action = QcAdventureRules::Action;
        QCOMPARE(info->adventure.blocks(Action::Break, u"qtcraft:stone_pickaxe"_s), QStringList{u"qtcraft:mossy_cobblestone"_s});
        QCOMPARE(info->adventure.blocks(Action::PlaceOn, u"qtcraft:lime_wool"_s), QStringList{u"qtcraft:lime_concrete"_s});

        const QcShowcaseGenerator generator(m_content);
        QVERIFY(std::ranges::any_of(generator.plan().placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name() == u"qtcraft:adventure_station"_s;
        }));
    }

    // The dungeon station (decision E-520): its spawner brings forth confined zombies, its chest
    // holds loot from the dungeon table, and the torch of its corridor stub lies outside the light
    // reach of the spawn area.
    void theDungeonStationHasItsSpawnerAndLoot()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = QcShowcaseTestUtil::makeServer(m_content, directory.path(), u"showcase"_s, 0);
        QVERIFY(server->openWorld());
        const QcShowcaseGenerator generator(m_content);
        const auto station = std::ranges::find_if(generator.plan().placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name().endsWith(u"dungeon_station"_s);
        });
        QVERIFY(station != generator.plan().placements.end());
        const QcStructure &structure = station->structure();
        QCOMPARE(structure.spawners.size(), 1);
        QCOMPARE(structure.loot.size(), 1);
        const QcBlockPos spawner = station->toWorld(structure.spawners[0].at);
        const QcBlockPos chest = station->toWorld(structure.loot[0].at);
        server->world().loadChunk(spawner.chunk());
        server->world().loadChunk(chest.chunk());

        const QcSpawners::Entry *entry = server->world().spawners().at(spawner);
        QVERIFY(entry);
        QCOMPARE(entry->mob, u"qtcraft:zombie"_s);
        QVERIFY(entry->confined);
        const QcBlockEntities::Container *container = server->blockEntities().container(chest);
        QVERIFY(container && std::ranges::any_of(container->stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); }));

        const QcBlockId torch = *m_content->stateId(u"qtcraft:torch[attach=north]"_s);
        int torches = 0;
        for (int x = 0; x < station->sizeX(); ++x) {
            for (int z = 0; z < station->sizeZ(); ++z) {
                for (int y = 0; y < structure.sizeY(); ++y) {
                    const QcBlockPos at = station->toWorld(QcBlockPos{x, y, z});
                    if (server->world().loadChunk(at.chunk()); server->world().blockAt(at) == torch) {
                        ++torches;
                        const int distance = std::max(0, std::abs(at.x - spawner.x) - 4) + std::max(0, std::abs(at.y - spawner.y) - 1)
                                             + std::max(0, std::abs(at.z - spawner.z) - 4);
                        QVERIFY2(distance >= 15, qPrintable(QString::number(distance)));
                    }
                }
            }
        }
        QCOMPARE(torches, 1);
    }

    // The desert temple station (decisions E-591, E-593): its cellar gets a sand golem that keeps to
    // it, and the golem of its farm appears on the pillar in the pool and crumbles into sand and its
    // head, with a viewer in reach but not too close.
    void theDesertTempleStationGuardsAndCrumbles()
    {
        QcServerConfig config;
        config.generator = u"showcase"_s;
        config.viewDistance = 4;
        const auto generator = std::make_shared<QcShowcaseGenerator>(m_content);
        const auto station = std::ranges::find_if(generator->plan().placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name() == u"qtcraft:desert_temple_station"_s;
        });
        QVERIFY(station != generator->plan().placements.end());
        const QList<QcGuardSite> sites = QcGuardSite::of(*station);
        QCOMPARE(sites.size(), 2);
        const QcBlockPos middle = station->toWorld(QcBlockPos{13, 0, 4});
        config.spawnColumn = std::array<int, 2>{middle.x, middle.z + 20};
        QcServer server(config, m_content, QcWorldGenerators::create(u"showcase"_s, m_content, 0));
        QVERIFY(server.openWorld());
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        server.addClient(std::move(serverEnd), QcServer::ClientOrigin::Local);
        QcRawClient client(std::move(clientEnd));
        client.hello(u"Viewer"_s);
        QcTestUtil::pump();

        const int golem = *m_content->mobIndex(u"qtcraft:sand_golem"_s);
        const auto guarding = [&](const QcGuardSite &site) {
            return std::ranges::count_if(server.mobs().mobs(), [&](const QcCreature &mob) { return mob.type == golem && mob.guards && mob.guards->site == site.id; });
        };
        int ticks = 0;
        for (; ticks < 800 && (guarding(sites[0]) == 0 || server.creatures().crumbling().crumbled() == 0); ++ticks) {
            server.tick();
            QcTestUtil::pump();
        }
        QVERIFY2(guarding(sites[0]) == 1, qPrintable(QString::number(ticks)));
        QVERIFY2(server.creatures().crumbling().crumbled() >= 1, qPrintable(QString::number(ticks)));
        // The pillar carries the crumbled golem's feet, the cellar's golem stands in the cellar.
        const QcBlockPos pad = station->toWorld(QcBlockPos{22, 1, 4});
        QCOMPARE(m_content->block(*server.world().blockAt(pad)).name, u"qtcraft:sand"_s);
        const auto cellarGolem = std::ranges::find_if(server.mobs().mobs(), [&](const QcCreature &mob) { return mob.guards && mob.guards->site == sites[0].id; });
        QVERIFY(sites[0].contains(QcPhysics::blockContaining(cellarGolem->motion.position)));
    }

    void theJungleTempleAndWitchHutStationsWork()
    {
        // The temple's cellar: its dispensers hold arrows, the left lever opens the hidden chamber
        // (decision E-621). The hut: its witch stays inside behind glass (decisions E-622, E-623).
        QcServerConfig config;
        config.generator = u"showcase"_s;
        config.viewDistance = 4;
        const auto generator = std::make_shared<QcShowcaseGenerator>(m_content);
        const auto stationNamed = [&](const QString &name) {
            return std::ranges::find_if(generator->plan().placements, [&](const QcStructurePlacement &placement) { return placement.structure().name() == name; });
        };
        const auto temple = stationNamed(u"qtcraft:jungle_temple_station"_s);
        const auto hut = stationNamed(u"qtcraft:witch_hut_station"_s);
        QVERIFY(temple != generator->plan().placements.end() && hut != generator->plan().placements.end());
        const QcBlockPos middle = temple->toWorld(QcBlockPos{6, 0, 7});
        config.spawnColumn = std::array<int, 2>{middle.x, middle.z + 12};
        QcServer server(config, m_content, QcWorldGenerators::create(u"showcase"_s, m_content, 0));
        QVERIFY(server.openWorld());
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        server.addClient(std::move(serverEnd), QcServer::ClientOrigin::Local);
        QcRawClient client(std::move(clientEnd));
        client.hello(u"Viewer"_s);
        QcTestUtil::pump();
        for (int i = 0; i < 10; ++i) {
            server.tick();
            QcTestUtil::pump();
        }
        for (const auto &[x, z] : {std::pair{8, 2}, {10, 2}, {11, 10}}) {
            const QcBlockEntities::Container *dispenser = server.blockEntities().container(temple->toWorld(QcBlockPos{x, 1, z}));
            QVERIFY(dispenser && std::ranges::any_of(dispenser->stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); }));
        }
        const QcBlockPos door = temple->toWorld(QcBlockPos{4, 1, 4});
        QCOMPARE(m_content->block(*server.world().blockAt(door)).name, u"qtcraft:mossy_cobblestone"_s);
        QVERIFY(server.systems().circuits.use(temple->toWorld(QcBlockPos{6, 1, 3})));
        for (int i = 0; i < 12; ++i) {
            server.tick();
            QcTestUtil::pump();
        }
        QCOMPARE(*server.world().blockAt(door), qcAirBlockId);

        // The witch: confined in her hut, still inside after a while.
        const int witchType = *m_content->mobIndex(u"qtcraft:witch"_s);
        server.world().loadChunk(hut->origin().chunk());
        server.world().loadChunk(hut->toWorld(QcBlockPos{8, 0, 10}).chunk());
        for (int i = 0; i < 100; ++i) {
            server.tick();
            QcTestUtil::pump();
        }
        const auto witch = std::ranges::find_if(server.mobs().mobs(), [&](const QcCreature &mob) {
            const QcBlockPos at = QcPhysics::blockContaining(mob.motion.position);
            return mob.type == witchType && at.x >= hut->origin().x && at.x < hut->origin().x + hut->sizeX() && at.z >= hut->origin().z
                   && at.z < hut->origin().z + hut->sizeZ();
        });
        QVERIFY(witch != server.mobs().mobs().cend());
        QVERIFY(witch->confined);
        const QcBlockPos feet = QcPhysics::blockContaining(witch->motion.position);
        QCOMPARE(feet.y, hut->origin().y + 4);
        QVERIFY(server.entities().projectiles().all().isEmpty());
    }

    // The pens of stages 52.2 and 52.4 (decisions E-451 to E-453, E-500 to E-503): the spider cave,
    // the silverfish wall, the chicken coop, the cow pasture, the aquarium and the fishing pier hold
    // their creatures for a minute of game time; none escapes or hides (fish stay in their water).
    void creaturePensOfStage52HoldTheirCreatures()
    {
        QTemporaryDir directory;
        // Outlives the server, whose departure listener writes to it.
        QStringList departures;
        std::unique_ptr<QcServer> server = QcShowcaseTestUtil::makeServer(m_content, directory.path(), u"showcase"_s, 0);
        QVERIFY(server->openWorld());
        const QcShowcaseGenerator generator(m_content);
        using Pen = ShowcasePen;
        const QList<Pen> pens = placePens(*server, generator, {{u"qtcraft:spider_cave"_s, {u"qtcraft:spider"_s, u"qtcraft:cave_spider"_s}, 3, {}},
                        {u"qtcraft:silverfish_wall"_s, {u"qtcraft:silverfish"_s}, 3, {}},
                        {u"qtcraft:chicken_coop"_s, {u"qtcraft:chicken"_s}, 4, {}},
                        {u"qtcraft:cow_pasture"_s, {u"qtcraft:cow"_s}, 3, {}},
                        {u"qtcraft:aquarium"_s, {u"qtcraft:cod"_s, u"qtcraft:salmon"_s}, 7, {}},
                        {u"qtcraft:fishing_pier"_s, {u"qtcraft:cod"_s}, 3, {}},
                        {u"qtcraft:river_station"_s, {u"qtcraft:salmon"_s}, 2, {}}});
        if (QTest::currentTestFailed()) {
            return;
        }
        server->mobs().onDeparture([&](const QcCreature &mob, QcMobManager::Departure why) {
            const QcVec3 &at = mob.motion.position;
            departures += u"%1 #%2 reason %3 health %4 at %5 %6 %7"_s.arg(m_content->mobs()[mob.type].name).arg(mob.id).arg(int(why))
                              .arg(mob.health).arg(at.x).arg(at.y).arg(at.z);
        });

        const auto inside = [&](const Pen &pen) {
            return std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
                const QcVec3 &at = mob.motion.position;
                return pen.kinds.contains(m_content->mobs()[mob.type].name) && at.x >= pen.box.min.x && at.x <= pen.box.max.x
                       && at.z >= pen.box.min.z && at.z <= pen.box.max.z && at.y >= pen.box.min.y && at.y <= pen.box.max.y;
            });
        };
        // The creature area's row of glass pens holds the same kinds; cod live in two of the pens.
        const QcShowcaseAreaInfo row = *std::ranges::find(generator.plan().areas, u"creatures"_s, &QcShowcaseAreaInfo::name);
        const auto escaped = [&](const Pen &pen) {
            return std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
                const QcVec3 &at = mob.motion.position;
                const bool inRow = at.x >= row.origin.x && at.x <= row.origin.x + row.sizeX && at.z >= row.origin.z && at.z <= row.origin.z + row.sizeZ;
                const bool inPen = std::ranges::any_of(pens, [&](const Pen &other) {
                    return at.x >= other.box.min.x && at.x <= other.box.max.x && at.z >= other.box.min.z && at.z <= other.box.max.z;
                });
                return pen.kinds.contains(m_content->mobs()[mob.type].name) && !inRow && !inPen;
            });
        };
        for (int tick = 0; tick < 2; ++tick) {
            server->tick();
        }
        for (const Pen &pen : std::as_const(pens)) {
            QVERIFY2(inside(pen) == pen.creatures, qPrintable(penReport(*server, pen, departures)));
        }
        QHash<quint32, QcVec3> start;
        for (const QcCreature &mob : server->mobs().mobs()) {
            start.insert(mob.id, mob.motion.position);
        }
        for (int tick = 0; tick < 1200; ++tick) {
            server->tick();
        }
        // They are awake and walk about their pens.
        const auto moved = std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
            return start.contains(mob.id) && (mob.motion.position - start.value(mob.id)).length() > 0.5;
        });
        QVERIFY2(moved >= 5, qPrintable(QString::number(moved)));
        for (const Pen &pen : std::as_const(pens)) {
            QVERIFY2(inside(pen) == pen.creatures && escaped(pen) == 0, qPrintable(penReport(*server, pen, departures)));
        }
        // The fish swim: none lies on land.
        QCOMPARE(fishOnLand(*server), 0);
    }
};

QTEST_GUILESS_MAIN(TestShowcaseStations)
#include "test_showcase_stations.moc"
