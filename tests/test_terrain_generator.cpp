#include "QcBaseContent.h"
#include "QcContentParser.h"
#include "QcLiquidFlow.h"
#include "QcMailbox.h"
#include "QcTerrainGenerator.h"
#include "QcWorkerPool.h"

#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <array>
#include <queue>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> baseContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

QcBlockDefinition simpleBlock(const QString &name)
{
    QcBlockDefinition block;
    block.name = name;
    block.faceTextures.fill(name);
    return block;
}

// Minimal content: stone, water, a red surface block and one flat biome; caves optional.
std::shared_ptr<QcContentRegistry> flatContent(bool caves)
{
    auto content = std::make_shared<QcContentRegistry>();
    for (const QString &name : {u"t:stone"_s, u"t:red"_s, u"t:ore"_s, u"t:bedrock"_s}) {
        if (!content->registerBlock(simpleBlock(name))) {
            qFatal("cannot register block");
        }
    }
    QcBlockDefinition water = simpleBlock(u"t:water"_s);
    water.solid = false;
    water.liquid = true;
    if (!content->registerBlock(water)) {
        qFatal("cannot register water");
    }
    QcTerrainSettings terrain;
    terrain.stoneBlock = u"t:stone"_s;
    terrain.waterBlock = u"t:water"_s;
    terrain.bedrockBlock = u"t:bedrock"_s;
    terrain.caves = caves;
    content->setTerrainSettings(terrain);
    QcBiomeDefinition biome;
    biome.name = u"t:flat"_s;
    biome.baseHeight = 80.0;
    biome.heightVariation = 0.0;
    biome.surfaceBlock = u"t:red"_s;
    biome.subsurfaceBlock = u"t:red"_s;
    biome.subsurfaceDepth = 2;
    content->registerBiome(biome);
    return content;
}

// Generated chunks of a region as a block world; outside the region counts as not loaded.
class ChunkRegion : public QcBlockAccess
{
public:
    explicit ChunkRegion(const QcContentRegistry &content)
        : m_content(content)
    {
    }
    void add(const std::shared_ptr<QcChunk> &chunk) { m_chunks.insert(chunk->pos(), chunk); }
    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        const auto it = m_chunks.constFind(pos.chunk());
        if (it == m_chunks.constEnd()) {
            return std::nullopt;
        }
        return pos.isInsideWorldHeight() ? (*it)->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    }
    const QcContentRegistry &content() const override { return m_content; }
    const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks() const { return m_chunks; }

private:
    const QcContentRegistry &m_content;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;
};

} // namespace

class TestTerrainGenerator : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_base = baseContent();

private slots:
    void baseContentResolvesWithoutWarnings()
    {
        QStringList warnings;
        const QcTerrainGenerator generator(m_base, 1, &warnings);
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
        QStringList warnings;
        const QcTerrainGenerator generator(content, 5, &warnings);
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
                QCOMPARE(chunk->block(x, generator.seaLevel(), 0), qcAirBlockId);
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
        content->registerOre(ore);
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
        constexpr int chunks = 6;
        constexpr int size = chunks * QcWorld::chunkSize;
        constexpr int maxY = 128;
        QList<std::shared_ptr<QcChunk>> generated;
        QList<int> heights(qsizetype(size) * size);
        for (int cz = 0; cz < chunks; ++cz) {
            for (int cx = 0; cx < chunks; ++cx) {
                generated.append(generator.generate({cx, cz}));
            }
        }
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                heights[z * size + x] = generator.column(x, z).height;
            }
        }
        const auto isCaveAir = [&](int x, int y, int z) {
            if (x < 0 || x >= size || z < 0 || z >= size || y <= 0 || y >= maxY) {
                return false;
            }
            const int surface = heights[z * size + x];
            const auto &chunk = generated[(z / 16) * chunks + x / 16];
            return y < surface && chunk->block(x % 16, y, z % 16) == qcAirBlockId;
        };

        // Largest 6-connected cave component, and whether any component reaches the surface.
        QSet<qint64> visited;
        const auto key = [](int x, int y, int z) { return (qint64(x) * 4096 + y) * 4096 + z; };
        int largest = 0;
        bool openToSurface = false;
        int walkable = 0;
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                for (int y = 1; y < maxY; ++y) {
                    if (!isCaveAir(x, y, z) || visited.contains(key(x, y, z))) {
                        continue;
                    }
                    int count = 0;
                    std::queue<std::array<int, 3>> queue;
                    queue.push({x, y, z});
                    visited.insert(key(x, y, z));
                    while (!queue.empty()) {
                        const auto [cx, cy, cz] = queue.front();
                        queue.pop();
                        ++count;
                        if (cy == heights[cz * size + cx] - 1) {
                            openToSurface = true;
                        }
                        // A player fits where two air blocks stand on solid ground.
                        if (isCaveAir(cx, cy + 1, cz) && !isCaveAir(cx, cy - 1, cz)) {
                            ++walkable;
                        }
                        for (const auto &[dx, dy, dz] : {std::array{1, 0, 0}, std::array{-1, 0, 0}, std::array{0, 1, 0},
                                                         std::array{0, -1, 0}, std::array{0, 0, 1}, std::array{0, 0, -1}}) {
                            const int nx = cx + dx;
                            const int ny = cy + dy;
                            const int nz = cz + dz;
                            if (isCaveAir(nx, ny, nz) && !visited.contains(key(nx, ny, nz))) {
                                visited.insert(key(nx, ny, nz));
                                queue.push({nx, ny, nz});
                            }
                        }
                    }
                    largest = std::max(largest, count);
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
        QHash<QcBlockId, QcBlockId> trunkOf;
        for (const char *wood : {"oak", "birch", "spruce"}) {
            trunkOf.insert(*m_base->blockId(u"qtcraft:%1_leaves"_s.arg(QLatin1StringView(wood))),
                           *m_base->blockId(u"qtcraft:%1_log"_s.arg(QLatin1StringView(wood))));
        }
        int checked = 0;
        for (int cx = -40; cx <= 40; cx += 20) {
            for (int cz = -40; cz <= 40; cz += 20) {
                QHash<QcChunkPos, std::shared_ptr<QcChunk>> area;
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        area.insert({cx + dx, cz + dz}, generator.generate({cx + dx, cz + dz}));
                    }
                }
                const auto blockAt = [&](int x, int y, int z) {
                    const QcBlockPos pos{x, y, z};
                    return area.value(pos.chunk())->block(pos.localX(), y, pos.localZ());
                };
                const std::shared_ptr<QcChunk> &centre = area[{cx, cz}];
                for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
                    for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
                        const int x = cx * QcWorld::chunkSize + lx;
                        const int z = cz * QcWorld::chunkSize + lz;
                        for (int y = 1; y < QcWorld::worldHeight - 1; ++y) {
                            const auto trunk = trunkOf.constFind(centre->block(lx, y, lz));
                            if (trunk == trunkOf.constEnd()) {
                                continue;
                            }
                            // Crowns reach at most three blocks from the trunk, which ends at most
                            // one block below the highest leaves.
                            bool found = false;
                            for (int dx = -3; dx <= 3 && !found; ++dx) {
                                for (int dz = -3; dz <= 3 && !found; ++dz) {
                                    found = blockAt(x + dx, y, z + dz) == *trunk || blockAt(x + dx, y - 1, z + dz) == *trunk;
                                }
                            }
                            QVERIFY2(found, qPrintable(u"leaf without trunk at %1 %2 %3"_s.arg(x).arg(y).arg(z)));
                            ++checked;
                        }
                    }
                }
            }
        }
        QVERIFY2(checked > 1000, qPrintable(QString::number(checked)));
    }

    void cavesOpenOnlyAtEntrancesAndHoldLavaDeepDown()
    {
        const QcTerrainGenerator generator(m_base, 42);
        const QcTerrainSettings &terrain = m_base->terrainSettings();
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        const QcBlockId bedrock = *m_base->blockId(u"qtcraft:bedrock"_s);
        int dry = 0;
        int open = 0;
        int deepCave = 0;
        int deepLava = 0;
        std::array<int, 6> bedrockPerLayer{};
        constexpr int half = 8;
        for (int cz = -half; cz < half; ++cz) {
            for (int cx = -half; cx < half; ++cx) {
                const auto chunk = generator.generate({cx, cz});
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        const QcTerrainGenerator::Column column = generator.column(cx * 16 + x, cz * 16 + z);
                        if (column.height - 1 > generator.seaLevel()) {
                            ++dry;
                            open += chunk->block(x, generator.groundTop(cx * 16 + x, cz * 16 + z, column), z) == qcAirBlockId;
                        }
                        for (int y = 0; y < 6; ++y) {
                            bedrockPerLayer[std::size_t(y)] += chunk->block(x, y, z) == bedrock;
                        }
                        // Below the liquid level, caves hold lava and never air.
                        for (int y = 1; y <= terrain.caveLiquidLevel; ++y) {
                            const QcBlockId block = chunk->block(x, y, z);
                            QVERIFY2(block != qcAirBlockId, qPrintable(u"air at %1 %2 %3"_s.arg(cx * 16 + x).arg(y).arg(cz * 16 + z)));
                            deepLava += block == lava;
                            deepCave += generator.isCave(cx * 16 + x, y, cz * 16 + z);
                        }
                    }
                }
            }
        }
        // A small share of the ground opens into caves (about caveEntrances of the land at most).
        const double share = double(open) / dry;
        qInfo("open ground %.4f, deep lava %d", share, deepLava);
        QVERIFY2(share > 0.0005 && share < terrain.caveEntrances / 4.0, qPrintable(QString::number(share)));
        QVERIFY(deepLava > 1000 && deepLava <= deepCave);
        // Bedrock: solid at the bottom, thinning out to nothing above bedrockLayers.
        const int columns = 4 * half * half * 256;
        QCOMPARE(bedrockPerLayer[0], columns);
        for (int y = 1; y < terrain.bedrockLayers; ++y) {
            const double expected = double(terrain.bedrockLayers - y) / terrain.bedrockLayers;
            QVERIFY(std::abs(double(bedrockPerLayer[std::size_t(y)]) / columns - expected) < 0.03);
        }
        for (int y = terrain.bedrockLayers; y < 6; ++y) {
            QCOMPARE(bedrockPerLayer[std::size_t(y)], 0);
        }
    }

    void lavaLakesLieOpenOnDryLandOfTheirBiomes()
    {
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        const auto lake = std::ranges::find(m_base->features(), u"qtcraft:lava_lake"_s, &QcFeatureDefinition::name);
        QVERIFY(lake != m_base->features().end() && lake->surface);
        int lakes = 0;
        for (const qint64 seed : {42, 777}) {
            const QcTerrainGenerator generator(m_base, seed);
            for (int cz = -8; cz < 8; ++cz) {
                for (int cx = -8; cx < 8; ++cx) {
                    const auto chunk = generator.generate({cx, cz});
                    bool found = false;
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            const QcTerrainGenerator::Column column = generator.column(cx * 16 + x, cz * 16 + z);
                            for (int y = generator.seaLevel(); y < QcWorld::worldHeight - 1; ++y) {
                                if (chunk->block(x, y, z) != lava) {
                                    continue;
                                }
                                found = true;
                                QVERIFY(x > 0 && x < 15 && z > 0 && z < 15);
                                QVERIFY(lake->biomes.contains(generator.biomeName(column.biome)));
                                // Nothing for it to flow into, and only liquid or open air above.
                                for (const auto &[dx, dy, dz] : {std::array{1, 0, 0}, std::array{-1, 0, 0}, std::array{0, 0, 1},
                                                                 std::array{0, 0, -1}, std::array{0, -1, 0}}) {
                                    const QcBlockId next = chunk->block(x + dx, y + dy, z + dz);
                                    QVERIFY(next == lava || (m_base->block(next).solid && !m_base->block(next).replaceable));
                                }
                                const QcBlockId above = chunk->block(x, y + 1, z);
                                QVERIFY(above == lava || above == qcAirBlockId || m_base->block(above).solid);
                            }
                        }
                    }
                    lakes += found;
                }
            }
        }
        qInfo("chunks with a lava lake: %d of %d", lakes, 2 * 256);
        QVERIFY(lakes >= 2 && lakes < 2 * 256 / 10);
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
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        const int liquidLevel = m_base->terrainSettings().caveLiquidLevel;
        const QcBlockId bedrock = *m_base->blockId(u"qtcraft:bedrock"_s);
        int checked = 0;
        for (const QcChunkPos pos : {QcChunkPos{0, 0}, QcChunkPos{-3, 5}, QcChunkPos{7, -2}}) {
            const auto chunk = generator.generate(pos);
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    const int wx = pos.x * 16 + x;
                    const int wz = pos.z * 16 + z;
                    const QcTerrainGenerator::Column column = generator.column(wx, wz);
                    const bool wet = column.height <= generator.seaLevel() + 1;
                    const int top = wet ? column.height - 6 : column.height - 1;
                    // Below the protected top, a stone/ore-free cell is carved exactly where isCave says
                    // so; carved cells deep down hold lava. Columns with a lava pool (sunk into a cave
                    // floor and hollowed above) and with overhangs are left out.
                    bool pool = false;
                    for (int y = liquidLevel + 1; y < 64; ++y) {
                        pool |= chunk->block(x, y, z) == lava;
                    }
                    if (pool) {
                        continue;
                    }
                    // Overhang hollows are not caves: stop below the band they reshape.
                    const int checkTop = column.overhang >= 1.0 ? std::min(top, column.height - int(std::ceil(column.overhang)) - 1) : top;
                    for (int y = 3; y < std::min(checkTop, 60); ++y) {
                        const QcBlockId block = chunk->block(x, y, z);
                        // Bedrock is never carved.
                        if (block == bedrock) {
                            continue;
                        }
                        const bool carved = y <= liquidLevel ? block == lava : block == qcAirBlockId;
                        QCOMPARE(carved, generator.isCave(wx, y, wz));
                        ++checked;
                    }
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

    void lavaPoolsAreDeterministicAndContained()
    {
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        const auto lavaCells = [&](qint64 seed) {
            const QcTerrainGenerator generator(m_base, seed);
            QList<QcBlockPos> cells;
            for (int cz = 0; cz < 12; ++cz) {
                for (int cx = 0; cx < 12; ++cx) {
                    const auto chunk = generator.generate({cx, cz});
                    for (int y = 0; y < QcWorld::worldHeight; ++y) {
                        for (int z = 0; z < 16; ++z) {
                            for (int x = 0; x < 16; ++x) {
                                // Pools only: the lava lakes at the bottom of the caves (up to caveLiquidLevel)
                                // and the lakes on the surface are other features.
                                if (chunk->block(x, y, z) != lava || y <= m_base->terrainSettings().caveLiquidLevel
                                    || y >= QcWorld::seaLevel) {
                                    continue;
                                }
                                cells.append({cx * 16 + x, y, cz * 16 + z});
                                // Never on the chunk's edge, so a chunk's pools need no neighbour.
                                if (x == 0 || x == 15 || z == 0 || z == 15) {
                                    return QList<QcBlockPos>{};
                                }
                                // Every side and the cell below is lava or a block that holds it: the
                                // generated lava has nowhere to flow.
                                for (const auto &[dx, dy, dz] : {std::array{1, 0, 0}, std::array{-1, 0, 0}, std::array{0, 0, 1},
                                                                 std::array{0, 0, -1}, std::array{0, -1, 0}}) {
                                    const QcBlockId next = chunk->block(x + dx, y + dy, z + dz);
                                    const QcBlockDefinition &definition = m_base->block(next);
                                    if (next != lava && (next == qcAirBlockId || !definition.solid || definition.liquid || definition.replaceable)) {
                                        return QList<QcBlockPos>{};
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return cells;
        };
        const QList<QcBlockPos> first = lavaCells(12345);
        // A few pools in 144 chunks, deep underground and open to a cave.
        qInfo("lava cells in 12 x 12 chunks: %lld", qint64(first.size()));
        QVERIFY2(first.size() >= 20, qPrintable(QString::number(first.size())));
        QCOMPARE(lavaCells(12345), first);
        const QList<QcBlockPos> other = lavaCells(54321);
        QVERIFY(!other.isEmpty());
        QVERIFY(other != first);
        for (const QcBlockPos &cell : first) {
            QVERIFY(cell.y > m_base->terrainSettings().caveLiquidLevel && cell.y < 44);
        }
    }

    // Every liquid cell next to something else takes a flow step with the server's rules: nothing
    // may change (no flow, no reaction). Many lava and water pools per chunk provoke pools meeting
    // each other, oceans and cave walls; the chunks generated in reverse order are identical.
    void generatedLiquidsStayStill()
    {
        auto content = std::make_shared<QcContentRegistry>(*m_base);
        QcFeatureDefinition lava = *std::ranges::find(content->features(), u"qtcraft:lava_pool"_s, &QcFeatureDefinition::name);
        lava.attempts = 8;
        lava.chance = 1.0;
        lava.maxY = 60;
        content->registerFeature(lava);
        QcFeatureDefinition water = lava;
        water.name = u"t:water_pool"_s;
        water.trunkBlock = u"qtcraft:water"_s;
        water.radius = 5;
        water.depth = 3;
        content->registerFeature(water);
        QStringList warnings;
        const QcTerrainGenerator generator(content, 2468, &warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
        constexpr int size = 10;
        QList<QcChunkPos> order;
        for (int cz = 0; cz < size; ++cz) {
            for (int cx = 0; cx < size; ++cx) {
                order.append({cx - 4, cz - 4});
            }
        }
        ChunkRegion region(*content);
        for (const QcChunkPos pos : std::as_const(order)) {
            region.add(generator.generate(pos));
        }
        std::ranges::reverse(order);
        for (const QcChunkPos pos : std::as_const(order)) {
            const auto again = generator.generate(pos);
            const auto &first = region.chunks().value(pos);
            for (int s = 0; s < QcWorld::sectionCount; ++s) {
                for (int y = 0; y < QcWorld::sectionHeight; ++y) {
                    for (int z = 0; z < 16; ++z) {
                        for (int x = 0; x < 16; ++x) {
                            if (again->section(s).block(x, y, z) != first->section(s).block(x, y, z)) {
                                QFAIL(qPrintable(u"chunk %1 %2 differs"_s.arg(pos.x).arg(pos.z)));
                            }
                        }
                    }
                }
            }
        }

        const QcBlockId lavaId = *content->blockId(u"qtcraft:lava"_s);
        const QcBlockId waterId = *content->blockId(u"qtcraft:water"_s);
        QcLiquidFlow flow(content);
        int lavaCells = 0;
        int poolWater = 0;
        int planned = 0;
        for (const auto &chunk : region.chunks()) {
            const int baseX = chunk->pos().x * 16;
            const int baseZ = chunk->pos().z * 16;
            for (int y = 1; y < 80; ++y) {
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        const QcBlockId block = chunk->block(x, y, z);
                        if (!content->block(block).liquid) {
                            continue;
                        }
                        lavaCells += block == lavaId;
                        poolWater += block == waterId && y < 50;
                        const QcBlockPos pos{baseX + x, y, baseZ + z};
                        const bool surface = std::ranges::any_of(qcAllBlockFaces, [&](QcBlockFace face) {
                            const std::optional<QcBlockId> next = region.blockAt(qcNeighbour(pos, face));
                            return next && *next != block;
                        });
                        if (surface && flow.schedule(region, pos, 0)) {
                            ++planned;
                        }
                    }
                }
            }
        }
        qInfo("lava cells %d, pool water cells %d, planned steps %d", lavaCells, poolWater, planned);
        QVERIFY(lavaCells > 500);
        QVERIFY(poolWater > 500);
        QList<std::pair<QcBlockPos, QcBlockId>> changes;
        const QcLiquidFlow::TickResult result = flow.tick(
            1000, region, [&changes](QcBlockPos pos, QcBlockId block) { changes.append({pos, block}); }, planned + 1);
        QCOMPARE(result.steps, planned);
        for (const auto &[pos, block] : std::as_const(changes)) {
            qWarning("change at %d %d %d to %s", pos.x, pos.y, pos.z, qPrintable(content->stateName(block)));
        }
        QCOMPARE(changes.size(), qsizetype(0));
    }

    void poolsAreParsedAndKeptOutOfBiomes()
    {
        auto content = flatContent(true);
        QcBlockDefinition lava = simpleBlock(u"t:lava"_s);
        lava.solid = false;
        lava.liquid = true;
        lava.replaceable = true;
        QVERIFY(content->registerBlock(lava));
        const auto pool = QcContentParser::parseFeature(
            QJsonObject{{u"name"_s, u"pond"_s}, {u"type"_s, u"pool"_s}, {u"block"_s, u"lava"_s}, {u"minY"_s, 10}, {u"maxY"_s, 60},
                        {u"attempts"_s, 8}, {u"chance"_s, 1.0}, {u"radius"_s, 4}, {u"depth"_s, 1}},
            u"t"_s);
        QVERIFY2(pool, qPrintable(pool.error()));
        QCOMPARE(pool->type, QcFeatureDefinition::Type::Pool);
        QCOMPARE(pool->trunkBlock, u"t:lava"_s);
        QCOMPARE(pool->attempts, 8);
        QCOMPARE(pool->radius, 4);
        QVERIFY(!QcContentParser::parseFeature(QJsonObject{{u"name"_s, u"p"_s}, {u"type"_s, u"pool"_s}}, u"t"_s));
        QVERIFY(!QcContentParser::parseFeature(
            QJsonObject{{u"name"_s, u"p"_s}, {u"type"_s, u"pool"_s}, {u"block"_s, u"lava"_s}, {u"attempts"_s, 9}}, u"t"_s));
        QVERIFY(!QcContentParser::parseFeature(
            QJsonObject{{u"name"_s, u"p"_s}, {u"type"_s, u"pool"_s}, {u"block"_s, u"lava"_s}, {u"depth"_s, 0}}, u"t"_s));
        content->registerFeature(*pool);
        // A biome listing a pool is warned about; the pool still appears in caves.
        QcBiomeDefinition biome = content->biomes().constFirst();
        biome.features.append({u"t:pond"_s, 0.5});
        content->registerBiome(biome);
        QStringList warnings;
        const QcTerrainGenerator generator(content, 77, &warnings);
        QCOMPARE(warnings.size(), qsizetype(1));
        QVERIFY(warnings.constFirst().contains(u"t:pond"_s));
        const QcBlockId lavaId = *content->blockId(u"t:lava"_s);
        int cells = 0;
        for (int cx = 0; cx < 8; ++cx) {
            const auto chunk = generator.generate({cx, 0});
            for (int y = 0; y < 80; ++y) {
                for (int z = 0; z < 16; ++z) {
                    for (int x = 0; x < 16; ++x) {
                        if (chunk->block(x, y, z) == lavaId) {
                            ++cells;
                            QVERIFY(y >= 9 && y < 60);
                        }
                    }
                }
            }
        }
        QVERIFY(cells > 0);
    }

    void spawnPointIsDryLand()
    {
        const QcTerrainGenerator generator(m_base, 12345);
        const QcBlockPos spawn = generator.spawnPoint();
        QVERIFY(spawn.y > generator.seaLevel());
        const auto chunk = generator.generate(spawn.chunk());
        const QcBlockId ground = chunk->block(spawn.localX(), spawn.y - 1, spawn.localZ());
        QVERIFY(m_base->block(ground).solid);
    }
};

QTEST_GUILESS_MAIN(TestTerrainGenerator)
#include "test_terrain_generator.moc"
