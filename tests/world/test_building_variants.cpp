#include "common/gameplay/QcCrafting.h"
#include "QcCircuitFixture.h"

#include <QTest>

using namespace Qt::StringLiterals;

// Building variants and lying logs (decisions E-750 to E-753): the variant blocks craft and
// smelt as listed, logs, wood, stripped logs and bone blocks lie along the clicked face with their
// textures turned, an axe strips logs keeping their axis, a block of Signal Dust powers what lies
// next to it without conducting, jack o'lanterns shine, coarse dirt tills into dirt, beds come in
// sixteen colours and every wood has its own buttons and pressure plates.
class TestBuildingVariants : public QcCircuitFixture
{
    Q_OBJECT

private:
    QcItemId baseItem(const QString &name) const { return item(qPrintable(u"qtcraft:"_s + name)); }

    void hold(const QString &name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(name).arg(count));
        const int slot = slotOf(baseItem(name));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Result and count of a crafting grid of `size` x `size` with the named items in the listed
    // cells; an empty name and 0 when no recipe matches.
    std::pair<QString, int> crafted(const QList<std::pair<int, QString>> &cells, int size) const
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

    static QList<std::pair<int, QString>> filled(const QString &name, int cells)
    {
        QList<std::pair<int, QString>> result;
        for (int cell = 0; cell < cells; ++cell) {
            result.append({cell, name});
        }
        return result;
    }

    QString smelted(const QString &input) const
    {
        const QcSmeltingRecipe *recipe = m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:"_s + input);
        return recipe ? recipe->resultItem : QString();
    }

    QString nameAt(QcBlockPos pos) const { return m_content->block(*m_server->world().blockAt(pos)).name; }
    QString propertyAt(QcBlockPos pos, const QString &name) const { return m_content->property(*m_server->world().blockAt(pos), name); }

    static QStringList colours()
    {
        return {u"white"_s, u"orange"_s, u"magenta"_s, u"light_blue"_s, u"yellow"_s, u"lime"_s, u"pink"_s, u"gray"_s,
                u"light_gray"_s, u"cyan"_s, u"purple"_s, u"blue"_s, u"brown"_s, u"green"_s, u"red"_s, u"black"_s};
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void bricksAndStoneCraftAndSmelt()
    {
        QCOMPARE(crafted({{0, u"stone_bricks"_s}, {1, u"vines"_s}}, 2), (std::pair{u"qtcraft:mossy_stone_bricks"_s, 1}));
        QCOMPARE(crafted({{0, u"cobblestone"_s}, {1, u"vines"_s}}, 2), (std::pair{u"qtcraft:mossy_cobblestone"_s, 1}));
        QCOMPARE(crafted({{0, u"stone_bricks_slab"_s}, {2, u"stone_bricks_slab"_s}}, 2), (std::pair{u"qtcraft:chiseled_stone_bricks"_s, 1}));
        QCOMPARE(smelted(u"stone_bricks"_s), u"qtcraft:cracked_stone_bricks"_s);
        // Stone smelts into polished stone, polished stone into smooth stone.
        QCOMPARE(smelted(u"stone"_s), u"qtcraft:polished_stone"_s);
        QCOMPARE(smelted(u"polished_stone"_s), u"qtcraft:smooth_stone"_s);

        // The variant materials have their parts, smooth stone and cut sandstone only slabs.
        for (const QString &part : {u"mossy_stone_bricks_slab"_s, u"mossy_stone_bricks_stairs"_s, u"mossy_stone_bricks_wall"_s,
                                    u"mossy_cobblestone_slab"_s, u"mossy_cobblestone_stairs"_s, u"mossy_cobblestone_wall"_s,
                                    u"smooth_stone_slab"_s, u"cut_sandstone_slab"_s}) {
            QVERIFY2(m_content->blockId(u"qtcraft:"_s + part), qPrintable(part));
        }
        for (const QString &missing : {u"smooth_stone_stairs"_s, u"smooth_stone_wall"_s, u"cut_sandstone_stairs"_s}) {
            QVERIFY2(!m_content->blockId(u"qtcraft:"_s + missing), qPrintable(missing));
        }
        QCOMPARE(crafted({{0, u"smooth_stone"_s}, {1, u"smooth_stone"_s}, {2, u"smooth_stone"_s}}, 3),
                 (std::pair{u"qtcraft:smooth_stone_slab"_s, 6}));
    }

    void glazedTerracottaSmeltsFromEveryColour()
    {
        const QStringList all = colours();
        for (const QString &colour : all) {
            const QString glazed = colour + u"_glazed_terracotta"_s;
            QCOMPARE(smelted(colour + u"_terracotta"_s), u"qtcraft:"_s + glazed);
            const QcBlockDefinition &block = m_content->block(id(qPrintable(u"qtcraft:"_s + glazed)));
            QCOMPARE(block.facingRule, QcFacingRule::Toward);
            QVERIFY(block.property(u"facing"_s));
        }

        // Turned, its top texture turns with it; the sides keep their texture upright.
        const QcBlockId south = m_content->withProperty(id("qtcraft:white_glazed_terracotta"), u"facing"_s, u"east"_s);
        QCOMPARE(int(m_content->state(south).faceUvTurns[std::size_t(QcBlockFace::Up)]), 1);
        QCOMPARE(int(m_content->state(south).faceUvTurns[std::size_t(QcBlockFace::Down)]), 3);
        QCOMPARE(int(m_content->state(south).faceUvTurns[std::size_t(QcBlockFace::North)]), 0);
    }

    void storageBlocksCraftBothWays()
    {
        QCOMPARE(crafted(filled(u"bone_meal"_s, 9), 3), (std::pair{u"qtcraft:bone_block"_s, 1}));
        QCOMPARE(crafted({{0, u"bone_block"_s}}, 2), (std::pair{u"qtcraft:bone_meal"_s, 9}));
        QCOMPARE(crafted(filled(u"signal_dust"_s, 9), 3), (std::pair{u"qtcraft:signal_dust_block"_s, 1}));
        QCOMPARE(crafted({{0, u"signal_dust_block"_s}}, 2), (std::pair{u"qtcraft:signal_dust"_s, 9}));
        QCOMPARE(crafted({{0, u"dirt"_s}, {1, u"gravel"_s}, {2, u"gravel"_s}, {3, u"dirt"_s}}, 2), (std::pair{u"qtcraft:coarse_dirt"_s, 4}));
    }

    void jackOLanternsShine()
    {
        QCOMPARE(crafted({{0, u"carved_pumpkin"_s}, {2, u"torch"_s}}, 2), (std::pair{u"qtcraft:jack_o_lantern"_s, 1}));
        QCOMPARE(crafted({{0, u"carved_pumpkin"_s}, {2, u"small_torch"_s}}, 2), (std::pair{u"qtcraft:small_jack_o_lantern"_s, 1}));
        QCOMPARE(m_content->block(id("qtcraft:jack_o_lantern")).lightLevel, 15);
        QCOMPARE(m_content->block(id("qtcraft:small_jack_o_lantern")).lightLevel, 8);
        QCOMPARE(m_content->block(id("qtcraft:carved_pumpkin")).lightLevel, 0);
    }

    void bedsComeInSixteenColours()
    {
        const QStringList all = colours();
        for (const QString &colour : all) {
            const QString bed = colour == u"red"_s ? u"bed"_s : colour + u"_bed"_s;
            const QcBlockDefinition &block = m_content->block(id(qPrintable(u"qtcraft:"_s + bed)));
            QVERIFY2(block.bed, qPrintable(bed));
            const QString wool = colour + u"_wool"_s;
            const QList<std::pair<int, QString>> grid{{0, wool}, {1, wool}, {2, wool}, {3, u"birch_planks"_s}, {4, u"oak_planks"_s}, {5, u"oak_planks"_s}};
            QCOMPARE(crafted(grid, 3), (std::pair{u"qtcraft:"_s + bed, 1}));
            // Any bed with a dye becomes a bed of that colour.
            QCOMPARE(crafted({{0, u"white_bed"_s}, {1, colour + u"_dye"_s}}, 2), (std::pair{u"qtcraft:"_s + bed, 1}));
        }
    }

    void everyWoodHasButtonsAndPlates()
    {
        for (const QString &wood : {u"oak"_s, u"birch"_s, u"spruce"_s}) {
            const QcBlockDefinition &button = m_content->block(id(qPrintable(u"qtcraft:"_s + wood + u"_button"_s)));
            const QcBlockDefinition &plate = m_content->block(id(qPrintable(u"qtcraft:"_s + wood + u"_pressure_plate"_s)));
            QCOMPARE(button.signal.kind, QcSignalKind::Button);
            QCOMPARE(button.signal.pulseTicks, 30);
            QCOMPARE(plate.signal.kind, QcSignalKind::Plate);
            QVERIFY(plate.signal.detectsItems);
            QCOMPARE(button.hardness, 0.5);
            QCOMPARE(crafted({{0, wood + u"_planks"_s}}, 2), (std::pair{u"qtcraft:"_s + wood + u"_button"_s, 1}));
            QCOMPARE(crafted({{0, wood + u"_planks"_s}, {1, wood + u"_planks"_s}}, 2), (std::pair{u"qtcraft:"_s + wood + u"_pressure_plate"_s, 1}));
        }
        QCOMPARE(m_content->block(id("qtcraft:birch_button")).texture(QcBlockFace::North), u"qtcraft:birch_planks"_s);
    }

    void logsLieAlongTheClickedFace()
    {
        join();
        const QcBlockPos base = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(base, id("qtcraft:stone"));
        for (const QString &block : {u"oak_log"_s, u"birch_wood"_s, u"stripped_spruce_log"_s, u"bone_block"_s}) {
            hold(block, 3);
            send(QcMsg::UseItem{base, QcBlockFace::Up});
            QCOMPARE(propertyAt(base.offset(0, 1, 0), u"axis"_s), u"y"_s);
            send(QcMsg::UseItem{base, QcBlockFace::East});
            QCOMPARE(propertyAt(base.offset(1, 0, 0), u"axis"_s), u"x"_s);
            send(QcMsg::UseItem{base, QcBlockFace::North});
            QCOMPARE(propertyAt(base.offset(0, 0, -1), u"axis"_s), u"z"_s);
            for (const QcBlockPos placed : {base.offset(0, 1, 0), base.offset(1, 0, 0), base.offset(0, 0, -1)}) {
                QCOMPARE(nameAt(placed), u"qtcraft:"_s + block);
                m_server->world().setBlock(placed, qcAirBlockId);
            }
        }

        // A log the world generator or an old world wrote without an axis stands upright.
        QCOMPARE(m_content->property(id("qtcraft:oak_log"), u"axis"_s), u"y"_s);
    }

    void lyingLogsShowRingsAtTheirEnds()
    {
        const QcBlockId log = id("qtcraft:oak_log");
        const auto source = [this](QcBlockId state, QcBlockFace face) { return m_content->state(state).faceSource[static_cast<std::size_t>(face)]; };
        const auto turns = [this](QcBlockId state, QcBlockFace face) {
            return static_cast<int>(m_content->state(state).faceUvTurns[static_cast<std::size_t>(face)]);
        };

        // Upright: rings on top and bottom, nothing turned.
        QCOMPARE(source(log, QcBlockFace::Up), QcBlockFace::Up);
        QCOMPARE(turns(log, QcBlockFace::East), 0);

        // Along x: the rings look east and west; the bark on the other four faces runs east to west.
        const QcBlockId alongX = m_content->withProperty(log, u"axis"_s, u"x"_s);
        QVERIFY(m_content->state(alongX).fullCube);
        for (const QcBlockFace end : {QcBlockFace::East, QcBlockFace::West}) {
            const QcBlockFace shown = source(alongX, end);
            QVERIFY(shown == QcBlockFace::Up || shown == QcBlockFace::Down);
            QCOMPARE(turns(alongX, end), 0);
        }
        for (const QcBlockFace side : {QcBlockFace::North, QcBlockFace::South, QcBlockFace::Up, QcBlockFace::Down}) {
            const QcBlockFace shown = source(alongX, side);
            QVERIFY(shown != QcBlockFace::Up && shown != QcBlockFace::Down);
            QCOMPARE(turns(alongX, side), 1);
        }

        // Along z: the rings look north and south; the bark of the top and bottom already runs along z.
        const QcBlockId alongZ = m_content->withProperty(log, u"axis"_s, u"z"_s);
        for (const QcBlockFace end : {QcBlockFace::North, QcBlockFace::South}) {
            const QcBlockFace shown = source(alongZ, end);
            QVERIFY(shown == QcBlockFace::Up || shown == QcBlockFace::Down);
        }
        QCOMPARE(turns(alongZ, QcBlockFace::East), 1);
        QCOMPARE(turns(alongZ, QcBlockFace::West), 1);
        QCOMPARE(turns(alongZ, QcBlockFace::Up), 0);
    }

    void axesStripLogsKeepingTheirAxis()
    {
        join();
        const QcBlockPos log = m_feet.offset(2, 0, 0);
        const QcBlockPos wood = m_feet.offset(-2, 0, 0);
        m_server->world().setBlock(log, m_content->withProperty(id("qtcraft:birch_log"), u"axis"_s, u"x"_s));
        m_server->world().setBlock(wood, id("qtcraft:oak_wood"));

        hold(u"iron_axe"_s);
        const int slot = m_client->selected;
        send(QcMsg::UseItem{log, QcBlockFace::Up});
        QCOMPARE(nameAt(log), u"qtcraft:stripped_birch_log"_s);
        QCOMPARE(propertyAt(log, u"axis"_s), u"x"_s);
        QCOMPARE(m_client->stacks[slot].damage, 1);
        send(QcMsg::UseItem{wood, QcBlockFace::Up});
        QCOMPARE(nameAt(wood), u"qtcraft:stripped_oak_wood"_s);

        // Stripping drops nothing, and a stripped log stays as it is.
        ticks(2);
        QCOMPARE(droppedCount(baseItem(u"birch_log"_s)) + droppedCount(baseItem(u"stripped_birch_log"_s)), 0);
        send(QcMsg::UseItem{log, QcBlockFace::Up});
        QCOMPARE(nameAt(log), u"qtcraft:stripped_birch_log"_s);
        QCOMPARE(m_client->stacks[slot].damage, 2);

        // A hoe turns coarse dirt into dirt.
        const QcBlockPos ground = m_feet.offset(0, 0, 2);
        m_server->world().setBlock(ground, id("qtcraft:coarse_dirt"));
        hold(u"stone_hoe"_s);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        QCOMPARE(nameAt(ground), u"qtcraft:dirt"_s);

        QCOMPARE(crafted({{0, u"stripped_oak_log"_s}}, 2), (std::pair{u"qtcraft:oak_planks"_s, 4}));
        QCOMPARE(crafted(filled(u"stripped_spruce_log"_s, 4), 2), (std::pair{u"qtcraft:stripped_spruce_wood"_s, 3}));
    }

    void signalDustBlockPowersItsNeighbours()
    {
        buildArea();
        // The block at (5, 0, 5) with dust east of it, a lamp west of it and a stone block north of
        // it, with a lamp behind the stone.
        put(5, 0, 5, u"signal_dust_block"_s);
        dustLine(6, 0, 5, 3);
        put(4, 0, 5, u"signal_lamp"_s);
        put(5, 0, 4, u"stone"_s);
        put(5, 0, 3, u"signal_lamp"_s);
        ticks(4);

        QCOMPARE(power(6, 0, 5), 15);
        QCOMPARE(power(8, 0, 5), 13);
        QVERIFY(isBlock(4, 0, 5, "signal_lamp_lit"));
        // It never powers a block: the stone passes nothing to the lamp behind it.
        QVERIFY(isBlock(5, 0, 3, "signal_lamp"));

        // A lamp on any side lights, also one set later.
        put(5, 0, 6, u"signal_lamp"_s);
        ticks(4);
        QVERIFY(isBlock(5, 0, 6, "signal_lamp_lit"));

        // Removing it switches everything off.
        put(5, 0, 5, u"air"_s);
        ticks(8);
        QCOMPARE(power(6, 0, 5), 0);
        QVERIFY(isBlock(4, 0, 5, "signal_lamp"));
    }
};

QTEST_GUILESS_MAIN(TestBuildingVariants)
#include "test_building_variants.moc"
