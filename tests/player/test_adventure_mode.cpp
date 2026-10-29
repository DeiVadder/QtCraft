// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerClient.h"
#include "server/gameplay/QcHunger.h"
#include "server/storage/QcWorldStorage.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The adventure mode on the server (decision E-735): no breaking and no acting on blocks except
// where the world's allow-lists permit it, everything else as in survival (using blocks, attacking,
// eating); the lists set with /adventure, sent to adventure players (not to spectators) and kept in
// world.json; the mode kept in the player file.
class TestAdventureMode : public QcServerTestFixture
{
    Q_OBJECT

private:
    static QcServerConfig adventure(QcServerConfig config = {})
    {
        config.defaultGameMode = QcGameMode::Adventure;
        return config;
    }

    static QcServerConfig savedIn(const QTemporaryDir &directory)
    {
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        return config;
    }

    // Mines the block for long enough to break it by hand or with any tool.
    void mine(QcBlockPos pos)
    {
        send(QcMsg::StartMining{pos});
        ticks(80);
        send(QcMsg::FinishMining{pos});
    }

    void hold(const char *name)
    {
        chat(u"/give %1"_s.arg(QString::fromLatin1(name)));
        send(QcMsg::SelectHotbarSlot{slotOf(item(name))});
    }

    // The last reply of the server, as its untranslated key.
    QString lastReply() const { return m_client->chatKeys.isEmpty() ? QString() : m_client->chatKeys.constLast().key; }

private slots:
    void nothingBreaksWithoutAnAllowList()
    {
        join(adventure());
        QCOMPARE(m_client->gameMode, QcGameMode::Adventure);
        const QcBlockPos dirt = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));
        mine(dirt);
        QCOMPARE(m_server->world().blockAt(dirt), id("qtcraft:dirt"));

        hold("qtcraft:iron_shovel");
        mine(dirt);
        QCOMPARE(m_server->world().blockAt(dirt), id("qtcraft:dirt"));
    }

    void theListedItemBreaksTheListedBlocks()
    {
        join(adventure());
        const QcBlockPos stone = m_feet.offset(1, 0, 0);
        const QcBlockPos dirt = m_feet.offset(-1, 0, 0);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));
        chat(u"/adventure canBreak iron_pickaxe stone"_s);

        // The empty hand is not the pickaxe.
        mine(stone);
        QCOMPARE(m_server->world().blockAt(stone), id("qtcraft:stone"));

        hold("qtcraft:iron_pickaxe");
        mine(dirt);
        QCOMPARE(m_server->world().blockAt(dirt), id("qtcraft:dirt"));
        mine(stone);
        QCOMPARE(m_server->world().blockAt(stone), qcAirBlockId);
        // Broken as in survival: the tool wears.
        QCOMPARE(m_client->stacks[m_client->selected].damage, 1);
    }

    void anyItemStandsForEveryItemAndTheHand()
    {
        join(adventure());
        const QcBlockPos dirt = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));
        chat(u"/adventure canBreak * dirt"_s);
        mine(dirt);
        QCOMPARE(m_server->world().blockAt(dirt), qcAirBlockId);
    }

    void placingNeedsTheClickedBlockOnTheList()
    {
        join(adventure());
        hold("qtcraft:stone");
        const QcBlockPos ground = m_feet.offset(2, -1, 0);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(ground.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:stone")), 1);

        chat(u"/adventure canPlaceOn stone grass_block"_s);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(ground.offset(0, 1, 0)), id("qtcraft:stone"));
        QCOMPARE(countOf(item("qtcraft:stone")), 0);
    }

    void itemsDoNotActOnUnlistedBlocks()
    {
        join(adventure());
        const QcBlockPos ground = m_feet.offset(2, -1, 0);
        hold("qtcraft:wooden_hoe");
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(ground), id("qtcraft:grass_block"));

        hold("qtcraft:water_bucket");
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(ground.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:water_bucket")), 1);

        hold("qtcraft:armor_stand");
        send(QcMsg::UseItem{ground, QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QVERIFY(m_server->stands().stands().isEmpty());

        // Allowed on grass, the hoe tills.
        chat(u"/adventure canPlaceOn wooden_hoe grass_block"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:wooden_hoe"))});
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(ground), id("qtcraft:farmland"));
    }

    void usingBlocksStaysAllowed()
    {
        join(adventure());
        const QcBlockPos table = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(table, id("qtcraft:crafting_table"));
        send(QcMsg::UseItem{table, QcBlockFace::West});
        QCOMPARE(m_client->openedCrafting, 3);
        send(QcMsg::CloseCrafting{});

        const QcBlockPos chest = m_feet.offset(-2, 0, 0);
        m_server->world().setBlock(chest, id("qtcraft:chest"));
        send(QcMsg::UseItem{chest, QcBlockFace::East});
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(chest));
        send(QcMsg::CloseMachine{});

        const QcBlockPos lever = m_feet.offset(0, 0, 2);
        const QcBlockId off = id("qtcraft:lever");
        m_server->world().setBlock(lever, off);
        send(QcMsg::UseItem{lever, QcBlockFace::Up});
        QVERIFY(m_server->world().blockAt(lever) != std::optional<QcBlockId>(off));
    }

    void attackingAndEatingStayAllowed()
    {
        join(adventure());
        const QcBlockPos pit = m_feet.offset(1, -1, 0);
        m_server->world().setBlock(pit, qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -1, 0), qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -2, 0), id("qtcraft:stone"));
        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), {pit.x + 0.5, static_cast<double>(pit.y - 1), pit.z + 0.5}, 500);
        ticks(1);
        send(QcMsg::AttackEntity{pig});
        QVERIFY(!m_server->mobs().find(pig) || m_server->mobs().find(pig)->health < 10.0);

        // Food is eaten as in survival, also a carrot aimed at grass it may not be planted on.
        m_server->entities().clients().front()->hunger.restore(10, 0.0);
        hold("qtcraft:carrot");
        send(QcMsg::UseItem{m_feet.offset(2, -1, 0), QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(m_feet.offset(2, 0, 0)), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:carrot")), 0);
        QVERIFY(m_server->entities().clients().front()->hunger.food() > 10);
    }

    void armourStandsWithstandAdventurePlayers()
    {
        join();
        hold("qtcraft:armor_stand");
        send(QcMsg::UseItem{m_feet.offset(0, -1, -2), QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QCOMPARE(m_server->stands().stands().size(), 1);
        const quint32 stand = m_server->stands().stands().constLast().id;

        chat(u"/gamemode adventure"_s);
        for (int i = 0; i < 3; ++i) {
            send(QcMsg::AttackEntity{stand});
            idle(12);
        }
        QVERIFY(m_server->stands().find(stand));
    }

    void othersStillChangeBlocks()
    {
        join(adventure());
        // Commands, explosions, pistons, fluids and creatures are no player actions.
        const QcBlockPos target = m_feet.offset(2, 0, 0);
        chat(u"/setblock %1 %2 %3 stone"_s.arg(target.x).arg(target.y).arg(target.z));
        QCOMPARE(m_server->world().blockAt(target), id("qtcraft:stone"));
    }

    void allowListsReachAdventurePlayersOnly()
    {
        join();
        chat(u"/adventure canBreak * dirt glass"_s);
        QVERIFY(m_client->adventure.isEmpty());

        chat(u"/gamemode a"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Adventure);
        QCOMPARE(m_client->adventure.blocks(QcAdventureRules::Action::Break, u"*"_s), (QStringList{u"qtcraft:dirt"_s, u"qtcraft:glass"_s}));

        // A change reaches him at once.
        chat(u"/adventure remove canBreak * glass"_s);
        QCOMPARE(m_client->adventure.blocks(QcAdventureRules::Action::Break, u"*"_s), QStringList{u"qtcraft:dirt"_s});
        chat(u"/adventure clear"_s);
        QVERIFY(m_client->adventure.isEmpty());
    }

    void aSpectatorInAnAdventureWorldGetsNoListsAndTouchesNothing()
    {
        // Spectator wins over the allow-lists (decisions E-730, E-735): he breaks nothing they
        // name, gets no lists and no changes to them; back in adventure he gets the current ones.
        join(adventure());
        const QcBlockPos dirt = m_feet.offset(1, 0, 0);
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));
        chat(u"/adventure canBreak * dirt"_s);
        QCOMPARE(m_client->adventure.blocks(QcAdventureRules::Action::Break, u"*"_s), QStringList{u"qtcraft:dirt"_s});

        chat(u"/gamemode sp"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Spectator);
        QVERIFY(m_client->adventure.isEmpty());
        mine(dirt);
        QCOMPARE(m_server->world().blockAt(dirt), id("qtcraft:dirt"));
        chat(u"/adventure canBreak * glass"_s);
        QVERIFY(m_client->adventure.isEmpty());

        chat(u"/gamemode adventure"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Adventure);
        QCOMPARE(m_client->adventure.blocks(QcAdventureRules::Action::Break, u"*"_s), (QStringList{u"qtcraft:dirt"_s, u"qtcraft:glass"_s}));
        mine(dirt);
        QCOMPARE(m_server->world().blockAt(dirt), qcAirBlockId);
    }

    void adventureCommandChecksItsWords()
    {
        join();
        chat(u"/adventure"_s);
        QCOMPARE(lastReply(), u"No adventure allow-lists: players in adventure mode can neither break nor place blocks"_s);
        chat(u"/adventure canBreak no_such_item stone"_s);
        QCOMPARE(lastReply(), u"Unknown item: %1"_s);
        chat(u"/adventure canBreak * no_such_block"_s);
        QCOMPARE(lastReply(), u"Unknown block: %1"_s);
        chat(u"/adventure canBreak *"_s);
        QVERIFY(lastReply().startsWith(u"Usage: /adventure"_s));
        chat(u"/adventure remove canBreak stone"_s);
        QCOMPARE(lastReply(), u"Nothing to remove for %1"_s);
        chat(u"/adventure"_s);
        QCOMPARE(lastReply(), u"No adventure allow-lists: players in adventure mode can neither break nor place blocks"_s);

        chat(u"/adventure canplaceon stone dirt"_s);
        QCOMPARE(lastReply(), u"%1 may be placed on: %2"_s);
        chat(u"/adventure"_s);
        QCOMPARE(lastReply(), u"%1 may be placed on: %2"_s);
        QCOMPARE(m_client->chatKeys.constLast().arguments, (QStringList{u"qtcraft:stone"_s, u"qtcraft:dirt"_s}));
    }

    void onlyOperatorsChangeTheModeAndTheLists()
    {
        join(notOperator(adventure()));
        chat(u"/gamemode survival"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Adventure);
        chat(u"/adventure canBreak * dirt"_s);
        QVERIFY(m_client->adventure.isEmpty());

        // Everyone may ask for his own mode.
        chat(u"/gamemode"_s);
        QCOMPARE(lastReply(), u"Game mode of %1: adventure"_s);
    }

    void staleNamesCanBeRemoved()
    {
        QTemporaryDir directory;
        join(savedIn(directory));
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        m_server.reset();

        // A mod that is gone left its block in the lists.
        QFile file(directory.filePath(u"world.json"_s));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        json.insert(u"adventure"_s, QJsonObject{{u"canBreak"_s, QJsonObject{{u"*"_s, QJsonArray{u"gone:block"_s, u"qtcraft:dirt"_s}}}}});
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(json).toJson());
        file.close();

        join(savedIn(directory));
        chat(u"/adventure canBreak * gone:block"_s);
        QCOMPARE(lastReply(), u"Unknown block: %1"_s);
        chat(u"/adventure remove canBreak * gone:block"_s);
        QCOMPARE(lastReply(), u"Removed from the adventure allow-lists of %1"_s);
        chat(u"/adventure"_s);
        QCOMPARE(m_client->chatKeys.constLast().arguments, (QStringList{u"*"_s, u"qtcraft:dirt"_s}));
    }

    void modeAndListsAreKept()
    {
        QTemporaryDir directory;
        join(savedIn(directory));
        chat(u"/gamemode adventure"_s);
        chat(u"/adventure canBreak iron_pickaxe stone"_s);
        chat(u"/defaultgamemode adventure"_s);
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        m_server.reset();

        const QcResult<QcWorldInfo> info = QcWorldStorage(directory.path(), nullptr).readInfo();
        QVERIFY(info);
        QCOMPARE(info->defaultGameMode, QcGameMode::Adventure);
        QCOMPARE(info->adventure.blocks(QcAdventureRules::Action::Break, u"qtcraft:iron_pickaxe"_s), QStringList{u"qtcraft:stone"_s});

        join(savedIn(directory));
        QCOMPARE(m_client->gameMode, QcGameMode::Adventure);
        QCOMPARE(m_client->adventure.items(QcAdventureRules::Action::Break), QStringList{u"qtcraft:iron_pickaxe"_s});
    }
};

QTEST_MAIN(TestAdventureMode)
#include "test_adventure_mode.moc"
