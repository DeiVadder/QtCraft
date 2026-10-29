// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcSoundKeys.h"
#include "common/core/QcCrc32.h"
#include "common/gameplay/QcArmour.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int head = QcMsg::MoveInventoryItem::equipmentSlot;
constexpr int chest = head + 1;
constexpr int feet = head + 3;

} // namespace

// Armour on the server (decisions E-234, E-236): putting it on in all three ways, the damage it
// takes off and the wear it suffers, breaking, repair, drops on death, the player file and what
// other players see of it.
class TestArmourServer : public QcServerTestFixture
{
    Q_OBJECT

    void giveAndHold(const char *name)
    {
        chat(u"/give "_s + QString::fromLatin1(name));
        send(QcMsg::SelectHotbarSlot{slotOf(item(name))});
    }

    // Stands on magma (1 damage every half second, armoured) for `seconds`; returns the health lost.
    double burnOnMagma(int ticks)
    {
        const double before = m_client->health;
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:magma_block"));
        idle(ticks);
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:grass_block"));
        return before - m_client->health;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void armourGoesOnByMovingOnlyIntoItsSlot()
    {
        join();
        chat(u"/give iron_helmet"_s);
        chat(u"/give iron_chestplate"_s);
        const int helmet = slotOf(item("qtcraft:iron_helmet"));
        const int plate = slotOf(item("qtcraft:iron_chestplate"));

        // A chestplate does not go on the head, a stick nowhere.
        move(plate, head);
        QVERIFY(m_client->equipment.at(QcEquipmentSlot::Head).isEmpty());
        QCOMPARE(countOf(item("qtcraft:iron_chestplate")), 1);
        chat(u"/give stick 3"_s);
        move(slotOf(item("qtcraft:stick")), chest);
        QVERIFY(m_client->equipment.isEmpty());

        move(helmet, head);
        move(plate, chest);
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Head).item, item("qtcraft:iron_helmet"));
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Chest).item, item("qtcraft:iron_chestplate"));
        QCOMPARE(countOf(item("qtcraft:iron_helmet")), 0);

        // Swapping two worn pieces would put each into the wrong slot: refused.
        move(head, chest);
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Head).item, item("qtcraft:iron_helmet"));

        // And back into the inventory.
        move(head, 20);
        QVERIFY(m_client->equipment.at(QcEquipmentSlot::Head).isEmpty());
        QCOMPARE(m_client->stacks[20].item, item("qtcraft:iron_helmet"));
    }

    void shiftClickPutsArmourOnAndTakesItOff()
    {
        join();
        chat(u"/give golden_boots"_s);
        chat(u"/give iron_boots"_s);
        send(QcMsg::QuickMoveItem{slotOf(item("qtcraft:golden_boots"))});
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Feet).item, item("qtcraft:golden_boots"));

        // The slot is taken: the second pair moves between hotbar and storage as usual.
        const int ironSlot = slotOf(item("qtcraft:iron_boots"));
        send(QcMsg::QuickMoveItem{ironSlot});
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Feet).item, item("qtcraft:golden_boots"));
        QVERIFY(slotOf(item("qtcraft:iron_boots")) >= QcInventory::hotbarSize);

        send(QcMsg::QuickMoveItem{feet});
        QVERIFY(m_client->equipment.isEmpty());
        QCOMPARE(countOf(item("qtcraft:golden_boots")), 1);
    }

    void rightClickPutsHeldArmourOnAndSwaps()
    {
        join();
        giveAndHold("qtcraft:leather_helmet");
        const int hand = m_client->stacks.indexOf(QcItemStack{item("qtcraft:leather_helmet"), 1, 0});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Head).item, item("qtcraft:leather_helmet"));
        QVERIFY(m_client->stacks[hand].isEmpty());

        // A second helmet in the hand trades places with the worn one.
        chat(u"/give diamond_helmet"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:diamond_helmet"))});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Head).item, item("qtcraft:diamond_helmet"));
        QCOMPARE(countOf(item("qtcraft:leather_helmet")), 1);
    }

    void armourTakesDamageOffAndWears()
    {
        join();
        const double bare = burnOnMagma(40);
        QVERIFY2(bare >= 3.0, qPrintable(QString::number(bare)));
        chat(u"/heal"_s);

        // Full diamond: 20 points take 80 % off; every burn wears every piece by one.
        chat(u"/equip diamond"_s);
        QCOMPARE(QcArmour::points(*m_content, m_client->equipment), 20);
        const double armoured = burnOnMagma(40);
        QVERIFY2(std::abs(armoured - bare * 0.2) < 0.01, qPrintable(u"%1 vs %2"_s.arg(armoured).arg(bare)));
        const int hits = static_cast<int>(std::lround(bare));
        for (const QcItemStack &stack : m_client->equipment.stacks) {
            QCOMPARE(stack.damage, hits);
        }

        // Falls go through armour and do not wear it.
        const QcEquipment before = m_client->equipment;
        fallDownShaft();
        QCOMPARE(m_client->health, 20.0 - armoured - 7.0);
        QCOMPARE(m_client->equipment, before);
    }

    void wornOutArmourBreaks()
    {
        join();
        chat(u"/equip leather_boots"_s);
        const int durability = m_content->item(item("qtcraft:leather_boots")).durability;
        // Every half second one burn: the boots last `durability` burns; /heal keeps the player alive.
        for (int round = 0; round < durability / 10 + 2 && !m_client->equipment.isEmpty(); ++round) {
            burnOnMagma(100);
            chat(u"/heal"_s);
        }
        QVERIFY(m_client->equipment.isEmpty());
        QVERIFY(!m_client->dead);
    }

    void wornPiecesAreRepairedInTheGrid()
    {
        join();
        // Two worn chestplates: wear them one after the other on magma.
        chat(u"/equip iron_chestplate"_s);
        burnOnMagma(40);
        chat(u"/heal"_s);
        const int firstDamage = m_client->equipment.at(QcEquipmentSlot::Chest).damage;
        QVERIFY(firstDamage > 0);
        move(chest, 10);
        chat(u"/equip iron_chestplate"_s);
        burnOnMagma(20);
        move(chest, 11);

        move(10, QcMsg::MoveInventoryItem::craftingGridSlot);
        move(11, QcMsg::MoveInventoryItem::craftingGridSlot + 1);
        const int durability = m_content->item(item("qtcraft:iron_chestplate")).durability;
        QCOMPARE(m_client->craftingResult.item, item("qtcraft:iron_chestplate"));
        const int expectedLeft = 2 * durability - firstDamage - m_client->craftingGrid[1].damage + durability / 20;
        QCOMPARE(durability - m_client->craftingResult.damage, std::min(durability, expectedLeft));

        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(item("qtcraft:iron_chestplate")), 1);
        QVERIFY(m_client->craftingGrid[0].isEmpty() && m_client->craftingGrid[1].isEmpty());
    }

    void armourIsCraftedFromIngots()
    {
        join();
        // A crafting table next to the player for the 3 x 3 recipes.
        m_server->world().setBlock(m_feet.offset(1, 0, 0), id("qtcraft:crafting_table"));
        chat(u"/give iron_ingot 24"_s);
        for (const char *piece : {"iron_helmet", "iron_chestplate", "iron_leggings", "iron_boots"}) {
            send(QcMsg::CraftRecipe{u"qtcraft:"_s + QString::fromLatin1(piece), 1});
            QCOMPARE(countOf(item(qPrintable(u"qtcraft:"_s + QString::fromLatin1(piece)))), 1);
        }
        QCOMPARE(countOf(item("qtcraft:iron_ingot")), 0);
    }

    void deathDropsArmourUnlessKept()
    {
        join();
        m_server->setKeepInventory(false);
        chat(u"/equip iron"_s);
        ticks(1);
        QVERIFY(m_client->sounds.contains(QString::fromLatin1(QcSoundKeys::armourEquip)));
        m_client->sounds.clear();
        chat(u"/kill"_s);
        ticks(1);
        QVERIFY(m_client->dead);
        // Dying takes nothing off: no equip sound.
        QVERIFY2(!m_client->sounds.contains(QString::fromLatin1(QcSoundKeys::armourEquip)), qPrintable(m_client->sounds.join(u' ')));
        QVERIFY(m_client->equipment.isEmpty());
        QCOMPARE(droppedCount(item("qtcraft:iron_chestplate")), 1);
        QCOMPARE(droppedCount(item("qtcraft:iron_boots")), 1);

        send(QcMsg::Respawn{});
        m_server->setKeepInventory(true);
        chat(u"/equip golden"_s);
        chat(u"/kill"_s);
        QCOMPARE(QcArmour::points(*m_content, m_client->equipment), 11);
        QCOMPARE(droppedCount(item("qtcraft:golden_helmet")), 0);
    }

    void equipCommandKeepsThePiecesItReplaces()
    {
        join();
        chat(u"/equip iron_chestplate"_s);
        burnOnMagma(20);
        chat(u"/heal"_s);
        const QcItemStack worn = m_client->equipment.at(QcEquipmentSlot::Chest);
        QVERIFY(worn.damage > 0);

        // The worn chestplate goes into the inventory as it was; the new set is worn.
        chat(u"/equip golden"_s);
        QCOMPARE(QcArmour::points(*m_content, m_client->equipment), 11);
        QCOMPARE(slotOf(worn.item) >= 0 ? m_client->stacks[slotOf(worn.item)] : QcItemStack(), worn);

        // Clearing puts every piece into the inventory; without room (all slots full of dirt but
        // the one of the chestplate) they are thrown.
        chat(u"/give dirt 2304"_s);
        chat(u"/equip clear"_s);
        QVERIFY(m_client->equipment.isEmpty());
        int thrown = 0;
        for (const char *piece : {"qtcraft:golden_helmet", "qtcraft:golden_chestplate", "qtcraft:golden_leggings", "qtcraft:golden_boots"}) {
            QCOMPARE(countOf(item(piece)), 0);
            thrown += droppedCount(item(piece));
        }
        QCOMPARE(thrown, 4);
    }

    void othersSeeWhatIsWorn()
    {
        join();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient other(std::move(clientEnd));
        other.hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(3);

        const auto spawnOfRaw = [&other]() -> std::optional<QcMsg::EntitySpawn> {
            for (const QcMsg::EntitySpawn &spawn : std::as_const(other.spawns)) {
                if (spawn.kind == QcEntityKind::Player && spawn.name == u"Raw"_s) {
                    return spawn;
                }
            }
            return std::nullopt;
        };
        QVERIFY(spawnOfRaw().has_value());
        QVERIFY(spawnOfRaw()->equipment.isEmpty());

        // Putting armour on sends the spawn again with it.
        chat(u"/equip iron_helmet"_s);
        ticks(2);
        QCOMPARE(spawnOfRaw()->equipment.at(QcEquipmentSlot::Head).item, item("qtcraft:iron_helmet"));
        chat(u"/equip clear"_s);
        ticks(2);
        QVERIFY(spawnOfRaw()->equipment.isEmpty());
    }

    void playerFileKeepsArmourAndOlderFilesAreRefused()
    {
        QTemporaryDir dir;
        const QcWorldStorage storage(dir.path(), m_content);
        QcPlayerData player;
        player.motion.position = {0.5, 70.0, 0.5};
        player.equipment.at(QcEquipmentSlot::Chest) = {item("qtcraft:diamond_chestplate"), 1, 17};
        player.equipment.at(QcEquipmentSlot::Feet) = {item("qtcraft:leather_boots"), 1, 0};
        QVERIFY(storage.writePlayer(u"Knight"_s, player).has_value());

        const auto read = storage.readPlayer(u"Knight"_s);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(read->player->equipment, player.equipment);
        QVERIFY(read->warnings.isEmpty());

        // A file of format 2 (before armour) is refused, not converted (decision E-356).
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << 0.5 << 70.0 << 0.5 << 0.0 << 0.0 << 0.0 << true << 90.0f << 0.0f << 12.0 << qint32{0} << static_cast<quint32>(QcInventory::slotCount);
        for (int i = 0; i < QcInventory::slotCount; ++i) {
            stream << (i == 0 ? u"qtcraft:stick"_s : QString()) << static_cast<qint32>(i == 0 ? 5 : 0) << qint32{0};
        }
        stream << qint32{15} << 1.5;
        QByteArray file("QCPL");
        QDataStream header(&file, QIODevice::Append);
        header.setVersion(QDataStream::Qt_6_8);
        header << quint16{2} << static_cast<quint32>(payload.size()) << qcCrc32(payload);
        file.append(payload);
        QVERIFY(QDir().mkpath(dir.filePath(u"players"_s)));
        QFile old(dir.filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Old"_s)));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write(file);
        old.close();

        const auto oldRead = storage.readPlayer(u"Old"_s);
        QVERIFY(!oldRead.has_value());
        QVERIFY2(oldRead.error().error.contains(u"older development version"_s), qPrintable(oldRead.error().error));
    }

    void armourOfAnotherSlotGoesIntoTheInventory()
    {
        // A file whose head slot holds a chestplate (a mod moved it): it lands in the inventory.
        QTemporaryDir dir;
        const QcWorldStorage storage(dir.path(), m_content);
        QcPlayerData player;
        player.motion.position = {0.5, 70.0, 0.5};
        player.equipment.at(QcEquipmentSlot::Head) = {item("qtcraft:iron_chestplate"), 1, 3};
        QVERIFY(storage.writePlayer(u"Odd"_s, player).has_value());

        const auto read = storage.readPlayer(u"Odd"_s);
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QVERIFY(read->player->equipment.isEmpty());
        QCOMPARE(read->player->inventory.slot(0), (QcItemStack{item("qtcraft:iron_chestplate"), 1, 3}));
    }

    void armourComesBackAfterRejoining()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/equip golden"_s);
        QVERIFY(m_server->save().has_value());
        m_client.reset();
        m_server.reset();

        join(config);
        QCOMPARE(QcArmour::points(*m_content, m_client->equipment), 11);
    }
};

QTEST_GUILESS_MAIN(TestArmourServer)
#include "test_armour_server.moc"
