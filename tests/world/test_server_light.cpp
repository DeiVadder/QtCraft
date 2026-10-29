// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/light/QcLightEngine.h"
#include "common/world/QcLightVolume.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/storage/QcChunkIo.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using Channel = QcLightEngine::Channel;

namespace {

constexpr int radius = 2;

// The light of a fresh engine that loads the server world's chunks in one go: what an incremental
// engine must equal.
QString differenceToFreshEngine(const QcServerWorld &world)
{
    QcLightEngine fresh(std::make_shared<const QcLightTable>(world.content()), {});
    const QList<QcChunkPos> loaded = world.loadedChunks();
    std::vector<std::shared_ptr<const QcChunk>> keep;
    for (const QcChunkPos pos : loaded) {
        keep.push_back(world.chunk(pos));
        fresh.addChunk(keep.back());
    }

    for (const QcChunkPos pos : loaded) {
        for (int index = 0; index < 16 * 16 * QcWorld::worldHeight; ++index) {
            const QcBlockPos cell{pos.x * 16 + index % 16, index / 256, pos.z * 16 + (index / 16) % 16};
            for (const Channel channel : {Channel::Block, Channel::Sky}) {
                const int got = world.light().level(channel, cell);
                const int want = fresh.level(channel, cell);
                if (got != want) {
                    return u"%1 light at %2 %3 %4: server %5, recomputed %6"_s.arg(channel == Channel::Sky ? u"sky"_s : u"block"_s)
                        .arg(cell.x).arg(cell.y).arg(cell.z).arg(got).arg(want);
                }
            }
        }
    }
    return {};
}

} // namespace

class TestServerLight : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content;

    QcBlockId id(const char *name) const { return *m_content->blockId(u"qtcraft:"_s + QLatin1StringView(name)); }

    std::unique_ptr<QcServerWorld> makeWorld() const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcTerrainGenerator>(m_content, 42));
        for (int cx = -radius; cx <= radius; ++cx) {
            for (int cz = -radius; cz <= radius; ++cz) {
                world->loadChunk({cx, cz});
            }
        }
        return world;
    }

    // `count` random edits near the surface of the loaded square: air, stone, light sources, glass,
    // leaves, water, slabs and stairs. Returns the edited cells, none when the world refused an edit.
    QList<QcBlockPos> editAtRandom(QcServerWorld &world, QRandomGenerator &random, int count) const
    {
        const QList<QcBlockId> blocks = {qcAirBlockId, id("stone"), id("torch"), id("lantern"), id("glass"), id("oak_leaves"),
                                         id("water"), id("cobblestone_slab"), id("cobblestone_stairs")};
        QList<QcBlockPos> edited;
        for (int edit = 0; edit < count; ++edit) {
            const QcBlockPos column{random.bounded(-radius * 16, radius * 16 + 16), 0, random.bounded(-radius * 16, radius * 16 + 16)};
            const int surface = world.chunk(column.chunk())->highestNonAir(column.localX(), column.localZ());
            const QcBlockPos pos{column.x, std::clamp(surface - 4 + random.bounded(10), 0, 255), column.z};
            if (!world.setBlock(pos, blocks[static_cast<qsizetype>(random.bounded(static_cast<quint32>(blocks.size())))])) {
                return {};
            }
            edited.append(pos);
        }
        return edited;
    }

private slots:
    void initTestCase()
    {
        m_content = QcTestUtil::loadContent();
    }

    // The values the engine reads come from the content: torches, lanterns, glass, leaves, water.
    void lightValuesComeFromTheContent()
    {
        const QcLightTable table(*m_content);
        QCOMPARE(table.entry(id("torch")).emission, 14);
        QCOMPARE(table.entry(id("lantern")).emission, 15);
        QCOMPARE(table.entry(id("small_torch")).emission, 10);
        QCOMPARE(table.entry(id("stone")).opacity, 15);
        QCOMPARE(table.entry(id("glass")).skyStop, QcLightPassage::SkyStop::None);
        QCOMPARE(table.entry(id("oak_leaves")).opacity, m_content->state(id("oak_leaves")).lightOpacity);
        QCOMPARE(table.entry(id("oak_leaves")).skyStop, QcLightPassage::SkyStop::Cell);
        QCOMPARE(table.entry(id("water")).skyStop, QcLightPassage::SkyStop::Cell);
        QCOMPARE(table.entry(qcAirBlockId).skyStop, QcLightPassage::SkyStop::None);
        QVERIFY(table.hasLightSources());
    }

    // Generated terrain with caves, trees and water, edited at random: the server's light equals a
    // recompute after every round.
    void serverLightFollowsEdits()
    {
        const auto world = makeWorld();
        QCOMPARE(differenceToFreshEngine(*world), QString());

        QRandomGenerator random(99);
        for (int round = 0; round < 3; ++round) {
            QVERIFY(!editAtRandom(*world, random, 150).isEmpty());
            QCOMPARE(differenceToFreshEngine(*world), QString());
        }
    }

    // The server's block light at and around edited cells equals the flood fill the server used
    // before (spawning decisions stay the same).
    void blockLightEqualsTheFloodFill()
    {
        const auto world = makeWorld();
        QRandomGenerator random(31);
        const QList<QcBlockPos> edited = editAtRandom(*world, random, 200);
        QVERIFY(!edited.isEmpty());
        for (const QcBlockPos pos : edited) {
            for (const QcBlockPos at : {pos, pos.offset(0, 1, 0), pos.offset(2, 0, 1)}) {
                QCOMPARE(world->blockLight(at), QcLightVolume::blockLightAt(*world, at));
            }
        }
    }

    // Edited chunks unloaded and loaded again, in the middle and at the edge of the loaded square:
    // the light of the chunks around follows each step and equals a recompute.
    void serverLightFollowsUnloadedChunks()
    {
        const auto world = makeWorld();
        QRandomGenerator random(7);
        for (const QcChunkPos gone : {QcChunkPos{0, 0}, QcChunkPos{radius, -1}}) {
            QVERIFY(!editAtRandom(*world, random, 100).isEmpty());
            const std::shared_ptr<QcChunk> taken = world->takeChunk(gone);
            QCOMPARE(differenceToFreshEngine(*world), QString());
            world->adoptStoredChunks({{gone, taken}});
            QCOMPARE(differenceToFreshEngine(*world), QString());
        }
    }

    // Chunks generated in the background come with their own light prepared on the generator
    // threads; the tick joins only their borders, and the result equals a recompute.
    void generatedChunksJoinWithPreparedLight()
    {
        const auto world = makeWorld();
        QList<QcChunkPos> requested;
        for (int cx = -radius - 1; cx <= radius + 1; ++cx) {
            for (const int cz : {-radius - 1, radius + 1}) {
                world->requestChunk({cx, cz});
                requested.append({cx, cz});
            }
        }
        QVERIFY(QTest::qWaitFor([&world] {
            world->collectGeneratedChunks();
            return world->pendingGenerationCount() == 0;
        }, 30000));
        for (const QcChunkPos pos : std::as_const(requested)) {
            QVERIFY(world->chunk(pos));
        }
        QCOMPARE(differenceToFreshEngine(*world), QString());
    }

    // Chunks read from the world's storage get their light prepared on the I/O thread
    // (QcChunkIo::setPreparation); their edited blocks light the neighbours once they are back.
    void storedChunksJoinWithPreparedLight()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QcChunkIo io(dir.path(), m_content);
        QcResult<QHash<QcChunkPos, QcChunkStore::Stored>> catalog = io.open();
        QVERIFY(catalog);
        const auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcTerrainGenerator>(m_content, 42));
        world->setStorage(&io, *catalog);
        for (int cx = -radius; cx <= radius; ++cx) {
            for (int cz = -radius; cz <= radius; ++cz) {
                world->loadChunk({cx, cz});
            }
        }

        // A lantern on the border and a stone roof over a column, then the chunks leave and are kept.
        const QList<QcChunkPos> kept = {{0, 0}, {1, 0}};
        for (const QcChunkPos pos : kept) {
            const QcBlockPos column{pos.x * 16 + 15, 0, pos.z * 16 + 7};
            const int surface = world->chunk(pos)->highestNonAir(15, 7);
            QVERIFY(world->setBlock(column.offset(0, surface + 1, 0), id("lantern")));
            QVERIFY(world->setBlock(column.offset(-3, surface + 6, 0), id("stone")));
        }
        for (const QcChunkPos pos : kept) {
            QcChunkIo::Departure departure;
            departure.pos = pos;
            departure.blocks = world->takeChunk(pos);
            QVERIFY(departure.blocks);
            world->saves().noteKept(departure);
            io.keep(std::move(departure));
        }
        QCOMPARE(differenceToFreshEngine(*world), QString());

        for (const QcChunkPos pos : kept) {
            world->requestChunk(pos);
        }
        qsizetype prepared = 0;
        QVERIFY(QTest::qWaitFor([&world, &prepared] {
            for (QcServerWorld::Arrival &arrival : world->collectArrivals()) {
                prepared += arrival.light ? 1 : 0;
                world->admit(std::move(arrival));
            }
            return world->pendingGenerationCount() == 0;
        }, 30000));
        QCOMPARE(prepared, kept.size());
        for (const QcChunkPos pos : kept) {
            QVERIFY(world->saves().differsFromGenerated(pos));
        }
        QCOMPARE(differenceToFreshEngine(*world), QString());
    }

    // A regenerated chunk takes its new blocks' light.
    void regeneratedChunksAreRelit()
    {
        const auto world = makeWorld();
        const QcBlockPos top{3, world->chunk({0, 0})->highestNonAir(3, 3) + 1, 3};
        for (int y = top.y; y < top.y + 4; ++y) {
            QVERIFY(world->setBlock({3, y, 3}, id("stone")));
        }
        QVERIFY(world->setBlock({3, top.y + 4, 3}, id("lantern")));
        QCOMPARE(world->blockLight(top.offset(0, 5, 0)), 14);
        QVERIFY(world->regenerateChunk({0, 0}));
        QCOMPARE(differenceToFreshEngine(*world), QString());
        QCOMPARE(world->blockLight(top.offset(0, 5, 0)), QcLightVolume::blockLightAt(*world, top.offset(0, 5, 0)));
        QVERIFY(world->blockLight(top.offset(0, 5, 0)) < 14);
        QCOMPARE(world->skyLight(top), 15);
    }
};

QTEST_GUILESS_MAIN(TestServerLight)
#include "test_server_light.moc"
