#include "common/content/parser/QcContentParser.h"
#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
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
    // Diagonal neighbours too: a narrow river in a gorge runs diagonally through the grid's cells.
    static constexpr std::array<std::pair<int, int>, 8> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
    Body body;
    QList<qsizetype> stack{start};
    seen[start] = true;
    while (!stack.isEmpty()) {
        const qsizetype cell = stack.takeLast();
        ++body.size;
        const int i = int(cell % window.cells);
        const int j = int(cell / window.cells);
        body.leaves = body.leaves || i == 0 || j == 0 || i == window.cells - 1 || j == window.cells - 1;
        for (const auto [di, dj] : sides) {
            const int ni = i + di;
            const int nj = j + dj;
            if (ni < 0 || nj < 0 || ni >= window.cells || nj >= window.cells) {
                continue;
            }
            const qsizetype next = qsizetype(nj) * window.cells + ni;
            body.joins = body.joins || window.grid[next] == Cell::OtherWater;
            if (window.grid[next] == Cell::River && !seen[next]) {
                seen[next] = true;
                stack.append(next);
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

// The generated chunks of a square of 3 × 3 chunks, looked up by world coordinates.
class ChunkArea
{
public:
    ChunkArea(const QcTerrainGenerator &generator, QcChunkPos centre)
        : m_centre(centre)
    {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                m_chunks.append(generator.generate({centre.x + dx, centre.z + dz}));
            }
        }
    }

    [[nodiscard]] QcBlockId block(int x, int y, int z) const
    {
        const QcBlockPos pos{x, y, z};
        const QcChunkPos chunk = pos.chunk();
        return m_chunks[(chunk.z - m_centre.z + 1) * 3 + (chunk.x - m_centre.x + 1)]->block(pos.localX(), y, pos.localZ());
    }

private:
    QcChunkPos m_centre;
    QList<std::shared_ptr<QcChunk>> m_chunks;
};

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

    // Every water block of the area's middle chunk beside or above air that no cave carved (a wall
    // or a sheet of water that would flow when the chunk loads), also across the chunk's borders.
    [[nodiscard]] static QStringList openWater(const QcTerrainGenerator &generator, const ChunkArea &area, QcChunkPos centre)
    {
        static constexpr std::array<std::array<int, 3>, 5> sides{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}}};
        QStringList open;
        for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
            for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
                const int x = centre.x * QcWorld::chunkSize + lx;
                const int z = centre.z * QcWorld::chunkSize + lz;
                for (int y = 1; y < generator.seaLevel(); ++y) {
                    if (area.block(x, y, z) != generator.waterBlock()) {
                        continue;
                    }
                    for (const auto &side : sides) {
                        const QcBlockPos next{x + side[0], y + side[1], z + side[2]};
                        if (area.block(next.x, next.y, next.z) == qcAirBlockId && !generator.isCave(next.x, next.y, next.z)) {
                            open.append(u"%1 %2 %3"_s.arg(x).arg(y).arg(z));
                        }
                    }
                }
            }
        }
        return open;
    }

    // A bridge of the plan over a river: a deck from which bridge cells run straight along x or z
    // (`along`) to dry land at both ends.
    struct Bridge
    {
        QcBlockPos at;
        std::pair<int, int> along;
    };
    static std::optional<Bridge> bridgeOf(const QcTerrainGenerator &generator, const QcVillagePlan &plan)
    {
        QSet<std::pair<int, int>> decks;
        for (const QcVillagePlan::Deck &deck : plan.bridges) {
            decks.insert({deck.at.x, deck.at.z});
        }
        const auto dryEnd = [&](int x, int z, std::pair<int, int> step) {
            while (decks.contains({x, z})) {
                x += step.first;
                z += step.second;
            }
            return generator.column(x, z).height >= generator.seaLevel();
        };
        for (const auto &[x, z] : decks) {
            if (!generator.isRiverBiome(generator.column(x, z).biome)) {
                continue;
            }
            for (const std::pair<int, int> along : {std::pair{1, 0}, std::pair{0, 1}}) {
                if (dryEnd(x, z, along) && dryEnd(x, z, {-along.first, -along.second})) {
                    return Bridge{{x, generator.seaLevel(), z}, along};
                }
            }
        }
        return std::nullopt;
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

    // Every deck of the plan's bridges lies over a river, and every post under one reaches down to a
    // solid bed (not ice).
    void checkPosts(const QcTerrainGenerator &generator, const QcVillagePlan &plan) const
    {
        for (const QcVillagePlan::Deck &deck : plan.bridges) {
            const QcTerrainColumn column = generator.column(deck.at.x, deck.at.z);
            QVERIFY2(generator.isRiverBiome(column.biome),
                     qPrintable(u"deck over %1 at %2 %3"_s.arg(generator.biomeName(column.biome)).arg(deck.at.x).arg(deck.at.z)));
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
                const QStringList open = openWater(generator, ChunkArea(generator, centre), centre);
                QVERIFY2(open.isEmpty(), qPrintable(u"water beside air at "_s + open.mid(0, 5).join(u", "_s)));
            }
        }
    }

    // Rivers in cold land are frozen: the frozen river biome with ice on top.
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
                const QcBlockId top = generator.generate(pos.chunk())->block(pos.localX(), pos.y, pos.localZ());
                QVERIFY2(top == id(u"qtcraft:ice"_s), qPrintable(u"%1 at %2 %3, seed %4"_s.arg(m_content->block(top).name).arg(x).arg(z).arg(seed)));
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
        for (const qint64 seed : {qint64(7), qint64(42), qint64(1234), m_randomSeed}) {
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
                    const std::optional<Bridge> bridge = bridgeOf(generator, *plan);
                    if (!bridge) {
                        continue;
                    }
                    qInfo("seed %lld: village at %d %d (%s) crosses a river at %d %d", seed, plan->centre.x, plan->centre.z,
                          qPrintable(plan->style), bridge->at.x, bridge->at.z);
                    ++bridges;
                    checkBanksJoined(*plan, *bridge);
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
        for (const qint64 seed : {qint64(7), qint64(42), qint64(1234), qint64(1034578912), m_randomSeed}) {
            const QcTerrainGenerator generator(m_content, seed);
            const Window window = survey(generator, 1536, 2);
            const Outlets outlets = outletsOf(window);
            const double share = double(window.river) / double(window.land);
            const double closed = double(outlets.closed) / double(outlets.joined + outlets.leaving + outlets.closed);
            const double dry = double(window.dryRiver) / double(std::max<qint64>(1, window.dryLand));
            const double humid = double(window.humidRiver) / double(std::max<qint64>(1, window.humidLand));
            qInfo("seed %lld: rivers %.4f of the land (dry %.4f, humid %.4f); river columns into other water %lld, out of the window "
                  "%lld, closed %lld (%.3f)",
                  seed, share, dry, humid, outlets.joined, outlets.leaving, outlets.closed, closed);
            QVERIFY2(share > 0.012 && share < 0.04, qPrintable(QString::number(share)));
            // Closed: pieces ending in deserts at both ends and what is left of the fading rings of the
            // noise's zero line round a small hill (0.5 %, 0.3 %, 2.8 %, 4.4 %; 0.5–6.7 % on five more
            // seeds; without the fading seed 1234 had 9.7 %, most of it one ring of 1400 cells). Seed
            // 1034578912 (random, decision E-846) showed 14 % while the flood joined only the four
            // sides: narrow rivers in gorges broke into pieces diagonally.
            QVERIFY2(closed < 0.08, qPrintable(QString::number(closed)));
            QVERIFY2(dry < 0.5 * humid, qPrintable(u"dry %1, humid %2"_s.arg(dry).arg(humid)));
        }
    }
};

QTEST_GUILESS_MAIN(TestRivers)
#include "test_rivers.moc"
