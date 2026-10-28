#include "common/content/parser/QcContentParser.h"
#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "world/QcWaterTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// A delta found on a coarse grid: its middle (the mean of its delta columns) and size.
struct Delta
{
    int x = 0;
    int z = 0;
    // Grid points of the delta biomes.
    int points = 0;
    QString biome;
};

// Grid points of a delta biome connected on a grid of `step` blocks over a square window, and how
// many land columns (dry above the sea, or a delta) the window holds.
struct Survey
{
    QList<Delta> deltas;
    qint64 land = 0;
    qint64 delta = 0;
};

// Floods the grid points of delta biomes connected to `start` (marking them in `seen`, a scratch
// list of the caller): their middle and number.
Delta floodDelta(const QList<int> &biome, QList<bool> &seen, qsizetype start, int cells)
{
    static constexpr std::array<std::pair<int, int>, 4> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    Delta delta;
    qint64 sumI = 0;
    qint64 sumJ = 0;
    QList<qsizetype> stack{start};
    seen[start] = true;
    while (!stack.isEmpty()) {
        const qsizetype cell = stack.takeLast();
        const int i = static_cast<int>(cell % cells);
        const int j = static_cast<int>(cell / cells);
        ++delta.points;
        sumI += i;
        sumJ += j;
        for (const auto [di, dj] : sides) {
            const int ni = i + di;
            const int nj = j + dj;
            const qsizetype next = static_cast<qsizetype>(nj) * cells + ni;
            if (ni >= 0 && nj >= 0 && ni < cells && nj < cells && biome[next] >= 0 && !seen[next]) {
                seen[next] = true;
                stack.append(next);
            }
        }
    }
    delta.x = static_cast<int>(sumI / delta.points);
    delta.z = static_cast<int>(sumJ / delta.points);
    return delta;
}

// The deltas of a square window around the origin on a grid of `step` blocks.
Survey survey(const QcTerrainGenerator &generator, int reach, int step)
{
    const int cells = 2 * reach / step;
    Survey result;
    QList<int> biome(static_cast<qsizetype>(cells) * cells, -1);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const QcTerrainColumn column = generator.column(-reach + i * step, -reach + j * step);
            const bool delta = generator.isDeltaBiome(column.biome);
            biome[static_cast<qsizetype>(j) * cells + i] = delta ? column.biome : -1;
            result.land += delta || (column.height > generator.seaLevel() && column.landFactor > 0.99) ? 1 : 0;
            result.delta += delta ? 1 : 0;
        }
    }

    QList<bool> seen(biome.size(), false);
    for (qsizetype start = 0; start < biome.size(); ++start) {
        if (biome[start] >= 0 && !seen[start]) {
            Delta delta = floodDelta(biome, seen, start, cells);
            delta.biome = generator.biomeName(biome[start]);
            delta.x = -reach + delta.x * step;
            delta.z = -reach + delta.z * step;
            result.deltas.append(delta);
        }
    }
    return result;
}

// The deltas of at least `points` grid points, the largest first.
QList<Delta> largest(QList<Delta> deltas, int points)
{
    deltas.removeIf([points](const Delta &delta) { return delta.points < points; });
    std::ranges::sort(deltas, [](const Delta &a, const Delta &b) { return a.points > b.points; });
    return deltas;
}

// The water of a square window round a delta, column by column: how much of the delta's water is
// one body with the sea.
struct Waters
{
    qint64 deltaWater = 0;
    qint64 reached = 0;
    // Water at least two blocks deep (the arms; the ponds on the islands are shallower).
    qint64 deepWater = 0;
    qint64 deepReached = 0;
};

Waters watersAround(const QcTerrainGenerator &generator, const Delta &delta, int reach)
{
    const int cells = 2 * reach + 1;
    QList<QcTerrainColumn> columns;
    columns.reserve(static_cast<qsizetype>(cells) * cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            columns.append(generator.column(delta.x - reach + i, delta.z - reach + j));
        }
    }
    const auto water = [&](qsizetype cell) { return columns[cell].height < generator.seaLevel(); };

    // Flood from the sea: water of the land's biomes (not of a river or delta) a few blocks deep.
    QList<bool> seen(columns.size(), false);
    QList<qsizetype> stack;
    for (qsizetype cell = 0; cell < columns.size(); ++cell) {
        const QcTerrainColumn &column = columns[cell];
        if (!generator.isDeltaBiome(column.biome) && !generator.isRiverBiome(column.biome) && column.height < generator.seaLevel() - 4) {
            seen[cell] = true;
            stack.append(cell);
        }
    }
    while (!stack.isEmpty()) {
        const qsizetype cell = stack.takeLast();
        const int i = static_cast<int>(cell % cells);
        const int j = static_cast<int>(cell / cells);
        for (const auto [di, dj] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
            const qsizetype next = static_cast<qsizetype>(j + dj) * cells + (i + di);
            if (i + di >= 0 && j + dj >= 0 && i + di < cells && j + dj < cells && !seen[next] && water(next)) {
                seen[next] = true;
                stack.append(next);
            }
        }
    }

    // Only the water near the delta's middle counts: a delta's arms leave a window of `reach`
    // blocks only into the sea.
    Waters waters;
    for (qsizetype cell = 0; cell < columns.size(); ++cell) {
        const int i = static_cast<int>(cell % cells);
        const int j = static_cast<int>(cell / cells);
        if (!generator.isDeltaBiome(columns[cell].biome) || !water(cell) || std::max(std::abs(i - reach), std::abs(j - reach)) > reach / 2) {
            continue;
        }
        const bool deep = columns[cell].height <= generator.seaLevel() - 2;
        ++waters.deltaWater;
        waters.reached += seen[cell] ? 1 : 0;
        waters.deepWater += deep ? 1 : 0;
        waters.deepReached += deep && seen[cell] ? 1 : 0;
    }
    return waters;
}

} // namespace

// River deltas (decision E-850): fans of shallow arms and low islands where wide rivers run through
// flat land into the sea.
class TestRiverDeltas : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // The base game's content with a delta at every wide river mouth: the mouths that would get one
    // (rare in the base game), and many deltas for the tests of their kinds and neighbours.
    std::shared_ptr<const QcContentRegistry> m_everyMouth = [this] {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcTerrainSettings settings = content->worldgen().terrainSettings();
        settings.deltaShare = 1.0;
        content->worldgen().setTerrainSettings(settings);
        return std::shared_ptr<const QcContentRegistry>(std::move(content));
    }();
    // The random seed of this run besides the fixed ones (decision E-846): QC_TEST_SEED if set.
    const qint64 m_randomSeed = randomSeed();

    static qint64 randomSeed()
    {
        bool ok = false;
        const qint64 fixed = qEnvironmentVariable("QC_TEST_SEED").toLongLong(&ok);
        const qint64 seed = ok ? fixed : static_cast<qint64>(QRandomGenerator::global()->generate() & 0x7fffffff);
        qInfo("random seed %lld (set QC_TEST_SEED to repeat)", seed);
        return seed;
    }

    [[nodiscard]] QcBlockId id(const QString &name) const { return m_content->blockId(name).value_or(qcAirBlockId); }

    // The surveys of the seeds by content (the base game's or with every mouth), seed and reach (a
    // survey of a large window costs seconds; several tests read the same).
    mutable std::map<std::tuple<const QcContentRegistry *, qint64, int>, Survey> m_surveys;
    // The generators by content and seed, shared by the surveys and the tests: a generator caches
    // the river and delta sites it has looked at, which the tests then find again around the deltas
    // of the survey (the same seed gives the same terrain, which sameSeedSameDeltas checks against
    // a fresh generator).
    // A seed only one test reads (sharedSeed()) keeps its generator only during that test
    // (releaseOneOff()).
    mutable std::map<std::pair<const QcContentRegistry *, qint64>, std::unique_ptr<const QcTerrainGenerator>> m_generators;

    [[nodiscard]] bool sharedSeed(qint64 seed) const { return seed == 7 || seed == 42 || seed == 161831057 || seed == m_randomSeed; }

    // Frees the generator of a seed only one test reads (not sharedSeed()) when the returned guard
    // ends, after that test.
    [[nodiscard]] auto releaseOneOff(const std::shared_ptr<const QcContentRegistry> &content, qint64 seed) const
    {
        return qScopeGuard([this, key = std::pair{content.get(), seed}] {
            if (!sharedSeed(key.second)) {
                m_generators.erase(key);
            }
        });
    }

    [[nodiscard]] const QcTerrainGenerator &generatorOf(const std::shared_ptr<const QcContentRegistry> &content, qint64 seed) const
    {
        std::unique_ptr<const QcTerrainGenerator> &generator = m_generators[{content.get(), seed}];
        if (!generator) {
            generator = std::make_unique<const QcTerrainGenerator>(content, seed);
        }
        return *generator;
    }

    [[nodiscard]] const Survey &surveyOf(const std::shared_ptr<const QcContentRegistry> &content, qint64 seed, int reach) const
    {
        const std::tuple<const QcContentRegistry *, qint64, int> key{content.get(), seed, reach};
        if (const auto found = m_surveys.find(key); found != m_surveys.end()) {
            return found->second;
        }
        return m_surveys.emplace(key, survey(generatorOf(content, seed), reach, 8)).first->second;
    }

    // A seed whose window of 9216 blocks holds no delta (it once failed a test that divided by
    // the delta land it found).
    static constexpr qint64 noDeltaSeed = 2115900183;

    // Deltas are rare (about one in ten wide river mouths): a fixed seed's window of 6144 blocks
    // holds one to four; the random seed's is larger and may still hold none.
    [[nodiscard]] bool mayLackDeltas(qint64 seed) const { return seed == m_randomSeed || seed == noDeltaSeed; }
    [[nodiscard]] int reachOf(qint64 seed) const { return mayLackDeltas(seed) ? 4608 : 3072; }

    // The largest deltas of a seed (at most `count`; a fixed seed has at least one, checked).
    [[nodiscard]] QList<Delta> deltasOf(qint64 seed, int count) const
    {
        const QList<Delta> found = largest(surveyOf(m_content, seed, reachOf(seed)).deltas, 20);
        for (const Delta &delta : found) {
            qInfo("seed %lld: %s at %d %d, %d points", seed, qPrintable(delta.biome), delta.x, delta.z, delta.points);
        }
        [&] { QVERIFY2(!found.isEmpty() || mayLackDeltas(seed), qPrintable(u"seed %1: no delta"_s.arg(seed))); }();
        return found.mid(0, count);
    }

private slots:
    void sameSeedSameDeltas()
    {
        const QcTerrainGenerator &first = generatorOf(m_content, 42);
        const QcTerrainGenerator again(m_content, 42);
        const QList<Delta> deltas = deltasOf(42, 2);
        for (const Delta &delta : deltas) {
            for (int dz = -48; dz <= 48; dz += 3) {
                for (int dx = -48; dx <= 48; dx += 3) {
                    const QcTerrainColumn one = first.column(delta.x + dx, delta.z + dz);
                    const QcTerrainColumn two = again.column(delta.x + dx, delta.z + dz);
                    QCOMPARE(one.height, two.height);
                    QCOMPARE(one.biome, two.biome);
                }
            }
            const QcChunkPos pos = QcBlockPos{delta.x, 0, delta.z}.chunk();
            const std::shared_ptr<QcChunk> a = first.generate(pos);
            const std::shared_ptr<QcChunk> b = again.generate(pos);
            for (int y = 0; y < QcWorld::worldHeight; ++y) {
                for (int i = 0; i < QcWorld::chunkSize; ++i) {
                    QCOMPARE(a->block(i, y, i), b->block(i, y, i));
                }
            }
        }
    }

    // Some seeds have deltas, and their arms are one body of water with the sea: the deep water
    // (the arms) nearly all of it, the rest but the ponds on the islands. Where two rivers meet a
    // bay from opposite sides, the fans overlap and a side arm may end short of the sea: at most
    // one such delta per seed, and none cut off by much. 161831057 and 649604540 (random,
    // decision E-907) had a patch of fan beside the river near its mouth, found by a few cells
    // only and cut off from the sea; a delta needs the cells at its apex now.
    void armsRunIntoTheSea()
    {
        for (const qint64 seed : {qint64{7}, qint64{42}, qint64{161831057}, qint64{649604540}, m_randomSeed}) {
            const QcTerrainGenerator &generator = generatorOf(m_content, seed);
            const QList<Delta> deltas = deltasOf(seed, 3);
            int cutShort = 0;
            for (const Delta &delta : deltas) {
                const Waters waters = watersAround(generator, delta, 240);
                const double reached = static_cast<double>(waters.reached) / static_cast<double>(std::max<qint64>(1, waters.deltaWater));
                const double deep = static_cast<double>(waters.deepReached) / static_cast<double>(std::max<qint64>(1, waters.deepWater));
                qInfo("seed %lld, delta at %d %d: water %lld, %.3f joined to the sea; deep %lld, %.3f", seed, delta.x, delta.z,
                      waters.deltaWater, reached, waters.deepWater, deep);
                const QString where = u"seed %1 at %2 %3: %4, %5"_s.arg(seed).arg(delta.x).arg(delta.z).arg(deep).arg(reached);
                QVERIFY2(waters.deepWater > 50 && deep > 0.85 && reached > 0.6, qPrintable(where));
                cutShort += deep < 0.95 ? 1 : 0;
            }
            QVERIFY2(cutShort <= 1, qPrintable(u"seed %1: %2 deltas with arms short of the sea"_s.arg(seed).arg(cutShort)));
        }
    }

    // No water stands beside or over air in and around deltas, also across chunk borders.
    void noWaterBesideAir()
    {
        for (const qint64 seed : {qint64{7}, qint64{42}, m_randomSeed}) {
            const QcTerrainGenerator &generator = generatorOf(m_content, seed);
            const QList<Delta> deltas = deltasOf(seed, 2);
            for (const Delta &delta : deltas) {
                for (const auto [dx, dz] : std::array<std::pair<int, int>, 3>{{{0, 0}, {-40, 24}, {32, -40}}}) {
                    const QcChunkPos centre = QcBlockPos{delta.x + dx, 0, delta.z + dz}.chunk();
                    const QStringList open = QcWaterTestUtil::openWater(generator, QcWaterTestUtil::ChunkArea(generator, centre), centre);
                    QVERIFY2(open.isEmpty(), qPrintable(u"seed %1: water beside air at "_s.arg(seed) + open.mid(0, 5).join(u", "_s)));
                }
            }
        }
    }

    // The land of a delta lies just above the sea: sand banks flush with the water and islands a
    // block or two above it; nothing of it stands high.
    void islandsJustAboveTheSea()
    {
        for (const qint64 seed : {qint64{7}, qint64{42}, noDeltaSeed, m_randomSeed}) {
            const auto release = releaseOneOff(m_content, seed);
            const QcTerrainGenerator &generator = generatorOf(m_content, seed);
            const QList<Delta> deltas = deltasOf(seed, 3);
            qint64 land = 0;
            qint64 low = 0;
            int highest = 0;
            for (const Delta &delta : deltas) {
                for (int dz = -96; dz <= 96; ++dz) {
                    for (int dx = -96; dx <= 96; ++dx) {
                        const QcTerrainColumn column = generator.column(delta.x + dx, delta.z + dz);
                        if (!generator.isDeltaBiome(column.biome) || column.height < generator.seaLevel()) {
                            continue;
                        }
                        const int above = column.height - generator.seaLevel();
                        ++land;
                        low += above <= 2 ? 1 : 0;
                        highest = std::max(highest, above);
                    }
                }
            }
            // Only a seed that may lack deltas finds no delta land (and nothing to measure).
            if (deltas.isEmpty()) {
                QVERIFY2(mayLackDeltas(seed) && land == 0, qPrintable(u"seed %1"_s.arg(seed)));
                continue;
            }
            qInfo("seed %lld: delta land %lld columns, %.3f at most 2 above the sea, highest %d", seed, land,
                static_cast<double>(low) / static_cast<double>(land), highest);
            QVERIFY2(land > 300, qPrintable(u"seed %1: %2 columns"_s.arg(seed).arg(land)));
            QVERIFY2(double(low) / double(land) > 0.9 && highest <= 10, qPrintable(u"seed %1"_s.arg(seed)));
        }
    }

    // Deltas cover a small share of the land.
    void shareSmallAndBounded()
    {
        for (const qint64 seed : {qint64{7}, qint64{42}, qint64{161831057}, m_randomSeed}) {
            const Survey &area = surveyOf(m_content, seed, 3072);
            const double share = static_cast<double>(area.delta) / static_cast<double>(area.land);
            qInfo("seed %lld: deltas %.4f of the land, %lld deltas of 20 points and more", seed, share, static_cast<qint64>(largest(area.deltas, 20).size()));
            QVERIFY2((share > 0.0 || seed == m_randomSeed) && share < 0.004, qPrintable(u"seed %1: %2"_s.arg(seed).arg(share)));
        }
    }

    // Deltas are rare (the human's decision, E-850): about one in ten wide river mouths gets one.
    // The mouths that would get a delta are the deltas with a share of 1; over the seeds at most
    // 25 % of them get one, and each fixed seed has one or more in its window. The chance noise
    // spans some 1100 blocks, so neighbouring mouths share it and a window's share varies widely
    // (48766985, a fixed seed: 7 deltas of 23 mouths; decision E-907): 20 % failed on the four
    // seeds with it.
    void fewMouthsGetADelta()
    {
        qint64 mouths = 0;
        qint64 deltas = 0;
        for (const qint64 seed : {qint64{7}, qint64{42}, qint64{161831057}, qint64{48766985}, m_randomSeed}) {
            const qsizetype wide = largest(surveyOf(m_everyMouth, seed, 3072).deltas, 20).size();
            const qsizetype found = largest(surveyOf(m_content, seed, 3072).deltas, 20).size();
            qInfo("seed %lld: %lld wide river mouths, %lld deltas", seed, static_cast<qint64>(wide), static_cast<qint64>(found));
            mouths += wide;
            deltas += found;
        }
        const double share = static_cast<double>(deltas) / static_cast<double>(std::max<qint64>(1, mouths));
        qInfo("%lld deltas of %lld wide river mouths: %.3f", deltas, mouths, share);
        QVERIFY2(mouths >= 20 && share <= 0.25, qPrintable(u"%1 of %2"_s.arg(deltas).arg(mouths)));
    }

private:
    // The water of a cold delta's chunk: shallow columns under ice, deeper ones open, and the first
    // column whose top is neither (empty for none).
    struct Frozen
    {
        int ice = 0;
        int open = 0;
        QString wrong;
    };
    [[nodiscard]] Frozen frozenIn(const QcTerrainGenerator &generator, const Delta &delta) const
    {
        Frozen frozen;
        const QcChunkPos pos = QcBlockPos{delta.x, 0, delta.z}.chunk();
        const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
        for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
            for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
                const QcTerrainColumn column = generator.column(pos.x * QcWorld::chunkSize + lx, pos.z * QcWorld::chunkSize + lz);
                const int depth = generator.seaLevel() - column.height;
                if (generator.biomeName(column.biome) != delta.biome || depth <= 0 || !frozen.wrong.isEmpty()) {
                    continue;
                }
                // Shallow edges freeze by the column's temperature (decision E-858): ice or, on the
                // warmer edges of a cold delta, open water; deeper water stays open.
                const QcBlockId top = chunk->block(lx, generator.seaLevel() - 1, lz);
                const bool iced = top == id(u"qtcraft:ice"_s);
                frozen.ice += iced ? 1 : 0;
                frozen.open += depth >= 2 && top == generator.waterBlock() ? 1 : 0;
                if ((depth == 1 && !iced && top != generator.waterBlock()) || (depth >= 2 && top != generator.waterBlock())) {
                    frozen.wrong = u"%1 at depth %2"_s.arg(m_content->block(top).name).arg(depth);
                }
            }
        }
        return frozen;
    }

private slots:
    // Cold deltas freeze at their shallow edges only: ice where the water is a block deep, open
    // water in the deeper arms (with a delta at every wide mouth: the base game's are too rare to
    // hold a cold one on every seed).
    void coldDeltasFreezeAtTheEdges()
    {
        int ice = 0;
        int open = 0;
        for (const qint64 seed : {qint64{7}, qint64{42}}) {
            const QcTerrainGenerator &generator = generatorOf(m_everyMouth, seed);
            const QList<Delta> deltas = largest(surveyOf(m_everyMouth, seed, 3072).deltas, 20);
            for (const Delta &delta : deltas) {
                if (delta.biome != u"qtcraft:frozen_delta"_s) {
                    continue;
                }
                const Frozen frozen = frozenIn(generator, delta);
                QVERIFY2(frozen.wrong.isEmpty(), qPrintable(u"seed %1: "_s.arg(seed) + frozen.wrong));
                ice += frozen.ice;
                open += frozen.open;
            }
        }
        qInfo("frozen deltas: %d shallow columns under ice, %d deeper ones open", ice, open);
        QVERIFY(ice > 0 && open > 0);
    }

    // The delta columns in the rectangle from `from` to `to` (x and z, both inclusive): every column.
    static QStringList deltaColumnsIn(const QcTerrainGenerator &generator, QcBlockPos from, QcBlockPos to)
    {
        QStringList found;
        for (int z = from.z; z <= to.z; ++z) {
            for (int x = from.x; x <= to.x; ++x) {
                if (generator.isDeltaBiome(generator.column(x, z).biome)) {
                    found.append(u"%1 %2"_s.arg(x).arg(z));
                }
            }
        }
        return found;
    }

    // The delta columns under the pieces, road cells (with the paths to the doors), decks and bridges
    // of a village.
    static QStringList deltaColumnsOf(const QcTerrainGenerator &generator, const QcVillagePlan &plan)
    {
        QStringList found;
        for (const QcStructurePlacement &piece : plan.pieces) {
            const QcBlockPos at = piece.origin();
            found += deltaColumnsIn(generator, at, {at.x + piece.sizeX() - 1, 0, at.z + piece.sizeZ() - 1});
        }
        const QList<QcBlockPos> roads = plan.roads + plan.branchRoads;
        for (const QcBlockPos &cell : roads) {
            found += deltaColumnsIn(generator, cell, cell);
        }
        const QList<QcVillagePlan::Deck> decks = plan.bridges + plan.decks;
        for (const QcVillagePlan::Deck &deck : decks) {
            found += deltaColumnsIn(generator, deck.at, deck.at);
        }
        return found;
    }

    // The footprints of the structures, coastal towers and lakes of the chunks within `reach` of
    // `middle`, each once.
    static QList<QcSurfacePlanner::Rect> footprintsAround(const QcTerrainGenerator &generator, QcChunkPos middle, int reach)
    {
        QList<QcSurfacePlanner::Rect> footprints;
        std::set<std::tuple<int, int, int, int>> seen;
        for (int cz = middle.z - reach; cz <= middle.z + reach; ++cz) {
            for (int cx = middle.x - reach; cx <= middle.x + reach; ++cx) {
                const QList<QcSurfacePlanner::Rect> near = generator.structureFootprintsNear({cx, cz}, 0, false);
                for (const QcSurfacePlanner::Rect &rect : near) {
                    if (seen.insert({rect.x0, rect.z0, rect.x1, rect.z1}).second) {
                        footprints.append(rect);
                    }
                }
            }
        }
        return footprints;
    }

    // Villages and structures keep off deltas: no column of a piece, road, path, deck or bridge of a
    // village and none of the footprint of a structure lies in a delta (with a delta at every wide
    // mouth, for more villages near deltas). Seed 613409102 has a ruined portal whose edge an arm
    // crosses between the samples of its ground, seed 23846387 a village jetty out over an arm.
    void villagesAndStructuresKeepOff()
    {
        int checked = 0;
        for (const qint64 seed : {qint64{7}, qint64{42}, qint64{1234}, qint64{613409102}, qint64{23846387}, m_randomSeed}) {
            const auto release = releaseOneOff(m_everyMouth, seed);
            const QcTerrainGenerator &generator = generatorOf(m_everyMouth, seed);
            const QList<Delta> deltas = largest(surveyOf(m_everyMouth, seed, 3072).deltas, 20);
            for (const Delta &delta : deltas) {
                const QcChunkPos middle = QcBlockPos{delta.x, 0, delta.z}.chunk();
                const QList<QcSurfacePlanner::Rect> footprints = footprintsAround(generator, middle, 8);
                for (const QcSurfacePlanner::Rect &rect : footprints) {
                    const QStringList under = deltaColumnsIn(generator, {rect.x0, 0, rect.z0}, {rect.x1, 0, rect.z1});
                    QVERIFY2(under.isEmpty(), qPrintable(u"seed %1: structure at %2"_s.arg(seed).arg(under.value(0))));
                }
                QSet<qint64> seen;
                for (int cz = middle.z - 8; cz <= middle.z + 8; ++cz) {
                    for (int cx = middle.x - 8; cx <= middle.x + 8; ++cx) {
                        const std::shared_ptr<const QcVillagePlan> plan = generator.villageFor({cx, cz});
                        if (!plan || seen.contains(plan->id)) {
                            continue;
                        }
                        seen.insert(plan->id);
                        ++checked;
                        const QStringList onDelta = deltaColumnsOf(generator, *plan);
                        QVERIFY2(onDelta.isEmpty(), qPrintable(u"seed %1: village at %2"_s.arg(seed).arg(onDelta.value(0))));
                    }
                }
            }
        }
        qInfo("%d villages near deltas checked", checked);
    }

    // The loader checks the fields of deltas: a surface patch needs its share and must be solid
    // ground, a biome is a river or a delta biome, not both, a land biome's delta biome must be one,
    // and fields that do not apply are ignored with a warning.
    void loaderChecksDeltaFields()
    {
        const QJsonObject missingShare =
            QJsonDocument::fromJson(R"({"name":"d","delta":true,"surface":"mud","surfacePatches":[{"block":"sand"}]})").object();
        QVERIFY(!QcContentParser::parseBiome(missingShare, u"b"_s).has_value());
        const QJsonObject both = QJsonDocument::fromJson(R"({"name":"d","delta":true,"river":true,"surface":"mud"})").object();
        QVERIFY(!QcContentParser::parseBiome(both, u"b"_s).has_value());
        const QJsonObject tooDeep = QJsonDocument::fromJson(R"({"name":"d","delta":true,"surface":"mud","frozenDepth":40})").object();
        QVERIFY(!QcContentParser::parseBiome(tooDeep, u"b"_s).has_value());

        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcBiomeDefinition delta = *content->worldgen().biome(u"qtcraft:delta"_s);
        delta.surfacePatches.append({u"qtcraft:water"_s, 0.1});
        delta.deltaBiome = u"qtcraft:frozen_delta"_s;
        content->worldgen().registerBiome(delta);
        QcBiomeDefinition plains = *content->worldgen().biome(u"qtcraft:plains"_s);
        plains.deltaBiome = u"qtcraft:river"_s;
        plains.surfacePatches.append({u"qtcraft:sand"_s, 0.1});
        content->worldgen().registerBiome(plains);
        const QcTerrainGenerator generator(content, 1);
        const QString warnings = generator.warnings().join(u'\n');
        for (const QString &expected : {u"surface patch qtcraft:water"_s, u"qtcraft:delta is no land biome and names a delta biome"_s,
                                        u"names qtcraft:river as its delta biome"_s, u"qtcraft:plains has surface patches"_s}) {
            QVERIFY2(warnings.contains(expected), qPrintable(expected + u": "_s + warnings));
        }
    }
};

QTEST_GUILESS_MAIN(TestRiverDeltas)
#include "test_river_deltas.moc"
