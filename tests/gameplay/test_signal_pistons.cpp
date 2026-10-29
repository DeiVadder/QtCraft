// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcCircuitFixture.h"
#include "QcTestUtil.h"
#include "server/signal/QcMovingBlocks.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

// Base content plus a piston that refuses quasi-connectivity in its data.
std::shared_ptr<const QcContentRegistry> contentWithDirectPiston()
{
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    QcBlockDefinition direct = content->block(*content->blockId(u"qtcraft:signal_piston"_s));
    direct.name = u"test:direct_piston"_s;
    direct.signal.quasiConnectivity = false;
    if (!content->registerBlock(direct)) {
        QcTestUtil::failCase(QStringLiteral("cannot register test:direct_piston"));
    }
    return content;
}

} // namespace

// Signal Pistons on a running server (decisions E-337, E-338): the push limit, immovable and breaking
// blocks, sticky pulling, infested blocks moved whole, quasi-connectivity (BUD) with the game rule and per block, 0-tick pulses
// and block spitting, chests and furnaces moved with their contents across chunk borders and through
// a save, bodies pushed, conflicting pistons, and one machine that behaves the same in all four
// turns and at other coordinates.
class TestSignalPistons : public QcCircuitFixture
{
    Q_OBJECT

private:
    int diamondsInWorld() const
    {
        int total = 0;
        const QcItemId diamond = item("qtcraft:diamond");
        const QList<QcBlockEntities::Container> containers = allContainers();
        for (const QcBlockEntities::Container &container : containers) {
            for (const QcItemStack &stack : container.stacks) {
                total += stack.item == diamond ? stack.count : 0;
            }
        }
        return total + droppedCount(diamond) + countOf(diamond);
    }

    // A chest with five diamonds at local (x, y, z).
    void chestWithDiamonds(int x, int y, int z)
    {
        put(x, y, z, u"chest[facing=south]"_s);
        QVERIFY(ensureEntity(at(x, y, z)));
        m_server->blockEntities().container(at(x, y, z))->stacks[0] = {item("qtcraft:diamond"), 5, 0};
    }

    // Records every cell of the area, turned back, after each tick.
    QStringList run(int count)
    {
        QStringList frames;
        for (int tick = 0; tick < count; ++tick) {
            ticks(1);
            QString frame;
            for (int y = 0; y < 3; ++y) {
                for (int z = 0; z < areaSize; ++z) {
                    for (int x = 0; x < areaSize; ++x) {
                        frame += look(x, y, z) + u';';
                    }
                }
            }
            frames.append(frame);
        }
        return frames;
    }

    // Lever, wire with a corner and a climb, a lamp, a repeater, a torch on a block, a sticky piston
    // with a chest, two pistons that want the same cell.
    void machine()
    {
        put(0, 0, 2, u"lever[attach=floor]"_s);
        dustLine(1, 0, 2, 3);
        put(3, 0, 3, u"signal_dust"_s);
        put(3, 0, 4, u"stone"_s);
        put(3, 1, 4, u"signal_dust"_s);
        put(3, 0, 5, u"signal_dust"_s);
        put(3, 0, 6, u"signal_repeater[facing=south,delay=2]"_s);
        put(3, 0, 7, u"signal_dust"_s);
        put(3, 0, 8, u"stone"_s);
        put(4, 0, 8, u"signal_torch[attach=west]"_s);
        put(5, 0, 8, u"sticky_signal_piston[facing=east]"_s);
        chestWithDiamonds(6, 0, 8);
        put(2, 0, 4, u"signal_lamp"_s);
        // Two repeaters on one wire power two pistons that want the same cell.
        put(9, 0, 2, u"signal_piston[facing=east]"_s);
        put(11, 0, 2, u"signal_piston[facing=west]"_s);
        put(9, 0, 3, u"signal_repeater[facing=north,delay=1]"_s);
        put(11, 0, 3, u"signal_repeater[facing=north,delay=1]"_s);
        dustLine(9, 0, 4, 3);
        put(12, 0, 4, u"lever[attach=floor]"_s);
    }

private slots:
    void initTestCase() { m_content = contentWithDirectPiston(); }

    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void pushesUpToTwelveBlocks()
    {
        buildArea();
        put(0, 0, 5, u"lever[attach=floor]"_s);
        put(1, 0, 5, u"signal_piston[facing=east]"_s);
        for (int x = 2; x < 14; ++x) {
            put(x, 0, 5, u"stone"_s);
        }
        put(0, 0, 8, u"lever[attach=floor]"_s);
        put(1, 0, 8, u"signal_piston[facing=east]"_s);
        for (int x = 2; x < 15; ++x) {
            put(x, 0, 8, u"stone"_s);
        }
        ticks(1);

        flip(0, 0, 5);
        flip(0, 0, 8);
        ticks(1);
        QVERIFY(flag(1, 0, 5, u"extended"_s));
        QVERIFY(isBlock(2, 0, 5, "signal_piston_head"));
        QVERIFY(isBlock(14, 0, 5, "stone"));
        // Thirteen are too many.
        QVERIFY(!flag(1, 0, 8, u"extended"_s));
        QVERIFY(isBlock(2, 0, 8, "stone"));

        // The client sees the stones slide as entities and gets the far cell when the slide ends.
        const auto sliding = std::ranges::count_if(m_client->spawns, [&](const QcMsg::EntitySpawn &spawn) {
            return spawn.kind == QcEntityKind::MovingBlock && spawn.block == id("qtcraft:stone");
        });
        QCOMPARE(sliding, 12);
        const QcBlockPos far = at(14, 0, 5);
        const auto arrived = [&] {
            return std::ranges::any_of(m_client->blockUpdates, [&](QcMsg::BlockUpdate update) { return update.pos == far; });
        };
        QVERIFY(!arrived());
        ticks(QcMovingBlocks::moveTicks);
        QVERIFY(arrived());
    }

    void theHeadSlidesInStepWithThePushedBlock()
    {
        // Decision E-885: the head slides out of the base like the block it pushes, starting in the
        // same tick, one cell behind it all the way; the clients get the head's cell only when the
        // slide ends, so it never shows inside the sliding block.
        buildArea();
        put(3, 0, 8, u"lever[attach=floor]"_s);
        put(4, 0, 8, u"signal_piston[facing=east]"_s);
        put(5, 0, 8, u"stone"_s);
        ticks(1);
        flip(3, 0, 8);
        ticks(1);
        QVERIFY(isBlock(5, 0, 8, "signal_piston_head"));

        const QcBlockPos base = at(4, 0, 8);
        const QcBlockPos headCell = at(5, 0, 8);
        const QList<QcMovingBlock> &moving = m_server->movingBlocks().all();
        const auto head = std::ranges::find_if(moving, [&](const QcMovingBlock &m) { return m.from == base && m.to == headCell; });
        const auto stone = std::ranges::find_if(moving, [&](const QcMovingBlock &m) { return m.from == headCell && m.to == at(6, 0, 8); });
        QVERIFY(head != moving.end());
        QVERIFY(stone != moving.end());
        QCOMPARE(head->block, *m_server->world().blockAt(headCell));
        QCOMPARE(head->start, stone->start);
        for (int tick = 0; tick <= QcMovingBlocks::moveTicks; ++tick) {
            const quint64 now = head->start + static_cast<quint64>(tick);
            QCOMPARE(QcMovingBlocks::positionAt(*stone, now).x - QcMovingBlocks::positionAt(*head, now).x, 1.0);
        }

        const QcBlockId headBlock = head->block;
        const auto shown = [&] {
            return std::ranges::any_of(m_client->blockUpdates, [&](QcMsg::BlockUpdate update) { return update.pos == headCell && update.block == headBlock; });
        };
        QVERIFY(!shown());
        ticks(QcMovingBlocks::moveTicks);
        QVERIFY(shown());
    }

    void aShortPulseStopsTheSlidingHead()
    {
        // Decision E-885: retracting before the head arrived, the head's slide ends at once and never
        // hands the clients a head; its cell stays empty for them.
        buildArea();
        put(3, 0, 11, u"lever[attach=floor]"_s);
        put(4, 0, 11, u"signal_piston[facing=east]"_s);
        ticks(1);
        flip(3, 0, 11);
        ticks(1);
        const QcBlockPos headCell = at(5, 0, 11);
        QVERIFY(m_server->movingBlocks().isMovingTo(headCell));
        flip(3, 0, 11);
        ticks(1);
        QVERIFY(!flag(4, 0, 11, u"extended"_s));
        QVERIFY(!m_server->movingBlocks().isMovingTo(headCell));
        ticks(QcMovingBlocks::moveTicks + 1);
        QVERIFY(isBlock(5, 0, 11, "air"));
        const auto last = std::ranges::find_last_if(m_client->blockUpdates, [&](QcMsg::BlockUpdate update) { return update.pos == headCell; });
        QVERIFY(last.empty() || last.front().block == qcAirBlockId);
    }

    void aStickyRetractionEmptiesTheHeadBeforeThePull()
    {
        // Decision E-885: the clients see the head's cell empty before the pulled block slides into
        // it, then the pulled block there once the slide ended.
        buildArea();
        put(3, 0, 11, u"lever[attach=floor]"_s);
        put(4, 0, 11, u"sticky_signal_piston[facing=east]"_s);
        put(5, 0, 11, u"gold_block"_s);
        ticks(1);
        flip(3, 0, 11);
        ticks(4);
        const QcBlockPos headCell = at(5, 0, 11);
        QVERIFY(isBlock(5, 0, 11, "signal_piston_head"));
        const qsizetype before = m_client->blockUpdates.size();
        flip(3, 0, 11);
        ticks(1);
        QVERIFY(isBlock(5, 0, 11, "gold_block"));
        const QList<QcMsg::BlockUpdate> updates = m_client->blockUpdates.mid(before);
        const auto emptied = std::ranges::find_if(updates, [&](QcMsg::BlockUpdate update) { return update.pos == headCell; });
        QVERIFY(emptied != updates.end());
        QCOMPARE(emptied->block, qcAirBlockId);
        ticks(QcMovingBlocks::moveTicks + 1);
        const auto last = std::ranges::find_last_if(m_client->blockUpdates, [&](QcMsg::BlockUpdate update) { return update.pos == headCell; });
        QVERIFY(!last.empty());
        QCOMPARE(last.front().block, *m_server->world().blockAt(headCell));
    }

    void immovableBlocksStopAndSmallOnesBreak()
    {
        buildArea();
        put(0, 0, 3, u"lever[attach=floor]"_s);
        put(1, 0, 3, u"signal_piston[facing=east]"_s);
        put(2, 0, 3, u"obsidian"_s);
        put(0, 0, 6, u"lever[attach=floor]"_s);
        put(1, 0, 6, u"signal_piston[facing=east]"_s);
        put(2, 0, 6, u"stone"_s);
        put(3, 0, 6, u"torch[attach=floor]"_s);
        ticks(1);

        flip(0, 0, 3);
        flip(0, 0, 6);
        ticks(1);
        QVERIFY(!flag(1, 0, 3, u"extended"_s));
        QVERIFY(flag(1, 0, 6, u"extended"_s));
        QVERIFY(isBlock(3, 0, 6, "stone"));
        QCOMPARE(droppedCount(item("qtcraft:torch")), 1);
    }

    void stickyPistonPullsItsBlockBack()
    {
        buildArea();
        put(0, 0, 5, u"lever[attach=floor]"_s);
        put(1, 0, 5, u"sticky_signal_piston[facing=east]"_s);
        put(2, 0, 5, u"gold_block"_s);
        ticks(1);
        flip(0, 0, 5);
        ticks(4);
        QVERIFY(isBlock(3, 0, 5, "gold_block"));
        flip(0, 0, 5);
        ticks(1);
        QVERIFY(!flag(1, 0, 5, u"extended"_s));
        QVERIFY(isBlock(2, 0, 5, "gold_block"));
        QVERIFY(isBlock(3, 0, 5, "air"));
    }

    void infestedBlocksMoveWithoutLettingTheirCreatureOut()
    {
        // Pushed and pulled back by a sticky piston (decision E-453): no silverfish comes out.
        buildArea();
        put(0, 0, 5, u"lever[attach=floor]"_s);
        put(1, 0, 5, u"sticky_signal_piston[facing=east]"_s);
        put(2, 0, 5, u"infested_stone"_s);
        ticks(1);
        m_server->mobs().removeAll();
        flip(0, 0, 5);
        ticks(4);
        QVERIFY(isBlock(3, 0, 5, "infested_stone"));
        flip(0, 0, 5);
        ticks(4);
        QVERIFY(isBlock(2, 0, 5, "infested_stone"));
        const int silverfish = *m_content->mobIndex(u"qtcraft:silverfish"_s);
        QCOMPARE(std::ranges::count(m_server->mobs().mobs(), silverfish, &QcCreature::type), 0);
    }

    void budPistonWaitsForAnUpdate_data()
    {
        QTest::addColumn<QString>("piston");
        QTest::addColumn<bool>("gameRule");
        QTest::addColumn<bool>("extends");
        QTest::addRow("quasi-connectivity") << u"signal_piston"_s << true << true;
        QTest::addRow("game rule off") << u"signal_piston"_s << false << false;
        QTest::addRow("off in the block's data") << u"test:direct_piston"_s << true << false;
    }

    void budPistonWaitsForAnUpdate()
    {
        QFETCH(QString, piston);
        QFETCH(bool, gameRule);
        QFETCH(bool, extends);
        buildArea();
        chat(gameRule ? u"/gamerule quasiConnectivity true"_s : u"/gamerule quasiConnectivity false"_s);
        // The lever powers the cell above the piston but nothing next to the piston itself.
        put(5, 0, 5, piston + u"[facing=north]"_s);
        put(7, 1, 5, u"stone"_s);
        put(6, 1, 5, u"lever[attach=east]"_s);
        ticks(1);

        flip(6, 1, 5);
        ticks(5);
        QVERIFY(!flag(5, 0, 5, u"extended"_s));

        // Any block update next to the piston lets it notice.
        put(4, 0, 5, u"stone"_s);
        ticks(1);
        QCOMPARE(flag(5, 0, 5, u"extended"_s), extends);
    }

    void budPistonStaysExtendedAfterThePowerGoes()
    {
        buildArea();
        put(5, 0, 5, u"signal_piston[facing=north]"_s);
        put(7, 1, 5, u"stone"_s);
        put(6, 1, 5, u"lever[attach=east]"_s);
        ticks(1);
        flip(6, 1, 5);
        ticks(1);
        put(4, 0, 5, u"stone"_s);
        ticks(1);
        QVERIFY(flag(5, 0, 5, u"extended"_s));

        // The power above goes, but nothing next to the piston changes: it stays out.
        flip(6, 1, 5);
        ticks(5);
        QVERIFY(flag(5, 0, 5, u"extended"_s));
        put(4, 0, 5, u"air"_s);
        ticks(1);
        QVERIFY(!flag(5, 0, 5, u"extended"_s));
    }

    void chunkRegeneratedInTheMiddleOfAMove()
    {
        buildArea();
        put(3, 0, 8, u"lever[attach=floor]"_s);
        put(4, 0, 8, u"signal_piston[facing=east]"_s);
        chestWithDiamonds(6, 0, 8);
        put(5, 0, 8, u"stone"_s);
        ticks(1);

        // The chest crosses into the next chunk, which is generated again while it slides.
        flip(3, 0, 8);
        ticks(1);
        const QcBlockPos arrived = at(7, 0, 8);
        QVERIFY(m_server->movingBlocks().isMovingTo(arrived));
        chat(u"/regenerate %1 %2"_s.arg(arrived.chunk().x).arg(arrived.chunk().z));
        chat(u"/confirm"_s);
        ticks(QcMovingBlocks::moveTicks + 2);
        QVERIFY(!m_server->movingBlocks().isMovingTo(arrived));
        QVERIFY(!m_server->blockEntities().container(arrived));
        QVERIFY(!isBlock(7, 0, 8, "chest"));
        // The client got the cell as it is now, not the chest.
        const auto last = std::ranges::find_last_if(m_client->blockUpdates, [arrived](QcMsg::BlockUpdate update) { return update.pos == arrived; });
        QVERIFY(!last.empty());
        QCOMPARE(last.front().block, *m_server->world().blockAt(arrived));
    }

    void zeroTickPulseSpitsTheBlock_data()
    {
        QTest::addColumn<QString>("piston");
        QTest::addRow("sticky piston lets go") << u"sticky_signal_piston"_s;
        QTest::addRow("piston pushes once") << u"signal_piston"_s;
    }

    void zeroTickPulseSpitsTheBlock()
    {
        QFETCH(QString, piston);
        buildArea();
        // The wire powers block B (4, 0, 5), which powers the piston beside it; piston A below B
        // pushes B away in the same step: the piston is powered and unpowered within one tick.
        put(1, 0, 5, u"lever[attach=floor]"_s);
        dustLine(2, 0, 5, 2);
        put(4, 0, 5, u"stone"_s);
        put(4, -1, 5, u"signal_piston[facing=up]"_s);
        put(4, 0, 6, piston + u"[facing=east]"_s);
        put(5, 0, 6, u"stone"_s);
        ticks(1);

        flip(1, 0, 5);
        ticks(1);
        QVERIFY(isBlock(4, 1, 5, "stone"));
        QVERIFY(!flag(4, 0, 6, u"extended"_s));
        QVERIFY(isBlock(5, 0, 6, "air"));
        QVERIFY(isBlock(6, 0, 6, "stone"));
    }

    void longPulseLetsTheStickyPistonPull()
    {
        buildArea();
        put(3, 0, 6, u"lever[attach=floor]"_s);
        put(4, 0, 6, u"sticky_signal_piston[facing=east]"_s);
        put(5, 0, 6, u"stone"_s);
        ticks(1);
        flip(3, 0, 6);
        ticks(3);
        flip(3, 0, 6);
        ticks(1);
        QVERIFY(isBlock(5, 0, 6, "stone"));
        QVERIFY(isBlock(6, 0, 6, "air"));
    }

    void containersMoveWithTheirContents()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        buildArea(config);

        // The chest crosses a chunk border; the furnace goes along behind it.
        put(3, 0, 8, u"lever[attach=floor]"_s);
        put(4, 0, 8, u"sticky_signal_piston[facing=east]"_s);
        put(5, 0, 8, u"furnace[facing=south]"_s);
        QVERIFY(ensureEntity(at(5, 0, 8)));
        m_server->blockEntities().machine(at(5, 0, 8))->stacks[0] = {item("qtcraft:raw_iron"), 3, 0};
        m_server->blockEntities().machine(at(5, 0, 8))->progress = 4.0;
        m_server->blockEntities().machine(at(5, 0, 8))->progressItem = item("qtcraft:raw_iron");
        chestWithDiamonds(6, 0, 8);
        QVERIFY(at(6, 0, 8).chunk() != at(7, 0, 8).chunk());
        ticks(1);

        flip(3, 0, 8);
        ticks(1);
        QVERIFY(m_server->blockEntities().container(at(7, 0, 8)));
        QVERIFY(!m_server->blockEntities().container(at(6, 0, 8)));
        // Without fuel the progress falls back slowly, as it would have in place.
        QVERIFY(m_server->blockEntities().machine(at(6, 0, 8))->progress > 3.5);
        QCOMPARE(m_server->blockEntities().machine(at(6, 0, 8))->stacks[0].count, 3);
        QCOMPARE(diamondsInWorld(), 5);

        // Saved while the clients still see the chest sliding: one chest, five diamonds after loading.
        QVERIFY(m_server->movingBlocks().isMovingTo(at(7, 0, 8)));
        QVERIFY(m_server->save().has_value());
        const QcBlockPos origin = m_origin;
        cleanup();
        join(config);
        m_origin = origin;
        QVERIFY(isBlock(7, 0, 8, "chest"));
        QCOMPARE(diamondsInWorld(), 5);
        QCOMPARE(m_server->blockEntities().container(at(7, 0, 8))->stacks[0].count, 5);

        // Pulled back: the furnace returns with its fire and ore.
        flip(3, 0, 8);
        ticks(1);
        QCOMPARE(m_server->blockEntities().machine(at(5, 0, 8))->stacks[0].count, 3);
        QVERIFY(!m_server->blockEntities().machine(at(6, 0, 8)));
        QCOMPARE(diamondsInWorld(), 5);
    }

    void openScreensCloseWhenTheirBlockMoves_data()
    {
        QTest::addColumn<int>("chests");
        QTest::addRow("alone") << 1;
        QTest::addRow("in a chain") << 2;
    }

    void openScreensCloseWhenTheirBlockMoves()
    {
        QFETCH(int, chests);
        buildArea();
        put(1, 0, 5, u"lever[attach=floor]"_s);
        put(2, 0, 5, u"signal_piston[facing=east]"_s);
        for (int i = 0; i < chests; ++i) {
            chestWithDiamonds(3 + i, 0, 5);
        }
        ticks(1);

        // The player looks into the last chest; after the push its cell holds the chest before it
        // (or nothing), so the screen must not show that one.
        const QcBlockPos opened = at(2 + chests, 0, 5);
        const QcBlockPos stand = at(2 + chests, 0, 7);
        chat(u"/tp %1 %2 %3"_s.arg(stand.x + 0.5).arg(stand.y).arg(stand.z + 0.5));
        idle(2);
        send(QcMsg::UseItem{opened, QcBlockFace::South});
        QVERIFY(!m_client->machineClosed);
        QCOMPARE(m_client->machineStacks.value(0).count, 5);

        flip(1, 0, 5);
        idle(1);
        QVERIFY(m_client->machineClosed);
        QCOMPARE(diamondsInWorld(), 5 * chests);
    }

    void bodiesArePushedAlong()
    {
        buildArea();
        put(0, 0, 10, u"lever[attach=floor]"_s);
        put(1, 0, 10, u"signal_piston[facing=east]"_s);
        put(2, 0, 10, u"stone"_s);
        ticks(1);

        // An item, an armour stand and a pig in the cell the stone moves into.
        const QcBlockPos cell = at(3, 0, 10);
        const QcVec3 centre{cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
        const quint32 dropped = m_server->items().spawnScattered({item("qtcraft:stick"), 1, 0}, centre, 800001);
        QcArmourStand stand;
        stand.id = 800002;
        stand.motion.position = centre;
        QVERIFY(m_server->stands().add(stand));
        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), centre, 800003, false, true);
        for (QcItemEntity &entity : m_server->items().editableItems()) {
            entity.motion.position = entity.id == dropped ? centre : entity.motion.position;
        }

        flip(0, 0, 10);
        ticks(1);
        QVERIFY(isBlock(3, 0, 10, "stone"));
        const double front = cell.x + 1.0;
        const auto item = std::ranges::find(m_server->items().items(), dropped, &QcItemEntity::id);
        QVERIFY(item != m_server->items().items().end());
        QVERIFY2(item->motion.position.x - 0.125 >= front - 0.05, qPrintable(QString::number(item->motion.position.x)));
        QVERIFY(m_server->stands().find(800002)->motion.position.x > front);
        QVERIFY(m_server->mobs().find(pig)->motion.position.x > front);

        // A player standing in front of the head is pushed out of its cell as well.
        const QcBlockPos playerCell = at(4, 0, 12);
        put(0, 0, 12, u"lever[attach=floor]"_s);
        put(3, 0, 12, u"signal_piston[facing=east]"_s);
        put(2, 0, 12, u"signal_dust"_s);
        put(1, 0, 12, u"signal_dust"_s);
        chat(u"/tp %1 %2 %3"_s.arg(playerCell.x + 0.5).arg(playerCell.y).arg(playerCell.z + 0.5));
        idle(2);
        flip(0, 0, 12);
        idle(2);
        QVERIFY2(m_client->motion.position.x - 0.3 >= playerCell.x + 1.0 - 0.05, qPrintable(QString::number(m_client->motion.position.x)));
    }

    void conflictingPistonsBothStay()
    {
        buildArea();
        put(1, 0, 3, u"lever[attach=floor]"_s);
        put(2, 0, 3, u"signal_piston[facing=east]"_s);
        put(4, 0, 3, u"signal_piston[facing=west]"_s);
        put(5, 0, 3, u"lever[attach=floor]"_s);
        ticks(1);
        flip(1, 0, 3);
        flip(5, 0, 3);
        ticks(2);
        QVERIFY(!flag(2, 0, 3, u"extended"_s));
        QVERIFY(!flag(4, 0, 3, u"extended"_s));
        QVERIFY(isBlock(3, 0, 3, "air"));
    }

    void loserOfAConflictLooksAgain()
    {
        buildArea();
        put(1, 0, 3, u"lever[attach=floor]"_s);
        put(2, 0, 3, u"signal_piston[facing=east]"_s);
        put(4, 0, 3, u"signal_piston[facing=west]"_s);
        put(5, 0, 3, u"lever[attach=floor]"_s);
        ticks(1);
        flip(1, 0, 3);
        flip(5, 0, 3);
        ticks(3);
        QVERIFY(!flag(2, 0, 3, u"extended"_s));
        QVERIFY(!flag(4, 0, 3, u"extended"_s));

        // One lever off: nothing changes next to the other piston, yet it extends now.
        flip(1, 0, 3);
        ticks(2);
        QVERIFY(!flag(2, 0, 3, u"extended"_s));
        QVERIFY(flag(4, 0, 3, u"extended"_s));
        QVERIFY(isBlock(3, 0, 3, "signal_piston_head"));
    }

    void sameMachineInEveryTurnAndPlace()
    {
        buildArea();
        QList<QStringList> runs;
        for (const QcBlockPos origin : {m_origin, m_origin.offset(2000, 7, -3000)}) {
            for (int turns = 0; turns < 4; ++turns) {
                clearArea(origin);
                m_turns = turns;
                machine();
                ticks(1);
                flip(0, 0, 2);
                flip(12, 0, 4);
                QStringList frames = run(30);
                flip(0, 0, 2);
                frames.append(run(30));
                runs.append(frames);
            }
        }

        // The machine did something: the lamp lit, the torch went out, the sticky piston moved.
        QVERIFY(runs.constFirst()[3].contains(u"signal_lamp_lit"_s));
        QVERIFY(runs.constFirst()[29].contains(u"signal_torch_off"_s));
        QVERIFY(runs.constFirst()[29] != runs.constFirst()[0]);
        for (int i = 1; i < runs.size(); ++i) {
            for (qsizetype tick = 0; tick < runs[0].size(); ++tick) {
                QVERIFY2(runs[i][tick] == runs[0][tick], qPrintable(u"run %1 differs in tick %2"_s.arg(i).arg(tick)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestSignalPistons)
#include "test_signal_pistons.moc"
