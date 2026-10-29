// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcMining.h"
#include "QcServerTestFixture.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QMap>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

// Ores and raw metal (stage 60, decisions E-740 to E-744): iron, gold and copper ore drop raw
// metal, raw metal smelts into ingots (the ores no longer do), every metal crafts ingot ↔ block ↔
// nuggets and raw metal ↔ raw block, chains run along the clicked face and hold lanterns, copper
// ore lies between its heights in the generated world, loot and trades name the new items.
class TestOresMetals : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcItemId baseItem(const char *name) const { return item(qPrintable(u"qtcraft:"_s + QString::fromLatin1(name))); }

    // Gives `count` of the item and selects its first hotbar slot.
    void hold(const char *name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(QString::fromLatin1(name)).arg(count));
        const int slot = slotOf(baseItem(name));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Mines the block at `pos` with the held tool, waiting as long as mining it takes.
    void mine(QcBlockPos pos)
    {
        const QcBlockDefinition &block = m_content->block(*m_server->world().blockAt(pos));
        const QcItemStack &held = m_client->stacks[m_client->selected];
        const QcItemDefinition *tool = held.isEmpty() ? nullptr : &m_content->item(held.item);
        send(QcMsg::StartMining{pos});
        ticks(static_cast<int>(std::ceil(QcMining::breakSeconds(block, tool) * QcPhysics::tickRate)) + 1);
        send(QcMsg::FinishMining{pos});
        QCOMPARE(*m_server->world().blockAt(pos), qcAirBlockId);
    }

    // How many of `drop` mining `ore` next to the player gave, each time of `times`.
    QMap<int, int> dropsOf(const char *ore, const char *drop, int times)
    {
        const QcItemId wanted = baseItem(drop);
        const QcBlockPos pos = m_feet.offset(1, 0, 0);
        QMap<int, int> counts;
        for (int i = 0; i < times; ++i) {
            const int before = countOf(wanted) + droppedCount(wanted);
            m_server->world().setBlock(pos, id(qPrintable(u"qtcraft:"_s + QString::fromLatin1(ore))));
            mine(pos);
            ticks(1);
            ++counts[countOf(wanted) + droppedCount(wanted) - before];
        }
        return counts;
    }

    // Result and count of a crafting grid of `size` x `size` with the named items in the listed
    // cells; an empty name and 0 when no recipe matches.
    std::pair<QString, int> crafted(const QList<std::pair<int, const char *>> &cells, int size) const
    {
        QList<QcItemStack> grid(static_cast<qsizetype>(size) * size);
        for (const auto &[cell, name] : cells) {
            grid[cell] = {baseItem(name), 1, 0};
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
        if (!recipe) {
            return {};
        }
        return {m_content->recipes()[*recipe].resultItem, m_content->recipes()[*recipe].resultCount};
    }

    static QList<std::pair<int, const char *>> filled(const char *name, int cells)
    {
        QList<std::pair<int, const char *>> result;
        for (int cell = 0; cell < cells; ++cell) {
            result.append({cell, name});
        }
        return result;
    }

    QString property(QcBlockPos pos, const QString &name) const
    {
        return m_content->property(*m_server->world().blockAt(pos), name);
    }

    // Heights of `ore` in the generated chunks around the origin (`radius` chunks to each side).
    QMap<int, int> heightsOf(const char *ore, int radius) const
    {
        const QcTerrainGenerator generator(m_content, 60);
        const QcBlockId wanted = id(ore);
        QMap<int, int> heights;
        for (int cx = -radius; cx <= radius; ++cx) {
            for (int cz = -radius; cz <= radius; ++cz) {
                const auto chunk = generator.generate({cx, cz});
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    for (int z = 0; z < QcWorld::chunkSize; ++z) {
                        for (int y = 0; y < QcWorld::worldHeight; ++y) {
                            if (chunk->block(x, y, z) == wanted) {
                                ++heights[y];
                            }
                        }
                    }
                }
            }
        }
        return heights;
    }

    static int sum(const QMap<int, int> &counts, int from, int to)
    {
        int total = 0;
        for (auto it = counts.lowerBound(from); it != counts.end() && it.key() <= to; ++it) {
            total += it.value();
        }
        return total;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void oresDropRawMetal()
    {
        join();
        hold("iron_pickaxe");
        QCOMPARE(dropsOf("iron_ore", "raw_iron", 3), (QMap<int, int>{{1, 3}}));
        QCOMPARE(dropsOf("gold_ore", "raw_gold", 3), (QMap<int, int>{{1, 3}}));

        // Copper: two to five pieces, with a stone pickaxe already.
        hold("stone_pickaxe");
        const QMap<int, int> copper = dropsOf("copper_ore", "raw_copper", 40);
        QCOMPARE(copper.firstKey(), 2);
        QCOMPARE(copper.lastKey(), 5);

        // The ore itself never drops (no silk touch yet).
        QCOMPARE(countOf(baseItem("copper_ore")) + droppedCount(baseItem("copper_ore")), 0);
        QCOMPARE(countOf(baseItem("iron_ore")) + droppedCount(baseItem("iron_ore")), 0);
    }

    void rawMetalSmeltsAndOresDoNot()
    {
        for (const auto &[input, result] : {std::pair{"raw_iron", "iron_ingot"}, std::pair{"raw_gold", "gold_ingot"}, std::pair{"raw_copper", "copper_ingot"}}) {
            const QcSmeltingRecipe *recipe = m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:"_s + QString::fromLatin1(input));
            QVERIFY2(recipe, input);
            QCOMPARE(recipe->resultItem, u"qtcraft:"_s + QString::fromLatin1(result));
            QCOMPARE(recipe->seconds, 10.0);
        }

        for (const char *name : {"iron_ore", "gold_ore", "copper_ore", "raw_iron_block", "raw_gold_block", "raw_copper_block"}) {
            QVERIFY2(!m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:"_s + QString::fromLatin1(name)), name);
        }
    }

    void everyMetalCraftsBothWays()
    {
        for (const char *metal : {"iron", "gold", "copper"}) {
            const QString name = QString::fromLatin1(metal);
            const QByteArray raw = "raw_" + QByteArray(metal);
            const QByteArray rawBlock = raw + "_block";
            const QByteArray ingot = QByteArray(metal) + "_ingot";
            const QByteArray block = QByteArray(metal) + "_block";
            const QByteArray nugget = QByteArray(metal) + "_nugget";

            QCOMPARE(crafted(filled(raw.constData(), 9), 3), (std::pair{u"qtcraft:raw_"_s + name + u"_block"_s, 1}));
            QCOMPARE(crafted({{0, rawBlock.constData()}}, 2), (std::pair{u"qtcraft:raw_"_s + name, 9}));
            QCOMPARE(crafted(filled(ingot.constData(), 9), 3), (std::pair{u"qtcraft:"_s + name + u"_block"_s, 1}));
            QCOMPARE(crafted({{0, block.constData()}}, 2), (std::pair{u"qtcraft:"_s + name + u"_ingot"_s, 9}));
            QCOMPARE(crafted({{0, ingot.constData()}}, 2), (std::pair{u"qtcraft:"_s + name + u"_nugget"_s, 9}));
            QCOMPARE(crafted(filled(nugget.constData(), 9), 3), (std::pair{u"qtcraft:"_s + name + u"_ingot"_s, 1}));
        }
    }

    void chainsLanternsAndCopperParts()
    {
        // Nugget, ingot, nugget from top to bottom.
        QCOMPARE(crafted({{1, "iron_nugget"}, {4, "iron_ingot"}, {7, "iron_nugget"}}, 3), (std::pair{u"qtcraft:chain"_s, 1}));
        QCOMPARE(crafted({{1, "copper_nugget"}, {4, "copper_ingot"}, {7, "copper_nugget"}}, 3), (std::pair{u"qtcraft:copper_chain"_s, 1}));

        // Eight nuggets around a torch.
        QList<std::pair<int, const char *>> lantern = filled("copper_nugget", 9);
        lantern[4].second = "torch";
        QCOMPARE(crafted(lantern, 3), (std::pair{u"qtcraft:copper_lantern"_s, 1}));
        lantern = filled("iron_nugget", 9);
        lantern[4].second = "torch";
        QCOMPARE(crafted(lantern, 3), (std::pair{u"qtcraft:lantern"_s, 1}));

        const QList<std::pair<int, const char *>> door{{0, "copper_ingot"}, {1, "copper_ingot"}, {3, "copper_ingot"},
                                                       {4, "copper_ingot"}, {6, "copper_ingot"}, {7, "copper_ingot"}};
        QCOMPARE(crafted(door, 3), (std::pair{u"qtcraft:copper_door"_s, 3}));
        QCOMPARE(crafted(filled("copper_ingot", 4), 2), (std::pair{u"qtcraft:copper_trapdoor"_s, 1}));
        QCOMPARE(crafted(filled("copper_ingot", 6), 3), (std::pair{u"qtcraft:copper_bars"_s, 16}));

        // Copper doors open by hand and by a signal; iron doors only by a signal.
        const QcBlockDefinition &copperDoor = m_content->block(id("qtcraft:copper_door"));
        QVERIFY(copperDoor.toggles);
        QVERIFY(!m_content->block(id("qtcraft:iron_door")).toggles);
        QCOMPARE(m_content->block(id("qtcraft:copper_lantern")).lightLevel, 15);
    }

    void chainsRunAlongTheClickedFaceAndHoldLanterns()
    {
        join();
        const QcBlockPos base = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(base, id("qtcraft:stone"));

        // On top of a block a chain stands upright, on its east side it lies east to west.
        hold("chain", 4);
        send(QcMsg::UseItem{base, QcBlockFace::Up});
        QCOMPARE(property(base.offset(0, 1, 0), u"axis"_s), u"y"_s);
        send(QcMsg::UseItem{base, QcBlockFace::East});
        QCOMPARE(property(base.offset(1, 0, 0), u"axis"_s), u"x"_s);
        send(QcMsg::UseItem{base, QcBlockFace::South});
        QCOMPARE(property(base.offset(0, 0, 1), u"axis"_s), u"z"_s);

        // A lantern stands on the upright chain's end.
        hold("lantern", 4);
        send(QcMsg::UseItem{base.offset(0, 1, 0), QcBlockFace::Up});
        const QcBlockPos standing = base.offset(0, 2, 0);
        QCOMPARE(m_content->block(*m_server->world().blockAt(standing)).name, u"qtcraft:lantern"_s);
        QCOMPARE(property(standing, u"attach"_s), u"floor"_s);

        // A chain hangs without support; a lantern hangs from its lower end and falls with it.
        const QcBlockPos top = m_feet.offset(-2, 3, 0);
        m_server->world().setBlock(top, id("qtcraft:copper_chain"));
        hold("copper_lantern", 4);
        send(QcMsg::UseItem{top, QcBlockFace::Down});
        const QcBlockPos hanging = top.offset(0, -1, 0);
        QCOMPARE(property(hanging, u"attach"_s), u"ceiling"_s);
        ticks(2);
        QCOMPARE(m_content->block(*m_server->world().blockAt(hanging)).name, u"qtcraft:copper_lantern"_s);
        hold("iron_pickaxe");
        mine(top);
        ticks(2);
        QCOMPARE(*m_server->world().blockAt(hanging), qcAirBlockId);

        // A lying chain does not hold a lantern under it: its axis does not run downwards.
        const QcBlockPos lying = m_feet.offset(-2, 3, 2);
        m_server->world().setBlock(lying, m_content->withProperty(id("qtcraft:chain"), u"axis"_s, u"x"_s));
        hold("lantern", 4);
        send(QcMsg::UseItem{lying, QcBlockFace::Down});
        QCOMPARE(*m_server->world().blockAt(lying.offset(0, -1, 0)), qcAirBlockId);
    }

    void copperOreLiesBetweenItsHeights()
    {
        // 25 chunks: copper only between heights 16 and 96, spread over them, about as much as iron.
        const QMap<int, int> copper = heightsOf("qtcraft:copper_ore", 2);
        QVERIFY(!copper.isEmpty());
        QVERIFY2(copper.firstKey() >= 16 && copper.lastKey() <= 96, qPrintable(u"%1..%2"_s.arg(copper.firstKey()).arg(copper.lastKey())));
        const int total = sum(copper, 0, 400);
        qInfo("copper ore in 25 chunks: %d (16-40: %d, 41-64: %d, 65-96: %d)", total, sum(copper, 16, 40), sum(copper, 41, 64),
              sum(copper, 65, 96));
        QVERIFY2(total >= 25 * 30, qPrintable(QString::number(total)));
        QVERIFY(sum(copper, 16, 40) > total / 5);
        QVERIFY(sum(copper, 41, 64) > total / 5);

        const QMap<int, int> iron = heightsOf("qtcraft:iron_ore", 2);
        QVERIFY(iron.firstKey() >= 5 && iron.lastKey() <= 72);
        QVERIFY2(total > sum(iron, 0, 400) / 2, qPrintable(u"copper %1, iron %2"_s.arg(total).arg(sum(iron, 0, 400))));
    }

    void lootAndTradesNameTheNewItems()
    {
        const auto entriesOf = [this](const char *table) {
            QStringList items;
            const QcLootTableDefinition *found = m_content->lootTables().find(u"qtcraft:"_s + QString::fromLatin1(table));
            if (!found) {
                found = m_content->lootTables().find(QString::fromLatin1(table));
            }
            for (const QcLootTableDefinition::Pool &pool : found ? found->pools : QList<QcLootTableDefinition::Pool>{}) {
                for (const QcLootTableDefinition::Entry &entry : pool.entries) {
                    items.append(entry.item.section(u':', -1));
                }
            }
            return items;
        };
        QVERIFY(entriesOf("mineshaft_cart").contains(u"raw_copper"_s));
        QVERIFY(entriesOf("mineshaft_cart").contains(u"raw_iron"_s));
        QVERIFY(entriesOf("dungeon_chest").contains(u"copper_ingot"_s));
        QVERIFY(entriesOf("coastal_tower").contains(u"copper_ingot"_s));

        QStringList offers;
        for (const QcTradeSet &set : m_content->villageCatalog().tradeSets()) {
            for (const QcTradeOffer &offer : set.offers) {
                offers.append(offer.id);
            }
        }
        QVERIFY2(offers.contains(u"qtcraft:copper_ingot>qtcraft:emerald"_s), qPrintable(offers.join(u", "_s)));
        QVERIFY2(offers.contains(u"qtcraft:emerald>qtcraft:chain"_s), qPrintable(offers.join(u", "_s)));
    }
};

QTEST_GUILESS_MAIN(TestOresMetals)
#include "test_ores_metals.moc"
