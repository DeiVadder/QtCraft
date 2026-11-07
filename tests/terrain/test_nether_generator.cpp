// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcBlockAccess.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/nether/QcNetherGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QHash>
#include <QRandomGenerator>
#include <QTest>

#include <array>
#include <bit>
#include <optional>

using namespace Qt::StringLiterals;

// The nether generator (decisions E-1030 to E-1032): the frame of bedrock, the caves and the lava
// sea, its two biomes, what it scatters and the ruined portals. Seeds as in decision E-846: fixed
// ones plus a random one per run, in the log; QC_TEST_SEED sets it.
namespace {

constexpr int lavaLevel = 31;
constexpr int roof = 127;
// The chunks a world of the test looks at, per side.
constexpr int areaChunks = 12;

// Counts blocks by content name over an area of chunks.
class Census
{
public:
    Census(const QcNetherGenerator &generator, const QcContentRegistry &content)
    {
        for (int z = 0; z < areaChunks; ++z) {
            for (int x = 0; x < areaChunks; ++x) {
                add(*generator.generate({x - areaChunks / 2, z - areaChunks / 2}), content);
            }
        }
    }

    [[nodiscard]] qint64 count(const QString &block) const { return m_counts.value(block); }
    [[nodiscard]] qint64 airInCaves() const { return m_airInCaves; }
    [[nodiscard]] qint64 cellsInCaves() const { return m_cellsInCaves; }

private:
    void add(const QcChunk &chunk, const QcContentRegistry &content)
    {
        QHash<QcBlockId, qint64> counts;
        for (int y = 0; y <= roof; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    const QcBlockId block = chunk.block(x, y, z);
                    ++counts[block];
                    if (y > lavaLevel + 4 && y < roof - 24) {
                        ++m_cellsInCaves;
                        m_airInCaves += block == qcAirBlockId ? 1 : 0;
                    }
                }
            }
        }
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            m_counts[content.stateName(it.key()).section(u'[', 0, 0)] += it.value();
        }
    }

    QHash<QString, qint64> m_counts;
    qint64 m_airInCaves = 0;
    qint64 m_cellsInCaves = 0;
};

// The generated nether as a block access, chunk by chunk as asked for.
class Generated : public QcBlockAccess
{
public:
    Generated(const QcNetherGenerator &generator, const QcContentRegistry &content)
        : m_generator(generator)
        , m_content(content)
    {
    }

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        if (pos.y < 0 || pos.y >= QcWorld::worldHeight) {
            return qcAirBlockId;
        }
        std::shared_ptr<QcChunk> &chunk = m_chunks[pos.chunk()];
        if (!chunk) {
            chunk = m_generator.generate(pos.chunk());
        }
        return chunk->block(pos.localX(), pos.y, pos.localZ());
    }

    const QcContentRegistry &content() const override { return m_content; }

private:
    const QcNetherGenerator &m_generator;
    const QcContentRegistry &m_content;
    mutable QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;
};

// A check of a spawn apart from QcSafeStand: firm ground, an empty body and no lava, fire or
// hurting block in the 5 x 5 cells around, from below the feet to above the head. Returns what is
// wrong, empty when it is safe.
QString unsafeAt(const Generated &world, QcBlockPos feet)
{
    const QcContentRegistry &content = world.content();
    const QcBlockDefinition &ground = content.block(*world.blockAt(feet.offset(0, -1, 0)));
    if (!ground.solid || ground.liquid || ground.contactDamage > 0.0) {
        return u"ground "_s + ground.name;
    }
    for (int dy = 0; dy <= 1; ++dy) {
        const QcBlockDefinition &body = content.block(*world.blockAt(feet.offset(0, dy, 0)));
        if (body.solid || body.liquid) {
            return u"body "_s + body.name;
        }
    }
    for (int cell = 0; cell < 5 * 5 * 4; ++cell) {
        const QcBlockPos at = feet.offset(cell % 5 - 2, cell / 25 - 1, cell / 5 % 5 - 2);
        const QcBlockDefinition &near = content.block(*world.blockAt(at));
        if (near.isLava() || near.fire.isFire || near.contactDamage > 0.0) {
            return u"near "_s + near.name;
        }
    }
    return {};
}

} // namespace

class TestNetherGenerator : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // The random seed of this run besides the fixed ones (decision E-846): QC_TEST_SEED if set.
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

    // A bit for each side of `cell` that is open: a natural fire may burn in the opening.
    [[nodiscard]] int openSidesOf(const QcChunk &chunk, QcBlockPos cell) const
    {
        static constexpr std::array<std::array<int, 2>, 4> steps{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        int open = 0;
        for (std::size_t side = 0; side < steps.size(); ++side) {
            const QcBlockId next = chunk.block(cell.localX() + steps[side][0], cell.y, cell.localZ() + steps[side][1]);
            open |= m_content->block(next).solid ? 0 : 1 << side;
        }
        return open;
    }

    // The block a fall from the open side of `cell` lands on.
    [[nodiscard]] QcBlockId landingOf(const QcChunk &chunk, QcBlockPos cell) const
    {
        static constexpr std::array<std::array<int, 2>, 4> steps{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
        const std::size_t side = static_cast<std::size_t>(std::countr_zero(static_cast<unsigned>(openSidesOf(chunk, cell))));
        const int x = cell.localX() + steps[side][0];
        const int z = cell.localZ() + steps[side][1];
        int y = cell.y;
        while (y > 0 && chunk.block(x, y, z) == qcAirBlockId) {
            --y;
        }
        return chunk.block(x, y, z);
    }

    // A spring high enough to pass over the arrival station, in the first chunks that have one.
    [[nodiscard]] std::optional<QcBlockPos> highSpring(const QcNetherGenerator &nether) const
    {
        for (int z = -6; z < 6; ++z) {
            for (int x = -6; x < 6; ++x) {
                const std::shared_ptr<QcChunk> chunk = nether.generate({x, z});
                const QList<QcBlockPos> cells = nether.liquidStartsIn(*chunk);
                for (const QcBlockPos &cell : cells) {
                    if (cell.y > lavaLevel + 14 && landingOf(*chunk, cell) == block(u"qtcraft:lava"_s)) {
                        return cell;
                    }
                }
            }
        }
        return std::nullopt;
    }

    // How many cells of `chunk` hold each block state.
    static QHash<QcBlockId, int> statesOf(const QcChunk &chunk)
    {
        QHash<QcBlockId, int> states;
        for (int y = 0; y < roof; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    ++states[chunk.block(x, y, z)];
                }
            }
        }
        return states;
    }

private slots:
    void seeds_data()
    {
        QTest::addColumn<qint64>("seed");
        for (const qint64 seed : {qint64{42}, qint64{7}, qint64{1110}, qint64{20261105}, randomSeed()}) {
            QTest::addRow("seed %lld", static_cast<long long>(seed)) << seed;
        }
    }

    // Bedrock closes the floor and the roof, nothing lies above the roof, lava fills the sea and
    // only the sea; the caves leave a good share of the height open.
    void theFrameHolds_data() { seeds_data(); }
    void theFrameHolds()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        const QcBlockId bedrock = block(u"qtcraft:bedrock"_s);
        for (const QcChunkPos pos : {QcChunkPos{0, 0}, QcChunkPos{-5, 3}, QcChunkPos{40, -17}}) {
            const std::shared_ptr<QcChunk> chunk = nether->generate(pos);
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    QCOMPARE(chunk->block(x, 0, z), bedrock);
                    QCOMPARE(chunk->block(x, roof, z), bedrock);
                    for (int y = roof + 1; y < QcWorld::worldHeight; ++y) {
                        QCOMPARE(chunk->block(x, y, z), qcAirBlockId);
                    }
                    for (int y = 1; y <= lavaLevel; ++y) {
                        QVERIFY2(chunk->block(x, y, z) != qcAirBlockId, "air below the lava's surface");
                    }
                }
            }
        }

        const Census census(*nether, *m_content);
        QVERIFY(census.count(u"qtcraft:lava"_s) > 1000);
        const double open = static_cast<double>(census.airInCaves()) / static_cast<double>(census.cellsInCaves());
        qInfo("open share between lava and roof: %.3f", open);
        QVERIFY2(open > 0.3 && open < 0.8, qPrintable(QString::number(open)));
    }

    // The single question and the chunk agree, cell by cell, on the bare rock.
    void theColumnsMatchTheChunk()
    {
        const std::unique_ptr<QcNetherGenerator> nether = generator(42);
        const QcChunkPos pos{3, -2};
        QcChunk chunk(pos);
        nether->terrain().fill(chunk);
        for (int y = 1; y < roof; y += 3) {
            for (const auto &[x, z] : {std::pair{0, 0}, {5, 9}, {15, 15}, {7, 2}}) {
                const bool solid = chunk.block(x, y, z) != qcAirBlockId && chunk.block(x, y, z) != block(u"qtcraft:lava"_s);
                QCOMPARE(nether->terrain().isSolid(pos.x * QcWorld::chunkSize + x, y, pos.z * QcWorld::chunkSize + z), solid);
            }
        }
    }

    // Same seed, same world, in any order of the chunks; another seed, another world.
    // The patches of the ground are the world's own, not the same in every world.
    void patchesDependOnTheSeed()
    {
        const std::unique_ptr<QcNetherGenerator> one = generator(42);
        const std::unique_ptr<QcNetherGenerator> other = generator(43);
        const QcNetherDefinitions::Biome &biome = one->definitions().biomes.first();
        int differences = 0;
        for (int x = 0; x < 64; ++x) {
            differences += one->decorator().patchRoll(biome, x, 3) != other->decorator().patchRoll(biome, x, 3) ? 1 : 0;
        }
        QVERIFY(differences > 32);
    }

    void theWorldDependsOnTheSeedOnly()
    {
        const std::unique_ptr<QcNetherGenerator> one = generator(42);
        const std::unique_ptr<QcNetherGenerator> two = generator(42);
        const std::unique_ptr<QcNetherGenerator> other = generator(43);
        const QcChunkPos pos{2, 2};
        Q_UNUSED(two->generate({9, 9}));
        const std::shared_ptr<QcChunk> first = one->generate(pos);
        const std::shared_ptr<QcChunk> second = two->generate(pos);
        const std::shared_ptr<QcChunk> third = other->generate(pos);
        int differences = 0;
        for (int y = 1; y < roof; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    QCOMPARE(first->block(x, y, z), second->block(x, y, z));
                    differences += first->block(x, y, z) != third->block(x, y, z) ? 1 : 0;
                }
            }
        }
        QVERIFY(differences > 1000);
    }

    // The biomes lie on the map, the soul valley in a fair share, with the soul valley's own ground.
    void theBiomesShareTheMap_data() { seeds_data(); }
    void theBiomesShareTheMap()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        QHash<QString, int> columns;
        constexpr int side = 96;
        for (int z = 0; z < side; ++z) {
            for (int x = 0; x < side; ++x) {
                ++columns[nether->biomeAt((x - side / 2) * 32, (z - side / 2) * 32)];
            }
        }
        QCOMPARE(columns.size(), 5);
        const double valley = static_cast<double>(columns.value(u"qtcraft:soul_sand_valley"_s)) / (side * side);
        qInfo("soul sand valley: %.3f of the map", valley);
        QVERIFY2(valley > 0.08 && valley < 0.5, qPrintable(QString::number(valley)));
    }

    // Everything the nether scatters is there in a dozen chunks square.
    void theFeaturesAreThere_data() { seeds_data(); }
    void theFeaturesAreThere()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        const Census census(*nether, *m_content);
        for (const QString &name : {u"qtcraft:glowstone"_s, u"qtcraft:nether_quartz_ore"_s, u"qtcraft:nether_gold_ore"_s, u"qtcraft:gravel"_s,
                                    u"qtcraft:soul_sand"_s, u"qtcraft:fire"_s}) {
            qInfo("%s: %lld", qPrintable(name), static_cast<long long>(census.count(name)));
            QVERIFY2(census.count(name) > 0, qPrintable(name));
        }
        QVERIFY(census.count(u"qtcraft:nether_quartz_ore"_s) > census.count(u"qtcraft:nether_gold_ore"_s));
    }

    // A spring sits in a wall with one open side, its liquid ready to run.
    void springsOpenOnOneSide_data() { seeds_data(); }
    void springsOpenOnOneSide()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        int sides = 0;
        const QcBlockId lava = block(u"qtcraft:lava"_s);
        int springs = 0;
        for (int z = -6; z < 6; ++z) {
            for (int x = -6; x < 6; ++x) {
                const std::shared_ptr<QcChunk> chunk = nether->generate({x, z});
                const QList<QcBlockPos> cells = nether->liquidStartsIn(*chunk);
                for (const QcBlockPos &cell : cells) {
                    QVERIFY(cell.y > lavaLevel);
                    QCOMPARE(chunk->block(cell.localX(), cell.y, cell.localZ()), lava);
                    const int open = openSidesOf(*chunk, cell);
                    QVERIFY(open != 0);
                    sides |= open;
                }
                springs += static_cast<int>(cells.size());
            }
        }
        qInfo("springs in 144 chunks: %d", springs);
        QVERIFY(springs < 4 || std::popcount(static_cast<unsigned>(sides)) >= 2);
        QVERIFY(springs > 0);
    }

    // A station built under a fall takes the spring away: no lava lands on a structure and spreads.
    void noSpringFallsOntoTheStation()
    {
        const std::optional<QcBlockPos> spring = highSpring(*generator(42));
        QVERIFY(spring.has_value());
        const QcNetherGenerator nether(m_content, 42, QcBlockPos{spring->x, lavaLevel + 1, spring->z});
        const std::shared_ptr<QcChunk> chunk = nether.generate(spring->chunk());
        const QList<QcBlockPos> cells = nether.liquidStartsIn(*chunk);
        for (const QcBlockPos &cell : cells) {
            QCOMPARE(landingOf(*chunk, cell), block(u"qtcraft:lava"_s));
        }
    }

    // Ruined portals stand on a floor above the lava; /locate finds them, their chest is furnished.
    void ruinedPortalsStandAboveTheLava_data() { seeds_data(); }
    void ruinedPortalsStandAboveTheLava()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        QVERIFY(nether->locatableNames().contains(u"ruined_portal_nether"_s));
        const std::optional<QcStructurePlacement> portal = nether->nearestStructure(u"qtcraft:ruined_portal_nether"_s, {0, 64, 0}, 2048);
        QVERIFY(portal);
        QVERIFY(portal->origin().y > lavaLevel - 1);
        // The ruin may lie in the chunks around the corner of its plan.
        int obsidian = 0;
        for (int z = -1; z <= 1; ++z) {
            for (int x = -1; x <= 1; ++x) {
                const QcChunkPos corner = portal->origin().chunk();
                obsidian += statesOf(*nether->generate({corner.x + x, corner.z + z})).value(block(u"qtcraft:obsidian"_s));
            }
        }
        QVERIFY(obsidian > 0);

        const QcBlockPos at = portal->toWorld(portal->structure().loot.first().at);
        const QcChunkFurnishing furnishing = nether->furnishingIn(at.chunk());
        QCOMPARE(furnishing.chests.size(), 1);
        QCOMPARE(furnishing.chests.first().at, at);
    }

    // The spawn stands on rock with room above, over the lava.
    void theSpawnHasGround_data() { seeds_data(); }
    void theSpawnHasGround()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
        const QcBlockPos spawn = nether->spawnPoint();
        const std::shared_ptr<QcChunk> chunk = nether->generate(spawn.chunk());
        QVERIFY(spawn.y > lavaLevel);
        QVERIFY(nether->isNaturalGround(chunk->block(spawn.localX(), spawn.y - 1, spawn.localZ())));
        QVERIFY(!nether->terrain().isSolid(spawn.x, spawn.y, spawn.z));
        QVERIFY(!nether->terrain().isSolid(spawn.x, spawn.y + 1, spawn.z));
        const QString unsafe = unsafeAt(Generated(*nether, *m_content), spawn);
        QVERIFY2(unsafe.isEmpty(), qPrintable(unsafe));
    }

    // The spawn search looks at the real blocks (decision E-1111): over many seeds no spawn stands
    // on or near lava, magma or fire, and most lie in a forest.
    void theSpawnIsSafeOverManySeeds()
    {
        const qint64 first = randomSeed();
        int inForest = 0;
        constexpr int seeds = 60;
        qint64 slowest = 0;
        for (qint64 seed = first; seed < first + seeds; ++seed) {
            const std::unique_ptr<QcNetherGenerator> nether = generator(seed);
            QElapsedTimer timer;
            timer.start();
            const QcBlockPos spawn = nether->spawnPoint();
            slowest = std::max(slowest, timer.elapsed());
            const QString unsafe = unsafeAt(Generated(*nether, *m_content), spawn);
            QVERIFY2(unsafe.isEmpty(), qPrintable(u"seed %1: %2"_s.arg(seed).arg(unsafe)));
            const QString biome = nether->biomeAt(spawn.x, spawn.z);
            inForest += biome == u"qtcraft:crimson_forest"_s || biome == u"qtcraft:warped_forest"_s ? 1 : 0;
        }
        qInfo("%d of %d spawns in a forest, the slowest search %lld ms", inForest, seeds, slowest);
        QVERIFY(inForest * 2 > seeds);
    }

    // The overworld's terrain never picks a biome or an ore of the nether.
    void theOverworldKeepsToItself()
    {
        const QcTerrainGenerator terrain(m_content, 42);
        for (int z = -4000; z < 4000; z += 97) {
            for (int x = -4000; x < 4000; x += 97) {
                const QString biome = terrain.biomeAt(x, z);
                QVERIFY2(biome != u"qtcraft:nether_wastes"_s && biome != u"qtcraft:soul_sand_valley"_s, qPrintable(biome));
            }
        }
        const std::shared_ptr<QcChunk> chunk = terrain.generate({0, 0});
        for (int y = 0; y < 100; ++y) {
            QVERIFY(chunk->block(3, y, 3) != block(u"qtcraft:nether_quartz_ore"_s));
        }
    }

    // A world cannot be created as a nether; dimensions can use the generator.
    void theGeneratorIsForDimensions()
    {
        const QString nether = QString::fromLatin1(QcWorldGenerators::nether);
        QVERIFY(!QcWorldGenerators::types().contains(nether));
        QVERIFY(!QcWorldGenerators::isKnown(nether));
        QVERIFY(QcWorldGenerators::checkOptions(nether, {}, *m_content));
        QVERIFY(!QcWorldGenerators::checkOptions(nether, u"bedrock"_s, *m_content));
        QCOMPARE(QString::fromLatin1(QcWorldGenerators::versionSeries(nether).name), u"nether"_s);
        QCOMPARE(QcWorldGenerators::versionSeries(nether).forLateDimension(), 1);
    }

    // Places worth a look, for the screenshots of the package (printed only).
    void placesToLookAt()
    {
        const std::unique_ptr<QcNetherGenerator> nether = generator(42);
        const QcNetherTerrain &terrain = nether->terrain();
        for (int z = -640; z <= 640; z += 32) {
            for (int x = -640; x <= 640; x += 32) {
                const std::optional<int> floor = terrain.floorAt(x, z, 0, 6);
                if (floor && nether->biomeAt(x, z) == u"qtcraft:soul_sand_valley"_s && x % 128 == 0 && z % 128 == 0) {
                    qInfo("valley floor %d %d %d", x, *floor, z);
                }
            }
        }
        const std::optional<QcStructurePlacement> portal = nether->nearestStructure(u"qtcraft:ruined_portal_nether"_s, {0, 64, 0}, 2048);
        QVERIFY(portal);
        qInfo("portal %d %d %d, spawn %d %d %d", portal->origin().x, portal->origin().y, portal->origin().z, nether->spawnPoint().x, nether->spawnPoint().y, nether->spawnPoint().z);
    }

    // What a chunk costs against the overworld's, for docs/Performance-Verlauf.md (printed only).
    void generationCost()
    {
        const std::unique_ptr<QcNetherGenerator> nether = generator(42);
        const QcTerrainGenerator terrain(m_content, 42);
        const auto timed = [](const QcWorldGenerator &world, int firstX, int firstZ) {
            QElapsedTimer timer;
            timer.start();
            for (int z = 0; z < 6; ++z) {
                for (int x = 0; x < 6; ++x) {
                    Q_UNUSED(world.generate({x + firstX, z + firstZ}));
                }
            }
            return static_cast<double>(timer.nsecsElapsed()) / 36.0e6;
        };
        qInfo("ms per chunk: nether %.2f, overworld %.2f", timed(*nether, 100, 100), timed(terrain, 100, 100));

        // Chunks inside each of the biomes with the most to build (seed 42).
        qInfo("ms per chunk: crimson forest %.2f, warped forest %.2f, basalt deltas %.2f", timed(*nether, -70, -77), timed(*nether, -49, -69), timed(*nether, -60, -103));

        // A fortress: planning a region, and the chunks around the middle of one, first with its
        // plan still to make, then with the plan in the cache.
        const std::unique_ptr<QcNetherGenerator> planner = generator(42);
        QElapsedTimer timer;
        timer.start();
        std::shared_ptr<const QcFortressPlan> plan;
        for (int i = 0; i < 16; ++i) {
            const std::shared_ptr<const QcFortressPlan> found = planner->fortress().planOf(i % 4 - 2, i / 4 - 2);
            plan = plan ? plan : found;
        }
        const double planning = static_cast<double>(timer.nsecsElapsed()) / 16.0e6;
        QVERIFY(plan);
        const QcChunkPos middle = plan->middle().chunk();
        const std::unique_ptr<QcNetherGenerator> cold = generator(42);
        const double first = timed(*cold, middle.x - 3, middle.z - 3);
        qInfo("fortress: ms per region planned %.2f; ms per chunk around its middle %.2f with planning, %.2f planned", planning, first, timed(*cold, middle.x - 3, middle.z - 3));
    }
};

QTEST_GUILESS_MAIN(TestNetherGenerator)
#include "test_nether_generator.moc"
