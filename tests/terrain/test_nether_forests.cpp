// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/blockrules/QcBlockRules.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <QHash>
#include <QRandomGenerator>
#include <QTest>

#include <array>
#include <memory>
#include <ranges>

using namespace Qt::StringLiterals;

// The forests and deltas of the nether (decisions E-1045 to E-1047): the five biomes share the map,
// huge fungi cross chunk borders whole, plants and strands stand on what carries them, and the
// pools of the deltas hold their lava without a block update. Seeds as in decision E-846.
namespace {

constexpr int lavaLevel = 31;
constexpr int roof = 127;
constexpr std::array<std::array<int, 2>, 4> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// Generated chunks of a world, kept, read by world coordinates.
class Area
{
public:
    explicit Area(const QcNetherGenerator &generator)
        : m_generator(generator)
    {
    }

    [[nodiscard]] QcBlockId block(int x, int y, int z)
    {
        const int chunkX = x >> 4;
        const int chunkZ = z >> 4;
        return chunk(chunkX, chunkZ).block(x & 15, y, z & 15);
    }

    [[nodiscard]] const QcChunk &chunk(int x, int z)
    {
        const std::pair key{x, z};
        auto found = m_chunks.find(key);
        if (found == m_chunks.end()) {
            found = m_chunks.insert(key, m_generator.generate({x, z}));
        }
        return **found;
    }

private:
    const QcNetherGenerator &m_generator;
    QHash<std::pair<int, int>, std::shared_ptr<QcChunk>> m_chunks;
};

} // namespace

class TestNetherForests : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    static qint64 randomSeed()
    {
        static const qint64 seed = [] {
            bool ok = false;
            const qint64 fixed = qEnvironmentVariable("QC_TEST_SEED").toLongLong(&ok);
            const qint64 drawn = ok ? fixed : static_cast<qint64>(QRandomGenerator::global()->generate());
            qInfo("random seed %lld (set QC_TEST_SEED to repeat)", static_cast<long long>(drawn));
            return drawn;
        }();
        return seed;
    }

    [[nodiscard]] QcBlockId block(const QString &name) const { return *m_content->blockId(name); }
    [[nodiscard]] std::unique_ptr<QcNetherGenerator> generator(qint64 seed) const { return std::make_unique<QcNetherGenerator>(m_content, seed); }

    // The first spot whose column lies in `biome`, on a coarse search around the origin.
    [[nodiscard]] static std::optional<std::pair<int, int>> findBiome(const QcNetherGenerator &nether, const QString &biome)
    {
        for (int ring = 0; ring < 4000; ring += 32) {
            for (int step = 0; step < 16; ++step) {
                const int x = ring * (step % 4 - 2) / 2;
                const int z = ring * (step / 4 - 2) / 2;
                if (nether.biomeAt(x, z) == biome) {
                    return std::pair{x, z};
                }
            }
        }
        return std::nullopt;
    }

private:
    // Cells of wart on both sides of the border between a chunk and its east neighbour.
    int capCellsOverEastBorder(Area &area, int chunkX, int chunkZ) const
    {
        const QcBlockId wart = block(u"qtcraft:nether_wart_block"_s);
        int cells = 0;
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int y = lavaLevel; y < roof; ++y) {
                const bool here = area.chunk(chunkX, chunkZ).block(15, y, z) == wart;
                const bool there = area.chunk(chunkX + 1, chunkZ).block(0, y, z) == wart;
                cells += here && there ? 1 : 0;
            }
        }
        return cells;
    }

    // Whether every stem in the column, not counting the low buttresses at a foot, ends under its
    // cap of wart or glow; a dome is hollow, so the cap may start some blocks higher.
    bool everyStemHasItsCap(Area &area, int x, int z) const
    {
        const QcBlockId stem = block(u"qtcraft:crimson_stem"_s);
        const QList<QcBlockId> cap{block(u"qtcraft:nether_wart_block"_s), block(u"qtcraft:shroomlight"_s)};
        int run = 0;
        for (int y = lavaLevel; y < roof - 8; ++y) {
            if (m_content->isSameBlock(area.block(x, y, z), stem)) {
                ++run;
                continue;
            }
            const bool capped = std::ranges::any_of(std::views::iota(0, 7), [&](int up) { return cap.contains(area.block(x, y + up, z)); });
            if (run >= 4 && !capped) {
                return false;
            }
            run = 0;
        }
        return true;
    }

    // Cells of crimson stem on the middle row of `chunk`.
    int stemCellsIn(const QcChunk &chunk) const
    {
        const QcBlockId stem = block(u"qtcraft:crimson_stem"_s);
        int cells = 0;
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            for (int y = lavaLevel; y < roof; ++y) {
                cells += m_content->isSameBlock(chunk.block(x, y, 8), stem) ? 1 : 0;
            }
        }
        return cells;
    }

    // How many plants a chunk holds, and the first one without what carries it.
    struct HeldPlants
    {
        int count = 0;
        QString loose;
    };

    [[nodiscard]] QString looseReason(const QcChunk &chunk, const QcBlockDefinition &plant, int x, int y, int z) const
    {
        const QcBlockId carrier = chunk.block(x, plant.support == QcBlockSupport::Above ? y + 1 : y - 1, z);
        if (carrier == qcAirBlockId) {
            return u"%1 at %2 %3 %4 hangs in the air"_s.arg(plant.name).arg(x).arg(y).arg(z);
        }
        if (!plant.groundBlocks.isEmpty() && !plant.groundBlocks.contains(m_content->block(carrier).name)) {
            return u"%1 at %2 %3 %4 stands on the wrong block"_s.arg(plant.name).arg(x).arg(y).arg(z);
        }
        return {};
    }

    [[nodiscard]] bool isPlant(QcBlockId id) const
    {
        if (id == qcAirBlockId || !m_content->isValidBlock(id)) {
            return false;
        }
        const QcBlockDefinition &definition = m_content->block(id);
        return definition.support != QcBlockSupport::None && !definition.liquid && definition.name != u"qtcraft:fire"_s
               && definition.name != u"qtcraft:soul_fire"_s;
    }

    [[nodiscard]] HeldPlants heldPlantsIn(const QcChunk &chunk) const
    {
        HeldPlants held;
        for (int y = lavaLevel + 1; y < roof; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize && held.loose.isEmpty(); ++x) {
                    if (isPlant(chunk.block(x, y, z))) {
                        ++held.count;
                        held.loose = looseReason(chunk, m_content->block(chunk.block(x, y, z)), x, y, z);
                    }
                }
            }
        }
        return held;
    }

private slots:
    void fiveBiomesShareTheMap_data()
    {
        QTest::addColumn<qint64>("seed");
        QTest::newRow("seed 42") << qint64{42};
        QTest::newRow("seed 7") << qint64{7};
        QTest::newRow("random") << randomSeed();
    }

    void fiveBiomesShareTheMap()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        QHash<QString, int> columns;
        int total = 0;
        for (int z = -6000; z <= 6000; z += 60) {
            for (int x = -6000; x <= 6000; x += 60) {
                ++columns[nether->biomeAt(x, z)];
                ++total;
            }
        }
        QCOMPARE(columns.size(), 5);
        for (auto it = columns.cbegin(); it != columns.cend(); ++it) {
            const double share = static_cast<double>(it.value()) / total;
            qInfo("%s: %.1f %%", qPrintable(it.key()), 100.0 * share);
            QVERIFY2(share > 0.08 && share < 0.4, qPrintable(it.key()));
        }
    }

    // The spawn search of a nether start looks for these forests (84.7b): they must lie within
    // walking distance of the origin in any world.
    void forestsLieNearTheOrigin()
    {
        for (const qint64 seed : {qint64{1}, qint64{42}, randomSeed()}) {
            const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
            QVERIFY(findBiome(*nether, u"qtcraft:crimson_forest"_s).has_value());
            QVERIFY(findBiome(*nether, u"qtcraft:warped_forest"_s).has_value());
        }
    }

    // A chunk's part of a fungus does not depend on the order of the chunks, and stems stand
    // whole: each block of a stem's top has its cap, wherever the chunk border runs.
    void hugeFungiCrossChunkBorders()
    {
        const std::unique_ptr<QcNetherGenerator> nether = generator(42);
        const auto spot = findBiome(*nether, u"qtcraft:crimson_forest"_s);
        QVERIFY(spot.has_value());
        const int centreX = spot->first >> 4;
        const int centreZ = spot->second >> 4;

        Area area(*nether);
        int borderCells = 0;
        int stems = 0;
        for (int chunkZ = centreZ - 3; chunkZ <= centreZ + 3; ++chunkZ) {
            for (int chunkX = centreX - 3; chunkX <= centreX + 3; ++chunkX) {
                borderCells += capCellsOverEastBorder(area, chunkX, chunkZ);
                stems += stemCellsIn(area.chunk(chunkX, chunkZ));
            }
        }
        QVERIFY2(stems > 0, "no crimson stem in the forest");
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                QVERIFY2(everyStemHasItsCap(area, centreX * QcWorld::chunkSize + x, centreZ * QcWorld::chunkSize + z), "a stem ends without a cap");
            }
        }
        QVERIFY2(borderCells > 0, "no cap crosses a chunk border");

        const std::shared_ptr<QcChunk> alone = generator(42)->generate({centreX, centreZ});
        for (int y = 0; y <= roof; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    QCOMPARE(alone->block(x, y, z), area.chunk(centreX, centreZ).block(x, y, z));
                }
            }
        }
    }

    // What the forests put on floors and roofs stays when a block update comes.
    void plantsStandOnWhatCarriesThem_data()
    {
        QTest::addColumn<QString>("biome");
        QTest::newRow("crimson") << u"qtcraft:crimson_forest"_s;
        QTest::newRow("warped") << u"qtcraft:warped_forest"_s;
    }

    void plantsStandOnWhatCarriesThem()
    {
        QFETCH(QString, biome);
        const std::unique_ptr<QcNetherGenerator> nether = generator(42);
        const auto spot = findBiome(*nether, biome);
        QVERIFY(spot.has_value());
        const int centreX = spot->first >> 4;
        const int centreZ = spot->second >> 4;

        int plants = 0;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const std::shared_ptr<QcChunk> chunk = nether->generate({centreX + dx, centreZ + dz});
                const HeldPlants held = heldPlantsIn(*chunk);
                QVERIFY2(held.loose.isEmpty(), qPrintable(held.loose));
                plants += held.count;
            }
        }
        QVERIFY(plants > 50);
    }

    // A pool's cell has rock or more lava on all four sides and below, also across chunk borders,
    // so no lava runs from it, and the deltas open no springs.
    void poolsHoldTheirLava_data()
    {
        QTest::addColumn<qint64>("seed");
        QTest::newRow("seed 42") << qint64{42};
        QTest::newRow("seed 152") << qint64{152};
        QTest::newRow("seed 170") << qint64{170};
        QTest::newRow("random") << randomSeed();
    }

    void poolsHoldTheirLava()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        const auto spot = findBiome(*nether, u"qtcraft:basalt_deltas"_s);
        QVERIFY(spot.has_value());

        Area area(*nether);
        const QcBlockId lava = block(u"qtcraft:lava"_s);
        const int originX = (spot->first >> 4) * QcWorld::chunkSize;
        const int originZ = (spot->second >> 4) * QcWorld::chunkSize;
        int pools = 0;
        for (int z = originZ - 48; z < originZ + 64; ++z) {
            for (int x = originX - 48; x < originX + 64; ++x) {
                if (nether->biomeAt(x, z) != u"qtcraft:basalt_deltas"_s) {
                    continue;
                }
                for (int y = lavaLevel + 2; y < roof; ++y) {
                    if (area.block(x, y, z) != lava) {
                        continue;
                    }
                    ++pools;
                    QVERIFY(area.block(x, y - 1, z) != qcAirBlockId);
                    for (const auto &[dx, dz] : sides) {
                        QVERIFY2(area.block(x + dx, y, z + dz) != qcAirBlockId, qPrintable(u"open pool at %1 %2 %3"_s.arg(x).arg(y).arg(z)));
                    }
                }
            }
        }
        QVERIFY2(pools > 0, "no pool in the deltas");
    }

    // A pool's lava at the edge of a chunk has no opening there to look at.
    void springsStayInsideTheChunk()
    {
        const std::unique_ptr<QcNetherGenerator> nether = generator(2);
        const std::shared_ptr<QcChunk> chunk = nether->generate({-127, 111});
        const QList<QcBlockPos> springs = nether->liquidStartsIn(*chunk);
        for (const QcBlockPos &spring : springs) {
            QCOMPARE(spring.chunk(), chunk->pos());
        }
    }

    void deltasAreDarkRock()
    {
        const std::unique_ptr<QcNetherGenerator> nether = generator(42);
        const auto spot = findBiome(*nether, u"qtcraft:basalt_deltas"_s);
        QVERIFY(spot.has_value());
        const QcChunkPos pos{spot->first >> 4, spot->second >> 4};
        const std::shared_ptr<QcChunk> chunk = nether->generate(pos);

        QHash<QString, int> blocks;
        for (int y = lavaLevel + 8; y < 100; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    const QcBlockId id = chunk->block(x, y, z);
                    if (nether->biomeAt(pos.x * 16 + x, pos.z * 16 + z) == u"qtcraft:basalt_deltas"_s && m_content->isValidBlock(id)) {
                        ++blocks[m_content->block(id).name];
                    }
                }
            }
        }
        QVERIFY(blocks.value(u"qtcraft:basalt"_s) > 0);
        QVERIFY(blocks.value(u"qtcraft:blackstone"_s) > 0);
        QCOMPARE(blocks.value(u"qtcraft:netherrack"_s), 0);
    }
};

QTEST_GUILESS_MAIN(TestNetherForests)
#include "test_nether_forests.moc"
