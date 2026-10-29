// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcContentParser.h"
#include "QcTestUtil.h"
#include "server/blocks/QcLiquidFlow.h"
#include "world/QcTerrainTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QJsonObject>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// The liquids of the generated terrain (lava lakes: test_lava_lakes): lava and water pools held in
// place underground, liquids that take no flow step after generation, and the pool features of content.
namespace {

using QcTerrainTestUtil::flatContent;
using QcTerrainTestUtil::simpleBlock;

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

bool sameBlocks(const QcChunk &chunk, const QcChunk &other)
{
    for (int s = 0; s < QcWorld::sectionCount; ++s) {
        for (int y = 0; y < QcWorld::sectionHeight; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    if (chunk.section(s).block(x, y, z) != other.section(s).block(x, y, z)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Liquid cells of a generated region and the flow steps planned for those next to something else.
struct LiquidSurvey
{
    int lavaCells = 0;
    int poolWater = 0;
    int planned = 0;
};

bool touchesOtherBlock(const ChunkRegion &region, QcBlockPos pos, QcBlockId block)
{
    return std::ranges::any_of(qcAllBlockFaces, [&](QcBlockFace face) {
        const std::optional<QcBlockId> next = region.blockAt(qcNeighbour(pos, face));
        return next && *next != block;
    });
}

LiquidSurvey surveyLiquids(const QcContentRegistry &content, const ChunkRegion &region, QcLiquidFlow &flow)
{
    const QcBlockId lavaId = *content.blockId(u"qtcraft:lava"_s);
    const QcBlockId waterId = *content.blockId(u"qtcraft:water"_s);
    LiquidSurvey survey;
    for (const auto &chunk : region.chunks()) {
        const int baseX = chunk->pos().x * 16;
        const int baseZ = chunk->pos().z * 16;
        for (int y = 1; y < 80; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    const QcBlockId block = chunk->block(x, y, z);
                    if (!content.block(block).liquid) {
                        continue;
                    }
                    survey.lavaCells += block == lavaId;
                    survey.poolWater += block == waterId && y < 50;
                    const QcBlockPos pos{baseX + x, y, baseZ + z};
                    if (touchesOtherBlock(region, pos, block) && flow.schedule(region, pos, 0)) {
                        ++survey.planned;
                    }
                }
            }
        }
    }
    return survey;
}

} // namespace

class TestTerrainLiquids : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_base = QcTestUtil::loadContent();

    // Whether the lava at local (x, y, z) of `chunk` is held in place: never on the chunk's edge, so a
    // chunk's pools need no neighbour, and every side and the cell below is lava or a block that holds
    // it, so the generated lava has nowhere to flow.
    bool isContainedPoolCell(const QcChunk &chunk, int x, int y, int z) const
    {
        if (x == 0 || x == 15 || z == 0 || z == 15) {
            return false;
        }
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        for (const auto &[dx, dy, dz] : {std::array{1, 0, 0}, std::array{-1, 0, 0}, std::array{0, 0, 1},
                                         std::array{0, 0, -1}, std::array{0, -1, 0}}) {
            const QcBlockId next = chunk.block(x + dx, y + dy, z + dz);
            const QcBlockDefinition &definition = m_base->block(next);
            if (next != lava && (next == qcAirBlockId || !definition.solid || definition.liquid || definition.replaceable)) {
                return false;
            }
        }
        return true;
    }

    // Appends the lava pool cells of `chunk`; false when one of them is not contained.
    bool addLavaPoolCells(const QcChunk &chunk, QList<QcBlockPos> &cells) const
    {
        const QcBlockId lava = *m_base->blockId(u"qtcraft:lava"_s);
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    // Pools only: the lava lakes at the bottom of the caves (up to caveLiquidLevel)
                    // and the lakes on the surface are other features.
                    if (chunk.block(x, y, z) != lava || y <= m_base->worldgen().terrainSettings().caveLiquidLevel || y >= QcWorld::seaLevel) {
                        continue;
                    }
                    cells.append({chunk.pos().x * 16 + x, y, chunk.pos().z * 16 + z});
                    if (!isContainedPoolCell(chunk, x, y, z)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    // The lava pool cells of 12 x 12 chunks generated with `seed`; empty when a pool is not contained.
    QList<QcBlockPos> lavaPoolCells(qint64 seed) const
    {
        const QcTerrainGenerator generator(m_base, seed);
        QList<QcBlockPos> cells;
        for (int cz = 0; cz < 12; ++cz) {
            for (int cx = 0; cx < 12; ++cx) {
                if (!addLavaPoolCells(*generator.generate({cx, cz}), cells)) {
                    return {};
                }
            }
        }
        return cells;
    }

    // The base content with many lava pools and water pools of the same kind in every chunk.
    std::shared_ptr<QcContentRegistry> contentWithManyPools() const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_base);
        QcFeatureDefinition lava = *std::ranges::find(content->worldgen().features(), u"qtcraft:lava_pool"_s, &QcFeatureDefinition::name);
        lava.attempts = 8;
        lava.chance = 1.0;
        lava.maxY = 60;
        content->worldgen().registerFeature(lava);
        QcFeatureDefinition water = lava;
        water.name = u"t:water_pool"_s;
        water.trunkBlock = u"qtcraft:water"_s;
        water.radius = 5;
        water.depth = 3;
        content->worldgen().registerFeature(water);
        return content;
    }

private slots:
    void lavaPoolsAreDeterministicAndContained()
    {
        const QList<QcBlockPos> first = lavaPoolCells(12345);
        // A few pools in 144 chunks, deep underground and open to a cave.
        qInfo("lava cells in 12 x 12 chunks: %lld", static_cast<qint64>(first.size()));
        QVERIFY2(first.size() >= 20, qPrintable(QString::number(first.size())));
        QCOMPARE(lavaPoolCells(12345), first);

        const QList<QcBlockPos> other = lavaPoolCells(54321);
        QVERIFY(!other.isEmpty());
        QVERIFY(other != first);
        for (const QcBlockPos &cell : first) {
            QVERIFY(cell.y > m_base->worldgen().terrainSettings().caveLiquidLevel && cell.y < 44);
        }
    }

    // Every liquid cell next to something else takes a flow step with the server's rules: nothing
    // may change (no flow, no reaction). Many lava and water pools per chunk provoke pools meeting
    // each other, oceans and cave walls; the chunks generated in reverse order are identical.
    void generatedLiquidsStayStill()
    {
        const std::shared_ptr<QcContentRegistry> content = contentWithManyPools();
        const QcTerrainGenerator generator(content, 2468);
        const QStringList &warnings = generator.warnings();
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
            if (!sameBlocks(*generator.generate(pos), *region.chunks().value(pos))) {
                QFAIL(qPrintable(u"chunk %1 %2 differs"_s.arg(pos.x).arg(pos.z)));
            }
        }

        QcLiquidFlow flow(content);
        const LiquidSurvey survey = surveyLiquids(*content, region, flow);
        qInfo("lava cells %d, pool water cells %d, planned steps %d", survey.lavaCells, survey.poolWater, survey.planned);
        QVERIFY(survey.lavaCells > 500);
        QVERIFY(survey.poolWater > 500);

        QList<std::pair<QcBlockPos, QcBlockId>> changes;
        const QcLiquidFlow::TickResult result = flow.tick(
            1000, region, [&changes](QcBlockPos pos, QcBlockId block) { changes.append({pos, block}); }, survey.planned + 1);
        QCOMPARE(result.steps, survey.planned);
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
        content->worldgen().registerFeature(*pool);

        // A biome listing a pool is warned about; the pool still appears in caves.
        QcBiomeDefinition biome = content->worldgen().biomes().constFirst();
        biome.features.append({u"t:pond"_s, 0.5});
        content->worldgen().registerBiome(biome);
        const QcTerrainGenerator generator(content, 77);
        const QStringList &warnings = generator.warnings();
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
};

QTEST_GUILESS_MAIN(TestTerrainLiquids)
#include "test_terrain_liquids.moc"
