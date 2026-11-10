// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcArmour.h"
#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcSmithing.h"
#include "fixtures/QcTestUtil.h"
#include "machines/QcEnchantingFixture.h"
#include "server/entities/QcHurtWindow.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/mobs/QcMobManager.h"
#include "server/projectiles/QcServerProjectiles.h"
#include "server/storage/QcPlayerRecords.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Netherite (decisions E-1115 to E-1118): the smithing table's rules and its screen through the
// protocol (the upgrade keeps damage, enchantments and name, takes one template and one ingot,
// refuses an output the client did not see, quick moves sort into the three slots, the slots go
// back on closing, death drops them, disconnect and a broken table give them back), the netherite values against the role model, the recipes around it, armour
// toughness and knockback resistance (a full set holds back 0.4 of a blow's and an arrow's push), and netherite items that neither burn nor sink in lava.
class TestSmithing : public QcEnchantingFixture
{
    Q_OBJECT

private:
    static constexpr int templateSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int baseSlot = QcMsg::MoveInventoryItem::machineFuelSlot;
    static constexpr int additionSlot = QcMsg::MoveInventoryItem::machineOutputSlot;

    const QcMsg::SmithingState &smithingState() const { return m_client->workstations.smithingState; }

    const QcItemDefinition &definition(const char *name) const { return m_content->item(item(name)); }

    // What the 3 x 3 `grid` crafts: the result's item and count; empty for nothing.
    std::pair<QString, int> crafted(const QList<QcItemStack> &grid) const
    {
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, 3);
        if (!recipe) {
            return {};
        }
        const QcRecipeDefinition &found = m_content->recipes()[*recipe];
        return {found.resultItem, found.resultCount};
    }

    // A smithing table three blocks east of the player, opened through the protocol.
    QcBlockPos openTable()
    {
        const QcBlockPos table = tablePos();
        putAt(table.offset(0, -1, 0), id("qtcraft:stone"));
        putAt(table, id("qtcraft:smithing_table"));
        send(QcMsg::UseItem{table, QcBlockFace::West});
        ticks(1);
        return table;
    }

    // Puts `stack` into the player's inventory and from there into the table's `slot`.
    void putIntoTable(const QcItemStack &stack, int slot)
    {
        player().inventory.setSlot(20, stack);
        move(20, slot);
    }

    // A worn, enchanted and named diamond sword.
    QcItemStack treasuredSword() const
    {
        QcItemStack sword = stackOf("qtcraft:diamond_sword", {{u"qtcraft:sharpness"_s, 3}}).withName(u"Edge"_s).withPriorWork(2);
        sword.damage = 300;
        return sword;
    }

    // The template, a base and an ingot as the three slots.
    QcSmithing::Inputs inputs(const QcItemStack &base, int templates = 1, int ingots = 1) const
    {
        return {QcItemStack{item("qtcraft:netherite_upgrade_smithing_template"), templates, 0}, base,
                QcItemStack{item("qtcraft:netherite_ingot"), ingots, 0}};
    }

    // Wears a full set of netherite armour.
    void wearNetherite()
    {
        player().equipment.at(QcEquipmentSlot::Head) = stackOf("qtcraft:netherite_helmet");
        player().equipment.at(QcEquipmentSlot::Chest) = stackOf("qtcraft:netherite_chestplate");
        player().equipment.at(QcEquipmentSlot::Legs) = stackOf("qtcraft:netherite_leggings");
        player().equipment.at(QcEquipmentSlot::Feet) = stackOf("qtcraft:netherite_boots");
    }

    // The player's eastward speed right after an arrow from the west hit him.
    double arrowPush()
    {
        player().health = QcServerClient::maxHealth;
        player().hurt.clear();
        player().motion.position = {m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        player().motion.velocity = {};
        const QcVec3 from{m_feet.x - 2.5, m_feet.y + 1.0, m_feet.z + 0.5};
        if (!m_server->systems().projectiles.launch({0, u"qtcraft:arrow"_s, from, {60.0, 0.0, 0.0}, QcProjectilePickup::Never})) {
            return 0.0;
        }
        ticks(2);
        return player().motion.velocity.x;
    }

    // Fills the open table's three slots with a template, a diamond sword and an ingot.
    void fillTable()
    {
        putIntoTable(QcItemStack{item("qtcraft:netherite_upgrade_smithing_template"), 1, 0}, templateSlot);
        putIntoTable(stackOf("qtcraft:diamond_sword"), baseSlot);
        putIntoTable(QcItemStack{item("qtcraft:netherite_ingot"), 1, 0}, additionSlot);
        QCOMPARE(smithingState().output.item, item("qtcraft:netherite_sword"));
    }

    // How many of the three inputs the player holds and how many lie on the ground.
    std::pair<int, int> heldAndDropped()
    {
        int held = 0;
        int dropped = 0;
        for (const char *name : {"qtcraft:netherite_upgrade_smithing_template", "qtcraft:diamond_sword", "qtcraft:netherite_ingot"}) {
            held += player().inventory.countOf(item(name));
            dropped += droppedCount(item(name));
        }
        return {held, dropped};
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void theUpgradeKeepsWhatTheBaseCarries()
    {
        const QcItemStack made = QcSmithing::output(*m_content, inputs(treasuredSword()));
        QCOMPARE(made.item, item("qtcraft:netherite_sword"));
        QCOMPARE(made.count, 1);
        QCOMPARE(made.damage, 300);
        QCOMPARE(made.name(), u"Edge"_s);
        QCOMPARE(made.priorWork(), 2);
        QCOMPARE(made.enchantments().size(), 1);
        QCOMPARE(made.enchantments().first().name, u"qtcraft:sharpness"_s);

        // Without the template, with an iron sword or with two swords nothing comes of it.
        QcSmithing::Inputs missing = inputs(treasuredSword());
        missing[0] = {};
        QVERIFY(QcSmithing::output(*m_content, missing).isEmpty());
        QVERIFY(QcSmithing::output(*m_content, inputs(stackOf("qtcraft:iron_sword"))).isEmpty());
        QVERIFY(QcSmithing::output(*m_content, inputs(QcItemStack{item("qtcraft:diamond_sword"), 2, 0})).isEmpty());
    }

    void everyDiamondPieceHasItsUpgrade()
    {
        // Nine recipes, as in the role model: five tools and four pieces of armour.
        QCOMPARE(m_content->smithingRecipes().size(), 9);
        for (const QcSmithingRecipe &recipe : m_content->smithingRecipes()) {
            QVERIFY2(m_content->itemId(recipe.templateItem) && m_content->itemId(recipe.base) && m_content->itemId(recipe.addition)
                         && m_content->itemId(recipe.result),
                     qPrintable(recipe.name));
            const QcItemStack made = QcSmithing::output(*m_content, inputs(QcItemStack{*m_content->itemId(recipe.base), 1, 0}));
            QCOMPARE(made.item, *m_content->itemId(recipe.result));
        }
        QCOMPARE(QcSmithing::slotFor(*m_content, QcItemStack{item("qtcraft:netherite_upgrade_smithing_template"), 1, 0}),
                 std::optional(QcSmithing::Slot::Template));
        QCOMPARE(QcSmithing::slotFor(*m_content, stackOf("qtcraft:diamond_boots")), std::optional(QcSmithing::Slot::Base));
        QCOMPARE(QcSmithing::slotFor(*m_content, QcItemStack{item("qtcraft:netherite_ingot"), 1, 0}), std::optional(QcSmithing::Slot::Addition));
        QVERIFY(!QcSmithing::slotFor(*m_content, stackOf("qtcraft:iron_sword")));
    }

    void netheriteHasTheRoleModelsValues()
    {
        for (const char *name : {"qtcraft:netherite_sword", "qtcraft:netherite_pickaxe", "qtcraft:netherite_axe", "qtcraft:netherite_shovel",
                                 "qtcraft:netherite_hoe"}) {
            QCOMPARE(definition(name).durability, 2031);
            QCOMPARE(definition(name).enchantability, 15);
            QCOMPARE(definition(name).repairWith, u"qtcraft:netherite_ingot"_s);
        }
        QCOMPARE(definition("qtcraft:netherite_sword").attackDamage, definition("qtcraft:diamond_sword").attackDamage + 1.0);
        QCOMPARE(definition("qtcraft:netherite_pickaxe").toolLevel, definition("qtcraft:diamond_pickaxe").toolLevel + 1);
        QCOMPARE(definition("qtcraft:netherite_pickaxe").toolSpeed, 9.0);

        const QList<std::pair<const char *, int>> armour{{"qtcraft:netherite_helmet", 407},
                                                         {"qtcraft:netherite_chestplate", 592},
                                                         {"qtcraft:netherite_leggings", 555},
                                                         {"qtcraft:netherite_boots", 481}};
        for (const auto &[name, durability] : armour) {
            QCOMPARE(definition(name).durability, durability);
            QCOMPARE(definition(name).armour->toughness, 3.0);
            QCOMPARE(definition(name).armour->knockbackResistance, 0.1);
            QVERIFY(definition(name).isFireResistant());
        }
        QCOMPARE(definition("qtcraft:diamond_chestplate").armour->toughness, 2.0);
        QCOMPARE(definition("qtcraft:netherite_chestplate").armour->points, definition("qtcraft:diamond_chestplate").armour->points);

        // Ancient debris and the netherite block hold out like obsidian against explosions.
        const QcBlockDefinition &debris = m_content->block(id("qtcraft:ancient_debris"));
        QCOMPARE(debris.hardness, 30.0);
        QCOMPARE(debris.blast.resistance, 1200.0);
        QCOMPARE(debris.toolLevel, definition("qtcraft:diamond_pickaxe").toolLevel);
        QCOMPARE(m_content->block(id("qtcraft:netherite_block")).hardness, 50.0);
        for (const char *name : {"qtcraft:ancient_debris", "qtcraft:netherite_block", "qtcraft:netherite_scrap", "qtcraft:netherite_ingot"}) {
            QVERIFY2(definition(name).isFireResistant(), name);
        }
        QVERIFY(m_content->block(id("qtcraft:smithing_table")).opensSmithing);
    }

    void theRecipesAroundNetherite()
    {
        // Four scrap and four gold make an ingot; debris smelts into scrap.
        const QcItemStack scrap{item("qtcraft:netherite_scrap"), 1, 0};
        const QcItemStack gold{item("qtcraft:gold_ingot"), 1, 0};
        const QList<QcItemStack> ingot{scrap, scrap, scrap, scrap, gold, gold, gold, gold, {}};
        QCOMPARE(crafted(ingot), (std::pair{u"qtcraft:netherite_ingot"_s, 1}));
        const QcSmeltingRecipe *smelting = m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:ancient_debris"_s);
        QVERIFY(smelting);
        QCOMPARE(smelting->resultItem, u"qtcraft:netherite_scrap"_s);

        // Seven diamonds, a template and netherrack make two templates.
        const QcItemStack diamond{item("qtcraft:diamond"), 1, 0};
        const QList<QcItemStack> copy{diamond, {item("qtcraft:netherite_upgrade_smithing_template"), 1, 0}, diamond,
                                      diamond, {item("qtcraft:netherrack"), 1, 0},                     diamond,
                                      diamond, diamond,                                                diamond};
        QCOMPARE(crafted(copy), (std::pair{u"qtcraft:netherite_upgrade_smithing_template"_s, 2}));
    }

    void theScreenUpgradesThroughTheProtocol()
    {
        join();
        const QcBlockPos table = openTable();
        QCOMPARE(m_client->workstations.smithing, std::optional(table));
        putIntoTable(QcItemStack{item("qtcraft:netherite_upgrade_smithing_template"), 2, 0}, templateSlot);
        putIntoTable(treasuredSword(), baseSlot);
        putIntoTable(QcItemStack{item("qtcraft:netherite_ingot"), 3, 0}, additionSlot);
        QCOMPARE(smithingState().output.item, item("qtcraft:netherite_sword"));

        // An output the client did not see takes nothing.
        send(QcMsg::ForgeSmithing{smithingState().output.withName(u"Other"_s)});
        QCOMPARE(smithingState().base.item, item("qtcraft:diamond_sword"));
        QCOMPARE(smithingState().templateStack.count, 2);

        send(QcMsg::ForgeSmithing{smithingState().output});
        QCOMPARE(smithingState().base.item, item("qtcraft:netherite_sword"));
        QCOMPARE(smithingState().base.damage, 300);
        QCOMPARE(smithingState().base.name(), u"Edge"_s);
        QCOMPARE(smithingState().templateStack.count, 1);
        QCOMPARE(smithingState().addition.count, 2);
        // Without a second diamond piece nothing more comes of the rest.
        QVERIFY(smithingState().output.isEmpty());

        // Closing gives everything back.
        send(QcMsg::CloseSmithing{});
        ticks(1);
        QCOMPARE(player().inventory.countOf(item("qtcraft:netherite_sword")), 1);
        QCOMPARE(player().inventory.countOf(item("qtcraft:netherite_upgrade_smithing_template")), 1);
        QCOMPARE(player().inventory.countOf(item("qtcraft:netherite_ingot")), 2);
    }

    void quickMovesSortIntoTheThreeSlots()
    {
        join();
        openTable();
        player().inventory.setSlot(20, QcItemStack{item("qtcraft:netherite_ingot"), 1, 0});
        player().inventory.setSlot(21, stackOf("qtcraft:diamond_helmet"));
        player().inventory.setSlot(22, QcItemStack{item("qtcraft:netherite_upgrade_smithing_template"), 1, 0});
        player().inventory.setSlot(23, stackOf("qtcraft:iron_sword"));
        for (int slot = 20; slot <= 23; ++slot) {
            send(QcMsg::QuickMoveItem{slot});
        }
        ticks(1);
        QCOMPARE(smithingState().templateStack.item, item("qtcraft:netherite_upgrade_smithing_template"));
        QCOMPARE(smithingState().base.item, item("qtcraft:diamond_helmet"));
        QCOMPARE(smithingState().addition.item, item("qtcraft:netherite_ingot"));
        QCOMPARE(smithingState().output.item, item("qtcraft:netherite_helmet"));
        // An item no recipe takes stays where it was.
        QCOMPARE(player().inventory.slot(23).item, item("qtcraft:iron_sword"));
    }

    void toughnessHoldsAgainstHardHits()
    {
        // The role model's formula: 20 points let 4 of a 10-point hit through without toughness, a
        // full netherite set (toughness 12) 2.8; small hits barely notice it.
        QCOMPARE(QcArmour::reducedDamage(10.0, 20, 0.0), 4.0);
        QVERIFY(qAbs(QcArmour::reducedDamage(10.0, 20, 12.0) - 2.8) < 1e-9);
        QVERIFY(QcArmour::reducedDamage(10.0, 20, 12.0) < QcArmour::reducedDamage(10.0, 20, 8.0));
        QVERIFY(qAbs(QcArmour::reducedDamage(1.0, 20, 0.0) - 0.22) < 1e-9);
        // At least a fifth of the points always counts.
        QVERIFY(qAbs(QcArmour::reducedDamage(100.0, 20, 0.0) - 84.0) < 1e-9);

        QcEquipment netherite;
        netherite.stacks = {stackOf("qtcraft:netherite_helmet"), stackOf("qtcraft:netherite_chestplate"), stackOf("qtcraft:netherite_leggings"),
                            stackOf("qtcraft:netherite_boots")};
        QCOMPARE(QcArmour::toughness(*m_content, netherite), 12.0);
        QVERIFY(qAbs(QcArmour::knockbackResistance(*m_content, netherite) - 0.4) < 1e-9);
    }

    void knockbackResistanceHoldsBackThePush()
    {
        join();
        ticks(2);
        // A blow from the west: a full netherite set leaves 0.6 of the bare push.
        QcPlayerHealth &health = m_server->systems().health;
        QCOMPARE(health.damage(player(), 2.0, {1.0, 0.0, 0.0}, QcPlayerHealth::DamageKind::Blow), QcHurtWindow::Take::Fresh);
        const double bareBlow = player().motion.velocity.x;
        QCOMPARE(bareBlow, QcMobManager::knockbackSpeed);
        wearNetherite();
        player().hurt.clear();
        player().motion.velocity = {};
        QCOMPARE(health.damage(player(), 2.0, {1.0, 0.0, 0.0}, QcPlayerHealth::DamageKind::Blow), QcHurtWindow::Take::Fresh);
        QVERIFY2(qAbs(player().motion.velocity.x / bareBlow - 0.6) < 1e-9, qPrintable(QString::number(player().motion.velocity.x)));

        // The same for an arrow.
        player().equipment = {};
        const double bareArrow = arrowPush();
        QVERIFY2(bareArrow > 0.0, qPrintable(QString::number(bareArrow)));
        wearNetherite();
        const double heldArrow = arrowPush();
        QVERIFY2(qAbs(heldArrow / bareArrow - 0.6) < 0.01, qPrintable(QString::number(heldArrow / bareArrow)));
    }

    void deathDropsTheSlots()
    {
        QcServerConfig config;
        config.keepInventory = false;
        join(config);
        openTable();
        fillTable();
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(!m_client->workstations.smithing.has_value());
        QVERIFY(!player().screen.smithing.has_value());
        // Each input lies on the ground once.
        QCOMPARE(heldAndDropped(), (std::pair{0, 3}));
    }

    void disconnectKeepsTheSlots()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        openTable();
        fillTable();
        // A save while the screen is open stores the slots in the inventory.
        const QcPlayerData saved = QcPlayerRecords::of(player(), *m_content).player;
        QCOMPARE(saved.inventory.countOf(item("qtcraft:netherite_upgrade_smithing_template")), 1);
        QCOMPARE(saved.inventory.countOf(item("qtcraft:diamond_sword")), 1);
        QCOMPARE(saved.inventory.countOf(item("qtcraft:netherite_ingot")), 1);

        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);
        rejoin();
        QVERIFY(!player().screen.smithing.has_value());
        QCOMPARE(heldAndDropped(), (std::pair{3, 0}));
    }

    void aBrokenTableGivesTheSlotsBack()
    {
        join();
        const QcBlockPos table = openTable();
        fillTable();
        m_server->world().setBlock(table, qcAirBlockId);
        ticks(2);
        QVERIFY(!player().screen.smithing.has_value());
        QVERIFY(!m_client->workstations.smithing.has_value());
        QCOMPARE(heldAndDropped(), (std::pair{3, 0}));
    }

    void netheriteFloatsInLavaWhereOthersBurn()
    {
        join();
        // A pool of lava three deep, walled in, beside the player.
        const QcBlockPos pool = m_feet.offset(6, 0, 0);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dy = -1; dy <= 3; ++dy) {
                    const bool inside = dx == 0 && dz == 0 && dy >= 0 && dy <= 2;
                    putAt(pool.offset(dx, dy, dz), inside ? id("qtcraft:lava") : id("qtcraft:stone"));
                }
            }
        }
        putAt(pool.offset(0, 3, 0), qcAirBlockId);
        const QcVec3 bottom{pool.x + 0.5, pool.y + 0.1, pool.z + 0.5};
        m_server->items().spawn({.stack = QcItemStack{item("qtcraft:netherite_ingot"), 1, 0}, .position = bottom, .velocity = {}, .pickupDelay = 1000.0},
                                m_server->entities().newId());
        m_server->items().spawn({.stack = QcItemStack{item("qtcraft:gold_ingot"), 1, 0}, .position = bottom, .velocity = {}, .pickupDelay = 1000.0},
                                m_server->entities().newId());
        ticks(60);

        QCOMPARE(droppedCount(item("qtcraft:gold_ingot")), 0);
        QCOMPARE(droppedCount(item("qtcraft:netherite_ingot")), 1);
        QVERIFY(m_server->items().items().first().motion.position.y > bottom.y + 1.0);
    }
};

QTEST_GUILESS_MAIN(TestSmithing)
#include "test_smithing.moc"
