#include "common/core/QcIntMath.h"
#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QHash>
#include <QTest>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>
#include <cmath>

using namespace Qt::StringLiterals;

// Soft edges of cold biomes (decision E-858): water freezes by the temperature of its column with a
// noise (a ragged edge, single floes, rivers only further into the cold), and the ground blocks mix
// over a few blocks at a biome border. No ice edge or ground edge runs straight along a chunk border
// or a biome border for more than a handful of blocks, and few edges lie on such borders at all. Fixed seeds plus one random seed per run
// (decision E-846).
namespace {

// A square of generated chunks: the top block of each column (plants left out) and its biome.
struct Window
{
    int x0 = 0;
    int z0 = 0;
    int size = 0;
    QList<QcBlockId> top;
    QList<int> biome;

    QcBlockId topAt(int x, int z) const { return top[(z - z0) * size + (x - x0)]; }
    int biomeAt(int x, int z) const { return biome[(z - z0) * size + (x - x0)]; }
};

// Where to look: the middle of a window.
struct Spot
{
    int x = 0;
    int z = 0;
};

constexpr int windowChunks = 4;
// Longest straight run of an edge on a chunk or biome border the test allows.
constexpr int handful = 8;

} // namespace

class TestColdEdges : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    const qint64 m_randomSeed = QcTestUtil::randomSeed();

    // Seed 544594159: frozen rivers on average as warm as frozen still water (the means are close).
    // Seed 826314780: a river along a lake shore at the river's freezing threshold froze the lake
    // but not the river, an ice edge of 26 blocks on the shore (decision E-914).
    std::array<qint64, 6> seeds() const { return {7, 42, 1234, 544594159, 826314780, m_randomSeed}; }
    [[nodiscard]] QcBlockId id(const QString &name) const { return m_content->blockId(name).value_or(qcAirBlockId); }

    Window windowAt(const QcTerrainGenerator &generator, Spot spot) const
    {
        Window window;
        const QcChunkPos first = QcBlockPos{spot.x, 0, spot.z}.chunk();
        const int firstX = first.x - windowChunks / 2;
        const int firstZ = first.z - windowChunks / 2;
        window.x0 = firstX * QcWorld::chunkSize;
        window.z0 = firstZ * QcWorld::chunkSize;
        window.size = windowChunks * QcWorld::chunkSize;
        window.top.resize(static_cast<qsizetype>(window.size) * window.size);
        window.biome.resize(static_cast<qsizetype>(window.size) * window.size);
        for (int cz = 0; cz < windowChunks; ++cz) {
            for (int cx = 0; cx < windowChunks; ++cx) {
                const std::shared_ptr<QcChunk> chunk = generator.generate({firstX + cx, firstZ + cz});
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        const int index = (cz * QcWorld::chunkSize + z) * window.size + cx * QcWorld::chunkSize + x;
                        window.top[index] = topBlock(*chunk, x, z);
                        window.biome[index] = generator.column(window.x0 + cx * QcWorld::chunkSize + x, window.z0 + cz * QcWorld::chunkSize + z).biome;
                    }
                }
            }
        }
        return window;
    }

    QcBlockId topBlock(const QcChunk &chunk, int x, int z) const
    {
        for (int y = QcWorld::worldHeight - 1; y > 0; --y) {
            const QcBlockId block = chunk.block(x, y, z);
            if (block != qcAirBlockId && (m_content->block(block).solid || m_content->block(block).liquid)) {
                return block;
            }
        }
        return qcAirBlockId;
    }

    // The longest run of edges between two kinds of `blocks` that lies on a chunk border or a biome
    // border, straight along x or z.
    struct Run
    {
        int length = 0;
        // The column where it ends and its neighbour across the edge.
        int x = 0;
        int z = 0;
        int nx = 0;
        int nz = 0;
    };
    static Run longestBorderRun(const Window &window, const QList<QcBlockId> &blocks)
    {
        const auto edge = [&](int x, int z, int nx, int nz) {
            const QcBlockId a = window.topAt(x, z);
            const QcBlockId b = window.topAt(nx, nz);
            const bool border = (nx != x && qcFloorMod(nx, QcWorld::chunkSize) == 0) || (nz != z && qcFloorMod(nz, QcWorld::chunkSize) == 0)
                                || window.biomeAt(x, z) != window.biomeAt(nx, nz);
            return a != b && blocks.contains(a) && blocks.contains(b) && border;
        };
        Run longest;
        for (int line = 0; line + 1 < window.size; ++line) {
            int alongZ = 0;
            int alongX = 0;
            for (int step = 0; step < window.size; ++step) {
                const int x = window.x0 + line;
                const int z = window.z0 + step;
                alongZ = edge(x, z, x + 1, z) ? alongZ + 1 : 0;
                if (alongZ > longest.length) {
                    longest = {alongZ, x, z, x + 1, z};
                }
                alongX = edge(window.x0 + step, window.z0 + line, window.x0 + step, window.z0 + line + 1) ? alongX + 1 : 0;
                if (alongX > longest.length) {
                    longest = {alongX, window.x0 + step, window.z0 + line, window.x0 + step, window.z0 + line + 1};
                }
            }
        }
        return longest;
    }

    // The edges between two kinds of `blocks` in a window: all of them, those on a biome border and
    // those on a chunk border.
    struct Edges
    {
        int all = 0;
        int onBiomeBorder = 0;
        int onChunkBorder = 0;

        Edges &operator+=(Edges other)
        {
            all += other.all;
            onBiomeBorder += other.onBiomeBorder;
            onChunkBorder += other.onChunkBorder;
            return *this;
        }
    };
    static Edges edgesOf(const Window &window, const QList<QcBlockId> &blocks)
    {
        Edges edges;
        const auto count = [&](int x, int z, int nx, int nz) {
            const QcBlockId a = window.topAt(x, z);
            const QcBlockId b = window.topAt(nx, nz);
            if (a == b || !blocks.contains(a) || !blocks.contains(b)) {
                return;
            }
            edges.all += 1;
            edges.onBiomeBorder += window.biomeAt(x, z) != window.biomeAt(nx, nz) ? 1 : 0;
            const bool chunkBorder = (nx != x && qcFloorMod(nx, QcWorld::chunkSize) == 0) || (nz != z && qcFloorMod(nz, QcWorld::chunkSize) == 0);
            edges.onChunkBorder += chunkBorder ? 1 : 0;
        };
        for (int z = window.z0; z < window.z0 + window.size - 1; ++z) {
            for (int x = window.x0; x < window.x0 + window.size - 1; ++x) {
                count(x, z, x + 1, z);
                count(x, z, x, z + 1);
            }
        }
        return edges;
    }

    // Checks the shares of edges on borders: a chunk border holds about 1 line in 16, a biome border
    // no more than a third of them (freezing or ground by biome put every edge on one).
    static void checkShares(Edges edges, const char *what)
    {
        qInfo("%s edges: %d, on biome borders %d, on chunk borders %d", what, edges.all, edges.onBiomeBorder, edges.onChunkBorder);
        QVERIFY(edges.all > 0);
        QVERIFY2(edges.onChunkBorder * 8 <= edges.all, what);
        QVERIFY2(edges.onBiomeBorder * 3 <= edges.all, what);
    }

    static QString runText(const QcTerrainGenerator &generator, const QString &what, const Run &run, qint64 seed)
    {
        const QcTerrainColumn a = generator.column(run.x, run.z);
        const QcTerrainColumn b = generator.column(run.nx, run.nz);
        return u"%1 edge of %2 blocks on a border ending at %3 %4 (%5, %6) / %7 %8 (%9, %10), seed %11"_s.arg(what)
            .arg(run.length)
            .arg(run.x)
            .arg(run.z)
            .arg(generator.biomeName(a.biome))
            .arg(a.temperature, 0, 'f', 3)
            .arg(run.nx)
            .arg(run.nz)
            .arg(generator.biomeName(b.biome))
            .arg(b.temperature, 0, 'f', 3)
            .arg(seed);
    }

    // Spots on a coarse grid around a seeded origin where `wanted` holds between two points 16 apart.
    template<typename Wanted>
    static std::vector<Spot> spots(qint64 seed, Wanted wanted)
    {
        std::vector<Spot> found;
        const int originX = static_cast<int>(seed % 61) * 64 - 1900;
        const int originZ = static_cast<int>(seed % 53) * 64 - 1700;
        for (int z = originZ - 2048; z < originZ + 2048 && found.size() < 2; z += 48) {
            for (int x = originX - 2048; x < originX + 2048 && found.size() < 2; x += 48) {
                if (wanted(x, z, x + 16, z) || wanted(x, z, x, z + 16)) {
                    found.push_back({x, z});
                }
            }
        }
        return found;
    }

    bool hasSnowLine(const QcTerrainGenerator &generator, int biome) const
    {
        const QString name = generator.biomeName(biome);
        const auto it = std::ranges::find(m_content->worldgen().biomes(), name, &QcBiomeDefinition::name);
        return it != m_content->worldgen().biomes().end() && it->snowLine > 0;
    }

    bool hasSnowSurface(const QcTerrainGenerator &generator, int biome) const
    {
        const QString name = generator.biomeName(biome);
        const auto it = std::ranges::find(m_content->worldgen().biomes(), name, &QcBiomeDefinition::name);
        return it != m_content->worldgen().biomes().end() && it->surfaceBlock == u"qtcraft:snow_block"_s;
    }

    // Foreign ground (snow in a grass biome or grass in a snow biome) near the border and far from it.
    struct ForeignGround
    {
        int nearForeign = 0;
        int nearAll = 0;
        int farForeign = 0;
        int farAll = 0;

        ForeignGround &operator+=(ForeignGround other)
        {
            nearForeign += other.nearForeign;
            nearAll += other.nearAll;
            farForeign += other.farForeign;
            farAll += other.farAll;
            return *this;
        }
    };

    // Distance (Chebyshev, at most 9) from a column to the nearest column of the other kind of biome.
    int distanceToOtherBiome(const QcTerrainGenerator &generator, const Window &window, Spot spot, bool snowy) const
    {
        int distance = 9;
        for (int dz = -8; dz <= 8; ++dz) {
            for (int dx = -8; dx <= 8; ++dx) {
                if (hasSnowSurface(generator, window.biomeAt(spot.x + dx, spot.z + dz)) != snowy) {
                    distance = std::min(distance, std::max(std::abs(dx), std::abs(dz)));
                }
            }
        }
        return distance;
    }

    ForeignGround foreignGroundIn(const QcTerrainGenerator &generator, const Window &window, QcBlockId grass, QcBlockId snow) const
    {
        ForeignGround ground;
        for (int z = window.z0 + 8; z < window.z0 + window.size - 8; ++z) {
            for (int x = window.x0 + 8; x < window.x0 + window.size - 8; ++x) {
                const QcBlockId block = window.topAt(x, z);
                // Snow above a biome's snow line (the mountains) is no mixing.
                if ((block != grass && block != snow) || hasSnowLine(generator, window.biomeAt(x, z))) {
                    continue;
                }
                const bool snowy = hasSnowSurface(generator, window.biomeAt(x, z));
                const int distance = distanceToOtherBiome(generator, window, {x, z}, snowy);
                const bool foreign = (block == snow) != snowy;
                ground.nearForeign += distance <= 2 && foreign;
                ground.nearAll += distance <= 2;
                ground.farForeign += distance >= 7 && foreign;
                ground.farAll += distance >= 7;
            }
        }
        return ground;
    }

    // Sums of the temperatures of frozen water over a wide area, rivers apart from lakes and sea.
    struct FrozenTemperatures
    {
        double riverSum = 0.0;
        double stillSum = 0.0;
        int rivers = 0;
        int still = 0;
        // The warmest frozen column of each.
        double riverMax = 0.0;
        double stillMax = 0.0;
    };

    static FrozenTemperatures frozenTemperatures(const QcTerrainGenerator &generator, QcBlockId ice, qint64 seed)
    {
        FrozenTemperatures sums;
        const int offset = static_cast<int>(seed % 1000);
        for (int z = -2048; z < 2048; z += 16) {
            for (int x = -2048; x < 2048; x += 16) {
                const QcTerrainColumn column = generator.column(x + offset, z);
                if (column.height > generator.seaLevel() - 1 || generator.frozenWaterAt(x + offset, z, column) != ice) {
                    continue;
                }
                const bool river = generator.isRiverBiome(column.biome);
                (river ? sums.riverSum : sums.stillSum) += column.temperature;
                (river ? sums.rivers : sums.still) += 1;
                double &warmest = river ? sums.riverMax : sums.stillMax;
                warmest = std::max(warmest, column.temperature);
            }
        }
        return sums;
    }

    // Single ice blocks with water on all four sides.
    static int singleFloesIn(const Window &window, QcBlockId ice, QcBlockId water)
    {
        int floes = 0;
        for (int z = window.z0 + 1; z < window.z0 + window.size - 1; ++z) {
            for (int x = window.x0 + 1; x < window.x0 + window.size - 1; ++x) {
                const bool alone = window.topAt(x, z) == ice && window.topAt(x - 1, z) == water && window.topAt(x + 1, z) == water
                                   && window.topAt(x, z - 1) == water && window.topAt(x, z + 1) == water;
                floes += alone ? 1 : 0;
            }
        }
        return floes;
    }

    // Columns of the window whose generated top is one of `blocks`, and how many of them
    // surfaceBlockAt() names alike (the map preview and planners use it).
    static std::pair<int, int> surfaceAgreement(const QcTerrainGenerator &generator, const Window &window, const QList<QcBlockId> &blocks)
    {
        int same = 0;
        int all = 0;
        for (int z = window.z0; z < window.z0 + window.size; ++z) {
            for (int x = window.x0; x < window.x0 + window.size; ++x) {
                if (blocks.contains(window.topAt(x, z))) {
                    all += 1;
                    same += generator.surfaceBlockAt(x, z) == window.topAt(x, z) ? 1 : 0;
                }
            }
        }
        return {same, all};
    }

    // Still water shallower than any river bed (the depth does not change its freezing share).
    static bool shallowStill(const QcTerrainGenerator &generator, const QcTerrainColumn &column)
    {
        return !generator.isRiverBiome(column.biome) && column.height <= generator.seaLevel() - 1 && generator.seaLevel() - column.exactHeight < 4.0;
    }

    // Steps of the river's freezing share between neighbouring columns of shallow still water.
    struct ShareSteps
    {
        int pairs = 0;
        int fading = 0;
        double steepest = 0.0;

        // Adds a pair; the step when it is larger than allowed. Where a river fades out (its half
        // width reaches 0 smoothly) the share is nearly 0 on both sides. Elsewhere it changes by
        // little across the ramp (the distance is an estimate, it may grow faster than a block per
        // block), and more only where the river's own half width jumps. Where a closed piece of a
        // river begins (decision E-851) the river field ends at once; that end is not smoothed and
        // not counted (decision E-914).
        std::optional<double> add(const QcTerrainGenerator &generator, const QcTerrainColumn &a, const QcTerrainColumn &b)
        {
            if (!shallowStill(generator, b) || (a.riverDistance >= QcRiverField::far) != (b.riverDistance >= QcRiverField::far)) {
                return std::nullopt;
            }
            pairs += 1;
            const bool fades = (a.riverHalfWidth > 0.0) != (b.riverHalfWidth > 0.0);
            fading += fades ? 1 : 0;
            const double step = std::abs(generator.riverFreezeShare(a) - generator.riverFreezeShare(b));
            steepest = std::max(steepest, step);
            const double widthChange = std::abs(a.riverHalfWidth - b.riverHalfWidth);
            const double allowed = fades && widthChange < 1.0 ? 0.1 : 0.3 + 0.6 * widthChange;
            return step > allowed ? std::optional(step) : std::nullopt;
        }
    };

private slots:
    // Where snow ground meets grass ground the two mix over a few blocks: neither edge runs along
    // a chunk or biome border, grass lies on the snowy side near the border and snow on the grassy
    // one, and none of it far inside the other biome.
    void groundMixesAtBiomeBorders()
    {
        const QcBlockId grass = id(u"qtcraft:grass_block"_s);
        const QcBlockId snow = id(u"qtcraft:snow_block"_s);
        int windows = 0;
        Edges edges;
        ForeignGround ground;
        int longRuns = 0;
        int surfaceSame = 0;
        int surfaceAll = 0;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator generator(m_content, seed);
            const auto snowMeetsGrass = [&](int x, int z, int nx, int nz) {
                const QcTerrainColumn a = generator.column(x, z);
                const QcTerrainColumn b = generator.column(nx, nz);
                return a.height > generator.seaLevel() + 2 && b.height > generator.seaLevel() + 2
                       && hasSnowSurface(generator, a.biome) != hasSnowSurface(generator, b.biome);
            };
            for (const Spot &spot : spots(seed, snowMeetsGrass)) {
                const Window window = windowAt(generator, spot);
                windows += 1;
                qInfo("seed %lld: ground edge near %d,%d", seed, spot.x, spot.z);
                const Run run = longestBorderRun(window, {grass, snow});
                // A random seed may meet a border that runs straight for a while: one window in
                // eight may hold a run over a handful, none one over twice that.
                QVERIFY2(run.length <= 2 * handful, qPrintable(runText(generator, u"ground"_s, run, seed)));
                longRuns += run.length > handful ? 1 : 0;
                edges += edgesOf(window, {grass, snow});
                ground += foreignGroundIn(generator, window, grass, snow);
                const std::pair<int, int> agreement = surfaceAgreement(generator, window, {grass, snow});
                surfaceSame += agreement.first;
                surfaceAll += agreement.second;
            }
        }
        qInfo("%d windows; foreign ground within 2 blocks of the border %d of %d, from 7 blocks on %d of %d", windows, ground.nearForeign,
              ground.nearAll, ground.farForeign, ground.farAll);
        QVERIFY(windows >= 4);
        checkShares(edges, "ground");
        QVERIFY2(ground.nearForeign * 10 >= ground.nearAll, "the ground does not mix at the border");
        QVERIFY2(longRuns * 8 <= windows, qPrintable(u"%1 of %2 windows with a long straight edge"_s.arg(longRuns).arg(windows)));
        qInfo("surfaceBlockAt agrees with the generated top on %d of %d grass and snow columns", surfaceSame, surfaceAll);
        QVERIFY2(surfaceSame * 100 >= surfaceAll * 98, "surfaceBlockAt differs from the generated ground");
        QVERIFY2(ground.farForeign * 100 <= ground.farAll, "the ground mixes far into the other biome");
    }

    // The freezing threshold has no step along a line in still water (decision E-914): where a
    // river runs on into a lake or the sea, fades out (dry land, the end of a ring) or ends, the
    // share of its lower threshold changes by little from one column to the next, except where the
    // river's own half width changes. Only water shallower than any river bed, where the depth
    // does not count.
    void riverThresholdHasNoStep()
    {
        ShareSteps steps;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator generator(m_content, seed);
            const int offset = static_cast<int>(seed % 1000);
            for (int z = -1024; z < 1024; z += 2) {
                for (int x = -1024 + offset; x < 1024 + offset; x += 2) {
                    const QcTerrainColumn a = generator.column(x, z);
                    if (!shallowStill(generator, a) || a.riverDistance >= 30.0) {
                        continue;
                    }
                    for (const auto &[nx, nz] : {std::pair{x + 1, z}, std::pair{x, z + 1}}) {
                        const std::optional<double> excess = steps.add(generator, a, generator.column(nx, nz));
                        QVERIFY2(!excess, qPrintable(u"share step %1 too far at %2 %3 / %4 %5, seed %6"_s.arg(excess.value_or(0.0)).arg(x).arg(z).arg(nx).arg(nz).arg(seed)));
                    }
                }
            }
        }
        qInfo("%d pairs of shallow still water near rivers, %d where a river fades out; steepest step %.3f", steps.pairs, steps.fading, steps.steepest);
        QVERIFY(steps.pairs > 0 && steps.fading > 0);
    }

    // Ice ends in a ragged edge across the climate, not along biome or chunk borders; single floes
    // lie off it, and rivers freeze only where it is colder than for lakes and the sea.
    void iceFreezesByTemperature()
    {
        const QcBlockId ice = id(u"qtcraft:ice"_s);
        const QcBlockId water = id(u"qtcraft:water"_s);
        int windows = 0;
        Edges edges;
        int floes = 0;
        FrozenTemperatures sums;
        int longRuns = 0;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator generator(m_content, seed);
            const int sea = generator.seaLevel();
            const auto frozenAt = [&](int x, int z) -> std::optional<bool> {
                const QcTerrainColumn column = generator.column(x, z);
                if (column.height > sea - 1) {
                    return std::nullopt;
                }
                return generator.frozenWaterAt(x, z, column) == ice;
            };
            const auto iceMeetsWater = [&](int x, int z, int nx, int nz) {
                const std::optional<bool> a = frozenAt(x, z);
                const std::optional<bool> b = a ? frozenAt(nx, nz) : std::nullopt;
                return a && b && *a != *b;
            };
            const FrozenTemperatures found = frozenTemperatures(generator, ice, seed);
            sums.riverSum += found.riverSum;
            sums.stillSum += found.stillSum;
            sums.rivers += found.rivers;
            sums.still += found.still;
            sums.riverMax = std::max(sums.riverMax, found.riverMax);
            sums.stillMax = std::max(sums.stillMax, found.stillMax);
            for (const Spot &spot : spots(seed, iceMeetsWater)) {
                const Window window = windowAt(generator, spot);
                windows += 1;
                qInfo("seed %lld: ice edge near %d,%d", seed, spot.x, spot.z);
                const Run run = longestBorderRun(window, {ice, water});
                // A random seed may meet a border that runs straight for a while: one window in
                // eight may hold a run over a handful, none one over twice that.
                QVERIFY2(run.length <= 2 * handful, qPrintable(runText(generator, u"ice"_s, run, seed)));
                longRuns += run.length > handful ? 1 : 0;
                edges += edgesOf(window, {ice, water});
                floes += singleFloesIn(window, ice, water);
            }
        }
        const int rivers = sums.rivers;
        const int still = sums.still;
        qInfo("%d windows, %d single floes; frozen rivers %d (mean temperature %.3f, warmest %.3f), frozen lakes and sea %d (%.3f, %.3f)",
              windows, floes, rivers, rivers > 0 ? sums.riverSum / rivers : 0.0, sums.riverMax, still, still > 0 ? sums.stillSum / still : 0.0,
              sums.stillMax);
        QVERIFY(windows >= 4);
        checkShares(edges, "ice");
        QVERIFY(floes > 0);
        QVERIFY2(longRuns * 8 <= windows, qPrintable(u"%1 of %2 windows with a long straight edge"_s.arg(longRuns).arg(windows)));
        QVERIFY(rivers > 0 && still > 0);
        // Rivers freeze only in colder land than still water (riverTemperature below temperature):
        // the warmest frozen river is colder than the warmest frozen lake or sea. (Their means lie
        // close together: most ice of both lies far inside the cold land.)
        QVERIFY2(sums.riverMax < sums.stillMax, qPrintable(u"%1 %2"_s.arg(sums.riverMax).arg(sums.stillMax)));
    }
};

QTEST_GUILESS_MAIN(TestColdEdges)
#include "test_cold_edges.moc"
