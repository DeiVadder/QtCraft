// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcIntMath.h"
#include "common/world/QcChunk.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QHash>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

using namespace Qt::StringLiterals;

// Lava lakes on the surface (decisions E-856, E-936): rare (at most one per 288 chunks of fitting
// land), kept apart, each an irregular basin inside its chunk whose lava lies 1–2 blocks below the
// ground around it, walled in by stone where it would touch air or soil, with no grass or plants
// right at its rim. Fixed seeds plus one random seed per run (decision E-846).
namespace {

using Site = QcSurfaceLakes::Site;

// The planned lakes of a square of chunks around the origin.
struct Survey
{
    int chunks = 0;
    QList<Site> lakes;
};

} // namespace

class TestLavaLakes : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // The random seed of this run besides the fixed ones (decision E-846): QC_TEST_SEED if set.
    const qint64 m_randomSeed = QcTestUtil::randomSeed();
    QHash<qint64, Survey> m_surveys;

    // Fixed seeds first; 510556236 has a window with few lakes (E-846, E-856), 1380505090 the most
    // found (19 lakes, three quarters of its window flat land of the lakes' biomes; E-907, E-936).
    static constexpr std::array<qint64, 5> fixedSeeds{7, 42, 1234, 510556236, 1380505090};

    std::array<qint64, 6> seeds() const
    {
        return {fixedSeeds[0], fixedSeeds[1], fixedSeeds[2], fixedSeeds[3], fixedSeeds[4], m_randomSeed};
    }

    [[nodiscard]] QcBlockId id(const QString &name) const { return m_content->blockId(name).value_or(qcAirBlockId); }

    // The lakes of the chunks within `reach` of the origin; the window starts at a seeded offset.
    static Survey surveyAround(const QcTerrainGenerator &generator, qint64 seed, int reach)
    {
        Survey result;
        const int offsetX = static_cast<int>(seed % 97) * 10 - 480;
        const int offsetZ = static_cast<int>(seed % 89) * 10 - 440;
        for (int cz = offsetZ - reach; cz < offsetZ + reach; ++cz) {
            for (int cx = offsetX - reach; cx < offsetX + reach; ++cx) {
                result.chunks += 1;
                if (std::optional<Site> lake = generator.lakeIn({cx, cz})) {
                    result.lakes.append(std::move(*lake));
                }
            }
        }
        return result;
    }

    // The lakes of the window of 120 x 120 chunks of a seed, surveyed once for all test functions
    // (the plans depend only on the seed).
    const Survey &survey(const QcTerrainGenerator &generator, qint64 seed)
    {
        if (!m_surveys.contains(seed)) {
            m_surveys.insert(seed, surveyAround(generator, seed, 60));
        }
        return m_surveys[seed];
    }

    // The highest block that is neither air nor a plant at a column of the chunk.
    int groundTop(const QcChunk &chunk, int x, int z) const
    {
        for (int y = QcWorld::worldHeight - 1; y > 0; --y) {
            const QcBlockId block = chunk.block(x, y, z);
            if (block != qcAirBlockId && (m_content->block(block).solid || m_content->block(block).liquid)) {
                return y;
            }
        }
        return 0;
    }

    // Why the lake of `site` in its generated chunk is wrong; empty when it is right.
    QString lakeProblem(const QcTerrainGenerator &generator, const Site &site, const QcChunk &chunk) const
    {
        const QcBlockId lava = id(u"qtcraft:lava"_s);
        const int baseX = chunk.pos().x * QcWorld::chunkSize;
        const int baseZ = chunk.pos().z * QcWorld::chunkSize;
        const QString at = u" (lake at chunk %1 %2, level %3)"_s.arg(chunk.pos().x).arg(chunk.pos().z).arg(site.level);
        int cells = 0;
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                const int depth = generator.lakes().depthAt(site, baseX + x, baseZ + z);
                const QString problem = depth >= 1 ? basinProblem(chunk, site, x, z) : rimProblem(generator, chunk, site, x, z);
                if (!problem.isEmpty()) {
                    return problem + u" at %1 %2"_s.arg(baseX + x).arg(baseZ + z) + at;
                }
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    cells += chunk.block(x, y, z) == lava;
                }
            }
        }
        return cells == 0 ? u"no lava"_s + at : QString();
    }

    // A column of the basin: lava up to the level with air above it, walled in by rock.
    QString basinProblem(const QcChunk &chunk, const Site &site, int x, int z) const
    {
        const QcBlockId lava = id(u"qtcraft:lava"_s);
        if (x == 0 || x == 15 || z == 0 || z == 15) {
            return u"lava on the chunk's edge"_s;
        }
        if (chunk.block(x, site.level, z) != lava || chunk.block(x, site.level + 1, z) != qcAirBlockId) {
            return u"no open lava at the level"_s;
        }
        for (int y = site.level; chunk.block(x, y, z) == lava; --y) {
            for (const auto &[dx, dy, dz] : {std::array{1, 0, 0}, std::array{-1, 0, 0}, std::array{0, 0, 1},
                                             std::array{0, 0, -1}, std::array{0, -1, 0}}) {
                const QcBlockId next = chunk.block(x + dx, y + dy, z + dz);
                const QcBlockDefinition &definition = m_content->block(next);
                if (next != lava && (!definition.solid || definition.liquid || definition.replaceable)) {
                    return u"lava can flow"_s;
                }
                if (next != lava && isSoil(next)) {
                    return u"lava touches soil"_s;
                }
            }
        }
        return {};
    }

    // A column around the basin: within one block rock at least one block above the level, within
    // two no plants.
    QString rimProblem(const QcTerrainGenerator &generator, const QcChunk &chunk, const Site &site, int x, int z) const
    {
        const int baseX = chunk.pos().x * QcWorld::chunkSize;
        const int baseZ = chunk.pos().z * QcWorld::chunkSize;
        int ring = 3;
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (generator.lakes().depthAt(site, baseX + x + dx, baseZ + z + dz) >= 1) {
                    ring = std::min(ring, std::max(std::abs(dx), std::abs(dz)));
                }
            }
        }
        if (ring > 2) {
            return {};
        }
        const int top = groundTop(chunk, x, z);
        const QcBlockId above = chunk.block(x, top + 1, z);
        if (above != qcAirBlockId) {
            return u"a plant near the lava"_s;
        }
        if (ring == 1 && top < site.level + 1) {
            return u"the rim is not above the lava"_s;
        }
        if (ring == 1 && isSoil(chunk.block(x, top, z))) {
            return u"grass or soil right at the rim"_s;
        }
        return {};
    }

    bool isSoil(QcBlockId block) const
    {
        static const QStringList soil{u"qtcraft:grass_block"_s, u"qtcraft:dirt"_s, u"qtcraft:sand"_s, u"qtcraft:gravel"_s,
                                      u"qtcraft:snow_block"_s, u"qtcraft:coarse_dirt"_s};
        return std::ranges::any_of(soil, [&](const QString &name) { return id(name) == block; });
    }

    // Whether a column (world) is in the basin of the site or next to it.
    static bool nearBasin(const QcTerrainGenerator &generator, const Site &site, int x, int z)
    {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (generator.lakes().depthAt(site, x + dx, z + dz) >= 1) {
                    return true;
                }
            }
        }
        return false;
    }

    // The lowest ground top of the terrain (before the lake) in the basin and the ring around it.
    static int lowestGroundAround(const QcTerrainGenerator &generator, const Site &site)
    {
        int lowest = QcWorld::worldHeight;
        for (int z = site.footprint.z0; z <= site.footprint.z1; ++z) {
            for (int x = site.footprint.x0; x <= site.footprint.x1; ++x) {
                if (nearBasin(generator, site, x, z)) {
                    lowest = std::min(lowest, generator.column(x, z).height - 1);
                }
            }
        }
        return lowest;
    }

    // How many lakes the windows of 16 x 16 chunks (about what a player sees around himself) that
    // hold any lake hold.
    struct Crowding
    {
        // Windows holding at least one lake, at least three, and the most in one.
        int windows = 0;
        int crowded = 0;
        int most = 0;
    };

    static Crowding crowding(const QList<Site> &lakes)
    {
        constexpr int size = 16;
        Crowding result;
        if (lakes.isEmpty()) {
            return result;
        }
        const auto [minX, maxX] = std::ranges::minmax(lakes | std::views::transform([](const Site &site) { return site.chunk.x; }));
        const auto [minZ, maxZ] = std::ranges::minmax(lakes | std::views::transform([](const Site &site) { return site.chunk.z; }));
        for (int z0 = minZ - size + 1; z0 <= maxZ; ++z0) {
            for (int x0 = minX - size + 1; x0 <= maxX; ++x0) {
                const auto inside = [&](const Site &site) {
                    return site.chunk.x >= x0 && site.chunk.x < x0 + size && site.chunk.z >= z0 && site.chunk.z < z0 + size;
                };
                const int count = static_cast<int>(std::ranges::count_if(lakes, inside));
                result.windows += count >= 1;
                result.crowded += count >= 3;
                result.most = std::max(result.most, count);
            }
        }
        return result;
    }

private slots:
    // At most one lake per 288 chunks of flat dry land of the lakes' biomes (every other square of
    // 12 x 12 chunks; decision E-936), about one per 1400 chunks of whole worlds, and lakes of
    // neighbouring squares apart. A window of 120 x 120 chunks holds anywhere from a few to about
    // twenty lakes: the lakes go to such land, and a square there often finds a place in its 8
    // tries, so a window's count follows how much of it is such land (decision E-907). The rate is
    // checked over the fixed seeds only (deterministic); every window, the random one too, holds at
    // most one lake per square and at most 4 in 16 x 16 chunks.
    void lakesAreRareAndApart()
    {
        const auto lake = std::ranges::find(m_content->worldgen().features(), u"qtcraft:lava_lake"_s, &QcFeatureDefinition::name);
        QVERIFY(lake != m_content->worldgen().features().end());
        int chunks = 0;
        int lakes = 0;
        int nearest = 1 << 20;
        int densest = 0;
        int windows = 0;
        int crowded = 0;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator generator(m_content, seed);
            const Survey &found = survey(generator, seed);
            if (!found.lakes.isEmpty()) {
                // A place to look at a lake (screenshots, debugging).
                const Site &first = found.lakes.first();
                qInfo("seed %lld: %lld lakes, first in chunk %d,%d, level %d", seed, static_cast<qint64>(found.lakes.size()), first.chunk.x,
                      first.chunk.z, first.level);
            }
            QSet<std::pair<int, int>> squares;
            for (const Site &site : found.lakes) {
                const std::pair<int, int> square{qcFloorDiv(site.chunk.x, lake->region), qcFloorDiv(site.chunk.z, lake->region)};
                QVERIFY2(!squares.contains(square), qPrintable(u"seed %1: two lakes in one square"_s.arg(seed)));
                squares.insert(square);
            }
            // A window of 16 x 16 chunks touches the tried chunks of at most 2 x 2 squares.
            const Crowding crowd = crowding(found.lakes);
            QVERIFY2(crowd.most <= 4, qPrintable(u"seed %1: %2 lakes in 16 x 16 chunks"_s.arg(seed).arg(crowd.most)));
            densest = std::max(densest, crowd.most);
            // Loosely above the densest window measured (21 lakes in 14400 chunks, one per 686;
            // decision E-936): a window of flat dry land of the lakes' biomes.
            QVERIFY2(found.lakes.size() * 400 <= found.chunks, qPrintable(u"seed %1: %2 lakes"_s.arg(seed).arg(found.lakes.size())));
            if (std::ranges::find(fixedSeeds, seed) != fixedSeeds.end()) {
                chunks += found.chunks;
                lakes += static_cast<int>(found.lakes.size());
                windows += crowd.windows;
                crowded += crowd.crowded;
            }
            for (qsizetype i = 0; i < found.lakes.size(); ++i) {
                for (qsizetype j = i + 1; j < found.lakes.size(); ++j) {
                    const QcChunkPos a = found.lakes[i].chunk;
                    const QcChunkPos b = found.lakes[j].chunk;
                    nearest = std::min(nearest, std::max(std::abs(a.x - b.x), std::abs(a.z - b.z)));
                }
            }
        }

        const double perLake = lakes > 0 ? static_cast<double>(chunks) / lakes : 0.0;
        const double crowdedShare = windows > 0 ? static_cast<double>(crowded) / windows : 0.0;
        qInfo("%d lava lakes in %d chunks: one per %.0f chunks; nearest two %d chunks apart; at most %d in 16 x 16 chunks, "
              "3 or more in %.1f %% of those with a lake",
              lakes, chunks, perLake, nearest, densest, 100.0 * crowdedShare);
        // Measured one per 1412 chunks (one per 216 with squares of 6 x 6 chunks and every square
        // planning; decision E-936).
        QVERIFY2(perLake >= 800.0 && perLake <= 3000.0, qPrintable(QString::number(perLake)));
        // A player sees one lake around himself, now and then two: measured 3 or more in 2.1 % of
        // the windows with a lake on the fixed seeds (44 % before on 15 seeds; decision E-936).
        QVERIFY2(crowdedShare <= 0.05, qPrintable(QString::number(crowdedShare)));
        // The tried chunks of two squares keep `spacing` chunks between them.
        QVERIFY2(nearest >= lake->spacing + 1, qPrintable(QString::number(nearest)));
    }

    // Generated lakes: lava at the planned level in an irregular basin of at least three ellipses,
    // walled in by rock, the rim above the lava and bare of grass and plants; sizes differ.
    void lakesAreWalledBasinsBelowTheirRim()
    {
        QSet<int> sizes;
        int checked = 0;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator generator(m_content, seed);
            const Survey &found = survey(generator, seed);
            for (qsizetype i = 0; i < std::min<qsizetype>(found.lakes.size(), 4); ++i) {
                const Site &site = found.lakes[i];
                const std::shared_ptr<QcChunk> chunk = generator.generate(site.chunk);
                const QString problem = lakeProblem(generator, site, *chunk);
                QVERIFY2(problem.isEmpty(), qPrintable(problem + u" seed %1"_s.arg(seed)));

                int columns = 0;
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        columns += generator.lakes().depthAt(site, site.chunk.x * 16 + x, site.chunk.z * 16 + z) >= 1;
                    }
                }
                sizes.insert(columns);
                QVERIFY2(site.blobs.size() >= 3, "a lake is one ellipse");
                checked += 1;
            }
        }
        qInfo("%d lakes checked, %lld different sizes", checked, static_cast<qint64>(sizes.size()));
        QVERIFY(checked >= 6);
        QVERIFY(sizes.size() >= 3);
    }

    // The lava's surface lies 1 or 2 blocks below the lowest ground of the basin and the ring around
    // it (the terrain before the lake), never at it.
    void lavaLiesOneOrTwoBelowTheRim()
    {
        int one = 0;
        int two = 0;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator generator(m_content, seed);
            const Survey &found = survey(generator, seed);
            for (const Site &site : found.lakes) {
                const int lowest = lowestGroundAround(generator, site);
                const int below = lowest - site.level;
                QVERIFY2(below == 1 || below == 2, qPrintable(QString::number(below)));
                one += below == 1;
                two += below == 2;
            }
        }
        qInfo("lava 1 below the rim: %d lakes, 2 below: %d", one, two);
        QVERIFY(one > 0 && two > 0);
    }
};

QTEST_GUILESS_MAIN(TestLavaLakes)
#include "test_lava_lakes.moc"
