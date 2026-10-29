// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcMachineFixture.h"

#include <functional>

using namespace Qt::StringLiterals;

// Long lines of Signal Hoppers (decision E-344) move items at the reference's rate: one item per
// 8 ticks and per hopper, straight, round corners, through chests, across chunk borders, into a
// chunk that loads only later, and in 15 lines with 2310 hoppers in all. A hopper under a line pulls
// every item that passes over it (the passing hopper rests 8 ticks after receiving, the one below
// pulls meanwhile), as in the reference: a line feeding a row of furnaces fills the first one.
class TestHopperLines : public QcMachineFixture
{
    Q_OBJECT

private:
    // The lines start here, away from the player, in world coordinates (not turned).
    QcBlockPos m_base;

    QcBlockId block(const QString &text) const { return *m_content->stateId(u"qtcraft:"_s + text); }

    int countAt(QcBlockPos pos, const char *name) const
    {
        int total = 0;
        if (const QcBlockEntities::Container *container = m_server->blockEntities().container(pos)) {
            for (const QcItemStack &slot : container->stacks) {
                total += slot.item == item(name) ? slot.count : 0;
            }
        }
        if (const QcBlockEntities::Machine *machine = m_server->blockEntities().machine(pos)) {
            for (const QcItemStack &slot : machine->stacks) {
                total += slot.item == item(name) ? slot.count : 0;
            }
        }
        return total;
    }

    // Loads and clears width x 4 x depth cells from m_base, which lies at a chunk corner.
    void prepare(int width, int depth)
    {
        buildArea();
        const QcBlockPos near = m_feet.offset(40, 3, 40);
        m_base = {near.x - near.x % 16 + (near.x < 0 ? -16 : 0), near.y, near.z - near.z % 16 + (near.z < 0 ? -16 : 0)};
        for (int x = -16; x <= width + 16; x += 16) {
            for (int z = -16; z <= depth + 16; z += 16) {
                m_server->world().loadChunk(m_base.offset(x, 0, z).chunk());
            }
        }
        for (int x = -1; x <= width; ++x) {
            for (int z = -1; z <= depth; ++z) {
                for (int y = -1; y <= 3; ++y) {
                    m_server->world().setBlock(m_base.offset(x, y, z), y < 0 ? id("qtcraft:stone") : qcAirBlockId);
                }
            }
        }
        ticks(2);
    }

    void fillAt(QcBlockPos pos, const QcItemStack &stacked)
    {
        QVERIFY(ensureEntity(pos));
        m_server->blockEntities().container(pos)->stacks[0] = stacked;
        m_server->noteContents(pos);
    }

    // Ticks until `done` holds, at most `limit` ticks; returns the ticks taken or -1.
    int ticksUntil(int limit, const std::function<bool()> &done)
    {
        for (int tick = 1; tick <= limit; ++tick) {
            ticks(1);
            if (done()) {
                return tick;
            }
        }
        return -1;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void straightLineAcrossChunkBorders()
    {
        // 40 hoppers eastwards over two chunk borders, pulling from a chest above the first one.
        constexpr int length = 40;
        prepare(length + 1, 1);
        for (int x = 0; x < length; ++x) {
            putAt(m_base.offset(x, 0, 0), block(u"signal_hopper[facing=east]"_s));
        }
        const QcBlockPos end = m_base.offset(length, 0, 0);
        putAt(end, block(u"chest[facing=south]"_s));
        putAt(m_base.offset(0, 1, 0), block(u"chest[facing=south]"_s));
        fillAt(m_base.offset(0, 1, 0), stack("qtcraft:cobblestone", 64));

        // An item needs 8 ticks per hopper; then one arrives every 8 ticks.
        const int first = ticksUntil(2 * 8 * length, [&] { return countAt(end, "qtcraft:cobblestone") > 0; });
        QVERIFY2(first >= 8 * length - 8 && first <= 8 * length + 16, qPrintable(QString::number(first)));
        ticks(160);
        const int later = countAt(end, "qtcraft:cobblestone");
        QVERIFY2(later >= 20 && later <= 22, qPrintable(QString::number(later)));
        QVERIFY(ticksUntil(64 * 8, [&] { return countAt(end, "qtcraft:cobblestone") == 64; }) > 0);
        QCOMPARE(everywhere("qtcraft:cobblestone"), 64);
    }

    void turnsAndChestsInTheLine()
    {
        // East 10, south 10 into a chest; a hopper under the chest pulls and leads 10 west into the
        // last chest.
        prepare(12, 13);
        for (int x = 0; x < 10; ++x) {
            putAt(m_base.offset(x, 1, 0), block(u"signal_hopper[facing=east]"_s));
        }
        for (int z = 0; z < 10; ++z) {
            putAt(m_base.offset(10, 1, z), block(u"signal_hopper[facing=south]"_s));
        }
        putAt(m_base.offset(10, 1, 10), block(u"chest[facing=south]"_s));
        for (int x = 10; x > 0; --x) {
            putAt(m_base.offset(x, 0, 10), block(u"signal_hopper[facing=west]"_s));
        }
        const QcBlockPos end = m_base.offset(0, 0, 10);
        putAt(end, block(u"chest[facing=south]"_s));
        fillAt(m_base.offset(0, 1, 0), stack("qtcraft:diamond", 32));

        // 31 hoppers and a chest on the way, then 32 items one every 8 ticks.
        QVERIFY(ticksUntil(31 * 8 + 32 * 8 + 32, [&] { return countAt(end, "qtcraft:diamond") == 32; }) > 0);
        QCOMPARE(everywhere("qtcraft:diamond"), 32);
    }

    void lineIntoAChunkLoadedLater()
    {
        // The last hopper of a line points into a chunk that is not loaded: the items wait in it.
        prepare(16, 1);
        for (int x = 0; x < 16; ++x) {
            putAt(m_base.offset(x, 0, 0), block(u"signal_hopper[facing=east]"_s));
        }
        const QcBlockPos end = m_base.offset(16, 0, 0);
        QVERIFY(end.chunk() != m_base.offset(15, 0, 0).chunk());
        m_server->world().takeChunk(end.chunk());
        fillAt(m_base, stack("qtcraft:diamond", 10));
        ticks(16 * 8 + 10 * 8 + 40);
        QCOMPARE(countAt(m_base.offset(15, 0, 0), "qtcraft:diamond"), 10);

        // Loaded with a chest in the way, the waiting items go on.
        m_server->world().loadChunk(end.chunk());
        putAt(end, block(u"chest[facing=south]"_s));
        QVERIFY(ticksUntil(10 * 8 + 40, [&] { return countAt(end, "qtcraft:diamond") == 10; }) > 0);
        QCOMPARE(everywhere("qtcraft:diamond"), 10);
    }

    void manyLinesOverFurnaces()
    {
        // 15 lines of 134 hoppers, each over 20 hoppers into 20 furnaces: 2310 hoppers in all, a
        // stack of raw iron dropped on the start of every line.
        constexpr int rows = 15;
        constexpr int length = 134;
        prepare(length, rows * 3);
        for (int r = 0; r < rows; ++r) {
            const QcBlockPos start = m_base.offset(0, 0, 3 * r);
            for (int x = 0; x < length; ++x) {
                putAt(start.offset(x, 2, 0), block(u"signal_hopper[facing=east]"_s));
            }
            for (int i = 0; i < 20; ++i) {
                putAt(start.offset(4 + 3 * i, 1, 0), block(u"signal_hopper[facing=down]"_s));
                putAt(start.offset(4 + 3 * i, 0, 0), block(u"furnace"_s));
            }
        }
        ticks(2);
        QCOMPARE(m_server->world().hoppers().size(), rows * (length + 20));
        for (int r = 0; r < rows; ++r) {
            QcItemEntityManager::Drop entity;
            entity.stack = stack("qtcraft:raw_iron", 64);
            entity.position = {m_base.x + 0.5, m_base.y + 3.0, m_base.z + 3 * r + 0.5};
            entity.pickupDelay = 0.0;
            QVERIFY(m_server->items().spawn(entity, m_nextItemId++) != 0);
        }

        // Every item passes 5 line hoppers and the one down into the first furnace of its line, one
        // item every 8 ticks; the furnaces further on get nothing.
        const auto firstFurnaces = [&] {
            int total = 0;
            for (int r = 0; r < rows; ++r) {
                total += countAt(m_base.offset(4, 0, 3 * r), "qtcraft:raw_iron");
            }
            return total;
        };
        QVERIFY(ticksUntil(6 * 8 + 64 * 8 + 32, [&] { return firstFurnaces() == rows * 64; }) > 0);
        QCOMPARE(everywhere("qtcraft:raw_iron"), rows * 64);
    }
};

QTEST_GUILESS_MAIN(TestHopperLines)
#include "test_hopper_lines.moc"
