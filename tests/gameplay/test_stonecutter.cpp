// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientStonecutter.h"
#include "common/content/recipes/QcStonecutting.h"
#include "common/content/registry/QcContentParser.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcPlayerUseFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/storage/QcPlayerRecords.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The stonecutter (decisions E-784, E-785): its recipes come from data and from the building parts
// of stone materials at the reference's ratios; its screen has an input and an output slot as in the
// reference, reached through the slot messages. Taking the output uses up one input item, all or
// nothing; the input comes back on every way the screen ends, and nothing is ever doubled.
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

    static constexpr int inputSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int outputSlot = QcMsg::MoveInventoryItem::machineOutputSlot;
    static QString slab() { return u"qtcraft:stone_slab_from_stone"_s; }

    // A stonecutter next to the player (or `offset` away), opened through the protocol.
    QcBlockPos openStonecutter(int offset = 2)
    {
        const QcBlockPos cutter = m_feet.offset(offset, 0, 0);
        putAt(cutter, id("qtcraft:stonecutter"));
        send(QcMsg::UseItem{cutter, QcBlockFace::West});
        ticks(1);
        return cutter;
    }

    // Moves `count` stone from the inventory into the input slot.
    void putStoneIn(int count) { send(QcMsg::MoveInventoryItem{slotOf(baseItem(u"stone"_s)), inputSlot, count}); }

    void select(const QString &recipe) { send(QcMsg::SelectStonecutterRecipe{recipe}); }

    // The first empty inventory slot.
    int emptySlot() const
    {
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            if (m_client->stacks[slot].isEmpty()) {
                return slot;
            }
        }
        return -1;
    }

    // Fills every empty slot of the inventory with a full stack of dirt.
    void fillInventory()
    {
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            if (player().inventory.slot(slot).isEmpty()) {
                player().inventory.setSlot(slot, {item("qtcraft:dirt"), 64, 0});
            }
        }
    }

    // Stone in the input slot as the client was last told.
    int stoneInInput() const
    {
        const QcItemStack input = m_client->workstations.stonecutterState.input;
        return input.item == baseItem(u"stone"_s) ? input.count : 0;
    }

    // All stone there is: in the inventory, in the input slot and lying in the world.
    int allStone() const { return countOf(baseItem(u"stone"_s)) + stoneInInput() + droppedCount(baseItem(u"stone"_s)); }

    // A block with a screen (furnace, chest) two blocks west of the player, opened through the protocol.
    QcBlockPos openBlockScreen(const char *block)
    {
        const QcBlockPos pos = m_feet.offset(-2, 0, 0);
        putAt(pos, id(block));
        send(QcMsg::UseItem{pos, QcBlockFace::East});
        ticks(1);
        return pos;
    }

    // Rejoins as the same player after the connection ended.
    void rejoin()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(2);
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
        const QcMsg::SelectStonecutterRecipe choice{slab()};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(choice), QcProtocol::Peer::Client);
        QVERIFY(decoded.has_value());
        QCOMPARE(std::get<QcMsg::SelectStonecutterRecipe>(*decoded).recipe, choice.recipe);
        const QcMsg::StonecutterState state{{item("qtcraft:stone"), 7, 0}, slab()};
        const QcResult<QcMessage> back = QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server);
        QVERIFY(back.has_value());
        QCOMPARE(std::get<QcMsg::StonecutterState>(*back).input, state.input);
        QCOMPARE(std::get<QcMsg::StonecutterState>(*back).recipe, state.recipe);
        // Only the server opens the screen and tells its slots; only the client chooses.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::OpenStonecutter{{1, 2, 3}}), QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(choice), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::SelectStonecutterRecipe{QString(300, u'a')}), QcProtocol::Peer::Client).has_value());
    }

    void clientShowsTheSlots()
    {
        QcClientStonecutter cutter;
        // What the client sent, as text: the chosen recipe, or "close".
        QStringList sent;
        cutter.setSender([&sent](const QcMessage &message) {
            const auto *choice = std::get_if<QcMsg::SelectStonecutterRecipe>(&message);
            sent.append(choice ? choice->recipe : std::holds_alternative<QcMsg::CloseStonecutter>(message) ? u"close"_s : QString());
        });
        QVERIFY(!cutter.select(slab()));
        QVERIFY(cutter.handle(QcMsg::OpenStonecutter{{1, 2, 3}}));
        QVERIFY(cutter.handle(QcMsg::StonecutterState{{item("qtcraft:stone"), 3, 0}, slab()}));
        const QList<QcItemStack> stacks = cutter.slotStacks(*m_content);
        QCOMPARE(stacks.size(), 3);
        QCOMPARE(stacks[0], (QcItemStack{item("qtcraft:stone"), 3, 0}));
        QVERIFY(stacks[1].isEmpty());
        QCOMPARE(stacks[2], (QcItemStack{item("qtcraft:stone_slab"), 2, 0}));
        // A recipe for another input shows no output.
        QVERIFY(cutter.handle(QcMsg::StonecutterState{{item("qtcraft:cobblestone"), 3, 0}, slab()}));
        // A named list: indexing the returned temporary inside QVERIFY keeps clazy busy for minutes.
        const QList<QcItemStack> other = cutter.slotStacks(*m_content);
        QVERIFY(other[2].isEmpty());

        QVERIFY(cutter.select(u"qtcraft:cobblestone_wall_from_cobblestone"_s));
        QCOMPARE(sent.value(0), u"qtcraft:cobblestone_wall_from_cobblestone"_s);
        cutter.close();
        QVERIFY(!cutter.isOpen());
        QVERIFY(cutter.input().isEmpty());
        QCOMPARE(sent.value(1), u"close"_s);
        // A state still on its way after the own close is ignored.
        QVERIFY(cutter.handle(QcMsg::StonecutterState{{item("qtcraft:stone"), 3, 0}, slab()}));
        QVERIFY(cutter.input().isEmpty());
    }

    void putInSelectAndTake()
    {
        join();
        hold(u"stone"_s, 5);
        const QcBlockPos cutter = openStonecutter();
        QCOMPARE(m_client->workstations.stonecutter, std::optional(cutter));
        putStoneIn(3);
        QCOMPARE(stoneInInput(), 3);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 2);

        // Nothing chosen: the output gives nothing.
        const int target = emptySlot();
        send(QcMsg::MoveInventoryItem{outputSlot, target, 0});
        QCOMPARE(stoneInInput(), 3);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);

        select(slab());
        QCOMPARE(m_client->workstations.stonecutterState.recipe, slab());
        m_client->sounds.clear();
        send(QcMsg::MoveInventoryItem{outputSlot, target, 1});
        QCOMPARE(m_client->stacks[target], (QcItemStack{baseItem(u"stone_slab"_s), 2, 0}));
        QCOMPARE(stoneInInput(), 2);
        // Taking sounds like the stonecutter being struck.
        QVERIFY2(std::ranges::any_of(m_client->sounds, [](const QString &key) { return key.endsWith(u".hit"_s); }),
                 qPrintable(m_client->sounds.join(u", "_s)));

        // Onto the same stack, then onto another item: the second is refused.
        send(QcMsg::MoveInventoryItem{outputSlot, target, 0});
        QCOMPARE(m_client->stacks[target].count, 4);
        send(QcMsg::MoveInventoryItem{outputSlot, slotOf(baseItem(u"stone"_s)), 0});
        QCOMPARE(stoneInInput(), 1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 2);
        // Nothing goes into the output, and nothing else than the output comes out of it.
        send(QcMsg::MoveInventoryItem{slotOf(baseItem(u"stone"_s)), outputSlot, 0});
        QCOMPARE(countOf(baseItem(u"stone"_s)), 2);

        // The last input item: the slot empties and the choice goes with it.
        send(QcMsg::MoveInventoryItem{outputSlot, target, 0});
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 6);
        QCOMPARE(stoneInInput(), 0);
        QVERIFY(m_client->workstations.stonecutterState.recipe.isEmpty());
    }

    void theChoiceFollowsTheInput()
    {
        join();
        hold(u"stone"_s, 4);
        chat(u"/give cobblestone 2"_s);
        openStonecutter();
        // No input: no choice; an unknown recipe or one for another input is refused.
        select(slab());
        QVERIFY(m_client->workstations.stonecutterState.recipe.isEmpty());
        putStoneIn(2);
        select(u"qtcraft:nothing"_s);
        select(u"qtcraft:cobblestone_wall_from_cobblestone"_s);
        QVERIFY(m_client->workstations.stonecutterState.recipe.isEmpty());

        select(slab());
        // More of the same stone keeps the choice (shift-click puts stone into the input).
        send(QcMsg::QuickMoveItem{slotOf(baseItem(u"stone"_s))});
        QCOMPARE(stoneInInput(), 4);
        QCOMPARE(m_client->workstations.stonecutterState.recipe, slab());
        // Another stone swapped in drops it.
        send(QcMsg::MoveInventoryItem{slotOf(baseItem(u"cobblestone"_s)), inputSlot, 0});
        QCOMPARE(m_client->workstations.stonecutterState.input.item, baseItem(u"cobblestone"_s));
        QVERIFY(m_client->workstations.stonecutterState.recipe.isEmpty());
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
        // What the stonecutter cannot cut does not go in by shift-click.
        chat(u"/give oak_planks 3"_s);
        send(QcMsg::QuickMoveItem{slotOf(baseItem(u"oak_planks"_s))});
        QCOMPARE(countOf(baseItem(u"oak_planks"_s)), 3);
        // Taken out again by shift-click.
        send(QcMsg::QuickMoveItem{inputSlot});
        QVERIFY(m_client->workstations.stonecutterState.input.isEmpty());
        QCOMPARE(countOf(baseItem(u"cobblestone"_s)), 2);
    }

    void shiftTakeStopsWhereTheRoomEnds()
    {
        join();
        hold(u"stone"_s, 40);
        openStonecutter();
        putStoneIn(0);
        select(slab());
        // One empty slot and 63 slabs: 32 takes of two fill the free 65 places but one.
        fillInventory();
        player().inventory.setSlot(QcInventory::slotCount - 1, {});
        player().inventory.setSlot(QcInventory::slotCount - 2, {item("qtcraft:stone_slab"), 63, 0});
        send(QcMsg::QuickMoveItem{outputSlot});
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 63 + 2 * 32);
        QCOMPARE(stoneInInput(), 40 - 32);
        QCOMPARE(m_client->workstations.stonecutterState.recipe, slab());

        // With room for all: the input runs out and the choice goes.
        player().inventory = {};
        send(QcMsg::QuickMoveItem{outputSlot});
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 16);
        QCOMPARE(stoneInInput(), 0);
        QVERIFY(m_client->workstations.stonecutterState.recipe.isEmpty());
    }

    void aFullInventoryTakesNothing()
    {
        join();
        hold(u"stone"_s, 2);
        openStonecutter();
        putStoneIn(0);
        select(slab());
        fillInventory();
        send(QcMsg::MoveInventoryItem{outputSlot, 0, 0});
        send(QcMsg::QuickMoveItem{outputSlot});
        QCOMPARE(stoneInInput(), 2);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);

        // Closing with a full inventory throws the input out in front of the player.
        send(QcMsg::CloseStonecutter{});
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 2);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 0);
    }

    void theInputComesBackOnClose()
    {
        join();
        hold(u"stone"_s, 5);
        openStonecutter();
        putStoneIn(3);
        send(QcMsg::CloseStonecutter{});
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 0);
        // Closed: neither the output nor the choice does anything.
        select(slab());
        send(QcMsg::QuickMoveItem{outputSlot});
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);

        // Opened again it starts empty; another stonecutter gives the first one's input back.
        openStonecutter();
        putStoneIn(3);
        openStonecutter(-2);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
        QCOMPARE(stoneInInput(), 0);
    }

    void aBrokenStonecutterGivesTheInputBack()
    {
        join();
        hold(u"stone"_s, 5);
        const QcBlockPos cutter = openStonecutter();
        putStoneIn(3);
        select(slab());
        m_server->world().setBlock(cutter, qcAirBlockId);
        ticks(1);
        QVERIFY(m_client->workstations.stonecutterClosed);
        QVERIFY(!m_client->workstations.stonecutter.has_value());
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 0);
        send(QcMsg::QuickMoveItem{outputSlot});
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);
    }

    void outOfReachGivesTheInputBack()
    {
        join();
        hold(u"stone"_s, 4);
        openStonecutter();
        putStoneIn(4);
        player().motion.position = player().motion.position + QcVec3{0.0, 0.0, 20.0};
        ticks(2);
        QVERIFY(m_client->workstations.stonecutterClosed);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
    }

    void deathDropsTheInputOnce()
    {
        QcServerConfig config;
        config.keepInventory = false;
        join(config);
        hold(u"stone"_s, 5);
        openStonecutter();
        putStoneIn(3);
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(m_client->workstations.stonecutterClosed);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 0);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 5);
    }

    void deathWithKeepInventoryGivesTheInputBack()
    {
        join();
        QVERIFY(m_server->keepInventory());
        hold(u"stone"_s, 5);
        openStonecutter();
        putStoneIn(3);
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(m_client->workstations.stonecutterClosed);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 0);
    }

    void disconnectAndStopKeepTheInput()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        hold(u"stone"_s, 5);
        openStonecutter();
        putStoneIn(3);
        // A save while the screen is open stores the input in the inventory.
        QCOMPARE(QcPlayerRecords::of(player(), *m_content).player.inventory.countOf(baseItem(u"stone"_s)), 5);

        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);
        rejoin();
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 0);

        // The same through a server stop.
        openStonecutter();
        putStoneIn(3);
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        rejoin();
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
    }

    void aSpectatorCannotUseTheSlots()
    {
        join();
        hold(u"stone"_s, 4);
        openStonecutter();
        putStoneIn(2);
        select(slab());
        chat(u"/gamemode spectator"_s);
        ticks(1);
        // The screen closed and its input went back before the inventories changed.
        QVERIFY(m_client->workstations.stonecutterClosed);
        send(QcMsg::SelectStonecutterRecipe{slab()});
        send(QcMsg::QuickMoveItem{outputSlot});
        send(QcMsg::MoveInventoryItem{outputSlot, 0, 0});
        chat(u"/gamemode survival"_s);
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);
        QCOMPARE(allStone(), 4);
    }

    void longCrossPackNamesAreSkipped()
    {
        // Pack recipes cutting blocks of another pack one to one into stone: the stone's parts are
        // cut from them too, under "<part>_from_<namespace>:<path>", unless that name is too long.
        QcContentRegistry content = *m_content;
        const QString shortInput = u"otherpack:rock"_s;
        const QString longInput = u"otherpack:"_s + QString(240, u'a');
        for (const QString &input : {shortInput, longInput}) {
            const QString recipeName = input == shortInput ? u"otherpack:rock_to_stone"_s : u"otherpack:long_to_stone"_s;
            QcItemDefinition definition;
            definition.name = input;
            QVERIFY(content.registerItem(definition).has_value());
            QcSmeltingRecipe recipe;
            recipe.name = recipeName;
            recipe.category = QString::fromLatin1(QcSmeltingRecipe::stonecuttingCategory);
            recipe.inputItem = input;
            recipe.resultItem = u"qtcraft:stone"_s;
            content.registerStonecuttingRecipe(recipe);
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Stonecutting recipe .*otherpack:a+.* skipped"_s));
        QcStonecutting::generate(content);

        const bool shortCut = content.stonecuttingIndex(u"qtcraft:stone_slab_from_otherpack:rock"_s).has_value();
        QVERIFY(shortCut);
        const QList<qsizetype> longCuts = QcStonecutting::recipesFor(content, longInput);
        QCOMPARE(longCuts.size(), 1);
        const QList<QcSmeltingRecipe> &recipes = content.stonecuttingRecipes();
        const bool allFit = std::ranges::all_of(recipes, [](const QcSmeltingRecipe &recipe) {
            return recipe.name.size() <= QcStonecutting::maxRecipeName;
        });
        QVERIFY(allFit);

        // A pack's own recipe with so long a name is refused when it is read.
        const QJsonObject json{{u"name"_s, QString(QcStonecutting::maxRecipeName, u'b')}, {u"input"_s, u"stone"_s},
                               {u"result"_s, QJsonObject{{u"item"_s, u"stone_slab"_s}, {u"count"_s, 2}}}};
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Stonecutting recipe .*b+.* skipped"_s));
        const bool parsed = QcContentParser::parseStonecuttingRecipe(json, u"qtcraft"_s).has_value();
        QVERIFY(!parsed);
        QJsonObject fitting = json;
        fitting.insert(u"name"_s, u"short_cut"_s);
        const bool fittingParsed = QcContentParser::parseStonecuttingRecipe(fitting, u"qtcraft"_s).has_value();
        QVERIFY(fittingParsed);
    }

    void aBlockScreenClosesTheStonecutter()
    {
        join();
        hold(u"stone"_s, 5);
        openStonecutter();
        putStoneIn(3);
        select(slab());

        // The furnace's screen takes over: the stonecutter closes and gives its input back, and the
        // slots 45 to 47 are the furnace's.
        const QcBlockPos furnace = openBlockScreen("qtcraft:furnace");
        const bool cutterOpen = player().screen.stonecutter.has_value();
        QVERIFY(!cutterOpen);
        QVERIFY(m_client->workstations.stonecutterClosed);
        QCOMPARE(m_client->openMachine, std::optional(furnace));
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
        send(QcMsg::MoveInventoryItem{slotOf(baseItem(u"stone"_s)), inputSlot, 2});
        const QcItemStack smelting = m_server->blockEntities().machine(furnace)->stacks[0];
        QCOMPARE(smelting.count, 2);
        send(QcMsg::QuickMoveItem{outputSlot});
        QCOMPARE(countOf(baseItem(u"stone_slab"_s)), 0);

        // And back: the stonecutter closes the furnace, whose slots stay as they are.
        openStonecutter();
        QVERIFY(m_client->machineClosed);
        QVERIFY(!m_client->openMachine.has_value());
        putStoneIn(1);
        QCOMPARE(stoneInInput(), 1);
        const QcItemStack stillSmelting = m_server->blockEntities().machine(furnace)->stacks[0];
        QCOMPARE(stillSmelting.count, 2);
    }

    void aChestClosesTheStonecutterAndBack()
    {
        join();
        hold(u"stone"_s, 5);
        const QcBlockPos chest = openBlockScreen("qtcraft:chest");
        send(QcMsg::MoveInventoryItem{slotOf(baseItem(u"stone"_s)), inputSlot, 1});
        const QcItemStack stored = m_server->blockEntities().container(chest)->stacks[0];
        QCOMPARE(stored.count, 1);

        // The stonecutter closes the chest; 45 is its input now.
        openStonecutter();
        QVERIFY(m_client->machineClosed);
        const bool chestOpen = player().screen.open.has_value();
        QVERIFY(!chestOpen);
        putStoneIn(3);
        QCOMPARE(stoneInInput(), 3);
        const QcItemStack stillStored = m_server->blockEntities().container(chest)->stacks[0];
        QCOMPARE(stillStored.count, 1);

        // The chest again: the input held comes back.
        openBlockScreen("qtcraft:chest");
        QVERIFY(m_client->workstations.stonecutterClosed);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 4);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 0);
    }

    void reopeningKeepsTheInput()
    {
        join();
        hold(u"stone"_s, 5);
        openStonecutter();
        putStoneIn(3);
        select(slab());
        openStonecutter();
        QCOMPARE(stoneInInput(), 3);
        QCOMPARE(m_client->workstations.stonecutterState.recipe, slab());
        QCOMPARE(countOf(baseItem(u"stone"_s)), 2);
    }

    void creativeCloseWithAFullInventoryDropsNothing()
    {
        join();
        chat(u"/gamemode creative"_s);
        ticks(1);
        hold(u"stone"_s, 2);
        openStonecutter();
        putStoneIn(0);
        fillInventory();
        // Catalogue items without room are gone rather than thrown into the world (decision E-283).
        send(QcMsg::CloseStonecutter{});
        const bool cutterOpen = player().screen.stonecutter.has_value();
        QVERIFY(!cutterOpen);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 0);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 0);
    }

    void switchingToCreativeReturnsTheInput()
    {
        join();
        hold(u"stone"_s, 5);
        openStonecutter();
        putStoneIn(3);
        chat(u"/gamemode creative"_s);
        ticks(1);
        // The input went into the survival inventory, which is put aside.
        const bool emptyInput = !player().screen.stonecutter || player().screen.stonecutter->input.isEmpty();
        QVERIFY(emptyInput);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 0);
        chat(u"/gamemode survival"_s);
        ticks(1);
        QCOMPARE(countOf(baseItem(u"stone"_s)), 5);
        QCOMPARE(droppedCount(baseItem(u"stone"_s)), 0);
    }
};

QTEST_GUILESS_MAIN(TestStonecutter)
#include "test_stonecutter.moc"
