// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcAnvilSwaps.h"
#include "common/gameplay/QcAnvil.h"
#include "fixtures/QcTestUtil.h"
#include "machines/QcEnchantingFixture.h"

#include <array>

using namespace Qt::StringLiterals;

namespace {

// The four pairs of the base game: the ordinary block and its decoupled variant.
constexpr std::array<std::pair<const char *, const char *>, 4> pairs{{
    {"qtcraft:signal_piston", "qtcraft:decoupled_signal_piston"},
    {"qtcraft:sticky_signal_piston", "qtcraft:decoupled_sticky_signal_piston"},
    {"qtcraft:signal_dispenser", "qtcraft:decoupled_signal_dispenser"},
    {"qtcraft:signal_dropper", "qtcraft:decoupled_signal_dropper"},
}};

} // namespace

// The decoupled Signal blocks and the anvil swap (decision E-870): the swaps from the block data
// (both ways, warnings for unknown, self and double swaps), the cost of a swap (one Signal Dust
// chunk, a level per started 16 items, the prior-work penalty without raising the prior work, a
// rename on top), a full stack swapped both ways through the anvil's screen with the anvil wearing,
// and the pairs in circuits: power only above sets off the ordinary block after a block update and
// leaves the decoupled one still, power next to them sets off both.
class TestAnvilVariants : public QcEnchantingFixture
{
    Q_OBJECT

private:
    static constexpr int leftSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int rightSlot = QcMsg::MoveInventoryItem::machineFuelSlot;

    const QcMsg::AnvilState &anvilState() const { return m_client->workstations.anvilState; }

    QcItemStack chunks(int count) const { return {item("qtcraft:signal_dust_chunk"), count, 0}; }

    // An anvil three blocks east of the player, opened through the protocol.
    QcBlockPos openAnvil()
    {
        const QcBlockPos anvil = tablePos();
        putAt(anvil.offset(0, -1, 0), id("qtcraft:stone"));
        putAt(anvil, id("qtcraft:anvil"));
        send(QcMsg::UseItem{anvil, QcBlockFace::West});
        ticks(1);
        return anvil;
    }

    // Puts `stack` into the player's inventory and from there into the anvil's `slot`.
    void putIntoAnvil(const QcItemStack &stack, int slot)
    {
        player().inventory.setSlot(20, stack);
        move(20, slot);
    }

    // The base content plus test blocks copied from the Signal Piston with the given swaps, finished.
    std::shared_ptr<QcContentRegistry> contentWith(const QList<std::pair<QString, std::optional<QcAnvilSwap>>> &blocks) const
    {
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        for (const auto &[name, swap] : blocks) {
            QcBlockDefinition block = content->block(*content->blockId(u"qtcraft:signal_piston"_s));
            block.name = name;
            block.translationKey = name;
            block.anvilSwap = swap;
            if (!content->registerBlock(block)) {
                QcTestUtil::failCase(u"cannot register %1"_s.arg(name));
            }
        }
        return content;
    }

    const std::optional<QcAnvilSwap> &swapOf(const QcContentRegistry &content, const QString &name) const
    {
        return content.item(*content.itemId(name)).anvilSwap;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void bothBlocksOfAPairSwap()
    {
        for (const auto &[ordinary, decoupled] : pairs) {
            const QString plain = QLatin1StringView(ordinary);
            const QString variant = QLatin1StringView(decoupled);
            QCOMPARE(swapOf(*m_content, plain), std::optional(QcAnvilSwap{variant, u"qtcraft:signal_dust_chunk"_s}));
            QCOMPARE(swapOf(*m_content, variant), std::optional(QcAnvilSwap{plain, u"qtcraft:signal_dust_chunk"_s}));
            // The same block but for quasi-connectivity, in the creative catalogue's Signal category.
            const QcBlockDefinition &original = m_content->block(id(ordinary));
            const QcBlockDefinition &variantBlock = m_content->block(id(decoupled));
            QVERIFY(original.signal.quasiConnectivity);
            QVERIFY(!variantBlock.signal.quasiConnectivity);
            QCOMPARE(variantBlock.signal.kind, original.signal.kind);
            QCOMPARE(variantBlock.signal.head, original.signal.head);
            QCOMPARE(variantBlock.signal.sticky, original.signal.sticky);
            QCOMPARE(variantBlock.containerSlots, original.containerSlots);
            QVERIFY(variantBlock.faceTextures != original.faceTextures);
        }
        // A plain block has none; finishing again gives the same swaps and no warnings about them.
        QVERIFY(!swapOf(*m_content, u"qtcraft:stone"_s));
        QcContentRegistry again = *m_content;
        const QStringList warnings = again.finish();
        QVERIFY2(!warnings.join(u'\n').contains(u"anvil swap"_s), qPrintable(warnings.join(u'\n')));
        QCOMPARE(again.item(*again.itemId(u"qtcraft:signal_piston"_s)).anvilSwap, swapOf(*m_content, u"qtcraft:signal_piston"_s));
    }

    void brokenSwapsAreLeftOutWithAWarning()
    {
        const QString chunk = u"qtcraft:signal_dust_chunk"_s;
        std::shared_ptr<QcContentRegistry> content = contentWith({
            {u"test:unknown_into"_s, QcAnvilSwap{u"test:nothing"_s, chunk}},
            {u"test:unknown_with"_s, QcAnvilSwap{u"qtcraft:stone"_s, u"test:no_item"_s}},
            {u"test:itself"_s, QcAnvilSwap{u"test:itself"_s, chunk}},
            // A second swap for an item that already has one, and a pair declared on both blocks.
            {u"test:double"_s, QcAnvilSwap{u"qtcraft:signal_piston"_s, chunk}},
            {u"test:one"_s, QcAnvilSwap{u"test:two"_s, chunk}},
            {u"test:two"_s, QcAnvilSwap{u"test:one"_s, chunk}},
        });
        const QStringList warnings = content->finish();
        const QString all = warnings.join(u'\n');
        QVERIFY2(all.contains(u"block 'test:unknown_into': anvil swap into 'test:nothing': names the unknown item 'test:nothing'"_s), qPrintable(all));
        QVERIFY2(all.contains(u"block 'test:unknown_with': anvil swap into 'qtcraft:stone': names the unknown item 'test:no_item'"_s), qPrintable(all));
        QVERIFY2(all.contains(u"block 'test:itself': anvil swap into 'test:itself': turns the block into itself"_s), qPrintable(all));
        QVERIFY2(all.contains(u"block 'test:double': anvil swap into 'qtcraft:signal_piston': 'qtcraft:signal_piston' already swaps"_s), qPrintable(all));
        QVERIFY2(!all.contains(u"test:one"_s) && !all.contains(u"test:two"_s), qPrintable(all));

        for (const QString &name : {u"test:unknown_into"_s, u"test:unknown_with"_s, u"test:itself"_s, u"test:double"_s, u"qtcraft:stone"_s}) {
            QVERIFY2(!swapOf(*content, name), qPrintable(name));
        }
        QCOMPARE(swapOf(*content, u"qtcraft:signal_piston"_s)->into, u"qtcraft:decoupled_signal_piston"_s);
        QCOMPARE(swapOf(*content, u"test:one"_s), std::optional(QcAnvilSwap{u"test:two"_s, chunk}));
        QCOMPARE(swapOf(*content, u"test:two"_s), std::optional(QcAnvilSwap{u"test:one"_s, chunk}));
        // The resolver alone gives the same.
        QCOMPARE(QcAnvilSwaps::resolve(*content).byItem.value(u"test:two"_s).into, u"test:one"_s);
    }

    void blockWithoutItemHasNoSwap()
    {
        std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcBlockDefinition block = content->block(id("qtcraft:signal_piston"));
        block.name = u"test:no_item"_s;
        block.hasItem = false;
        block.anvilSwap = QcAnvilSwap{u"qtcraft:stone"_s, u"qtcraft:signal_dust_chunk"_s};
        QVERIFY(content->registerBlock(block));
        const QString all = content->finish().join(u'\n');
        QVERIFY2(all.contains(u"block 'test:no_item': anvil swap into 'qtcraft:stone': the block has no item"_s), qPrintable(all));
        QVERIFY(!swapOf(*content, u"qtcraft:stone"_s));
    }

    void swapGoneFromTheDataIsGoneFromBothItems()
    {
        // A pack that replaces the decoupled piston without its swap: finishing again takes it off
        // the ordinary piston as well.
        QcContentRegistry content = *m_content;
        QcBlockDefinition decoupled = content.block(id("qtcraft:decoupled_signal_piston"));
        decoupled.anvilSwap.reset();
        QVERIFY(content.registerBlock(decoupled));
        [[maybe_unused]] const QStringList warnings = content.finish();
        QVERIFY(!swapOf(content, u"qtcraft:signal_piston"_s));
        QVERIFY(!swapOf(content, u"qtcraft:decoupled_signal_piston"_s));
        QVERIFY(swapOf(content, u"qtcraft:signal_dropper"_s));
    }

    void swapNeedsRoomInTheOtherItemsStack()
    {
        // A block that swaps into an item stacking to 16: 16 swap, 17 do not.
        std::shared_ptr<QcContentRegistry> content = contentWith({{u"test:big"_s, QcAnvilSwap{u"test:small"_s, u"qtcraft:signal_dust_chunk"_s}}});
        QcItemDefinition small;
        small.name = u"test:small"_s;
        small.maxStack = 16;
        QVERIFY(content->registerItem(small));
        const QString all = content->finish().join(u'\n');
        QVERIFY2(!all.contains(u"test:big"_s), qPrintable(all));
        const QcItemId big = *content->itemId(u"test:big"_s);
        const QcItemStack chunk{*content->itemId(u"qtcraft:signal_dust_chunk"_s), 1, 0};
        QCOMPARE(QcAnvil::result(*content, {big, 16, 0}, chunk, QString()).output.item, *content->itemId(u"test:small"_s));
        QVERIFY(QcAnvil::result(*content, {big, 17, 0}, chunk, QString()).isEmpty());
    }

    void swapCostsALevelPerStartedSixteen_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("levels");
        QTest::newRow("one") << 1 << 1;
        QTest::newRow("sixteen") << 16 << 1;
        QTest::newRow("seventeen") << 17 << 2;
        QTest::newRow("a full stack") << 64 << 4;
    }

    void swapCostsALevelPerStartedSixteen()
    {
        QFETCH(int, count);
        QFETCH(int, levels);
        for (const auto &[ordinary, decoupled] : pairs) {
            const QcItemStack there{item(ordinary), count, 0};
            const QcAnvil::Result swapped = QcAnvil::result(*m_content, there, chunks(10), QString());
            QCOMPARE(swapped.output, (QcItemStack{item(decoupled), count, 0}));
            QCOMPARE(swapped.levelCost, levels);
            QCOMPARE(swapped.rightUsed, 1);
            // And back again, to a stack that stacks with a fresh one.
            const QcAnvil::Result back = QcAnvil::result(*m_content, swapped.output, chunks(1), QString());
            QCOMPARE(back.output, there);
            QCOMPARE(back.levelCost, levels);
        }
        QCOMPARE(QcAnvil::swapLevels(count), levels);
    }

    void swapNeedsTheChunkAndKeepsThePriorWork()
    {
        const QcItemStack pistons{item("qtcraft:signal_piston"), 64, 0};
        // Only the chunk swaps: Signal Dust, a second piston stack or nothing do not.
        QVERIFY(QcAnvil::result(*m_content, pistons, {item("qtcraft:signal_dust"), 1, 0}, QString()).isEmpty());
        QVERIFY(QcAnvil::result(*m_content, pistons, pistons, QString()).isEmpty());
        QVERIFY(QcAnvil::result(*m_content, pistons, {}, QString()).isEmpty());
        QVERIFY(QcAnvil::result(*m_content, {item("qtcraft:stone"), 64, 0}, chunks(1), QString()).isEmpty());

        // A rename on top costs one more and keeps the name on the swapped stack.
        const QcAnvil::Result named = QcAnvil::result(*m_content, pistons, chunks(1), u"Doors"_s);
        QCOMPARE(named.output.item, item("qtcraft:decoupled_signal_piston"));
        QCOMPARE(named.output.name(), u"Doors"_s);
        QCOMPARE(named.levelCost, 5);
        QCOMPARE(named.output.priorWork(), 0);

        // The prior-work penalty counts, but a swap does not raise the prior work.
        const QcAnvil::Result worked = QcAnvil::result(*m_content, pistons.withPriorWork(2), chunks(1), QString());
        QCOMPARE(worked.levelCost, 4 + 3);
        QCOMPARE(worked.output.priorWork(), 2);
        QVERIFY(QcAnvil::result(*m_content, pistons.withPriorWork(6), chunks(1), QString()).isTooExpensive());
    }

    void screenSwapsAFullStackBothWaysAndWearsTheAnvil()
    {
        join();
        player().experience = QcExperience(200, 0);
        const QcBlockPos anvil = openAnvil();
        putIntoAnvil({item("qtcraft:signal_dispenser"), 64, 0}, leftSlot);
        putIntoAnvil(chunks(64), rightSlot);
        QCOMPARE(anvilState().levelCost, 4);
        QCOMPARE(anvilState().output, (QcItemStack{item("qtcraft:decoupled_signal_dispenser"), 64, 0}));

        send(QcMsg::ForgeAnvil{4, anvilState().output});
        QCOMPARE(anvilState().left, (QcItemStack{item("qtcraft:decoupled_signal_dispenser"), 64, 0}));
        QCOMPARE(anvilState().right.count, 63);
        QCOMPARE(player().experience.level(), 196);
        send(QcMsg::ForgeAnvil{4, anvilState().output});
        QCOMPARE(anvilState().left, (QcItemStack{item("qtcraft:signal_dispenser"), 64, 0}));
        QCOMPARE(anvilState().right.count, 62);
        QCOMPARE(player().experience.level(), 192);

        // Each swap wears the anvil like any other use (12 %): within 40 swaps it no longer is a new
        // anvil, unless chance spares it (0.6 %; the server's random numbers are seeded).
        int swaps = 2;
        while (swaps < 42 && blockNameAt(anvil) == u"qtcraft:anvil"_s) {
            send(QcMsg::ForgeAnvil{4, anvilState().output});
            ++swaps;
        }
        QCOMPARE(blockNameAt(anvil), u"qtcraft:chipped_anvil"_s);
        QCOMPARE(anvilState().right.count, 64 - swaps);
        QCOMPARE(player().experience.level(), 200 - 4 * swaps);
        QCOMPARE(anvilState().left.count, 64);
    }

    void pairsInCircuits_data()
    {
        QTest::addColumn<int>("pair");
        QTest::addColumn<bool>("decoupled");
        for (int pair = 0; pair < std::ssize(pairs); ++pair) {
            const auto &[ordinary, variant] = pairs.at(static_cast<std::size_t>(pair));
            QTest::addRow("%s", ordinary) << pair << false;
            QTest::addRow("%s", variant) << pair << true;
        }
    }

    void pairsInCircuits()
    {
        QFETCH(int, pair);
        QFETCH(bool, decoupled);
        const auto &[ordinary, variant] = pairs.at(static_cast<std::size_t>(pair));
        const QString block = QString::fromLatin1(decoupled ? variant : ordinary).section(u':', 1);
        const bool piston = block.contains(u"piston"_s);
        buildArea();
        // The lever powers the cell above the block but nothing next to the block itself.
        put(5, 0, 5, block + u"[facing=north]"_s);
        if (!piston) {
            QVERIFY(ensureEntity(at(5, 0, 5)));
            m_server->blockEntities().container(at(5, 0, 5))->stacks[0] = {item("qtcraft:stick"), 4, 0};
        }
        put(7, 1, 5, u"stone"_s);
        put(6, 1, 5, u"lever[attach=east]"_s);
        ticks(2);
        flip(6, 1, 5);
        ticks(10);
        const QcItemId stick = item("qtcraft:stick");
        int dropped = 0;
        // Whether the piston is out, or the dispenser threw a stick since the last look.
        const auto acted = [&] {
            const int now = droppedCount(stick);
            const bool threw = now > dropped;
            dropped = now;
            return piston ? flag(5, 0, 5, u"extended"_s) : threw;
        };
        QVERIFY(!acted());

        // A block update next to it: the ordinary block notices the power above, the decoupled not.
        put(4, 0, 5, u"stone"_s);
        ticks(10);
        QCOMPARE(acted(), !decoupled);

        // Without power and after another update both rest; power next to them sets off both.
        flip(6, 1, 5);
        put(4, 0, 5, u"air"_s);
        ticks(10);
        QVERIFY(!acted());
        put(5, 0, 6, u"lever[attach=floor]"_s);
        flip(5, 0, 6);
        ticks(10);
        QVERIFY(acted());
    }
};

QTEST_GUILESS_MAIN(TestAnvilVariants)
#include "test_anvil_variants.moc"
