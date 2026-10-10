#include "QcCrafting.h"
#include "QcPhysics.h"
#include "QcServerTestFixture.h"
#include "QcTerrainGenerator.h"
#include "QcTestWorld.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

// Blocks and recipes of the side branch (docs/FORTSCHRITT-Bloecke-Rezepte.md): cobwebs and mud in
// the shared physics, concrete powder, magma, glass bottles and soup on the server, and the new
// recipe chains.
class TestNewBlocks : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcBlockId block(const char *name) const { return *m_content->blockId(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcItemId baseItem(const char *name) const { return *m_content->itemId(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcBlockId at(QcBlockPos pos) const { return m_server->world().blockAt(pos).value_or(qcAirBlockId); }
    void set(QcBlockPos pos, const char *name) { m_server->world().setBlock(pos, block(name)); }

    // Result of a crafting grid of `size` x `size` with one item in each listed cell, empty when no
    // recipe matches.
    QString crafted(const QList<std::pair<int, const char *>> &cells, int size) const
    {
        QList<QcItemStack> grid(qsizetype(size) * size);
        for (const auto &[cell, name] : cells) {
            grid[cell] = {baseItem(name), 1, 0};
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
        return recipe ? m_content->recipes()[*recipe].resultItem : QString();
    }
    int craftedCount(const QList<std::pair<int, const char *>> &cells, int size) const
    {
        QList<QcItemStack> grid(qsizetype(size) * size);
        for (const auto &[cell, name] : cells) {
            grid[cell] = {baseItem(name), 1, 0};
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
        return recipe ? m_content->recipes()[*recipe].resultCount : 0;
    }
    QList<std::pair<int, const char *>> filled(const char *name, int cells) const
    {
        QList<std::pair<int, const char *>> result;
        for (int cell = 0; cell < cells; ++cell) {
            result.append({cell, name});
        }
        return result;
    }

    // A stone floor at y = 63 with `ground` on top of it at y = 64 under the start (0.5, 65, 0.5).
    QcTestWorld floorWorld(const char *ground) const
    {
        QcTestWorld world(*m_content);
        world.fillBox({-20, 63, -20}, {20, 63, 20}, block("stone"));
        world.fillBox({-20, 64, -20}, {20, 64, 20}, block(ground));
        return world;
    }
    static QcPlayerInput walking(bool jump = false)
    {
        QcPlayerInput input;
        input.forward = 1.0f;
        input.jump = jump;
        return input;
    }
    // Distance walked north in `ticks` ticks from a standstill.
    static double walked(const QcBlockAccess &world, int ticks)
    {
        QcEntityMotion motion;
        motion.position = {0.5, 65.0, 0.5};
        motion.onGround = true;
        for (int tick = 0; tick < ticks; ++tick) {
            QcPhysics::stepPlayer(motion, walking(), world, QcPhysics::tickDuration);
        }
        return std::abs(motion.position.z - 0.5);
    }
    // Highest rise above the ground of one standing jump.
    static double jumpHeight(const QcBlockAccess &world)
    {
        QcEntityMotion motion;
        motion.position = {0.5, 65.0, 0.5};
        motion.onGround = true;
        QcPlayerInput input;
        input.jump = true;
        double peak = 65.0;
        for (int tick = 0; tick < 30; ++tick) {
            QcPhysics::stepPlayer(motion, input, world, QcPhysics::tickDuration);
            input.jump = false;
            peak = std::max(peak, motion.position.y);
        }
        return peak - 65.0;
    }

    // A generated cobweb in the chunks around the player with no solid block below it.
    std::optional<QcBlockPos> floatingCobweb() const
    {
        const QcChunkPos centre = m_feet.chunk();
        const int west = (centre.x - 1) * QcWorld::chunkSize;
        const int north = (centre.z - 1) * QcWorld::chunkSize;
        for (int x = west; x < west + 3 * QcWorld::chunkSize; ++x) {
            for (int z = north; z < north + 3 * QcWorld::chunkSize; ++z) {
                for (int y = 10; y <= 50; ++y) {
                    if (at({x, y, z}) == block("cobweb") && !m_content->block(at({x, y - 1, z})).solid) {
                        return QcBlockPos{x, y, z};
                    }
                }
            }
        }
        return std::nullopt;
    }
    // The cobwebs connected to `start` through their faces.
    QList<QcBlockPos> clumpOf(QcBlockPos start) const
    {
        QList<QcBlockPos> clump{start};
        for (qsizetype i = 0; i < clump.size(); ++i) {
            for (const QcBlockFace face : qcAllBlockFaces) {
                const QcBlockPos next = qcNeighbour(clump[i], face);
                if (at(next) == block("cobweb") && !clump.contains(next)) {
                    clump.append(next);
                }
            }
        }
        return clump;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void mudSlowsWalkingAndLowersTheJump()
    {
        const QcTestWorld grass = floorWorld("grass_block");
        const QcTestWorld mud = floorWorld("mud");

        const double onGrass = walked(grass, 40);
        const double onMud = walked(mud, 40);
        QVERIFY2(onMud < onGrass * 0.7 && onMud > onGrass * 0.5, qPrintable(u"%1 vs %2"_s.arg(onMud).arg(onGrass)));

        // A jump from mud is clearly lower but still clears one block (E-207).
        const double high = jumpHeight(grass);
        const double low = jumpHeight(mud);
        QVERIFY2(high > 1.2, qPrintable(QString::number(high)));
        QVERIFY2(low > 1.0 && low < high * 0.9, qPrintable(u"%1 vs %2"_s.arg(low).arg(high)));
    }

    void cobwebsSlowBodiesInsideAndBreakFalls()
    {
        QcTestWorld open = floorWorld("grass_block");
        QcTestWorld webbed = floorWorld("grass_block");
        webbed.fillBox({-1, 65, -20}, {1, 66, 1}, block("cobweb"));

        const double free = walked(open, 40);
        const double caught = walked(webbed, 40);
        QVERIFY2(caught < free * 0.35, qPrintable(u"%1 vs %2"_s.arg(caught).arg(free)));

        // Falling through a column of webs stays slow.
        QcTestWorld shaft(*m_content);
        shaft.fillBox({-1, 40, -1}, {1, 40, 1}, block("stone"));
        shaft.fillBox({0, 41, 0}, {0, 60, 0}, block("cobweb"));
        QcEntityMotion motion;
        motion.position = {0.5, 59.0, 0.5};
        for (int tick = 0; tick < 20; ++tick) {
            QcPhysics::stepPlayer(motion, {}, shaft, QcPhysics::tickDuration);
        }
        QVERIFY(motion.slowed);
        QVERIFY2(motion.velocity.y > -1.0, qPrintable(QString::number(motion.velocity.y)));
        QVERIFY2(motion.position.y > 57.5, qPrintable(QString::number(motion.position.y)));
    }

    void cobwebsGrowInCavesAndDropString()
    {
        const QcBlockDefinition &web = m_content->block(block("cobweb"));
        QVERIFY(!web.solid);
        QCOMPARE(web.slowsInside, 0.25);
        QCOMPARE(web.drops.first().item, u"qtcraft:string"_s);
        QVERIFY(std::ranges::any_of(m_content->ores(), [](const QcOreDefinition &ore) { return ore.block == u"qtcraft:cobweb"_s; }));
        QCOMPARE(crafted({{0, "string"}, {1, "string"}, {2, "string"}, {3, "string"}}, 2), u"qtcraft:white_wool"_s);
    }

    void cobwebsHangWithoutSupport()
    {
        join();
        // A cobweb in the air stays when the block beside it is mined.
        const QcBlockPos web = m_feet.offset(2, 1, 0);
        const QcBlockPos dirt = web.offset(0, 0, 1);
        set(web, "cobweb");
        set(dirt, "dirt");
        send(QcMsg::StartMining{dirt});
        ticks(40);
        send(QcMsg::FinishMining{dirt});
        ticks(5);
        QCOMPARE(at(dirt), qcAirBlockId);
        QCOMPARE(at(web), block("cobweb"));
    }

    void generatedCobwebClumpsStayPut()
    {
        join({}, std::make_unique<QcTerrainGenerator>(m_content, 7));
        const std::optional<QcBlockPos> floating = floatingCobweb();
        QVERIFY(floating);
        const QList<QcBlockPos> clump = clumpOf(*floating);

        // Changing a neighbour of every web of the clump updates all of them; none falls.
        for (const QcBlockPos web : clump) {
            for (const QcBlockFace face : qcAllBlockFaces) {
                const QcBlockPos beside = qcNeighbour(web, face);
                if (at(beside) != block("cobweb")) {
                    const QString next = at(beside) == qcAirBlockId ? u"stone"_s : u"air"_s;
                    chat(u"/setblock %1 %2 %3 %4"_s.arg(beside.x).arg(beside.y).arg(beside.z).arg(next));
                    ticks(3);
                    break;
                }
            }
        }
        ticks(10);
        for (const QcBlockPos web : clump) {
            QCOMPARE(at(web), block("cobweb"));
        }
    }

    void concretePowderHardensNextToWater()
    {
        join();
        chat(u"/give red_concrete_powder 2"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:red_concrete_powder"))});

        // Placed directly beside existing water (into a hole next to a pool, against the side of the
        // block beyond), the powder turns into concrete right away.
        const QcBlockPos pool = m_feet.offset(-2, -1, 0);
        const QcBlockPos hole = pool.offset(-1, 0, 0);
        set(pool, "water");
        set(hole, "air");
        set(hole.offset(0, -1, 0), "stone");
        set(hole.offset(-1, 0, 0), "stone");
        send(QcMsg::UseItem{hole.offset(-1, 0, 0), QcBlockFace::East, {1.0, 0.5, 0.5}});
        QCOMPARE(at(hole), block("red_concrete"));

        // Water that touches it only diagonally (one down and one to the side) does not count.
        set(m_feet.offset(2, -1, 0), "water");
        send(QcMsg::UseItem{m_feet.offset(2, -1, 1), QcBlockFace::Up, {0.5, 1.0, 0.5}});
        ticks(2);
        QCOMPARE(at(m_feet.offset(2, 0, 1)), block("red_concrete_powder"));

        // Water that arrives beside it later hardens it as well.
        const QcBlockPos beside = m_feet.offset(2, 0, 2);
        chat(u"/setblock %1 %2 %3 water"_s.arg(beside.x).arg(beside.y).arg(beside.z));
        ticks(3);
        QCOMPARE(at(m_feet.offset(2, 0, 1)), block("red_concrete"));

        // Powder that falls into a pool hardens where it touches the water.
        const QcBlockPos basin = m_feet.offset(-2, -2, -2);
        set(basin, "stone");
        set(basin.offset(0, 1, 0), "water");
        chat(u"/setblock %1 %2 %3 blue_concrete_powder"_s.arg(basin.x).arg(basin.y + 5).arg(basin.z));
        ticks(60);
        // It falls into the water cell and hardens there (E-217), nothing is left above.
        QCOMPARE(at(basin.offset(0, 1, 0)), block("blue_concrete"));
        for (int dy = 2; dy <= 5; ++dy) {
            QCOMPARE(at(basin.offset(0, dy, 0)), qcAirBlockId);
        }
    }

    void magmaBurnsWhoeverStandsOnIt()
    {
        join();
        set(m_feet.offset(0, -1, 0), "magma_block");
        idle(40);
        QVERIFY2(m_client->health < 20.0, qPrintable(QString::number(m_client->health)));

        const QcBlockDefinition &magma = m_content->block(block("magma_block"));
        QCOMPARE(magma.lightLevel, 3);
    }

    void swampShoresKeepTheirPlants()
    {
        // Mud shores still carry sugar cane, grass and orchids.
        for (const char *plant : {"sugar_cane", "tall_grass", "blue_orchid"}) {
            QVERIFY2(m_content->block(block(plant)).groundBlocks.contains(u"qtcraft:mud"_s), plant);
            const auto feature = std::ranges::find(m_content->features(), u"qtcraft:"_s + QString::fromLatin1(plant), &QcFeatureDefinition::name);
            QVERIFY2(feature != m_content->features().end() && feature->groundBlocks.contains(u"qtcraft:mud"_s), plant);
        }
    }

    void glassBottlesFillWithoutTakingTheWater()
    {
        join();
        const QcBlockPos pool = m_feet.offset(2, -1, 0);
        set(pool, "water");
        chat(u"/give glass_bottle 3"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:glass_bottle"))});
        send(QcMsg::UseItem{pool, QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QCOMPARE(countOf(item("qtcraft:water_bottle")), 1);
        QCOMPARE(countOf(item("qtcraft:glass_bottle")), 2);
        QCOMPARE(at(pool), block("water"));

        // Dirt and a water bottle make mud.
        QCOMPARE(crafted({{0, "dirt"}, {1, "water_bottle"}}, 2), u"qtcraft:mud"_s);
    }

    void soupLeavesItsBowl()
    {
        join();
        chat(u"/give mushroom_stew"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:mushroom_stew"))});
        chat(u"/feed 4"_s);
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:mushroom_stew")), 0);
        QCOMPARE(countOf(item("qtcraft:bowl")), 1);
    }

    void nuggetsChunksAndStorageBlocks()
    {
        QCOMPARE(crafted({{0, "iron_ingot"}}, 2), u"qtcraft:iron_nugget"_s);
        QCOMPARE(craftedCount({{0, "iron_ingot"}}, 2), 9);
        QCOMPARE(crafted(filled("gold_nugget", 9), 3), u"qtcraft:gold_ingot"_s);
        QCOMPARE(crafted({{0, "charcoal"}}, 2), u"qtcraft:coal_chunk"_s);
        QCOMPARE(crafted({{0, "coal_chunk"}}, 2), u"qtcraft:black_dye"_s);
        QCOMPARE(crafted(filled("coal_chunk", 9), 3), u"qtcraft:coal"_s);
        QCOMPARE(crafted(filled("diamond", 9), 3), u"qtcraft:diamond_block"_s);
        QCOMPARE(crafted({{0, "iron_block"}}, 2), u"qtcraft:iron_ingot"_s);
        QCOMPARE(m_content->block(block("coal_block")).fuelSeconds, 720.0);

        // A small torch from one chunk lights less than a torch (E-201); a lantern lights most.
        QCOMPARE(crafted({{1, "coal_chunk"}, {4, "stick"}}, 3), u"qtcraft:small_torch"_s);
        QVERIFY(m_content->block(block("small_torch")).lightLevel < m_content->block(block("torch")).lightLevel);
        QList<std::pair<int, const char *>> lantern = filled("iron_nugget", 9);
        lantern[4].second = "torch";
        QCOMPARE(crafted(lantern, 3), u"qtcraft:lantern"_s);
        QCOMPARE(m_content->block(block("lantern")).lightLevel, 15);
    }

    void fireflyGlassStaysDim()
    {
        // Four glass around a firefly bottle give four blocks of faint light (E-202).
        const QList<std::pair<int, const char *>> cells{{1, "glass"}, {3, "glass"}, {4, "firefly_bottle"}, {5, "glass"}, {7, "glass"}};
        QCOMPARE(crafted(cells, 3), u"qtcraft:firefly_glass"_s);
        QCOMPARE(craftedCount(cells, 3), 4);
        const QcBlockDefinition &glass = m_content->block(block("firefly_glass"));
        QVERIFY(glass.lightLevel > 0 && glass.lightLevel <= 5);
        QCOMPARE(glass.lightOpacity, 0);
    }

    void leatherFromRottenFleshIsSlow()
    {
        // Nine scraps of 15 s each make one leather (E-203).
        const QcSmeltingRecipe *scrap = m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:rotten_flesh"_s);
        QVERIFY(scrap);
        QCOMPARE(scrap->resultItem, u"qtcraft:leather_scrap"_s);
        QVERIFY(scrap->seconds >= 15.0);
        QCOMPARE(crafted(filled("leather_scrap", 8), 3), QString());
        QCOMPARE(crafted(filled("leather_scrap", 9), 3), u"qtcraft:leather"_s);
        QCOMPARE(crafted({{0, "paper"}, {1, "paper"}, {2, "paper"}, {3, "leather"}}, 2), u"qtcraft:book"_s);
    }

    void clayWorksLikeVanilla()
    {
        // Clay blocks drop four balls; balls fire into bricks, four bricks make the block (E-204).
        const QcBlockDefinition &clay = m_content->block(block("clay"));
        QCOMPARE(clay.drops.first().item, u"qtcraft:clay_ball"_s);
        QCOMPARE(clay.drops.first().count, 4);
        QCOMPARE(m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:clay_ball"_s)->resultItem, u"qtcraft:brick"_s);
        QCOMPARE(m_content->smeltingRecipe(u"smelting"_s, u"qtcraft:clay"_s)->resultItem, u"qtcraft:terracotta"_s);
        QCOMPARE(crafted(filled("brick", 4), 2), u"qtcraft:bricks"_s);
        QCOMPARE(crafted(filled("clay_ball", 4), 2), u"qtcraft:clay"_s);
    }

    void sixteenColours()
    {
        // New dyes from flowers and mixes; every colour has wool, carpet, glass, pane, terracotta
        // and concrete with its powder (E-205, E-206).
        QCOMPARE(crafted({{0, "lily_of_the_valley"}}, 2), u"qtcraft:white_dye"_s);
        QCOMPARE(crafted({{0, "blue_dye"}, {1, "green_dye"}}, 2), u"qtcraft:cyan_dye"_s);
        QCOMPARE(crafted({{0, "red_dye"}, {1, "white_dye"}}, 2), u"qtcraft:pink_dye"_s);
        QCOMPARE(crafted({{0, "white_wool"}, {1, "lime_dye"}}, 2), u"qtcraft:lime_wool"_s);

        for (const char *colour : {"white", "light_gray", "gray", "black", "brown", "red", "orange", "yellow", "lime", "green", "cyan",
                                   "light_blue", "blue", "purple", "magenta", "pink"}) {
            const QString c = QString::fromLatin1(colour);
            for (const QString &suffix : {u"_wool"_s, u"_wool_carpet"_s, u"_stained_glass"_s, u"_stained_glass_pane"_s, u"_terracotta"_s,
                                          u"_concrete"_s, u"_concrete_powder"_s}) {
                QVERIFY2(m_content->blockId(u"qtcraft:"_s + c + suffix), qPrintable(c + suffix));
            }
            QVERIFY2(m_content->itemId(u"qtcraft:%1_dye"_s.arg(c)), colour);
            const QcBlockDefinition &powder = m_content->block(*m_content->blockId(u"qtcraft:%1_concrete_powder"_s.arg(c)));
            QVERIFY(powder.falls);
            QCOMPARE(powder.hardensInto, u"qtcraft:%1_concrete"_s.arg(c));
        }
    }
};

QTEST_GUILESS_MAIN(TestNewBlocks)
#include "test_new_blocks.moc"
