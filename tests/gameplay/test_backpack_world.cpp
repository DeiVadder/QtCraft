// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcWorldConstants.h"
#include "QcCircuitFixture.h"
#include "QcTestUtil.h"
#include "server/explosions/QcServerExplosions.h"
#include "server/signal/QcMovingBlocks.h"

using namespace Qt::StringLiterals;

// The backpack in the world around it (decision E-832): a blast breaks a filled one into exactly one
// item carrying the contents, a piston carries a filled one along with its contents and pushes a
// chest into its old cell without minting a backpack from the chest's stacks, and a dropped
// backpack destroyed on the ground (lava, fire, the void, old age) is gone together with its
// contents, without spilling them.
class TestBackpackWorld : public QcCircuitFixture
{
    Q_OBJECT

    QcItemStack stackOf(const char *name, int count) const { return {item(name), count, 0}; }
    QcItemStack backpack() const { return stackOf("qtcraft:backpack", 1); }
    QcItemStack packed(QList<QcItemStack> contents) const { return backpack().withContents(std::move(contents)); }

    QList<QcItemStack> padded(QList<QcItemStack> stacks) const
    {
        stacks.resize(9);
        return stacks;
    }

    int onTheGround(const QcItemStack &wanted) const
    {
        int found = 0;
        for (const QcItemEntity &dropped : m_server->items().items()) {
            found += dropped.stack == wanted ? 1 : 0;
        }
        return found;
    }

    int itemsOnTheGround() const { return static_cast<int>(m_server->items().items().size()); }

    // A backpack at local (x, y, z) holding `inside`.
    void backpackWith(int x, int y, int z, const QList<QcItemStack> &inside)
    {
        put(x, y, z, u"backpack[facing=south]"_s);
        QVERIFY(ensureEntity(at(x, y, z)));
        m_server->blockEntities().container(at(x, y, z))->stacks = padded(inside);
    }

    // A piston at (4, 0, 8) facing east, its lever at (3, 0, 8); pushes once the lever flips.
    void pushEast()
    {
        flip(3, 0, 8);
        ticks(QcMovingBlocks::moveTicks + 4);
    }

    void pistonRow()
    {
        put(3, 0, 8, u"lever[attach=floor]"_s);
        put(4, 0, 8, u"signal_piston[facing=east]"_s);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aBlastDropsOneBackpackWithTheContents()
    {
        buildArea();
        const QList<QcItemStack> inside{stackOf("qtcraft:dirt", 5), {}, stackOf("qtcraft:stick", 3)};
        backpackWith(6, 0, 6, inside);
        const QcBlockPos cell = at(6, 0, 6);
        // Power 1: every broken cell drops (chance 1 / power).
        m_server->systems().explosions.explode({QcVec3{cell.x + 0.5, cell.y + 0.5, cell.z + 0.5}, 1.0, true, false});
        for (int tick = 0; tick < 20 && m_server->systems().explosions.waitingCells() > 0; ++tick) {
            ticks(1);
        }
        ticks(1);

        QCOMPARE(*m_server->world().blockAt(cell), qcAirBlockId);
        QCOMPARE(onTheGround(packed(inside)), 1);
        QCOMPARE(onTheGround(backpack()), 0);
        QCOMPARE(droppedCount(item("qtcraft:backpack")), 1);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 0);
    }

    void aPistonPushesAChestIntoTheBackpacksCell()
    {
        buildArea();
        pistonRow();
        put(5, 0, 8, u"chest[facing=south]"_s);
        QVERIFY(ensureEntity(at(5, 0, 8)));
        const QList<QcItemStack> chest{stackOf("qtcraft:dirt", 5), stackOf("qtcraft:cobblestone", 64)};
        m_server->blockEntities().container(at(5, 0, 8))->stacks[0] = chest[0];
        m_server->blockEntities().container(at(5, 0, 8))->stacks[1] = chest[1];
        const QList<QcItemStack> inside{stackOf("qtcraft:stick", 3)};
        backpackWith(6, 0, 8, inside);
        ticks(1);
        pushEast();

        QVERIFY(isBlock(6, 0, 8, "chest"));
        QVERIFY(isBlock(7, 0, 8, "backpack"));
        QCOMPARE(itemsOnTheGround(), 0);
        const QList<QcItemStack> &moved = m_server->blockEntities().container(at(6, 0, 8))->stacks;
        QCOMPARE(moved.at(0), chest[0]);
        QCOMPARE(moved.at(1), chest[1]);
        QCOMPARE(m_server->blockEntities().container(at(7, 0, 8))->stacks, padded(inside));
    }

    void aPistonCarriesAFilledBackpack()
    {
        buildArea();
        pistonRow();
        const QList<QcItemStack> inside{stackOf("qtcraft:dirt", 5), {}, stackOf("qtcraft:stick", 3)};
        backpackWith(5, 0, 8, inside);
        ticks(1);
        pushEast();

        QVERIFY(isBlock(6, 0, 8, "backpack"));
        QCOMPARE(itemsOnTheGround(), 0);
        QCOMPARE(m_server->blockEntities().container(at(6, 0, 8))->stacks, padded(inside));
        QVERIFY(!m_server->blockEntities().container(at(5, 0, 8)));
    }

    void aDestroyedDroppedBackpackTakesItsContents_data()
    {
        QTest::addColumn<QString>("way");
        QTest::addRow("lava") << u"lava"_s;
        QTest::addRow("fire") << u"fire"_s;
        QTest::addRow("void") << u"void"_s;
        QTest::addRow("despawn") << u"despawn"_s;
    }

    // A deliberate difference from the reference game, which spills a shulker box's contents
    // (decision E-832): the item is gone with everything in it, nothing spills and nothing doubles.
    void aDestroyedDroppedBackpackTakesItsContents()
    {
        QFETCH(QString, way);
        buildArea();
        const QcItemStack full = packed({stackOf("qtcraft:dirt", 5), stackOf("qtcraft:stick", 2)});
        const QcBlockPos cell = at(6, 0, 6);
        QcItemEntityManager::Drop drop{full, {cell.x + 0.5, static_cast<double>(cell.y) + 0.1, cell.z + 0.5}, {}, 1000.0};
        if (way == u"void"_s) {
            drop.position.y = QcWorld::voidLimitY + 2.0;
        } else if (way == u"despawn"_s) {
            drop.age = QcItemEntityManager::despawnSeconds - 0.2;
        } else {
            put(6, 0, 6, way);
        }
        QVERIFY(m_server->items().spawn(drop, m_server->entities().newId()) != 0);
        QCOMPARE(itemsOnTheGround(), 1);
        for (int tick = 0; tick < 200 && itemsOnTheGround() > 0; ++tick) {
            ticks(1);
        }

        QCOMPARE(itemsOnTheGround(), 0);
        ticks(5);
        QCOMPARE(itemsOnTheGround(), 0);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 0);
        QCOMPARE(droppedCount(item("qtcraft:backpack")), 0);
    }
};

QTEST_GUILESS_MAIN(TestBackpackWorld)
#include "test_backpack_world.moc"
