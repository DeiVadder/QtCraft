// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcAnvil.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcTestUtil.h"
#include "machines/QcEnchantingFixture.h"
#include "server/items/QcItemEntityManager.h"
#include "server/mobs/QcMobManager.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcEntityRecord.h"
#include "server/storage/QcLoadWarnings.h"

using namespace Qt::StringLiterals;

// The anvil's rules (decision E-866): repair with material or a second item, merging enchantments,
// renaming and cleaning names, the prior-work penalty and the limit, and its messages on the wire.
// The screen, falling anvils and name tags: test_anvil_screen.
class TestAnvil : public QcEnchantingFixture
{
    Q_OBJECT

private:
    QcItemStack worn(const char *name, int damage) const
    {
        QcItemStack stack = stackOf(name);
        stack.damage = damage;
        return stack;
    }

private slots:
    void renameCostsOneLevelAndKeepsTheCount()
    {
        const QcItemStack bricks{item("qtcraft:stone"), 12, 0};
        const QcAnvil::Result renamed = QcAnvil::result(*m_content, bricks, {}, u"  Corner stones  "_s);
        QCOMPARE(renamed.output.name(), u"Corner stones"_s);
        QCOMPARE(renamed.output.count, 12);
        QCOMPARE(renamed.levelCost, 1);
        QCOMPARE(renamed.output.priorWork(), 0);
        // The same name, or blank text on a stack without one, changes nothing.
        QVERIFY(QcAnvil::result(*m_content, renamed.output, {}, u"Corner stones"_s).isEmpty());
        QVERIFY(QcAnvil::result(*m_content, bricks, {}, u"   "_s).isEmpty());
        // Blank text takes a custom name off.
        QCOMPARE(QcAnvil::result(*m_content, renamed.output, {}, QString()).output.name(), QString());
        // Names are cut to the limit.
        QCOMPARE(QcAnvil::result(*m_content, bricks, {}, QString(80, u'x')).output.name().size(), QcItemData::maxNameLength);
    }

    void repairWithMaterial()
    {
        // An iron pickaxe (250 uses) worn by 200: each ingot takes 62 off, three would be needed.
        const QcItemStack pickaxe = worn("qtcraft:iron_pickaxe", 200);
        const QcAnvil::Result two = QcAnvil::result(*m_content, pickaxe, {item("qtcraft:iron_ingot"), 2, 0}, QString());
        QCOMPARE(two.output.damage, 76);
        QCOMPARE(two.rightUsed, 2);
        QCOMPARE(two.levelCost, 2);
        QCOMPARE(two.output.priorWork(), 1);
        const QcAnvil::Result many = QcAnvil::result(*m_content, pickaxe, {item("qtcraft:iron_ingot"), 10, 0}, QString());
        QCOMPARE(many.output.damage, 0);
        QCOMPARE(many.rightUsed, 4);
        // Planks repair wooden tools through their tag; the wrong material does nothing.
        QVERIFY(!QcAnvil::result(*m_content, worn("qtcraft:wooden_sword", 30), {item("qtcraft:oak_planks"), 1, 0}, QString()).isEmpty());
        QVERIFY(QcAnvil::result(*m_content, pickaxe, {item("qtcraft:gold_ingot"), 1, 0}, QString()).isEmpty());
        // An undamaged tool is not repaired.
        QVERIFY(QcAnvil::result(*m_content, stackOf("qtcraft:iron_pickaxe"), {item("qtcraft:iron_ingot"), 1, 0}, QString()).isEmpty());
    }

    void combineMergesEnchantments()
    {
        const QcItemStack left = worn("qtcraft:iron_pickaxe", 200).withEnchantments({{u"qtcraft:efficiency"_s, 2}});
        const QcItemStack right = worn("qtcraft:iron_pickaxe", 100).withEnchantments(
            {{u"qtcraft:efficiency"_s, 2}, {u"qtcraft:unbreaking"_s, 1}, {u"qtcraft:sharpness"_s, 3}});
        const QcAnvil::Result combined = QcAnvil::result(*m_content, left, right, QString());
        // 50 + 150 + 30 (12 %) uses left of 250.
        QCOMPARE(combined.output.damage, 20);
        const QList<QcEnchantment> expected{{u"qtcraft:efficiency"_s, 3}, {u"qtcraft:unbreaking"_s, 1}};
        QCOMPARE(combined.output.enchantments(), expected);
        // Repair 2, efficiency III at 1 per level, unbreaking I at 2 (weight 5), sharpness does not fit (1).
        QCOMPARE(combined.levelCost, 2 + 3 + 2 + 1);
        QCOMPARE(combined.rightUsed, 1);

        // Different levels keep the higher; the highest level never rises.
        const QcItemStack top = stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:efficiency"_s, 5}});
        const QcAnvil::Result capped = QcAnvil::result(*m_content, stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:efficiency"_s, 3}}), top, QString());
        QCOMPARE(capped.output.enchantments().constFirst().level, 5);
        // Two at the highest level change nothing, so there is nothing to forge.
        QVERIFY(QcAnvil::result(*m_content, top, top, QString()).isEmpty());
        // Another kind of item does not combine.
        QVERIFY(QcAnvil::result(*m_content, left, stackOf("qtcraft:iron_axe"), QString()).isEmpty());
    }

    void priorWorkRaisesTheCost()
    {
        QCOMPARE(QcAnvil::penaltyOf(0), 0);
        QCOMPARE(QcAnvil::penaltyOf(3), 7);
        const QcItemStack pickaxe = worn("qtcraft:iron_pickaxe", 200).withPriorWork(3);
        const QcAnvil::Result result = QcAnvil::result(*m_content, pickaxe, {item("qtcraft:iron_ingot"), 1, 0}, QString());
        QCOMPARE(result.levelCost, 1 + 7);
        QCOMPARE(result.output.priorWork(), 4);
        // From 40 levels on the anvil refuses, except a rename alone, which stays at 39.
        const QcItemStack old = pickaxe.withPriorWork(6);
        QVERIFY(QcAnvil::result(*m_content, old, {item("qtcraft:iron_ingot"), 1, 0}, QString()).isTooExpensive());
        const QcAnvil::Result renamed = QcAnvil::result(*m_content, old, {}, u"Old friend"_s);
        QCOMPARE(renamed.levelCost, QcAnvil::tooExpensive - 1);
        QCOMPARE(renamed.output.priorWork(), 6);
    }

    void priorWorkAndAnvilMessagesOnTheWire()
    {
        QcMsg::AnvilState state;
        state.left = worn("qtcraft:iron_pickaxe", 10).withPriorWork(2).withName(u"Pick"_s);
        state.right = {item("qtcraft:iron_ingot"), 3, 0};
        state.name = u"Pick"_s;
        state.output = state.left.withPriorWork(3);
        state.levelCost = 4;
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server);
        QVERIFY(decoded.has_value());
        const auto *back = std::get_if<QcMsg::AnvilState>(&*decoded);
        QVERIFY(back);
        QCOMPARE(back->left, state.left);
        QCOMPARE(back->output.priorWork(), 3);
        QCOMPARE(back->levelCost, 4);
        // A client never sends a state; the server never sends a rename.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::RenameAnvil{u"x"_s}), QcProtocol::Peer::Server).has_value());
        // A forge order carries the output the client saw.
        const QcResult<QcMessage> forge = QcProtocol::decode(QcProtocol::encode(QcMsg::ForgeAnvil{4, state.output}), QcProtocol::Peer::Client);
        QVERIFY(forge.has_value());
        QCOMPARE(std::get<QcMsg::ForgeAnvil>(*forge).output, state.output);
    }

    void namesAreCleaned()
    {
        const QcItemStack bricks{item("qtcraft:stone"), 1, 0};
        const auto named = [&](const QString &text) { return QcAnvil::result(*m_content, bricks, {}, text).output.name(); };
        // Control and format characters and line breaks go; spaces around go.
        QCOMPARE(named(u"  Co\u0007rner\nstone\u200B "_s), u"Cornerstone"_s);
        QCOMPARE(QcAnvil::nameFrom(u"\t\r\n"_s), QString());
        // A surrogate pair at the cut is kept whole or dropped whole, never cut in half.
        const QString emoji = u"\U0001F600"_s;
        const QString cut = QcAnvil::nameFrom(QString(QcItemData::maxNameLength - 1, u'x') + emoji + u"tail"_s);
        QVERIFY(cut.size() <= QcItemData::maxNameLength);
        QVERIFY(!cut.back().isHighSurrogate());
        QCOMPARE(cut, QString(QcItemData::maxNameLength - 1, u'x'));
        const QString emojis = QcAnvil::nameFrom(emoji.repeated(QcItemData::maxNameLength));
        QCOMPARE(emojis, emoji.repeated(QcItemData::maxNameLength / 2));
    }

    void unfittingEnchantmentsCostOneAndUseNothingAlone()
    {
        // Sharpness does not fit a pickaxe (the content has no conflicting pair; both take the same
        // branch of QcAnvil): it costs one and stays off.
        const QcItemStack left = worn("qtcraft:iron_pickaxe", 100).withEnchantments({{u"qtcraft:efficiency"_s, 1}});
        const QcItemStack right = worn("qtcraft:iron_pickaxe", 100).withEnchantments({{u"qtcraft:sharpness"_s, 1}});
        const QcAnvil::Result repaired = QcAnvil::result(*m_content, left, right, QString());
        QCOMPARE(repaired.output.enchantments(), left.enchantments());
        QCOMPARE(repaired.levelCost, 2 + 1);

        // Undamaged, only the unfitting enchantment: nothing to forge, not even with a rename, so the
        // right pickaxe is not used up.
        const QcItemStack whole = stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:efficiency"_s, 1}});
        QVERIFY(QcAnvil::result(*m_content, whole, stackOf("qtcraft:iron_pickaxe", {{u"qtcraft:sharpness"_s, 1}}), u"Edge"_s).isEmpty());
    }

    void tooExpensiveFromFortyAndRenameCappedAtThirtyNine()
    {
        // Prior work 5 and 3 (31 + 7) plus combining two worn pickaxes (2): exactly 40; with 5 and 2
        // (31 + 3) it is 36.
        const QcItemStack left = worn("qtcraft:iron_pickaxe", 100).withPriorWork(5);
        const QcAnvil::Result forty = QcAnvil::result(*m_content, left, worn("qtcraft:iron_pickaxe", 100).withPriorWork(3), QString());
        QCOMPARE(forty.levelCost, QcAnvil::tooExpensive);
        QVERIFY(forty.isTooExpensive());
        const QcAnvil::Result below = QcAnvil::result(*m_content, left, worn("qtcraft:iron_pickaxe", 100).withPriorWork(2), QString());
        QCOMPARE(below.levelCost, 2 + 31 + 3);
        QVERIFY(!below.isTooExpensive());
        // A rename alone with any prior work stays at 39.
        QCOMPARE(QcAnvil::result(*m_content, left.withPriorWork(20), {}, u"Old"_s).levelCost, QcAnvil::tooExpensive - 1);
    }

    void priorWorkAndNamesSurviveASave()
    {
        // A dropped stack with prior work and a named creature, through the entity codec.
        QcEntityRecord record;
        record.chunk = {0, 0};
        QcMobManager::Stored sheep;
        sheep.type = *m_content->mobIndex(u"qtcraft:sheep"_s);
        sheep.position = {8.5, 65.0, 8.5};
        sheep.health = 8.0;
        sheep.uid = 5;
        sheep.name = u"Dolly"_s;
        record.creatures = {{sheep, 0}};
        const QcItemStack pickaxe = worn("qtcraft:iron_pickaxe", 30).withPriorWork(3).withName(u"Digger"_s);
        record.items = {{QcItemEntityManager::Drop{pickaxe, {1.5, 64.0, 2.5}, {}, 0.0, 12.5}, 0}};

        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> back = QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), {0, 0}, *m_content, warnings);
        QVERIFY(back.has_value());
        QCOMPARE(back->creatures.size(), 1);
        QCOMPARE(back->creatures.first().entry.name, u"Dolly"_s);
        QCOMPARE(back->items.size(), 1);
        QCOMPARE(back->items.first().entry.stack.priorWork(), 3);
        QCOMPARE(back->items.first().entry.stack, pickaxe);
    }

    void stacksWithOtherPriorWorkDoNotStack()
    {
        const QcItemStack plain{item("qtcraft:stone"), 1, 0};
        QVERIFY(plain.stacksWith(plain));
        QVERIFY(!plain.stacksWith(plain.withPriorWork(1)));
        QVERIFY(plain.withPriorWork(2).stacksWith(plain.withPriorWork(2)));
    }

};

QTEST_GUILESS_MAIN(TestAnvil)
#include "test_anvil.moc"
