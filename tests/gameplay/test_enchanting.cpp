// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientWorld.h"
#include "clientcore/session/QcEnchantingAura.h"
#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/gameplay/QcEnchanting.h"
#include "common/gameplay/QcEnchantingTable.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcMining.h"
#include "common/protocol/QcProtocol.h"
#include "common/world/QcChunk.h"
#include "gameplay/QcEnchantingFixture.h"
#include "QcTestUtil.h"
#include "server/storage/QcLoadWarnings.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <QJsonObject>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Empty chunks around the origin (x and z from -32 to 31).
QList<std::shared_ptr<const QcChunk>> emptyChunks()
{
    QList<std::shared_ptr<const QcChunk>> chunks;
    for (int cx = -2; cx <= 1; ++cx) {
        for (int cz = -2; cz <= 1; ++cz) {
            chunks.append(std::make_shared<const QcChunk>(QcChunkPos{cx, cz}));
        }
    }
    return chunks;
}

// The positions of the tables an aura shows, in its order.
QList<QcBlockPos> tablesOf(const QcEnchantingAura &aura)
{
    QList<QcBlockPos> positions;
    for (const QcEnchantingAura::Table &table : aura.tables()) {
        positions.append(table.pos);
    }
    return positions;
}

} // namespace

// The enchanting table (decision E-864): enchantments come from data and reach the client, the
// offers follow the bookshelves around the table and the player's seed as in the reference, the
// server takes levels and lapis by the offer's place, the enchantments live in the stack's data
// (wire and save), and the first effects work.
class TestEnchanting : public QcEnchantingFixture
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void enchantmentsFromDataReachTheClient()
    {
        QCOMPARE(m_content->enchantments().size(), 5);
        const QcEnchantmentDefinition *efficiency = m_content->enchantment(u"qtcraft:efficiency"_s);
        QVERIFY(efficiency);
        QCOMPARE(efficiency->effect, QcEnchantmentEffect::Efficiency);
        QCOMPARE(efficiency->maxLevel, 5);

        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> copy = QcContentSerializer::read(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        QCOMPARE(copy->enchantments(), m_content->enchantments());
        QVERIFY(copy->block(*copy->blockId(u"qtcraft:enchanting_table"_s)).opensEnchanting);

        // Unknown effects and item kinds are refused.
        QJsonObject json{{u"name"_s, u"x"_s}, {u"effect"_s, u"flight"_s}, {u"items"_s, QJsonArray{u"armour"_s}}};
        QVERIFY(!QcContentParser::parseEnchantment(json, u"qtcraft"_s).has_value());
        json[u"effect"_s] = u"unbreaking"_s;
        QVERIFY(QcContentParser::parseEnchantment(json, u"qtcraft"_s).has_value());
        json[u"items"_s] = QJsonArray{u"spoon"_s};
        QVERIFY(!QcContentParser::parseEnchantment(json, u"qtcraft"_s).has_value());
    }

    void shelvesCountWithAirBetween()
    {
        const QcBlockPos table{0, 64, 0};
        const auto everywhere = [](QcBlockPos) { return true; };
        const auto nowhere = [](QcBlockPos) { return false; };
        QCOMPARE(QcEnchantingTable::shelvesAround(table, everywhere, everywhere), QcEnchantingTable::maxShelves);
        QCOMPARE(QcEnchantingTable::shelvesAround(table, everywhere, nowhere), 0);
        // Only the ring two blocks away counts, at the table's height and one above.
        const auto ringNorth = [](QcBlockPos at) { return at.z == -2 && at.y == 64; };
        QCOMPARE(QcEnchantingTable::shelvesAround(table, ringNorth, everywhere), 5);
        const auto tooHigh = [](QcBlockPos at) { return at.y == 66; };
        QCOMPARE(QcEnchantingTable::shelvesAround(table, tooHigh, everywhere), 0);
    }

    void clientCountsTheShelvesAsTheServer()
    {
        join();
        const QcBlockPos table = openTable(15);
        // Stone between the table and three shelves of the east side hides them; tall grass between
        // the table and three shelves of the south side does not (decision E-884: one rule, in
        // QcEnchantingTable::shelvesIn, for the server's offers and the client's runes).
        putAt(table.offset(1, 0, 0), id("qtcraft:stone"));
        putAt(table.offset(0, 0, 1), id("qtcraft:tall_grass"));
        ticks(1);
        QCOMPARE(m_client->workstations.enchantingState.shelves, 12);

        QcClientWorld world(m_content);
        for (int cx = -1; cx <= 1; ++cx) {
            for (int cz = -1; cz <= 1; ++cz) {
                const QcChunkPos pos{table.chunk().x + cx, table.chunk().z + cz};
                if (const std::shared_ptr<const QcChunk> chunk = m_server->world().chunk(pos)) {
                    world.setChunk(chunk);
                }
            }
        }
        QcEnchantingAura aura;
        aura.refresh(world, {m_feet.x + 0.5, m_feet.y + 1.6, m_feet.z + 0.5}, 0);
        const auto found = std::ranges::find(aura.tables(), table, &QcEnchantingAura::Table::pos);
        QVERIFY(found != aura.tables().end());
        QCOMPARE(static_cast<int>(found->shelves.size()), m_client->workstations.enchantingState.shelves);
    }

    void runeSeedIsNotTheEnchantingSeed()
    {
        // The screen gets the player's own rune seed, whatever the enchanting seed (decision E-884).
        join();
        player().enchantSeed = 1234;
        player().runeSeed = 99;
        openTable(15);
        QCOMPARE(m_client->workstations.enchantingState.runeSeed, 99u);
        send(QcMsg::CloseEnchanting{});

        // A player of an older file has none: one is drawn, and the offers stay.
        player().runeSeed = 0;
        openTable(15);
        QVERIFY(player().runeSeed != 0);
        QCOMPARE(m_client->workstations.enchantingState.runeSeed, player().runeSeed);
        QCOMPARE(player().enchantSeed, 1234u);
    }

    void auraShowsTheNearestTables()
    {
        // Ten tables in a row east of the viewer, two blocks apart; the nearest eight are shown.
        QcClientWorld world(m_content);
        const QList<std::shared_ptr<const QcChunk>> chunks = emptyChunks();
        for (const std::shared_ptr<const QcChunk> &chunk : chunks) {
            world.setChunk(chunk);
        }
        QList<QcBlockPos> tables;
        for (int index = 0; index < 10; ++index) {
            tables.append({2 + 2 * index, 64, 0});
            QVERIFY(world.setBlock(tables.constLast(), id("qtcraft:enchanting_table")));
        }
        const QcVec3 eye{0.5, 65.6, 0.5};
        QcEnchantingAura aura;
        aura.refresh(world, eye, 100);
        QCOMPARE(tablesOf(aura), tables.first(QcEnchantingAura::maxTables));
        QVERIFY(aura.tables().constFirst().shelves.isEmpty());

        // A table taken away stays shown until rescanTicks have passed.
        QVERIFY(world.setBlock(tables[0], qcAirBlockId));
        aura.refresh(world, eye, 100 + QcEnchantingAura::rescanTicks - 1);
        QCOMPARE(tablesOf(aura).constFirst(), tables[0]);
        aura.refresh(world, eye, 100 + QcEnchantingAura::rescanTicks);
        QCOMPARE(tablesOf(aura), tables.mid(1, QcEnchantingAura::maxTables));

        // Other content (a world joined anew) looks again at once.
        QVERIFY(world.setBlock(tables[1], qcAirBlockId));
        QcClientWorld rejoined(QcTestUtil::loadContent());
        const QList<QcChunkPos> positions = world.chunkPositions();
        for (const QcChunkPos pos : positions) {
            rejoined.setChunk(world.chunk(pos));
        }
        aura.refresh(rejoined, eye, 100 + QcEnchantingAura::rescanTicks + 1);
        QCOMPARE(tablesOf(aura), tables.mid(2, QcEnchantingAura::maxTables));
    }

    void offersFollowShelvesAndSeed()
    {
        const QcItemStack pickaxe = stackOf("qtcraft:iron_pickaxe");
        for (quint32 seed = 1; seed <= 200; ++seed) {
            const QcEnchantingTable::Offers full = QcEnchantingTable::offers(*m_content, pickaxe, 15, seed);
            const QcEnchantingTable::Offers none = QcEnchantingTable::offers(*m_content, pickaxe, 0, seed);
            // The same seed gives the same offers.
            QCOMPARE(QcEnchantingTable::offers(*m_content, pickaxe, 15, seed), full);
            QCOMPARE(full[2].levelCost, 30);
            QVERIFY(none[2].levelCost <= 8);
            QVERIFY(full[0].levelCost >= 1 && full[1].levelCost >= 2);
            for (const QcEnchantingTable::Offer &offer : full) {
                QVERIFY(!offer.enchantments.isEmpty());
                for (const QcEnchantment &enchantment : offer.enchantments) {
                    const QcEnchantmentDefinition *definition = m_content->enchantment(enchantment.name);
                    QVERIFY(definition && QcEnchanting::appliesTo(*definition, m_content->item(pickaxe.item)));
                    QVERIFY(enchantment.level >= 1 && enchantment.level <= definition->maxLevel);
                }
            }
        }
        QVERIFY(QcEnchantingTable::offers(*m_content, pickaxe, 15, 1) != QcEnchantingTable::offers(*m_content, pickaxe, 15, 2));
        // Nothing for items without enchantability or with enchantments already.
        QCOMPARE(QcEnchantingTable::offers(*m_content, stackOf("qtcraft:dirt"), 15, 1)[2].levelCost, 0);
        QCOMPARE(QcEnchantingTable::offers(*m_content, stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:unbreaking"_s, 1}}), 15, 1)[2].levelCost, 0);
        // Boots may get feather falling, a sword never efficiency.
        bool featherFalling = false;
        for (quint32 seed = 1; seed <= 300; ++seed) {
            for (const QcEnchantingTable::Offer &offer : QcEnchantingTable::offers(*m_content, stackOf("qtcraft:iron_boots"), 15, seed)) {
                featherFalling = featherFalling || std::ranges::any_of(offer.enchantments, [](const QcEnchantment &e) { return e.name == u"qtcraft:feather_falling"_s; });
            }
            for (const QcEnchantingTable::Offer &offer : QcEnchantingTable::offers(*m_content, stackOf("qtcraft:iron_sword"), 15, seed)) {
                QVERIFY(std::ranges::none_of(offer.enchantments, [](const QcEnchantment &e) { return e.name == u"qtcraft:efficiency"_s; }));
            }
        }
        QVERIFY(featherFalling);
    }

    void enchantmentsTravelAndPersist()
    {
        const QcItemStack sword = stackOf("qtcraft:iron_sword", {{u"qtcraft:sharpness"_s, 3}, {u"qtcraft:unbreaking"_s, 2}});
        QCOMPARE(sword.enchantments().size(), 2);
        QVERIFY(!sword.stacksWith(stackOf("qtcraft:iron_sword")));

        QcMsg::EnchantingState state;
        state.item = sword;
        state.shelves = 12;
        state.offers[1] = {9, u"qtcraft:sharpness"_s, 2};
        state.runeSeed = 0xdeadbeef;
        const QcResult<QcMessage> back = QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server);
        QVERIFY(back.has_value());
        QCOMPARE(std::get<QcMsg::EnchantingState>(*back).item, sword);
        QCOMPARE(std::get<QcMsg::EnchantingState>(*back).offers, state.offers);
        QCOMPARE(std::get<QcMsg::EnchantingState>(*back).runeSeed, 0xdeadbeefu);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::ChooseEnchantment{1}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::ChooseEnchantment{3}), QcProtocol::Peer::Client).has_value());

        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcSaveFile::writeStack(out, sword, *m_content);
        }
        QDataStream in(bytes);
        QcLoadWarnings warnings;
        QCOMPARE(QcStackReader(*m_content, u"test"_s, warnings).read(in), sword);
    }

    void effects()
    {
        const QcContentRegistry &content = *m_content;
        const QcItemStack pickaxe = stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:efficiency"_s, 3}});
        QCOMPARE(QcEnchanting::levelOf(content, pickaxe, QcEnchantmentEffect::Efficiency), 3);
        QCOMPARE(QcEnchanting::levelOf(content, pickaxe, QcEnchantmentEffect::Unbreaking), 0);
        const QcBlockDefinition &stone = content.block(id("qtcraft:stone"));
        const QcItemDefinition &iron = content.item(pickaxe.item);
        QVERIFY(QcMining::breakSeconds(stone, &iron, QcEnchanting::speedBonus(3)) < QcMining::breakSeconds(stone, &iron) * 0.5);

        QVERIFY(QcEnchanting::keepsDurability(3, 0.7, false));
        QVERIFY(!QcEnchanting::keepsDurability(3, 0.8, false));
        QVERIFY(!QcEnchanting::keepsDurability(3, 0.31, true));
        QCOMPARE(QcEnchanting::damageBonus(5), 3.0);

        QcEquipment equipment;
        equipment.stacks[static_cast<std::size_t>(QcEquipmentSlot::Feet)] = stackOf("qtcraft:iron_boots", {{u"qtcraft:feather_falling"_s, 4}});
        equipment.stacks[static_cast<std::size_t>(QcEquipmentSlot::Chest)] = stackOf("qtcraft:iron_chestplate", {{u"qtcraft:protection"_s, 4}});
        QCOMPARE(QcEnchanting::protectionPoints(content, equipment, false), 4);
        QCOMPARE(QcEnchanting::protectionPoints(content, equipment, true), 16);
        QCOMPARE(QcEnchanting::reducedDamage(10.0, 16), 3.6);
    }

    void tableTakesLevelsAndLapis()
    {
        join();
        player().experience = QcExperience(40, 0);
        const QcBlockPos table = openTable(15);
        QCOMPARE(m_client->workstations.enchanting, std::optional(table));
        putPickaxeAndLapis(5);
        QCOMPARE(m_client->workstations.enchantingState.item.item, item("qtcraft:iron_pickaxe"));
        QCOMPARE(m_client->workstations.enchantingState.lapis.count, 5);
        QCOMPARE(m_client->workstations.enchantingState.shelves, 15);
        QCOMPARE(m_client->workstations.enchantingState.offers[2].levelCost, 30);
        const quint32 seed = player().enchantSeed;
        QVERIFY(seed != 0);
        // The rune seed is drawn on its own, not derived from the enchanting seed (decision E-884):
        // the client gets it, and it changes when the enchanting seed is drawn again.
        const quint32 runeSeed = m_client->workstations.enchantingState.runeSeed;
        QVERIFY(runeSeed != 0);
        QCOMPARE(runeSeed, player().runeSeed);

        send(QcMsg::ChooseEnchantment{2});
        QCOMPARE(player().experience.level(), 37);
        QCOMPARE(m_client->workstations.enchantingState.lapis.count, 2);
        QVERIFY(!m_client->workstations.enchantingState.item.enchantments().isEmpty());
        QVERIFY(player().enchantSeed != seed);
        QVERIFY(m_client->workstations.enchantingState.runeSeed != runeSeed);
        QCOMPARE(m_client->workstations.enchantingState.runeSeed, player().runeSeed);
        // An enchanted item gets no more offers; a second try changes nothing.
        QCOMPARE(m_client->workstations.enchantingState.offers[0].levelCost, 0);
        send(QcMsg::ChooseEnchantment{0});
        QCOMPARE(player().experience.level(), 37);

        // Closing gives item and lapis back.
        send(QcMsg::CloseEnchanting{});
        QCOMPARE(countOf(item("qtcraft:lapis_lazuli")), 2);
        const QcItemStack back = player().inventory.slot(serverSlotOf(item("qtcraft:iron_pickaxe")));
        QVERIFY(!back.enchantments().isEmpty());
    }

    void notEnoughLevelsOrLapis()
    {
        join();
        player().experience = QcExperience(5, 0);
        openTable(15);
        putPickaxeAndLapis(1);
        send(QcMsg::ChooseEnchantment{2});
        QCOMPARE(player().experience.level(), 5);
        QVERIFY(m_client->workstations.enchantingState.item.enchantments().isEmpty());
        player().experience = QcExperience(40, 0);
        // Offer 2 costs three lapis; one is not enough.
        send(QcMsg::ChooseEnchantment{2});
        QVERIFY(m_client->workstations.enchantingState.item.enchantments().isEmpty());
        // Only lapis goes into the lapis slot.
        player().inventory.add(QcItemStack{item("qtcraft:dirt"), 1, 0}, *m_content);
        move(serverSlotOf(item("qtcraft:dirt")), lapisSlot);
        QCOMPARE(m_client->workstations.enchantingState.lapis.item, item("qtcraft:lapis_lazuli"));
    }
};

QTEST_GUILESS_MAIN(TestEnchanting)
#include "test_enchanting.moc"
