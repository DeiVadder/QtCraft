// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcMobReader.h"
#include "common/content/parser/QcWorldgenParser.h"
#include "common/content/registry/QcSpawnWeights.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = 0.05;
constexpr double noon = QcDayCycle::dayLength * 0.25;
constexpr double midnight = QcDayCycle::dayLength * 0.75;

// The test terrain in one biome, with a stone roof three blocks above the ground when `roofed`.
class OneBiomeTerrain : public QcTestTerrainGenerator
{
public:
    OneBiomeTerrain(const std::shared_ptr<const QcContentRegistry> &content, QString biome, bool roofed = false)
        : QcTestTerrainGenerator(content)
        , m_biome(std::move(biome))
        , m_roof(roofed ? content->blockId(u"qtcraft:stone"_s) : std::nullopt)
    {
    }

    QString biomeAt(int x, int z) const override
    {
        Q_UNUSED(x);
        Q_UNUSED(z);
        return m_biome;
    }

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override
    {
        std::shared_ptr<QcChunk> chunk = QcTestTerrainGenerator::generate(pos);
        for (int lx = 0; m_roof && lx < QcWorld::chunkSize; ++lx) {
            for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
                const int ground = std::max(surfaceHeight(pos.x * QcWorld::chunkSize + lx, pos.z * QcWorld::chunkSize + lz), QcWorld::seaLevel);
                chunk->setBlock(lx, ground + 3, lz, *m_roof);
            }
        }
        return chunk;
    }

private:
    QString m_biome;
    std::optional<QcBlockId> m_roof;
};

// What spawned: creatures by kind, and every new creature with its kind and feet.
struct Spawns
{
    QHash<QString, int> byKind;
    struct One
    {
        QString kind;
        QcVec3 feet;
    };
    QList<One> all;

    int of(const char *kind) const { return byKind.value(u"qtcraft:"_s + QString::fromLatin1(kind)); }
};

} // namespace

// The monster lists of the biomes (decision E-838): the weights and the waste of attempts as in the
// original game, husks and strays only in their biomes and under the open sky, drowned only in water,
// cave spiders never by themselves; on the test terrain in one biome and on generated worlds with
// fixed seeds and one random seed per run (decision E-846). Ticks are stepped by hand.
class TestBiomeSpawning : public QObject
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
        const qint64 seed = ok ? fixed : static_cast<qint64>(QRandomGenerator::global()->generate() & 0x7fffffff);
        qInfo("random seed %lld (set QC_TEST_SEED to repeat)", seed);
        return seed;
    }

    int type(const char *name) const { return m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name)).value_or(-1); }

    // The content with every monster in groups of one, so each spawn is one draw of the list.
    std::shared_ptr<const QcContentRegistry> singles() const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        for (QcMobDefinition definition : m_content->mobs()) {
            if (definition.isMonster()) {
                definition.maxGroup = 1;
                content->registerMob(definition);
            }
        }
        return content;
    }

    // The monsters that spawn within `ticks` at `time` around a player at `centre` in `world` with
    // the chunks within `radius` loaded; each is taken away at once, so no cap is reached.
    struct Run
    {
        double time = midnight;
        int ticks = 0;
        int radius = 6;
        // Seed of the spawner's draws.
        quint32 draws = 11;
    };
    static Spawns spawnsIn(const std::shared_ptr<const QcContentRegistry> &content, QcServerWorld &world, QcBlockPos centre, const Run &run)
    {
        const int radius = run.radius;
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world.loadChunk({centre.chunk().x + x, centre.chunk().z + z});
            }
        }
        const QcBlockPos column{centre.x, 0, centre.z};
        const QcVec3 player{centre.x + 0.5,
            static_cast<double>(world.chunk(column.chunk())->highestNonAir(column.localX(), column.localZ()) + 1), centre.z + 0.5};
        QcMobManager mobs(content, run.draws);
        quint32 nextId = 100;
        Spawns spawns;
        for (int tick = 0; tick < run.ticks; ++tick) {
            mobs.tick(world, {{7, player, true}}, run.time, dt, [&nextId] { return nextId++; });
            for (const QcCreature &mob : mobs.mobs()) {
                if (!content->mobs()[mob.type].isMonster()) {
                    continue;
                }
                const QString &kind = content->mobs()[mob.type].name;
                spawns.byKind[kind] += 1;
                spawns.all.append({kind, mob.motion.position});
            }
            mobs.removeAll();
        }
        return spawns;
    }

    Spawns spawnsInBiome(const QString &biome, double time, int ticks, bool roofed = false) const
    {
        const std::shared_ptr<const QcContentRegistry> content = singles();
        QcServerWorld world(content, std::make_unique<OneBiomeTerrain>(content, biome, roofed));
        return spawnsIn(content, world, {0, 0, 0}, {time, ticks});
    }

    // The nearest column to the origin that `fits` on a spiral of 48-block steps, at most 3000
    // blocks out, starting at an offset drawn from the seed; std::nullopt when there is none.
    template<typename Fits>
    static std::optional<QcBlockPos> findColumn(qint64 seed, const Fits &fits)
    {
        QRandomGenerator random{static_cast<quint32>(seed)};
        const int ox = random.bounded(-500, 500);
        const int oz = random.bounded(-500, 500);
        for (int ring = 0; ring <= 62; ++ring) {
            for (int dx = -ring; dx <= ring; ++dx) {
                for (int dz = -ring; dz <= ring; ++dz) {
                    if ((std::abs(dx) == ring || std::abs(dz) == ring) && fits(ox + dx * 48, oz + dz * 48)) {
                        return QcBlockPos{ox + dx * 48, 0, oz + dz * 48};
                    }
                }
            }
        }
        return std::nullopt;
    }

    static std::optional<QcBlockPos> findBiome(const QcTerrainGenerator &generator, const QString &biome, qint64 seed)
    {
        return findColumn(seed, [&](int x, int z) { return generator.biomeAt(x, z) == biome; });
    }

    // P(X <= k) for X ~ binomial(n, p).
    static double binomialLowerTail(int n, int k, double p)
    {
        double term = std::pow(1.0 - p, n); // P(X = 0)
        double sum = term;
        for (int i = 1; i <= k; ++i) {
            term *= static_cast<double>(n - i + 1) / static_cast<double>(i) * p / (1.0 - p);
            sum += term;
        }
        return sum;
    }

    // Husks and zombies under the open sky of deserts, over all generated worlds of a run.
    struct DesertDraws
    {
        int husks = 0;
        int zombies = 0;
    };
    DesertDraws m_desert;

    // On a generated world: husks only in deserts, strays only in tundra, both under the open sky,
    // drowned with their feet in water, no cave spiders; counts the desert's draws in m_desert.
    // The desert runs longer than the others for a large enough count.
    void checkGeneratedWorld(qint64 seed)
    {
        const std::shared_ptr<const QcContentRegistry> content = singles();
        const QcTerrainGenerator generator(content, seed);
        for (const QString &biome : {u"qtcraft:desert"_s, u"qtcraft:tundra"_s, u"qtcraft:river"_s}) {
            const std::optional<QcBlockPos> found = findBiome(generator, biome, seed);
            if (!found) {
                qInfo("seed %lld: no %s found", seed, qPrintable(biome));
                continue;
            }
            QcServerWorld world(content, std::make_unique<QcTerrainGenerator>(content, seed));
            const int ticks = biome == u"qtcraft:desert"_s ? 4800 : 800;
            const Spawns spawns = spawnsIn(content, world, *found, {midnight, ticks, 4, static_cast<quint32>(seed)});
            qInfo() << "seed" << seed << biome << "at" << found->x << found->z << spawns.byKind;
            for (const Spawns::One &one : spawns.all) {
                const QcBlockPos feet = QcPhysics::blockContaining(one.feet);
                const QString here = generator.biomeAt(feet.x, feet.z);
                const bool water = QcPhysics::isWaterAt(world, feet);
                QVERIFY2(one.kind != u"qtcraft:cave_spider"_s, qPrintable(u"seed %1"_s.arg(seed)));
                QVERIFY2(one.kind != u"qtcraft:husk"_s || (here == u"qtcraft:desert"_s && world.skyLight(feet) == 15), qPrintable(here));
                QVERIFY2(one.kind != u"qtcraft:stray"_s || (here == u"qtcraft:tundra"_s && world.skyLight(feet) == 15), qPrintable(here));
                QVERIFY2((one.kind == u"qtcraft:drowned"_s) == water, qPrintable(u"%1 at %2 %3 %4"_s.arg(one.kind).arg(feet.x).arg(feet.y).arg(feet.z)));
                const bool open = here == u"qtcraft:desert"_s && world.skyLight(feet) == 15;
                m_desert.husks += one.kind == u"qtcraft:husk"_s;
                m_desert.zombies += open && one.kind == u"qtcraft:zombie"_s;
            }
        }
    }

private slots:
    void weightsOfTheLists()
    {
        const QcSpawnWeights weights(*m_content);
        const auto weight = [&](const char *kind, const char *biome) { return weights.weight(type(kind), u"qtcraft:"_s + QString::fromLatin1(biome)); };
        // The common list: zombies, skeletons, spiders, slimes and Kjuts 100, shadewalkers 10,
        // drowned 5 (deep water only); no witches (only at their huts), cave spiders or variants.
        for (const char *kind : {"zombie", "skeleton", "spider", "slime", "kjut"}) {
            QCOMPARE(weight(kind, "plains"), 100.0);
        }
        QCOMPARE(weight("shadewalker", "plains"), 10.0);
        QCOMPARE(weight("witch", "plains"), 0.0);
        QCOMPARE(weight("drowned", "plains"), 5.0);
        QCOMPARE(weight("cave_spider", "plains"), 0.0);
        QCOMPARE(weight("husk", "plains"), 0.0);
        QCOMPARE(weight("stray", "plains"), 0.0);
        QCOMPARE(weight("cow", "plains"), 0.0);
        QCOMPARE(weights.total(u"qtcraft:plains"_s), 515.0);
        QCOMPARE(weights.total(QString()), 515.0);

        // Desert: husks 80 of the zombies' 100; tundra: strays 80 of the skeletons'; rivers.
        QCOMPARE(weight("zombie", "desert"), 20.0);
        QCOMPARE(weight("husk", "desert"), 80.0);
        QCOMPARE(weights.total(u"qtcraft:desert"_s), 515.0);
        QCOMPARE(weight("skeleton", "tundra"), 20.0);
        QCOMPARE(weight("stray", "tundra"), 80.0);
        QCOMPARE(weight("stray", "snowy_taiga"), 0.0);
        QCOMPARE(weight("drowned", "river"), 100.0);
        QCOMPARE(weight("drowned", "frozen_river"), 1.0);
        QCOMPARE(weight("slime", "swamp"), 100.0);
        QVERIFY(QcSpawnWeights::problems(*m_content).isEmpty());
    }

    void biomeWeightsAreChecked()
    {
        const auto parse = [](const char *json) {
            return QcWorldgenParser::parseBiome(QJsonDocument::fromJson(json).object(), u"qtcraft"_s);
        };
        QVERIFY(parse(R"({"name": "b", "surface": "sand", "monsters": {"zombie": 20, "husk": 80}})"));
        QCOMPARE(parse(R"({"name": "b", "surface": "sand", "monsters": {"zombie": 20}})")->monsterWeights.first().mob, u"qtcraft:zombie"_s);
        QVERIFY(!parse(R"({"name": "b", "surface": "sand", "monsters": {"zombie": 120}})"));
        QVERIFY(!parse(R"({"name": "b", "surface": "sand", "monsters": {"zombie": "many"}})"));
        QVERIFY(!parse(R"({"name": "b", "surface": "sand", "monsters": ["zombie"]})"));

        // Unknown creatures and ones that are no monsters are left out with a warning.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcBiomeDefinition plains = *std::ranges::find(content->worldgen().biomes(), u"qtcraft:plains"_s, &QcBiomeDefinition::name);
        plains.monsterWeights = {{u"qtcraft:nobody"_s, 10.0}, {u"qtcraft:cow"_s, 10.0}};
        content->worldgen().registerBiome(plains);
        QCOMPARE(QcSpawnWeights::problems(*content).size(), 2);
        QCOMPARE(QcSpawnWeights(*content).weight(type("cow"), u"qtcraft:plains"_s), 0.0);

        // Spawning in water is for walkers.
        const auto mob = [](const char *json) { return QcMobReader(QJsonDocument::fromJson(json).object(), u"qtcraft"_s).read(); };
        const QcResult<QcMobDefinition> walker = mob(R"({"name": "m", "hostile": true, "skeleton": "humanoid", "outfit": "zombie", "spawn": {"water": true}})");
        QVERIFY2(walker, qPrintable(walker ? QString() : walker.error()));
        QVERIFY(!mob(R"({"name": "m", "hostile": true, "skeleton": "humanoid", "outfit": "zombie", "flying": true, "spawn": {"water": true}})"));
        QVERIFY(!mob(R"({"name": "m", "hostile": true, "skeleton": "humanoid", "outfit": "zombie", "spawn": {"also": [{"water": true}]}, "aquatic": {}})"));
    }

    void sharesPerBiome_data()
    {
        QTest::addColumn<QString>("biome");
        QTest::addColumn<QString>("variant");
        QTest::addColumn<QString>("replaced");
        QTest::newRow("no biome") << QString() << QString() << QString();
        QTest::newRow("plains") << u"qtcraft:plains"_s << QString() << QString();
        QTest::newRow("desert") << u"qtcraft:desert"_s << u"qtcraft:husk"_s << u"qtcraft:zombie"_s;
        QTest::newRow("tundra") << u"qtcraft:tundra"_s << u"qtcraft:stray"_s << u"qtcraft:skeleton"_s;
    }

    void sharesPerBiome()
    {
        QFETCH(QString, biome);
        QFETCH(QString, variant);
        QFETCH(QString, replaced);
        const Spawns spawns = spawnsInBiome(biome, midnight, 1600);
        const auto count = [&spawns](const QString &kind) { return spawns.byKind.value(kind); };
        const int total = static_cast<int>(spawns.all.size());
        QVERIFY2(total > 250, qPrintable(QString::number(total)));
        qInfo() << biome << spawns.byKind;

        // Nothing but the list's kinds, no cave spiders, no variants of other biomes.
        QCOMPARE(spawns.of("cave_spider"), 0);
        QCOMPARE(spawns.of("husk"), biome == u"qtcraft:desert"_s ? spawns.of("husk") : 0);
        QCOMPARE(spawns.of("stray"), biome == u"qtcraft:tundra"_s ? spawns.of("stray") : 0);
        QCOMPARE(spawns.of("slime"), 0);

        // Spiders and Kjuts keep 100 each in every list; a variant takes 80 of its kind's 100.
        const double spiders = count(u"qtcraft:spider"_s);
        const double kjuts = count(u"qtcraft:kjut"_s);
        QVERIFY2(kjuts > 0.75 * spiders && kjuts < 1.33 * spiders, qPrintable(u"%1 %2"_s.arg(kjuts).arg(spiders)));
        if (!variant.isEmpty()) {
            const double share = static_cast<double>(count(variant)) / (count(variant) + count(replaced));
            QVERIFY2(share > 0.7 && share < 0.9, qPrintable(QString::number(share)));
            QVERIFY2(count(variant) + count(replaced) > 0.75 * spiders && count(variant) + count(replaced) < 1.33 * spiders,
                     qPrintable(QString::number(count(variant) + count(replaced))));
        } else {
            QVERIFY2(count(u"qtcraft:zombie"_s) > 0.75 * spiders && count(u"qtcraft:zombie"_s) < 1.33 * spiders, qPrintable(QString::number(count(u"qtcraft:zombie"_s))));
        }
        // Shadewalkers 10 of the 100 of a spider, no witches.
        QVERIFY2(spawns.of("shadewalker") > 0 && spawns.of("shadewalker") < 0.25 * spiders, qPrintable(QString::number(spawns.of("shadewalker"))));
        QCOMPARE(spawns.of("witch"), 0);
    }

    // Husks and strays need the open sky: under a roof by day only their plain kinds spawn.
    void variantsNeedTheOpenSky()
    {
        for (const QString &biome : {u"qtcraft:desert"_s, u"qtcraft:tundra"_s}) {
            const Spawns spawns = spawnsInBiome(biome, noon, 600, true);
            QVERIFY2(spawns.all.size() > 30, qPrintable(QString::number(spawns.all.size())));
            QCOMPARE(spawns.of("husk"), 0);
            QCOMPARE(spawns.of("stray"), 0);
            QVERIFY(spawns.of("zombie") + spawns.of("skeleton") > 0);
        }
    }

    // Drowned only with their feet in water, the others never; in rivers far more often than in
    // other water (100 against 5 of the list).
    void drownedOnlyInWater()
    {
        const std::shared_ptr<const QcContentRegistry> content = singles();
        int inRivers = 0;
        int elsewhere = 0;
        for (const QString &biome : {u"qtcraft:river"_s, u"qtcraft:plains"_s}) {
            QcServerWorld world(content, std::make_unique<OneBiomeTerrain>(content, biome));
            const Spawns spawns = spawnsIn(content, world, {0, 0, 0}, {midnight, 1200});
            for (const Spawns::One &one : spawns.all) {
                const bool water = QcPhysics::isWaterAt(world, QcPhysics::blockContaining(one.feet));
                QVERIFY2((one.kind == u"qtcraft:drowned"_s) == water, qPrintable(one.kind));
            }
            (biome == u"qtcraft:river"_s ? inRivers : elsewhere) = spawns.of("drowned");
        }
        // Outside rivers only the deepest spots of the test terrain's pools are deep enough.
        QVERIFY2(inRivers > 10 && elsewhere * 5 < inRivers, qPrintable(u"%1 %2"_s.arg(inRivers).arg(elsewhere)));
    }

    // The cost of the beds of water as spawn spots (decision E-838): around a player over the open
    // sea every column offers its bed, so each try draws a kind and tests its rules, as on land.
    // Logs the time per tick over the sea and over land and what spawned (docs/Performance-Verlauf.md,
    // Paket 73.2); checks only that over the sea nothing but drowned comes up from the water.
    void oceanBedsAsSpawnSpots()
    {
        const std::shared_ptr<const QcContentRegistry> content = singles();
        const QcTerrainGenerator generator(content, 42);
        const int sea = generator.seaLevel();
        const std::optional<QcBlockPos> ocean = findColumn(42, [&](int x, int z) { return generator.column(x, z).height < sea - 12; });
        const std::optional<QcBlockPos> land = findBiome(generator, u"qtcraft:plains"_s, 42);
        QVERIFY(ocean && land);
        for (const QcBlockPos &centre : {*ocean, *land}) {
            QcServerWorld world(content, std::make_unique<QcTerrainGenerator>(content, 42));
            // One tick first: the chunks load and generate outside the measurement.
            spawnsIn(content, world, centre, {midnight, 1, 4});
            QElapsedTimer timer;
            timer.start();
            const Spawns spawns = spawnsIn(content, world, centre, {midnight, 400, 4});
            qInfo() << (centre == *ocean ? "sea" : "land") << "at" << centre.x << centre.z
                << static_cast<double>(timer.nsecsElapsed()) / 400e6 << "ms per tick" << spawns.byKind;
            for (const Spawns::One &one : spawns.all) {
                const bool water = QcPhysics::isWaterAt(world, QcPhysics::blockContaining(one.feet));
                QVERIFY2(!water || one.kind == u"qtcraft:drowned"_s, qPrintable(one.kind));
            }
        }
    }

    void generatedWorlds()
    {
        m_desert = {};
        for (const qint64 seed : {qint64{7}, qint64{42}, m_randomSeed}) {
            checkGeneratedWorld(seed);
            if (QTest::currentTestFailed()) {
                return;
            }
        }

        // Under the open sky of deserts husks take 80 of the zombies' 100: of n draws the husks are
        // binomial(n, 0.8). The check fails only below the 0.1 % quantile, so a correct list fails
        // at most once in 1000 runs; the three worlds give n of about 50 to 90 (the fixed seeds
        // alone 41), where an even split (p = 0.5) would fail with 94 % at n = 50.
        const int draws = m_desert.husks + m_desert.zombies;
        qInfo("open desert: %d husks, %d zombies", m_desert.husks, m_desert.zombies);
        QVERIFY2(draws >= 20, qPrintable(QString::number(draws)));
        QVERIFY2(binomialLowerTail(draws, m_desert.husks, 0.8) >= 0.001, qPrintable(u"%1 husks, %2 zombies"_s.arg(m_desert.husks).arg(m_desert.zombies)));
    }
};

QTEST_MAIN(TestBiomeSpawning)
#include "test_biome_spawning.moc"
