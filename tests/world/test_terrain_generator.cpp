#include "common/core/QcMailbox.h"
#include "common/core/QcWorkerPool.h"
#include "QcTestUtil.h"
#include "world/QcTerrainTestUtil.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <array>
#include <queue>

using namespace Qt::StringLiterals;

namespace {

using QcTerrainTestUtil::flatContent;

// A square of generated chunks from chunk (0, 0) with the surface height of every column.
class CaveArea
{
public:
    static constexpr int chunks = 6;
    static constexpr int size = chunks * QcWorld::chunkSize;
    static constexpr int maxY = 128;

    explicit CaveArea(const QcTerrainGenerator &generator)
        : m_heights(qsizetype(size) * size)
    {
        for (int cz = 0; cz < chunks; ++cz) {
            for (int cx = 0; cx < chunks; ++cx) {
                m_generated.append(generator.generate({cx, cz}));
            }
        }
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                m_heights[z * size + x] = generator.column(x, z).height;
            }
        }
    }

    int height(int x, int z) const { return m_heights[z * size + x]; }

    // Air below the surface, above y = 0 and below maxY.
    bool isCaveAir(int x, int y, int z) const
    {
        if (x < 0 || x >= size || z < 0 || z >= size || y <= 0 || y >= maxY) {
            return false;
        }
        const int surface = m_heights[z * size + x];
        const auto &chunk = m_generated[(z / 16) * chunks + x / 16];
        return y < surface && chunk->block(x % 16, y, z % 16) == qcAirBlockId;
    }

private:
    QList<std::shared_ptr<QcChunk>> m_generated;
    QList<int> m_heights;
};

qint64 caveKey(int x, int y, int z)
{
    return (qint64(x) * 4096 + y) * 4096 + z;
}

// One 6-connected cave component: its cells, the cells a player fits in, and whether it reaches the
// surface.
struct CaveComponent
{
    int count = 0;
    int walkable = 0;
    bool openToSurface = false;
};

// Walks the cave component of `start`, marking its cells in `visited`.
CaveComponent exploreCave(const CaveArea &area, std::array<int, 3> start, QSet<qint64> &visited)
{
    CaveComponent component;
    std::queue<std::array<int, 3>> queue;
    queue.push(start);
    visited.insert(caveKey(start[0], start[1], start[2]));

    while (!queue.empty()) {
        const auto [cx, cy, cz] = queue.front();
        queue.pop();
        ++component.count;
        if (cy == area.height(cx, cz) - 1) {
            component.openToSurface = true;
        }

        // A player fits where two air blocks stand on solid ground.
        if (area.isCaveAir(cx, cy + 1, cz) && !area.isCaveAir(cx, cy - 1, cz)) {
            ++component.walkable;
        }
        for (const auto &[dx, dy, dz] : {std::array{1, 0, 0}, std::array{-1, 0, 0}, std::array{0, 1, 0},
                                         std::array{0, -1, 0}, std::array{0, 0, 1}, std::array{0, 0, -1}}) {
            const int nx = cx + dx;
            const int ny = cy + dy;
            const int nz = cz + dz;
            if (area.isCaveAir(nx, ny, nz) && !visited.contains(caveKey(nx, ny, nz))) {
                visited.insert(caveKey(nx, ny, nz));
                queue.push({nx, ny, nz});
            }
        }
    }

    return component;
}

using ChunkArea = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;

// The generated chunk `centre` and its eight neighbours.
ChunkArea generateAround(const QcTerrainGenerator &generator, QcChunkPos centre)
{
    ChunkArea area;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            area.insert({centre.x + dx, centre.z + dz}, generator.generate({centre.x + dx, centre.z + dz}));
        }
    }
    return area;
}

// Crowns reach at most four blocks from the trunk or a branch (giant trees, decision E-822), which
// ends at most two blocks below the highest leaves.
bool trunkNear(const ChunkArea &area, QcBlockPos leaf, const QSet<QcBlockId> &trunk)
{
    const auto blockAt = [&](int x, int y, int z) {
        const QcBlockPos pos{x, y, z};
        return area.value(pos.chunk())->block(pos.localX(), y, pos.localZ());
    };
    bool found = false;
    for (int dx = -4; dx <= 4 && !found; ++dx) {
        for (int dz = -4; dz <= 4 && !found; ++dz) {
            for (int dy = -2; dy <= 0 && !found; ++dy) {
                found = trunk.contains(blockAt(leaf.x + dx, leaf.y + dy, leaf.z + dz));
            }
        }
    }
    return found;
}

// Checks every leaf of the centre chunk of `area` for its trunk and counts them into `checked`;
// returns the first leaf without one.
QString leafWithoutTrunk(const ChunkArea &area, QcChunkPos centre, const QHash<QcBlockId, QSet<QcBlockId>> &trunkOf, int &checked)
{
    const std::shared_ptr<QcChunk> chunk = area.value(centre);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int x = centre.x * QcWorld::chunkSize + lx;
            const int z = centre.z * QcWorld::chunkSize + lz;
            for (int y = 1; y < QcWorld::worldHeight - 1; ++y) {
                const auto trunk = trunkOf.constFind(chunk->block(lx, y, lz));
                if (trunk == trunkOf.constEnd()) {
                    continue;
                }
                if (!trunkNear(area, {x, y, z}, *trunk)) {
                    return u"leaf without trunk at %1 %2 %3"_s.arg(x).arg(y).arg(z);
                }
                ++checked;
            }
        }
    }
    return {};
}

// What cavesOpenOnlyAtEntrancesAndHoldLavaDeepDown counts: dry columns and those whose ground opens
// into a cave, cave cells below the liquid level and those holding lava, bedrock per layer.
struct CaveFloorCounts
{
    int dry = 0;
    int open = 0;
    int deepCave = 0;
    int deepLava = 0;
    std::array<int, 6> bedrockPerLayer{};
};

} // namespace

class TestTerrainGenerator : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_base = QcTestUtil::loadContent();

    // Adds the counts of the generated chunk `pos`; returns the first air cell below the cave liquid
    // level.
    QString countCaveFloor(const QcTerrainGenerator &generator, QcChunkPos pos, CaveFloorCounts &counts) const
    {
        const QcTerrainSettings &terrain = m_base->worldgen().terrainSettings();
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        const QcBlockId bedrock = *m_base->blockId(u"qtcraft:bedrock"_s);
        const auto chunk = generator.generate(pos);

        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const int wx = pos.x * 16 + x;
                const int wz = pos.z * 16 + z;
                const QcTerrainGenerator::Column column = generator.column(wx, wz);
                if (column.height - 1 > generator.seaLevel()) {
                    ++counts.dry;
                    counts.open += chunk->block(x, generator.groundTop(wx, wz, column), z) == qcAirBlockId;
                }

                for (int y = 0; y < 6; ++y) {
                    counts.bedrockPerLayer[std::size_t(y)] += chunk->block(x, y, z) == bedrock;
                }

                // Below the liquid level, caves hold lava and never air.
                for (int y = 1; y <= terrain.caveLiquidLevel; ++y) {
                    const QcBlockId block = chunk->block(x, y, z);
                    if (block == qcAirBlockId) {
                        return u"air at %1 %2 %3"_s.arg(wx).arg(y).arg(wz);
                    }
                    counts.deepLava += block == lava;
                    counts.deepCave += generator.isCave(wx, y, wz);
                }
            }
        }

        return {};
    }

    // Compares isCave with the carved cells of column (x, z) of `chunk`, counting the compared cells
    // into `checked`; returns the first mismatch.
    QString caveMismatchInColumn(const QcTerrainGenerator &generator, const QcChunk &chunk, int x, int z, int &checked) const
    {
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        const int liquidLevel = m_base->worldgen().terrainSettings().caveLiquidLevel;
        const QcBlockId bedrock = *m_base->blockId(u"qtcraft:bedrock"_s);
        // Cobwebs grow into carved air like ores into stone.
        const QcBlockId cobweb = *m_base->blockId(u"qtcraft:cobweb"_s);
        const int wx = chunk.pos().x * 16 + x;
        const int wz = chunk.pos().z * 16 + z;
        const QcTerrainGenerator::Column column = generator.column(wx, wz);
        const bool wet = column.height <= generator.seaLevel() + 1;
        const int top = wet ? column.height - 6 : column.height - 1;
        // Below the protected top, a stone/ore-free cell is carved exactly where isCave says
        // so; carved cells deep down hold lava. Columns with a lava pool (sunk into a cave
        // floor and hollowed above) and with overhangs are left out.
        bool pool = false;
        for (int y = liquidLevel + 1; y < 64; ++y) {
            pool |= chunk.block(x, y, z) == lava;
        }
        if (pool) {
            return {};
        }

        // Overhang hollows are not caves: stop below the band they reshape.
        const int checkTop = column.overhang >= 1.0 ? std::min(top, column.height - int(std::ceil(column.overhang)) - 1) : top;
        for (int y = 3; y < std::min(checkTop, 60); ++y) {
            const QcBlockId block = chunk.block(x, y, z);
            // Bedrock is never carved.
            if (block == bedrock) {
                continue;
            }
            const bool carved = y <= liquidLevel ? block == lava : block == qcAirBlockId || block == cobweb;
            if (carved != generator.isCave(wx, y, wz)) {
                return u"carved %1, isCave %2 at %3 %4 %5"_s.arg(int(carved)).arg(int(!carved)).arg(wx).arg(y).arg(wz);
            }
            ++checked;
        }
        return {};
    }

private slots:
    void baseContentResolvesWithoutWarnings()
    {
        const QcTerrainGenerator generator(m_base, 1);
        const QStringList &warnings = generator.warnings();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
    }

    void sameSeedGivesSameChunk()
    {
        const QcTerrainGenerator a(m_base, 777);
        const QcTerrainGenerator b(m_base, 777);
        const QcTerrainGenerator other(m_base, 778);
        const auto first = a.generate({3, -2});
        const auto second = b.generate({3, -2});
        const auto different = other.generate({3, -2});
        int differences = 0;
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    QCOMPARE(first->block(x, y, z), second->block(x, y, z));
                    differences += first->block(x, y, z) != different->block(x, y, z);
                }
            }
        }
        QVERIFY(differences > 100);
    }

    void surfaceAndLayersComeFromBiomeData()
    {
        const auto content = flatContent(false);
        const QcTerrainGenerator generator(content, 5);
        const QStringList &warnings = generator.warnings();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        const QcBlockId red = *content->blockId(u"t:red"_s);
        const QcBlockId stone = *content->blockId(u"t:stone"_s);
        const QcBlockId bedrock = *content->blockId(u"t:bedrock"_s);

        int landColumns = 0;
        for (int cx = -4; cx <= 4 && landColumns == 0; ++cx) {
            const auto chunk = generator.generate({cx * 7, 0});
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const QcTerrainGenerator::Column column = generator.column(cx * 7 * 16 + x, z);
                    QCOMPARE(chunk->block(x, 0, z), bedrock);
                    if (column.landFactor < 1.0) {
                        continue;
                    }
                    // Flat biome without variation: surface at 79, two subsurface layers, then stone.
                    ++landColumns;
                    QCOMPARE(column.height, 80);
                    QCOMPARE(chunk->block(x, 79, z), red);
                    QCOMPARE(chunk->block(x, 77, z), red);
                    QCOMPARE(chunk->block(x, 76, z), stone);
                    QCOMPARE(chunk->block(x, 80, z), qcAirBlockId);
                }
            }
        }

        QVERIFY(landColumns > 0);
    }

    void oceansAreFilledUpToSeaLevel()
    {
        const QcTerrainGenerator generator(m_base, 12345);
        const QcBlockId water = *m_base->blockId(u"qtcraft:water"_s);
        const QcBlockId ice = *m_base->blockId(u"qtcraft:ice"_s);
        const QcBlockId lilyPad = *m_base->blockId(u"qtcraft:lily_pad"_s);
        int checked = 0;
        for (int cx = -40; cx <= 40 && checked < 50; cx += 4) {
            const auto chunk = generator.generate({cx, 0});
            for (int x = 0; x < 16; ++x) {
                const QcTerrainGenerator::Column column = generator.column(cx * 16 + x, 0);
                if (column.height >= generator.seaLevel()) {
                    continue;
                }
                for (int y = column.height; y < generator.seaLevel(); ++y) {
                    const QcBlockId block = chunk->block(x, y, 0);
                    QVERIFY(block == water || (block == ice && y == generator.seaLevel() - 1));
                }
                // Above the water is air, or a lily pad on swamp water (decision E-774).
                const QcBlockId above = chunk->block(x, generator.seaLevel(), 0);
                QVERIFY(above == qcAirBlockId || (above == lilyPad && chunk->block(x, generator.seaLevel() - 1, 0) == water));
                ++checked;
            }
        }
        QVERIFY(checked >= 50);
    }

    void oresStayInTheirHeightBandAndReplaceStoneOnly()
    {
        auto content = flatContent(false);
        QcOreDefinition ore;
        ore.name = u"t:ore"_s;
        ore.block = u"t:ore"_s;
        ore.replaces = u"t:stone"_s;
        ore.minY = 10;
        ore.maxY = 20;
        ore.veinsPerChunk = 20;
        ore.veinSize = 6;
        content->worldgen().registerOre(ore);

        const QcTerrainGenerator generator(content, 9);
        const QcBlockId oreId = *content->blockId(u"t:ore"_s);
        int found = 0;
        const auto chunk = generator.generate({0, 0});
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    if (chunk->block(x, y, z) == oreId) {
                        ++found;
                        QVERIFY2(y >= 10 && y <= 20, qPrintable(QString::number(y)));
                    }
                }
            }
        }
        QVERIFY(found > 20);
    }

    void cavesAreLargeConnectedAndOpenToTheSurface()
    {
        const QcTerrainGenerator generator(m_base, 12345);
        const CaveArea area(generator);

        // Largest 6-connected cave component, and whether any component reaches the surface.
        QSet<qint64> visited;
        int largest = 0;
        bool openToSurface = false;
        int walkable = 0;
        for (int z = 0; z < CaveArea::size; ++z) {
            for (int x = 0; x < CaveArea::size; ++x) {
                for (int y = 1; y < CaveArea::maxY; ++y) {
                    if (!area.isCaveAir(x, y, z) || visited.contains(caveKey(x, y, z))) {
                        continue;
                    }
                    const CaveComponent component = exploreCave(area, {x, y, z}, visited);
                    largest = std::max(largest, component.count);
                    walkable += component.walkable;
                    openToSurface = openToSurface || component.openToSurface;
                }
            }
        }

        qInfo("cave air %lld, largest component %d, walkable %d", qint64(visited.size()), largest, walkable);
        QVERIFY2(largest > 2000, qPrintable(QString::number(largest)));
        QVERIFY(walkable > 500);
        QVERIFY(openToSurface);
    }

    void treeCrownsContinueAcrossChunkBorders()
    {
        const QcTerrainGenerator generator(m_base, 12345);
        const QSet<QcBlockId> logs = {*m_base->blockId(u"qtcraft:oak_log"_s), *m_base->blockId(u"qtcraft:birch_log"_s)};
        const QSet<QcBlockId> leaves = {*m_base->blockId(u"qtcraft:oak_leaves"_s), *m_base->blockId(u"qtcraft:birch_leaves"_s)};
        int checkedTrees = 0;

        // Several strips of chunks, so that the test does not depend on one biome.
        for (int cx = 0; cx < 64 && checkedTrees < 3; cx += 8) {
            for (int cz = 0; cz < 40 && checkedTrees < 3; ++cz) {
                const auto chunk = generator.generate({cx, cz});
                for (int z = 2; z < 14; ++z) {
                    const QcTerrainGenerator::Column column = generator.column(cx * 16 + 15, cz * 16 + z);
                    const QcBlockId base = chunk->block(15, column.height, z);
                    if (!logs.contains(base)) {
                        continue;
                    }

                    // A round crown spans two blocks around the trunk, so it must reach into the next chunk.
                    const auto east = generator.generate({cx + 1, cz});
                    bool leavesAcrossBorder = false;
                    for (int y = column.height; y < column.height + 10; ++y) {
                        leavesAcrossBorder |= leaves.contains(east->block(0, y, z));
                    }
                    QVERIFY2(leavesAcrossBorder, qPrintable(u"tree at %1 %2"_s.arg(cx * 16 + 15).arg(cz * 16 + z)));
                    ++checkedTrees;
                }
            }
        }

        QVERIFY(checkedTrees > 0);
    }

    void everyLeafHasItsTrunkAcrossChunkBorders()
    {
        // Both chunks at a border decide each tree on their own; a leaf without a trunk next to it
        // means one side placed the tree and the other did not.
        const QcTerrainGenerator generator(m_base, 12345);
        // Every axis of the log: branches of giant trees lie.
        QHash<QcBlockId, QSet<QcBlockId>> trunkOf;
        for (const char *wood : {"oak", "birch", "spruce", "jungle"}) {
            const QcBlockId log = *m_base->blockId(u"qtcraft:%1_log"_s.arg(QLatin1StringView(wood)));
            trunkOf.insert(*m_base->blockId(u"qtcraft:%1_leaves"_s.arg(QLatin1StringView(wood))),
                           {log, m_base->withProperty(log, u"axis"_s, u"x"_s), m_base->withProperty(log, u"axis"_s, u"z"_s)});
        }

        int checked = 0;
        for (int cx = -40; cx <= 40; cx += 20) {
            for (int cz = -40; cz <= 40; cz += 20) {
                const ChunkArea area = generateAround(generator, {cx, cz});
                const QString problem = leafWithoutTrunk(area, {cx, cz}, trunkOf, checked);
                QVERIFY2(problem.isEmpty(), qPrintable(problem));
            }
        }
        QVERIFY2(checked > 1000, qPrintable(QString::number(checked)));
    }

    void leavesBesideStructuresHaveTheirTrunk()
    {
        // Trees keep a margin from surface structures; every chunk a tree reaches must see the same
        // structures, or one side places the tree and the other does not.
        const QcTerrainGenerator generator(m_base, 12345);
        std::optional<QcSurfacePlanner::Rect> footprint;
        for (int cz = -64; cz <= 64 && !footprint; cz += 2) {
            for (int cx = -64; cx <= 64 && !footprint; cx += 2) {
                const QList<QcSurfacePlanner::Rect> near = generator.surfaceStructures().footprintsNear({cx, cz}, 0);
                if (!near.isEmpty()) {
                    footprint = near.first();
                }
            }
        }
        QVERIFY(footprint.has_value());

        QHash<QcBlockId, QSet<QcBlockId>> trunkOf;
        for (const char *wood : {"oak", "birch", "spruce", "jungle"}) {
            const QcBlockId log = *m_base->blockId(u"qtcraft:%1_log"_s.arg(QLatin1StringView(wood)));
            trunkOf.insert(*m_base->blockId(u"qtcraft:%1_leaves"_s.arg(QLatin1StringView(wood))),
                           {log, m_base->withProperty(log, u"axis"_s, u"x"_s), m_base->withProperty(log, u"axis"_s, u"z"_s)});
        }
        // The chunks of the footprint and one ring around it.
        const QcChunkPos first = QcBlockPos{footprint->x0, 0, footprint->z0}.chunk();
        const QcChunkPos last = QcBlockPos{footprint->x1, 0, footprint->z1}.chunk();
        int checked = 0;
        for (int cz = first.z - 1; cz <= last.z + 1; ++cz) {
            for (int cx = first.x - 1; cx <= last.x + 1; ++cx) {
                const ChunkArea area = generateAround(generator, {cx, cz});
                const QString problem = leafWithoutTrunk(area, {cx, cz}, trunkOf, checked);
                QVERIFY2(problem.isEmpty(), qPrintable(problem));
            }
        }
        qInfo() << "leaves checked" << checked;
    }

    void cavesOpenOnlyAtEntrancesAndHoldLavaDeepDown()
    {
        const QcTerrainGenerator generator(m_base, 42);
        const QcTerrainSettings &terrain = m_base->worldgen().terrainSettings();
        CaveFloorCounts counts;
        constexpr int half = 8;
        for (int cz = -half; cz < half; ++cz) {
            for (int cx = -half; cx < half; ++cx) {
                const QString problem = countCaveFloor(generator, {cx, cz}, counts);
                QVERIFY2(problem.isEmpty(), qPrintable(problem));
            }
        }

        // A small share of the ground opens into caves (about caveEntrances of the land at most).
        const double share = double(counts.open) / counts.dry;
        qInfo("open ground %.4f, deep lava %d", share, counts.deepLava);
        QVERIFY2(share > 0.0005 && share < terrain.caveEntrances / 4.0, qPrintable(QString::number(share)));
        QVERIFY(counts.deepLava > 1000 && counts.deepLava <= counts.deepCave);

        // Bedrock: solid at the bottom, thinning out to nothing above bedrockLayers.
        const int columns = 4 * half * half * 256;
        QCOMPARE(counts.bedrockPerLayer[0], columns);
        for (int y = 1; y < terrain.bedrockLayers; ++y) {
            const double expected = double(terrain.bedrockLayers - y) / terrain.bedrockLayers;
            QVERIFY(std::abs(double(counts.bedrockPerLayer[std::size_t(y)]) / columns - expected) < 0.03);
        }
        for (int y = terrain.bedrockLayers; y < 6; ++y) {
            QCOMPARE(counts.bedrockPerLayer[std::size_t(y)], 0);
        }
    }

    void overhangsShapeMountainsAboveTheSea()
    {
        const QcTerrainGenerator generator(m_base, 42);
        int overhanging = 0;
        int columns = 0;

        for (int cz = -6; cz < 6; ++cz) {
            for (int cx = -6; cx < 6; ++cx) {
                const auto chunk = generator.generate({cx, cz});
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        const int wx = cx * 16 + x;
                        const int wz = cz * 16 + z;
                        const QcTerrainGenerator::Column column = generator.column(wx, wz);
                        if (column.overhang < 1.0) {
                            continue;
                        }
                        ++columns;

                        // The band never reaches down to the sea, so no hollow meets water.
                        QVERIFY(column.height - column.overhang >= generator.seaLevel() + 3 - 1e-9);
                        // Air below solid ground within the column (not a cave): an overhang.
                        const int top = generator.groundTop(wx, wz, column);
                        QVERIFY(chunk->block(x, top, z) != qcAirBlockId || generator.isCave(wx, top, wz));
                        bool airBelow = false;
                        for (int y = top - 1; y > top - 2 * int(column.overhang) && y > 0; --y) {
                            airBelow = airBelow || (chunk->block(x, y, z) == qcAirBlockId && !generator.isCave(wx, y, wz));
                        }
                        overhanging += airBelow;
                    }
                }
            }
        }

        qInfo("overhang columns %d, with air under ground %d", columns, overhanging);
        QVERIFY(columns > 1000);
        QVERIFY(overhanging > columns / 100);
    }

    void isCaveAgreesWithTheCarvedChunk()
    {
        const QcTerrainGenerator generator(m_base, 2024);
        int checked = 0;
        for (const QcChunkPos pos : {QcChunkPos{0, 0}, QcChunkPos{-3, 5}, QcChunkPos{7, -2}}) {
            const auto chunk = generator.generate(pos);
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const QString mismatch = caveMismatchInColumn(generator, *chunk, x, z, checked);
                    QVERIFY2(mismatch.isEmpty(), qPrintable(mismatch));
                }
            }
        }
        QVERIFY(checked > 10000);
    }

    void concurrentGenerationIsDeterministic()
    {
        const QcTerrainGenerator generator(m_base, 31337);
        QList<QcChunkPos> positions;
        for (int i = 0; i < 16; ++i) {
            positions.append({i % 4 - 2, i / 4 - 2});
        }
        QList<std::shared_ptr<QcChunk>> serial;
        for (const QcChunkPos pos : std::as_const(positions)) {
            serial.append(generator.generate(pos));
        }

        QcMailbox<std::shared_ptr<QcChunk>> results;
        {
            QcWorkerPool pool(4);
            for (const QcChunkPos pos : std::as_const(positions)) {
                pool.submit([&generator, &results, pos] { results.push(generator.generate(pos)); });
            }
            pool.waitForIdle();
        }

        const std::vector<std::shared_ptr<QcChunk>> parallel = results.takeAll();
        QCOMPARE(qsizetype(parallel.size()), positions.size());

        for (const std::shared_ptr<QcChunk> &chunk : parallel) {
            const auto reference = serial[positions.indexOf(chunk->pos())];
            for (int s = 0; s < QcWorld::sectionCount; ++s) {
                for (int y = 0; y < 16; ++y) {
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            QCOMPARE(chunk->section(s).block(x, y, z), reference->section(s).block(x, y, z));
                        }
                    }
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestTerrainGenerator)
#include "test_terrain_generator.moc"
