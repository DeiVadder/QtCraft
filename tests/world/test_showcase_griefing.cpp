// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/villages/QcServerVillagers.h"
#include "server/villages/QcVillagePopulation.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// The showcase world with the game rule mobGriefing on (decision E-510): the settlement's free
// farmers harvest their field and pick up bread, while every creature of a pen is confined and
// changes no block of its pen (the silverfish stay out of the stone, the shadewalkers take nothing),
// and the residents of the village rows are confined and leave their ripe fields as built.
// Ticks are stepped by hand.
class TestShowcaseGriefing : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    const QcShowcaseGenerator m_generator{m_content};

    std::unique_ptr<QcServer> makeServer(const QString &directory) const
    {
        QcServerConfig config;
        config.worldDirectory = directory;
        config.generator = u"showcase"_s;
        config.viewDistance = 1;
        config.autosaveSeconds = 0;
        config.keepChunksLoaded = true;
        return std::make_unique<QcServer>(config, m_content, QcWorldGenerators::create(u"showcase"_s, m_content, 0, {}, QcGeneratorVersion::current));
    }

    // Loads the chunks of the box and one ring around it.
    static void loadAround(QcServer &server, QcBlockPos low, QcBlockPos high)
    {
        for (int x = low.x - 16; x <= high.x + 16; x += 16) {
            for (int z = low.z - 16; z <= high.z + 16; z += 16) {
                server.world().loadChunk(QcBlockPos{x, 0, z}.chunk());
            }
        }
    }

    // Loads the chunks of the first placement outside `pens` that shows a spawner block.
    void loadSpawnerOnShow(QcServer &server, const QStringList &pens) const
    {
        const QcBlockId spawner = *m_content->blockId(u"qtcraft:spawner"_s);
        for (const QcStructurePlacement &placement : m_generator.plan().placements) {
            const QcStructure &structure = placement.structure();
            if (pens.contains(structure.name())) {
                continue;
            }
            for (int x = 0; x < structure.sizeX(); ++x) {
                for (int y = 0; y < structure.sizeY(); ++y) {
                    for (int z = 0; z < structure.sizeZ(); ++z) {
                        if (structure.block(x, y, z) == spawner) {
                            const QcBlockPos at = placement.toWorld(QcBlockPos{x, y, z});
                            loadAround(server, at, at);
                            return;
                        }
                    }
                }
            }
        }
    }

    // The block kinds (not states: doors open and close) of the box.
    static QList<QString> kindsIn(QcServer &server, const QcContentRegistry &content, QcBlockPos low, QcBlockPos high)
    {
        QList<QString> kinds;
        for (int y = low.y; y <= high.y; ++y) {
            for (int x = low.x; x <= high.x; ++x) {
                for (int z = low.z; z <= high.z; ++z) {
                    const std::optional<QcBlockId> block = server.world().blockAt({x, y, z});
                    kinds.append(block ? content.block(*block).name : QString());
                }
            }
        }
        return kinds;
    }

    // Ripe wheat around `centre` (8 blocks each way, ground level): the settlement's field.
    int ripeWheat(QcServer &server, QcBlockPos centre) const
    {
        int ripe = 0;
        for (int x = centre.x - 8; x <= centre.x + 8; ++x) {
            for (int z = centre.z - 8; z <= centre.z + 8; ++z) {
                const std::optional<QcBlockId> block = server.world().blockAt({x, centre.y, z});
                ripe += block && m_content->stateName(*block) == u"qtcraft:wheat_crop[age=7]"_s ? 1 : 0;
            }
        }
        return ripe;
    }

    // Whether the crop at `pos` is at its last stage (QcServerVillagers::harvest takes only those).
    bool ripe(QcServer &server, QcBlockPos pos) const
    {
        const std::optional<QcBlockId> block = server.world().blockAt(pos);
        const QcBlockDefinition *crop = block ? &m_content->block(*block) : nullptr;
        const QcBlockProperty *stages = crop && crop->crop.isCrop() ? crop->property(crop->crop.property) : nullptr;
        return stages && !stages->values.isEmpty() && m_content->property(*block, crop->crop.property) == stages->values.constLast();
    }

    // A structure of the showcase whose creatures are confined: its box and the block kinds in it.
    struct Pen
    {
        QString name;
        QcBlockPos low;
        QcBlockPos high;
        QList<QString> kinds;

        bool contains(QcBlockPos pos) const
        {
            return pos.x >= low.x && pos.x <= high.x && pos.y >= low.y && pos.y <= high.y && pos.z >= low.z && pos.z <= high.z;
        }
    };

    // The pens of the showcase with their chunks loaded: structures with confined creatures, or a
    // spawner whose creatures are confined (the dungeon and mine shaft stations).
    QList<Pen> loadedPens(QcServer &server) const
    {
        QList<Pen> pens;
        for (const QcStructurePlacement &placement : m_generator.plan().placements) {
            const QcStructure &structure = placement.structure();
            if (std::ranges::none_of(structure.mobs, &QcStructure::Mob::confined)
                && std::ranges::none_of(structure.spawners, &QcStructureDefinition::Spawner::confined)) {
                continue;
            }
            const QcBlockPos a = placement.toWorld(QcBlockPos{0, 0, 0});
            const QcBlockPos b = placement.toWorld(QcBlockPos{structure.sizeX() - 1, structure.sizeY() - 1, structure.sizeZ() - 1});
            const Pen pen{structure.name(), {std::min(a.x, b.x), a.y, std::min(a.z, b.z)}, {std::max(a.x, b.x), b.y, std::max(a.z, b.z)}, {}};
            loadAround(server, pen.low, pen.high);
            pens.append(pen);
        }
        return pens;
    }

private slots:
    void penCreaturesChangeNoBlock()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        QVERIFY(m_generator.setup().mobGriefing);

        QList<Pen> pens = loadedPens(*server);
        const QStringList names = [&pens] {
            QStringList list;
            for (const Pen &pen : std::as_const(pens)) {
                list.append(pen.name);
            }
            return list;
        }();
        for (const char *expected : {"qtcraft:silverfish_wall", "qtcraft:spider_cave", "qtcraft:chicken_coop", "qtcraft:golem_farm", "qtcraft:dungeon_station", "qtcraft:mineshaft_station",
                                     "showcase/creatures"}) {
            QVERIFY2(names.contains(QString::fromLatin1(expected)), qPrintable(names.join(u' ')));
        }

        // A spawner block on show outside the pens (the block fields), wherever the layout puts it.
        loadSpawnerOnShow(*server, names);

        for (int tick = 0; tick < 2; ++tick) {
            server->tick();
        }
        for (Pen &pen : pens) {
            pen.kinds = kindsIn(*server, *m_content, pen.low, pen.high);
        }
        // Long enough that a free silverfish would hide many times over (QcInfestation::hideChance).
        for (int tick = 0; tick < 1200; ++tick) {
            server->tick();
        }
        for (const Pen &pen : std::as_const(pens)) {
            QVERIFY2(kindsIn(*server, *m_content, pen.low, pen.high) == pen.kinds, qPrintable(pen.name));
        }
        // The spawners of the dungeon and mine shaft stations give their creatures `confined`: they
        // change no block either. Spawner blocks on show elsewhere (the block fields) stay empty.
        const QList<QcBlockPos> spawners = server->world().spawners().positions();
        qsizetype inPens = 0;
        for (const QcBlockPos &pos : spawners) {
            const bool inPen = std::ranges::any_of(pens, [pos](const Pen &pen) { return pen.contains(pos); });
            if (inPen) {
                QVERIFY(server->world().spawners().at(pos)->confined);
                ++inPens;
            } else {
                QVERIFY2(server->world().spawners().at(pos)->mob.isEmpty(), qPrintable(server->world().spawners().at(pos)->mob));
            }
        }
        QVERIFY(inPens >= 2);
        QVERIFY(spawners.size() > inPens);
        const int silverfish = *m_content->mobIndex(u"qtcraft:silverfish"_s);
        // The wall's three and the creature row's one are all still out.
        QCOMPARE(std::ranges::count(server->mobs().mobs(), silverfish, &QcCreature::type), 4);
    }

    void settlementFarmersHarvestAndPickUpBread()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        // The settlement's two villagers are the only free ones of the setup.
        QList<QcVec3> settlers;
        const QcWorldSetup setup = m_generator.setup();
        for (const QcWorldSetup::Mob &mob : setup.mobs) {
            if (!mob.confined && mob.owner.isEmpty()) {
                settlers.append(mob.at);
            }
        }
        QCOMPARE(settlers.size(), 2);
        const QcBlockPos centre{static_cast<int>(std::floor(settlers.first().x)), static_cast<int>(std::floor(settlers.first().y)),
            static_cast<int>(std::floor(settlers.first().z))};
        loadAround(*server, centre.offset(-32, 0, -32), centre.offset(32, 0, 32));
        const int ripe = ripeWheat(*server, centre);
        QVERIFY(ripe > 0);

        // Settled, they harvest their field.
        int now = ripe;
        for (int tick = 0; tick < 2 * QcVillagePopulation::villageLifeTicks + 2400 && now == ripe; ++tick) {
            server->tick();
            now = ripeWheat(*server, centre);
        }
        QVERIFY2(now < ripe, qPrintable(QString::number(now)));

        // Bread thrown at the feet of one that wants food is picked up.
        QcCreature *settler = nullptr;
        for (QcCreature &mob : server->mobs().editableMobs()) {
            if (mob.isVillager() && !mob.confined && (mob.motion.position - settlers.first()).length() < 24.0) {
                settler = &mob;
            }
        }
        QVERIFY(settler);
        settler->villager->produce = 0;
        QcItemEntityManager::Drop bread;
        bread.stack = {*m_content->itemId(u"qtcraft:bread"_s), 16, 0};
        bread.position = settler->motion.position;
        bread.pickupDelay = 0.0;
        const quint32 item = server->items().spawn(bread, server->entities().newId());
        QVERIFY(item != 0);
        const auto lying = [&server, item] {
            const QList<QcItemEntity> &items = server->items().items();
            const auto found = std::ranges::find(items, item, &QcItemEntity::id);
            return found == items.end() ? 0 : found->stack.count;
        };
        for (int tick = 0; tick < 4 * QcServerVillagers::foodTicks && lying() == 16; ++tick) {
            server->tick();
        }
        QVERIFY(lying() < 16);
    }
    void villageRowFieldsStayRipe()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        QSet<qint64> rows;
        QList<QcBlockPos> fields;
        for (const std::shared_ptr<const QcVillagePlan> &village : m_generator.plan().villages) {
            QVERIFY(village->residentsConfined);
            rows.insert(village->id);
            fields.append(QList<QcBlockPos>(village->fields.cbegin(), village->fields.cend()));
        }
        QVERIFY(!fields.isEmpty());
        QcBlockPos low = fields.constFirst();
        QcBlockPos high = low;
        for (const QcBlockPos &cell : std::as_const(fields)) {
            low = {std::min(low.x, cell.x), std::min(low.y, cell.y), std::min(low.z, cell.z)};
            high = {std::max(high.x, cell.x), std::max(high.y, cell.y), std::max(high.z, cell.z)};
        }
        loadAround(*server, low, high);
        server->tick();
        const auto ripeCells = [this, &server, &fields] {
            return std::ranges::count_if(fields, [this, &server](QcBlockPos cell) { return ripe(*server, cell); });
        };
        const qsizetype ripeAtStart = ripeCells();
        QVERIFY(ripeAtStart > 0);

        // Long enough that a free farmer harvests (settlementFarmersHarvestAndPickUpBread).
        for (int tick = 0; tick < 1200; ++tick) {
            server->tick();
        }
        QCOMPARE(ripeCells(), ripeAtStart);
        // The rows have residents, all of them confined.
        qsizetype residents = 0;
        for (const QcCreature &mob : server->mobs().mobs()) {
            if (mob.isVillager() && rows.contains(mob.villager->village)) {
                QVERIFY(mob.confined);
                ++residents;
            }
        }
        QVERIFY(residents > 0);
    }
};

QTEST_GUILESS_MAIN(TestShowcaseGriefing)
#include "test_showcase_griefing.moc"
