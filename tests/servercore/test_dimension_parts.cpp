// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcDimensionParser.h"
#include "common/maps/QcMapPixel.h"
#include "common/world/QcDayCycle.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/maps/QcMapStore.h"
#include "server/maps/QcMapSurvey.h"
#include "server/maps/QcServerMaps.h"

#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {

constexpr auto emptyName = "test:empty";

// The base content with an empty test dimension: a stone platform at the origin, no weather.
std::shared_ptr<QcContentRegistry> contentWithDimension()
{
    const QJsonObject json{{u"name"_s, QString::fromLatin1(emptyName)},
                           {u"generator"_s, QJsonObject{{u"type"_s, u"void"_s}, {u"options"_s, u"platform=qtcraft:stone,radius=1,y=1"_s}}},
                           {u"weather"_s, false}};
    QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
    if (!dimension) {
        qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
    }
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    content->worldgen().registerDimension(std::move(*dimension));
    return content;
}

} // namespace

// What runs once per dimension in a part that exists once for the server (decision E-988): each
// dimension keeps its own, nothing of one reaches the other, and a second open dimension changes
// nothing in the first.
class TestDimensionParts : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr int fullInventory = 64 * QcInventory::slotCount;

    QcServerDimension *m_elsewhere = nullptr;
    std::unique_ptr<RawClient> m_other;

    // A server with the test dimension open; nothing spawns or changes with the time.
    void start()
    {
        m_content = contentWithDimension();
        QcServerConfig config;
        config.operators = {u"Raw"_s, u"Other"_s};
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule daylightCycle false"_s);
        m_server->setWorldTime(QcDayCycle::dayLength * 10.1);
        QcResult<QcServerDimension *> opened = m_server->openDimension(QString::fromLatin1(emptyName));
        QVERIFY2(opened, opened ? "" : qPrintable(opened.error()));
        m_elsewhere = *opened;
    }

    // Sets the player of the raw client on the platform of the test dimension.
    void goElsewhere()
    {
        chat(u"/tp 0.5 2 0.5 in %1"_s.arg(QString::fromLatin1(emptyName)));
        ticks(3);
    }

    // A second player, in the overworld.
    void joinOther()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), QcServer::ClientOrigin::Remote);
        m_other = std::make_unique<RawClient>(std::move(clientEnd));
        m_other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
    }

    // The second player, in the test dimension where the raw client stands in the overworld, on a
    // floor of stone. Chunks there load with him.
    void otherToTheSamePlaceElsewhere() { otherElsewhereAt(m_feet); }

    // The second player, in the test dimension at `feet` on a floor of stone.
    void otherElsewhereAt(QcBlockPos feet)
    {
        joinOther();
        chat(u"/tp Other %1 %2 %3 in %4"_s.arg(feet.x + 0.5).arg(feet.y).arg(feet.z + 0.5).arg(QString::fromLatin1(emptyName)));
        ticks(5);
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                m_elsewhere->world().setBlock(feet.offset(dx, -1, dz), id("qtcraft:stone"));
            }
        }
        ticks(2);
    }

    // A full inventory of dirt and a stack of it in the crafting grid, which has nowhere to go.
    void fillInventoryAndGrid()
    {
        chat(u"/give dirt %1"_s.arg(fullInventory));
        move(0, QcMsg::MoveInventoryItem::craftingGridSlot);
        chat(u"/give dirt 64"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), fullInventory);
    }

    // What is left of the grid: nothing in it, the inventory still full.
    void checkGridIsEmptyAndInventoryFull()
    {
        QVERIFY(std::ranges::all_of(m_client->craftingGrid, &QcItemStack::isEmpty));
        QCOMPARE(countOf(item("qtcraft:dirt")), fullInventory);
    }

    // The dirt lying within a few blocks of `around` in `items`.
    int dirtNear(const QList<QcItemEntity> &items, const QcVec3 &around) const
    {
        int count = 0;
        for (const QcItemEntity &drop : items) {
            if (drop.stack.item == item("qtcraft:dirt") && (drop.motion.position - around).length() < 6.0) {
                count += drop.stack.count;
            }
        }
        return count;
    }

    static QcVec3 centreOf(QcBlockPos feet) { return {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5}; }

    void chatOther(const QString &text)
    {
        m_other->connection.send(QcMsg::ChatInput{text});
        QcTestUtil::pump();
    }

    template<typename Condition>
    bool tickUntil(Condition condition, int limit)
    {
        for (int tick = 0; tick < limit && !condition(); ++tick) {
            ticks(1);
        }
        return condition();
    }

    static qsizetype soundCount(const RawClient &client, const QString &sound) { return client.sounds.count(sound); }

    // Spawns a creature of `kind` beside the player in the test dimension.
    quint32 spawnElsewhere(const QString &kind, const QcVec3 &at) const
    {
        QcEntities &entities = m_elsewhere->entities();
        return entities.creatures().spawn(*m_content->mobKind(kind), at, entities.newId());
    }

    // Hits the creature `entity` in the test dimension after the player's cooldown.
    void hitElsewhere(quint32 entity)
    {
        idle(12);
        QcCreature *mob = m_elsewhere->entities().creatures().find(entity);
        QVERIFY(mob);
        mob->motion.position = {1.5, 2.0, 0.5};
        mob->motion.velocity = {};
        send(QcMsg::AttackEntity{entity});
        ticks(1);
    }

private slots:
    void cleanup()
    {
        m_other.reset();
        m_client.reset();
        m_server.reset();
        m_elsewhere = nullptr;
    }

    void aTradeElsewhereEndsWhenTheVillagerIsOutOfReach()
    {
        start();
        goElsewhere();
        const quint32 entity = spawnElsewhere(u"qtcraft:villager"_s, {1.5, 2.0, 0.5});
        ticks(1);
        chat(u"/profession qtcraft:farmer"_s);
        QcCreature *villager = m_elsewhere->entities().creatures().find(entity);
        QVERIFY(villager);
        send(QcMsg::UseEntity{entity});
        ticks(2);
        QVERIFY(m_client->trade);

        villager = m_elsewhere->entities().creatures().find(entity);
        QVERIFY(villager);
        villager->motion.position = {0.5, 2.0, 30.5};
        ticks(3);
        QVERIFY(m_client->tradeClosed);
    }

    void aChestOpenWithASecondDimensionSoundsOnce()
    {
        start();
        const QcBlockPos chest = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(chest, id("qtcraft:chest"));
        send(QcMsg::UseItem{chest, QcBlockFace::West});
        ticks(10);
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(chest));
        QCOMPARE(soundCount(*m_client, u"qtcraft:block.chest.open"_s), 1);
        QCOMPARE(soundCount(*m_client, u"qtcraft:block.chest.close"_s), 0);
    }

    void seedsPlantThemselvesWithASecondDimensionOpen()
    {
        start();
        const QcBlockPos soil = m_feet.offset(2, -1, 0);
        m_server->world().setBlock(soil, id("qtcraft:farmland"));
        QcItemEntityManager::Drop seeds;
        seeds.stack = {item("qtcraft:wheat_seeds"), 1, 0};
        seeds.position = {soil.x + 0.5, soil.y + 1.2, soil.z + 0.5};
        seeds.pickupDelay = 1000.0;
        QVERIFY(m_server->items().spawn(seeds, m_server->entities().newId()) != 0);
        QVERIFY(tickUntil([&] { return m_server->world().blockAt(soil.offset(0, 1, 0)) != qcAirBlockId; }, 400));
    }

    void villagersPickUpFoodWithASecondDimensionOpen()
    {
        start();
        const QcVec3 at{m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        const quint32 entity = m_server->entities().newId();
        m_server->mobs().spawn(*m_content->mobKind(u"qtcraft:villager"_s), at, entity);
        QcItemEntityManager::Drop bread;
        bread.stack = {item("qtcraft:bread"), 16, 0};
        bread.position = at;
        bread.pickupDelay = 0.0;
        QVERIFY(m_server->items().spawn(bread, m_server->entities().newId()) != 0);
        QVERIFY(tickUntil([&] { return m_server->mobs().find(entity)->villager->produce > 0; }, 100));
    }

    void aParentDyingElsewhereTakesItsChildren()
    {
        start();
        goElsewhere();
        const quint32 parent = spawnElsewhere(u"qtcraft:kjut"_s, {1.5, 2.0, 0.5});
        const quint32 child = spawnElsewhere(u"qtcraft:kjut_child"_s, {0.5, 2.0, -0.5});
        QcMobManager &creatures = m_elsewhere->entities().creatures();
        creatures.find(parent)->noAi = true;
        creatures.find(parent)->health = 0.5;
        creatures.find(child)->parent = parent;
        hitElsewhere(parent);
        ticks(1);
        QVERIFY(!creatures.find(parent));
        QVERIFY(!creatures.find(child));
        QCOMPARE(m_server->creatures().families().popped(), 1);
    }

    void aParentTakenAwayOutsideItsDimensionsScopeTakesItsChildren()
    {
        start();
        const quint32 parent = spawnElsewhere(u"qtcraft:kjut"_s, {1.5, 2.0, 0.5});
        const quint32 child = spawnElsewhere(u"qtcraft:kjut_child"_s, {0.5, 2.0, -0.5});
        QcMobManager &creatures = m_elsewhere->entities().creatures();
        creatures.find(child)->parent = parent;
        // Here the current dimension is the overworld, as in any entry point without a Scope.
        creatures.remove(parent);
        ticks(2);
        QVERIFY(!creatures.find(child));
    }

    void aCreatureDyingElsewhereDropsWhatItCarriesThere()
    {
        start();
        goElsewhere();
        const quint32 entity = spawnElsewhere(u"qtcraft:shadewalker"_s, {1.5, 2.0, 0.5});
        QcCreature *mob = m_elsewhere->entities().creatures().find(entity);
        mob->noAi = true;
        m_elsewhere->entities().creatures().carryBlock(*mob, id("qtcraft:grass_block"));
        mob->health = 0.5;
        hitElsewhere(entity);
        ticks(1);
        const QList<QcItemEntity> &here = m_elsewhere->entities().items().items();
        QVERIFY(std::ranges::any_of(here, [this](const QcItemEntity &drop) { return drop.stack.item == item("qtcraft:grass_block"); }));
        QVERIFY(m_server->items().items().isEmpty());
    }

    void aGameModeForAPlayerElsewhereActsWhereHeIs()
    {
        start();
        joinOther();
        const QString name = QString::fromLatin1(emptyName);
        chat(u"/gamemode spectator Other"_s);
        chat(u"/tp Other 0.5 2 0.5 in %1"_s.arg(name));
        ticks(3);
        // He stands in the stone of a pillar there; in the overworld this is another place.
        for (int y = 1; y <= 6; ++y) {
            m_elsewhere->world().setBlock({0, y, 0}, id("qtcraft:stone"));
        }
        chat(u"/gamemode survival Other"_s);
        ticks(2);
        QCOMPARE(m_other->motion.position.y, 7.0);
    }

    void soundsAndBlockChangesStayInTheirDimension()
    {
        start();
        otherToTheSamePlaceElsewhere();
        m_client->sounds.clear();
        m_client->blockUpdates.clear();
        m_other->blockUpdates.clear();

        const QcBlockPos chest = m_feet.offset(1, 0, 0);
        chatOther(u"/setblock %1 %2 %3 qtcraft:chest"_s.arg(chest.x).arg(chest.y).arg(chest.z));
        ticks(2);
        const auto reaches = [&chest](const RawClient &client) {
            return std::ranges::any_of(client.blockUpdates, [&chest](QcMsg::BlockUpdate update) { return update.pos == chest; });
        };
        QVERIFY(reaches(*m_other));
        QVERIFY(!reaches(*m_client));

        m_other->connection.send(QcMsg::UseItem{chest, QcBlockFace::West});
        QcTestUtil::pump();
        ticks(2);
        QCOMPARE(soundCount(*m_other, u"qtcraft:block.chest.open"_s), 1);
        QCOMPARE(soundCount(*m_client, u"qtcraft:block.chest.open"_s), 0);
    }

    void aPlayerIsToldTheDimensionHeEnters()
    {
        start();
        const QList<QcChunkPos> before = m_client->chunks;
        QVERIFY(!before.isEmpty());
        QVERIFY(m_client->unloaded.isEmpty());
        QCOMPARE(m_client->dimensions, QStringList{QString::fromLatin1(QcDimensionDefinition::overworldName)});
        goElsewhere();
        // One message says the dimension; the client forgets every chunk on it (decision E-1002).
        QCOMPARE(m_client->dimensions.size(), 2);
        QCOMPARE(m_client->dimensions.last(), QString::fromLatin1(emptyName));
        QVERIFY(m_client->unloaded.isEmpty());
    }

    void theSamePositionInTwoDimensionsIsTwoPlaces()
    {
        start();
        otherToTheSamePlaceElsewhere();
        const QcBlockPos above = m_feet.offset(2, 4, 0);
        m_server->world().setBlock(above.offset(0, -1, 0), qcAirBlockId);
        m_server->world().setBlock(above.offset(0, -2, 0), qcAirBlockId);
        // Both in the same tick: each dimension's queue holds the position once.
        const QString sand = u"/setblock %1 %2 %3 qtcraft:sand"_s.arg(above.x).arg(above.y).arg(above.z);
        chat(sand);
        chatOther(sand);
        ticks(40);
        QCOMPARE(m_server->world().blockAt(above), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(m_elsewhere->world().blockAt(above), std::optional<QcBlockId>(qcAirBlockId));
    }

    void travelEndsTheScreensAPlayerHasOpen()
    {
        start();
        otherToTheSamePlaceElsewhere();
        // The same chest at the same place there: reach and block alone would keep it open.
        const QcBlockPos chest = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(chest, id("qtcraft:chest"));
        m_elsewhere->world().setBlock(chest, id("qtcraft:chest"));
        send(QcMsg::UseItem{chest, QcBlockFace::West});
        ticks(1);
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(chest));
        chat(u"/tp %1 %2 %3 in %4"_s.arg(m_feet.x + 0.5).arg(m_feet.y).arg(m_feet.z + 0.5).arg(QString::fromLatin1(emptyName)));
        ticks(3);
        QVERIFY(m_client->machineClosed);
        QVERIFY(!m_client->openMachine);
    }

    void aDeathElsewhereDropsThereAndRespawnsInTheOverworld()
    {
        start();
        m_server->setKeepInventory(false);
        chat(u"/give dirt 3"_s);
        goElsewhere();
        QCOMPARE(m_elsewhere->clients().size(), std::size_t{1});
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(m_client->dead);
        const QList<QcItemEntity> &there = m_elsewhere->entities().items().items();
        QVERIFY(std::ranges::any_of(there, [this](const QcItemEntity &drop) { return drop.stack.item == item("qtcraft:dirt"); }));
        QVERIFY(m_server->items().items().isEmpty());

        send(QcMsg::Respawn{});
        ticks(3);
        QVERIFY(!m_client->dead);
        QVERIFY(m_elsewhere->clients().empty());

        // And the way back.
        goElsewhere();
        QCOMPARE(m_elsewhere->clients().size(), std::size_t{1});
        QCOMPARE(m_client->motion.position.y, 2.0);
    }

    void travelToADimensionThrowsTheGridWhereHeStood()
    {
        start();
        fillInventoryAndGrid();
        const QcVec3 stood = m_client->motion.position;
        goElsewhere();
        checkGridIsEmptyAndInventoryFull();
        QCOMPARE(dirtNear(m_server->items().items(), stood), 64);
        QVERIFY(m_elsewhere->entities().items().items().isEmpty());
    }

    void aTeleportInsideADimensionThrowsTheGridWhereHeStood()
    {
        start();
        fillInventoryAndGrid();
        const QcVec3 stood = m_client->motion.position;
        chat(u"/tp %1 %2 %3"_s.arg(stood.x + 40.0).arg(stood.y + 2.0).arg(stood.z));
        ticks(2);
        checkGridIsEmptyAndInventoryFull();
        QCOMPARE(dirtNear(m_server->items().items(), stood), 64);
    }

    void aDeathKeepingTheInventoryThrowsTheGridWhereHeFell()
    {
        start();
        QVERIFY(m_server->keepInventory());
        const QcBlockPos fell = m_feet.offset(40, 0, 40);
        otherElsewhereAt(fell);
        chat(u"/tp %1 %2 %3 in %4"_s.arg(fell.x + 0.5).arg(fell.y).arg(fell.z + 0.5).arg(QString::fromLatin1(emptyName)));
        ticks(3);
        fillInventoryAndGrid();

        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(m_client->dead);
        send(QcMsg::Respawn{});
        ticks(3);
        QVERIFY(!m_client->dead);
        QVERIFY(m_elsewhere->clients().size() == 1);
        checkGridIsEmptyAndInventoryFull();
        QCOMPARE(dirtNear(m_elsewhere->entities().items().items(), centreOf(fell)), 64);
        QVERIFY(m_server->items().items().isEmpty());
    }

    void aSignLeftBehindTakesNoTextWhereHeArrives()
    {
        start();
        const QcBlockPos sign = m_feet.offset(1, 0, 0);
        chat(u"/give oak_sign 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:oak_sign"))});
        m_server->world().setBlock(m_feet.offset(1, -1, 0), id("qtcraft:stone"));
        send(QcMsg::UseItem{m_feet.offset(1, -1, 0), QcBlockFace::Up});
        ticks(1);
        QCOMPARE(m_client->signEditors.size(), 1);

        chat(u"/tp %1 %2 %3 in %4"_s.arg(m_feet.x + 0.5).arg(m_feet.y).arg(m_feet.z + 0.5).arg(QString::fromLatin1(emptyName)));
        ticks(3);
        m_elsewhere->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:stone"));
        m_elsewhere->world().setBlock(sign, m_server->world().blockAt(sign).value_or(qcAirBlockId));
        send(QcMsg::EditSign{sign, QcSignText::Lines{u"Hacked"_s}});
        ticks(1);
        QVERIFY(!m_elsewhere->world().signs().at(sign));
    }

    void aMapOnlyDrawsAndMarksInItsOwnDimension()
    {
        // Made on the stone platform of the test dimension, the map is one of that dimension
        // (decision E-1005) and shows the stone and its holder there.
        start();
        goElsewhere();
        chat(u"/give map 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:map"))});
        send(QcMsg::ConsumeItem{});
        const int slot = slotOf(item("qtcraft:filled_map"));
        QVERIFY(slot >= 0);
        send(QcMsg::SelectHotbarSlot{slot});
        const quint32 mapId = m_client->stacks.value(slot).mapId();
        const QcMapData *map = m_server->maps().find(mapId);
        QVERIFY(map);
        QCOMPARE(map->dimension(), QString::fromLatin1(emptyName));
        ticks(QcMapSurvey::sweep + 1);
        const std::optional<QPoint> origin = map->pixelAt(0, 0);
        QVERIFY(origin);
        QCOMPARE(QcMapPixel::blockOf(map->at(*origin)), std::optional<quint32>(m_content->state(id("qtcraft:stone")).type));
        ticks(QcServerMaps::markerInterval + 1);
        QVERIFY(!m_client->mapUpdates.isEmpty());
        QCOMPARE(m_client->mapUpdates.last().markers.size(), 1);

        // Held at the same place in the overworld it neither draws the grass nor marks him.
        chat(u"/tp 0.5 %1 0.5 in %2"_s.arg(m_feet.y).arg(QString::fromLatin1(QcDimensionDefinition::overworldName)));
        ticks(3);
        send(QcMsg::SelectHotbarSlot{slot});
        m_client->mapUpdates.clear();
        ticks(QcMapSurvey::sweep + QcServerMaps::markerInterval + 1);
        QCOMPARE(QcMapPixel::blockOf(m_server->maps().find(mapId)->at(*origin)), std::optional<quint32>(m_content->state(id("qtcraft:stone")).type));
        for (const QcMsg::MapUpdate &update : std::as_const(m_client->mapUpdates)) {
            QVERIFY(update.markers.isEmpty());
        }
    }
};

QTEST_MAIN(TestDimensionParts)
#include "test_dimension_parts.moc"
