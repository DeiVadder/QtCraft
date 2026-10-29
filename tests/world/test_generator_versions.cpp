// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcTestUtil.h"
#include "world/QcChunkChecksum.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"
#include "worldgen/terrain/QcRiverField.h"
#include "worldgen/terrain/QcSurfaceLakes.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QTest>

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

// Every world generator version still generates what it generated when it was recorded (decision
// E-922). For each version, terrain worlds of three seeds with version 1's default options and with
// changed ones, flat worlds and a void world are generated, and a SHA-256 per world is compared with
// tests/data/generator-versions.json. A terrain world hashes its spawn point, its one-time setup,
// four fixed chunks, the chunks of the nearest village (centre and several houses, and the plan's
// data), dungeon, mine shaft (with the chunks around it and its way up), coastal tower and of every
// surface structure /locate knows (all chunks under it), the nearest columns of rivers, deltas,
// frozen rivers, swamps, deserts and snowy land (a coarse biome search) and of a lake inland: their
// blocks by state name, furnishing and biomes. The worlds with version 1's default options also have
// a checksum of their rivers ("<world> rivers"): the distance to a built river of each column on a
// coarse grid, which shows which pieces of rivers are built (decisions E-851, E-935); few of them
// lie in the hashed chunks. The seeds and options are fixed (no seed drawn per run
// as in decision E-846, and version 1's stored values rather than today's defaults): the test
// compares with recorded values.
//
// The values are kept per platform ("<kernel>-<cpu>", e.g. darwin-arm64): floating point rounds
// differently on other CPUs and C libraries; the same world on the same system is what a version
// promises. A platform without values fails, unless the reference lists it under "unrecorded" or the
// version is still open and the run is no CI run (CI set); then it skips and prints them. QC_RECORD_GENERATOR_REFERENCE=<version> writes this platform's values of that
// version into the file; for a frozen version (QcGeneratorVersion::lastFrozen) only when the platform
// has none yet (the checklist in decision E-922).
namespace {

using Checksums = QJsonObject;

// One world of the reference: generator type, options, world generation options and seed.
struct Case
{
    QString name;
    QString type;
    QString options;
    QJsonObject worldOptions;
    qint64 seed = 0;
    // Also hash the surface structures, the biome search and the lake (the worlds with version 1's
    // default options; the others only the fixed chunks and the places, which is where their
    // options act and keeps the run short).
    bool explore = false;
};

// What the terrain worlds of one version covered, for the coverage checks.
struct Coverage
{
    QSet<QString> located;
    QSet<QString> biomes;
    QSet<QString> blocks;
    qint64 caveCells = 0;
    qint64 riverColumns = 0;
};

const QString terrain = QString::fromLatin1(QcWorldGenerators::terrain);

// The world generation options a new terrain world of version 1 stores with the defaults.
const QJsonObject version1Options{{u"qtcraft:biome_size"_s, 1},       {u"qtcraft:caves"_s, true},       {u"qtcraft:coastal_towers"_s, 0.4},
                                  {u"qtcraft:dungeons"_s, 0.6},       {u"qtcraft:mineshafts"_s, 0.6},   {u"qtcraft:ore_amount"_s, 1},
                                  {u"qtcraft:rivers"_s, true},        {u"qtcraft:village_chance"_s, 0.65}, {u"qtcraft:village_spacing"_s, 384}};

// Chunks every world of the reference generates: around the origin and some further away.
constexpr std::array<QcChunkPos, 4> fixedChunks{{{0, 0}, {1, -1}, {-3, 2}, {37, -12}}};
// How far places, structures, biomes and lakes are looked for, and the grid of the biome search.
constexpr int placeReach = 1024;
constexpr int structureReach = 2048;
constexpr int biomeReach = 1536;
constexpr int biomeStep = 48;
constexpr int wideBiomeReach = 3072;
constexpr int wideBiomeStep = 64;
constexpr int lakeRings = 24;
// The rivers are hashed on a grid of riverStep blocks within riverReach of the origin.
constexpr int riverReach = 1536;
constexpr int riverStep = 16;
// The places /locate finds that are no placed structure.
const QStringList places{u"village"_s, u"dungeon"_s, u"mineshaft"_s, u"coastal_tower"_s};
// The biomes looked for; one column of each set nearest to the origin.
const QList<QStringList> wantedBiomes{{u"qtcraft:river"_s},
                                      {u"qtcraft:delta"_s, u"qtcraft:lush_delta"_s, u"qtcraft:frozen_delta"_s},
                                      {u"qtcraft:frozen_river"_s},
                                      {u"qtcraft:swamp"_s},
                                      {u"qtcraft:desert"_s},
                                      {u"qtcraft:tundra"_s, u"qtcraft:snowy_taiga"_s}};

QString platform()
{
    return QSysInfo::kernelType() + u'-' + QSysInfo::buildCpuArchitecture();
}

// The checksum of a terrain world's rivers and how many of its columns lie near one.
struct Rivers
{
    QString checksum;
    qint64 columns = 0;
};

// The distance to the nearest built river (whole blocks, -1 beyond its reach) of each column on the
// river grid: a closed piece (decision E-851) leaves its columns far from every river.
Rivers riversOf(const QcTerrainGenerator &generator)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    Rivers rivers;
    for (int z = -riverReach; z < riverReach; z += riverStep) {
        for (int x = -riverReach; x < riverReach; x += riverStep) {
            const double distance = generator.column(x, z).riverDistance;
            const qint32 near = distance < QcRiverField::reach ? static_cast<qint32>(std::lround(distance)) : -1;
            rivers.columns += near >= 0 ? 1 : 0;
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): raw bytes of a trivially copyable value for the hash
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(&near), sizeof near));
        }
    }
    rivers.checksum = QString::fromLatin1(hash.result().toHex());
    return rivers;
}

// The column of each set of `wanted` biomes nearest to the origin on a grid of `step` blocks within
// `reach`, if any.
QList<std::optional<QcBlockPos>> nearestOnGrid(const QcWorldGenerator &generator, const QList<QStringList> &wanted, int reach, int step)
{
    QList<std::optional<QcBlockPos>> nearest(wanted.size());
    const auto distance = [](QcBlockPos pos) { return std::abs(pos.x) + std::abs(pos.z); };
    for (int z = -reach; z <= reach; z += step) {
        for (int x = -reach; x <= reach; x += step) {
            const QString biome = generator.biomeAt(x, z);
            const QcBlockPos at{x, 64, z};
            for (qsizetype i = 0; i < wanted.size(); ++i) {
                if (wanted.at(i).contains(biome) && (!nearest.at(i) || distance(at) < distance(*nearest.at(i)))) {
                    nearest[i] = at;
                }
            }
        }
    }
    return nearest;
}

// The chunk of the column of each wanted biome nearest to the origin on a coarse grid; what is not
// within biomeReach (deltas lie at the coast, often further) is looked for on a coarser, wider grid.
QList<QcChunkPos> biomeChunks(const QcWorldGenerator &generator)
{
    QList<std::optional<QcBlockPos>> nearest = nearestOnGrid(generator, wantedBiomes, biomeReach, biomeStep);
    QList<QStringList> missing;
    for (qsizetype i = 0; i < wantedBiomes.size(); ++i) {
        missing.append(nearest.at(i) ? QStringList() : wantedBiomes.at(i));
    }
    const QList<std::optional<QcBlockPos>> further = nearestOnGrid(generator, missing, wideBiomeReach, wideBiomeStep);
    QList<QcChunkPos> chunks;
    for (qsizetype i = 0; i < wantedBiomes.size(); ++i) {
        if (const std::optional<QcBlockPos> at = nearest.at(i) ? nearest.at(i) : further.at(i)) {
            chunks.append(at->chunk());
        }
    }
    return chunks;
}

// The chunk of the lake nearest to the origin (in rings of chunks) above the sea level: inland.
std::optional<QcChunkPos> lakeChunk(const QcTerrainGenerator &generator)
{
    for (int ring = 0; ring <= lakeRings; ++ring) {
        for (int z = -ring; z <= ring; ++z) {
            for (int x = -ring; x <= ring; ++x) {
                if (std::max(std::abs(x), std::abs(z)) != ring) {
                    continue;
                }
                const std::optional<QcSurfaceLakes::Site> site = generator.lakes().siteIn({x, z});
                if (site && site->level > generator.seaLevel()) {
                    return site->chunk;
                }
            }
        }
    }
    return std::nullopt;
}

// The checksum of one world and what it covered: the chunks it hashes, gathered first, then hashed.
class Sampler
{
public:
    Sampler(const QcWorldGenerator &generator, const QcContentRegistry &content, Coverage &coverage, bool explore)
        : m_generator(generator)
        , m_hash(content)
        , m_coverage(coverage)
        , m_explore(explore)
    {
    }

    QString checksum()
    {
        m_hash.addBlockPos(m_generator.spawnPoint());
        m_hash.addSetup(m_generator.setup());
        for (const QcChunkPos pos : fixedChunks) {
            add(pos);
        }
        if (dynamic_cast<const QcTerrainGenerator *>(&m_generator)) {
            addPlaces();
        }
        if (const auto *terrainGenerator = dynamic_cast<const QcTerrainGenerator *>(&m_generator); terrainGenerator && m_explore) {
            addStructures();
            const QList<QcChunkPos> biomes = biomeChunks(m_generator);
            for (const QcChunkPos pos : biomes) {
                add(pos);
            }
            if (const std::optional<QcChunkPos> lake = lakeChunk(*terrainGenerator)) {
                m_coverage.located.insert(u"lake"_s);
                add(*lake);
            }
        }
        qInfo("  %lld chunks", static_cast<long long>(m_chunks.size()));
        for (const QcChunkPos pos : std::as_const(m_chunks)) {
            hashChunk(pos);
        }
        m_coverage.blocks.unite(m_hash.seenNames());
        m_coverage.caveCells += m_hash.caveCells();
        return m_hash.result();
    }

private:
    void add(QcChunkPos pos)
    {
        if (!m_chunks.contains(pos)) {
            m_chunks.append(pos);
        }
    }

    // The nearest village (centre, several houses spread over the plan, and the plan's data), dungeon,
    // mine shaft (with the chunks beside its centre and its way up) and coastal tower.
    void addPlaces()
    {
        const QcBlockPos origin{0, 64, 0};
        if (const std::shared_ptr<const QcVillagePlan> village = m_generator.nearestVillage(origin, placeReach)) {
            m_coverage.located.insert(u"village"_s);
            m_hash.addVillage(*village);
            add(village->centre.chunk());
            const qsizetype step = std::max<qsizetype>(1, village->pieces.size() / 4);
            for (qsizetype i = 0; i < village->pieces.size(); i += step) {
                add(village->pieces.at(i).origin().chunk());
            }
        }
        for (const QString &kind : {u"dungeon"_s, u"mineshaft"_s, u"coastal_tower"_s}) {
            const QcLocateResult found = m_generator.locate(kind, origin, placeReach);
            if (!found.place) {
                continue;
            }
            m_coverage.located.insert(kind);
            const QcChunkPos at = found.place->at.chunk();
            add(at);
            if (kind == u"mineshaft"_s) {
                for (const QcChunkPos side : {QcChunkPos{at.x + 1, at.z}, QcChunkPos{at.x - 1, at.z}, QcChunkPos{at.x, at.z + 1}, QcChunkPos{at.x, at.z - 1}}) {
                    add(side);
                }
                if (found.place->entrance) {
                    add(found.place->entrance->chunk());
                }
            }
        }
    }

    // The chunks under the nearest copy of every surface structure /locate knows.
    void addStructures()
    {
        const QStringList names = m_generator.locatableNames();
        for (const QString &name : names) {
            if (places.contains(name)) {
                continue;
            }
            const QString full = name.contains(u':') ? name : u"qtcraft:"_s + name;
            if (const std::optional<QcStructurePlacement> placement = m_generator.nearestStructure(full, {0, 64, 0}, structureReach)) {
                m_coverage.located.insert(name);
                add(placement->origin().offset(placement->sizeX() / 2, 0, placement->sizeZ() / 2).chunk());
            }
        }
    }

    void hashChunk(QcChunkPos pos)
    {
        m_hash.addInts({pos.x, pos.z});
        m_hash.addBlocks(*m_generator.generate(pos));
        m_hash.addFurnishing(m_generator.furnishingIn(pos));
        for (int z = 0; z < QcWorld::chunkSize; z += 4) {
            for (int x = 0; x < QcWorld::chunkSize; x += 4) {
                const QString biome = m_generator.biomeAt(pos.x * QcWorld::chunkSize + x, pos.z * QcWorld::chunkSize + z);
                m_hash.addString(biome);
                m_coverage.biomes.insert(biome);
            }
        }
    }

    const QcWorldGenerator &m_generator;
    QcChunkChecksum m_hash;
    Coverage &m_coverage;
    QList<QcChunkPos> m_chunks;
    const bool m_explore;
};

} // namespace

class TestGeneratorVersions : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // The worlds of the reference, with the options a new world of version 1 stores.
    static QList<Case> cases()
    {
        const QString flat = QString::fromLatin1(QcWorldGenerators::flat);
        const QString empty = QString::fromLatin1(QcWorldGenerators::empty);
        QJsonObject changed = version1Options;
        changed.insert(u"qtcraft:biome_size"_s, 2);
        changed.insert(u"qtcraft:caves"_s, false);
        changed.insert(u"qtcraft:ore_amount"_s, 2);
        changed.insert(u"qtcraft:village_spacing"_s, 256);
        QJsonObject wilderness = version1Options;
        wilderness.insert(u"qtcraft:rivers"_s, false);
        wilderness.insert(u"qtcraft:village_chance"_s, 0);
        wilderness.insert(u"qtcraft:dungeons"_s, 0);
        return {
            {u"terrain 42"_s, terrain, {}, version1Options, 42, true},
            {u"terrain 7"_s, terrain, {}, version1Options, 7, true},
            {u"terrain 20261017"_s, terrain, {}, version1Options, 20261017, true},
            {u"terrain 42 changed options"_s, terrain, {}, changed, 42},
            {u"terrain 7 wilderness"_s, terrain, {}, wilderness, 7},
            {u"flat default"_s, flat, u"bedrock,59*stone,3*dirt,grass_block"_s, {}, 3},
            {u"flat layers"_s, flat, u"bedrock,2*dirt,grass_block"_s, {}, 3},
            {u"void default"_s, empty, u"platform=qtcraft:stone,radius=2,y=63"_s, {}, 3},
        };
    }

    // The checksums of the worlds of the reference as generator version `version` makes them, and
    // what their terrain worlds covered.
    struct Run
    {
        Checksums checksums;
        Coverage coverage;
    };

    Run checksumsOf(int version) const
    {
        Checksums checksums;
        Coverage coverage;
        const QList<Case> worlds = cases();
        for (const Case &world : worlds) {
            const QcWorldOptions::Applied applied = QcWorldOptions::apply(m_content, world.type, world.worldOptions);
            const std::unique_ptr<QcWorldGenerator> generator =
                QcWorldGenerators::create(world.type, applied.content, world.seed, world.options, version);
            if (!generator) {
                checksums.insert(world.name, u"no generator"_s);
                continue;
            }
            Sampler sampler(*generator, *applied.content, coverage, world.explore);
            checksums.insert(world.name, sampler.checksum());
            if (const auto *terrainGenerator = dynamic_cast<const QcTerrainGenerator *>(generator.get()); terrainGenerator && world.explore) {
                const Rivers rivers = riversOf(*terrainGenerator);
                checksums.insert(world.name + u" rivers"_s, rivers.checksum);
                coverage.riverColumns += rivers.columns;
            }
        }
        return {checksums, coverage};
    }

    static QJsonObject readReference()
    {
        QFile file(QStringLiteral(QTCRAFT_GENERATOR_REFERENCE));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    static bool writeReference(const QJsonObject &reference)
    {
        QFile file(QStringLiteral(QTCRAFT_GENERATOR_REFERENCE));
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(reference).toJson()) > 0;
    }

    // Records this platform's values of `version` (QC_RECORD_GENERATOR_REFERENCE); a frozen version
    // keeps values it has.
    static QString record(int version, const Checksums &actual)
    {
        QJsonObject reference = readReference();
        QJsonObject versions = reference.value(u"versions"_s).toObject();
        QJsonObject platforms = versions.value(QString::number(version)).toObject();
        if (version <= QcGeneratorVersion::lastFrozen && platforms.contains(platform())) {
            return u"version %1 is frozen: its values for %2 stay as recorded"_s.arg(version).arg(platform());
        }
        platforms.insert(platform(), actual);
        versions.insert(QString::number(version), platforms);
        reference.insert(u"versions"_s, versions);
        return writeReference(reference) ? QString() : u"cannot write the reference"_s;
    }

    // Whether the reference may lack this platform's values of `version`: listed as unrecorded, or
    // an open version outside a CI run.
    static bool mayLackPlatform(int version)
    {
        const bool listed = readReference().value(u"unrecorded"_s).toArray().contains(platform());
        const bool open = version > QcGeneratorVersion::lastFrozen;
        return listed || (open && !qEnvironmentVariableIsSet("CI"));
    }

private slots:
    void versionsKeepTheirWorlds_data()
    {
        QTest::addColumn<int>("version");
        for (int version = QcGeneratorVersion::oldest; version <= QcGeneratorVersion::current; ++version) {
            QTest::addRow("version %d", version) << version;
        }
    }

    void versionsKeepTheirWorlds()
    {
        QFETCH(int, version);
        const Run run = checksumsOf(version);
        const Checksums &actual = run.checksums;
        const Coverage &coverage = run.coverage;
        qInfo().noquote() << u"version %1 on %2:\n%3"_s.arg(version).arg(platform(), QString::fromUtf8(QJsonDocument(actual).toJson()));
        checkCoverage(coverage);

        if (qEnvironmentVariableIntValue("QC_RECORD_GENERATOR_REFERENCE") == version) {
            const QString refused = record(version, actual);
            QVERIFY2(refused.isEmpty(), qPrintable(refused));
        }
        const QJsonObject platforms = readReference().value(u"versions"_s).toObject().value(QString::number(version)).toObject();
        QVERIFY2(!platforms.isEmpty(), qPrintable(u"no values of generator version %1 in the reference; record them"_s.arg(version)));
        if (!platforms.contains(platform())) {
            const QString missing = u"no values of generator version %1 for %2 (printed above)"_s.arg(version).arg(platform());
            QVERIFY2(mayLackPlatform(version), qPrintable(missing + u"; record them or list the platform as unrecorded"_s));
            QSKIP(qPrintable(missing));
        }

        const QJsonObject expected = platforms.value(platform()).toObject();
        QCOMPARE(QStringList(actual.keys()), QStringList(expected.keys()));
        for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
            // A difference means generator version `version` no longer makes the worlds it made:
            // bring the old output back for this version (E-922) instead of updating the values.
            QVERIFY2(actual.value(it.key()) == it.value(), qPrintable(u"version %1, %2"_s.arg(version).arg(it.key())));
        }
    }

    // The reference holds the versions the game has, no more; frozen versions are the oldest ones.
    void theReferenceHoldsTheKnownVersions()
    {
        const QJsonObject versions = readReference().value(u"versions"_s).toObject();
        QVERIFY2(!versions.isEmpty(), QTCRAFT_GENERATOR_REFERENCE);
        const QStringList keys = versions.keys();
        for (const QString &key : keys) {
            QVERIFY2(QcGeneratorVersion::isKnown(key.toInt()), qPrintable(u"the reference has values of unknown version '%1'"_s.arg(key)));
        }
        QVERIFY(QcGeneratorVersion::lastFrozen <= QcGeneratorVersion::current);
    }

    // A generator exists for every version the game has and for no other, and knows its version.
    void theFactoryKnowsTheVersions()
    {
        for (int version = QcGeneratorVersion::oldest; version <= QcGeneratorVersion::current; ++version) {
            const std::unique_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(terrain, m_content, 1, {}, version);
            QVERIFY(generator);
            QCOMPARE(generator->version(), version);
        }
        QVERIFY(!QcWorldGenerators::create(terrain, m_content, 1, {}, QcGeneratorVersion::current + 1));
        QVERIFY(!QcWorldGenerators::create(terrain, m_content, 1, {}, QcGeneratorVersion::oldest - 1));
        QVERIFY(!QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::flat), m_content, 1, {}, 0));
        QCOMPARE(QcGeneratorVersion::unversioned, 1);
        // The showcase is not versioned: always the current code.
        QCOMPARE(QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::showcase), m_content, 1, {}, QcGeneratorVersion::oldest)->version(),
                 QcGeneratorVersion::current);
    }

private:
    // The terrain worlds found every kind /locate knows, a lake, and hashed rivers, deltas, ice or
    // snow, ores, leaves and caves, and columns near rivers.
    void checkCoverage(const Coverage &coverage) const
    {
        const QcTerrainGenerator generator(m_content, 42);
        QStringList kinds = generator.locatableNames();
        kinds.append(u"lake"_s);
        for (const QString &kind : std::as_const(kinds)) {
            QVERIFY2(coverage.located.contains(kind), qPrintable(u"no %1 found"_s.arg(kind)));
        }
        for (const QStringList &biomes : wantedBiomes) {
            QVERIFY2(std::ranges::any_of(biomes, [&coverage](const QString &biome) { return coverage.biomes.contains(biome); }), qPrintable(biomes.join(u'/')));
        }
        const auto seen = [&coverage](QStringView part) {
            return std::ranges::any_of(coverage.blocks, [part](const QString &name) { return name.contains(part); });
        };
        for (const QStringView part : {u"ice", u"snow", u"leaves", u"coal_ore", u"iron_ore", u"copper_ore", u"gold_ore", u"lapis_ore", u"water"}) {
            QVERIFY2(seen(part), qPrintable(part.toString()));
        }
        QVERIFY(coverage.caveCells > 0);
        QVERIFY(coverage.riverColumns > 0);
    }
};

QTEST_GUILESS_MAIN(TestGeneratorVersions)
#include "test_generator_versions.moc"
