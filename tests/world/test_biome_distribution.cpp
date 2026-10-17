#include "common/content/loader/QcBaseContent.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

// Land columns on a 16-block grid within 1024 blocks of the origin.
struct BiomeSample
{
    int land = 0;
    int mountains = 0;
    // Pairs of neighbouring land samples and those of them in different biomes.
    int pairs = 0;
    int borders = 0;
};

constexpr int gridStep = 16;
constexpr int gridCells = 2 * 1024 / gridStep;

// Biome index per grid cell (row-major), -1 for water and coasts.
QList<int> biomeGrid(const QcTerrainGenerator &generator)
{
    QList<int> biomes(qsizetype(gridCells) * gridCells, -1);
    for (int i = 0; i < gridCells; ++i) {
        for (int j = 0; j < gridCells; ++j) {
            const QcTerrainGenerator::Column column = generator.column(-1024 + j * gridStep, -1024 + i * gridStep);
            biomes[qsizetype(i) * gridCells + j] = column.landFactor > 0.99 ? column.biome : -1;
        }
    }
    return biomes;
}

// Counts the land neighbours east and south of the cell (i, j) of `biome` and those in another biome.
void countBorders(const QList<int> &biomes, int i, int j, int biome, BiomeSample &result)
{
    const int east = j + 1 < gridCells ? biomes[qsizetype(i) * gridCells + j + 1] : -1;
    const int south = i + 1 < gridCells ? biomes[qsizetype(i + 1) * gridCells + j] : -1;
    for (const int other : {east, south}) {
        if (other >= 0) {
            ++result.pairs;
            result.borders += other != biome ? 1 : 0;
        }
    }
}

BiomeSample sample(const QcTerrainGenerator &generator)
{
    const QList<int> biomes = biomeGrid(generator);
    BiomeSample result;
    for (int i = 0; i < gridCells; ++i) {
        for (int j = 0; j < gridCells; ++j) {
            const int biome = biomes[qsizetype(i) * gridCells + j];
            if (biome < 0) {
                continue;
            }
            ++result.land;
            result.mountains += generator.biomeName(biome) == u"qtcraft:mountains"_s ? 1 : 0;
            countBorders(biomes, i, j, biome, result);
        }
    }
    return result;
}

} // namespace

// The climate map of the terrain (decision E-101): mountain ranges are common and biome borders are
// frayed. Pinned on several seeds; the values before E-101 are given for comparison.
class TestBiomeDistribution : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

private slots:
    void mountainsAreCommonAndBordersFrayed()
    {
        BiomeSample total;
        for (const qint64 seed : {1, 7, 42, 1234}) {
            const BiomeSample one = sample(QcTerrainGenerator(m_content, seed));
            total.land += one.land;
            total.mountains += one.mountains;
            total.pairs += one.pairs;
            total.borders += one.borders;
        }

        const double mountainShare = double(total.mountains) / total.land;
        const double borderShare = double(total.borders) / total.pairs;
        qInfo("mountains %.3f of the land, borders %.3f of neighbouring samples", mountainShare, borderShare);
        // Measured: mountains 0.263 (0.197 before E-101), borders 0.088 (0.061 before).
        QVERIFY2(mountainShare > 0.22 && mountainShare < 0.32, qPrintable(QString::number(mountainShare)));
        QVERIFY2(borderShare > 0.075, qPrintable(QString::number(borderShare)));
    }
};

QTEST_GUILESS_MAIN(TestBiomeDistribution)
#include "test_biome_distribution.moc"
