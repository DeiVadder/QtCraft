#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/gameplay/QcEnchanting.h"
#include "common/gameplay/QcEnchantingTable.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcMining.h"
#include "common/protocol/QcProtocol.h"
#include "gameplay/QcEnchantingFixture.h"
#include "QcTestUtil.h"
#include "server/storage/QcLoadWarnings.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <QJsonObject>

using namespace Qt::StringLiterals;

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
        const QcResult<QcMessage> back = QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server);
        QVERIFY(back.has_value());
        QCOMPARE(std::get<QcMsg::EnchantingState>(*back).item, sword);
        QCOMPARE(std::get<QcMsg::EnchantingState>(*back).offers, state.offers);
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

        send(QcMsg::ChooseEnchantment{2});
        QCOMPARE(player().experience.level(), 37);
        QCOMPARE(m_client->workstations.enchantingState.lapis.count, 2);
        QVERIFY(!m_client->workstations.enchantingState.item.enchantments().isEmpty());
        QVERIFY(player().enchantSeed != seed);
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
