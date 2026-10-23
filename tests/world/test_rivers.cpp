#include "common/content/parser/QcContentParser.h"
#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "world/QcWaterTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

// What a column of a survey window holds: dry land, river water or other water (sea, lakes).
enum class Cell : quint8 { Land, River, OtherWater };

// The columns of a square window around the origin on a grid of `step` blocks.
struct Window
{
    int reach = 0;
    int step = 0;
    int cells = 0;
    QList<Cell> grid;
    // Land columns (dry land and rivers, coasts left out) and river columns, for the share of rivers.
    qint64 land = 0;
    qint64 river = 0;
    // Land and river columns in dry and in humid climates (decision E-840: fewer rivers in deserts).
    qint64 dryLand = 0;
    qint64 dryRiver = 0;
    qint64 humidLand = 0;
    qint64 humidRiver = 0;
    // River columns where the climate leaves rivers no strength (QcRiverField: humidity under 0.05).
    qint64 desertRiver = 0;
};

// What a column holds; land and river columns are counted in the window's shares.
Cell sampleColumn(const QcTerrainGenerator &generator, Window &window, int x, int z)
{
    const QcTerrainColumn column = generator.column(x, z);
    const bool river = generator.isRiverBiome(column.biome);
    const bool water = column.height < generator.seaLevel();
    if (river || (!water && column.landFactor > 0.99)) {
        ++window.land;
        window.river += river ? 1 : 0;
        const double humidity = generator.climateAt(x, z).humidity;
        window.dryLand += humidity < 0.15 ? 1 : 0;
        window.dryRiver += humidity < 0.15 && river ? 1 : 0;
        window.humidLand += humidity > 0.4 ? 1 : 0;
        window.humidRiver += humidity > 0.4 && river ? 1 : 0;
        window.desertRiver += humidity < 0.05 && river ? 1 : 0;
    }
    return river ? Cell::River : (water ? Cell::OtherWater : Cell::Land);
}

Window survey(const QcTerrainGenerator &generator, int reach, int step)
{
    Window window{reach, step, 2 * reach / step, {}};
    window.grid.resize(qsizetype(window.cells) * window.cells);
    for (int j = 0; j < window.cells; ++j) {
        for (int i = 0; i < window.cells; ++i) {
            window.grid[qsizetype(j) * window.cells + i] = sampleColumn(generator, window, -reach + i * step, -reach + j * step);
        }
    }
    return window;
}

// River columns by where their body of water leads: into other water (the sea or a lake), out of
// the window (it goes on outside), or nowhere (a river closed inside the window).
struct Outlets
{
    qint64 joined = 0;
    qint64 leaving = 0;
    qint64 closed = 0;
};

// One body of river water flooded from a cell: its size and where it leads.
struct Body
{
    qint64 size = 0;
    bool joins = false;
    bool leaves = false;
};

// Floods the river cells connected to `start` (marking them in `seen`, a scratch list of the caller).
Body flood(const Window &window, QList<bool> &seen, qsizetype start)
{
    // River cells up to two cells apart belong together: a narrow river in a gorge (three blocks
    // of water) running at a slant of two blocks along to one across is joined block by block but
    // leaves gaps of one cell on the grid of two blocks. Other water joins as one of the eight
    // neighbours.
    Body body;
    QList<qsizetype> stack{start};
    seen[start] = true;
    while (!stack.isEmpty()) {
        const qsizetype cell = stack.takeLast();
        ++body.size;
        const int i = int(cell % window.cells);
        const int j = int(cell / window.cells);
        body.leaves = body.leaves || i == 0 || j == 0 || i == window.cells - 1 || j == window.cells - 1;
        for (int dj = -2; dj <= 2; ++dj) {
            for (int di = -2; di <= 2; ++di) {
                const int ni = i + di;
                const int nj = j + dj;
                if (ni < 0 || nj < 0 || ni >= window.cells || nj >= window.cells) {
                    continue;
                }
                const qsizetype next = qsizetype(nj) * window.cells + ni;
                body.joins = body.joins || (std::abs(di) <= 1 && std::abs(dj) <= 1 && window.grid[next] == Cell::OtherWater);
                if (window.grid[next] == Cell::River && !seen[next]) {
                    seen[next] = true;
                    stack.append(next);
                }
            }
        }
    }
    return body;
}

Outlets outletsOf(const Window &window)
{
    Outlets outlets;
    QList<bool> seen(window.grid.size(), false);
    for (qsizetype start = 0; start < window.grid.size(); ++start) {
        if (window.grid[start] != Cell::River || seen[start]) {
            continue;
        }
        const Body body = flood(window, seen, start);
        (body.joins ? outlets.joined : body.leaves ? outlets.leaving : outlets.closed) += body.size;
    }
    return outlets;
}

// River columns on a coarse grid, at least `apart` blocks (taxicab) from each other.
QList<std::pair<int, int>> riverColumns(const QcTerrainGenerator &generator, int count, int apart)
{
    QList<std::pair<int, int>> found;
    for (int z = -2048; z < 2048 && found.size() < count; z += 3) {
        for (int x = -2048; x < 2048 && found.size() < count; x += 37) {
            const bool far = std::ranges::none_of(found, [&](auto at) { return std::abs(at.first - x) + std::abs(at.second - z) < apart; });
            if (far && generator.isRiverBiome(generator.column(x, z).biome)) {
                found.append({x, z});
            }
        }
    }
    return found;
}

} // namespace

// Rivers (decision E-840): winding channels at the sea level through the land, down to the sea.
class TestRivers : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // The random seed of this run besides the fixed ones (decision E-846): QC_TEST_SEED if set.
    const qint64 m_randomSeed = randomSeed();

    static qint64 randomSeed()
    {
        bool ok = false;
        const qint64 fixed = qEnvironmentVariable("QC_TEST_SEED").toLongLong(&ok);
        const qint64 seed = ok ? fixed : qint64(QRandomGenerator::global()->generate() & 0x7fffffff);
        qInfo("random seed %lld (set QC_TEST_SEED to repeat)", seed);
        return seed;
    }

    [[nodiscard]] QcBlockId id(const QString &name) const { return m_content->blockId(name).value_or(qcAirBlockId); }

    // The bridges of the plan: each stretch of decks on the middle line of a road run, from its
    // first deck on in the run's direction (`along`, along x, z or a diagonal). Across a diagonal
    // road a straight line along x or z leaves the deck at its side, beside the road, so the walk
    // follows the road.
    struct Bridge
    {
        QcBlockPos at;
        std::pair<int, int> along;
    };
    static QList<Bridge> bridgesOf(const QcTerrainGenerator &generator, const QcVillagePlan &plan)
    {
        QSet<std::pair<int, int>> decks;
        for (const QcVillagePlan::Deck &deck : plan.bridges) {
            decks.insert({deck.at.x, deck.at.z});
        }
        QList<Bridge> bridges;
        for (const QcVillagePlan::Run &run : plan.runs) {
            for (int d = 0; d < run.length; ++d) {
                const int x = run.x + run.dx * d;
                const int z = run.z + run.dz * d;
                if (decks.contains({x, z}) && !decks.contains({x - run.dx, z - run.dz})) {
                    bridges.append(Bridge{{x, generator.seaLevel(), z}, {run.dx, run.dz}});
                }
            }
        }
        return bridges;
    }

    // A bridge crosses a river from dry land to dry land (decision E-841): some of the water under
    // its decks is a river's (the rest may be other water beside it, not a lake or the sea alone),
    // and the first cell past its decks each way is no water and no low bank under the sea level.
    static void checkCrossing(const QcTerrainGenerator &generator, const QcVillagePlan &plan, const Bridge &bridge)
    {
        QSet<std::pair<int, int>> decks;
        for (const QcVillagePlan::Deck &deck : plan.bridges) {
            decks.insert({deck.at.x, deck.at.z});
        }
        bool river = false;
        for (const int sign : {1, -1}) {
            int x = bridge.at.x;
            int z = bridge.at.z;
            while (decks.contains({x, z})) {
                river = river || generator.isRiverBiome(generator.column(x, z).biome);
                x += sign * bridge.along.first;
                z += sign * bridge.along.second;
            }
            QVERIFY2(generator.column(x, z).height >= generator.seaLevel(),
                     qPrintable(u"bridge at %1 %2 ends in water at %3 %4"_s.arg(bridge.at.x).arg(bridge.at.z).arg(x).arg(z)));
        }
        QVERIFY2(river, qPrintable(u"bridge at %1 %2 over no river"_s.arg(bridge.at.x).arg(bridge.at.z)));
    }

    // Both banks of a bridge join its deck as villagers walk (QcVillagerRoads, decision E-411): the
    // first cell past each end of the deck is a road cell at most a block above or below it.
    static void checkBanksJoined(const QcVillagePlan &plan, const Bridge &bridge)
    {
        QSet<std::pair<int, int>> decks;
        for (const QcVillagePlan::Deck &deck : plan.bridges) {
            decks.insert({deck.at.x, deck.at.z});
        }
        QHash<std::pair<int, int>, int> roads;
        const QList<QcBlockPos> cells = plan.roads + plan.branchRoads;
        for (const QcBlockPos &cell : cells) {
            roads.insert({cell.x, cell.z}, cell.y);
        }
        for (const int sign : {1, -1}) {
            int x = bridge.at.x;
            int z = bridge.at.z;
            while (decks.contains({x, z})) {
                x += sign * bridge.along.first;
                z += sign * bridge.along.second;
            }
            const std::optional<int> road = roads.contains({x, z}) ? std::optional<int>(roads.value({x, z})) : std::nullopt;
            QVERIFY2(road && std::abs(*road - bridge.at.y) <= 1,
                     qPrintable(u"bank at %1 %2: %3"_s.arg(x).arg(z).arg(road ? QString::number(*road) : u"no road"_s)));
        }
    }

    // A plaza's ring closes (a ring arm is laid only in full): an arm of four runs that each turn a
    // quarter clockwise, whose last run ends on the line of its first, ends at its start. A corner
    // of the ring on a bridge leaves the plaza out instead of moving the corner.
    static void checkRingsClose(const QcVillagePlan &plan)
    {
        QMap<int, QList<QcVillagePlan::Run>> arms;
        for (const QcVillagePlan::Run &run : plan.runs) {
            arms[run.arm].append(run);
        }
        for (const QList<QcVillagePlan::Run> &runs : std::as_const(arms)) {
            const bool square = runs.size() == 4 && std::ranges::all_of(runs, [](const QcVillagePlan::Run &run) {
                return (run.dx == 0) != (run.dz == 0);
            });
            bool clockwise = square;
            for (qsizetype i = 1; clockwise && i < runs.size(); ++i) {
                clockwise = runs[i].dx == -runs[i - 1].dz && runs[i].dz == runs[i - 1].dx;
            }
            if (!clockwise) {
                continue;
            }
            const QcVillagePlan::Run &first = runs.front();
            const QcVillagePlan::Run &last = runs.back();
            const int endX = last.x + last.dx * last.length;
            const int endZ = last.z + last.dz * last.length;
            const bool onFirstLine = first.dx != 0 ? endZ == first.z : endX == first.x;
            if (onFirstLine && std::abs(endX - first.x) + std::abs(endZ - first.z) <= 24) {
                QVERIFY2(endX == first.x && endZ == first.z,
                         qPrintable(u"ring from %1 %2 ends at %3 %4"_s.arg(first.x).arg(first.z).arg(endX).arg(endZ)));
            }
        }
    }

    // Every post under a deck of the plan's bridges reaches down to a solid bed (not ice).
    void checkPosts(const QcTerrainGenerator &generator, const QcVillagePlan &plan) const
    {
        for (const QcVillagePlan::Deck &deck : plan.bridges) {
            if (deck.post == qcAirBlockId) {
                continue;
            }
            const std::shared_ptr<QcChunk> chunk = generator.generate(deck.at.chunk());
            const auto at = [&](int y) { return chunk->block(deck.at.localX(), y, deck.at.localZ()); };
            int y = deck.at.y - 1;
            while (y > 0 && at(y) == deck.post) {
                --y;
            }
            const QcBlockDefinition &foot = m_content->block(at(y));
            QVERIFY2(foot.solid && !foot.liquid && at(y) != id(u"qtcraft:ice"_s),
                     qPrintable(u"%1 %2 %3"_s.arg(deck.at.x).arg(y).arg(deck.at.z)));
        }
    }

private slots:
    void sameSeedSameRivers()
    {
        const QcTerrainGenerator first(m_content, 42);
        const QcTerrainGenerator again(m_content, 42);
        const QList<std::pair<int, int>> rivers = riverColumns(first, 6, 512);
        QVERIFY(rivers.size() == 6);
        for (const auto [x, z] : rivers) {
            const QcRiverField::Sample one = first.riverAt(x, z);
            const QcRiverField::Sample two = again.riverAt(x, z);
            QCOMPARE(one.distance, two.distance);
            QCOMPARE(one.halfWidth, two.halfWidth);
            const QcChunkPos pos = QcBlockPos{x, 0, z}.chunk();
            const std::shared_ptr<QcChunk> a = first.generate(pos);
            const std::shared_ptr<QcChunk> b = again.generate(pos);
            for (int y = 0; y < QcWorld::worldHeight; ++y) {
                QCOMPARE(a->block(x - pos.x * QcWorld::chunkSize, y, z - pos.z * QcWorld::chunkSize),
                         b->block(x - pos.x * QcWorld::chunkSize, y, z - pos.z * QcWorld::chunkSize));
            }
        }
    }

    // The river field tells where the water is (for later stages such as deltas): river columns lie
    // inside the channel's width, and their water stands at the sea level over a solid bed.
    void riverColumnsHoldWaterAtTheSeaLevel()
    {
        const QcTerrainGenerator generator(m_content, 7);
        const QList<std::pair<int, int>> rivers = riverColumns(generator, 24, 256);
        QVERIFY(rivers.size() >= 16);
        for (const auto [x, z] : rivers) {
            const QcTerrainColumn column = generator.column(x, z);
            const QcRiverField::Sample river = generator.riverAt(x, z);
            QVERIFY2(river.distance < river.halfWidth + 1.0,
                     qPrintable(u"%1 %2: %3 of %4"_s.arg(x).arg(z).arg(river.distance).arg(river.halfWidth)));
            QVERIFY(column.height < generator.seaLevel());

            const QcBlockPos bed{x, column.height - 1, z};
            const std::shared_ptr<QcChunk> chunk = generator.generate(bed.chunk());
            const QcBlockId top = chunk->block(bed.localX(), generator.seaLevel() - 1, bed.localZ());
            QVERIFY(top == generator.waterBlock() || top == id(u"qtcraft:ice"_s));
            QCOMPARE(chunk->block(bed.localX(), generator.seaLevel(), bed.localZ()), qcAirBlockId);
            QVERIFY(m_content->block(chunk->block(bed.localX(), bed.y, bed.localZ())).solid);
        }
    }

    // No water stands beside or over air around rivers, also across chunk borders: no walls of
    // water, no floating water, nothing that flows when the chunks load.
    void noWaterBesideAir()
    {
        for (const qint64 seed : {qint64(42), qint64(1234), m_randomSeed}) {
            const QcTerrainGenerator generator(m_content, seed);
            const QList<std::pair<int, int>> rivers = riverColumns(generator, 8, 384);
            QVERIFY2(rivers.size() >= 6, qPrintable(u"seed %1"_s.arg(seed)));
            for (const auto [x, z] : rivers) {
                const QcChunkPos centre = QcBlockPos{x, 0, z}.chunk();
                const QStringList open = QcWaterTestUtil::openWater(generator, QcWaterTestUtil::ChunkArea(generator, centre), centre);
                QVERIFY2(open.isEmpty(), qPrintable(u"water beside air at "_s + open.mid(0, 5).join(u", "_s)));
            }
        }
    }

    // Rivers in cold land freeze: in the frozen river biome the top water layer is the block the
    // temperature asks for (ice in the cold part, decision E-858), and some of it is ice.
    void coldRiversFreeze()
    {
        int frozen = 0;
        for (const qint64 seed : {qint64(7), qint64(42), qint64(1234)}) {
            const QcTerrainGenerator generator(m_content, seed);
            const QList<std::pair<int, int>> rivers = riverColumns(generator, 60, 128);
            for (const auto [x, z] : rivers) {
                if (generator.biomeName(generator.column(x, z).biome) != u"qtcraft:frozen_river"_s) {
                    continue;
                }
                // Villages build over rivers (fields with water channels): only the terrain's own rivers.
                const QcBlockPos pos{x, generator.seaLevel() - 1, z};
                if (generator.villageFor(pos.chunk())) {
                    continue;
                }
                // Rivers freeze by the temperature of the column (decision E-858), in the cold part of the
                // cold biomes.
                const QcBlockId top = generator.generate(pos.chunk())->block(pos.localX(), pos.y, pos.localZ());
                const QcBlockId expected = generator.frozenWaterAt(x, z, generator.column(x, z));
                QVERIFY2(top == expected, qPrintable(u"%1 at %2 %3, seed %4"_s.arg(m_content->block(top).name).arg(x).arg(z).arg(seed)));
                if (top != id(u"qtcraft:ice"_s)) {
                    continue;
                }
                if (frozen++ == 0) {
                    qInfo("seed %lld: frozen river at %d %d", seed, x, z);
                }
            }
        }
        QVERIFY2(frozen > 0, "no frozen river found");
    }

    // Roads of villages cross rivers on bridges (decision E-841): a deck one block above the
    // water from dry bank to dry bank, joined to the roads on both banks, carried by posts down to
    // the bed; no bridge over a lake or the sea.
    void villageRoadsCrossRiversOnBridges()
    {
        int bridges = 0;
        // 1059853484: a diagonal bridge with low dry bank beside its deck; 555555 and 888888888: roads
        // that turned on a bridge (no longer: bridges run straight); 298323796: failed on the old check;
        // 20261017: a bridge over a river and the low water beside it in another biome.
        // 782953143 and 614800303: failed on earlier bridge placements.
        for (const qint64 seed : {qint64(7), qint64(42), qint64(1234), qint64(1059853484), qint64(555555), qint64(888888888),
                                  qint64(298323796), qint64(20261017), qint64(782953143), qint64(614800303), m_randomSeed}) {
            const QcTerrainGenerator generator(m_content, seed);
            QSet<qint64> seen;
            // Villages lie a region (384 blocks, 24 chunks) apart: every sixth chunk finds each.
            for (int cz = -256; cz < 256; cz += 6) {
                for (int cx = -256; cx < 256; cx += 6) {
                    const std::shared_ptr<const QcVillagePlan> plan = generator.villageFor({cx, cz});
                    if (!plan || seen.contains(plan->id)) {
                        continue;
                    }
                    seen.insert(plan->id);
                    checkPosts(generator, *plan);
                    checkRingsClose(*plan);
                    if (QTest::currentTestFailed()) {
                        return;
                    }
                    const QList<Bridge> crossings = bridgesOf(generator, *plan);
                    for (const Bridge &bridge : std::as_const(crossings)) {
                        qInfo("seed %lld: village at %d %d (%s) crosses a river at %d %d", seed, plan->centre.x, plan->centre.z,
                              qPrintable(plan->style), bridge.at.x, bridge.at.z);
                        ++bridges;
                        checkCrossing(generator, *plan, bridge);
                        checkBanksJoined(*plan, bridge);
                        if (QTest::currentTestFailed()) {
                            return;
                        }
                    }
                }
            }
        }
        QVERIFY2(bridges >= 6, qPrintable(QString::number(bridges)));
    }

    // The loader checks the fields of rivers: a patch of the river bed needs its share and must be
    // solid ground (else left out with a warning), and a river biome's own river biome and rarity
    // are ignored with a warning.
    void loaderChecksRiverFields()
    {
        const QJsonObject missingShare =
            QJsonDocument::fromJson(R"({"name":"r","river":true,"underwaterPatches":[{"block":"sand"}]})").object();
        QVERIFY(!QcContentParser::parseBiome(missingShare, u"b"_s).has_value());

        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcBiomeDefinition river = *content->worldgen().biome(u"qtcraft:river"_s);
        river.underwaterPatches.append({u"qtcraft:water"_s, 0.1});
        river.underwaterPatches.append({u"qtcraft:poppy"_s, 0.1});
        river.riverBiome = u"qtcraft:frozen_river"_s;
        river.rarity = 0.5;
        content->worldgen().registerBiome(river);
        const QcTerrainGenerator generator(content, 1);
        const QString warnings = generator.warnings().join(u'\n');
        for (const QString &expected : {u"underwater patch qtcraft:water"_s, u"underwater patch qtcraft:poppy"_s,
                                        u"names a river biome of its own"_s, u"River biome qtcraft:river has a rarity"_s}) {
            QVERIFY2(warnings.contains(expected), qPrintable(expected + u": "_s + warnings));
        }
    }

    // Rivers cover a small share of the land, run into the sea or lakes (or on out of the window),
    // and are rarer in dry land.
    void shareAndOutlets()
    {
        // The dry and humid land and river columns of all seeds together.
        Window climates;
        // 1448947576: its window's dry land lies almost all at a humidity of 0.12 to 0.15, where rivers
        // are narrower but not rarer; its own dry share equalled the humid one. 345777499, 476165307
        // 807288324 and 2025406668 (random, decision E-846) had 8–14 % closed river columns before
        // closed pieces were left out (decision E-851): large rings and arcs ending on land at both
        // ends. 1360590162 (random) had 14 %: a long river whose gorge stretch was water a block
        // wide, joined only at corners, and three blocks wide still broke on the survey grid.
        for (const qint64 seed : {qint64(7), qint64(42), qint64(1234), qint64(1034578912), qint64(1448947576), qint64(345777499),
                                  qint64(476165307), qint64(807288324), qint64(2025406668), qint64(1360590162), m_randomSeed}) {
            const QcTerrainGenerator generator(m_content, seed);
            const Window window = survey(generator, 1536, 2);
            climates.dryLand += window.dryLand;
            climates.dryRiver += window.dryRiver;
            climates.humidLand += window.humidLand;
            climates.humidRiver += window.humidRiver;
            const Outlets outlets = outletsOf(window);
            const double share = double(window.river) / double(window.land);
            const double closed = double(outlets.closed) / double(outlets.joined + outlets.leaving + outlets.closed);
            const double dry = double(window.dryRiver) / double(std::max<qint64>(1, window.dryLand));
            const double humid = double(window.humidRiver) / double(std::max<qint64>(1, window.humidLand));
            qInfo("seed %lld: rivers %.4f of the land (dry %.4f of %lld, humid %.4f of %lld); river columns into other water %lld, "
                  "out of the window %lld, closed %lld (%.3f)",
                  seed, share, dry, window.dryLand, humid, window.humidLand, outlets.joined, outlets.leaving, outlets.closed, closed);
            QVERIFY2(share > 0.012 && share < 0.04, qPrintable(QString::number(share)));
            // Closed: river bodies that neither reach other water nor leave the window. Pieces that end
            // on land at both ends are not built (decision E-851): 0.0–0.7 % on eleven seeds. Before
            // E-851: 0.3–14 %, rings and arcs ending on land.
            QVERIFY2(closed < 0.08, qPrintable(QString::number(closed)));
            // Deserts have no rivers (decision E-840): under a humidity of 0.05 the river field has
            // no strength.
            QVERIFY2(window.desertRiver == 0, qPrintable(QString::number(window.desertRiver)));
        }
        // Fewer rivers in dry climates (humidity under 0.15) than in humid ones (over 0.4). Rivers
        // fade out from a humidity of 0.22 down to 0.05, so the share of one window depends on how
        // its dry land spreads over that range (1 to 21 % of the humid share on 9 seeds, but 99 %
        // for 1448947576), so the seeds count together.
        const double dry = double(climates.dryRiver) / double(std::max<qint64>(1, climates.dryLand));
        const double humid = double(climates.humidRiver) / double(std::max<qint64>(1, climates.humidLand));
        qInfo("all seeds: dry %.4f of %lld, humid %.4f of %lld", dry, climates.dryLand, humid, climates.humidLand);
        QVERIFY2(dry < 0.5 * humid, qPrintable(u"dry %1, humid %2"_s.arg(dry).arg(humid)));
    }
};

QTEST_GUILESS_MAIN(TestRivers)
#include "test_rivers.moc"
