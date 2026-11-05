// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcDimensionParser.h"
#include "common/world/QcWorldInfo.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerConfig.h"
#include "server/storage/QcGeneratedChunks.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcWorldStorage.h"
#include "server/versions/QcGeneratorSwitch.h"
#include "server/versions/QcVersionMapFile.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/generators/QcVersionedGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The seed whose terrain differs between generator versions 1 and 2 (near rivers, decision E-969;
// "terrain 7" in tests/data/generator-versions.json).
constexpr qint64 seed = 7;
const QString overworldName = QString::fromLatin1(QcDimensionDefinition::overworldName);
const QString terrainName = u"test:terrain"_s;

// The river near the column and its ground as numbers that differ when the versions build
// another river there: distance and width (quarter blocks, -1 far from all) and height.
std::array<long, 4> riverAt(const QcTerrainGenerator &generator, int x, int z)
{
    const QcTerrainColumn column = generator.column(x, z);
    const auto quarters = [](double value) { return value < 1.0e8 ? std::lround(value * 4.0) : -1; };
    return {quarters(column.riverDistance), quarters(column.riverNearDistance), quarters(column.riverHalfWidth), column.height};
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Puts `content` into the file `path`, its folder made first.
void putFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        QcTestUtil::failCase(u"Cannot write %1"_s.arg(path));
    }
}

// The base content with a terrain dimension of its own (decision E-978).
std::shared_ptr<QcContentRegistry> contentWithTerrainDimension()
{
    std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    const QJsonObject json{{u"name"_s, terrainName}, {u"generator"_s, QJsonObject{{u"type"_s, u"terrain"_s}}}, {u"weather"_s, false}};
    QcResult<QcDimensionDefinition> dimension = QcDimensionParser::parseDimension(json, u"test"_s);
    if (!dimension) {
        qFatal("test dimension does not parse: %s", qPrintable(dimension.error()));
    }
    content->worldgen().registerDimension(std::move(*dimension));
    return content;
}

} // namespace

// Switching a world to a newer generator version (package 87.1, decisions E-1010 to E-1012): every
// chunk generated before, changed or not, stays bit for bit as it was; only chunks never generated
// get the new version. Version 1 and 2 of the terrain generator are both frozen and differ near
// rivers, which the test looks for first.
class TestGeneratorSwitch : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = contentWithTerrainDimension();
    // Two chunks whose blocks differ between the versions, far apart: the kept and the fresh one.
    QList<QcChunkPos> m_differing;

    std::unique_ptr<QcWorldGenerator> terrain(int version) const
    {
        return QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::terrain), m_content, seed, {}, version);
    }

    QcServerConfig config(const QString &directory, int version) const
    {
        QcServerConfig config;
        config.worldName = u"Switched"_s;
        config.seed = seed;
        config.worldDirectory = directory;
        config.keepChunksLoaded = true;
        config.generatorVersion = version;
        return config;
    }

    std::unique_ptr<QcServer> server(const QString &directory, int version) const
    {
        auto server = std::make_unique<QcServer>(config(directory, version), m_content, terrain(version));
        if (const QcStatus opened = server->openWorld(); !opened) {
            QcTestUtil::failCase(u"The world in %1 does not open as version %2: %3"_s.arg(directory).arg(version).arg(opened.error()));
        }
        return server;
    }

    // The chunk `pos` of `world` as it loads.
    static QcChunk loaded(QcServerWorld &world, QcChunkPos pos)
    {
        const std::shared_ptr<const QcChunk> chunk = world.loadChunk(pos);
        if (!chunk) {
            QcTestUtil::failCase(u"The chunk %1, %2 does not load"_s.arg(pos.x).arg(pos.z));
            return QcChunk(pos);
        }
        return *chunk;
    }

    // A saved world of `type` on the test terrain in `directory`, open.
    std::unique_ptr<QcServer> testWorld(const QString &directory, const QString &type) const
    {
        QcServerConfig open = config(directory, QcGeneratorVersion::current);
        open.generator = type;
        auto world = std::make_unique<QcServer>(open, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        if (!world->openWorld() || !world->save()) {
            QcTestUtil::failCase(u"The test world in %1 does not open or save"_s.arg(directory));
        }
        return world;
    }

    // Why switching `dimension` of the world in `directory` to `version` is refused; empty when it
    // is not.
    QString refusal(const QString &directory, const QString &dimension, int version) const
    {
        const QcResult<QcGeneratorSwitch::Switched> switched = QcGeneratorSwitch::switchDimension(directory, dimension, version, m_content);
        qInfo().noquote() << (switched ? u"switched"_s : switched.error());
        return switched ? QString() : switched.error();
    }

    // The first two chunks on a spiral of the river grid whose blocks differ between the versions.
    QList<QcChunkPos> findDiffering() const
    {
        const std::unique_ptr<QcWorldGenerator> one = terrain(1);
        const std::unique_ptr<QcWorldGenerator> two = terrain(2);
        const auto *riversOne = dynamic_cast<const QcTerrainGenerator *>(one.get());
        const auto *riversTwo = dynamic_cast<const QcTerrainGenerator *>(two.get());
        QList<QcChunkPos> found;
        for (int ring = 0; ring < 96 && found.size() < 2; ++ring) {
            for (int dx = -ring; dx <= ring && found.size() < 2; ++dx) {
                for (int dz = -ring; dz <= ring && found.size() < 2; ++dz) {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring) {
                        continue;
                    }
                    const int x = dx * 16 + 8;
                    const int z = dz * 16 + 8;
                    const QcChunkPos chunk = QcBlockPos{x, 0, z}.chunk();
                    const bool far = found.isEmpty() || qcChunkDistance(found.front(), chunk) > 4;
                    if (far && riverAt(*riversOne, x, z) != riverAt(*riversTwo, x, z) && !QcTestUtil::sameBlocks(*one->generate(chunk), *two->generate(chunk))) {
                        found.append(chunk);
                    }
                }
            }
        }
        return found;
    }

private slots:
    void initTestCase()
    {
        m_differing = findDiffering();
        QCOMPARE(m_differing.size(), qsizetype{2});
        qInfo() << "Chunks that differ between versions 1 and 2:" << m_differing.front().x << m_differing.front().z << m_differing.back().x
                << m_differing.back().z;
    }

    // An overworld of version 1, explored and partly changed, switched to version 2: the explored
    // chunks load exactly as before, the changed one with its change, a new chunk as version 2.
    void theOverworldKeepsWhatWasGenerated()
    {
        QTemporaryDir dir;
        const QcChunkPos kept = m_differing.front();
        const QcChunkPos neighbour{kept.x + 1, kept.z};
        const QcChunkPos fresh = m_differing.back();
        const QcBlockPos glassAt{neighbour.x * QcWorld::chunkSize + 3, 200, neighbour.z * QcWorld::chunkSize + 5};
        const QcBlockId glass = *m_content->blockId(u"qtcraft:glass"_s);
        QcChunk keptBefore;
        QcChunk neighbourBefore;
        {
            auto world = server(dir.path(), 1);
            // The origin, where a world starts, is always explored.
            QVERIFY(world->world().loadChunk({0, 0}) != nullptr);
            keptBefore = loaded(world->world(), kept);
            QVERIFY(world->world().loadChunk(neighbour) != nullptr);
            QVERIFY(world->world().setBlock(glassAt, glass));
            neighbourBefore = *world->world().chunk(neighbour);
            QVERIFY(world->world().chunk(fresh) == nullptr);
            QVERIFY(world->save().has_value());
        }

        const QcResult<QcGeneratorSwitch::Switched> switched = QcGeneratorSwitch::switchDimension(dir.path(), overworldName, 2, m_content);
        QVERIFY2(switched.has_value(), qcErrorText(switched).constData());
        QCOMPARE(switched->from, 1);
        QCOMPARE(switched->to, 2);
        QVERIFY(switched->keptChunks >= 2);
        const QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(dir.path());
        QVERIFY(map.has_value());
        QCOMPARE(map->versionAt(kept), std::optional(1));
        QCOMPARE(map->versionAt(fresh), std::nullopt);
        QCOMPARE(QcWorldStorage(dir.path(), m_content).readInfo()->generatorVersion, 2);

        // The world is version 2 now and opens only as that.
        QVERIFY(!QcServer(config(dir.path(), 1), m_content, terrain(1)).openWorld().has_value());
        auto world = server(dir.path(), 2);
        const QcChunk keptAfter = loaded(world->world(), kept);
        QVERIFY(QcTestUtil::sameBlocks(keptAfter, keptBefore));
        QVERIFY(!QcTestUtil::sameBlocks(keptAfter, *terrain(2)->generate(kept)));
        QVERIFY(QcTestUtil::sameBlocks(loaded(world->world(), neighbour), neighbourBefore));
        QCOMPARE(world->world().blockAt(glassAt), std::optional(glass));
        const QcChunk freshAfter = loaded(world->world(), fresh);
        QVERIFY(QcTestUtil::sameBlocks(freshAfter, *terrain(2)->generate(fresh)));
        QVERIFY(!QcTestUtil::sameBlocks(freshAfter, *terrain(1)->generate(fresh)));

        // The spawn is version 1's, found on the terrain that stays.
        const QcBlockPos spawnBefore = terrain(1)->spawnPoint();
        qInfo() << "spawn of version 1 and 2 differ:" << (spawnBefore != terrain(2)->spawnPoint());
        QCOMPARE(world->world().generator().spawnPoint(), spawnBefore);
    }

    // A dimension switches on its own: its map lies in its folder, the overworld keeps its version
    // and has no map.
    void aDimensionSwitchesOnItsOwn()
    {
        QTemporaryDir dir;
        const QcChunkPos kept = m_differing.front();
        const QcChunkPos fresh = m_differing.back();
        {
            auto world = server(dir.path(), 2);
            QVERIFY(world->save().has_value());
        }
        // A world whose terrain dimension was made with version 1 (as one made before version 2).
        const QcWorldStorage storage(dir.path(), m_content);
        QcWorldInfo info = *storage.readInfo();
        QCOMPARE(info.dimensionGeneratorVersions.value(terrainName), 2);
        info.dimensionGeneratorVersions.insert(terrainName, 1);
        QVERIFY(storage.writeInfo(info).has_value());

        QcChunk keptBefore;
        {
            auto world = server(dir.path(), 2);
            QcResult<QcServerDimension *> dimension = world->openDimension(terrainName);
            QVERIFY2(dimension.has_value(), qcErrorText(dimension).constData());
            QCOMPARE((*dimension)->generatorVersion(), 1);
            keptBefore = loaded((*dimension)->world(), kept);
            QVERIFY(world->save().has_value());
        }

        const QcResult<QcGeneratorSwitch::Switched> switched = QcGeneratorSwitch::switchDimension(dir.path(), terrainName, 2, m_content);
        QVERIFY2(switched.has_value(), qcErrorText(switched).constData());
        QCOMPARE(switched->from, 1);
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(u"dimensions/test/terrain/"_s + QcVersionMapFile::fileName.toString())));
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QcVersionMapFile::fileName.toString())));
        const QcWorldInfo after = *storage.readInfo();
        QCOMPARE(after.generatorVersion, 2);
        QCOMPARE(after.dimensionGeneratorVersions.value(terrainName), 2);

        auto world = server(dir.path(), 2);
        const auto &overworld = dynamic_cast<const QcVersionedGenerator &>(world->world().generator());
        QVERIFY(overworld.versionMap().isEmpty());
        QcResult<QcServerDimension *> dimension = world->openDimension(terrainName);
        QVERIFY(dimension.has_value());
        QCOMPARE((*dimension)->generatorVersion(), 2);
        QVERIFY(QcTestUtil::sameBlocks(loaded((*dimension)->world(), kept), keptBefore));
        QVERIFY(QcTestUtil::sameBlocks(loaded((*dimension)->world(), fresh), *terrain(2)->generate(fresh)));
    }

    // A switch leaves the world as it is when it cannot be done, and says why.
    void aSwitchRefusesWhatItCannotDo()
    {
        QTemporaryDir dir;
        const QString info = QDir(dir.path()).filePath(QString::fromLatin1(QcWorldStorage::infoFileName));
        {
            auto world = testWorld(dir.path(), QString::fromLatin1(QcWorldGenerators::terrain));
            QVERIFY(refusal(dir.path(), overworldName, 1).contains(u"is open in QtCraft"_s));
        }
        const QByteArray before = fileBytes(info);
        QVERIFY(refusal(dir.path(), overworldName, QcGeneratorVersion::current).contains(u"already"_s));
        QVERIFY(refusal(dir.path(), overworldName, QcGeneratorVersion::current + 1).contains(u"cannot be generated as version"_s));
        QVERIFY(refusal(dir.path(), u"test:nowhere"_s, 1).contains(u"There is no dimension"_s));
        QVERIFY(refusal(QDir(dir.path()).filePath(u"none"_s), overworldName, 1).contains(u"There is no world"_s));
        QCOMPARE(fileBytes(info), before);
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QcVersionMapFile::fileName.toString())));

        QTemporaryDir showcaseDir;
        testWorld(showcaseDir.path(), QString::fromLatin1(QcWorldGenerators::showcase)).reset();
        QVERIFY(refusal(showcaseDir.path(), overworldName, 1).contains(u"showcase"_s));
    }

    // A damaged index of generated chunks refuses the switch, also once it was kept aside: the
    // area would miss the chunks only it knew.
    void aDamagedIndexRefusesTheSwitch()
    {
        QTemporaryDir dir;
        testWorld(dir.path(), QString::fromLatin1(QcWorldGenerators::terrain)).reset();
        const QString info = QDir(dir.path()).filePath(QString::fromLatin1(QcWorldStorage::infoFileName));
        const QByteArray before = fileBytes(info);
        putFile(QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString()), "QCGC damaged");
        QVERIFY(refusal(dir.path(), overworldName, 1).contains(u"damaged"_s));
        QVERIFY(refusal(dir.path(), overworldName, 1).contains(u"kept aside"_s));
        QCOMPARE(fileBytes(info), before);
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QcVersionMapFile::fileName.toString())));
    }

    // A map that is damaged, of a newer format or names a version this game does not generate
    // keeps the world closed, world.json untouched: without it the chunks generated before would
    // change.
    void aMapThatCannotBeFollowedKeepsTheWorldClosed()
    {
        QTemporaryDir dir;
        testWorld(dir.path(), QString::fromLatin1(QcWorldGenerators::terrain)).reset();
        const QString path = QDir(dir.path()).filePath(QcVersionMapFile::fileName.toString());
        const QString info = QDir(dir.path()).filePath(QString::fromLatin1(QcWorldStorage::infoFileName));
        const QByteArray before = fileBytes(info);
        QcGeneratorVersionMap map;
        QcChunkSet origin;
        origin.insert({0, 0});
        map.mark(origin, 1);
        QcGeneratorVersionMap unknown;
        unknown.mark(origin, QcGeneratorVersion::current + 1);

        const QList<QByteArray> files{"QCVM damaged", QcSaveFile::bytes(QcVersionMapFile::magic, map.bytes(), QcVersionMapFile::formatVersion + 1),
                                      QcSaveFile::bytes(QcVersionMapFile::magic, unknown.bytes(), QcVersionMapFile::formatVersion)};
        for (const QByteArray &bytes : files) {
            putFile(path, bytes);
            QcServer world(config(dir.path(), QcGeneratorVersion::current), m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
            const QcStatus opened = world.openWorld();
            QVERIFY(!opened.has_value());
            QVERIFY2(opened.error().contains(u"generatorversions.qcv"_s) || opened.error().contains(u"version %1"_s.arg(QcGeneratorVersion::current + 1)),
                     qPrintable(opened.error()));
        }
        QCOMPARE(fileBytes(info), before);
    }

    // A dimension with a damaged map stays closed; the world plays on.
    void aDamagedDimensionMapKeepsTheDimensionClosed()
    {
        QTemporaryDir dir;
        testWorld(dir.path(), QString::fromLatin1(QcWorldGenerators::terrain)).reset();
        putFile(QDir(dir.path()).filePath(u"dimensions/test/terrain/"_s + QcVersionMapFile::fileName.toString()), "QCVM damaged");
        auto world = testWorld(dir.path(), QString::fromLatin1(QcWorldGenerators::terrain));
        const QcResult<QcServerDimension *> dimension = world->openDimension(terrainName);
        QVERIFY(!dimension.has_value());
        QVERIFY2(dimension.error().contains(QcVersionMapFile::fileName.toString()), qPrintable(dimension.error()));
        QVERIFY(world->world().loadChunk({0, 0}) != nullptr);
    }
};

QTEST_GUILESS_MAIN(TestGeneratorSwitch)
#include "test_generator_switch.moc"
