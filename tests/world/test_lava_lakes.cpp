#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QSet>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

// Lava lakes on the surface (decision E-856): about one per 200 chunks, kept apart, each an
// irregular basin inside its chunk whose lava lies 1–2 blocks below the ground around it, walled in
// by stone where it would touch air or soil, with no grass or plants right at its rim. Fixed seeds
// plus one random seed per run (decision E-846).
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

    // Fixed seeds first; 510556236 has a window with almost no lakes (E-846, E-856).
    static constexpr std::array<qint64, 4> fixedSeeds{7, 42, 1234, 510556236};

    std::array<qint64, 5> seeds() const { return {fixedSeeds[0], fixedSeeds[1], fixedSeeds[2], fixedSeeds[3], m_randomSeed}; }

    [[nodiscard]] QcBlockId id(const QString &name) const { return m_content->blockId(name).value_or(qcAirBlockId); }

    // The lakes of the chunks within `reach` of the origin; the window starts at a seeded offset.
    static Survey survey(const QcTerrainGenerator &generator, qint64 seed, int reach)
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

private slots:
    // About one lake per 200 chunks over many worlds, and lakes of neighbouring squares apart. One
    // window of 80 x 80 chunks holds anywhere from none to 63 lakes (ocean, unsuited biomes), so
    // the rate is checked over the fixed seeds only (deterministic); every window, the random one
    // too, at most one lake per 100 chunks.
    void lakesAreRareAndApart()
    {
        int chunks = 0;
        int lakes = 0;
        int nearest = 1 << 20;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator generator(m_content, seed);
            const Survey found = survey(generator, seed, 40);
            if (!found.lakes.isEmpty()) {
                // A place to look at a lake (screenshots, debugging).
                const Site &first = found.lakes.first();
                qInfo("seed %lld: first lake in chunk %d,%d, level %d", seed, first.chunk.x, first.chunk.z, first.level);
            }
            QVERIFY2(found.lakes.size() * 100 <= found.chunks, qPrintable(u"seed %1: %2 lakes"_s.arg(seed).arg(found.lakes.size())));
            if (std::ranges::find(fixedSeeds, seed) != fixedSeeds.end()) {
                chunks += found.chunks;
                lakes += static_cast<int>(found.lakes.size());
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
        qInfo("%d lava lakes in %d chunks: one per %.0f chunks; nearest two %d chunks apart", lakes, chunks, perLake, nearest);
        QVERIFY2(perLake >= 130.0 && perLake <= 400.0, qPrintable(QString::number(perLake)));
        // The tried chunks of two squares keep `spacing` chunks between them.
        const auto lake = std::ranges::find(m_content->worldgen().features(), u"qtcraft:lava_lake"_s, &QcFeatureDefinition::name);
        QVERIFY(lake != m_content->worldgen().features().end());
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
            const Survey found = survey(generator, seed, 24);
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
            const Survey found = survey(generator, seed, 40);
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
