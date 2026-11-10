// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/core/QcWorldConstants.h"
#include "common/world/QcChunkSet.h"
#include "common/world/QcGeneratorVersion.h"
#include "common/world/QcGeneratorVersionMap.h"
#include "common/world/QcWorldInfo.h"
#include "fixtures/QcDirectoryHashes.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerConfig.h"
#include "server/storage/QcWorldStorage.h"
#include "server/versions/QcVersionMapFile.h"
#include "worldgen/generators/QcVersionedGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/towers/QcCoastalTowerGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"
#include "worlds/QcGeneratorUpgrade.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

// The newest frozen version older than the current one: a world of it has an upgrade on offer.
constexpr int olderFrozen = std::min(QcGeneratorVersion::lastFrozen, QcGeneratorVersion::current - 1);
// The newest frozen version without loot in village chests (it came with version 3, decision
// E-1015): the chests of its explored chunks stay empty after a switch.
constexpr int withoutVillageLoot = 2;

// The seed of the generator tests (test_generator_switch, "terrain 7" in
// tests/data/generator-versions.json): versions 1 and 2 are frozen and differ there.
constexpr qint64 seed = 7;
const QString overworldName = QString::fromLatin1(QcDimensionDefinition::overworldName);

// The chunks the columns of `rect` lie in.
QList<QcChunkPos> chunksOf(QcXzRect rect)
{
    QList<QcChunkPos> chunks;
    const QcChunkPos low = QcBlockPos{rect.minX, 0, rect.minZ}.chunk();
    const QcChunkPos high = QcBlockPos{rect.maxX, 0, rect.maxZ}.chunk();
    for (int z = low.z; z <= high.z; ++z) {
        for (int x = low.x; x <= high.x; ++x) {
            chunks.append({x, z});
        }
    }
    return chunks;
}

// The stages a conversion reported, in their order, each once.
struct Recorder
{
    QList<QcConversionControl::Stage> stages;

    void note(const QcConversionControl::Step &step)
    {
        if (stages.isEmpty() || stages.constLast() != step.stage) {
            stages.append(step.stage);
        }
    }
};

// What the mine shafts and coastal towers of `terrain` plan around the chunks of `area`: each plan's
// bounds (a tower's footprint), for comparing plans of two generators.
QList<QcXzRect> plannedAround(const QcTerrainGenerator &terrain, QcXzRect area)
{
    QList<QcXzRect> plans;
    const auto chunks = chunksOf(area);
    for (const QcChunkPos chunk : chunks) {
        const auto mineshafts = terrain.mineshafts().plansTouching(chunk);
        for (const auto &plan : mineshafts) {
            plans.append(plan->bounds());
        }
        const auto towers = terrain.coastalTowers().plansTouching(chunk);
        for (const auto &plan : towers) {
            plans.append(plan->footprint());
        }
    }
    return plans;
}

} // namespace

// Switching a saved world to a newer world generator before it is played (package 87.2,
// decisions E-1013, E-1014): the offer, the backup, cancelling, structures across the border of
// the explored area, and qtcraft-headless --convert-generator. The worlds are terrain worlds of
// version 1 (frozen) switched to version 2.
class TestGeneratorUpgrade : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // A chunk near the origin into which a structure of version 1 reaches over more than one
    // chunk, and that structure's reach.
    QcChunkPos m_started;
    QcXzRect m_structure;

    std::unique_ptr<QcWorldGenerator> terrain(int version) const
    {
        return QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::terrain), m_content, seed, {}, version);
    }

    // The saved terrain world in `directory`, open as `version` (made as it when new).
    std::unique_ptr<QcServer> open(const QString &directory, int version) const
    {
        QcServerConfig config;
        config.worldName = u"Upgraded"_s;
        config.seed = seed;
        config.worldDirectory = directory;
        config.keepChunksLoaded = true;
        config.generatorVersion = version;
        auto server = std::make_unique<QcServer>(config, m_content, terrain(version));
        if (const QcStatus opened = server->openWorld(); !opened) {
            QcTestUtil::failCase(u"The world does not open as version %1: %2"_s.arg(version).arg(opened.error()));
        }
        return server;
    }

    // A world of version 1 in `directory` with the chunk `explored` generated, saved and closed;
    // returns the blocks of that chunk.
    QcChunk exploredWorld(const QString &directory, QcChunkPos explored) const
    {
        auto world = open(directory, 1);
        const std::shared_ptr<const QcChunk> chunk = world->world().loadChunk(explored);
        if (!chunk || !world->save()) {
            QcTestUtil::failCase(u"The world of version 1 does not generate or save"_s);
            return QcChunk(explored);
        }
        return *chunk;
    }

    // A chest of the first and of the last village with loot in the regions around the origin.
    static QList<QcBlockPos> villageChests(const QcTerrainGenerator &generator)
    {
        QList<QcBlockPos> chests;
        constexpr int rings = 6;
        for (int rz = -rings; rz <= rings; ++rz) {
            for (int rx = -rings; rx <= rings; ++rx) {
                const std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz);
                if (!plan || generator.villageFor(QcBlockPos{plan->centre.x, 0, plan->centre.z}.chunk()) != plan) {
                    continue;
                }
                const auto house = std::ranges::find_if(plan->pieces, [](const QcStructurePlacement &piece) {
                    return !piece.structure().loot.isEmpty() && piece.structure().loot.front().table.startsWith(u"qtcraft:village_"_s);
                });
                if (house != plan->pieces.end()) {
                    chests.resize(std::min(chests.size(), qsizetype{1}));
                    chests.append(house->toWorld(house->structure().loot.front().at));
                }
            }
        }
        return chests;
    }

    bool isChest(const QcChunk &chunk, QcBlockPos at) const
    {
        return m_content->block(chunk.block(at.localX(), at.y, at.localZ())).name == u"qtcraft:chest"_s;
    }

    // A village and a chest of it, by the plan of a generator.
    struct RingVillage
    {
        QcXzRect bounds;
        QcBlockPos chest;
    };

    // The narrowest village with a house chest near the origin, if one fits a ring of the widest
    // kind from a chunk three chunks east of it.
    static std::optional<RingVillage> smallVillage(const QcTerrainGenerator &generator)
    {
        std::optional<RingVillage> best;
        constexpr int rings = 6;
        constexpr int limit = (QcBlendWidth::maxChunks - 5) * QcWorld::chunkSize;
        for (int rz = -rings; rz <= rings; ++rz) {
            for (int rx = -rings; rx <= rings; ++rx) {
                const std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz);
                if (!plan || generator.villageFor(QcBlockPos{plan->centre.x, 0, plan->centre.z}.chunk()) != plan) {
                    continue;
                }
                const QcXzRect bounds = plan->bounds();
                const int size = std::max(bounds.maxX - bounds.minX, bounds.maxZ - bounds.minZ);
                const auto house = std::ranges::find_if(plan->pieces, [](const QcStructurePlacement &piece) {
                    return !piece.structure().loot.isEmpty() && piece.structure().loot.front().table.startsWith(u"qtcraft:village_"_s);
                });
                if (size <= limit && house != plan->pieces.end() && (!best || size < best->bounds.maxX - best->bounds.minX)) {
                    best = RingVillage{bounds, house->toWorld(house->structure().loot.front().at)};
                }
            }
        }
        return best;
    }

    // The conversion of the world in `directory` from version 1 to the current one, its backup below `backups`.
    static QcGeneratorUpgrade::Request request(const QString &directory, const QString &backups)
    {
        return {directory, {{overworldName, 1, QcGeneratorVersion::current}}, QcGeneratorUpgrade::Mode::Hard, QcBlendWidth::standardChunks, backups};
    }

private slots:
    void initTestCase()
    {
        // The nearest chunk on a spiral into which a structure reaches over several chunks.
        const std::unique_ptr<QcWorldGenerator> one = terrain(1);
        for (int ring = 0; ring < 48 && m_structure.maxX == m_structure.minX; ++ring) {
            for (int dz = -ring; dz <= ring; ++dz) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    const QList<QcXzRect> reaches = std::max(std::abs(dx), std::abs(dz)) == ring ? one->structuresTouching({dx, dz}) : QList<QcXzRect>{};
                    const auto wide = std::ranges::find_if(reaches, [](QcXzRect reach) { return chunksOf(reach).size() >= 4; });
                    if (m_structure.maxX == m_structure.minX && wide != reaches.end()) {
                        m_started = {dx, dz};
                        m_structure = *wide;
                    }
                }
            }
        }
        QVERIFY(chunksOf(m_structure).size() >= 4);
        qInfo() << "Structure from" << m_structure.minX << m_structure.minZ << "to" << m_structure.maxX << m_structure.maxZ << "started in chunk"
                << m_started.x << m_started.z;
    }

    // The overworld of a world older than the game's generator is offered, nothing else.
    void onlyAnotherDimensionGetsAHardBorderWarning()
    {
        const QcGeneratorUpgrade::Change overworld{overworldName, 1, 2};
        const QcGeneratorUpgrade::Change nether{QStringLiteral("qtcraft:nether"), 1, 2};
        QVERIFY(!QcGeneratorUpgrade::reachBeyondOverworld({}));
        QVERIFY(!QcGeneratorUpgrade::reachBeyondOverworld({overworld}));
        QVERIFY(QcGeneratorUpgrade::reachBeyondOverworld({nether}));
        QVERIFY(QcGeneratorUpgrade::reachBeyondOverworld({overworld, nether}));
    }

    void theOfferNamesOutdatedDimensions()
    {
        QcWorldInfo info;
        info.generator = QString::fromLatin1(QcWorldGenerators::terrain);
        info.generatorVersion = 1;
        const QList<QcGeneratorUpgrade::Change> changes = QcGeneratorUpgrade::outdatedIn(info, *m_content);
        QCOMPARE(changes.size(), qsizetype{1});
        QCOMPARE(changes.front().dimension, overworldName);
        QCOMPARE(changes.front().from, 1);
        QCOMPARE(changes.front().to, QcGeneratorVersion::current);

        // A world of the newest frozen version before the current one gets the same offer.
        info.generatorVersion = olderFrozen;
        const QList<QcGeneratorUpgrade::Change> frozen = QcGeneratorUpgrade::outdatedIn(info, *m_content);
        QCOMPARE(frozen.constFirst().from, olderFrozen);
        QCOMPARE(frozen.constFirst().to, QcGeneratorVersion::current);

        info.generatorVersion = QcGeneratorVersion::current;
        QVERIFY(QcGeneratorUpgrade::outdatedIn(info, *m_content).isEmpty());
        // A showcase world always generates with the newest code.
        info.generator = QString::fromLatin1(QcWorldGenerators::showcase);
        info.generatorVersion = 1;
        QVERIFY(QcGeneratorUpgrade::outdatedIn(info, *m_content).isEmpty());
    }

    // A world of a frozen version takes the same way to the current one, hard and with a ring: the
    // explored chunk stays as it was and its chests stay empty, a village beyond the old area and
    // the ring has its loot (decision E-1015).
    void aWorldOfTheFrozenVersionSwitches()
    {
        constexpr int frozen = withoutVillageLoot;
        const std::unique_ptr<QcWorldGenerator> current = terrain(QcGeneratorVersion::current);
        const QList<QcBlockPos> chests = villageChests(dynamic_cast<const QcTerrainGenerator &>(*current));
        QVERIFY(chests.size() >= 2);
        const QcChunkPos explored = chests.front().chunk();
        const QcChunkPos beyond = chests.back().chunk();
        QVERIFY(std::abs(explored.x - beyond.x) + std::abs(explored.z - beyond.z) > 16);

        QHash<QcGeneratorUpgrade::Mode, qsizetype> kept;
        for (const QcGeneratorUpgrade::Mode mode : {QcGeneratorUpgrade::Mode::Hard, QcGeneratorUpgrade::Mode::Blend}) {
            QTemporaryDir dir;
            QTemporaryDir backups;
            const QcChunk before = [&] {
                auto world = open(dir.path(), frozen);
                const QcChunk chunk = *world->world().loadChunk(explored);
                if (!world->save()) {
                    QcTestUtil::failCase(u"The world of the frozen version does not save"_s);
                }
                return chunk;
            }();

            const QList<QcGeneratorUpgrade::Change> offer = QcGeneratorUpgrade::outdatedIn(*QcWorldStorage(dir.path(), m_content).readInfo(), *m_content);
            QCOMPARE(offer.size(), qsizetype{1});
            QCOMPARE(offer.front().from, frozen);
            QCOMPARE(offer.front().to, QcGeneratorVersion::current);
            const QcResult<QcGeneratorUpgrade::Converted> converted =
                QcGeneratorUpgrade::convert({dir.path(), offer, mode, QcBlendWidth::standardChunks, backups.path()}, m_content, {});
            QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
            QCOMPARE(QcWorldStorage(dir.path(), m_content).readInfo()->generatorVersion, QcGeneratorVersion::current);

            const QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(dir.path());
            QVERIFY(map.has_value());
            QCOMPARE(map->versionAt(explored), std::optional(frozen));
            QVERIFY(!map->versionAt(beyond).has_value());
            kept.insert(mode, map->chunksOf(frozen).size());

            auto world = open(dir.path(), QcGeneratorVersion::current);
            QVERIFY(QcTestUtil::sameBlocks(*world->world().loadChunk(explored), before));
            const QcWorldGenerator &generator = world->world().generator();
            QVERIFY(isChest(before, chests.front()));
            QVERIFY(generator.villageFor(explored));
            const auto isVillageTable = [](const QcChunkFurnishing::Chest &chest) { return chest.lootTable.startsWith(u"qtcraft:village_"_s); };
            QVERIFY(std::ranges::none_of(generator.furnishingIn(explored).chests, isVillageTable));
            const QcChunkFurnishing fresh = generator.furnishingIn(beyond);
            QVERIFY(std::ranges::any_of(fresh.chests, [&](const QcChunkFurnishing::Chest &chest) { return chest.at == chests.back() && isVillageTable(chest); }));
        }
        QVERIFY(kept[QcGeneratorUpgrade::Mode::Blend] > kept[QcGeneratorUpgrade::Mode::Hard]);
    }

    // A village nobody has seen, wholly in the ring around an explored chunk beside it, is built and
    // furnished by the old version (decision E-1040): its chest stands, without loot.
    void aVillageInTheRingKeepsItsVersion()
    {
        constexpr int frozen = withoutVillageLoot;
        const std::unique_ptr<QcWorldGenerator> current = terrain(QcGeneratorVersion::current);
        const std::optional<RingVillage> village = smallVillage(dynamic_cast<const QcTerrainGenerator &>(*current));
        QVERIFY(village.has_value());
        const QcChunkPos beside = QcBlockPos{village->bounds.maxX + 3 * QcWorld::chunkSize, 0, (village->bounds.minZ + village->bounds.maxZ) / 2}.chunk();

        QTemporaryDir dir;
        QTemporaryDir backups;
        {
            auto world = open(dir.path(), frozen);
            QVERIFY(world->world().loadChunk(beside));
            QVERIFY(world->save().has_value());
        }
        const QList<QcGeneratorUpgrade::Change> offer = QcGeneratorUpgrade::outdatedIn(*QcWorldStorage(dir.path(), m_content).readInfo(), *m_content);
        const QcResult<QcGeneratorUpgrade::Converted> converted =
            QcGeneratorUpgrade::convert({dir.path(), offer, QcGeneratorUpgrade::Mode::Blend, QcBlendWidth::maxChunks, backups.path()}, m_content, {});
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());

        const QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(dir.path());
        QVERIFY(map.has_value());
        const QList<QcChunkPos> covered = chunksOf(village->bounds);
        for (const QcChunkPos chunk : covered) {
            QCOMPARE(map->versionAt(chunk), std::optional(frozen));
        }
        auto world = open(dir.path(), QcGeneratorVersion::current);
        const QcChunkPos pos = village->chest.chunk();
        QVERIFY(isChest(*world->world().loadChunk(pos), village->chest));
        const QcWorldGenerator &generator = world->world().generator();
        QVERIFY(generator.villageFor(pos));
        const QcChunkFurnishing furnishing = generator.furnishingIn(pos);
        QVERIFY(std::ranges::none_of(furnishing.chests, [](const QcChunkFurnishing::Chest &chest) { return chest.lootTable.startsWith(u"qtcraft:village_"_s); }));
    }

    // A structure an explored chunk started keeps its old plan: every chunk it reaches stays at
    // version 1 and builds it, and version 2 builds nothing that reaches into the old chunks.
    void aStructureAcrossTheBorderKeepsItsPlan()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        const QcChunk before = exploredWorld(dir.path(), m_started);
        const QcResult<QcGeneratorUpgrade::Converted> converted = QcGeneratorUpgrade::convert(request(dir.path(), backups.path()), m_content, {});
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());

        const QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(dir.path());
        QVERIFY(map.has_value());
        const QList<QcChunkPos> reached = chunksOf(m_structure);
        for (const QcChunkPos chunk : reached) {
            QCOMPARE(map->versionAt(chunk), std::optional(1));
        }

        auto world = open(dir.path(), QcGeneratorVersion::current);
        const QcWorldGenerator &generator = world->world().generator();
        QVERIFY(QcTestUtil::sameBlocks(*world->world().loadChunk(m_started), before));
        for (const QcChunkPos chunk : reached) {
            QVERIFY2(generator.structuresTouching(chunk).contains(m_structure), qPrintable(u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z)));
        }
        // Around the old area: what version 2 builds lies wholly in its own chunks.
        const QcXzRect around = m_structure.grown(8 * QcWorld::chunkSize);
        const QList<QcChunkPos> aroundChunks = chunksOf(around);
        for (const QcChunkPos chunk : aroundChunks) {
            if (map->versionAt(chunk)) {
                continue;
            }
            const QList<QcXzRect> reaches = generator.structuresTouching(chunk);
            for (const QcXzRect reach : reaches) {
                const QList<QcChunkPos> covered = chunksOf(reach);
                QVERIFY(std::ranges::none_of(covered, [&map](QcChunkPos c) { return map->versionAt(c).has_value(); }));
            }
        }
    }

    // The conversion backs the world up, reports its stages and leaves the world at version 2
    // with the map of the chunks that stay at version 1.
    void theConversionBacksUpAndSwitches()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        exploredWorld(dir.path(), m_started);
        const QMap<QString, QByteArray> before = QcTestUtil::filesOf(dir.path());
        Recorder recorder;
        const QcConversionControl control{[&recorder](const QcConversionControl::Step &step) { recorder.note(step); }, {}};
        const QcResult<QcGeneratorUpgrade::Converted> converted = QcGeneratorUpgrade::convert(request(dir.path(), backups.path()), m_content, control);
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
        QVERIFY(!converted->cancelled);
        QVERIFY(converted->keptChunks >= chunksOf(m_structure).size());
        QCOMPARE(recorder.stages, (QList{QcConversionControl::Stage::Backup, QcConversionControl::Stage::Survey, QcConversionControl::Stage::Write}));

        QVERIFY(converted->backup.startsWith(backups.path()));
        QVERIFY(converted->backup.endsWith(QcGeneratorUpgrade::backupLabel(QcGeneratorVersion::current)));
        QCOMPARE(QcTestUtil::filesOf(converted->backup), before);
        QCOMPARE(QcWorldStorage(dir.path(), m_content).readInfo()->generatorVersion, QcGeneratorVersion::current);
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(QcVersionMapFile::fileName.toString())));
        QVERIFY(QcGeneratorUpgrade::outdatedIn(*QcWorldStorage(dir.path(), m_content).readInfo(), *m_content).isEmpty());
    }

    // Cancelled before writing, in the backup or while surveying, the world stays byte for byte
    // as it was and no backup is left.
    void cancellingLeavesTheWorldAsItWas()
    {
        for (const QcConversionControl::Stage at : {QcConversionControl::Stage::Backup, QcConversionControl::Stage::Survey}) {
            QTemporaryDir dir;
            QTemporaryDir backups;
            exploredWorld(dir.path(), m_started);
            const QMap<QString, QByteArray> before = QcTestUtil::filesOf(dir.path());
            bool stop = false;
            const QcConversionControl control{[&stop, at](const QcConversionControl::Step &step) { stop = stop || step.stage == at; },
                                              [&stop] { return stop; }};
            const QcResult<QcGeneratorUpgrade::Converted> converted = QcGeneratorUpgrade::convert(request(dir.path(), backups.path()), m_content, control);
            QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
            QVERIFY(converted->cancelled);
            QCOMPARE(QcTestUtil::filesOf(dir.path()), before);
            QVERIFY(QDir(backups.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
        }
    }

    // An open world, a ring of a width not allowed and nothing to switch are refused with the world
    // unchanged and no backup made.
    void refusalsChangeNothing()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        exploredWorld(dir.path(), m_started);
        const QMap<QString, QByteArray> before = QcTestUtil::filesOf(dir.path());
        QcGeneratorUpgrade::Request blend = request(dir.path(), backups.path());
        blend.mode = QcGeneratorUpgrade::Mode::Blend;
        blend.blendChunks = QcBlendWidth::maxChunks + 1;
        QVERIFY(QcGeneratorUpgrade::convert(blend, m_content, {}).error().contains(u"chunks wide"_s));
        QcGeneratorUpgrade::Request none = request(dir.path(), backups.path());
        none.changes.clear();
        QVERIFY(!QcGeneratorUpgrade::convert(none, m_content, {}).has_value());
        {
            auto world = open(dir.path(), 1);
            QVERIFY(QcGeneratorUpgrade::convert(request(dir.path(), backups.path()), m_content, {}).error().contains(u"is open"_s));
            QVERIFY(world->save().has_value());
        }
        QCOMPARE(QcTestUtil::filesOf(dir.path()).keys(), before.keys());
        QVERIFY(QDir(backups.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
    }

    // A world declined with "don't ask again" is not offered again until a newer version comes;
    // the answer stands in world.json and the settings forget it (decision E-1014).
    void dontAskAgainUntilANewerVersion()
    {
        QTemporaryDir dir;
        exploredWorld(dir.path(), m_started);
        const QList<QcGeneratorUpgrade::Change> changes{{overworldName, 1, QcGeneratorVersion::current}};
        QcWorldInfo info = *QcWorldStorage(dir.path(), m_content).readInfo();
        QVERIFY(!QcGeneratorUpgrade::isDeclined(info, changes));

        QVERIFY(QcGeneratorUpgrade::keepForGood(dir.path(), changes, m_content).has_value());
        info = *QcWorldStorage(dir.path(), m_content).readInfo();
        QCOMPARE(info.keepGeneratorVersion, QcGeneratorVersion::current);
        QCOMPARE(info.generatorVersion, 1);
        QVERIFY(QcGeneratorUpgrade::isDeclined(info, changes));
        // A newer generator asks again.
        QVERIFY(!QcGeneratorUpgrade::isDeclined(info, {{overworldName, 1, QcGeneratorVersion::current + 1}}));
        {
            // The server keeps the answer when it saves the world.
            auto world = open(dir.path(), 1);
            QVERIFY(world->save().has_value());
        }
        QCOMPARE(QcWorldStorage(dir.path(), m_content).readInfo()->keepGeneratorVersion, QcGeneratorVersion::current);

        QTemporaryDir other;
        exploredWorld(other.path(), m_started);
        QCOMPARE(QcGeneratorUpgrade::askAgain({dir.path(), other.path()}, m_content), 1);
        QCOMPARE(QcWorldStorage(dir.path(), m_content).readInfo()->keepGeneratorVersion, 0);
        QCOMPARE(QcGeneratorUpgrade::askAgain({dir.path()}, m_content), 0);
    }

    // Planning never asks the structure filter (decision E-1013): a village the filter rejects still
    // keeps mine shafts and coastal towers of its region where they were, else explored chunks next
    // to it would change.
    void planningIgnoresTheFilter()
    {
        const std::unique_ptr<QcWorldGenerator> plain = terrain(1);
        const std::unique_ptr<QcWorldGenerator> filtered = terrain(1);
        const auto *plainTerrain = dynamic_cast<const QcTerrainGenerator *>(plain.get());
        auto *filteredTerrain = dynamic_cast<QcTerrainGenerator *>(filtered.get());
        QVERIFY(plainTerrain && filteredTerrain);
        const std::shared_ptr<const QcVillagePlan> village = plainTerrain->villages().nearest({0, 64, 0}, 4096);
        QVERIFY(village != nullptr);
        const QcXzRect rejected = village->bounds();
        filteredTerrain->setStructureFilter([rejected](QcXzRect columns) { return !(columns == rejected); });
        QVERIFY(filteredTerrain->villages().nearest(village->centre, 64) != village);

        const QcXzRect around = rejected.grown(4 * QcWorld::chunkSize);
        QCOMPARE(plannedAround(*filteredTerrain, around), plannedAround(*plainTerrain, around));
    }

    // A structure of the new version whose reach crosses into chunks of the old one is left out, also
    // on the new side of the border.
    void aNewStructureAcrossTheBorderIsLeftOut()
    {
        const std::unique_ptr<QcWorldGenerator> two = terrain(2);
        QcXzRect reach;
        for (int ring = 0; ring < 48 && reach.maxX == reach.minX; ++ring) {
            for (int dz = -ring; dz <= ring && reach.maxX == reach.minX; ++dz) {
                for (int dx = -ring; dx <= ring && reach.maxX == reach.minX; ++dx) {
                    const QList<QcXzRect> reaches = two->structuresTouching({dx, dz});
                    const auto wide = std::ranges::find_if(reaches, [](QcXzRect r) { return chunksOf(r).size() >= 4; });
                    if (wide != reaches.end()) {
                        reach = *wide;
                    }
                }
            }
        }
        const QList<QcChunkPos> covered = chunksOf(reach);
        QVERIFY(covered.size() >= 4);
        // One chunk of the structure stays with version 1.
        const QcChunkPos old = covered.front();
        QcGeneratorVersionMap map;
        QcChunkSet chunks;
        chunks.insert(old);
        map.mark(chunks, 1);
        QcVersionedGenerator mapped(terrain(2));
        QVERIFY(mapped.setVersionMap(map, [this](int version) { return terrain(version); }).has_value());
        for (const QcChunkPos chunk : covered) {
            QVERIFY2(!mapped.structuresTouching(chunk).contains(reach), qPrintable(u"chunk %1, %2"_s.arg(chunk.x).arg(chunk.z)));
        }
    }

    // qtcraft-headless --convert-generator does the same for a world of its data directory and
    // refuses the transition with a message.
    void theHeadlessConversion()
    {
        QTemporaryDir data;
        const QString directory = QDir(data.path()).filePath(u"worlds/Old"_s);
        QVERIFY(QDir().mkpath(directory));
        const QcChunk before = exploredWorld(directory, m_started);
        const auto run = [&data](const QStringList &arguments) {
            QProcess process;
            process.start(QStringLiteral(QTCRAFT_HEADLESS_PATH),
                          QStringList{u"--data-dir"_s, data.path(), u"--no-mods"_s, u"--no-log-file"_s, u"--convert-generator"_s, u"Old"_s} + arguments);
            if (!process.waitForFinished(120000)) {
                process.kill();
                process.waitForFinished();
            }
            qInfo().noquote() << process.readAllStandardOutput() << process.readAllStandardError();
            return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
        };
        QCOMPARE(run({u"--mode"_s, u"blend"_s, u"--blend-chunks"_s, u"1"_s}), 1);
        QCOMPARE(QcWorldStorage(directory, m_content).readInfo()->generatorVersion, 1);

        // A transition in a world of its own: the ring is stored and the version switched.
        QTemporaryDir blendData;
        const QString blendedWorld = QDir(blendData.path()).filePath(u"worlds/Old"_s);
        QVERIFY(QDir().mkpath(blendedWorld));
        exploredWorld(blendedWorld, m_started);
        QProcess blended;
        blended.start(QStringLiteral(QTCRAFT_HEADLESS_PATH), {u"--data-dir"_s, blendData.path(), u"--no-mods"_s, u"--no-log-file"_s, u"--convert-generator"_s,
                                                              u"Old"_s, u"--mode"_s, u"blend"_s, u"--blend-chunks"_s, u"2"_s});
        QVERIFY(blended.waitForFinished(240000));
        QCOMPARE(blended.exitCode(), 0);
        QVERIFY(blended.readAllStandardOutput().contains("chunks blend"));
        QCOMPARE(QcWorldStorage(blendedWorld, m_content).readInfo()->generatorVersion, QcGeneratorVersion::current);
        QVERIFY(QDir(blendedWorld).exists(u"regions"_s));

        QCOMPARE(run({u"--to"_s, QString::number(QcGeneratorVersion::current), u"--mode"_s, u"hard"_s}), 0);
        QCOMPARE(QcWorldStorage(directory, m_content).readInfo()->generatorVersion, QcGeneratorVersion::current);
        const QDir backups(QDir(data.path()).filePath(u"backups"_s));
        QCOMPARE(backups.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), qsizetype{1});
        // Already at that version: refused before a backup is made.
        QCOMPARE(run({u"--to"_s, QString::number(QcGeneratorVersion::current)}), 1);
        QCOMPARE(backups.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), qsizetype{1});

        auto world = open(directory, QcGeneratorVersion::current);
        QVERIFY(QcTestUtil::sameBlocks(*world->world().loadChunk(m_started), before));
    }
};

QTEST_GUILESS_MAIN(TestGeneratorUpgrade)
#include "test_generator_upgrade.moc"
