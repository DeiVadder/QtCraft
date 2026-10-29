// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/gameplay/QcAnvil.h"
#include "common/gameplay/QcEnchanting.h"
#include "common/gameplay/QcEnchantingTable.h"
#include "gameplay/QcEnchantingFixture.h"
#include "QcTestUtil.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

// Enchanted books (decision E-873): the enchanting table makes one of a single book (a book takes
// every enchantment), its enchantments do nothing while it is held, and at the anvil a book gives
// them to the left item at half the price per level (at least 1), without repairing; two books merge
// into one of the next level up to the highest; the limit of 40 levels and the prior work hold.
class TestEnchantedBooks : public QcEnchantingFixture
{
    Q_OBJECT

private:
    QcItemStack book(QList<QcEnchantment> enchantments) const { return stackOf("qtcraft:enchanted_book", std::move(enchantments)); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void theTableMakesAnEnchantedBook()
    {
        // A single book gets offers, a stack of books none (one item at a time).
        const QcItemStack plain{item("qtcraft:book"), 1, 0};
        const QcEnchantingTable::Offers offers = QcEnchantingTable::offers(*m_content, plain, 15, 4711);
        QVERIFY(std::ranges::all_of(offers, [](const QcEnchantingTable::Offer &offer) { return offer.levelCost > 0; }));
        const QcEnchantingTable::Offers none = QcEnchantingTable::offers(*m_content, plain.withCount(2), 15, 4711);
        QVERIFY(std::ranges::all_of(none, [](const QcEnchantingTable::Offer &offer) { return offer.levelCost == 0; }));
        QCOMPARE(QcEnchantingTable::enchantedItem(*m_content, plain.item), item("qtcraft:enchanted_book"));
        QCOMPARE(QcEnchantingTable::enchantedItem(*m_content, item("qtcraft:iron_pickaxe")), item("qtcraft:iron_pickaxe"));

        // Every enchantment fits a book: over many seeds, armour and weapon enchantments both come.
        QSet<QString> seen;
        for (quint32 seed = 0; seed < 200; ++seed) {
            const QList<QcEnchantment> drawn = QcEnchantingTable::enchantmentsFor(*m_content, plain, 30, seed);
            for (const QcEnchantment &enchantment : drawn) {
                seen.insert(enchantment.name);
            }
        }
        QVERIFY(seen.contains(u"qtcraft:sharpness"_s));
        QVERIFY(seen.contains(u"qtcraft:protection"_s));
        QVERIFY(seen.contains(u"qtcraft:efficiency"_s));

        // Through the protocol: the book in the slot becomes an enchanted book.
        join();
        player().experience = QcExperience(40, 0);
        openTable(15);
        hold(u"book"_s);
        move(slotOf(item("qtcraft:book")), itemSlot);
        player().inventory.add(QcItemStack{item("qtcraft:lapis_lazuli"), 3, 0}, *m_content);
        move(serverSlotOf(item("qtcraft:lapis_lazuli")), lapisSlot);
        QVERIFY(m_client->workstations.enchantingState.offers[2].levelCost > 0);
        send(QcMsg::ChooseEnchantment{2});
        const QcItemStack &made = m_client->workstations.enchantingState.item;
        QCOMPARE(made.item, item("qtcraft:enchanted_book"));
        QVERIFY(!made.enchantments().isEmpty());
        QCOMPARE(made.count, 1);
    }

    void anUnknownEnchantedFormIsReported()
    {
        QcContentRegistry content;
        QcItemDefinition scroll;
        scroll.name = u"test:scroll"_s;
        scroll.enchantability = 1;
        scroll.enchantsInto = u"test:missing"_s;
        QVERIFY(content.registerItem(scroll).has_value());
        const QStringList warnings = content.finish();
        QVERIFY2(warnings.contains(u"item 'test:scroll': 'enchantsInto' names no item 'test:missing'; it enchants as itself"_s), qPrintable(warnings.join(u'\n')));
        QCOMPARE(QcEnchantingTable::enchantedItem(content, *content.itemId(u"test:scroll"_s)), *content.itemId(u"test:scroll"_s));
    }

    void aHeldBookDoesNothing()
    {
        const QcItemStack sharp = book({{u"qtcraft:sharpness"_s, 5}});
        QCOMPARE(QcEnchanting::levelOf(*m_content, sharp, QcEnchantmentEffect::Sharpness), 0);
        const QcItemStack sword = stackOf("qtcraft:iron_sword", {{u"qtcraft:sharpness"_s, 5}});
        QCOMPARE(QcEnchanting::levelOf(*m_content, sword, QcEnchantmentEffect::Sharpness), 5);

        // The client knows it too (its mining prediction): the flag travels with the content.
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> copy = QcContentSerializer::read(in);
        QVERIFY(copy.has_value());
        const QcItemStack efficient = book({{u"qtcraft:efficiency"_s, 5}});
        QVERIFY(copy->item(efficient.item).storesEnchantments);
        QVERIFY(!copy->item(item("qtcraft:book")).storesEnchantments);
        QCOMPARE(QcEnchanting::levelOf(*copy, efficient, QcEnchantmentEffect::Efficiency), 0);
    }

    void aBookEnchantsTheLeftItemAtHalfThePrice()
    {
        // Unbreaking (weight 5) costs 2 per level from an item, 1 from a book.
        const QcItemStack pickaxe = stackOf("qtcraft:iron_pickaxe");
        const QcAnvil::Result fromBook = QcAnvil::result(*m_content, pickaxe, book({{u"qtcraft:unbreaking"_s, 3}}), QString());
        QCOMPARE(fromBook.output.enchantments(), (QList<QcEnchantment>{{u"qtcraft:unbreaking"_s, 3}}));
        QCOMPARE(fromBook.levelCost, 3);
        QCOMPARE(fromBook.rightUsed, 1);
        QCOMPARE(fromBook.output.item, pickaxe.item);
        QCOMPARE(fromBook.output.priorWork(), 1);
        const QcAnvil::Result fromItem = QcAnvil::result(*m_content, pickaxe, stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:unbreaking"_s, 3}}), QString());
        QCOMPARE(fromItem.levelCost, 6);

        // Weight 10 costs 1 either way (never less than 1); an unfitting one costs 1 and stays off.
        const QcAnvil::Result mixed = QcAnvil::result(*m_content, pickaxe, book({{u"qtcraft:efficiency"_s, 2}, {u"qtcraft:sharpness"_s, 4}}), QString());
        QCOMPARE(mixed.output.enchantments(), (QList<QcEnchantment>{{u"qtcraft:efficiency"_s, 2}}));
        QCOMPARE(mixed.levelCost, 2 + 1);
        // A book whose enchantments all do not fit gives nothing (it is not used up).
        QVERIFY(QcAnvil::result(*m_content, pickaxe, book({{u"qtcraft:sharpness"_s, 4}}), QString()).isEmpty());

        // A book does not repair; renaming in the same step costs one more.
        const QcItemStack worn{item("qtcraft:iron_pickaxe"), 1, 200};
        const QcAnvil::Result named = QcAnvil::result(*m_content, worn, book({{u"qtcraft:efficiency"_s, 1}}), u"Digger"_s);
        QCOMPARE(named.output.damage, 200);
        QCOMPARE(named.output.name(), u"Digger"_s);
        QCOMPARE(named.levelCost, 1 + 1);
    }

    void conflictingAndRareEnchantmentsFromABook()
    {
        // Smite conflicts with sharpness; two rare enchantments (weight 2 and weight 1).
        QcContentRegistry content = *m_content;
        const auto add = [&content](const char *json) {
            QcResult<QcEnchantmentDefinition> parsed = QcContentParser::parseEnchantment(QJsonDocument::fromJson(json).object(), u"qtcraft"_s);
            if (!parsed) {
                return false;
            }
            content.registerEnchantment(*std::move(parsed));
            return true;
        };
        QVERIFY(add(R"({"name":"smite","effect":"sharpness","maxLevel":5,"weight":5,"items":["weapon"],"conflicts":["sharpness"]})"));
        QVERIFY(add(R"({"name":"looting","effect":"sharpness","maxLevel":3,"weight":2,"items":["weapon"]})"));
        QVERIFY(add(R"({"name":"rare_edge","effect":"sharpness","maxLevel":3,"weight":1,"items":["weapon"]})"));

        // A conflicting enchantment costs 1 and stays off; alone it changes nothing: no result.
        const QcItemStack sharp = stackOf("qtcraft:iron_sword", {{u"qtcraft:sharpness"_s, 1}});
        QVERIFY(QcAnvil::result(content, sharp, book({{u"qtcraft:smite"_s, 2}}), QString()).isEmpty());
        const QcAnvil::Result mixed = QcAnvil::result(content, sharp, book({{u"qtcraft:smite"_s, 2}, {u"qtcraft:unbreaking"_s, 1}}), QString());
        QCOMPARE(mixed.output.enchantments(), (QList<QcEnchantment>{{u"qtcraft:sharpness"_s, 1}, {u"qtcraft:unbreaking"_s, 1}}));
        QCOMPARE(mixed.levelCost, 1 + 1);

        // Rare ones at half the price per level: weight 2 costs 4 from an item, 2 from a book;
        // weight 1 costs 8 from an item, 4 from a book.
        const QcItemStack sword = stackOf("qtcraft:iron_sword");
        QCOMPARE(QcAnvil::result(content, sword, book({{u"qtcraft:looting"_s, 1}}), QString()).levelCost, 2);
        QCOMPARE(QcAnvil::result(content, sword, stackOf("qtcraft:iron_sword", {{u"qtcraft:looting"_s, 1}}), QString()).levelCost, 4);
        QCOMPARE(QcAnvil::result(content, sword, book({{u"qtcraft:rare_edge"_s, 1}}), QString()).levelCost, 4);
        QCOMPARE(QcAnvil::result(content, sword, stackOf("qtcraft:iron_sword", {{u"qtcraft:rare_edge"_s, 1}}), QString()).levelCost, 8);
    }

    void twoBooksMergeUpToTheHighestLevel()
    {
        const QcAnvil::Result merged = QcAnvil::result(*m_content, book({{u"qtcraft:sharpness"_s, 1}}), book({{u"qtcraft:sharpness"_s, 1}}), QString());
        QCOMPARE(merged.output.item, item("qtcraft:enchanted_book"));
        QCOMPARE(merged.output.enchantments(), (QList<QcEnchantment>{{u"qtcraft:sharpness"_s, 2}}));
        QCOMPARE(merged.levelCost, 2);
        // Different enchantments go together on a book.
        const QcAnvil::Result both = QcAnvil::result(*m_content, book({{u"qtcraft:sharpness"_s, 1}}), book({{u"qtcraft:protection"_s, 2}}), QString());
        QCOMPARE(both.output.enchantments(), (QList<QcEnchantment>{{u"qtcraft:sharpness"_s, 1}, {u"qtcraft:protection"_s, 2}}));
        // At the highest level two books change nothing.
        QVERIFY(QcAnvil::result(*m_content, book({{u"qtcraft:sharpness"_s, 5}}), book({{u"qtcraft:sharpness"_s, 5}}), QString()).isEmpty());
        // A plain book in the right slot is no enchanted book: nothing.
        QVERIFY(QcAnvil::result(*m_content, stackOf("qtcraft:iron_sword"), QcItemStack{item("qtcraft:book"), 1, 0}, QString()).isEmpty());
    }

    void priorWorkMakesBooksTooExpensive()
    {
        // Prior work 5 (31 levels) and 3 (7 levels) plus sharpness III (3): 41, too expensive.
        const QcItemStack sword = stackOf("qtcraft:iron_sword").withPriorWork(5);
        const QcAnvil::Result result = QcAnvil::result(*m_content, sword, book({{u"qtcraft:sharpness"_s, 3}}).withPriorWork(3), QString());
        QCOMPARE(result.levelCost, 41);
        QVERIFY(result.isTooExpensive());
        const QcAnvil::Result cheaper = QcAnvil::result(*m_content, sword, book({{u"qtcraft:sharpness"_s, 3}}).withPriorWork(2), QString());
        QCOMPARE(cheaper.levelCost, 31 + 3 + 3);
        QVERIFY(!cheaper.isTooExpensive());
    }

    void forgingAtTheAnvilPaysAndUsesTheBook()
    {
        join();
        player().experience = QcExperience(10, 0);
        const QcBlockPos anvil = tablePos();
        putAt(anvil.offset(0, -1, 0), id("qtcraft:stone"));
        putAt(anvil, id("qtcraft:anvil"));
        send(QcMsg::UseItem{anvil, QcBlockFace::West});
        ticks(1);
        player().inventory.setSlot(20, stackOf("qtcraft:iron_sword"));
        move(20, itemSlot);
        player().inventory.setSlot(21, book({{u"qtcraft:sharpness"_s, 2}}));
        move(21, lapisSlot);
        QCOMPARE(m_client->workstations.anvilState.levelCost, 2);
        send(QcMsg::ForgeAnvil{2, m_client->workstations.anvilState.output});
        QCOMPARE(player().experience.level(), 8);
        QCOMPARE(m_client->workstations.anvilState.left.enchantments(), (QList<QcEnchantment>{{u"qtcraft:sharpness"_s, 2}}));
        QVERIFY(m_client->workstations.anvilState.right.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestEnchantedBooks)
#include "test_enchanted_books.moc"
