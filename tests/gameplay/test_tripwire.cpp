// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcPlayerUseFixture.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Tripwire hooks, tripwire and the trapped chest (decisions E-782, E-783). Two hooks facing each
// other with tripwire between them form a line; anything touching the wire powers both hooks until
// it leaves, an armed wire torn by hand sets them off once, shears cut it quietly. A trapped chest
// gives as much power as players look into it. Each powered case has an unpowered twin, so nothing
// passes because everything lights.
class TestTripwire : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    // A line along x at local z: stones at x0 and x0 + wires + 3, hooks inside them, `wires` cells of
    // tripwire between, a lamp on each stone.
    void line(int x0, int z, int wires)
    {
        put(x0, 0, z, u"stone"_s);
        put(x0 + 1, 0, z, u"tripwire_hook[attach=west]"_s);
        for (int x = x0 + 2; x < x0 + 2 + wires; ++x) {
            put(x, 0, z, u"tripwire"_s);
        }
        put(x0 + 2 + wires, 0, z, u"tripwire_hook[attach=east]"_s);
        put(x0 + 3 + wires, 0, z, u"stone"_s);
        put(x0, 1, z, u"signal_lamp"_s);
        put(x0 + 3 + wires, 1, z, u"signal_lamp"_s);
    }

    // A second raw client on the server, near the first.
    std::unique_ptr<RawClient> joinOther(const QString &name)
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(name);
        QcTestUtil::pump();
        ticks(3);
        return other;
    }

    // An item lying still in the cell at local (x, y, z).
    void dropAt(int x, int y, int z)
    {
        const QcBlockPos cell = at(x, y, z);
        const QcVec3 centre{cell.x + 0.5, cell.y + 0.05, cell.z + 0.5};
        const quint32 dropped = m_server->items().spawnScattered({item("qtcraft:stick"), 1, 0}, centre, 780000 + static_cast<quint32>(cell.x & 0xFFFF));
        for (QcItemEntity &entity : m_server->items().editableItems()) {
            if (entity.id == dropped) {
                entity.motion.position = centre;
                entity.motion.velocity = {};
            }
        }
    }

    // A line along z next to the player at feet level: hooks at `feet + (2, 0, -2)` and `+2`, three
    // wires between; returns the middle wire.
    QcBlockPos lineBesidePlayer()
    {
        const QcBlockPos base = m_feet.offset(2, 0, 0);
        putAt(base.offset(0, 0, -3), id("qtcraft:stone"));
        putAt(base.offset(0, 0, 3), id("qtcraft:stone"));
        putAt(base.offset(0, 0, -2), m_content->withProperty(id("qtcraft:tripwire_hook"), u"attach"_s, u"north"_s));
        putAt(base.offset(0, 0, 2), m_content->withProperty(id("qtcraft:tripwire_hook"), u"attach"_s, u"south"_s));
        for (int dz = -1; dz <= 1; ++dz) {
            putAt(base.offset(0, 0, dz), id("qtcraft:tripwire"));
        }
        ticks(2);
        return base;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void recipes()
    {
        buildArea();
        QCOMPARE(crafted({{1, u"iron_ingot"_s}, {4, u"stick"_s}, {7, u"oak_planks"_s}}, 3), (std::pair{u"qtcraft:tripwire_hook"_s, 2}));
        QCOMPARE(crafted({{1, u"iron_nugget"_s}, {4, u"stick"_s}, {7, u"spruce_planks"_s}}, 3), (std::pair{u"qtcraft:tripwire_hook"_s, 1}));
        QCOMPARE(crafted({{0, u"chest"_s}, {1, u"tripwire_hook"_s}}, 2), (std::pair{u"qtcraft:trapped_chest"_s, 1}));
        QCOMPARE(crafted({{1, u"iron_ingot"_s}, {3, u"stone"_s}, {4, u"stone"_s}, {5, u"stone"_s}}, 3), (std::pair{u"qtcraft:stonecutter"_s, 1}));
        // A trapped chest is no chest with a hook on it twice.
        QCOMPARE(crafted({{0, u"chest"_s}}, 2), (std::pair<QString, int>{}));
    }

    void facingHooksArmTheirLine()
    {
        buildArea();
        line(0, 2, 4);
        // Twin: a hook whose wire ends in air, and one facing away.
        put(0, 0, 6, u"stone"_s);
        put(1, 0, 6, u"tripwire_hook[attach=west]"_s);
        put(2, 0, 6, u"tripwire"_s);
        put(3, 0, 6, u"tripwire"_s);
        put(5, 0, 6, u"tripwire_hook[attach=west]"_s);
        ticks(2);

        for (const int x : {1, 2, 3, 4, 5, 6}) {
            QVERIFY2(flag(x, 0, 2, u"attached"_s), qPrintable(look(x, 0, 2)));
            QVERIFY(!flag(x, 0, 2, u"powered"_s));
        }
        for (const int x : {1, 2, 3, 5}) {
            QVERIFY2(!flag(x, 0, 6, u"attached"_s), qPrintable(look(x, 0, 6)));
        }
        QVERIFY(isBlock(0, 1, 2, "signal_lamp"));

        // Breaking a hook leaves the wires loose and the other hook detached.
        put(6, 0, 2, u"air"_s);
        ticks(2);
        for (const int x : {1, 2, 3, 4, 5}) {
            QVERIFY2(!flag(x, 0, 2, u"attached"_s), qPrintable(look(x, 0, 2)));
        }
    }

    void somethingOnTheWirePowersBothHooks()
    {
        buildArea();
        line(0, 2, 5);
        line(0, 8, 5);
        ticks(2);

        dropAt(4, 0, 2);
        ticks(2);
        QVERIFY(flag(4, 0, 2, u"powered"_s));
        QVERIFY(flag(1, 0, 2, u"powered"_s));
        QVERIFY(flag(7, 0, 2, u"powered"_s));
        // Strongly into the stones they hang on: the lamps on them light.
        QVERIFY(isBlock(0, 1, 2, "signal_lamp_lit"));
        QVERIFY(isBlock(8, 1, 2, "signal_lamp_lit"));
        // The twin line without anything on it stays dark.
        QVERIFY(!flag(1, 0, 8, u"powered"_s));
        QVERIFY(isBlock(0, 1, 8, "signal_lamp"));

        // While the item lies there the hooks stay on, after it is gone they let go.
        ticks(25);
        QVERIFY(flag(1, 0, 2, u"powered"_s));
        m_server->items().removeWhere([](const QcItemEntity &) { return true; });
        ticks(12);
        QVERIFY(!flag(4, 0, 2, u"powered"_s));
        QVERIFY(!flag(1, 0, 2, u"powered"_s));
        QVERIFY(!flag(7, 0, 2, u"powered"_s));
        ticks(6);
        QVERIFY(isBlock(0, 1, 2, "signal_lamp"));
    }

    void reachIsFortyBlocks()
    {
        buildArea();
        // More room eastwards than the build area has.
        for (int x = 0; x <= 48; x += 8) {
            loadAround(at(x, 0, 0), 1);
        }
        line(0, 2, 39);
        line(0, 6, 40);
        ticks(3);
        QVERIFY(flag(1, 0, 2, u"attached"_s));
        QVERIFY(flag(41, 0, 2, u"attached"_s));
        QVERIFY(!flag(1, 0, 6, u"attached"_s));
        QVERIFY(!flag(42, 0, 6, u"attached"_s));
    }

    void tornByHandSetsTheHooksOffOnce()
    {
        join();
        const QcBlockPos base = lineBesidePlayer();
        QCOMPARE(propertyAt(base.offset(0, 0, -2), u"attached"_s), u"true"_s);
        mine(base);
        ticks(1);
        QCOMPARE(propertyAt(base.offset(0, 0, -2), u"powered"_s), u"true"_s);
        QCOMPARE(propertyAt(base.offset(0, 0, 2), u"powered"_s), u"true"_s);
        QCOMPARE(propertyAt(base.offset(0, 0, -2), u"attached"_s), u"false"_s);
        ticks(12);
        QCOMPARE(propertyAt(base.offset(0, 0, -2), u"powered"_s), u"false"_s);
        QCOMPARE(propertyAt(base.offset(0, 0, 2), u"powered"_s), u"false"_s);
        // The wire drops its string.
        QVERIFY(std::ranges::any_of(m_server->items().items(), [this](const QcItemEntity &entity) { return entity.stack.item == item("qtcraft:string"); }));
    }

    void shearsCutQuietly()
    {
        join();
        const QcBlockPos base = lineBesidePlayer();
        QCOMPARE(propertyAt(base.offset(0, 0, -2), u"attached"_s), u"true"_s);
        hold(u"shears"_s);
        mine(base);
        ticks(1);
        QCOMPARE(propertyAt(base.offset(0, 0, -2), u"powered"_s), u"false"_s);
        QCOMPARE(propertyAt(base.offset(0, 0, 2), u"powered"_s), u"false"_s);
        QCOMPARE(propertyAt(base.offset(0, 0, -2), u"attached"_s), u"false"_s);
    }

    void stringPlacesTripwireAndHooksHangOnSides()
    {
        join();
        const QcBlockPos ground = m_feet.offset(2, -1, 0);
        hold(u"string"_s, 2);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        ticks(1);
        QCOMPARE(blockNameAt(ground.offset(0, 1, 0)), u"qtcraft:tripwire"_s);
        QCOMPARE(countOf(baseItem(u"string"_s)), 1);

        // A hook goes on the side of a block, never on its top.
        const QcBlockPos wall = m_feet.offset(2, 0, 2);
        putAt(wall, id("qtcraft:stone"));
        hold(u"tripwire_hook"_s, 2);
        send(QcMsg::UseItem{wall, QcBlockFace::West});
        ticks(1);
        QCOMPARE(blockNameAt(wall.offset(-1, 0, 0)), u"qtcraft:tripwire_hook"_s);
        QCOMPARE(propertyAt(wall.offset(-1, 0, 0), u"attach"_s), u"east"_s);
        send(QcMsg::UseItem{wall, QcBlockFace::Up});
        ticks(1);
        QCOMPARE(*m_server->world().blockAt(wall.offset(0, 1, 0)), qcAirBlockId);
    }

    void aPoweredWireWithoutItsTickIsReleased()
    {
        // A wire whose release waited on an unloaded chunk comes back as a plain change without a
        // scheduled tick: the next change schedules one, so it does not stay powered.
        buildArea();
        line(0, 2, 3);
        line(0, 6, 3);
        ticks(2);
        put(3, 0, 2, u"tripwire[attached=true,east=true,west=true,powered=true]"_s);
        QVERIFY(!m_server->blockEntities().schedule().contains(at(3, 0, 2)));
        ticks(2);
        QVERIFY(flag(1, 0, 2, u"powered"_s));
        ticks(12);
        QVERIFY2(!flag(3, 0, 2, u"powered"_s), qPrintable(look(3, 0, 2)));
        QVERIFY(!flag(1, 0, 2, u"powered"_s));
        QVERIFY(!flag(6, 0, 2, u"powered"_s));
        // The twin line was never on.
        QVERIFY(!flag(1, 0, 6, u"powered"_s));
    }

    void linesAndChestPowerSurviveSaving()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        buildArea(config);
        line(0, 2, 3);
        line(0, 6, 3);
        put(5, 0, 6, u"air"_s);
        ticks(2);
        dropAt(3, 0, 2);
        ticks(2);
        QVERIFY(flag(1, 0, 2, u"powered"_s));
        // Saved with power although nobody looks in: loading sets it back.
        put(10, 0, 10, u"trapped_chest"_s);
        ticks(1);
        QVERIFY(m_server->world().setBlock(at(10, 0, 10), m_content->withProperty(*m_server->world().blockAt(at(10, 0, 10)), u"power"_s, u"2"_s)));

        QVERIFY(m_server->save().has_value());
        const QcBlockPos origin = m_origin;
        cleanup();
        join(config);
        m_origin = origin;
        loadAround(at(0, 0, 0), 1);
        loadAround(at(10, 0, 10), 1);

        for (const int x : {1, 2, 3, 4, 5}) {
            QVERIFY2(flag(x, 0, 2, u"attached"_s), qPrintable(look(x, 0, 2)));
        }
        QVERIFY(!flag(1, 0, 6, u"attached"_s));
        QCOMPARE(power(10, 0, 10), 0);
        // The saved line is still on while the item lies there and lets go when it is gone.
        QVERIFY(flag(1, 0, 2, u"powered"_s));
        m_server->items().removeWhere([](const QcItemEntity &) { return true; });
        ticks(14);
        QVERIFY2(!flag(3, 0, 2, u"powered"_s), qPrintable(look(3, 0, 2)));
        QVERIFY(!flag(1, 0, 2, u"powered"_s));
    }

    void trappedChestCountsEveryViewer()
    {
        QcServerConfig config;
        config.operators = {u"Raw"_s, u"Third"_s};
        join(config);
        const QcBlockPos trapped = m_feet.offset(2, 0, 0);
        putAt(trapped, id("qtcraft:trapped_chest"));
        ticks(1);
        std::unique_ptr<RawClient> other = joinOther(u"Other"_s);
        std::unique_ptr<RawClient> third = joinOther(u"Third"_s);
        send(QcMsg::UseItem{trapped, QcBlockFace::West});
        other->connection.send(QcMsg::UseItem{trapped, QcBlockFace::West});
        third->connection.send(QcMsg::UseItem{trapped, QcBlockFace::West});
        QcTestUtil::pump();
        ticks(2);
        QVERIFY(other->openMachine.has_value());
        QCOMPARE(propertyAt(trapped, u"power"_s), u"3"_s);

        // One leaves the game, one dies: the chest counts the one left.
        other.reset();
        QcTestUtil::pump();
        ticks(2);
        QCOMPARE(propertyAt(trapped, u"power"_s), u"2"_s);
        third->connection.send(QcMsg::ChatInput{u"/kill"_s});
        QcTestUtil::pump();
        ticks(2);
        QCOMPARE(propertyAt(trapped, u"power"_s), u"1"_s);
    }

    void trappedChestCountsItsViewers()
    {
        join();
        const QcBlockPos trapped = m_feet.offset(2, 0, 0);
        const QcBlockPos plain = m_feet.offset(2, 0, 2);
        putAt(trapped, id("qtcraft:trapped_chest"));
        putAt(trapped.offset(1, 0, 0), id("qtcraft:signal_lamp"));
        putAt(plain, id("qtcraft:chest"));
        putAt(plain.offset(1, 0, 0), id("qtcraft:signal_lamp"));
        ticks(1);

        // Looking into a plain chest powers nothing.
        send(QcMsg::UseItem{plain, QcBlockFace::West});
        ticks(2);
        QVERIFY(m_client->openMachine.has_value());
        QCOMPARE(blockNameAt(plain.offset(1, 0, 0)), u"qtcraft:signal_lamp"_s);

        send(QcMsg::CloseMachine{});
        send(QcMsg::UseItem{trapped, QcBlockFace::West});
        ticks(2);
        QCOMPARE(propertyAt(trapped, u"power"_s), u"1"_s);
        QCOMPARE(blockNameAt(trapped.offset(1, 0, 0)), u"qtcraft:signal_lamp_lit"_s);
        QCOMPARE(blockNameAt(plain.offset(1, 0, 0)), u"qtcraft:signal_lamp"_s);

        // Closed: nobody looks, no power.
        send(QcMsg::CloseMachine{});
        ticks(8);
        QCOMPARE(propertyAt(trapped, u"power"_s), u"0"_s);
        QCOMPARE(blockNameAt(trapped.offset(1, 0, 0)), u"qtcraft:signal_lamp"_s);
        // A comparator reads it like a chest.
        QCOMPARE(m_content->block(id("qtcraft:trapped_chest")).signal.reading, QcComparatorReading::Contents);
        QCOMPARE(m_content->block(id("qtcraft:trapped_chest")).containerSlots, m_content->block(id("qtcraft:chest")).containerSlots);
    }
};

QTEST_GUILESS_MAIN(TestTripwire)
#include "test_tripwire.moc"
