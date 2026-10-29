// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcServerTestFixture.h"
#include "server/pathing/QcPathCells.h"

using namespace Qt::StringLiterals;

// Dirt paths through the server (decision E-406): a shovel used on the top of grass or dirt makes
// one and wears by one (not in creative mode, not under a block, not from the side), a solid block
// placed on a path presses it back into dirt at the next block update, a mined path drops dirt, and
// creatures walk on it like on farmland.
class TestDirtPaths : public QcServerTestFixture
{
    Q_OBJECT

private:
    void hold(const char *name)
    {
        const int slot = slotOf(item(name));
        QVERIFY2(slot >= 0 && slot < 9, name);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    void use(QcBlockPos pos, QcBlockFace face = QcBlockFace::Up) { send(QcMsg::UseItem{pos, face}); }

    int shovelDamage(const char *name) const { return m_client->stacks[slotOf(item(name))].damage; }

private slots:
    void shovelsFlattenTheTopOfGrassAndDirt()
    {
        join();
        chat(u"/give wooden_shovel"_s);
        hold("qtcraft:wooden_shovel");
        const QcBlockPos grass = m_feet.offset(2, -1, 0);
        const QcBlockPos dirt = m_feet.offset(-2, -1, 0);
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));

        use(grass);
        QCOMPARE(*m_server->world().blockAt(grass), id("qtcraft:dirt_path"));
        use(dirt);
        QCOMPARE(*m_server->world().blockAt(dirt), id("qtcraft:dirt_path"));
        // Each path wears the shovel once.
        QCOMPARE(shovelDamage("qtcraft:wooden_shovel"), 2);

        // Not from a side, not under a block, not on stone, not on a path again.
        const QcBlockPos side = m_feet.offset(-2, -1, 2);
        use(side, QcBlockFace::North);
        QCOMPARE(*m_server->world().blockAt(side), id("qtcraft:grass_block"));
        const QcBlockPos covered = m_feet.offset(0, -1, 2);
        m_server->world().setBlock(covered.offset(0, 1, 0), id("qtcraft:oak_planks"));
        use(covered);
        QCOMPARE(*m_server->world().blockAt(covered), id("qtcraft:grass_block"));
        const QcBlockPos underGrass = m_feet.offset(2, -1, 2);
        m_server->world().setBlock(underGrass.offset(0, 1, 0), id("qtcraft:tall_grass"));
        use(underGrass);
        QCOMPARE(*m_server->world().blockAt(underGrass), id("qtcraft:grass_block"));
        const QcBlockPos stone = m_feet.offset(0, -1, -2);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        use(stone);
        QCOMPARE(*m_server->world().blockAt(stone), id("qtcraft:stone"));
        use(grass);
        QCOMPARE(*m_server->world().blockAt(grass), id("qtcraft:dirt_path"));
        QCOMPARE(shovelDamage("qtcraft:wooden_shovel"), 2);

        // Out of reach nothing changes.
        const QcBlockPos far = m_feet.offset(7, -1, 0);
        m_server->world().setBlock(far, id("qtcraft:grass_block"));
        m_server->world().setBlock(far.offset(0, 1, 0), qcAirBlockId);
        use(far);
        QCOMPARE(*m_server->world().blockAt(far), id("qtcraft:grass_block"));
        QCOMPARE(shovelDamage("qtcraft:wooden_shovel"), 2);
    }

    void creativeShovelsDoNotWear()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        join(config);
        chat(u"/give iron_shovel"_s);
        hold("qtcraft:iron_shovel");
        use(m_feet.offset(2, -1, 0));
        QCOMPARE(*m_server->world().blockAt(m_feet.offset(2, -1, 0)), id("qtcraft:dirt_path"));
        QCOMPARE(shovelDamage("qtcraft:iron_shovel"), 0);
    }

    void aHoeTillsAPath()
    {
        join();
        chat(u"/give wooden_hoe"_s);
        hold("qtcraft:wooden_hoe");
        const QcBlockPos path = m_feet.offset(2, -1, 0);
        m_server->world().setBlock(path, id("qtcraft:dirt_path"));
        use(path);
        QCOMPARE(m_content->block(*m_server->world().blockAt(path)).name, u"qtcraft:farmland"_s);
    }

    void aSolidBlockPressesAPathIntoDirt()
    {
        join();
        chat(u"/give oak_planks 2"_s);
        chat(u"/give glass 1"_s);
        const QcBlockPos path = m_feet.offset(2, -1, 0);
        const QcBlockPos glassed = m_feet.offset(-2, -1, 0);
        m_server->world().setBlock(path, id("qtcraft:dirt_path"));
        m_server->world().setBlock(glassed, id("qtcraft:dirt_path"));

        // Glass is no full opaque block: the path under it stays.
        hold("qtcraft:glass");
        use(glassed);
        ticks(2);
        QCOMPARE(*m_server->world().blockAt(glassed.offset(0, 1, 0)), id("qtcraft:glass"));
        QCOMPARE(*m_server->world().blockAt(glassed), id("qtcraft:dirt_path"));

        hold("qtcraft:oak_planks");
        use(path);
        ticks(2);
        QCOMPARE(*m_server->world().blockAt(path.offset(0, 1, 0)), id("qtcraft:oak_planks"));
        QCOMPARE(*m_server->world().blockAt(path), id("qtcraft:dirt"));
    }

    void aMinedPathDropsDirt()
    {
        const QcBlockDefinition &path = m_content->block(id("qtcraft:dirt_path"));
        QCOMPARE(path.drops.size(), 1);
        QCOMPARE(path.drops.front().item, u"qtcraft:dirt"_s);
        QCOMPARE(path.drops.front().chance, 1.0);

        join();
        const QcBlockPos at = m_feet.offset(2, -1, 0);
        m_server->world().setBlock(at, id("qtcraft:dirt_path"));
        send(QcMsg::StartMining{at});
        ticks(40);
        send(QcMsg::FinishMining{at});
        QCOMPARE(*m_server->world().blockAt(at), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:dirt")) + droppedCount(item("qtcraft:dirt")), 1);
        QCOMPARE(countOf(item("qtcraft:dirt_path")) + droppedCount(item("qtcraft:dirt_path")), 0);
    }

    void creaturesWalkOnPathsLikeOnFarmland()
    {
        // 15/16 high like farmland: a floor to stand on (its top lies above the reach of a low
        // cell's lift), never a wall.
        const QcPathCellTable cells(*m_content);
        QCOMPARE(QcPathCell::kindOf(cells.of(id("qtcraft:dirt_path"))), QcPathCell::Kind::Solid);
        QCOMPARE(cells.of(id("qtcraft:dirt_path")), cells.of(m_content->withProperty(id("qtcraft:farmland"), u"moist"_s, u"false"_s)));
        QVERIFY(m_content->block(id("qtcraft:dirt_path")).solid);
        QVERIFY(!m_content->state(id("qtcraft:dirt_path")).opaque);
    }
};

QTEST_GUILESS_MAIN(TestDirtPaths)
#include "test_dirt_paths.moc"
