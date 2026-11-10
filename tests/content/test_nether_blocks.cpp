// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The nether blocks of the base content (package 84.1, decision E-981): building parts of the new
// materials, blackstone in place of cobblestone for stone tools and the furnace, the soul blocks
// under soul fire, nether wart on soul sand, and the recipes, smelting and stonecutting that make
// the families.

#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcCrafting.h"

#include <QTest>

using namespace Qt::StringLiterals;

class TestNetherBlocks : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<QcContentRegistry> m_content;

    const QcBlockDefinition &block(const QString &name) const
    {
        const std::optional<QcBlockId> id = m_content->blockId(u"qtcraft:"_s + name);
        if (!id) {
            QTest::qFail(qPrintable(u"no block "_s + name), __FILE__, __LINE__);
            static const QcBlockDefinition none;
            return none;
        }
        return m_content->block(*id);
    }

    QcItemId item(const QString &name) const { return m_content->itemId(u"qtcraft:"_s + name).value_or(qcNoItem); }

    // The result and count of the recipe that a 3 x 3 grid of `rows` (characters for `key`) matches;
    // empty when none.
    std::pair<QString, int> crafted(const QStringList &rows, const QHash<QChar, QString> &key) const
    {
        QList<QcItemStack> grid(9);
        for (qsizetype row = 0; row < rows.size(); ++row) {
            for (qsizetype column = 0; column < rows[row].size(); ++column) {
                const QChar cell = rows[row][column];
                if (cell != u' ') {
                    grid[row * 3 + column] = {item(key.value(cell)), 1, 0};
                }
            }
        }
        const std::optional<qsizetype> index = QcCrafting::match(*m_content, grid, 3);
        if (!index) {
            return {};
        }
        return {m_content->recipes()[*index].resultItem, m_content->recipes()[*index].resultCount};
    }

private slots:
    void initTestCase()
    {
        const QcResult<QcBaseContent::Loaded> loaded = QcBaseContent::load();
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->warnings, QStringList{});
        m_content = loaded->registry;
    }

    void familiesHaveTheirParts_data()
    {
        QTest::addColumn<QString>("part");
        for (const char *name : {"quartz_slab", "quartz_stairs", "smooth_quartz_slab", "smooth_quartz_stairs",
                                 "nether_brick_slab", "nether_brick_stairs", "nether_brick_wall",
                                 "red_nether_brick_slab", "red_nether_brick_stairs", "red_nether_brick_wall",
                                 "blackstone_slab", "blackstone_stairs", "blackstone_wall", "polished_blackstone_slab",
                                 "polished_blackstone_stairs", "polished_blackstone_wall",
                                 "polished_blackstone_brick_slab", "polished_blackstone_brick_stairs",
                                 "polished_blackstone_brick_wall"}) {
            QTest::newRow(name) << QString::fromLatin1(name);
        }
    }

    void familiesHaveTheirParts()
    {
        QFETCH(QString, part);
        QVERIFY2(m_content->blockId(u"qtcraft:"_s + part).has_value(), qPrintable(part));
        QVERIFY2(item(part) != qcNoItem, qPrintable(part));
    }

    void quartzHasNoWall()
    {
        QVERIFY(!m_content->blockId(u"qtcraft:quartz_wall"_s).has_value());
        QVERIFY(!m_content->blockId(u"qtcraft:smooth_quartz_wall"_s).has_value());
    }

    // Blackstone counts as cobblestone for stone tools and the furnace, as in the reference.
    void blackstoneMakesStoneToolsAndTheFurnace()
    {
        for (const QString &stone : {u"cobblestone"_s, u"blackstone"_s}) {
            const QHash<QChar, QString> key{{u'#', stone}, {u'|', u"stick"_s}};
            QCOMPARE(crafted({u"###"_s, u" | "_s, u" | "_s}, key), (std::pair{u"qtcraft:stone_pickaxe"_s, 1}));
            QCOMPARE(crafted({u"#"_s, u"#"_s, u"|"_s}, key), (std::pair{u"qtcraft:stone_sword"_s, 1}));
            QCOMPARE(crafted({u"###"_s, u"# #"_s, u"###"_s}, key), (std::pair{u"qtcraft:furnace"_s, 1}));
        }
        QCOMPARE(crafted({u"###"_s, u" | "_s, u" | "_s}, {{u'#', u"polished_blackstone"_s}, {u'|', u"stick"_s}}), (std::pair<QString, int>{}));
    }

    void brickRecipes()
    {
        QCOMPARE(crafted({u"bb"_s, u"bb"_s}, {{u'b', u"nether_brick"_s}}), (std::pair{u"qtcraft:nether_bricks"_s, 1}));
        QCOMPARE(crafted({u"wb"_s, u"bw"_s}, {{u'w', u"nether_wart"_s}, {u'b', u"nether_brick"_s}}),
                 (std::pair{u"qtcraft:red_nether_bricks"_s, 1}));
        QCOMPARE(crafted({u"#|#"_s, u"#|#"_s}, {{u'#', u"nether_bricks"_s}, {u'|', u"nether_brick"_s}}),
                 (std::pair{u"qtcraft:nether_brick_fence"_s, 6}));
        QCOMPARE(crafted({u"c"_s, u"|"_s, u"s"_s}, {{u'c', u"coal"_s}, {u'|', u"stick"_s}, {u's', u"soul_soil"_s}}),
                 (std::pair{u"qtcraft:soul_torch"_s, 4}));
        QCOMPARE(crafted({u"gg"_s, u"gg"_s}, {{u'g', u"glowstone_dust"_s}}), (std::pair{u"qtcraft:glowstone"_s, 1}));
    }

    // Gates are a group of their own that every fence joins; wooden and nether brick fences do not
    // join each other.
    void fencesJoinGatesButNotOtherFences()
    {
        const QcBlockDefinition &wood = block(u"oak_fence"_s);
        const QcBlockDefinition &nether = block(u"nether_brick_fence"_s);
        const QcBlockDefinition &gate = block(u"oak_fence_gate"_s);
        QCOMPARE(gate.connectGroup, u"fence_gate"_s);
        QVERIFY(gate.connectsTo.isEmpty());
        QVERIFY(wood.connectsTo.contains(wood.connectGroup) && wood.connectsTo.contains(gate.connectGroup));
        QVERIFY(nether.connectsTo.contains(nether.connectGroup) && nether.connectsTo.contains(gate.connectGroup));
        QVERIFY(!wood.connectsTo.contains(nether.connectGroup));
        QVERIFY(!nether.connectsTo.contains(wood.connectGroup));
    }

    void dropsAndExperience_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("drop");
        QTest::addColumn<int>("fewest");
        QTest::addColumn<int>("most");
        QTest::addColumn<int>("leastExperience");
        QTest::addColumn<int>("mostExperience");
        QTest::newRow("glowstone") << u"glowstone"_s << u"qtcraft:glowstone_dust"_s << 2 << 4 << 0 << 0;
        QTest::newRow("quartz ore") << u"nether_quartz_ore"_s << u"qtcraft:quartz"_s << 1 << 1 << 2 << 5;
        QTest::newRow("gold ore") << u"nether_gold_ore"_s << u"qtcraft:gold_nugget"_s << 2 << 6 << 0 << 1;
    }

    void dropsAndExperience()
    {
        QFETCH(QString, name);
        QFETCH(QString, drop);
        QFETCH(int, fewest);
        QFETCH(int, most);
        QFETCH(int, leastExperience);
        QFETCH(int, mostExperience);
        const QcBlockDefinition &definition = block(name);
        QCOMPARE(definition.drops.size(), 1);
        QCOMPARE(definition.drops.first().item, drop);
        QCOMPARE(definition.drops.first().count, fewest);
        QCOMPARE(definition.drops.first().mostCount(), most);
        QCOMPARE(definition.experience.min, leastExperience);
        QCOMPARE(definition.experience.max, mostExperience);
    }

    void smeltingAndStonecutting()
    {
        QCOMPARE(m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:netherrack"_s)->resultItem, u"qtcraft:nether_brick"_s);
        QCOMPARE(m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:basalt"_s)->resultItem, u"qtcraft:smooth_basalt"_s);
        QCOMPARE(m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:nether_gold_ore"_s)->resultItem, u"qtcraft:gold_ingot"_s);
        const auto cuts = [this](const QString &input, const QString &result) {
            return std::ranges::any_of(m_content->stonecuttingRecipes(), [&](const QcSmeltingRecipe &recipe) {
                return recipe.inputItem == input && recipe.resultItem == result;
            });
        };
        QVERIFY(cuts(u"qtcraft:blackstone"_s, u"qtcraft:polished_blackstone_bricks"_s));
        QVERIFY(cuts(u"qtcraft:quartz_block"_s, u"qtcraft:quartz_pillar"_s));
    }

    // Fire on the soul blocks is soul fire and burns forever; netherrack keeps plain fire forever.
    void soulBlocksAndFire()
    {
        for (const QString &soul : {u"soul_sand"_s, u"soul_soil"_s}) {
            QCOMPARE(block(soul).fire.fireAbove, u"qtcraft:soul_fire"_s);
            QVERIFY(block(soul).fire.eternal);
        }
        QVERIFY(block(u"netherrack"_s).fire.eternal);
        QVERIFY(block(u"netherrack"_s).fire.fireAbove.isEmpty());
        QVERIFY(block(u"soul_fire"_s).fire.isFire);
        QCOMPARE(block(u"soul_fire"_s).lightLevel, 10);
        QCOMPARE(block(u"soul_sand"_s).surfaceSpeed, 0.4);
        QCOMPARE(block(u"soul_soil"_s).surfaceSpeed, 1.0);
        // The game's own fire stays the one sparks light.
        QCOMPARE(m_content->defaultFire(), m_content->blockId(u"qtcraft:fire"_s));
    }

    void netherWartIsAPlantOfSoulSand()
    {
        const QcBlockDefinition &wart = block(u"nether_wart_crop"_s);
        QCOMPARE(wart.groundBlocks, QStringList{u"qtcraft:soul_sand"_s});
        QCOMPARE(wart.crop.minLight, 0);
        QVERIFY(!wart.crop.fertilizable);
        QVERIFY(!wart.crop.tendedByFarmers);
        QVERIFY(block(u"wheat_crop"_s).crop.fertilizable);
        QVERIFY(block(u"wheat_crop"_s).crop.tendedByFarmers);
        QCOMPARE(m_content->item(item(u"nether_wart"_s)).placesBlock, u"qtcraft:nether_wart_crop"_s);
    }

    void lightSources()
    {
        QCOMPARE(block(u"glowstone"_s).lightLevel, 15);
        QCOMPARE(block(u"soul_torch"_s).lightLevel, 10);
        QCOMPARE(block(u"soul_lantern"_s).lightLevel, 10);
        QCOMPARE(block(u"soul_lantern"_s).hardness, block(u"lantern"_s).hardness);
    }
};

QTEST_GUILESS_MAIN(TestNetherBlocks)
#include "test_nether_blocks.moc"
