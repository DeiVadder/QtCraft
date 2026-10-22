#include "common/blockrules/QcBlockRules.h"
#include "common/gameplay/QcCrafting.h"
#include "QcCircuitFixture.h"
#include "server/core/QcContactDamage.h"
#include "server/machines/QcMachineRules.h"
#include "server/signal/QcComparatorReadings.h"
#include "server/storage/QcBlockEntityCodec.h"
#include "server/storage/QcLoadWarnings.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The kitchen and workshop blocks of package 62.2b (decisions E-770 to E-774): the smoker and the
// blast furnace smelt only their kind of recipe twice as fast; the campfire cooks food laid on it,
// hurts bodies but not items, goes out and is lit again; cake is eaten bite by bite and crafted with
// milk buckets that come back; lily pads lie on water, in swamps too.
class TestKitchenBlocks : public QcCircuitFixture
{
    Q_OBJECT

private:
    QcItemId baseItem(const QString &name) const { return item(qPrintable(u"qtcraft:"_s + name)); }

    // Result and count of a 3 x 3 crafting grid with the named items in the listed cells; an empty
    // name and 0 when no recipe matches.
    std::pair<QString, int> crafted(const QList<std::pair<int, QString>> &cells) const
    {
        QList<QcItemStack> grid(9);
        for (const auto &[cell, name] : cells) {
            grid[cell] = {baseItem(name), 1, 0};
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, 3);
        if (!recipe) {
            return {};
        }
        return {m_content->recipes()[*recipe].resultItem, m_content->recipes()[*recipe].resultCount};
    }

    // The result a machine block makes of `input`, empty when it does not take it.
    QString smeltedIn(const QcMachineRules &rules, const char *machine, const QString &input) const
    {
        const QcMachineDefinition *definition = rules.machineOf(id(machine));
        const QcSmeltingRecipe *recipe = definition ? rules.recipeFor(*definition, baseItem(input)) : nullptr;
        return recipe ? recipe->resultItem : QString();
    }

    // Fills the machine at `pos` with input and coal and lets `seconds` of game time pass.
    QcBlockEntities::Machine runMachine(QcBlockPos pos, const QString &input, double seconds)
    {
        if (!ensureEntity(pos)) {
            QcTestUtil::failCase(u"no machine at the position"_s);
            return {};
        }
        QcBlockEntities::Machine &machine = *m_server->blockEntities().machine(pos);
        machine.stacks = {QcItemStack{baseItem(input), 8, 0}, QcItemStack{baseItem(u"coal"_s), 4, 0}, QcItemStack{}};
        ticks(int(std::lround(seconds / QcPhysics::tickDuration)));
        return *m_server->blockEntities().machine(pos);
    }

    void hold(const QString &name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(name).arg(count));
        const int slot = slotOf(baseItem(name));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // An empty hand: the last hotbar slot, which the tests leave empty.
    void holdNothing() { send(QcMsg::SelectHotbarSlot{QcInventory::hotbarSize - 1}); }

    QString nameAt(QcBlockPos pos) const { return m_content->block(*m_server->world().blockAt(pos)).name; }
    QString propertyAt(QcBlockPos pos, const QString &name) const { return m_content->property(*m_server->world().blockAt(pos), name); }

    // The items on the campfire at `pos` on the server, empty slots left out.
    QList<QcItemId> itemsOn(QcBlockPos pos) const
    {
        QList<QcItemId> items;
        if (const QcCampfires::Entry *entry = m_server->world().campfires().at(pos)) {
            for (const QcCampfires::Slot &slot : entry->places) {
                if (!slot.stack.isEmpty()) {
                    items.append(slot.stack.item);
                }
            }
        }
        return items;
    }

    // Cooked items in the inventory and on the ground.
    int cooked(const QString &name) const { return countOf(baseItem(name)) + droppedCount(baseItem(name)); }

    // Moves the first stack of `name` into slot `to` (a crafting cell).
    void moveFirst(const QString &name, int to)
    {
        const int from = slotOf(baseItem(name));
        QVERIFY(from >= 0);
        move(from, to, 1);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void smokerAndBlastFurnaceCraft()
    {
        QCOMPARE(crafted({{1, u"oak_log"_s}, {3, u"spruce_log"_s}, {4, u"furnace"_s}, {5, u"stripped_birch_wood"_s}, {7, u"oak_wood"_s}}),
                 (std::pair{u"qtcraft:smoker"_s, 1}));
        QCOMPARE(crafted({{0, u"iron_ingot"_s}, {1, u"iron_ingot"_s}, {2, u"iron_ingot"_s}, {3, u"iron_ingot"_s}, {4, u"furnace"_s},
                          {5, u"iron_ingot"_s}, {6, u"smooth_stone"_s}, {7, u"smooth_stone"_s}, {8, u"smooth_stone"_s}}),
                 (std::pair{u"qtcraft:blast_furnace"_s, 1}));
    }

    void eachMachineSmeltsItsKinds()
    {
        const QcMachineRules rules(m_content);
        // The furnace takes everything, the smoker only food, the blast furnace only raw metal.
        QCOMPARE(smeltedIn(rules, "qtcraft:furnace", u"raw_beef"_s), u"qtcraft:cooked_beef"_s);
        QCOMPARE(smeltedIn(rules, "qtcraft:furnace", u"raw_iron"_s), u"qtcraft:iron_ingot"_s);
        QCOMPARE(smeltedIn(rules, "qtcraft:furnace", u"sand"_s), u"qtcraft:glass"_s);
        for (const QString &food : {u"raw_beef"_s, u"raw_porkchop"_s, u"raw_chicken"_s, u"raw_mutton"_s, u"raw_cod"_s, u"raw_salmon"_s, u"potato"_s}) {
            QVERIFY2(!smeltedIn(rules, "qtcraft:smoker", food).isEmpty(), qPrintable(food));
            QVERIFY2(smeltedIn(rules, "qtcraft:blast_furnace", food).isEmpty(), qPrintable(food));
        }
        for (const QString &metal : {u"raw_iron"_s, u"raw_gold"_s, u"raw_copper"_s}) {
            QVERIFY2(!smeltedIn(rules, "qtcraft:blast_furnace", metal).isEmpty(), qPrintable(metal));
            QVERIFY2(smeltedIn(rules, "qtcraft:smoker", metal).isEmpty(), qPrintable(metal));
        }
        for (const QString &other : {u"sand"_s, u"cobblestone"_s, u"oak_log"_s, u"clay_ball"_s, u"rotten_flesh"_s}) {
            QVERIFY2(smeltedIn(rules, "qtcraft:smoker", other).isEmpty(), qPrintable(other));
            QVERIFY2(smeltedIn(rules, "qtcraft:blast_furnace", other).isEmpty(), qPrintable(other));
        }

        // Every food and metal recipe of the base game has its kind.
        int food = 0;
        int metal = 0;
        for (const QcSmeltingRecipe &recipe : m_content->smeltingRecipes()) {
            food += recipe.kind == u"food"_s ? 1 : 0;
            metal += recipe.kind == u"metal"_s ? 1 : 0;
        }
        QCOMPARE(food, 7);
        QCOMPARE(metal, 3);
    }

    void smokerAndBlastFurnaceWorkTwiceAsFast()
    {
        buildArea();
        const QcBlockPos furnace = at(2, 0, 2);
        const QcBlockPos smoker = at(4, 0, 2);
        const QcBlockPos blast = at(6, 0, 2);
        m_server->world().setBlock(furnace, id("qtcraft:furnace"));
        m_server->world().setBlock(smoker, id("qtcraft:smoker"));
        m_server->world().setBlock(blast, id("qtcraft:blast_furnace"));

        // Recipes take 10 s at speed 1: after 5.5 s the furnace has nothing, the others one item.
        const QcBlockEntities::Machine slow = runMachine(furnace, u"raw_beef"_s, 5.5);
        QVERIFY(slow.stacks[QcMachineRules::outputSlot].isEmpty());
        const QcBlockEntities::Machine cooked = runMachine(smoker, u"raw_beef"_s, 5.5);
        QCOMPARE(cooked.stacks[QcMachineRules::outputSlot], (QcItemStack{baseItem(u"cooked_beef"_s), 1, 0}));
        const QcBlockEntities::Machine molten = runMachine(blast, u"raw_iron"_s, 5.5);
        QCOMPARE(molten.stacks[QcMachineRules::outputSlot], (QcItemStack{baseItem(u"iron_ingot"_s), 1, 0}));

        // What a machine does not take stays without burning fuel.
        const QcBlockEntities::Machine refused = runMachine(blast, u"raw_beef"_s, 2.0);
        QCOMPARE(refused.stacks[QcMachineRules::fuelSlot].count, 4);
        QCOMPARE(refused.stacks[QcMachineRules::inputSlot].count, 8);
    }

    void sameInputRecipesOfOtherKindsAreNotShadowed()
    {
        // Two recipes of one category for the same input: the furnace takes the first, the smoker the
        // first of its kind, also when another kind comes first (decision E-770).
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        content->registerSmeltingRecipe({u"test:stick_ash"_s, u"smelting"_s, u"qtcraft:stick"_s, u"qtcraft:charcoal"_s, 1, 10.0});
        QcSmeltingRecipe roasted{u"test:stick_roasted"_s, u"smelting"_s, u"qtcraft:stick"_s, u"qtcraft:torch"_s, 1, 10.0};
        roasted.kind = u"food"_s;
        content->registerSmeltingRecipe(roasted);
        const QcMachineRules rules(content);
        QCOMPARE(smeltedIn(rules, "qtcraft:furnace", u"stick"_s), u"qtcraft:charcoal"_s);
        QCOMPARE(smeltedIn(rules, "qtcraft:smoker", u"stick"_s), u"qtcraft:torch"_s);
        QCOMPARE(smeltedIn(rules, "qtcraft:blast_furnace", u"stick"_s), QString());
    }

    void hoppersFillOnlyWhatAMachineSmelts()
    {
        buildArea();
        const QcBlockPos blast = at(2, 0, 2);
        const QcBlockPos furnace = at(4, 0, 2);
        m_server->world().setBlock(blast, id("qtcraft:blast_furnace"));
        m_server->world().setBlock(furnace, id("qtcraft:furnace"));
        const QcItemStack beef{baseItem(u"raw_beef"_s), 2, 0};
        const QcItemStack iron{baseItem(u"raw_iron"_s), 2, 0};
        for (const QcBlockPos &machine : {blast, furnace}) {
            const QcBlockPos hopper = machine.offset(0, 1, 0);
            m_server->world().setBlock(hopper, state(u"signal_hopper[facing=down]"_s));
            QVERIFY(ensureEntity(hopper));
            m_server->blockEntities().container(hopper)->stacks[0] = beef;
            m_server->blockEntities().container(hopper)->stacks[1] = iron;
        }
        ticks(80);

        // The blast furnace leaves the beef in the hopper and takes the iron; the furnace takes both
        // in turn (the beef first, as in the reference).
        const QcBlockEntities::Container *above = m_server->blockEntities().container(blast.offset(0, 1, 0));
        QCOMPARE(above->stacks[0], beef);
        QVERIFY(above->stacks[1].count < 2);
        const QcBlockEntities::Machine *hot = m_server->blockEntities().machine(blast);
        QVERIFY(hot && hot->stacks[QcMachineRules::inputSlot].item == iron.item);
        const QcBlockEntities::Machine *plain = m_server->blockEntities().machine(furnace);
        QVERIFY(plain && plain->stacks[QcMachineRules::inputSlot].item == beef.item);
    }

    void campfireCooksFoodLaidOnIt()
    {
        join();
        const QcBlockPos fire = m_feet.offset(3, 0, 0);
        m_server->world().setBlock(fire, id("qtcraft:campfire"));
        QCOMPARE(m_content->block(id("qtcraft:campfire")).lightLevel, 15);
        QCOMPARE(m_content->block(id("qtcraft:unlit_campfire")).lightLevel, 0);

        // Food goes onto the fire one at a time, at most four; the client hears what lies there.
        hold(u"raw_beef"_s, 6);
        for (int use = 0; use < 5; ++use) {
            send(QcMsg::UseItem{fire, QcBlockFace::Up});
        }
        QCOMPARE(itemsOn(fire).size(), 4);
        QCOMPARE(countOf(baseItem(u"raw_beef"_s)), 2);
        QCOMPARE(m_client->campfires.value(fire), (QList<quint32>{baseItem(u"raw_beef"_s), baseItem(u"raw_beef"_s), baseItem(u"raw_beef"_s),
                                                                   baseItem(u"raw_beef"_s)}));
        QVERIFY(m_server->world().blockAt(fire.offset(0, 1, 0)) == qcAirBlockId);

        // What it cannot cook is not taken: sand is placed on top as usual.
        hold(u"sand"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(itemsOn(fire).size(), 4);
        m_server->world().setBlock(fire.offset(0, 1, 0), qcAirBlockId);

        // After 30 s each comes off cooked and lies near the fire without burning.
        ticks(int(29.0 / QcPhysics::tickDuration));
        QCOMPARE(itemsOn(fire).size(), 4);
        ticks(int(1.5 / QcPhysics::tickDuration));
        QVERIFY(itemsOn(fire).isEmpty());
        QCOMPARE(m_client->campfires.value(fire), QList<quint32>{});
        ticks(int(3.0 / QcPhysics::tickDuration));
        QCOMPARE(cooked(u"cooked_beef"_s), 4);
    }

    void campfireHurtsBodiesButNotItems()
    {
        join();
        const QcBlockPos fire = m_feet.offset(3, 0, 0);
        m_server->world().setBlock(fire, id("qtcraft:campfire"));
        const QcContactDamage contact(*m_content);
        // A body standing on the logs (7/16 high) touches the campfire's cell.
        const QcAabb standing{{fire.x + 0.2, fire.y + 0.44, fire.z + 0.2}, {fire.x + 0.8, fire.y + 2.2, fire.z + 0.8}};
        const QcContactDamage::Touch touch = contact.touching(m_server->world(), standing);
        QCOMPARE(touch.damage, 1.0);
        QCOMPARE(touch.alightTicks, 0);
        QVERIFY(!touch.burnsItems);

        // Fire burns items; an unlit campfire does nothing.
        m_server->world().setBlock(fire.offset(1, 0, 0), id("qtcraft:fire"));
        QVERIFY(contact.touching(m_server->world(), standing.moved({1.0, 0.0, 0.0})).burnsItems);
        m_server->world().setBlock(fire, id("qtcraft:unlit_campfire"));
        QCOMPARE(contact.touching(m_server->world(), standing).damage, 0.0);
    }

    void campfireSparesCreativePlayers()
    {
        join();
        m_server->world().setBlock(m_feet, id("qtcraft:campfire"));
        // In creative mode nothing hurts; back in survival the fire does.
        chat(u"/gamemode creative"_s);
        idle(40);
        QCOMPARE(m_client->health, 20.0);
        chat(u"/gamemode survival"_s);
        idle(40);
        QVERIFY(m_client->health < 20.0);
    }

    void adventurePlayersCookAndEatButDoNotPutOut()
    {
        join();
        const QcBlockPos fire = m_feet.offset(3, 0, 0);
        const QcBlockPos cake = m_feet.offset(2, 0, 2);
        m_server->world().setBlock(fire, id("qtcraft:campfire"));
        m_server->world().setBlock(cake, id("qtcraft:cake"));
        chat(u"/gamemode adventure"_s);

        // Putting it out changes the block: refused without an allow-list entry (decision E-735).
        hold(u"water_bucket"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(nameAt(fire), u"qtcraft:campfire"_s);
        QCOMPARE(countOf(baseItem(u"water_bucket"_s)), 1);
        hold(u"iron_shovel"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(nameAt(fire), u"qtcraft:campfire"_s);

        // Cooking on it and eating cake change no block of the world's design.
        hold(u"raw_beef"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(itemsOn(fire).size(), 1);
        chat(u"/feed 4"_s);
        holdNothing();
        send(QcMsg::UseItem{cake, QcBlockFace::Up});
        QCOMPARE(propertyAt(cake, u"bites"_s), u"1"_s);

        // With the shovel allowed on campfires it goes out.
        chat(u"/adventure canPlaceOn iron_shovel campfire"_s);
        hold(u"iron_shovel"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(nameAt(fire), u"qtcraft:unlit_campfire"_s);
    }

    void campfireGoesOutAndIsLitAgain()
    {
        join();
        const QcBlockPos fire = m_feet.offset(3, 0, 0);
        m_server->world().setBlock(fire, id("qtcraft:campfire"));
        hold(u"raw_cod"_s, 2);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        send(QcMsg::UseItem{fire, QcBlockFace::Up});

        // A shovel puts it out (and wears); what lay on it drops raw.
        hold(u"iron_shovel"_s);
        const int shovel = m_client->selected;
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(nameAt(fire), u"qtcraft:unlit_campfire"_s);
        QCOMPARE(m_client->stacks[shovel].damage, 1);
        ticks(2);
        QCOMPARE(droppedCount(baseItem(u"raw_cod"_s)) + countOf(baseItem(u"raw_cod"_s)), 2);
        QVERIFY(m_server->world().campfires().at(fire) == nullptr);

        // An unlit one takes no food; flint and steel lights it again.
        hold(u"raw_salmon"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QVERIFY(itemsOn(fire).isEmpty());
        hold(u"flint_and_steel"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(nameAt(fire), u"qtcraft:campfire"_s);

        // A water bucket puts it out and is poured empty.
        hold(u"water_bucket"_s);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        QCOMPARE(nameAt(fire), u"qtcraft:unlit_campfire"_s);
        QCOMPARE(countOf(baseItem(u"water_bucket"_s)), 0);
        QCOMPARE(countOf(baseItem(u"bucket"_s)), 1);
        QVERIFY(m_server->world().blockAt(fire.offset(0, 1, 0)) == qcAirBlockId);

        // Breaking it gives charcoal, not the campfire.
        QCOMPARE(m_content->block(id("qtcraft:campfire")).drops.size(), 1);
        QCOMPARE(m_content->block(id("qtcraft:campfire")).drops.first().item, u"qtcraft:charcoal"_s);
    }

    void campfireItemsAreSavedWithTheChunk()
    {
        join();
        const QcBlockPos fire = m_feet.offset(3, 0, 0);
        m_server->world().setBlock(fire, id("qtcraft:campfire"));
        hold(u"potato"_s, 2);
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        send(QcMsg::UseItem{fire, QcBlockFace::Up});
        ticks(40);

        // Through the block entities' table and back, with the cooking time.
        QcBlockEntityRecord record;
        record.chunk = fire.chunk();
        record.campfires = m_server->world().campfires().savedOf(fire.chunk());
        QCOMPARE(record.campfires.size(), 1);
        QVERIFY(record.campfires.first().places[0].cooked > 1.9);
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(record, *m_content), fire.chunk(), *m_content, warnings);
        QVERIFY(read.has_value());
        QCOMPARE(read->campfires, record.campfires);

        // A chunk that comes back gets them where its campfire still stands, not where it went out.
        QcCampfires campfires(*m_content);
        campfires.restore(read->campfires, *m_server->world().chunk(fire.chunk()));
        QCOMPARE(campfires.at(fire)->places, record.campfires.first().places);
        m_server->world().setBlock(fire, id("qtcraft:unlit_campfire"));
        QcCampfires none(*m_content);
        none.restore(read->campfires, *m_server->world().chunk(fire.chunk()));
        QCOMPARE(none.size(), 0);
    }

    void cakeIsEatenPieceByPiece()
    {
        join();
        const QcBlockPos cake = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(cake, id("qtcraft:cake"));
        const QcComparatorReadings readings(m_server->world(), m_server->blockEntities(), m_server->carts(), m_server->entities().hangings());
        QCOMPARE(readings.at(cake), std::optional<int>(14));

        // A full player leaves it as it is, also with food in the hand (nothing is eaten either).
        hold(u"bread"_s);
        send(QcMsg::UseItem{cake, QcBlockFace::Up});
        QCOMPARE(propertyAt(cake, u"bites"_s), u"0"_s);
        QCOMPARE(countOf(baseItem(u"bread"_s)), 1);

        // Hungry, every use eats a piece of 2 food points; the seventh takes the rest.
        chat(u"/feed 4"_s);
        holdNothing();
        for (int bite = 1; bite <= 6; ++bite) {
            send(QcMsg::UseItem{cake, QcBlockFace::Up});
            QCOMPARE(propertyAt(cake, u"bites"_s), QString::number(bite));
            QCOMPARE(m_client->food, 4 + 2 * bite);
        }
        QCOMPARE(readings.at(cake), std::optional<int>(2));
        send(QcMsg::UseItem{cake, QcBlockFace::Up});
        QVERIFY(m_server->world().blockAt(cake) == qcAirBlockId);
        QCOMPARE(m_client->food, 18);

        // Broken, it gives nothing back.
        QVERIFY(m_content->block(id("qtcraft:cake")).drops.isEmpty());
        QVERIFY(m_content->block(id("qtcraft:cake")).support == QcBlockSupport::Below);
    }

    void cakeGivesTheBucketsBack()
    {
        join();
        chat(u"/give crafting_table"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:crafting_table"))});
        const QcBlockPos table = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{table.offset(0, -1, 0), QcBlockFace::Up});
        send(QcMsg::UseItem{table, QcBlockFace::Up});
        QCOMPARE(m_client->craftingSize, 3);

        // From the recipe list: the buckets come back into the inventory.
        for (const auto &[name, count] : {std::pair{u"milk_bucket"_s, 3}, {u"sugar"_s, 2}, {u"egg"_s, 1}, {u"wheat"_s, 3}}) {
            chat(u"/give %1 %2"_s.arg(name).arg(count));
        }
        send(QcMsg::CraftRecipe{u"qtcraft:cake"_s, 1});
        QCOMPARE(countOf(baseItem(u"cake"_s)), 1);
        QCOMPARE(countOf(baseItem(u"bucket"_s)), 3);
        QCOMPARE(countOf(baseItem(u"milk_bucket"_s)), 0);

        // From the grid: each bucket stays empty in its cell.
        for (const auto &[name, count] : {std::pair{u"milk_bucket"_s, 3}, {u"sugar"_s, 2}, {u"egg"_s, 1}, {u"wheat"_s, 3}}) {
            chat(u"/give %1 %2"_s.arg(name).arg(count));
        }
        constexpr int grid = QcMsg::MoveInventoryItem::craftingGridSlot;
        const QList<std::pair<QString, int>> cells{{u"milk_bucket"_s, 0}, {u"milk_bucket"_s, 1}, {u"milk_bucket"_s, 2}, {u"sugar"_s, 3},
                                                   {u"egg"_s, 4},          {u"sugar"_s, 5},       {u"wheat"_s, 6},       {u"wheat"_s, 7},
                                                   {u"wheat"_s, 8}};
        for (const auto &[name, cell] : cells) {
            moveFirst(name, grid + cell);
        }
        QCOMPARE(m_client->craftingResult.item, baseItem(u"cake"_s));
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(baseItem(u"cake"_s)), 2);
        for (int cell = 0; cell < 3; ++cell) {
            QCOMPARE(m_client->craftingGrid[cell], (QcItemStack{baseItem(u"bucket"_s), 1, 0}));
        }
        QVERIFY(m_client->craftingGrid[4].isEmpty());
    }

    void lilyPadsLieOnWater()
    {
        join();
        const QcBlockPos pool = m_feet.offset(2, -1, 0);
        m_server->world().setBlock(pool, id("qtcraft:water"));
        hold(u"lily_pad"_s, 3);

        // On solid ground it does not hold; aimed at the water from any side it lies on top.
        send(QcMsg::UseItem{m_feet.offset(-2, -1, 0), QcBlockFace::Up});
        QVERIFY(m_server->world().blockAt(m_feet.offset(-2, 0, 0)) == qcAirBlockId);
        send(QcMsg::UseItem{pool, QcBlockFace::East, {1.0, 0.5, 0.5}});
        QCOMPARE(nameAt(pool.offset(0, 1, 0)), u"qtcraft:lily_pad"_s);
        QCOMPARE(countOf(baseItem(u"lily_pad"_s)), 2);

        // Only onto the surface: water under water takes none.
        m_server->world().setBlock(pool.offset(0, 0, 2), id("qtcraft:water"));
        m_server->world().setBlock(pool.offset(0, 1, 2), id("qtcraft:water"));
        send(QcMsg::UseItem{pool.offset(0, 0, 2), QcBlockFace::East, {1.0, 0.5, 0.5}});
        QCOMPARE(nameAt(pool.offset(0, 1, 2)), u"qtcraft:water"_s);
        QCOMPARE(countOf(baseItem(u"lily_pad"_s)), 2);

        // Thin, but bodies stand on it; it breaks at once.
        const QcBlockDefinition &lily = m_content->block(id("qtcraft:lily_pad"));
        QVERIFY(lily.floats && lily.solid && lily.hardness == 0.0);
        const QList<QcAabb> &collision = m_content->state(id("qtcraft:lily_pad")).collision;
        QCOMPARE(collision.size(), 1);
        QVERIFY(collision.first().max.y > 0.05 && collision.first().max.y < 0.15);

        // Without its water it has no hold, on flowing water it has.
        QVERIFY(QcBlockRules::isSupported(m_server->world(), pool.offset(0, 1, 0), id("qtcraft:lily_pad")));
        m_server->world().setBlock(pool, m_content->withProperty(id("qtcraft:water"), u"level"_s, u"3"_s));
        QVERIFY(QcBlockRules::isSupported(m_server->world(), pool.offset(0, 1, 0), id("qtcraft:lily_pad")));
        m_server->world().setBlock(pool, id("qtcraft:grass_block"));
        QVERIFY(!QcBlockRules::isSupported(m_server->world(), pool.offset(0, 1, 0), id("qtcraft:lily_pad")));
    }

    void swampsGrowLilyPadsOnTheirWater()
    {
        // Lily pads lie on the sea-level water of swamp columns (and of the ponds of a flower
        // valley, decision E-821) and nowhere else.
        const QcTerrainGenerator generator(m_content, 42);
        const QcBlockId lily = id("qtcraft:lily_pad");
        const QcBlockId water = id("qtcraft:water");
        const int y = generator.seaLevel();
        int swampWater = 0;
        int lilies = 0;
        QSet<QcChunkPos> seen;
        // Chunks found by sampling the biomes on a coarse grid, until enough swamp water is seen.
        for (int sample = 0; sample < 160 * 160 && swampWater < 2000 && seen.size() < 300; ++sample) {
            const int sx = (sample % 160 - 80) * 48;
            const int sz = (sample / 160 - 80) * 48;
            const QcChunkPos pos = QcBlockPos{sx, 0, sz}.chunk();
            if (generator.biomeAt(sx, sz) != u"qtcraft:swamp"_s || seen.contains(pos)) {
                continue;
            }
            seen.insert(pos);
            const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
            for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize; ++cell) {
                const int x = cell % QcWorld::chunkSize;
                const int z = cell / QcWorld::chunkSize;
                const bool swamp = generator.biomeAt(pos.x * QcWorld::chunkSize + x, pos.z * QcWorld::chunkSize + z) == u"qtcraft:swamp"_s;
                const bool wet = chunk->block(x, y - 1, z) == water;
                swampWater += swamp && wet ? 1 : 0;
                if (chunk->block(x, y, z) == lily) {
                    const bool valley = generator.biomeAt(pos.x * QcWorld::chunkSize + x, pos.z * QcWorld::chunkSize + z)
                                        == u"qtcraft:flower_valley"_s;
                    QVERIFY2((swamp || valley) && wet, qPrintable(u"lily pad at %1 %2"_s.arg(pos.x * 16 + x).arg(pos.z * 16 + z)));
                    ++lilies;
                }
            }
        }
        qInfo("%d lily pads on %d columns of swamp water", lilies, swampWater);
        QVERIFY(swampWater > 200);
        QVERIFY(lilies > swampWater / 100 && lilies < swampWater / 3);
    }
};

QTEST_GUILESS_MAIN(TestKitchenBlocks)
#include "test_kitchen_blocks.moc"
