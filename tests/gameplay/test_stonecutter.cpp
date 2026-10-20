#include "clientcore/session/QcClientStonecutter.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/content/registry/QcStonecutting.h"
#include "common/protocol/QcProtocol.h"
#include "QcPlayerUseFixture.h"

using namespace Qt::StringLiterals;

// The stonecutter (decisions E-784, E-785): its recipes come from data and from the building parts
// of stone materials at the reference's ratios, its screen opens through the protocol, and a cut
// takes one block from the inventory and puts its results there, all or nothing.
class TestStonecutter : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    // What the stonecutter makes from `input`: result name and count per recipe.
    QHash<QString, int> cutsOf(const QString &input) const
    {
        QHash<QString, int> cuts;
        const QList<qsizetype> recipes = QcStonecutting::recipesFor(*m_content, input);
        for (const qsizetype index : recipes) {
            const QcSmeltingRecipe &recipe = m_content->stonecuttingRecipes()[index];
            cuts.insert(recipe.resultItem, recipe.resultCount);
        }
        return cuts;
    }

    // A stonecutter next to the player, opened through the protocol.
    QcBlockPos openStonecutter()
    {
        const QcBlockPos cutter = m_feet.offset(2, 0, 0);
        putAt(cutter, id("qtcraft:stonecutter"));
        send(QcMsg::UseItem{cutter, QcBlockFace::West});
        ticks(1);
        return cutter;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void recipesAtTheReferenceRatios()
    {
        const QHash<QString, int> stone = cutsOf(u"qtcraft:stone"_s);
        QCOMPARE(stone.value(u"qtcraft:stone_slab"_s), 2);
        QCOMPARE(stone.value(u"qtcraft:stone_stairs"_s), 1);
        QCOMPARE(stone.value(u"qtcraft:stone_vertical_slab"_s), 2);
        QCOMPARE(stone.value(u"qtcraft:stone_bricks"_s), 1);
        QCOMPARE(stone.value(u"qtcraft:chiseled_stone_bricks"_s), 1);
        // On through the stone bricks into their shapes.
        QCOMPARE(stone.value(u"qtcraft:stone_bricks_slab"_s), 2);
        QCOMPARE(stone.value(u"qtcraft:stone_bricks_wall"_s), 1);
        // Stone has no wall (its material leaves it out), and nothing turns back into stone.
        QVERIFY(!stone.contains(u"qtcraft:stone_wall"_s));
        QVERIFY(cutsOf(u"qtcraft:stone_bricks"_s).value(u"qtcraft:chiseled_stone_bricks"_s) == 1);
        QVERIFY(!cutsOf(u"qtcraft:stone_bricks"_s).contains(u"qtcraft:stone_slab"_s));

        QCOMPARE(cutsOf(u"qtcraft:cobblestone"_s).value(u"qtcraft:cobblestone_wall"_s), 1);
        QCOMPARE(cutsOf(u"qtcraft:sandstone"_s).value(u"qtcraft:cut_sandstone"_s), 1);
        QCOMPARE(cutsOf(u"qtcraft:sandstone"_s).value(u"qtcraft:cut_sandstone_slab"_s), 2);
        // Wood is no stone; the crafting recipes of the parts stay as they were.
        QVERIFY(cutsOf(u"qtcraft:oak_planks"_s).isEmpty());
        QVERIFY(m_content->recipeIndex(u"qtcraft:stone_slab"_s).has_value());
        // The smelting recipes keep their own list.
        QVERIFY(std::ranges::none_of(m_content->smeltingRecipes(), [](const QcSmeltingRecipe &recipe) {
            return recipe.category == QLatin1StringView(QcSmeltingRecipe::stonecuttingCategory);
        }));
    }

    void recipesReachTheClient()
    {
        // The content as the client gets it carries the same recipes.
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> copy = QcContentSerializer::read(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        QCOMPARE(copy->stonecuttingRecipes().size(), m_content->stonecuttingRecipes().size());
        QCOMPARE(copy->smeltingRecipes().size(), m_content->smeltingRecipes().size());
        QVERIFY(copy->block(*copy->blockId(u"qtcraft:stonecutter"_s)).opensStonecutter);
    }

    void protocolMessagesRoundTrip()
    {
        const QcMsg::CutStone cut{u"qtcraft:stone_slab_from_stone"_s, true};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(cut), QcProtocol::Peer::Client);
        QVERIFY(decoded.has_value());
        const auto *back = std::get_if<QcMsg::CutStone>(&*decoded);
        QVERIFY(back);
        QCOMPARE(back->recipe, cut.recipe);
        QCOMPARE(back->repeat, true);
        // Only the server opens the screen.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::OpenStonecutter{{1, 2, 3}}), QcProtocol::Peer::Client).has_value());
        QVERIFY(QcProtocol::decode(QcProtocol::encode(QcMsg::OpenStonecutter{{1, 2, 3}}), QcProtocol::Peer::Server).has_value());
    }

    void clientListsTheStoneItCuts()
    {
        const QList<QcItemStack> inventory{{item("qtcraft:oak_planks"), 4, 0}, {item("qtcraft:stone"), 3, 0}, {}, {item("qtcraft:stone"), 5, 0},
                                           {item("qtcraft:cobblestone"), 1, 0}};
        QCOMPARE(QcClientStonecutter::inputs(*m_content, inventory), (QStringList{u"qtcraft:stone"_s, u"qtcraft:cobblestone"_s}));
    }

    void cutsFromTheInventory()
    {
        join();
        hold(u"stone"_s, 5);
        const QcBlockPos cutter = openStonecutter();
        QCOMPARE(m_client->stonecutter, std::optional(cutter));

        m_client->sounds.clear();
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 2);
        // The cut sounds like the stonecutter being struck.
        QVERIFY2(std::ranges::any_of(m_client->sounds, [](const QString &key) { return key.endsWith(u".hit"_s); }),
                 qPrintable(m_client->sounds.join(u", "_s)));

        send(QcMsg::CutStone{u"qtcraft:stone_stairs_from_stone"_s, true});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 0);
        QCOMPARE(countOf(baseItem(u"stone_stairs"_s)), 4);

        // Nothing left to cut, an unknown recipe, a recipe for another stone: nothing happens.
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        send(QcMsg::CutStone{u"qtcraft:nothing"_s, false});
        send(QcMsg::CutStone{u"qtcraft:cobblestone_wall_from_cobblestone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 2);
        QCOMPARE(countOf(baseItem(u"cobblestone_wall"_s)), 0);
    }

    void aFullInventoryCutsNothing()
    {
        join();
        hold(u"stone"_s, 2);
        // Every other slot holds a full stack of dirt: one of the two stones goes, two slabs have no
        // room.
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            if (player().inventory.slot(slot).isEmpty()) {
                player().inventory.setSlot(slot, {item("qtcraft:dirt"), 64, 0});
            }
        }
        openStonecutter();
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 2);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);
        // The last stone frees its slot for what is cut from it.
        player().inventory.setSlot(slotOf(baseItem(u"stone"_s)), {item("qtcraft:stone"), 1, 0});
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 0);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 2);
    }

    void repeatedCutsStopWhereTheRoomEnds()
    {
        join();
        hold(u"stone"_s, 40);
        // One empty slot and 63 slabs: 32 cuts fill the free 65 places but one.
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            if (player().inventory.slot(slot).isEmpty()) {
                player().inventory.setSlot(slot, {item("qtcraft:dirt"), 64, 0});
            }
        }
        player().inventory.setSlot(QcInventory::slotCount - 1, {});
        player().inventory.setSlot(QcInventory::slotCount - 2, {item("qtcraft:stone_slab"), 63, 0});
        openStonecutter();
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, true});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 63 + 2 * 32);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 40 - 32);
    }

    void outOfReachClosesTheScreen()
    {
        join();
        hold(u"stone"_s, 4);
        const QcBlockPos cutter = openStonecutter();
        QCOMPARE(m_client->stonecutter, std::optional(cutter));
        player().motion.position = player().motion.position + QcVec3{0.0, 0.0, 20.0};
        ticks(2);
        QVERIFY(m_client->stonecutterClosed);
        QVERIFY(!m_client->stonecutter.has_value());
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
    }

    void aSpectatorCutsNothing()
    {
        join();
        hold(u"stone"_s, 4);
        openStonecutter();
        chat(u"/gamemode spectator"_s);
        ticks(1);
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);
    }

    void cutsOnlyWithTheScreenOpen()
    {
        join();
        hold(u"stone"_s, 4);
        const QcBlockPos cutter = openStonecutter();
        QCOMPARE(m_client->stonecutter, std::optional(cutter));
        send(QcMsg::CloseStonecutter{});
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);

        // Opened again and the stonecutter broken: the server closes the screen and cuts nothing.
        send(QcMsg::UseItem{cutter, QcBlockFace::West});
        ticks(1);
        QVERIFY(m_client->stonecutter.has_value());
        m_server->world().setBlock(cutter, qcAirBlockId);
        ticks(1);
        QVERIFY(m_client->stonecutterClosed);
        QVERIFY(!m_client->stonecutter.has_value());
        send(QcMsg::CutStone{u"qtcraft:stone_slab_from_stone"_s, false});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
    }
};

QTEST_GUILESS_MAIN(TestStonecutter)
#include "test_stonecutter.moc"
