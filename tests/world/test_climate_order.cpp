#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QHash>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Land samples on a grid around the origin.
constexpr int gridStep = 8;
constexpr int gridReach = 2048;
constexpr int gridCells = 2 * gridReach / gridStep;

// The biome of every land sample of one seed (row-major, -1 for water) and how often two biomes lie
// within a distance of each other.
class BiomeMap
{
public:
    explicit BiomeMap(const QcTerrainGenerator &generator)
        : m_generator(generator)
        , m_biomes(static_cast<qsizetype>(gridCells) * gridCells, -1)
    {
        for (int i = 0; i < gridCells; ++i) {
            for (int j = 0; j < gridCells; ++j) {
                const QcTerrainGenerator::Column column = m_generator.column(-gridReach + j * gridStep, -gridReach + i * gridStep);
                const bool land = column.landFactor > 0.99 && column.height > m_generator.seaLevel();
                m_biomes[index(i, j)] = land ? column.biome : -1;
            }
        }
    }

    [[nodiscard]] QString name(int biome) const { return m_generator.biomeName(biome).mid(qsizetype{8}); }

    // Land samples per biome name.
    [[nodiscard]] QHash<QString, int> shares() const
    {
        QHash<QString, int> counts;
        for (const int biome : m_biomes) {
            if (biome >= 0) {
                ++counts[name(biome)];
            }
        }
        return counts;
    }

    // Samples of `from` with a sample of another biome within `reach` blocks, per pair "a|b" (names
    // sorted), each sample counted once per other biome.
    [[nodiscard]] QHash<QString, int> contacts(int reach) const
    {
        const int cells = reach / gridStep;
        QHash<QString, int> result;
        for (int i = 0; i < gridCells; ++i) {
            for (int j = 0; j < gridCells; ++j) {
                const int biome = m_biomes[index(i, j)];
                if (biome < 0) {
                    continue;
                }
                const QList<int> others = neighbours(i, j, cells);
                for (const int other : others) {
                    ++result[pairName(biome, other)];
                }
            }
        }
        return result;
    }

    [[nodiscard]] QString pairName(int a, int b) const
    {
        const QString first = name(a);
        const QString second = name(b);
        return first < second ? first + u'|' + second : second + u'|' + first;
    }

    [[nodiscard]] const QList<int> &biomes() const { return m_biomes; }

private:
    static qsizetype index(int i, int j) { return static_cast<qsizetype>(i) * gridCells + j; }

    // The other biomes within `cells` grid cells of (i, j), each once.
    [[nodiscard]] QList<int> neighbours(int i, int j, int cells) const
    {
        QList<int> found;
        const int own = m_biomes[index(i, j)];
        for (int di = -cells; di <= cells; ++di) {
            for (int dj = -cells; dj <= cells; ++dj) {
                const int ni = i + di;
                const int nj = j + dj;
                if (ni < 0 || nj < 0 || ni >= gridCells || nj >= gridCells || di * di + dj * dj > cells * cells) {
                    continue;
                }
                const int other = m_biomes[index(ni, nj)];
                if (other >= 0 && other != own && !found.contains(other)) {
                    found.append(other);
                }
            }
        }
        return found;
    }

    const QcTerrainGenerator &m_generator;
    QList<int> m_biomes;
};

template<typename Map>
void add(Map &total, const Map &one)
{
    for (auto it = one.cbegin(); it != one.cend(); ++it) {
        total[it.key()] += it.value();
    }
}

// Temperature and humidity change per block between neighbouring columns 4 blocks apart, sorted.
struct Gradients
{
    QList<double> temperature;
    QList<double> humidity;
};

Gradients gradients(const QcTerrainGenerator &generator)
{
    constexpr int step = 4;
    Gradients result;
    for (int z = -gridReach; z < gridReach; z += 64) {
        for (int x = -gridReach; x < gridReach; x += 64) {
            const QcTerrainClimate::Point here = generator.climateAt(x, z);
            for (const QcTerrainClimate::Point &next : {generator.climateAt(x + step, z), generator.climateAt(x, z + step)}) {
                result.temperature.append(std::abs(next.temperature - here.temperature) / step);
                result.humidity.append(std::abs(next.humidity - here.humidity) / step);
            }
        }
    }
    std::ranges::sort(result.temperature);
    std::ranges::sort(result.humidity);
    return result;
}

double quantile(const QList<double> &sorted, double q)
{
    return sorted[std::min(sorted.size() - 1, static_cast<qsizetype>(q * static_cast<double>(sorted.size())))];
}

// Biomes of the cold and the hot end of the temperature axis; they never meet.
const QStringList coldBiomes{u"tundra"_s, u"snowy_taiga"_s, u"taiga"_s};
const QStringList hotBiomes{u"desert"_s, u"forest_steppe"_s, u"jungle_edge"_s, u"jungle"_s, u"swamp"_s};

constexpr std::array seeds{qint64{1}, qint64{7}, qint64{42}, qint64{1234}};

// Where a climate point lies in the climate order: cold, hot or neither.
enum class Band : quint8 { Cold, Temperate, Hot };

// The bands of the biome cells on a grid over temperature and humidity at one ruggedness and
// rarity, as the terrain picks biomes (decision E-824).
class ClimateCells
{
public:
    static constexpr int steps = 100;

    ClimateCells(const QcTerrainGenerator &generator, double ruggedness, double rarity)
        : m_bands(static_cast<qsizetype>(steps + 1) * (steps + 1), Band::Temperate)
    {
        for (int i = 0; i <= steps; ++i) {
            for (int j = 0; j <= steps; ++j) {
                const QcTerrainClimate::Point point{static_cast<double>(i) / steps, static_cast<double>(j) / steps, ruggedness, rarity};
                const QString name = generator.biomeName(generator.biomeForClimate(point)).mid(qsizetype{8});
                const bool cold = coldBiomes.contains(name);
                m_bands[index(i, j)] = cold ? Band::Cold : hotBiomes.contains(name) ? Band::Hot : Band::Temperate;
            }
        }
    }

    // The smallest distance in climate space between a cold and a hot grid point; only points at
    // the edge of their band are compared (the nearest pair always lies there).
    [[nodiscard]] double coldHotDistance() const
    {
        const QList<std::array<int, 2>> cold = edge(Band::Cold);
        const QList<std::array<int, 2>> hot = edge(Band::Hot);
        double nearest = 2.0;
        for (const auto &a : cold) {
            for (const auto &b : hot) {
                nearest = std::min(nearest, std::hypot(a[0] - b[0], a[1] - b[1]) / steps);
            }
        }
        return nearest;
    }

private:
    static qsizetype index(int i, int j) { return static_cast<qsizetype>(i) * (steps + 1) + j; }

    [[nodiscard]] QList<std::array<int, 2>> edge(Band band) const
    {
        QList<std::array<int, 2>> points;
        for (int i = 0; i <= steps; ++i) {
            for (int j = 0; j <= steps; ++j) {
                const auto other = [&](int di, int dj) {
                    const int ni = i + di;
                    const int nj = j + dj;
                    return ni >= 0 && nj >= 0 && ni <= steps && nj <= steps && m_bands[index(ni, nj)] != band;
                };
                if (m_bands[index(i, j)] == band && (other(1, 0) || other(-1, 0) || other(0, 1) || other(0, -1))) {
                    points.append({i, j});
                }
            }
        }
        return points;
    }

    QList<Band> m_bands;
};

// Land samples per biome and contacts within 16 and within 64 blocks, over all seeds.
struct Survey
{
    QHash<QString, int> shares;
    QHash<QString, int> near;
    QHash<QString, int> far;
    int land = 0;
};

} // namespace

// The climate order of the terrain (decision E-824): temperature and humidity change smoothly and
// the biomes lie in climate space so that neighbours on the map are neighbours in climate: cold
// and hot biomes are always separated by temperate ones.
class TestClimateOrder : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    Survey m_survey;

private slots:
    void initTestCase()
    {
        for (const qint64 seed : seeds) {
            const QcTerrainGenerator generator(m_content, seed);
            QVERIFY2(generator.warnings().isEmpty(), qPrintable(generator.warnings().join(u'\n')));
            const BiomeMap map(generator);
            add(m_survey.shares, map.shares());
            add(m_survey.near, map.contacts(16));
            add(m_survey.far, map.contacts(64));
            const Gradients g = gradients(generator);
            qInfo("seed %lld: temperature per block p50 %.5f p99 %.5f max %.5f, humidity p50 %.5f p99 %.5f max %.5f", seed,
                  quantile(g.temperature, 0.5), quantile(g.temperature, 0.99), g.temperature.last(), quantile(g.humidity, 0.5),
                  quantile(g.humidity, 0.99), g.humidity.last());
        }
        for (const int count : std::as_const(m_survey.shares)) {
            m_survey.land += count;
        }
        QStringList pairs = m_survey.far.keys();
        pairs.sort();
        for (const QString &pair : std::as_const(pairs)) {
            qInfo("contact %-32s within 16: %6d, within 64: %6d", qPrintable(pair), m_survey.near.value(pair), m_survey.far[pair]);
        }
    }

    void coldAndHotBiomesNeverMeet() const
    {
        QStringList met;
        for (auto it = m_survey.far.cbegin(); it != m_survey.far.cend(); ++it) {
            const QStringList pair = it.key().split(u'|');
            const bool coldHot = coldBiomes.contains(pair[0]) && hotBiomes.contains(pair[1]);
            const bool hotCold = hotBiomes.contains(pair[0]) && coldBiomes.contains(pair[1]);
            if (coldHot || hotCold) {
                met.append(u"%1 (%2)"_s.arg(it.key()).arg(it.value()));
            }
        }
        QVERIFY2(met.isEmpty(), qPrintable(met.join(u", "_s)));
    }

    // The transition biomes (decision E-825) lie between the biomes they lead over to: the forest
    // steppe between plains and desert, the jungle edge between forest and jungle, the snowy taiga
    // between tundra and taiga.
    void transitionBiomesLieBetweenTheirNeighbours() const
    {
        for (const QString &pair : {u"desert|forest_steppe"_s, u"forest_steppe|plains"_s, u"jungle|jungle_edge"_s, u"snowy_taiga|tundra"_s,
                                    u"snowy_taiga|taiga"_s}) {
            QVERIFY2(m_survey.near.value(pair) > 1000, qPrintable(pair));
        }
        // The jungle edge is narrow beside the forest (the jungle has its old share back, E-825).
        const int forestEdge = m_survey.near.value(u"forest|jungle_edge"_s);
        QVERIFY2(forestEdge > 400, qPrintable(QString::number(forestEdge)));
        // The desert meets the plains far less than the forest steppe (before 66: 6770 samples, now
        // about 750), the forest the jungle never closely.
        const int desertPlains = m_survey.near.value(u"desert|plains"_s);
        QVERIFY2(desertPlains * 5 < m_survey.near.value(u"desert|forest_steppe"_s), qPrintable(QString::number(desertPlains)));
        QCOMPARE(m_survey.near.value(u"forest|jungle"_s), 0);
    }

    void everyBiomeHasASaneShare()
    {
        for (const QcBiomeDefinition &biome : m_content->worldgen().biomes()) {
            // River biomes lie under the water of rivers, not on land (test_rivers, decision E-840);
            // delta biomes are rare and only at river mouths (test_river_deltas, decision E-850).
            if (biome.river || biome.delta) {
                continue;
            }
            const QString name = biome.name.mid(qsizetype{8});
            const double share = static_cast<double>(m_survey.shares.value(name)) / m_survey.land;
            qInfo("share %-16s %.3f", qPrintable(name), share);
            // Rare biomes are pinned by their own tests (flower valley: 2-4.5 %).
            const double low = biome.rarity > 0.0 ? 0.01 : 0.025;
            QVERIFY2(share > low && share < 0.35, qPrintable(name + u' ' + QString::number(share)));
        }
    }

    // The layout of the biome cells in climate space (decision E-824): at every ruggedness, inside
    // and outside the rarity spots, a cold cell lies at least 0.3 from a hot one. At the steepest
    // climate change on the map (about 0.0053 per block) that is some 57 blocks of temperate land.
    void coldAndHotCellsLieApartInClimateSpace() const
    {
        const QcTerrainGenerator generator(m_content, 7);
        double nearest = 2.0;
        for (int r = 0; r <= 10; ++r) {
            for (const double rarity : {0.0, 1.0}) {
                nearest = std::min(nearest, ClimateCells(generator, r / 10.0, rarity).coldHotDistance());
            }
        }
        qInfo("smallest cold-hot distance in climate space %.3f", nearest);
        QVERIFY2(nearest >= 0.3, qPrintable(QString::number(nearest)));
    }

    // The shares the human asked for (2026-10-21): desert, jungle and swamp keep about the land
    // they had before the transition biomes (on these seeds 5.7, 6.7 and 5.1 %; now 5.5, 6.1 and
    // 5.2 %), the flower valley 2.5-3 % (2.9 %). Measured on a wide window, sparse: 6144 x 6144
    // blocks around the origin, every 24th; the bounds leave about a tenth for sampling.
    void largeBiomesKeepTheirShare() const
    {
        constexpr int reach = 3072;
        constexpr int step = 24;
        QHash<QString, int> counts;
        int land = 0;
        for (const qint64 seed : {qint64{7}, qint64{42}, qint64{1234}, qint64{99}}) {
            const QcTerrainGenerator generator(m_content, seed);
            for (int z = -reach; z < reach; z += step) {
                for (int x = -reach; x < reach; x += step) {
                    const QcTerrainGenerator::Column column = generator.column(x, z);
                    if (column.landFactor > 0.99 && column.height > generator.seaLevel()) {
                        ++counts[generator.biomeName(column.biome).mid(qsizetype{8})];
                        ++land;
                    }
                }
            }
        }
        const QList<std::pair<QString, double>> lowest{
            {u"desert"_s, 0.05}, {u"jungle"_s, 0.055}, {u"swamp"_s, 0.046}, {u"flower_valley"_s, 0.023}};
        for (const auto &[name, low] : lowest) {
            const double share = static_cast<double>(counts.value(name)) / land;
            qInfo("wide share %-14s %.4f", qPrintable(name), share);
            QVERIFY2(share >= low, qPrintable(name + u' ' + QString::number(share)));
        }
        // The flower valley stays rare.
        QVERIFY(double(counts.value(u"flower_valley"_s)) / land < 0.035);
    }

    void isDeterministic()
    {
        const QcTerrainGenerator first(m_content, 42);
        const QcTerrainGenerator again(m_content, 42);
        QVERIFY(BiomeMap(first).biomes() == BiomeMap(again).biomes());
    }
};

QTEST_GUILESS_MAIN(TestClimateOrder)
#include "test_climate_order.moc"
