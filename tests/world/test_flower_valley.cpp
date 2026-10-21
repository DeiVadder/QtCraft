#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QSet>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Land samples on a coarse grid around the origin.
constexpr int gridStep = 48;
constexpr int gridReach = 6144;
constexpr int gridCells = 2 * gridReach / gridStep;

struct ValleySample
{
    int land = 0;
    int valley = 0;
    // Valley samples (water included) where the world's rarity is 0; must stay 0.
    int outsideSpots = 0;
    // Inland samples of the valley under the sea level (ponds).
    int ponds = 0;
    // Heights of the valley's and the plains' land samples, summed.
    qint64 valleyHeight = 0;
    int plains = 0;
    qint64 plainsHeight = 0;
    // The biome name of every sample, row-major (empty for water).
    QStringList biomes;
};

ValleySample sample(const QcTerrainGenerator &generator)
{
    ValleySample result;
    result.biomes.reserve(qsizetype(gridCells) * gridCells);
    for (int i = 0; i < gridCells; ++i) {
        for (int j = 0; j < gridCells; ++j) {
            const int x = -gridReach + j * gridStep;
            const int z = -gridReach + i * gridStep;
            const QcTerrainGenerator::Column column = generator.column(x, z);
            const bool inland = column.landFactor > 0.99;
            const bool land = inland && column.height > generator.seaLevel();
            const QString biome = land ? generator.biomeName(column.biome) : QString();
            const bool valleyColumn = generator.biomeName(column.biome) == u"qtcraft:flower_valley"_s;
            result.outsideSpots += valleyColumn && generator.rarityAt(x, z) <= 0.0 ? 1 : 0;
            result.ponds += inland && !land && generator.biomeName(column.biome) == u"qtcraft:flower_valley"_s ? 1 : 0;
            result.biomes.append(biome);
            result.land += land ? 1 : 0;
            if (biome == u"qtcraft:flower_valley"_s) {
                ++result.valley;
                result.valleyHeight += column.height;
            } else if (biome == u"qtcraft:plains"_s) {
                ++result.plains;
                result.plainsHeight += column.height;
            }
        }
    }
    return result;
}

// Chunks whose four corners and centre all lie in `biome`, searched on the sample grid.
QList<QcChunkPos> chunksIn(const QcTerrainGenerator &generator, const QString &biome, qsizetype wanted)
{
    QList<QcChunkPos> chunks;
    constexpr int size = QcWorld::chunkSize;
    for (int i = 0; i < gridCells && chunks.size() < wanted; ++i) {
        for (int j = 0; j < gridCells && chunks.size() < wanted; ++j) {
            const QcChunkPos pos{(-gridReach + j * gridStep) / size, (-gridReach + i * gridStep) / size};
            bool inside = true;
            for (const auto &[dx, dz] : {std::pair{0, 0}, {size - 1, 0}, {0, size - 1}, {size - 1, size - 1}, {size / 2, size / 2}}) {
                const QcTerrainGenerator::Column column = generator.column(pos.x * size + dx, pos.z * size + dz);
                inside = inside && column.height > generator.seaLevel() + 1 && generator.biomeName(column.biome) == biome;
            }
            if (inside) {
                chunks.append(pos);
            }
        }
    }
    return chunks;
}

} // namespace

// The flower valley (decision E-821): a rare biome of the temperate climate in spots of the
// world's rarity map, low and flat, full of flowers.
class TestFlowerValley : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // Flower blocks (all biomes' flower tables) on top of the ground of the chunks.
    int flowersIn(const QcTerrainGenerator &generator, const QList<QcChunkPos> &chunks) const
    {
        QSet<QcBlockId> flowers;
        for (const QcBiomeDefinition &biome : m_content->worldgen().biomes()) {
            for (const QcVegetationPlant &plant : biome.vegetation.flowers.plants) {
                flowers.insert(m_content->blockId(plant.block).value_or(qcAirBlockId));
            }
        }
        flowers.remove(qcAirBlockId);

        int count = 0;
        for (const QcChunkPos &pos : chunks) {
            const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int y = generator.seaLevel(); y < generator.seaLevel() + 40; ++y) {
                        count += flowers.contains(chunk->block(x, y, z)) ? 1 : 0;
                    }
                }
            }
        }
        return count;
    }

private slots:
    void isRareAndDeterministic()
    {
        ValleySample total;
        int seedsWithValleys = 0;
        for (const qint64 seed : {1, 7, 42, 1234}) {
            const QcTerrainGenerator generator(m_content, seed);
            QVERIFY2(generator.warnings().isEmpty(), qPrintable(generator.warnings().join(u'\n')));
            const ValleySample one = sample(generator);
            qInfo("seed %lld: flower valley %.4f of the land", seed, double(one.valley) / one.land);
            total.land += one.land;
            total.valley += one.valley;
            total.outsideSpots += one.outsideSpots;
            total.ponds += one.ponds;
            total.valleyHeight += one.valleyHeight;
            total.plains += one.plains;
            total.plainsHeight += one.plainsHeight;
            seedsWithValleys += one.valley > 0 ? 1 : 0;
        }

        const double share = double(total.valley) / total.land;
        const double valleyHeight = double(total.valleyHeight) / total.valley;
        const double plainsHeight = double(total.plainsHeight) / total.plains;
        qInfo("flower valley %.4f of the land (%d samples, %d in ponds), height %.1f against plains %.1f", share, total.valley,
              total.ponds, valleyHeight, plainsHeight);
        // Measured: 0.031 of the land (0.027-0.034 per seed), 4 % of the valley in ponds, 66.0 against
        // 68.8 blocks.
        QVERIFY2(share > 0.02 && share < 0.045, qPrintable(QString::number(share)));
        QCOMPARE(seedsWithValleys, 4);
        // Only in the spots of the rarity map.
        QCOMPARE(total.outsideSpots, 0);
        // A valley: lower than the plains, with ponds in its deepest hollows.
        QVERIFY2(valleyHeight < plainsHeight - 0.5, qPrintable(u"%1 %2"_s.arg(valleyHeight).arg(plainsHeight)));
        QVERIFY2(total.ponds > total.valley / 50 && total.ponds < total.valley / 8, qPrintable(QString::number(total.ponds)));

        // The same seed gives the same map.
        const ValleySample first = sample(QcTerrainGenerator(m_content, 42));
        const ValleySample again = sample(QcTerrainGenerator(m_content, 42));
        QVERIFY(first.biomes == again.biomes);
    }

    // Outside the spots the terrain is the same as without the flower valley: the same height,
    // coast and biome in every column, so adding a rare biome moves no land elsewhere.
    void leavesTheLandOutsideTheSpotsAlone()
    {
        auto without = std::make_shared<QcContentRegistry>(*m_content);
        without->worldgen().removeBiome(u"qtcraft:flower_valley"_s);
        QCOMPARE(without->worldgen().biomes().size(), m_content->worldgen().biomes().size() - 1);

        for (const qint64 seed : {7, 42}) {
            const QcTerrainGenerator generator(m_content, seed);
            const QcTerrainGenerator plain(without, seed);
            int outside = 0;
            int inside = 0;
            int different = 0;
            for (int i = 0; i < gridCells; ++i) {
                for (int j = 0; j < gridCells; ++j) {
                    const int x = -gridReach + j * gridStep;
                    const int z = -gridReach + i * gridStep;
                    if (generator.rarityAt(x, z) > 0.0) {
                        ++inside;
                        continue;
                    }
                    ++outside;
                    const QcTerrainGenerator::Column column = generator.column(x, z);
                    const QcTerrainGenerator::Column reference = plain.column(x, z);
                    const bool same = column.height == reference.height && column.landFactor == reference.landFactor
                                      && column.overhang == reference.overhang
                                      && generator.biomeName(column.biome) == plain.biomeName(reference.biome);
                    different += same ? 0 : 1;
                }
            }
            QVERIFY2(inside > 0, qPrintable(QString::number(seed)));
            QVERIFY2(outside > 4 * inside, qPrintable(u"%1 %2"_s.arg(outside).arg(inside)));
            QCOMPARE(different, 0);
        }
    }

    // Only rare biomes (possible with mods, decision E-815): a load warning, and outside the spots
    // the nearest biome alone shapes the land instead of a blend of no weight at all.
    void fallsBackToTheNearestBiomeWhenEveryBiomeIsRare()
    {
        auto onlyRare = std::make_shared<QcContentRegistry>(*m_content);
        for (const QcBiomeDefinition &biome : m_content->worldgen().biomes()) {
            if (biome.rarity <= 0.0) {
                onlyRare->worldgen().removeBiome(biome.name);
            }
        }
        QCOMPARE(onlyRare->worldgen().biomes().size(), 1);

        const QcTerrainGenerator generator(onlyRare, 42);
        QVERIFY2(generator.warnings().join(u'\n').contains(u"Every biome has a rarity"_s), qPrintable(generator.warnings().join(u'\n')));
        int outside = 0;
        for (int i = 0; i < gridCells; i += 4) {
            for (int j = 0; j < gridCells; j += 4) {
                const int x = -gridReach + j * gridStep;
                const int z = -gridReach + i * gridStep;
                if (generator.rarityAt(x, z) > 0.0) {
                    continue;
                }
                ++outside;
                const QcTerrainGenerator::Column column = generator.column(x, z);
                QCOMPARE(generator.biomeName(column.biome), u"qtcraft:flower_valley"_s);
                QVERIFY(std::isfinite(column.overhang));
                // Inland the valley's own low land (base height 62.5, variation 3), not the lowest
                // height a broken blend would clamp to.
                if (column.landFactor > 0.99) {
                    QVERIFY2(column.height > generator.seaLevel() - 8 && column.height < generator.seaLevel() + 16,
                             qPrintable(u"%1 at %2 %3"_s.arg(column.height).arg(x).arg(z)));
                }
            }
        }
        QVERIFY(outside > 0);
    }

    void hasFarMoreFlowersThanThePlains()
    {
        const QcTerrainGenerator generator(m_content, 42);
        const QList<QcChunkPos> valley = chunksIn(generator, u"qtcraft:flower_valley"_s, 6);
        const QList<QcChunkPos> plains = chunksIn(generator, u"qtcraft:plains"_s, 6);
        QCOMPARE(valley.size(), 6);
        QCOMPARE(plains.size(), 6);

        const int valleyFlowers = flowersIn(generator, valley);
        const int plainsFlowers = flowersIn(generator, plains);
        qInfo("flowers in 6 chunks: valley %d, plains %d", valleyFlowers, plainsFlowers);
        QVERIFY2(valleyFlowers > 4 * plainsFlowers, qPrintable(u"%1 %2"_s.arg(valleyFlowers).arg(plainsFlowers)));
        // More than a quarter of the valley floor is in bloom.
        QVERIFY2(valleyFlowers > 6 * 256 / 4, qPrintable(QString::number(valleyFlowers)));
    }

    void newFlowersGrowOnlyInTheValley()
    {
        const QStringList newFlowers{u"qtcraft:oxeye_daisy"_s, u"qtcraft:orange_tulip"_s, u"qtcraft:pink_tulip"_s};
        for (const QcBiomeDefinition &biome : m_content->worldgen().biomes()) {
            QStringList plants;
            for (const QcVegetationPlant &plant : biome.vegetation.flowers.plants) {
                plants.append(plant.block);
            }
            for (const QString &flower : newFlowers) {
                const bool listed = plants.contains(flower);
                const bool valley = biome.name == u"qtcraft:flower_valley"_s;
                QVERIFY2(listed == valley, qPrintable(biome.name + u' ' + flower));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestFlowerValley)
#include "test_flower_valley.moc"
