#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/gameplay/QcHunger.h"
#include "server/gameplay/QcServerGameModes.h"
#include "server/network/QcServerSettings.h"
#include "server/storage/QcSaveFile.h"
#include "worlds/QcWorldLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The creative mode on the server (decisions E-282 to E-285): the mode per player and its default,
// separate inventories, the item catalogue, instant breaking, endless placing, invulnerability,
// frozen hunger, creatures that leave creative players alone, flight only in creative mode, pick
// block and what is saved.
class TestCreativeMode : public QcServerTestFixture
{
    Q_OBJECT

private:
    static QcServerConfig creative()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        return config;
    }

    static QcWorldLibrary::NewWorld creativeWorld()
    {
        QcWorldLibrary::NewWorld world;
        world.gameMode = QcGameMode::Creative;
        return world;
    }

    void flyInputs(int count, bool jump)
    {
        for (int i = 0; i < count; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            input.flying = true;
            input.jump = jump;
            m_client->connection.send(QcMsg::PlayerInput{input});
            QcTestUtil::pump();
            ticks(1);
        }
    }

    // Jumps on the spot for `count` ticks; every jump exhausts unless hunger is frozen.
    void jumpFor(int count)
    {
        for (int i = 0; i < count; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            input.jump = true;
            m_client->connection.send(QcMsg::PlayerInput{input});
            QcTestUtil::pump();
            ticks(1);
        }
    }

    static QcServerConfig savedIn(const QTemporaryDir &directory)
    {
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        return config;
    }

    // Stops the server (which saves the player) and drops the client.
    void leave()
    {
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        m_server.reset();
    }

    void mine(QcBlockPos pos)
    {
        m_client->connection.send(QcMsg::StartMining{pos});
        send(QcMsg::FinishMining{pos});
    }

private slots:
    void newPlayersStartInTheDefaultMode()
    {
        join();
        QCOMPARE(m_client->gameMode, QcGameMode::Survival);

        join(creative());
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);
    }

    void survivalAndCreativeKeepTheirOwnInventories()
    {
        join();
        chat(u"/give stone 10"_s);
        QCOMPARE(countOf(item("qtcraft:stone")), 10);

        chat(u"/gamemode creative"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);
        QCOMPARE(countOf(item("qtcraft:stone")), 0);
        send(QcMsg::CreativeItem{0, {item("qtcraft:diamond"), 5, 0}});
        QCOMPARE(countOf(item("qtcraft:diamond")), 5);

        chat(u"/gamemode survival"_s);
        QCOMPARE(countOf(item("qtcraft:stone")), 10);
        QCOMPARE(countOf(item("qtcraft:diamond")), 0);

        chat(u"/gamemode c"_s);
        QCOMPARE(countOf(item("qtcraft:diamond")), 5);
        QCOMPARE(countOf(item("qtcraft:stone")), 0);
    }

    void catalogueItemsAreRefusedInSurvival()
    {
        join();
        send(QcMsg::CreativeItem{0, {item("qtcraft:diamond"), 64, 0}});
        send(QcMsg::CreativeItem{QcMsg::CreativeItem::addSlot, {item("qtcraft:diamond"), 64, 0}});
        QCOMPARE(countOf(item("qtcraft:diamond")), 0);
    }

    void catalogueItemsFillSlotsWithinTheirLimits()
    {
        join(creative());
        const QcItemId diamond = item("qtcraft:diamond");
        send(QcMsg::CreativeItem{3, {diamond, 60, 0}});
        send(QcMsg::CreativeItem{3, {diamond, 10, 0}});
        QCOMPARE(m_client->stacks[3], (QcItemStack{diamond, 64, 0}));

        // Another item replaces the stack; an empty stack is the bin.
        send(QcMsg::CreativeItem{3, {item("qtcraft:stone"), 1, 0}});
        QCOMPARE(m_client->stacks[3], (QcItemStack{item("qtcraft:stone"), 1, 0}));
        send(QcMsg::CreativeItem{3, {}});
        QVERIFY(m_client->stacks[3].isEmpty());

        // Unknown items and counts above the stack size are cut to what the content allows.
        send(QcMsg::CreativeItem{4, {QcItemId(m_content->itemCount() + 5), 1, 0}});
        QVERIFY(m_client->stacks[4].isEmpty());
        send(QcMsg::CreativeItem{4, {item("qtcraft:diamond_helmet"), 7, 99999}});
        QCOMPARE(m_client->stacks[4].count, 1);
        QVERIFY(m_client->stacks[4].damage < m_content->item(item("qtcraft:diamond_helmet")).durability);

        send(QcMsg::CreativeItem{QcMsg::CreativeItem::addSlot, {diamond, 64, 0}});
        QCOMPARE(countOf(diamond), 64);
    }

    void armourSlotsTakeOnlyFittingArmour()
    {
        join(creative());
        const QcItemId helmet = item("qtcraft:diamond_helmet");
        send(QcMsg::CreativeItem{QcMsg::MoveInventoryItem::equipmentSlot, {helmet, 1, 0}});
        QCOMPARE(m_client->equipment.stacks[0].item, helmet);

        send(QcMsg::CreativeItem{QcMsg::MoveInventoryItem::equipmentSlot + 1, {helmet, 1, 0}});
        QVERIFY(m_client->equipment.stacks[1].isEmpty());
        send(QcMsg::CreativeItem{QcMsg::MoveInventoryItem::equipmentSlot, {}});
        QVERIFY(m_client->equipment.stacks[0].isEmpty());
    }

    void creativeBreaksAtOnceWithoutDrops()
    {
        join(creative());
        const QcBlockPos stone = m_feet.offset(1, 0, 0);
        const QcBlockPos bedrock = m_feet.offset(-1, 0, 0);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        m_server->world().setBlock(bedrock, id("qtcraft:bedrock"));

        mine(stone);
        mine(bedrock);
        QCOMPARE(m_server->world().blockAt(stone), qcAirBlockId);
        QCOMPARE(m_server->world().blockAt(bedrock), qcAirBlockId);
        ticks(5);
        QCOMPARE(m_server->items().items().size(), 0);
    }

    void weaponsDoNotWearInCreative()
    {
        join(creative());
        const QcItemId sword = item("qtcraft:diamond_sword");
        send(QcMsg::CreativeItem{0, {sword, 1, 0}});
        send(QcMsg::SelectHotbarSlot{0});

        // A pig in a pit next to the player cannot run away.
        const QcBlockPos pit = m_feet.offset(1, -1, 0);
        m_server->world().setBlock(pit, qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -1, 0), qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -2, 0), id("qtcraft:stone"));
        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), {pit.x + 0.5, double(pit.y - 1), pit.z + 0.5}, 500);
        ticks(1);
        send(QcMsg::AttackEntity{pig});
        QVERIFY(!m_server->mobs().find(pig) || m_server->mobs().find(pig)->health < 10.0);
        QCOMPARE(m_client->stacks[0], (QcItemStack{sword, 1, 0}));
    }

    void survivalCannotBreakAtOnce()
    {
        join();
        const QcBlockPos stone = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        mine(stone);
        QCOMPARE(m_server->world().blockAt(stone), id("qtcraft:stone"));
    }

    void placingKeepsTheStack()
    {
        join(creative());
        send(QcMsg::CreativeItem{0, {item("qtcraft:stone"), 1, 0}});
        send(QcMsg::SelectHotbarSlot{0});
        const QcBlockPos target = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{target.offset(0, -1, 0), QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(target), id("qtcraft:stone"));
        QCOMPARE(m_client->stacks[0], (QcItemStack{item("qtcraft:stone"), 1, 0}));
    }

    void creativePlayersTakeNoDamageButKill()
    {
        join(creative());
        fallDownShaft();
        QCOMPARE(m_client->health, 20.0);
        QVERIFY(!m_client->dead);

        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
    }

    void frozenHungerIgnoresExhaustionAndStarvation()
    {
        QcHunger hunger;
        hunger.restore(0, 0.0);
        hunger.setFrozen(true);
        hunger.exhaust(100.0);
        QCOMPARE(hunger.food(), 0);
        QCOMPARE(hunger.tick(60.0, 10.0, 20.0), 0.0);

        hunger.setFrozen(false);
        QVERIFY(hunger.tick(QcHunger::starvationInterval, 10.0, 20.0) < 0.0);
    }

    void creaturesLeaveCreativePlayersAlone()
    {
        for (const QcServerConfig &config : {QcServerConfig{}, creative()}) {
            join(config);
            chat(u"/time set night"_s);
            const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
            const QcVec3 near{m_feet.x + 3.5, double(m_feet.y), m_feet.z + 0.5};
            const quint32 mobId = m_server->mobs().spawn(zombie, near, 900'000);
            idle(10);

            const QcCreature *mob = m_server->mobs().find(mobId);
            QVERIFY(mob);
            const bool chases = mob->chasing == m_client->entityId;
            QCOMPARE(chases, !config.defaultGameMode.has_value());
        }
    }

    void serverAcceptsFlightOnlyInCreative()
    {
        join();
        const double ground = m_client->motion.position.y;
        flyInputs(20, true);
        QVERIFY(!m_client->motion.flying);
        QVERIFY(m_client->motion.position.y < ground + 1.3);

        chat(u"/gamemode creative"_s);
        flyInputs(20, true);
        QVERIFY(m_client->motion.flying);
        QVERIFY(m_client->motion.position.y > ground + 3.0);

        // Back in survival the flight ends at once and the player falls.
        chat(u"/gamemode survival"_s);
        idle(1);
        QVERIFY(!m_client->motion.flying);
    }

    void sneakingInFlightStillPlacesAgainstBlocks()
    {
        join(creative());
        const QcBlockPos table = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(table, id("qtcraft:crafting_table"));
        send(QcMsg::CreativeItem{0, {item("qtcraft:stone"), 1, 0}});
        send(QcMsg::SelectHotbarSlot{0});
        flyInputs(4, true);

        // Descending by sneaking: the click places against the table instead of opening it.
        QcPlayerInput down;
        down.sequence = ++m_sequence;
        down.flying = true;
        down.sneak = true;
        send(QcMsg::PlayerInput{down});
        QVERIFY(m_client->motion.flying);
        send(QcMsg::UseItem{table, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(table.offset(0, 1, 0)), id("qtcraft:stone"));
        QCOMPARE(m_client->openedCrafting, 0);
    }

    void pickBlockSelectsOrSwapsInSurvival()
    {
        join();
        const QcBlockPos stone = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(stone, id("qtcraft:stone"));

        // Nothing to pick: nothing changes.
        send(QcMsg::PickBlock{stone});
        QCOMPARE(m_client->selected, 0);

        chat(u"/give stone 5"_s);
        move(slotOf(item("qtcraft:stone")), 20);
        chat(u"/give dirt 3"_s);
        QCOMPARE(slotOf(item("qtcraft:dirt")), 0);

        // From the storage rows into the first empty hotbar slot, which gets selected.
        send(QcMsg::PickBlock{stone});
        QCOMPARE(m_client->stacks[1], (QcItemStack{item("qtcraft:stone"), 5, 0}));
        QVERIFY(m_client->stacks[20].isEmpty());
        QCOMPARE(m_client->selected, 1);

        // In the hotbar: only selected.
        send(QcMsg::SelectHotbarSlot{0});
        send(QcMsg::PickBlock{stone});
        QCOMPARE(m_client->selected, 1);
        QCOMPARE(countOf(item("qtcraft:stone")), 5);
    }

    void pickBlockTakesFromTheCatalogueInCreative()
    {
        join(creative());
        const QcBlockPos planks = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(planks, id("qtcraft:oak_planks"));
        send(QcMsg::CreativeItem{0, {item("qtcraft:stone"), 1, 0}});
        send(QcMsg::PickBlock{planks});
        QCOMPARE(m_client->stacks[1].item, item("qtcraft:oak_planks"));
        QCOMPARE(m_client->stacks[1].count, m_content->item(item("qtcraft:oak_planks")).maxStack);
        QCOMPARE(m_client->selected, 1);

        // Out of reach: refused.
        const QcBlockPos far = m_feet.offset(0, 30, 0);
        m_server->world().setBlock(far, id("qtcraft:glass"));
        send(QcMsg::PickBlock{far});
        QCOMPARE(countOf(item("qtcraft:glass")), 0);

        // Behind a wall: refused like mining through it.
        const QcBlockPos hidden = m_feet.offset(3, 1, 0);
        m_server->world().setBlock(hidden, id("qtcraft:glass"));
        for (int dy = -1; dy <= 3; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                m_server->world().setBlock(m_feet.offset(2, dy, dz), id("qtcraft:stone"));
            }
        }
        send(QcMsg::PickBlock{hidden});
        QCOMPARE(countOf(item("qtcraft:glass")), 0);
        QCOMPARE(m_client->selected, 1);
        // The wall itself is in sight: its stone lies in slot 0 and gets selected.
        send(QcMsg::PickBlock{m_feet.offset(2, 1, 0)});
        QCOMPARE(m_client->selected, 0);
    }

    void blocksStandForTheirItems()
    {
        const QcContentRegistry &content = *m_content;
        QCOMPARE(QcServerGameModes::itemForBlock(content, id("qtcraft:stone")), item("qtcraft:stone"));
        QVERIFY(!QcServerGameModes::itemForBlock(content, qcAirBlockId));
        // Water has no item of its own; its bucket places it.
        const std::optional<QcItemId> water = QcServerGameModes::itemForBlock(content, id("qtcraft:water"));
        QVERIFY(water);
        QCOMPARE(content.item(*water).placesBlock, u"qtcraft:water"_s);
    }

    void defaultGameModeCommandAndWorldFile()
    {
        join();
        chat(u"/defaultgamemode"_s);
        QCOMPARE(m_client->chatLines.last(), u"New players start in survival"_s);
        chat(u"/defaultgamemode creative"_s);
        QCOMPARE(m_client->chatLines.last(), u"New players start in creative"_s);

        QTemporaryDir root;
        const QcWorldLibrary library(root.path());
        const QcResult<QcWorldLibrary::Entry> entry = library.create(u"Built"_s, 7, creativeWorld());
        QVERIFY(entry);
        const QcResult<QcWorldInfo> info = QcWorldStorage(entry->directory, nullptr).readInfo();
        QVERIFY(info);
        QCOMPARE(info->defaultGameMode, QcGameMode::Creative);
    }

    void playerFilesKeepModeFlightAndBothInventories()
    {
        QTemporaryDir directory;
        const QcWorldStorage storage(directory.path(), m_content);
        QcPlayerData player;
        player.motion.position = {1.5, 80.0, -2.5};
        player.motion.flying = true;
        player.gameMode = QcGameMode::Creative;
        player.inventory.setSlot(2, {item("qtcraft:diamond"), 64, 0});
        player.keptInventory.setSlot(7, {item("qtcraft:stone"), 12, 0});
        player.keptInventory.setSelectedHotbarSlot(7);
        player.keptEquipment.stacks[0] = {item("qtcraft:diamond_helmet"), 1, 3};
        QVERIFY(storage.writePlayer(u"Ann"_s, player));

        const QcPlayerStorage::Result read = storage.readPlayer(u"Ann"_s);
        QVERIFY(read && read->player);
        const QcPlayerData &loaded = *read->player;
        QCOMPARE(loaded.gameMode, QcGameMode::Creative);
        QVERIFY(loaded.motion.flying);
        QCOMPARE(loaded.inventory.slot(2), (QcItemStack{item("qtcraft:diamond"), 64, 0}));
        QCOMPARE(loaded.keptInventory.slot(7), (QcItemStack{item("qtcraft:stone"), 12, 0}));
        QCOMPARE(loaded.keptInventory.selectedHotbarSlot(), 7);
        QCOMPARE(loaded.keptEquipment.stacks[0], (QcItemStack{item("qtcraft:diamond_helmet"), 1, 3}));
        QVERIFY(read->warnings.isEmpty());
    }

    void catalogueItemsStayOutOfTheWorld()
    {
        join(creative());
        m_server->setKeepInventory(false);
        const QcItemId diamond = item("qtcraft:diamond");
        const QcItemId helmet = item("qtcraft:diamond_helmet");
        send(QcMsg::CreativeItem{0, {diamond, 5, 0}});
        send(QcMsg::CreativeItem{QcMsg::MoveInventoryItem::equipmentSlot, {helmet, 1, 0}});

        // Dying drops nothing; the creative inventory and armour stay.
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        ticks(2);
        QCOMPARE(m_server->items().items().size(), 0);
        QCOMPARE(countOf(diamond), 5);
        QCOMPARE(m_client->equipment.stacks[0].item, helmet);

        // Respawning keeps the mode.
        send(QcMsg::Respawn{});
        QVERIFY(!m_client->dead);
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);

        // Throwing is deliberate and allowed, like putting items into a chest.
        send(QcMsg::DropItem{false});
        QCOMPARE(droppedCount(diamond), 1);
    }

    void craftingGridGoesBackIntoItsOwnInventory()
    {
        join();
        chat(u"/give stone 10"_s);
        move(slotOf(item("qtcraft:stone")), QcMsg::MoveInventoryItem::craftingGridSlot);
        QCOMPARE(countOf(item("qtcraft:stone")), 0);

        // The survival grid goes back into the survival inventory before it is put aside.
        chat(u"/gamemode creative"_s);
        QCOMPARE(countOf(item("qtcraft:stone")), 0);
        QCOMPARE(m_server->items().items().size(), 0);

        // A full creative inventory: what does not fit from the grid goes into the bin.
        const QcItemId diamond = item("qtcraft:diamond");
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            send(QcMsg::CreativeItem{slot, {diamond, 64, 0}});
        }
        move(0, QcMsg::MoveInventoryItem::craftingGridSlot);
        send(QcMsg::CreativeItem{0, {diamond, 64, 0}});
        chat(u"/gamemode survival"_s);
        QCOMPARE(countOf(item("qtcraft:stone")), 10);
        QCOMPARE(countOf(diamond), 0);
        ticks(2);
        QCOMPARE(m_server->items().items().size(), 0);

        chat(u"/gamemode creative"_s);
        QCOMPARE(countOf(diamond), QcInventory::slotCount * 64);
    }

    void creativeKeepsSurvivalValuesThroughEatingDeathAndRejoining()
    {
        QTemporaryDir directory;
        join(savedIn(directory));
        chat(u"/give stone 10"_s);
        chat(u"/gamemode creative"_s);
        send(QcMsg::CreativeItem{0, {item("qtcraft:apple"), 5, 0}});
        send(QcMsg::SelectHotbarSlot{0});
        leave();

        // Hungry, one jump away from losing the next food point.
        const QcWorldStorage storage(directory.path(), m_content);
        QcPlayerData player = *storage.readPlayer(u"Raw"_s)->player;
        player.food = 3;
        player.exhaustion = QcHunger::exhaustionPerFood - 0.01;
        QVERIFY(storage.writePlayer(u"Raw"_s, player));

        join(savedIn(directory));
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);
        QCOMPARE(m_client->food, 3);
        QCOMPARE(countOf(item("qtcraft:apple")), 5);
        QCOMPARE(countOf(item("qtcraft:stone")), 0);

        // Frozen hunger: no eating (the apple stays) and no exhaustion.
        send(QcMsg::ConsumeItem{});
        jumpFor(40);
        QCOMPARE(m_client->food, 3);
        QCOMPARE(countOf(item("qtcraft:apple")), 5);

        // Death and respawn change neither the mode nor the frozen values.
        m_server->setKeepInventory(false);
        chat(u"/kill"_s);
        send(QcMsg::Respawn{});
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);
        QCOMPARE(m_client->health, 20.0);
        QCOMPARE(m_client->food, 3);
        QCOMPARE(countOf(item("qtcraft:apple")), 5);
        jumpFor(40);
        QCOMPARE(m_client->food, 3);

        // Saved and loaded once more, then back to survival: the survival inventory returns and
        // the saved exhaustion costs a food point with the first jump.
        leave();
        join(savedIn(directory));
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);
        chat(u"/gamemode survival"_s);
        QCOMPARE(countOf(item("qtcraft:stone")), 10);
        QCOMPARE(countOf(item("qtcraft:apple")), 0);
        jumpFor(40);
        QCOMPARE(m_client->food, 2);
    }

    void formatThreePlayerFilesAreRefused()
    {
        QTemporaryDir directory;
        QVERIFY(QDir().mkpath(directory.filePath(u"players"_s)));
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(QcSaveFile::streamVersion);
        stream << 1.5 << 80.0 << -2.5 << 0.0 << 0.0 << 0.0 << true << 0.0f << 0.0f << 15.0 << qint32(2);
        stream << quint32(QcInventory::slotCount);
        for (int i = 0; i < QcInventory::slotCount; ++i) {
            QcSaveFile::writeStack(stream, i == 2 ? QcItemStack{item("qtcraft:stone"), 12, 0} : QcItemStack{}, *m_content);
        }
        stream << qint32(9) << 1.5;
        stream << quint32(QcEquipment::slotCount);
        for (std::size_t i = 0; i < QcEquipment::slotCount; ++i) {
            QcSaveFile::writeStack(stream, {}, *m_content);
        }
        const QString file = QDir(directory.filePath(u"players"_s)).filePath(QcWorldStorage::playerFileName(u"Ann"_s));
        QVERIFY(QcSaveFile::write(file, "QCPL", payload, 3));

        // Refused, not converted (decision E-356): a player of an older development version would
        // otherwise come back without the game mode he had.
        const QcPlayerStorage::Result read = QcWorldStorage(directory.path(), m_content).readPlayer(u"Ann"_s);
        QVERIFY(!read.has_value());
        QVERIFY2(read.error().error.contains(u"older development version"_s), qPrintable(read.error().error));
    }

    void defaultGameModeReachesNewPlayersAfterARestart()
    {
        QTemporaryDir directory;
        join(savedIn(directory));
        chat(u"/defaultgamemode creative"_s);
        // The one who changed it keeps her own mode.
        QCOMPARE(m_client->gameMode, QcGameMode::Survival);
        leave();

        const QcResult<QcWorldInfo> info = QcWorldStorage(directory.path(), nullptr).readInfo();
        QVERIFY(info);
        QCOMPARE(info->defaultGameMode, QcGameMode::Creative);

        // A player without a file is new to the world.
        QVERIFY(QFile::remove(QDir(directory.filePath(u"players"_s)).filePath(QcWorldStorage::playerFileName(u"Raw"_s))));
        join(savedIn(directory));
        QCOMPARE(m_client->gameMode, QcGameMode::Creative);
    }

    void unknownWorldGameModesAreKept()
    {
        QTemporaryDir directory;
        join(savedIn(directory));
        leave();

        // A newer game wrote a mode this version does not know.
        QFile file(directory.filePath(u"world.json"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        json.insert(u"defaultGameMode"_s, u"puzzle"_s);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(json).toJson());
        file.close();

        const auto savedMode = [&directory] {
            QFile saved(directory.filePath(u"world.json"_s));
            return saved.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(saved.readAll()).object().value(u"defaultGameMode"_s).toString() : QString();
        };

        // New players start in survival, the server warns, and saving keeps the name.
        QVERIFY(QFile::remove(QDir(directory.filePath(u"players"_s)).filePath(QcWorldStorage::playerFileName(u"Raw"_s))));
        join(savedIn(directory));
        QCOMPARE(m_client->gameMode, QcGameMode::Survival);
        QVERIFY(std::ranges::any_of(m_server->openWarnings(), [](const QString &warning) { return warning.contains(u"puzzle"_s); }));
        leave();
        QCOMPARE(savedMode(), u"puzzle"_s);

        // Until an operator sets a mode of this version.
        join(savedIn(directory));
        chat(u"/defaultgamemode creative"_s);
        leave();
        QCOMPARE(savedMode(), u"creative"_s);
    }

    void deadPlayersGetNothingFromTheCatalogue()
    {
        join(creative());
        const QcBlockPos planks = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(planks, id("qtcraft:oak_planks"));
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);

        send(QcMsg::CreativeItem{0, {item("qtcraft:diamond"), 5, 0}});
        send(QcMsg::CreativeItem{QcMsg::CreativeItem::addSlot, {item("qtcraft:diamond"), 5, 0}});
        send(QcMsg::PickBlock{planks});
        QCOMPARE(countOf(item("qtcraft:diamond")), 0);
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 0);
    }

    void leavingCreativeInTheAirFalls()
    {
        join(creative());
        const double ground = m_client->motion.position.y;
        flyInputs(20, true);
        QVERIFY(m_client->motion.position.y > ground + 5.0);

        chat(u"/gamemode survival"_s);
        idle(60);
        QVERIFY(!m_client->motion.flying);
        QVERIFY(m_client->motion.onGround);
        QVERIFY(std::abs(m_client->motion.position.y - ground) < 1e-6);
        // The fall counts from where the flight ended.
        QVERIFY(m_client->health < 20.0);
    }

    void serverSettingsReadTheDefaultGameMode()
    {
        QCOMPARE(QcServerSettings::fromJson({{u"defaultGameMode"_s, u"creative"_s}})->defaultGameMode, QcGameMode::Creative);
        QVERIFY(!QcServerSettings::fromJson({{u"defaultGameMode"_s, u""_s}})->defaultGameMode);
        QVERIFY(!QcServerSettings::fromJson({{u"defaultGameMode"_s, u"hardcore"_s}}));
        QVERIFY(!QcServerSettings::fromJson({{u"defaultGameMode"_s, 1}}));

        QcServerSettings settings;
        settings.defaultGameMode = QcGameMode::Creative;
        QCOMPARE(QcServerSettings::fromJson(settings.toJson())->defaultGameMode, QcGameMode::Creative);
        const QcServerConfig config = settings.serverConfig();
        QCOMPARE(config.defaultGameMode, QcGameMode::Creative);
    }
};

QTEST_GUILESS_MAIN(TestCreativeMode)
#include "test_creative_mode.moc"
