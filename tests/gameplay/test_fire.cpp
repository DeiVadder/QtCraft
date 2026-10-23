#include "QcMachineFixture.h"
#include "QcTestUtil.h"
#include "server/blockentities/QcBlockEntityChecks.h"
#include "server/storage/QcBlockEntityCodec.h"

#include <QCryptographicHash>
#include <QElapsedTimer>

#include <algorithm>
#include <numeric>

using namespace Qt::StringLiterals;

// Fire (stage 34.1, decision E-392): it burns, spreads from its own queue and goes out; a fire in a
// large forest ends within a bounded time on a bounded area; the caps per chunk and world and the
// steps per tick hold; fire never spreads into chunks that are not loaded; a chunk that leaves and
// comes back goes on exactly; the game rule fireSpread and the limit fireSpreadRadius stop it;
// eternal fire burns on; the same seed burns the same way. Ticks are stepped by hand.
class TestFire : public QcMachineFixture
{
    Q_OBJECT

private:
    // Ticks after which runFire() found a cell not loaded.
    int m_unobserved = 0;

    // Forest: trees every three blocks on a stone floor, trunks four logs high under a 3 x 3 crown of
    // leaves two high, so the crowns touch and fire can travel through the whole forest.
    static constexpr int treeSpacing = 3;
    static constexpr int trunkHeight = 4;

    void set(QcBlockPos pos, const char *block) { QVERIFY(m_server->world().setBlock(pos, id(block))); }

    // Loads the chunks of a square of `size` blocks at `corner` (with a margin of one chunk).
    void loadSquare(QcBlockPos corner, int size)
    {
        for (int x = -QcWorld::chunkSize; x <= size + QcWorld::chunkSize; x += QcWorld::chunkSize) {
            for (int z = -QcWorld::chunkSize; z <= size + QcWorld::chunkSize; z += QcWorld::chunkSize) {
                m_server->world().loadChunk(corner.offset(x, 0, z).chunk());
            }
        }
    }

    void clearAbove(QcBlockPos corner, int size, int height)
    {
        for (int x = 0; x < size; ++x) {
            for (int z = 0; z < size; ++z) {
                set(corner.offset(x, -1, z), "qtcraft:stone");
                for (int y = 0; y < height; ++y) {
                    QVERIFY(m_server->world().setBlock(corner.offset(x, y, z), qcAirBlockId));
                }
            }
        }
    }

    void tree(QcBlockPos base)
    {
        for (int y = 0; y < trunkHeight; ++y) {
            set(base.offset(0, y, 0), "qtcraft:oak_log");
        }
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int y = trunkHeight - 1; y <= trunkHeight; ++y) {
                    if (dx != 0 || dz != 0 || y == trunkHeight) {
                        set(base.offset(dx, y, dz), "qtcraft:oak_leaves");
                    }
                }
            }
        }
    }

    // A forest filling a square of `size` blocks at `corner` (the stone floor below y of `corner`).
    void forest(QcBlockPos corner, int size)
    {
        loadSquare(corner, size);
        clearAbove(corner, size, trunkHeight + 3);
        for (int x = 1; x < size - 1; x += treeSpacing) {
            for (int z = 1; z < size - 1; z += treeSpacing) {
                tree(corner.offset(x, 0, z));
            }
        }
    }

    QcBlockPos forestCorner(int size) const { return m_feet.offset(-size / 2, 20, -size / 2); }

    // A cell of air on the floor east of the trunk of the tree nearest (x, z) of a forest at `corner`.
    static QcBlockPos sparkNear(QcBlockPos corner, int x, int z)
    {
        const auto trunk = [](int at) { return 1 + (at - 1) / treeSpacing * treeSpacing; };
        return corner.offset(trunk(x) + 1, 0, trunk(z));
    }

    // The same near the forest's middle.
    static QcBlockPos sparkIn(QcBlockPos corner, int size) { return sparkNear(corner, size / 2, size / 2); }

    // Steps ticks until no fire burns (or `limit` ticks passed); returns the ticks taken. `each`
    // looks at the world after every tick.
    template<typename Each>
    int burnOut(int limit, Each each)
    {
        int tick = 0;
        while (tick < limit && (m_server->fires().size() > 0 || m_server->fires().unplannedCount() > 0)) {
            m_server->tick();
            ++tick;
            each();
            if (tick % 20 == 0) {
                QcTestUtil::pump();
            }
        }
        return tick;
    }

    int burnOut(int limit)
    {
        return burnOut(limit, [] {});
    }

    // The blocks of a box (corner, size x height x size) as one fingerprint, and the cells that are
    // no longer what `before` had.
    QList<QcBlockId> blocksOf(QcBlockPos corner, int size, int height) const
    {
        QList<QcBlockId> blocks;
        for (int y = -1; y < height; ++y) {
            for (int z = 0; z < size; ++z) {
                for (int x = 0; x < size; ++x) {
                    blocks.append(m_server->world().blockAt(corner.offset(x, y, z)).value_or(static_cast<QcBlockId>(~0u)));
                }
            }
        }
        return blocks;
    }

    QList<QcBlockPos> changedCells(QcBlockPos corner, int size, int height, const QList<QcBlockId> &before) const
    {
        const QList<QcBlockId> after = blocksOf(corner, size, height);
        QList<QcBlockPos> changed;
        qsizetype index = 0;
        for (int y = -1; y < height; ++y) {
            for (int z = 0; z < size; ++z) {
                for (int x = 0; x < size; ++x, ++index) {
                    if (after[index] != before[index]) {
                        changed.append(corner.offset(x, y, z));
                    }
                }
            }
        }
        return changed;
    }

    qsizetype fireBlocksIn(QcBlockPos corner, int size, int height) const
    {
        const QList<QcBlockId> blocks = blocksOf(corner, size, height);
        return std::ranges::count_if(blocks, [this](QcBlockId block) { return m_server->systems().fire.table().of(block).fire; });
    }

    // The fingerprint of a box after a forest fire with the world seed `seed`.
    QByteArray forestFireWithSeed(qint64 seed)
    {
        QcServerConfig config;
        config.seed = seed;
        join(config);
        const int size = 40;
        const QcBlockPos corner = forestCorner(size);
        forest(corner, size);
        if (!m_server->systems().fire.ignite(sparkIn(corner, size))) {
            return {};
        }
        burnOut(4000);
        QByteArray bytes;
        const QList<QcBlockId> blocks = blocksOf(corner, size, trunkHeight + 3);
        for (const QcBlockId block : blocks) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): raw bytes of a trivially copyable value
            bytes.append(reinterpret_cast<const char *>(&block), sizeof(block));
        }
        const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        cleanup();
        return hash;
    }

private slots:
    void cleanup()
    {
        if (m_server) {
            QCOMPARE(QcBlockEntityChecks::problems(m_server->world()), QStringList{});
        }
        m_client.reset();
        m_server.reset();
        m_unloadChunks = false;
    }

    // The mandatory test (stage 34): a spark in the middle of a large forest burns out within a
    // bounded number of ticks, and everything it changed lies within fireSpreadRadius of the spark.
    void aForestFireEndsWithinBoundsOfTimeAndArea()
    {
        join();
        const int size = 72;
        const int height = trunkHeight + 3;
        const QcBlockPos corner = forestCorner(size);
        forest(corner, size);
        const QList<QcBlockId> before = blocksOf(corner, size, height);
        const QcBlockPos spark = sparkIn(corner, size);
        QVERIFY(m_server->systems().fire.ignite(spark));

        qsizetype most = 0;
        const int taken = burnOut(4000, [&] { most = std::max(most, m_server->fires().size()); });
        // Every line of fires ends after maxAge steps: a fire lit by another starts with its age.
        const int bound = (QcFireQueue::maxAge + 1) * (QcFireRules::minStepTicks + QcFireRules::stepTicksSpread);
        QVERIFY2(taken <= bound, qPrintable(u"the fire burnt %1 ticks"_s.arg(taken)));
        QCOMPARE(m_server->fires().size(), 0);
        QCOMPARE(fireBlocksIn(corner, size, height), 0);

        const int reach = static_cast<int>(QcServerLimits{}.fireSpreadRadius);
        const QList<QcBlockPos> changed = changedCells(corner, size, height, before);
        QSet<std::pair<int, int>> columns;
        for (const QcBlockPos &cell : changed) {
            QVERIFY2(QcFireRules::withinReach(spark, cell, reach), qPrintable(u"%1 %2 %3 burnt"_s.arg(cell.x).arg(cell.y).arg(cell.z)));
            columns.insert({cell.x, cell.z});
        }
        QVERIFY(columns.size() <= qsizetype(2 * reach + 1) * qsizetype(2 * reach + 1));
        // The fire did burn: a fire that never spread would prove nothing.
        const QcServerFire::Numbers &numbers = m_server->systems().fire.numbers();
        QVERIFY2(numbers.burnt >= 20 && numbers.lit >= 40, qPrintable(u"burnt %1, lit %2"_s.arg(numbers.burnt).arg(numbers.lit)));
        QVERIFY(most >= 10);
        qInfo().noquote() << u"forest fire: %1 ticks, %2 lit, %3 burnt, %4 put out, at most %5 at once, %6 columns"_s.arg(taken)
                                 .arg(numbers.lit).arg(numbers.burnt).arg(numbers.putOut).arg(most).arg(columns.size());
    }

    // Lowered caps hold in every tick: fires per chunk and in the world; fire set beyond them by
    // commands goes out in the next tick.
    void capsPerChunkAndWorldHold()
    {
        QcServerConfig config;
        config.limits.firesPerChunk = 8;
        config.limits.fires = 20;
        join(config);
        const int size = 48;
        const QcBlockPos corner = forestCorner(size);
        forest(corner, size);
        QVERIFY(m_server->systems().fire.ignite(sparkIn(corner, size)));

        bool held = true;
        burnOut(4000, [&] {
            held = held && m_server->fires().size() <= 20;
            const QList<QcFireQueue::Fire> fires = m_server->fires().fires();
            for (const QcFireQueue::Fire &fire : fires) {
                held = held && m_server->fires().countIn(fire.pos.chunk()) <= 8;
            }
        });
        QVERIFY(held);
        QVERIFY(m_server->systems().fire.numbers().refused > 0);

        // Thirty fires set straight into one chunk: eight stay, the rest go out at once.
        const QcChunkPos chunk = corner.offset(QcWorld::chunkSize, 0, QcWorld::chunkSize).chunk();
        const QcBlockPos floor{chunk.x * QcWorld::chunkSize, corner.y + trunkHeight + 2, chunk.z * QcWorld::chunkSize};
        for (int i = 0; i < 30; ++i) {
            const QcBlockPos cell = floor.offset(i % 6 * 2, 0, i / 6 * 2);
            set(cell.offset(0, -1, 0), "qtcraft:stone");
            set(cell, "qtcraft:fire");
        }
        ticks(1);
        QCOMPARE(m_server->fires().countIn(chunk), 8);
        QCOMPARE(fireBlocksIn(floor, QcWorld::chunkSize, 1), 8);
    }

    // At most fireStepsPerTick fires take a step in a tick; the rest wait and take theirs later.
    void stepsPerTickWait()
    {
        QcServerConfig config;
        config.limits.fireStepsPerTick = 16;
        join(config);
        const QcBlockPos corner = m_feet.offset(-16, 20, -8);
        loadSquare(corner, 32);
        clearAbove(corner, 32, 2);
        for (int x = 0; x < 32; ++x) {
            for (int z = 0; z < 16; ++z) {
                set(corner.offset(x, 0, z), "qtcraft:fire");
            }
        }
        ticks(1);
        QCOMPARE(m_server->fires().size(), 512);

        quint64 steps = m_server->systems().fire.numbers().steps;
        bool waited = false;
        bool withinBudget = true;
        const int taken = burnOut(1000, [&] {
            const quint64 now = m_server->systems().fire.numbers().steps;
            withinBudget = withinBudget && now - steps <= 16;
            waited = waited || m_server->fires().dueCount(m_server->tickCount()) > 0;
            steps = now;
        });
        QVERIFY(withinBudget);
        QVERIFY(waited);
        // Without fuel a fire goes out after ageWithoutFuel + 1 steps: 5120 steps at 16 per tick.
        QVERIFY2(taken < 1000, qPrintable(QString::number(taken)));
        QCOMPARE(fireBlocksIn(corner, 32, 1), 0);
    }

    // Fire at the edge of the loaded world never lights or loads the chunk beyond; with that chunk
    // loaded the same fire crosses into it.
    void fireNeverSpreadsIntoUnloadedChunks()
    {
        for (const bool neighbourLoaded : {false, true}) {
            join();
            const QcChunkPos chunk{m_feet.chunk().x + 6, m_feet.chunk().z};
            const QcChunkPos beyond{chunk.x + 1, chunk.z};
            const QcBlockPos corner{chunk.x * QcWorld::chunkSize, m_feet.y + 20, chunk.z * QcWorld::chunkSize};
            m_server->world().loadChunk(chunk);
            if (neighbourLoaded) {
                m_server->world().loadChunk(beyond);
            }
            QCOMPARE(m_server->world().chunk(beyond) != nullptr, neighbourLoaded);

            // A wall of leaves along the whole east side of the chunk, and on into the next chunk.
            const int length = neighbourLoaded ? 24 : 16;
            for (int x = 8; x < 8 + length; ++x) {
                for (int z = 4; z < 12; ++z) {
                    if (m_server->world().blockAt(corner.offset(x, -1, z))) {
                        set(corner.offset(x, -1, z), "qtcraft:stone");
                        set(corner.offset(x, 0, z), "qtcraft:oak_leaves");
                        QVERIFY(m_server->world().setBlock(corner.offset(x, 1, z), qcAirBlockId));
                    }
                }
            }
            // Sparks along the last column before the border.
            for (int z = 4; z < 12; ++z) {
                QVERIFY(m_server->systems().fire.ignite(corner.offset(QcWorld::chunkSize - 1, 1, z)));
            }
            bool crossed = false;
            burnOut(4000, [&] {
                const QList<QcFireQueue::Fire> fires = m_server->fires().fires();
                for (const QcFireQueue::Fire &fire : fires) {
                    crossed = crossed || fire.pos.chunk() == beyond;
                }
            });
            QCOMPARE(crossed, neighbourLoaded);
            QCOMPARE(m_server->world().chunk(beyond) != nullptr, neighbourLoaded);
            cleanup();
        }
    }

    // A fire whose chunk leaves the world and comes back goes on exactly where it stopped: the same
    // states as a fire that was never away, tick by tick (decision E-374).
    void aFireGoesOnWhereItStoppedAfterItsChunkWasAway()
    {
        const auto run = [this](int leaveAt, int awayTicks) {
            m_unloadChunks = true;
            QcServerConfig config;
            config.limits.fireSpreadRadius = 3;
            join(config);
            chat(u"/gamerule mobSpawning false"_s);
            // A pile of leaves and wool in the middle of the player's chunk, the spark on the floor
            // beside it: the fire stays inside the chunk.
            const QcChunkPos chunk = m_feet.chunk();
            const QcBlockPos centre{chunk.x * QcWorld::chunkSize + 8, m_feet.y + 12, chunk.z * QcWorld::chunkSize + 8};
            clearAbove(centre.offset(-8, 0, -8), QcWorld::chunkSize, 6);
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dz = -2; dz <= 2; ++dz) {
                    for (int dy = 0; dy < 3; ++dy) {
                        set(centre.offset(dx, dy, dz), (dx + dz) % 2 == 0 ? "qtcraft:oak_leaves" : "qtcraft:white_wool");
                    }
                }
            }
            const QcBlockPos spark = centre.offset(3, 0, 0);
            set(spark, "qtcraft:fire");
            QList<QcBlockPos> cells;
            for (int dx = -3; dx <= 3; ++dx) {
                for (int dz = -3; dz <= 3; ++dz) {
                    for (int dy = 0; dy <= 4; ++dy) {
                        cells.append(centre.offset(dx, dy, dz));
                    }
                }
            }
            QStringList seen = runFire(cells, 700, leaveAt, awayTicks);
            cleanup();
            return seen;
        };
        const int leaveAt = 120;
        const QStringList reference = run(-1, 0);
        const QStringList returned = run(leaveAt, 90);
        // The fire's chunk was away for a while.
        QVERIFY(m_unobserved > 60);
        QStringList expected = reference;
        expected.removeAt(leaveAt);
        QCOMPARE(returned.size(), reference.size());
        QCOMPARE(returned.first(expected.size()), expected);
        // The fire changed something after the chunk came back: equal frozen lines would prove nothing.
        QVERIFY(QSet<QString>(reference.begin() + leaveAt, reference.end()).size() > 3);
    }

    // The game rule fireSpread off and the limit fireSpreadRadius 0 each keep fire where it is:
    // nothing burns away, nothing new is lit, and the fire still goes out.
    void fireSpreadOffKeepsFireWhereItIs()
    {
        for (const bool byRule : {true, false}) {
            QcServerConfig config;
            if (!byRule) {
                config.limits.fireSpreadRadius = 0;
            }
            join(config);
            if (byRule) {
                chat(u"/gamerule fireSpread false"_s);
            }
            const int size = 24;
            const QcBlockPos corner = forestCorner(size);
            forest(corner, size);
            const QList<QcBlockId> before = blocksOf(corner, size, trunkHeight + 3);
            const QcBlockPos spark = sparkIn(corner, size);
            QVERIFY(m_server->systems().fire.ignite(spark));
            QVERIFY(!m_server->systems().fire.spreads());
            const int taken = burnOut(4000);
            QVERIFY(taken <= QcFireQueue::maxAge * (QcFireRules::minStepTicks + QcFireRules::stepTicksSpread) + 2);
            QCOMPARE(m_server->systems().fire.numbers().lit, quint64(1));
            QCOMPARE(m_server->systems().fire.numbers().burnt, quint64(0));
            QCOMPARE(changedCells(corner, size, trunkHeight + 3, before), QList<QcBlockPos>{});
            cleanup();
        }
    }

    // Fire on an eternal block (magma) burns on and never spreads; it goes out once the block is gone.
    void eternalFireBurnsOn()
    {
        join();
        const QcBlockPos base = m_feet.offset(2, 0, 2);
        set(base, "qtcraft:magma_block");
        set(base.offset(1, 1, 0), "qtcraft:oak_leaves");
        set(base.offset(1, 0, 0), "qtcraft:stone");
        QVERIFY(m_server->systems().fire.ignite(base.offset(0, 1, 0)));
        const QcBlockPos leaves = base.offset(1, 1, 0);
        for (int i = 0; i < 3000; ++i) {
            m_server->tick();
        }
        QcTestUtil::pump();
        QCOMPARE(m_server->world().blockAt(base.offset(0, 1, 0)), id("qtcraft:fire"));
        QCOMPARE(m_server->world().blockAt(base.offset(1, 1, 0)), id("qtcraft:oak_leaves"));
        QCOMPARE(m_server->fires().size(), 1);
        QCOMPARE(m_server->fires().fires().constFirst().age, quint8(0));

        // Without its ground and fuel the fire goes out with the next block update.
        chat(u"/setblock %1 %2 %3 air"_s.arg(leaves.x).arg(leaves.y).arg(leaves.z));
        ticks(1);
        QCOMPARE(m_server->world().blockAt(base.offset(0, 1, 0)), id("qtcraft:fire"));
        chat(u"/setblock %1 %2 %3 air"_s.arg(base.x).arg(base.y).arg(base.z));
        ticks(2);
        QCOMPARE(m_server->world().blockAt(base.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(m_server->fires().size(), 0);
    }

    // A saved fire that finds no room when its chunk comes back goes out; it never waits to be
    // adopted later as a new spark with a new budget once room frees.
    void aRefusedRestoredFireGoesOut()
    {
        QcServerConfig config;
        config.limits.fires = 1;
        join(config);
        const QcBlockPos held = m_feet.offset(3, 0, 0);
        QVERIFY(m_server->systems().fire.ignite(held));

        const QcBlockPos restored = m_feet.offset(-3, 0, 0);
        set(restored.offset(0, -1, 0), "qtcraft:stone");
        set(restored, "qtcraft:fire");
        QCOMPARE(m_server->fires().unplannedCount(), 1);
        const std::shared_ptr<const QcChunk> chunk = m_server->world().chunk(restored.chunk());
        QVERIFY(chunk);
        m_server->fires().restore({{restored, restored.offset(-6, 0, 0), 30, 10}}, 0, *chunk);
        QCOMPARE(m_server->fires().unplannedCount(), 0);
        QCOMPARE(m_server->fires().refusedCount(), 1);

        // Room frees before the next fire step: the refused fire still goes out.
        QVERIFY(m_server->world().setBlock(held, qcAirBlockId));
        QVERIFY(m_server->fires().hasRoom(restored.chunk()));
        m_server->tick();
        QCOMPARE(m_server->world().blockAt(restored), qcAirBlockId);
        QCOMPARE(m_server->fires().size(), 0);
        QCOMPARE(m_server->systems().fire.numbers().refused, 1u);
    }

    // Fire needs a solid block below or fuel next to it; on stone without fuel it goes out after a
    // few steps, water flowing in washes it away.
    void fireNeedsGroundAndFuel()
    {
        join();
        const QcBlockPos air = m_feet.offset(2, 2, 2);
        QVERIFY(!m_server->systems().fire.ignite(air));
        const QcBlockPos onStone = m_feet.offset(-2, 0, -2);
        QVERIFY(m_server->systems().fire.ignite(onStone));
        const int taken = burnOut(1000);
        QVERIFY(taken <= (QcFireRules::ageWithoutFuel + 1) * (QcFireRules::minStepTicks + QcFireRules::stepTicksSpread));
        QCOMPARE(m_server->world().blockAt(onStone), qcAirBlockId);

        const QcBlockPos washed = m_feet.offset(2, 0, -2);
        QVERIFY(m_server->systems().fire.ignite(washed));
        const QcBlockPos source = washed.offset(1, 0, 0);
        chat(u"/setblock %1 %2 %3 water"_s.arg(source.x).arg(source.y).arg(source.z));
        ticks(40);
        QVERIFY(m_server->world().blockAt(washed) != id("qtcraft:fire"));
        QCOMPARE(m_server->fires().contains(washed), false);
    }

    // The same seed burns a forest the same way; another seed another way.
    void theSameSeedBurnsTheSameWay()
    {
        const QByteArray first = forestFireWithSeed(7);
        const QByteArray second = forestFireWithSeed(7);
        const QByteArray other = forestFireWithSeed(8);
        QVERIFY(!first.isEmpty());
        QCOMPARE(first, second);
        QVERIFY(first != other);
    }

    // Headless measurement (decision E-392): sparks on a grid in a forest, ticked by hand as fast as
    // the server goes; prints the time of the fire phase and of the whole tick. Small in every test
    // run; QTCRAFT_FIRE_MEASURE=full burns a forest of 160 x 160 blocks with 25 sparks (Release).
    void measureForestFire()
    {
        const bool full = qEnvironmentVariable("QTCRAFT_FIRE_MEASURE") == u"full"_s;
        const int size = full ? 160 : 48;
        const int sparkSpacing = 32;
        join();
        const QcBlockPos corner = forestCorner(size);
        forest(corner, size);
        int sparks = 0;
        for (int x = sparkSpacing / 2; x < size; x += sparkSpacing) {
            for (int z = sparkSpacing / 2; z < size; z += sparkSpacing) {
                sparks += m_server->systems().fire.ignite(sparkNear(corner, x, z)) ? 1 : 0;
            }
        }
        QVERIFY(sparks > 0);

        QList<double> firePhase;
        QList<double> tick;
        qsizetype most = 0;
        QElapsedTimer wall;
        wall.start();
        burnOut(6000, [&] {
            const auto &phases = m_server->tickPhases().lastTick();
            firePhase.append(static_cast<double>(phases[static_cast<std::size_t>(QcTickPhase::Fire)]) / 1e6);
            qint64 total = 0;
            for (int phase = 0; phase < QcTickPhases::inTick; ++phase) {
                total += phases[static_cast<std::size_t>(phase)];
            }
            tick.append(static_cast<double>(total) / 1e6);
            most = std::max(most, m_server->fires().size());
        });
        QCOMPARE(m_server->fires().size(), 0);
        const auto mean = [](const QList<double> &values) { return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size()); };
        const auto maximum = [](const QList<double> &values) { return *std::ranges::max_element(values); };
        const QcServerFire::Numbers &numbers = m_server->systems().fire.numbers();
        qInfo().noquote() << u"FIRE size %1 sparks %2 ticks %3 wall %4 ms: fire phase mean %5 ms max %6 ms, tick mean %7 ms max %8 ms; "
                             u"%9 steps, %10 lit, %11 burnt, at most %12 at once"_s
                                 .arg(size).arg(sparks).arg(firePhase.size()).arg(wall.elapsed())
                                 .arg(mean(firePhase), 0, 'f', 4).arg(maximum(firePhase), 0, 'f', 3)
                                 .arg(mean(tick), 0, 'f', 4).arg(maximum(tick), 0, 'f', 3)
                                 .arg(numbers.steps).arg(numbers.lit).arg(numbers.burnt).arg(most);
    }

    // Burning fires are saved with their chunk: position, spark, age and ticks left.
    void firesAreSavedWithTheirChunk()
    {
        QcBlockEntityRecord record;
        record.chunk = {3, -2};
        record.fires = {{{50, 70, -30}, {44, 70, -28}, 7, 12}, {{49, 71, -30}, {49, 71, -30}, 0, 31}};
        const QByteArray blob = QcBlockEntityCodec::encode(record, *m_content);
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> read = QcBlockEntityCodec::decode(blob, record.chunk, *m_content, warnings);
        QVERIFY(read.has_value());
        QCOMPARE(read->fires, record.fires);
        // A fire outside the record's chunk is damage.
        record.fires.append({{10, 70, 10}, {10, 70, 10}, 0, 1});
        QVERIFY(!QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(record, *m_content), record.chunk, *m_content, warnings));
    }

private:
    // The states of `cells` after each of `count` observed ticks; with `leaveAt` the player leaves
    // before that tick and comes back `awayTicks` later. Ticks after which a cell is not loaded are
    // not observed.
    QStringList runFire(const QList<QcBlockPos> &cells, int count, int leaveAt, int awayTicks)
    {
        QStringList seen;
        const QcBlockPos home = m_feet;
        int tick = 0;
        int guard = 0;
        while (seen.size() < count && guard++ < count + awayTicks + 400) {
            if (tick == leaveAt) {
                chat(u"/tp %1 %2 %3"_s.arg(home.x + 30 * QcWorld::chunkSize).arg(home.y).arg(home.z));
            }
            if (leaveAt >= 0 && tick == leaveAt + awayTicks) {
                chat(u"/tp %1 %2 %3"_s.arg(home.x).arg(home.y).arg(home.z));
            }
            ticks(1);
            ++tick;
            QStringList parts;
            for (const QcBlockPos &cell : cells) {
                const std::optional<QcBlockId> block = m_server->world().blockAt(cell);
                if (!block) {
                    ++m_unobserved;
                    parts.clear();
                    break;
                }
                parts.append(m_content->stateName(*block));
            }
            if (!parts.isEmpty()) {
                seen.append(parts.join(u' '));
            }
        }
        return seen;
    }
};

QTEST_GUILESS_MAIN(TestFire)
#include "test_fire.moc"
