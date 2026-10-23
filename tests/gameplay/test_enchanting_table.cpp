#include "common/content/parser/QcContentParser.h"
#include "common/gameplay/QcArmour.h"
#include "common/gameplay/QcEnchanting.h"
#include "common/gameplay/QcMining.h"
#include "gameplay/QcEnchantingFixture.h"
#include "QcTestUtil.h"
#include "server/storage/QcPlayerRecords.h"
#include "server/storage/QcPlayerStorage.h"
#include "server/storage/QcSaveFile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// The enchanting table's screen around the edges (decision E-864): the offers the client saw are
// the ones it gets, the table's slots come back on death, disconnect, mode switch, save and stop,
// a choice where the screen may not stay open does nothing, creative players enchant for free,
// old player files load, and the effects agree between client and server.
class TestEnchantingTable : public QcEnchantingFixture
{
    Q_OBJECT

private:
    // The pickaxe in the server's inventory (empty when there is none).
    QcItemStack pickaxeInInventory()
    {
        const int slot = serverSlotOf(item("qtcraft:iron_pickaxe"));
        return slot < 0 ? QcItemStack() : player().inventory.slot(slot);
    }

    // Removes the bookshelves on the table's ring at its height plus `dy`.
    void removeShelves(QcBlockPos table, int dy)
    {
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) == 2 || std::abs(dz) == 2) {
                    m_server->world().setBlock(table.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    // The health one burn on magma takes (armour and protection apply).
    double oneBurn()
    {
        const double before = m_client->health;
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:magma_block"));
        for (int tick = 0; tick < 40 && m_client->health == before; ++tick) {
            idle(1);
        }
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:grass_block"));
        return before - m_client->health;
    }

    // Whether mining `pos` with the held tool for `count` ticks breaks it on the server.
    bool breaksAfter(QcBlockPos pos, int count)
    {
        send(QcMsg::StartMining{pos});
        ticks(count);
        send(QcMsg::FinishMining{pos});
        return *m_server->world().blockAt(pos) == qcAirBlockId;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void longNamesAreRefused()
    {
        // The name with its namespace must fit item data (QcItemData::maxEnchantmentName).
        const QString name = QString(QcItemData::maxEnchantmentName - 8, u'x');
        QJsonObject json{{u"name"_s, name}, {u"effect"_s, u"unbreaking"_s}, {u"items"_s, QJsonArray{u"durable"_s}}};
        QVERIFY(QcContentParser::parseEnchantment(json, u"qtcraft"_s).has_value());
        json[u"name"_s] = QString(name + u"x"_s);
        const QcResult<QcEnchantmentDefinition> refused = QcContentParser::parseEnchantment(json, u"qtcraft"_s);
        QVERIFY(!refused.has_value());
        QVERIFY2(qcErrorText(refused).contains("longer than"), qcErrorText(refused).constData());
    }

    void offersChangedByShelvesAreSentAgain()
    {
        join();
        player().experience = QcExperience(40, 0);
        const QcBlockPos table = openTable(15);
        putPickaxeAndLapis(5);
        QCOMPARE(m_client->workstations.enchantingState.shelves, 15);

        // Another player breaks a shelf: the client hears the new offers within a tick.
        m_server->world().setBlock(table.offset(2, 0, 0), qcAirBlockId);
        ticks(1);
        QCOMPARE(m_client->workstations.enchantingState.shelves, 14);

        // A choice made before the client heard of a change takes nothing and sends the new offers.
        removeShelves(table, 0);
        send(QcMsg::ChooseEnchantment{2});
        QCOMPARE(player().experience.level(), 40);
        QCOMPARE(m_client->workstations.enchantingState.lapis.count, 5);
        QVERIFY(m_client->workstations.enchantingState.item.enchantments().isEmpty());
        QCOMPARE(m_client->workstations.enchantingState.shelves, 0);

        // Choosing again takes the offer shown, at its price.
        const QcMsg::EnchantingOffer shown = m_client->workstations.enchantingState.offers[2];
        QVERIFY(shown.levelCost > 0 && shown.levelCost <= 8);
        send(QcMsg::ChooseEnchantment{2});
        QCOMPARE(player().experience.level(), 37);
        const QList<QcEnchantment> got = m_client->workstations.enchantingState.item.enchantments();
        QVERIFY(!got.isEmpty());
        QVERIFY(std::ranges::any_of(got, [&shown](const QcEnchantment &e) { return e.name == shown.clue && e.level == shown.clueLevel; }));
    }

    void deathDropsTheSlots()
    {
        QcServerConfig config;
        config.keepInventory = false;
        join(config);
        openTable(0);
        putPickaxeAndLapis(4);
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(!m_client->workstations.enchanting.has_value());
        QVERIFY(!player().screen.enchanting.has_value());
        QCOMPARE(droppedCount(item("qtcraft:iron_pickaxe")), 1);
        QCOMPARE(droppedCount(item("qtcraft:lapis_lazuli")), 4);
    }

    void disconnectSaveAndStopKeepTheSlots()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        openTable(0);
        putPickaxeAndLapis(4);
        // A save while the screen is open stores the slots in the inventory.
        const QcPlayerData saved = QcPlayerRecords::of(player(), *m_content).player;
        QCOMPARE(saved.inventory.countOf(item("qtcraft:iron_pickaxe")), 1);
        QCOMPARE(saved.inventory.countOf(item("qtcraft:lapis_lazuli")), 4);

        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);
        rejoin();
        QCOMPARE(countOf(item("qtcraft:iron_pickaxe")), 1);
        QCOMPARE(countOf(item("qtcraft:lapis_lazuli")), 4);

        // The same through a server stop.
        send(QcMsg::UseItem{tablePos(), QcBlockFace::West});
        move(slotOf(item("qtcraft:iron_pickaxe")), itemSlot);
        move(slotOf(item("qtcraft:lapis_lazuli")), lapisSlot);
        QCOMPARE(countOf(item("qtcraft:lapis_lazuli")), 0);
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        rejoin();
        QCOMPARE(countOf(item("qtcraft:iron_pickaxe")), 1);
        QCOMPARE(countOf(item("qtcraft:lapis_lazuli")), 4);
    }

    void switchingModeReturnsTheSlots()
    {
        join();
        openTable(0);
        putPickaxeAndLapis(4);
        chat(u"/gamemode creative"_s);
        ticks(1);
        QVERIFY(!player().screen.enchanting || player().screen.enchanting->item.isEmpty());
        chat(u"/gamemode survival"_s);
        ticks(1);
        QCOMPARE(countOf(item("qtcraft:iron_pickaxe")), 1);
        QCOMPARE(countOf(item("qtcraft:lapis_lazuli")), 4);
        QCOMPARE(droppedCount(item("qtcraft:lapis_lazuli")), 0);
    }

    void choicesWhereTheScreenMayNotStayAreRefused()
    {
        join();
        player().experience = QcExperience(40, 0);
        const QcBlockPos table = openTable(15);
        putPickaxeAndLapis(5);
        // Out of reach: the choice closes the screen and takes nothing.
        player().motion.position = player().motion.position + QcVec3{0.0, 0.0, 20.0};
        send(QcMsg::ChooseEnchantment{0});
        QVERIFY(!player().screen.enchanting.has_value());
        QCOMPARE(player().experience.level(), 40);
        QVERIFY(pickaxeInInventory().enchantments().isEmpty());
        QCOMPARE(countOf(item("qtcraft:lapis_lazuli")), 5);

        // The table broken.
        player().motion.position = player().motion.position - QcVec3{0.0, 0.0, 20.0};
        ticks(1);
        openTable(15);
        move(slotOf(item("qtcraft:iron_pickaxe")), itemSlot);
        move(slotOf(item("qtcraft:lapis_lazuli")), lapisSlot);
        m_server->world().setBlock(table, qcAirBlockId);
        send(QcMsg::ChooseEnchantment{0});
        QVERIFY(!player().screen.enchanting.has_value());
        QCOMPARE(player().experience.level(), 40);
        QVERIFY(pickaxeInInventory().enchantments().isEmpty());

        // A spectator.
        openTable(15);
        move(slotOf(item("qtcraft:iron_pickaxe")), itemSlot);
        move(slotOf(item("qtcraft:lapis_lazuli")), lapisSlot);
        chat(u"/gamemode spectator"_s);
        send(QcMsg::ChooseEnchantment{0});
        chat(u"/gamemode survival"_s);
        ticks(1);
        QCOMPARE(player().experience.level(), 40);
        QVERIFY(pickaxeInInventory().enchantments().isEmpty());
        QCOMPARE(countOf(item("qtcraft:lapis_lazuli")), 5);
    }

    void creativeEnchantsForFree()
    {
        join();
        chat(u"/gamemode creative"_s);
        ticks(1);
        QCOMPARE(player().experience.level(), 0);
        openTable(15);
        putPickaxeAndLapis(1);
        send(QcMsg::ChooseEnchantment{2});
        QVERIFY(!m_client->workstations.enchantingState.item.enchantments().isEmpty());
        QCOMPARE(m_client->workstations.enchantingState.lapis.count, 1);
        QCOMPARE(player().experience.level(), 0);
    }

    void playerFilesWithoutSeedLoad()
    {
        QTemporaryDir dir;
        const QcPlayerStorage storage(dir.path(), m_content);
        QcPlayerData player;
        player.motion.position = {0.5, 70.0, 0.5};
        player.experience = QcExperience(7, 0);
        player.enchantSeed = 1234;
        const QcResult<QcSaveFile::Payload> current = QcSaveFile::parse(storage.bytes(player), u"p"_s, "QCPL", 7);
        QVERIFY(current.has_value());
        QCOMPARE(current->version, 7);

        // Version 6 is the same without the seed at the end.
        const QByteArray old = QcSaveFile::bytes("QCPL", current->bytes.chopped(int(sizeof(quint32))), 6);
        const QcPlayerStorage::Result read = storage.parse(u"Old"_s, old);
        QVERIFY(read.has_value() && read->player.has_value());
        QCOMPARE(read->player->enchantSeed, 0u);
        QCOMPARE(read->player->experience.level(), 7);
        const QcPlayerStorage::Result again = storage.parse(u"New"_s, storage.bytes(player));
        QVERIFY(again.has_value() && again->player.has_value());
        QCOMPARE(again->player->enchantSeed, 1234u);
    }

    void efficiencyBreaksAsTheClientPredicts()
    {
        join();
        hold(u"iron_pickaxe"_s);
        const QcBlockPos pos = m_feet.offset(1, 0, 0);
        const QcBlockDefinition &obsidian = m_content->block(id("qtcraft:obsidian"));
        const QcItemDefinition &pickaxe = m_content->item(item("qtcraft:iron_pickaxe"));
        // The client's prediction (QcHandActions) with efficiency V.
        const QcItemStack enchanted = stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:efficiency"_s, 5}});
        const int level = QcEnchanting::levelOf(*m_content, enchanted, QcEnchantmentEffect::Efficiency);
        const int predicted = int(std::ceil(QcMining::breakSeconds(obsidian, &pickaxe, QcEnchanting::speedBonus(level)) * QcPhysics::tickRate));
        const int plain = int(std::ceil(QcMining::breakSeconds(obsidian, &pickaxe) * QcPhysics::tickRate));
        QVERIFY2(plain > predicted + 10, qPrintable(u"%1 vs %2"_s.arg(plain).arg(predicted)));

        // Without the enchantment the predicted time is far too short.
        putAt(pos, id("qtcraft:obsidian"));
        QVERIFY(!breaksAfter(pos, predicted));
        // With it the server agrees.
        player().inventory.setSlot(player().inventory.selectedHotbarSlot(), enchanted);
        putAt(pos, id("qtcraft:obsidian"));
        QVERIFY(breaksAfter(pos, predicted));
    }

    void protectionCountsBeforeTheArmourBreaks()
    {
        join();
        const QcItemId chestplate = item("qtcraft:iron_chestplate");
        const int durability = m_content->item(chestplate).durability;
        const QList<QcEnchantment> protection{{u"qtcraft:protection"_s, 4}};
        player().equipment.stacks[std::size_t(QcEquipmentSlot::Chest)] = QcItemStack{chestplate, 1, durability - 1}.withEnchantments(protection);
        const int points = QcArmour::points(*m_content, player().equipment);
        const double expected = QcEnchanting::reducedDamage(QcArmour::reducedDamage(1.0, points), 4);

        // The burn wears the chestplate out, and its protection still counts for that burn.
        const double lost = oneBurn();
        QVERIFY(player().equipment.at(QcEquipmentSlot::Chest).isEmpty());
        QVERIFY2(std::abs(lost - expected) < 0.01, qPrintable(u"%1 vs %2"_s.arg(lost).arg(expected)));
    }
};

QTEST_GUILESS_MAIN(TestEnchantingTable)
#include "test_enchanting_table.moc"
