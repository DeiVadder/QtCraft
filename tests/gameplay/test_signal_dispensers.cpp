// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcMachineFixture.h"
#include "fixtures/QcTestUtil.h"

using namespace Qt::StringLiterals;

// Signal Dispensers and droppers on a running server (decision E-345): one item 4 ticks after rising
// power, into a container in front like a hopper, liquids poured and taken with buckets, armour put
// on stands and players, seeds only thrown (never planted, decision E-294), a random slot from a
// generator seeded by the world and the names of the items (not their load-order ids),
// quasi-connectivity per world, and actions on one cell that fail together.
class TestSignalDispensers : public QcMachineFixture
{
    Q_OBJECT

private:
    // Switches the lever at local (x, y, z) on and off again, waiting for the delay after each.
    void pulse(int x, int y, int z)
    {
        flip(x, y, z);
        ticks(6);
        flip(x, y, z);
        ticks(2);
    }

    QString blockAt(int x, int y, int z) const { return look(x, y, z); }

    // The items of a dropper with one each of nine kinds, fired nine times: the order they leave.
    QStringList fireOrder(qint64 seed = 0)
    {
        QcServerConfig config;
        config.seed = seed;
        buildArea(config);
        put(4, 0, 5, u"signal_dropper[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"chest[facing=south]"_s);
        const QList<const char *> kinds{"qtcraft:stick", "qtcraft:coal", "qtcraft:dirt", "qtcraft:cobblestone", "qtcraft:diamond",
                                        "qtcraft:paper", "qtcraft:string", "qtcraft:sugar", "qtcraft:apple"};
        QList<QcItemStack> stacks;
        for (const char *kind : kinds) {
            stacks.append(stack(kind, 1));
        }
        fill(4, 0, 5, stacks);
        ticks(2);

        QStringList order;
        for (int shot = 0; shot < 9; ++shot) {
            pulse(4, 0, 6);
            for (const char *kind : kinds) {
                if (countIn(5, 0, 5, kind) == 1 && !order.contains(QLatin1StringView(kind))) {
                    order.append(QLatin1StringView(kind));
                }
            }
        }
        return order;
    }

    // The base content after an item registered before all others: every item id is one higher.
    static std::shared_ptr<const QcContentRegistry> contentWithAnItemFirst()
    {
        auto content = std::make_shared<QcContentRegistry>();
        QcItemDefinition first;
        first.name = u"aaa:first"_s;
        first.translationKey = u"item.aaa.first"_s;
        if (!content->registerItem(first) || !QcBaseContent::loadInto(*content)) {
            QcTestUtil::failCase(QStringLiteral("cannot load the content with an item first"));
        }
        // The warnings of finishing do not matter here.
        static_cast<void>(content->finish());
        return content;
    }

    // The cod around local (5, 0, 5), where a dispenser facing east at (4, 0, 5) pours.
    QList<const QcCreature *> codInFront() const
    {
        const int cod = *m_content->mobIndex(u"qtcraft:cod"_s);
        const QcBlockPos front = at(5, 0, 5);
        const QcVec3 centre{front.x + 0.5, static_cast<double>(front.y), front.z + 0.5};
        QList<const QcCreature *> found;
        for (const QcCreature &mob : m_server->entities().creatures().mobs()) {
            if (mob.type == cod && (mob.motion.position - centre).length() < 3.0) {
                found.append(&mob);
            }
        }
        return found;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void dropperThrowsOneItemOnRisingPower()
    {
        buildArea();
        put(4, 0, 5, u"signal_dropper[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        fill(4, 0, 5, {stack("qtcraft:stick", 3)});
        ticks(2);

        flip(4, 0, 6);
        ticks(4);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 0);
        ticks(1);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 1);
        QVERIFY(flag(4, 0, 5, u"triggered"_s));
        // Staying powered fires nothing more.
        ticks(20);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:stick"), 2);

        flip(4, 0, 6);
        ticks(2);
        pulse(4, 0, 6);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:stick"), 1);
        QCOMPARE(everywhere("qtcraft:stick"), 3);
    }

    void dropperFillsAContainerInFront()
    {
        buildArea();
        put(4, 0, 5, u"signal_dropper[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"signal_hopper[facing=down,enabled=false]"_s);
        put(5, 0, 4, u"lever[attach=floor,powered=true]"_s);
        fill(4, 0, 5, {stack("qtcraft:water_bucket", 1), stack("qtcraft:water_bucket", 1)});
        ticks(2);
        pulse(4, 0, 6);
        QCOMPARE(countIn(5, 0, 5, "qtcraft:water_bucket"), 1);
        QCOMPARE(droppedCount(item("qtcraft:water_bucket")), 0);

        // Full: nothing happens, nothing is lost.
        fill(5, 0, 5, QList<QcItemStack>(5, stack("qtcraft:water_bucket", 1)));
        pulse(4, 0, 6);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:water_bucket"), 1);
        QCOMPARE(droppedCount(item("qtcraft:water_bucket")), 0);
    }

    void dispenserUsesOrThrowsInFrontOfAContainer()
    {
        // As in the reference: the dispenser does not fill the hopper; water cannot go there, so the
        // bucket is thrown out at the face.
        buildArea();
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"signal_hopper[facing=down,enabled=false]"_s);
        put(5, 0, 4, u"lever[attach=floor,powered=true]"_s);
        fill(4, 0, 5, {stack("qtcraft:water_bucket", 1)});
        ticks(2);
        pulse(4, 0, 6);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:water_bucket") + countIn(5, 0, 5, "qtcraft:water_bucket"), 0);
        QCOMPARE(everywhere("qtcraft:water_bucket"), 1);
        QCOMPARE(everywhere("qtcraft:bucket"), 0);
    }

    void dispenserReleasesTheFishOfAFishBucket()
    {
        buildArea();
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        fill(4, 0, 5, {stack("qtcraft:cod_bucket", 1)});
        ticks(2);
        pulse(4, 0, 6);
        QVERIFY2(blockAt(5, 0, 5).startsWith(u"water"_s), qPrintable(blockAt(5, 0, 5)));
        QCOMPARE(countIn(4, 0, 5, "qtcraft:bucket"), 1);
        // The cod comes out where the water goes, kept with the world (decision E-501).
        const QList<const QcCreature *> released = codInFront();
        QCOMPARE(released.size(), 1);
        QVERIFY(released.first()->persistent);
        QVERIFY(released.first()->bred);
    }

    void aFishBucketWithoutRoomStaysFull()
    {
        // Room for one cod near the dispenser, and one swims there already.
        QcServerConfig config;
        config.limits.breedingCrowd = 1;
        buildArea(config);
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        const QcBlockPos front = at(5, 0, 5);
        m_server->entities().creatures().spawn(*m_content->mobIndex(u"qtcraft:cod"_s), {front.x + 0.5, static_cast<double>(front.y), front.z + 0.5},
                                               m_server->entities().newId(), false, true);
        fill(4, 0, 5, {stack("qtcraft:cod_bucket", 1)});
        ticks(2);
        pulse(4, 0, 6);
        // Nothing poured: the full bucket is thrown out instead, the fish still inside.
        QVERIFY2(!blockAt(5, 0, 5).startsWith(u"water"_s), qPrintable(blockAt(5, 0, 5)));
        QCOMPARE(everywhere("qtcraft:cod_bucket"), 1);
        QCOMPARE(everywhere("qtcraft:bucket"), 0);
        QCOMPARE(codInFront().size(), 1);
    }

    void pouringStaysInsideTheWorld()
    {
        buildArea();
        // A dispenser at the bottom of the world facing down and one at the top facing up.
        const QcBlockPos low{m_origin.x + 2, 0, m_origin.z + 2};
        const QcBlockPos high{m_origin.x + 6, QcWorld::worldHeight - 1, m_origin.z + 2};
        for (const auto &[pos, facing] : {std::pair{low, u"down"_s}, std::pair{high, u"up"_s}}) {
            putAt(pos, *m_content->stateId(u"qtcraft:signal_dispenser[facing=%1]"_s.arg(facing)));
            putAt(pos.offset(1, 0, 0), *m_content->stateId(u"qtcraft:stone"_s));
            QVERIFY(ensureEntity(pos));
            m_server->blockEntities().container(pos)->stacks[0] = stack("qtcraft:water_bucket", 1);
        }
        ticks(2);
        for (const QcBlockPos &pos : {low, high}) {
            putAt(pos.offset(0, 0, 1), *m_content->stateId(u"qtcraft:lever[attach=east,powered=true]"_s));
        }
        ticks(10);
        // Nothing poured, no bucket emptied: both were thrown.
        QCOMPARE(everywhere("qtcraft:bucket"), 0);
        QCOMPARE(everywhere("qtcraft:water_bucket"), 2);
    }

    void scoopingWithAFullDispenserThrowsTheFilledBucket()
    {
        buildArea();
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        put(5, 0, 5, u"water"_s);
        // Every slot holds a full stack of empty buckets: the filled one finds no room.
        fill(4, 0, 5, QList<QcItemStack>(9, stack("qtcraft:bucket", 16)));
        ticks(2);
        pulse(4, 0, 6);
        QCOMPARE(countIn(4, 0, 5, "qtcraft:bucket"), 9 * 16 - 1);
        QCOMPARE(droppedCount(item("qtcraft:water_bucket")), 1);
        QVERIFY(blockAt(5, 0, 5) != u"water[falling=false,level=0]"_s);
    }

    void dropperIntoAFiringDropper()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // Both switched in the same tick: A puts its stick into B while B throws its coal.
            put(3, 0, 5, u"signal_dropper[facing=east]"_s);
            put(4, 0, 5, u"signal_dropper[facing=east]"_s);
            put(3, 0, 4, u"lever[attach=floor]"_s);
            put(4, 0, 4, u"lever[attach=floor]"_s);
            fill(3, 0, 5, {stack("qtcraft:stick", 1)});
            fill(4, 0, 5, {stack("qtcraft:coal", 1)});
            ticks(2);
            flip(3, 0, 4);
            flip(4, 0, 4);
            ticks(8);
            QCOMPARE(countIn(4, 0, 5, "qtcraft:stick"), 1);
            QCOMPARE(droppedCount(item("qtcraft:coal")), 1);
            QCOMPARE(everywhere("qtcraft:stick"), 1);
            cleanup();
        }
    }

    void dispenserPoursAndTakesWater()
    {
        buildArea();
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        fill(4, 0, 5, {stack("qtcraft:water_bucket", 1)});
        ticks(2);
        pulse(4, 0, 6);
        QCOMPARE(blockAt(5, 0, 5), u"water[falling=false,level=0]"_s);
        QCOMPARE(slotsAt(4, 0, 5).value(0), stack("qtcraft:bucket", 1));

        pulse(4, 0, 6);
        QVERIFY(blockAt(5, 0, 5) != u"water[falling=false,level=0]"_s);
        QCOMPARE(slotsAt(4, 0, 5).value(0), stack("qtcraft:water_bucket", 1));
    }

    void dropperOnlyThrowsBuckets()
    {
        buildArea();
        put(4, 0, 5, u"signal_dropper[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        fill(4, 0, 5, {stack("qtcraft:water_bucket", 1)});
        ticks(2);
        pulse(4, 0, 6);
        QCOMPARE(blockAt(5, 0, 5), u"air"_s);
        QCOMPARE(droppedCount(item("qtcraft:water_bucket")), 1);
    }

    void seedsAreThrownNeverPlanted_data()
    {
        QTest::addColumn<QString>("seed");
        QTest::newRow("wheat") << u"qtcraft:wheat_seeds"_s;
        QTest::newRow("carrot") << u"qtcraft:carrot"_s;
        QTest::newRow("potato") << u"qtcraft:potato"_s;
    }

    void seedsAreThrownNeverPlanted()
    {
        QFETCH(QString, seed);
        buildArea();
        // Facing down onto farmland, and facing a farmland cell sideways.
        put(4, -1, 5, u"farmland"_s);
        put(4, 1, 5, u"signal_dispenser[facing=down]"_s);
        put(4, 2, 5, u"lever[attach=floor]"_s);
        put(6, -1, 5, u"farmland"_s);
        put(5, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(5, 0, 4, u"lever[attach=floor]"_s);
        const QcItemStack seeds{*m_content->itemId(seed), 2, 0};
        fill(4, 1, 5, {seeds});
        fill(5, 0, 5, {seeds});
        ticks(2);

        flip(4, 2, 5);
        flip(5, 0, 4);
        ticks(5);
        QCOMPARE(blockAt(4, 0, 5), u"air"_s);
        QCOMPARE(blockAt(6, 0, 5), u"air"_s);
        QCOMPARE(droppedCount(seeds.item), 2);
    }

    void armourGoesOnAStand()
    {
        buildArea();
        put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
        put(4, 0, 6, u"lever[attach=floor]"_s);
        QcArmourStand stand;
        stand.id = 800002;
        const QcBlockPos cell = at(5, 0, 5);
        stand.motion.position = {cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
        QVERIFY(m_server->stands().add(stand));
        fill(4, 0, 5, {stack("qtcraft:iron_helmet", 1), stack("qtcraft:iron_helmet", 1)});
        ticks(2);

        pulse(4, 0, 6);
        QCOMPARE(m_server->stands().find(800002)->equipment.at(QcEquipmentSlot::Head), stack("qtcraft:iron_helmet", 1));
        // The slot is taken: the second helmet is thrown.
        pulse(4, 0, 6);
        QCOMPARE(droppedCount(item("qtcraft:iron_helmet")), 1);
    }

    void armourGoesOnAPlayer()
    {
        join();
        const QcBlockPos dispenser = m_feet.offset(-1, 0, 0);
        putAt(dispenser, *m_content->stateId(u"qtcraft:signal_dispenser[facing=east]"_s));
        putAt(dispenser.offset(0, 0, 1), *m_content->stateId(u"qtcraft:lever[attach=floor]"_s));
        QVERIFY(ensureEntity(dispenser));
        m_server->blockEntities().container(dispenser)->stacks[0] = stack("qtcraft:iron_boots", 1);
        ticks(2);
        QVERIFY(m_server->systems().circuits.use(dispenser.offset(0, 0, 1)));
        ticks(6);
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Feet), stack("qtcraft:iron_boots", 1));
    }

    void randomSlotIsSeededByTheWorld()
    {
        const QStringList first = fireOrder();
        cleanup();
        QCOMPARE(first.size(), 9);
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            QCOMPARE(fireOrder(), first);
            cleanup();
        }
    }

    // An item that sorts before the others (new content or a mod) shifts every id; the dropper still
    // fires the same items in the same order.
    void newContentKeepsTheSlotsFired()
    {
        const QStringList before = fireOrder();
        cleanup();
        const std::shared_ptr<const QcContentRegistry> base = m_content;
        m_content = contentWithAnItemFirst();
        QCOMPARE(*m_content->itemId(u"qtcraft:stick"_s), *base->itemId(u"qtcraft:stick"_s) + 1);
        const QStringList after = fireOrder();
        cleanup();
        m_content = base;
        QCOMPARE(before.size(), 9);
        QCOMPARE(after, before);
    }

    void movedDispenserFiresWhereItIs()
    {
        buildArea();
        put(2, 0, 5, u"signal_piston[facing=east]"_s);
        put(1, 0, 5, u"lever[attach=floor]"_s);
        put(3, 0, 5, u"signal_dropper[facing=south]"_s);
        put(3, 0, 6, u"lever[attach=floor]"_s);
        fill(3, 0, 5, {stack("qtcraft:stick", 1)});
        ticks(2);
        // Triggered, then pushed one block before its delay ends.
        flip(3, 0, 6);
        ticks(1);
        flip(1, 0, 5);
        ticks(8);
        QVERIFY(isBlock(4, 0, 5, "signal_dropper"));
        QCOMPARE(countIn(4, 0, 5, "qtcraft:stick"), 0);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 1);
    }

    void otherWorldsPickOtherSlots()
    {
        const QStringList first = fireOrder(1);
        cleanup();
        const QStringList second = fireOrder(2);
        cleanup();
        QCOMPARE(first.size(), 9);
        QCOMPARE(second.size(), 9);
        QVERIFY(first != second);
    }

    void creativePlayersGetArmourToo()
    {
        join();
        chat(u"/gamemode creative"_s);
        const QcBlockPos dispenser = m_feet.offset(-1, 0, 0);
        putAt(dispenser, *m_content->stateId(u"qtcraft:signal_dispenser[facing=east]"_s));
        putAt(dispenser.offset(0, 0, 1), *m_content->stateId(u"qtcraft:lever[attach=floor]"_s));
        QVERIFY(ensureEntity(dispenser));
        m_server->blockEntities().container(dispenser)->stacks[0] = stack("qtcraft:iron_helmet", 1);
        ticks(2);
        QVERIFY(m_server->systems().circuits.use(dispenser.offset(0, 0, 1)));
        ticks(6);
        QCOMPARE(m_client->equipment.at(QcEquipmentSlot::Head), stack("qtcraft:iron_helmet", 1));
    }

    void quasiConnectivityWaitsForAnUpdate_data()
    {
        QTest::addColumn<bool>("rule");
        QTest::newRow("on") << true;
        QTest::newRow("off") << false;
    }

    void quasiConnectivityWaitsForAnUpdate()
    {
        QFETCH(bool, rule);
        buildArea();
        chat(rule ? u"/gamerule quasiConnectivity true"_s : u"/gamerule quasiConnectivity false"_s);
        // The lever powers the cell above the dropper but nothing next to the dropper itself.
        put(5, 0, 5, u"signal_dropper[facing=north]"_s);
        fill(5, 0, 5, {stack("qtcraft:stick", 4)});
        put(7, 1, 5, u"stone"_s);
        put(6, 1, 5, u"lever[attach=east]"_s);
        ticks(2);
        flip(6, 1, 5);
        ticks(10);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 0);

        put(4, 0, 5, u"stone"_s);
        ticks(10);
        QCOMPARE(droppedCount(item("qtcraft:stick")), rule ? 1 : 0);
    }

    void twoDispensersOnOneCellBothFail()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            put(4, 0, 5, u"signal_dispenser[facing=east]"_s);
            put(6, 0, 5, u"signal_dispenser[facing=west]"_s);
            put(5, 0, 6, u"lever[attach=floor]"_s);
            put(4, 0, 6, u"signal_dust"_s);
            put(6, 0, 6, u"signal_dust"_s);
            fill(4, 0, 5, {stack("qtcraft:water_bucket", 1)});
            fill(6, 0, 5, {stack("qtcraft:water_bucket", 1)});
            ticks(2);
            pulse(5, 0, 6);
            QCOMPARE(blockAt(5, 0, 5), u"air"_s);
            QCOMPARE(everywhere("qtcraft:water_bucket"), 2);
            cleanup();
        }
    }
};

QTEST_GUILESS_MAIN(TestSignalDispensers)
#include "test_signal_dispensers.moc"
