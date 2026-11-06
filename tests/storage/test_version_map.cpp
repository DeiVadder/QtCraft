// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcWorldConstants.h"
#include "common/maps/QcMapData.h"
#include "common/maps/QcMapPixel.h"
#include "common/world/QcChunkSet.h"
#include "common/world/QcGeneratorVersionMap.h"
#include "fixtures/QcTestTerrainServer.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerConfig.h"
#include "server/maps/QcMapSaveCodec.h"
#include "server/storage/QcGeneratedChunks.h"
#include "server/storage/QcSaveFile.h"
#include "server/versions/QcExploredArea.h"
#include "server/versions/QcVersionMapFile.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/generators/QcVersionedGenerator.h"
#include "worldgen/structures/QcStructure.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QcChunkSet setOf(const QList<QcChunkPos> &chunks)
{
    QcChunkSet set;
    for (const QcChunkPos &chunk : chunks) {
        set.insert(chunk);
    }
    return set;
}

// A generator whose chunks carry its version as the block at the bottom corner, and whose only
// village lies at `village`.
class TaggedGenerator : public QcWorldGenerator
{
public:
    TaggedGenerator(int version, QcBlockPos village)
        : m_village(village)
    {
        setVersion(version);
    }

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override
    {
        auto chunk = std::make_shared<QcChunk>(pos);
        chunk->setBlock(0, 0, 0, static_cast<QcBlockId>(version()));
        return chunk;
    }
    QcBlockPos spawnPoint() const override { return {}; }
    QString biomeAt(int x, int z) const override
    {
        Q_UNUSED(x);
        Q_UNUSED(z);
        return u"v%1"_s.arg(version());
    }
    std::shared_ptr<const QcVillagePlan> nearestVillage(QcBlockPos from, int reach) const override
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        auto plan = std::make_shared<QcVillagePlan>();
        plan->centre = m_village;
        return plan;
    }

private:
    QcBlockPos m_village;
};

// A generator of version `version` with a one-column structure at each of `sites` that, like the
// real generators (decision E-1013), builds and finds only the structures its filter lets it
// build. `searched` is how far its locate() says it looked; `planned` whether its plans are made.
class SitedGenerator : public QcWorldGenerator
{
public:
    SitedGenerator(int version, QList<QcBlockPos> sites, std::shared_ptr<const QcContentRegistry> content)
        : m_sites(std::move(sites))
        , m_content(std::move(content))
    {
        setVersion(version);
    }

    void setSearched(int searched) { m_searched = searched; }
    void setPlanned(bool planned) { m_planned = planned; }

    std::shared_ptr<QcChunk> generate(QcChunkPos pos) const override { return std::make_shared<QcChunk>(pos); }
    QcBlockPos spawnPoint() const override { return {}; }
    QcStructureSites structureSitesNear(QcBlockPos from, int reach) const override
    {
        QcStructureSites sites;
        const QList<QcBlockPos> built = builtWithin(from, reach);
        for (const QcBlockPos site : built) {
            sites.guards.append(QcGuardSite{0, site, site, {}});
            sites.spawnAreas.append(QcSpawnArea{site, site, {}, {}});
        }
        return sites;
    }
    std::optional<QcStructureSites> plannedStructureSitesNear(QcBlockPos from, int reach) const override
    {
        return m_planned ? std::optional(structureSitesNear(from, reach)) : std::nullopt;
    }
    std::optional<QcStructurePlacement> nearestStructure(const QString &name, QcBlockPos from, int reach) const override
    {
        Q_UNUSED(name);
        const std::optional<QcBlockPos> site = nearest(from, reach);
        if (!site) {
            return std::nullopt;
        }
        return QcStructurePlacement(std::make_shared<const QcStructure>(u"hut"_s, 1, 1, 1), *site, 0, *m_content);
    }
    QcLocateResult locate(QStringView kind, QcBlockPos from, int reach) const override
    {
        Q_UNUSED(kind);
        const std::optional<QcBlockPos> site = nearest(from, reach);
        return {site ? std::optional(QcLocatedPlace{*site, std::nullopt}) : std::nullopt, m_searched};
    }
    QList<QcXzRect> structuresTouching(QcChunkPos pos) const override
    {
        QList<QcXzRect> touching;
        for (const QcBlockPos site : m_sites) {
            if (site.chunk() == pos && buildsStructure(columnOf(site))) {
                touching.append(columnOf(site));
            }
        }
        return touching;
    }

private:
    static QcXzRect columnOf(QcBlockPos site) { return {site.x, site.z, site.x, site.z}; }

    QList<QcBlockPos> builtWithin(QcBlockPos from, int reach) const
    {
        QList<QcBlockPos> built;
        std::ranges::copy_if(m_sites, std::back_inserter(built), [&](QcBlockPos site) {
            return QcPlaceSearch::distance(from, site) <= reach && buildsStructure(columnOf(site));
        });
        return built;
    }

    std::optional<QcBlockPos> nearest(QcBlockPos from, int reach) const
    {
        const QList<QcBlockPos> built = builtWithin(from, reach);
        const auto it = std::ranges::min_element(built, {}, [from](QcBlockPos site) { return QcPlaceSearch::distance(from, site); });
        return it != built.end() ? std::optional(*it) : std::nullopt;
    }

    QList<QcBlockPos> m_sites;
    std::shared_ptr<const QcContentRegistry> m_content;
    int m_searched = 1000;
    bool m_planned = true;
};

// The version tag of the chunk `pos` as `generator` makes it.
int tagOf(const QcWorldGenerator &generator, QcChunkPos pos)
{
    return static_cast<int>(generator.generate(pos)->block(0, 0, 0));
}

// Puts `content` into the file `path`, its folder made first.
void putFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    const bool written = file.open(QIODevice::WriteOnly) && file.write(content) == content.size() && file.commit();
    if (!written) {
        QcTestUtil::failCase(u"Cannot write %1"_s.arg(path));
    }
}

} // namespace

// The generator version map of a dimension (package 87.1, decisions E-1010 to E-1012): the chunk
// sets it is made of, its file, the generator that follows it and the explored area a switch keeps.
class TestVersionMap : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    std::unique_ptr<QcServer> server(const QString &directory) const
    {
        return QcTestUtil::openTestTerrainWorld(directory, m_content, u"Mapped"_s, 11);
    }

private slots:
    // Sets of chunks on both sides of region borders and of the origin: membership, size,
    // difference, union and their bytes.
    void aChunkSetKeepsItsChunks()
    {
        const QList<QcChunkPos> chunks{{0, 0}, {31, 31}, {32, 0}, {-1, -1}, {-33, 5}, {1000, -1000}};
        QcChunkSet set = setOf(chunks);
        QVERIFY(!set.insert({0, 0}));
        QCOMPARE(set.size(), chunks.size());
        QCOMPARE(set.regionCount(), qsizetype{5});
        QVERIFY(set.contains({-33, 5}));
        QVERIFY(!set.contains({-34, 5}));
        QVERIFY(set.hasRegionWithin({-2, -1}, {-2, 1}));
        QVERIFY(!set.hasRegionWithin({5, 5}, {6, 6}));

        const QcChunkSet some = setOf({{0, 0}, {-1, -1}, {7, 7}});
        QCOMPARE(set.without(some).chunks(), (QSet<QcChunkPos>{{31, 31}, {32, 0}, {-33, 5}, {1000, -1000}}));
        QVERIFY(set.intersects(some));
        QVERIFY(!set.without(some).intersects(some));
        QcChunkSet both = set;
        both.unite(some);
        QCOMPARE(both.size(), chunks.size() + 1);

        const QcResult<QcChunkSet> read = QcChunkSet::parse(set.bytes());
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(*read, set);
        QVERIFY(!QcChunkSet::parse(set.bytes().chopped(1)).has_value());
        QVERIFY(!QcChunkSet::parse(QByteArray(set.bytes() + "x")).has_value());
        // The westmost region reaches a chunk column beyond the world: a bit there is damage.
        const auto edgeRegion = [](int slot) {
            QByteArray bytes;
            QDataStream stream(&bytes, QIODevice::WriteOnly);
            stream.setVersion(QcChunkSet::streamVersion);
            QByteArray bitmap(QcChunkSet::bitmapBytes, '\0');
            bitmap[slot / 8] = static_cast<char>(1 << (slot % 8));
            stream << quint32{1} << static_cast<qint32>(QcChunkSet::regionOf({-QcWorld::maxHorizontalChunk, 0}).x) << qint32{0};
            stream.writeRawData(bitmap.constData(), QcChunkSet::bitmapBytes);
            return bytes;
        };
        QVERIFY(!QcChunkSet::parse(edgeRegion(0)).has_value());
        QVERIFY(QcChunkSet::parse(edgeRegion(1)).has_value());
        // Emptied regions are not kept, so equal sets compare equal.
        QCOMPARE(setOf({{3, 3}}).without(setOf({{3, 3}})), QcChunkSet{});
    }

    // A chunk keeps the first version it was given; the bytes round-trip, and damaged bytes (a
    // chunk named twice, versions out of order or range) are refused.
    void theVersionMapNamesEachChunkOnce()
    {
        QcGeneratorVersionMap map;
        QCOMPARE(map.mark(setOf({{0, 0}, {1, 0}}), 1), qsizetype{2});
        QCOMPARE(map.mark(setOf({{1, 0}, {2, 0}}), 2), qsizetype{1});
        QCOMPARE(map.mark(setOf({{2, 0}}), 3), qsizetype{0});
        QCOMPARE(map.versionAt({1, 0}), std::optional(1));
        QCOMPARE(map.versionAt({2, 0}), std::optional(2));
        QCOMPARE(map.versionOr({5, 5}, 4), 4);
        QCOMPARE(map.versions(), (QList<int>{1, 2}));

        const QcResult<QcGeneratorVersionMap> read = QcGeneratorVersionMap::parse(map.bytes());
        QVERIFY2(read.has_value(), qcErrorText(read).constData());
        QCOMPARE(*read, map);

        const auto forged = [](const QList<std::pair<quint16, QcChunkSet>> &entries) {
            QByteArray bytes;
            QDataStream stream(&bytes, QIODevice::WriteOnly);
            stream.setVersion(QcSaveFile::streamVersion);
            stream << static_cast<quint32>(entries.size());
            for (const auto &[version, chunks] : entries) {
                const QByteArray set = chunks.bytes();
                stream << version << static_cast<quint32>(set.size());
                stream.writeRawData(set.constData(), static_cast<int>(set.size()));
            }
            return bytes;
        };
        QVERIFY(QcGeneratorVersionMap::parse(forged({{1, setOf({{0, 0}})}, {2, setOf({{1, 1}})}})).has_value());
        QVERIFY(!QcGeneratorVersionMap::parse(forged({{1, setOf({{0, 0}})}, {2, setOf({{0, 0}})}})).has_value());
        QVERIFY(!QcGeneratorVersionMap::parse(forged({{2, setOf({{0, 0}})}, {1, setOf({{1, 1}})}})).has_value());
        QVERIFY(!QcGeneratorVersionMap::parse(forged({{0, setOf({{0, 0}})}})).has_value());
        QVERIFY(!QcGeneratorVersionMap::parse(forged({{1, QcChunkSet{}}})).has_value());
        QVERIFY(!QcGeneratorVersionMap::parse(map.bytes().chopped(3)).has_value());
    }

    // The file: none is an empty map, an empty map removes it, a damaged file or a newer format
    // is an error (the world must not open with chunks of the wrong version).
    void theFileKeepsTheMap()
    {
        QTemporaryDir dir;
        QcGeneratorVersionMap map;
        map.mark(setOf({{-5, 3}, {40, -2}}), 1);
        QVERIFY(QcVersionMapFile::read(dir.path())->isEmpty());
        QVERIFY(QcVersionMapFile::write(dir.path(), map).has_value());
        QCOMPARE(*QcVersionMapFile::read(dir.path()), map);

        const QString path = QDir(dir.path()).filePath(QcVersionMapFile::fileName.toString());
        putFile(path, QcSaveFile::bytes(QcVersionMapFile::magic, map.bytes(), QcVersionMapFile::formatVersion + 1));
        const QcResult<QcGeneratorVersionMap> newer = QcVersionMapFile::read(dir.path());
        QVERIFY(!newer.has_value());
        QVERIFY2(newer.error().contains(u"newer"_s), qPrintable(newer.error()));
        putFile(path, QcSaveFile::bytes(QcVersionMapFile::magic, map.bytes().chopped(1), QcVersionMapFile::formatVersion));
        QVERIFY(!QcVersionMapFile::read(dir.path()).has_value());
        QVERIFY(QcVersionMapFile::write(dir.path(), map).has_value());
        QCOMPARE(*QcVersionMapFile::read(dir.path()), map);

        QVERIFY(QcVersionMapFile::write(dir.path(), {}).has_value());
        QVERIFY(!QFile::exists(path));
    }

    // Each chunk and column goes to the generator of its version; searches keep only what a
    // generator finds in its own chunks; the map is set only before anything is generated.
    void theGeneratorFollowsTheMap()
    {
        // The current version's village lies in the old area, the old version's in it too.
        QcVersionedGenerator generator(std::make_unique<TaggedGenerator>(3, QcBlockPos{8, 64, 8}));
        QCOMPARE(tagOf(generator, {0, 0}), 3);

        QcVersionedGenerator mapped(std::make_unique<TaggedGenerator>(3, QcBlockPos{8, 64, 8}));
        QcGeneratorVersionMap map;
        map.mark(setOf({{0, 0}, {1, 0}}), 1);
        map.mark(setOf({{-1, 0}}), 3);
        const auto create = [](int version) -> std::unique_ptr<QcWorldGenerator> {
            return version == 1 ? std::make_unique<TaggedGenerator>(1, QcBlockPos{20, 64, 4}) : nullptr;
        };
        QVERIFY(mapped.setVersionMap(map, create).has_value());
        QCOMPARE(mapped.version(), 3);
        QCOMPARE(tagOf(mapped, {0, 0}), 1);
        QCOMPARE(tagOf(mapped, {1, 0}), 1);
        QCOMPARE(tagOf(mapped, {-1, 0}), 3);
        QCOMPARE(tagOf(mapped, {2, 0}), 3);
        QCOMPARE(mapped.biomeAt(17, 3), u"v1"_s);
        QCOMPARE(mapped.biomeAt(-1, 3), u"v3"_s);

        // The current version's village at (8, 8) lies in an old chunk: only the old one counts.
        const std::shared_ptr<const QcVillagePlan> village = mapped.nearestVillage({0, 64, 0}, 512);
        QVERIFY(village != nullptr);
        QCOMPARE(village->centre, (QcBlockPos{20, 64, 4}));

        QVERIFY(!mapped.setVersionMap(map, create).has_value());
        QcVersionedGenerator unknown(std::make_unique<TaggedGenerator>(3, QcBlockPos{}));
        QcGeneratorVersionMap far;
        far.mark(setOf({{9, 9}}), 2);
        QVERIFY(!unknown.setVersionMap(far, create).has_value());
        QCOMPARE(tagOf(unknown, {9, 9}), 3);
    }

    // The searches through a map ask every version near the place and keep the nearest structure
    // a version builds in its own chunks. A version skips its structures that reach into chunks of
    // another (decision E-1013), so a nearer one in old chunks never hides a farther valid one.
    void theSearchesFollowTheMap()
    {
        // Version 3 has a structure at (8, 8) in the old chunk 0, 0 and one at (40, 8) in its own
        // chunk 2, 0; version 1 one at (-200, 0), outside its chunks, and one asked for below.
        const QcBlockPos from{0, 64, 0};
        QcGeneratorVersionMap map;
        map.mark(setOf({{0, 0}, {1, 0}}), 1);
        const auto sited = [this](int version, QList<QcBlockPos> sites) { return std::make_unique<SitedGenerator>(version, std::move(sites), m_content); };
        const auto mapped = [&](const QList<QcBlockPos> &oldSites, int oldSearched) {
            auto generator = std::make_unique<QcVersionedGenerator>(sited(3, {{8, 64, 8}, {40, 64, 8}}));
            const auto create = [&](int version) -> std::unique_ptr<QcWorldGenerator> {
                auto old = sited(version, oldSites);
                old->setSearched(oldSearched);
                return old;
            };
            if (!generator->setVersionMap(map, create)) {
                QcTestUtil::failCase(u"The map is refused"_s);
            }
            return generator;
        };

        const std::unique_ptr<QcVersionedGenerator> farther = mapped({{-200, 64, 0}}, 300);
        QCOMPARE(farther->nearestStructure(u"hut"_s, from, 512)->origin(), (QcBlockPos{40, 64, 8}));
        const QcLocateResult located = farther->locate(u"dungeon", from, 512);
        QCOMPARE(located.place->at, (QcBlockPos{40, 64, 8}));
        QCOMPARE(located.searched, 300);
        const QcStructureSites sites = farther->structureSitesNear(from, 512);
        QCOMPARE(sites.guards.size(), qsizetype{1});
        QCOMPARE(sites.guards.front().min, (QcBlockPos{40, 64, 8}));
        QCOMPARE(sites.spawnAreas.size(), qsizetype{1});
        QCOMPARE(farther->plannedStructureSitesNear(from, 512)->guards.size(), qsizetype{1});
        QVERIFY(farther->structuresTouching({0, 0}).isEmpty());
        QCOMPARE(farther->structuresTouching({2, 0}), (QList<QcXzRect>{{40, 8, 40, 8}}));

        // Version 1's own structure at (20, 4) in chunk 1, 0 is nearer than version 3's.
        const std::unique_ptr<QcVersionedGenerator> older = mapped({{20, 64, 4}, {40, 64, 9}}, 1000);
        QCOMPARE(older->nearestStructure(u"hut"_s, from, 512)->origin(), (QcBlockPos{20, 64, 4}));
        QCOMPARE(older->locate(u"dungeon", from, 512).place->at, (QcBlockPos{20, 64, 4}));
        QCOMPARE(older->structureSitesNear(from, 512).guards.size(), qsizetype{2});
        QCOMPARE(older->structuresTouching({1, 0}), (QList<QcXzRect>{{20, 4, 20, 4}}));
        // Out of reach of everything.
        QVERIFY(!older->nearestStructure(u"hut"_s, {5000, 64, 5000}, 64).has_value());
        QVERIFY(!older->locate(u"dungeon", {5000, 64, 5000}, 64).place.has_value());
    }

    // A version whose plans near the place are not made yet makes the planned sites unknown.
    void unplannedSitesAreUnknown()
    {
        QcVersionedGenerator generator(std::make_unique<SitedGenerator>(3, QList<QcBlockPos>{}, m_content));
        QcGeneratorVersionMap map;
        map.mark(setOf({{0, 0}}), 1);
        const auto create = [this](int version) -> std::unique_ptr<QcWorldGenerator> {
            auto old = std::make_unique<SitedGenerator>(version, QList<QcBlockPos>{}, m_content);
            old->setPlanned(false);
            return old;
        };
        QVERIFY(generator.setVersionMap(map, create).has_value());
        QVERIFY(!generator.plannedStructureSitesNear({0, 64, 0}, 64).has_value());
        QVERIFY(generator.structureSitesNear({0, 64, 0}, 64).guards.isEmpty());
    }

    // The area of a world: the index of generated chunks, every chunk with stored data and every
    // chunk under a revealed pixel of a map. Without the index (a world from 0.1.0) the rest
    // still counts.
    void theExploredAreaUnitesWhatTheWorldKnows()
    {
        QTemporaryDir dir;
        QSet<QcChunkPos> loaded;
        const QcBlockPos changed{-40, 70, 9};
        {
            auto world = server(dir.path());
            for (int x = -3; x <= 2; ++x) {
                world->world().loadChunk({x, 4});
            }
            QVERIFY(world->world().loadChunk(changed.chunk()) != nullptr);
            QVERIFY(world->world().setBlock(changed, *m_content->blockId(u"qtcraft:glass"_s)));
            QVERIFY(world->save().has_value());
            const QList<QcChunkPos> chunks = world->world().loadedChunks();
            loaded = {chunks.cbegin(), chunks.cend()};
        }
        const QcDimensionDefinition &overworld = m_content->worldgen().overworld();
        QcResult<QcExploredArea::Explored> withIndex = QcExploredArea::of(dir.path(), overworld, *m_content);
        QVERIFY2(withIndex.has_value(), qcErrorText(withIndex).constData());
        QCOMPARE(withIndex->chunks.chunks(), loaded);

        // A map at scale 4 around (1000, 1000) with one revealed pixel of 16 x 16 columns: one
        // chunk (the map's squares are shifted by half a scale-0 map, 64 columns, whole chunks).
        QcMapData map = QcMapData::around(1000, 1000, 4);
        const QPoint pixel = *map.pixelAt(1001, 1001);
        QVERIFY(map.set(pixel, QcMapPixel::make(1, QcMapPixel::Shade::Normal)));
        const QPoint first = map.columnOf(pixel);
        QSet<QcChunkPos> underPixel;
        for (int x = first.x(); x < first.x() + 16; ++x) {
            for (int z = first.y(); z < first.y() + 16; ++z) {
                underPixel.insert(QcBlockPos{x, 0, z}.chunk());
            }
        }
        putFile(QDir(dir.path()).filePath(QcMapSaveCodec::fileName(1)), QcMapSaveCodec::write(map, *m_content));
        putFile(QDir(dir.path()).filePath(QcMapSaveCodec::fileName(2)), "not a map");

        QVERIFY(QFile::remove(QDir(dir.path()).filePath(QcGeneratedChunks::fileName.toString())));
        QcResult<QcExploredArea::Explored> without = QcExploredArea::of(dir.path(), overworld, *m_content);
        QVERIFY2(without.has_value(), qcErrorText(without).constData());
        QCOMPARE(without->warnings.size(), qsizetype{1});
        const QSet<QcChunkPos> area = without->chunks.chunks();
        QVERIFY(area.contains(changed.chunk()));
        QVERIFY(area.contains(underPixel));
        QVERIFY(!area.contains({200, 200}));
        QCOMPARE(QcExploredArea::ofMaps(dir.path(), *m_content).chunks.chunks(), underPixel);
        QCOMPARE(underPixel.size(), qsizetype{1});
    }
};

QTEST_GUILESS_MAIN(TestVersionMap)
#include "test_version_map.moc"
