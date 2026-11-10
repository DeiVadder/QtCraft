// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

using namespace Qt::StringLiterals;

namespace {

// The test terrain in a biome without vegetation.
class QcBarrenGenerator : public QcTestTerrainGenerator
{
public:
    using QcTestTerrainGenerator::QcTestTerrainGenerator;

    QString biomeAt(int, int) const override { return u"qtcraft:nether_wastes"_s; }
};

} // namespace

// The plants of the nether forests through the server (decisions E-1044, E-1045): fungi wait for
// bone meal on their own ground, vines hang and grow towards their tip, nylium spreads over
// netherrack, and the wood of the forests does not burn.
class TestNetherPlants : public QcServerTestFixture
{
    Q_OBJECT

private:
    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < 9, name);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    void use(QcBlockPos pos) { send(QcMsg::UseItem{pos, QcBlockFace::Up}); }

    // Bone meal on `pos` until `done`, at most `tries` times, a tick after each.
    template <typename Done>
    bool fertilizeUntil(QcBlockPos pos, Done done, int tries)
    {
        for (int attempt = 0; attempt < tries && !done(); ++attempt) {
            use(pos);
            ticks(2);
        }
        return done();
    }

    [[nodiscard]] QcBlockId at(QcBlockPos pos) const { return *m_server->world().blockAt(pos); }

    // Clears a pillar of air above `ground` and sets it to `block`.
    void clearAbove(QcBlockPos ground, const char *block, int height = 20)
    {
        m_server->world().setBlock(ground, id(block));
        for (int y = 1; y <= height; ++y) {
            m_server->world().setBlock(ground.offset(0, y, 0), qcAirBlockId);
        }
    }

private slots:
    void fungusWaitsForBoneMealOnItsOwnNylium()
    {
        join();
        chat(u"/give bone_meal 64"_s);
        hold("qtcraft:bone_meal");

        const QcBlockPos fungus = m_feet.offset(3, 0, 0);
        clearAbove(fungus.offset(0, -1, 0), "qtcraft:crimson_nylium");
        m_server->world().setBlock(fungus, id("qtcraft:crimson_fungus"));
        ticks(200);
        QCOMPARE(at(fungus), id("qtcraft:crimson_fungus"));

        const auto grown = [&] { return at(fungus) != id("qtcraft:crimson_fungus"); };
        QVERIFY(fertilizeUntil(fungus, grown, 40));
        QCOMPARE(m_content->block(at(fungus)).name, u"qtcraft:crimson_stem"_s);
    }

    // Caps are built from blocks that stay when the stem goes.
    void capsOfFungiDoNotDecay()
    {
        QVERIFY(!m_server->plants().isLeaves(id("qtcraft:nether_wart_block")));
        QVERIFY(!m_server->plants().isLeaves(id("qtcraft:warped_wart_block")));
        QVERIFY(m_server->plants().isLeaves(id("qtcraft:oak_leaves")));
    }

    void fungusStaysOnForeignGround()
    {
        join();
        chat(u"/give bone_meal 16"_s);
        hold("qtcraft:bone_meal");

        const QcBlockPos fungus = m_feet.offset(3, 0, 0);
        clearAbove(fungus.offset(0, -1, 0), "qtcraft:warped_nylium");
        m_server->world().setBlock(fungus, id("qtcraft:crimson_fungus"));
        const auto grown = [&] { return at(fungus) != id("qtcraft:crimson_fungus"); };
        QVERIFY(!fertilizeUntil(fungus, grown, 14));
        QCOMPARE(countOf(item("qtcraft:bone_meal")), 16);
    }

    // Where nothing can grow the bone meal stays in the hand.
    void boneMealStaysWhereNothingGrows()
    {
        join({}, std::make_unique<QcBarrenGenerator>(m_content));
        chat(u"/give bone_meal 16"_s);
        hold("qtcraft:bone_meal");

        const QcBlockPos ground = m_feet.offset(3, -1, 0);
        clearAbove(ground, "qtcraft:grass_block", 3);
        use(ground);
        idle(2);
        QCOMPARE(countOf(item("qtcraft:bone_meal")), 16);
        QCOMPARE(at(ground.offset(0, 1, 0)), qcAirBlockId);
    }

    // Nylium grows its own forest's plants in any biome.
    void nyliumGrowsItsOwnPlantsAnywhere()
    {
        join();
        chat(u"/give bone_meal 16"_s);
        hold("qtcraft:bone_meal");

        const QcBlockPos ground = m_feet.offset(3, -1, 0);
        for (int cell = 0; cell < 25; ++cell) {
            clearAbove(ground.offset(cell % 5 - 2, 0, cell / 5 - 2), "qtcraft:warped_nylium", 3);
        }
        use(ground);
        idle(2);
        QCOMPARE(countOf(item("qtcraft:bone_meal")), 15);

        const QList<QcBlockId> own{id("qtcraft:warped_roots"), id("qtcraft:nether_sprouts"), id("qtcraft:warped_fungus"), id("qtcraft:crimson_fungus")};
        int plants = 0;
        for (int cell = 0; cell < 25; ++cell) {
            const QcBlockId grown = at(ground.offset(cell % 5 - 2, 1, cell / 5 - 2));
            QVERIFY(grown == qcAirBlockId || own.contains(grown));
            plants += grown != qcAirBlockId ? 1 : 0;
        }
        QVERIFY(plants > 0);
    }

    // The plants go through the same placing as any block: on their ground, under their holder.
    void plantsArePlacedFromTheHand()
    {
        join();
        chat(u"/give crimson_fungus 1"_s);
        chat(u"/give weeping_vines 1"_s);
        hold("qtcraft:crimson_fungus");

        const QcBlockPos ground = m_feet.offset(3, -1, 0);
        clearAbove(ground, "qtcraft:crimson_nylium", 6);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        idle(2);
        QCOMPARE(at(ground.offset(0, 1, 0)), id("qtcraft:crimson_fungus"));

        const QcBlockPos holder = ground.offset(0, 4, 0);
        m_server->world().setBlock(holder, id("qtcraft:netherrack"));
        hold("qtcraft:weeping_vines");
        send(QcMsg::UseItem{holder, QcBlockFace::Down});
        idle(2);
        QCOMPARE(at(holder.offset(0, -1, 0)), id("qtcraft:weeping_vines"));
    }

    void hangingVinesGrowDownAndFallWithTheirHolder()
    {
        join();
        chat(u"/give bone_meal 8"_s);
        hold("qtcraft:bone_meal");

        const QcBlockPos holder = m_feet.offset(4, 6, 0);
        for (int y = 0; y < 6; ++y) {
            m_server->world().setBlock(holder.offset(0, -1 - y, 0), qcAirBlockId);
        }
        m_server->world().setBlock(holder, id("qtcraft:netherrack"));
        const QcBlockPos vine = holder.offset(0, -1, 0);
        m_server->world().setBlock(vine, id("qtcraft:weeping_vines"));

        use(vine);
        ticks(2);
        QCOMPARE(at(vine.offset(0, -1, 0)), id("qtcraft:weeping_vines"));
        QCOMPARE(at(holder), id("qtcraft:netherrack"));

        chat(u"/setblock %1 %2 %3 air"_s.arg(holder.x).arg(holder.y).arg(holder.z));
        ticks(4);
        QCOMPARE(at(vine), qcAirBlockId);
        QCOMPARE(at(vine.offset(0, -1, 0)), qcAirBlockId);
    }

    void risingVinesGrowUp()
    {
        join();
        chat(u"/give bone_meal 8"_s);
        hold("qtcraft:bone_meal");

        const QcBlockPos ground = m_feet.offset(4, -1, 0);
        clearAbove(ground, "qtcraft:warped_nylium");
        const QcBlockPos vine = ground.offset(0, 1, 0);
        m_server->world().setBlock(vine, id("qtcraft:twisting_vines"));
        use(vine);
        ticks(2);
        QCOMPARE(at(vine.offset(0, 1, 0)), id("qtcraft:twisting_vines"));
    }

    void boneMealSpreadsNyliumOverNetherrack()
    {
        join();
        chat(u"/give bone_meal 8"_s);
        hold("qtcraft:bone_meal");

        const QcBlockPos bare = m_feet.offset(4, -1, 0);
        clearAbove(bare, "qtcraft:netherrack");
        clearAbove(bare.offset(1, 0, 0), "qtcraft:netherrack");
        use(bare);
        ticks(2);
        QCOMPARE(at(bare), id("qtcraft:netherrack"));
        QCOMPARE(countOf(item("qtcraft:bone_meal")), 8);

        clearAbove(bare.offset(1, 0, 0), "qtcraft:warped_nylium");
        use(bare);
        ticks(2);
        QCOMPARE(at(bare), id("qtcraft:warped_nylium"));
        QCOMPARE(countOf(item("qtcraft:bone_meal")), 7);
    }

    void woodOfTheForestsDoesNotBurn()
    {
        for (const char *name : {"qtcraft:crimson_stem", "qtcraft:warped_hyphae", "qtcraft:crimson_planks", "qtcraft:warped_door",
                                 "qtcraft:crimson_sign", "qtcraft:warped_fence", "qtcraft:crimson_stairs", "qtcraft:warped_slab"}) {
            const QcBlockDefinition &block = m_content->block(*m_content->blockId(QString::fromLatin1(name)));
            QVERIFY2(block.fire.burn == 0 && block.fire.ignite == 0 && block.fuelSeconds == 0.0, name);
        }
    }
};

QTEST_MAIN(TestNetherPlants)
#include "test_nether_plants.moc"
