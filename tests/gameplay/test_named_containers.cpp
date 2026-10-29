// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientMachineScreen.h"
#include "QcPlayerUseFixture.h"
#include "QcTestUtil.h"
#include "server/storage/QcBlockEntityCodec.h"
#include "server/storage/QcLoadWarnings.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Chests, barrels, furnaces and hoppers placed from an item named at the anvil (decision E-873): the
// name is the title of their screen, stays with the block entity (while the chunk is loaded, unloaded
// and saved) and goes back onto the item when the block is broken or blown up, with or without
// contents; a backpack keeps its name with its contents. Showcase chests and barrels take a name from
// their structure.
class TestNamedContainers : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    // Places `block` named `name` from the hand on the stage two blocks east of the player.
    QcBlockPos placeNamed(const QString &block, const QString &name, int dx = 2)
    {
        hold(block);
        const int slot = player().inventory.selectedHotbarSlot();
        player().inventory.setSlot(slot, player().inventory.slot(slot).withName(name));
        const QcBlockPos pos = m_feet.offset(dx, 0, 0);
        send(QcMsg::UseItem{pos.offset(0, -1, 0), QcBlockFace::Up});
        ticks(1);
        return pos;
    }

    // The names of the dropped (and queued) items of `name`.
    QStringList droppedNames(const char *name) const
    {
        const QcItemId wanted = item(name);
        QStringList names;
        for (const QcItemEntity &dropped : m_server->items().items()) {
            if (dropped.stack.item == wanted) {
                names.append(dropped.stack.name());
            }
        }
        for (const auto &drop : m_server->items().queued()) {
            if (drop.stack.item == wanted) {
                names.append(drop.stack.name());
            }
        }
        return names;
    }

    // Opens the screen of the block at `pos` and returns its title as the client got it.
    QString titleOf(QcBlockPos pos)
    {
        send(QcMsg::CloseMachine{});
        send(QcMsg::UseItem{pos, QcBlockFace::West});
        ticks(1);
        return m_client->openMachine == pos ? m_client->machineName : u"(closed)"_s;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void theNameIsTheScreensTitle_data()
    {
        QTest::addColumn<QString>("block");
        QTest::addColumn<bool>("machine");
        QTest::newRow("chest") << u"chest"_s << false;
        QTest::newRow("barrel") << u"barrel"_s << false;
        QTest::newRow("hopper") << u"signal_hopper"_s << false;
        QTest::newRow("furnace") << u"furnace"_s << true;
        QTest::newRow("smoker") << u"smoker"_s << true;
        QTest::newRow("blast furnace") << u"blast_furnace"_s << true;
    }

    void theNameIsTheScreensTitle()
    {
        QFETCH(QString, block);
        QFETCH(bool, machine);
        join();
        const QcBlockPos named = placeNamed(block, u"Ores"_s);
        QCOMPARE(m_server->blockEntities().nameAt(named), u"Ores"_s);
        QCOMPARE(titleOf(named), u"Ores"_s);
        QCOMPARE(m_client->machineCategory.isEmpty(), !machine);
        // The client's screen keeps it as the title (QcPlayerController::nameOpenMachine shows it).
        QcClientMachineScreen screen;
        QCOMPARE(screen.handle(QcMessage(QcMsg::OpenMachine{.pos = named, .category = m_client->machineCategory, .name = u"Ores"_s}), m_content.get()),
                 QcClientMachineScreen::Change::Opened);
        QCOMPARE(screen.name(), u"Ores"_s);

        // One placed from an unnamed item has no name: the client shows the block's own.
        hold(block);
        const QcBlockPos plain = m_feet.offset(-2, 0, 0);
        send(QcMsg::UseItem{plain.offset(0, -1, 0), QcBlockFace::Up});
        ticks(1);
        QCOMPARE(titleOf(plain), QString());
    }

    void breakingDropsTheNamedItem()
    {
        join();
        const QcBlockPos chest = placeNamed(u"chest"_s, u"Ores"_s);
        m_server->blockEntities().container(chest)->stacks[0] = {item("qtcraft:cobblestone"), 5, 0};
        const QcBlockPos furnace = placeNamed(u"furnace"_s, u"Kitchen"_s, -2);
        hold(u"iron_pickaxe"_s);
        mine(chest);
        mine(furnace);
        ticks(2);
        QCOMPARE(droppedNames("qtcraft:chest"), QStringList{u"Ores"_s});
        QCOMPARE(droppedNames("qtcraft:furnace"), QStringList{u"Kitchen"_s});
        QCOMPARE(droppedCount(item("qtcraft:cobblestone")), 5);

        // Placed again, the dropped chest takes its name back.
        for (const QcItemEntity &dropped : m_server->items().items()) {
            if (dropped.stack.item == item("qtcraft:chest")) {
                player().inventory.setSlot(player().inventory.selectedHotbarSlot(), dropped.stack);
            }
        }
        send(QcMsg::UseItem{chest.offset(0, -1, 0), QcBlockFace::Up});
        ticks(1);
        QCOMPARE(m_server->blockEntities().nameAt(chest), u"Ores"_s);
    }

    void aBlastDropsTheNamedItem()
    {
        join();
        // A blast drops a quarter of the blocks it breaks, by dice of the cell (decision E-602): of
        // a ring of named barrels some drop, every one with its name.
        QList<QcBlockPos> barrels;
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                if (std::abs(dx) == 2 || std::abs(dz) == 2) {
                    barrels.append(m_feet.offset(dx, 0, dz));
                }
            }
        }
        for (const QcBlockPos &pos : std::as_const(barrels)) {
            putAt(pos, id("qtcraft:barrel"));
            QVERIFY(m_server->blockEntities().setName(pos, u"Seeds"_s));
        }
        chat(u"/gamemode creative"_s);
        m_server->systems().explosions.explode({QcVec3{m_feet.x + 0.5, m_feet.y + 0.5, m_feet.z + 0.5}, 6.0, true, false});
        ticks(5);
        QVERIFY(std::ranges::none_of(barrels, [this](QcBlockPos pos) { return m_server->world().blockAt(pos) == id("qtcraft:barrel"); }));
        const QStringList names = droppedNames("qtcraft:barrel");
        QVERIFY(!names.isEmpty());
        QCOMPARE(names, QStringList(names.size(), u"Seeds"_s));
    }

    void aBackpackKeepsItsNameWithItsContents()
    {
        join();
        const QcBlockPos pack = placeNamed(u"backpack"_s, u"Trip"_s);
        m_server->blockEntities().container(pack)->stacks[0] = {item("qtcraft:apple"), 3, 0};
        send(QcMsg::SelectHotbarSlot{8});
        mine(pack);
        ticks(2);
        const QcItemId backpack = item("qtcraft:backpack");
        const auto dropped = std::ranges::find(m_server->items().items(), backpack, [](const QcItemEntity &entity) { return entity.stack.item; });
        QVERIFY(dropped != m_server->items().items().end());
        QCOMPARE(dropped->stack.name(), u"Trip"_s);
        QCOMPARE(dropped->stack.contents().value(0), (QcItemStack{item("qtcraft:apple"), 3, 0}));
    }

    void namesSurviveUnloadingAndARestart()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        // Empty and cold: only the name keeps them (they would be forgotten and not saved without it).
        const QcBlockPos chest = placeNamed(u"chest"_s, u"Ores"_s);
        const QcBlockPos furnace = placeNamed(u"furnace"_s, u"Kitchen"_s, -2);
        m_server->blockEntities().chunkLeft(chest.chunk());
        m_server->blockEntities().chunkLeft(furnace.chunk());
        QCOMPARE(m_server->blockEntities().nameAt(chest), u"Ores"_s);
        QCOMPARE(m_server->blockEntities().nameAt(furnace), u"Kitchen"_s);

        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        loadAround(m_feet);
        QCOMPARE(m_server->blockEntities().nameAt(chest), u"Ores"_s);
        QCOMPARE(m_server->blockEntities().nameAt(furnace), u"Kitchen"_s);
    }

    void theCodecWritesNamesAndReadsOlderEntries()
    {
        join();
        QcBlockEntityRecord record;
        record.chunk = m_feet.chunk();
        QcBlockEntities::Container chest{m_feet, QList<QcItemStack>(27), 0, u"Ores"_s};
        record.containers.append(chest);
        QcBlockEntities::Machine furnace;
        furnace.pos = m_feet.offset(1, 0, 0);
        furnace.name = u"Kitchen"_s;
        record.machines.append(furnace);

        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(record, *m_content), record.chunk, *m_content, warnings);
        QVERIFY(read.has_value());
        QCOMPARE(read->containers.value(0).name, u"Ores"_s);
        QCOMPARE(read->machines.value(0).name, u"Kitchen"_s);
        QVERIFY(read->kept.isEmpty());

        // A name with control characters or beyond the limit is cleaned like one written at the anvil.
        record.containers[0].name = u"  Ores\u0007 and more ores, all the ores of the world, every one of them  "_s;
        const QcResult<QcBlockEntityRecord> cleaned = QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(record, *m_content), record.chunk, *m_content, warnings);
        QVERIFY(cleaned.has_value());
        QVERIFY(cleaned->containers.value(0).name.startsWith(u"Ores and more"_s));
        QVERIFY(cleaned->containers.value(0).name.size() <= QcItemData::maxNameLength);
    }

    void theShowcaseNamesItsChests()
    {
        const QcStructureDefinition *station = m_content->worldgen().structure(u"qtcraft:anvil"_s);
        QVERIFY(station);
        const auto books = std::ranges::find(station->contents, u"Enchanted Books"_s, &QcStructureDefinition::Contents::name);
        QVERIFY(books != station->contents.end());
        QCOMPARE(books->items.value(0).item, u"qtcraft:enchanted_book"_s);
        QCOMPARE(books->items.value(0).enchantments, (QList<QcEnchantment>{{u"qtcraft:sharpness"_s, 1}}));
        const auto barrel = std::ranges::find(station->contents, u"Named Barrel"_s, &QcStructureDefinition::Contents::name);
        QVERIFY(barrel != station->contents.end());
        QCOMPARE(barrel->items.value(0).name, u"Seeds"_s);
    }
};

QTEST_GUILESS_MAIN(TestNamedContainers)
#include "test_named_containers.moc"
