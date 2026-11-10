// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/core/QcWorldConstants.h"
#include "common/world/QcBlendWidth.h"
#include "common/world/QcChunkSet.h"
#include "common/world/QcGeneratorVersionMap.h"
#include "common/world/QcWorldInfo.h"
#include "fixtures/QcDirectoryHashes.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerConfig.h"
#include "server/storage/QcChunkCodec.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcWorldStorage.h"
#include "server/versions/QcGeneratorSwitch.h"
#include "server/versions/QcVersionMapFile.h"
#include "worldgen/blending/QcBlendField.h"
#include "worldgen/blending/QcChunkShape.h"
#include "worldgen/blending/QcRingBlender.h"
#include "worldgen/generators/QcVersionedGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcGeneratorUpgrade.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

// The seed of the generator tests ("terrain 7" in tests/data/generator-versions.json): versions 1
// and 2 are frozen and differ along the rivers there.
constexpr qint64 frozenSeed = 7;
const QString overworldName = QString::fromLatin1(QcDimensionDefinition::overworldName);

// The blended chunks of a ring.
using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;

// The square of chunks of the side `2 * radius + 1` around `middle`.
QcChunkSet squareAround(QcChunkPos middle, int radius)
{
    QcChunkSet chunks;
    for (int z = -radius; z <= radius; ++z) {
        for (int x = -radius; x <= radius; ++x) {
            chunks.insert({middle.x + x, middle.z + z});
        }
    }
    return chunks;
}

// Whether the columns `x`, `z` (local) of `a` and `b` hold the same blocks.
bool sameColumn(const QcChunk &a, const QcChunk &b, int x, int z)
{
    for (int y = 0; y < QcWorld::worldHeight; ++y) {
        if (a.block(x, y, z) != b.block(x, y, z)) {
            return false;
        }
    }
    return true;
}

// What a ring looks like from outside, counted over all its columns.
struct Findings
{
    // The columns compared at the two ends of the ring and those that differ.
    int innerColumns = 0;
    int innerDifferent = 0;
    int outerColumns = 0;
    int outerDifferent = 0;
    // The greatest step of the ground between neighbours beyond what the versions have there, and
    // the greatest step of the hard seam the ring replaces.
    int worstExcess = 0;
    int hardSeam = 0;
    // Blocks without a neighbour that are not plants on the ground, water next to air, per ring.
    int floating = 0;
    int waterWalls = 0;
    // The same in the two versions alone.
    int floatingInVersions = 0;
    int waterWallsInVersions = 0;
    // Columns with water in the ring that are dry in both versions, and how many of them lie
    // apart from every column wet in a version.
    int wetButDry = 0;
    int strayWater = 0;
};

} // namespace

// The ring between an old and a new generator version (package 87.3, decisions E-1040 to E-1042):
// its ends, its continuity, the structures in it, and the conversion that stores it. Versions 1
// and 2 are the frozen ones; two terrains of different seeds stand in for versions that differ
// everywhere, the hardest case a ring can get.
class TestGeneratorBlend : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    std::unique_ptr<QcWorldGenerator> terrain(qint64 seed, int version) const
    {
        return QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::terrain), m_content, seed, {}, version);
    }

    // The chunks of `blended` decoded.
    Chunks decoded(const QList<QcRingBlender::Blended> &blended) const
    {
        Chunks chunks;
        for (const QcRingBlender::Blended &chunk : blended) {
            QcLoadWarnings warnings;
            chunks.insert(chunk.pos, *QcChunkCodec::decode(chunk.blob, chunk.pos, *m_content, warnings));
        }
        return chunks;
    }

    // Blends `older` into `newer` around `old` in a ring `width` chunks wide.
    Chunks blend(const QcWorldGenerator &older, const QcWorldGenerator &newer, const QcBlendField &field) const
    {
        const auto encode = [this](const QcChunk &chunk) { return QcChunkCodec::encode(chunk, *m_content); };
        const std::optional<QList<QcRingBlender::Blended>> blended = QcRingBlender::blend({older, newer}, field, 1, encode, {});
        return blended ? decoded(*blended) : Chunks{};
    }

    // The ground of the column at `x`, `z` (world coordinates) of `chunks`; none outside them.
    static std::optional<int> groundOf(const QHash<QcChunkPos, QcChunkShape> &shapes, int x, int z)
    {
        const QcBlockPos pos{x, 0, z};
        const auto found = shapes.constFind(pos.chunk());
        return found == shapes.cend() ? std::nullopt : std::optional<int>{found->column(pos.localX(), pos.localZ()).ground};
    }

    // The block at `x`, `y`, `z` of the chunk `pos` of `chunks`, the coordinates reaching one chunk
    // beyond it; stone beyond the chunks, where nothing can be judged.
    static QcBlockId blockAround(const Chunks &chunks, QcChunkPos pos, std::array<int, 3> at)
    {
        const QcBlockPos world{pos.x * QcWorld::chunkSize + at[0], at[1], pos.z * QcWorld::chunkSize + at[2]};
        const auto found = chunks.constFind(world.chunk());
        return found == chunks.cend() ? QcBlockId{1} : (*found)->block(world.localX(), at[1], world.localZ());
    }

    // Counts the blocks of the chunk `pos` of `chunks` that float and the water that stands next to
    // air, also across its borders (`generator` tells ground from the rest).
    static void countLoose(const Chunks &chunks, QcChunkPos pos, const QcWorldGenerator &generator, Findings &findings)
    {
        static constexpr std::array<std::array<int, 3>, 6> steps{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}}};
        const QcChunk &chunk = **chunks.constFind(pos);
        const QcChunkShape shape(chunk, generator);
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                const QcColumnShape &column = shape.column(x, z);
                for (int y = column.ground + 1; y <= chunk.highestNonAir(x, z); ++y) {
                    const QcBlockId block = chunk.block(x, y, z);
                    if (block == qcAirBlockId) {
                        continue;
                    }
                    const auto open = [&](std::array<int, 3> step) {
                        const QcBlockId next = blockAround(chunks, pos, {x + step[0], y + step[1], z + step[2]});
                        return next == qcAirBlockId || generator.isWaterBlock(next);
                    };
                    if (generator.isWaterBlock(block)) {
                        const auto isAir = [&](std::array<int, 3> step) { return blockAround(chunks, pos, {x + step[0], y, z + step[2]}) == qcAirBlockId; };
                        findings.waterWalls += std::ranges::any_of(std::array{steps[0], steps[1], steps[2], steps[3]}, isAir) ? 1 : 0;
                    } else if (y > column.ground + 1 && std::ranges::all_of(steps, open)) {
                        ++findings.floating;
                    }
                }
            }
        }
    }

    // Counts the columns of `shapes` with water that are dry in both versions; the stray ones
    // are not joined to a column wet in a version by columns with water.
    static void countStrayWater(const QHash<QcChunkPos, QcChunkShape> &shapes, const QHash<QcChunkPos, QcChunkShape> &olderShapes,
                                const QHash<QcChunkPos, QcChunkShape> &newerShapes, Findings &findings)
    {
        const auto waterAt = [](const QHash<QcChunkPos, QcChunkShape> &from, int x, int z) {
            const QcBlockPos pos{x, 0, z};
            const auto found = from.constFind(pos.chunk());
            return found != from.cend() && found->column(pos.localX(), pos.localZ()).water >= 0;
        };
        QSet<std::pair<int, int>> reached;
        std::vector<std::pair<int, int>> open;
        QSet<std::pair<int, int>> dry;
        for (auto it = shapes.cbegin(); it != shapes.cend(); ++it) {
            for (std::size_t column = 0; column < QcBlendField::columnCount; ++column) {
                const int k = static_cast<int>(column);
                const std::pair<int, int> at{it.key().x * QcWorld::chunkSize + k % QcWorld::chunkSize, it.key().z * QcWorld::chunkSize + k / QcWorld::chunkSize};
                if (!waterAt(shapes, at.first, at.second)) {
                    continue;
                }
                if (waterAt(olderShapes, at.first, at.second) || waterAt(newerShapes, at.first, at.second)) {
                    reached.insert(at);
                    open.push_back(at);
                } else {
                    dry.insert(at);
                }
            }
        }
        findings.wetButDry += static_cast<int>(dry.size());
        while (!open.empty()) {
            const auto [x, z] = open.back();
            open.pop_back();
            for (const auto &[stepX, stepZ] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const std::pair<int, int> next{x + stepX, z + stepZ};
                if (dry.remove(next)) {
                    open.push_back(next);
                }
            }
        }
        findings.strayWater += static_cast<int>(dry.size());
    }

    // What the ring `chunks` of `field` shows against `older` and `newer`.
    Findings inspect(const Chunks &chunks, const QcBlendField &field, const QcWorldGenerator &older, const QcWorldGenerator &newer) const
    {
        Findings findings;
        QHash<QcChunkPos, QcChunkShape> shapes;
        QHash<QcChunkPos, QcChunkShape> olderShapes;
        QHash<QcChunkPos, QcChunkShape> newerShapes;
        Chunks olderChunks;
        Chunks newerChunks;
        for (auto it = chunks.cbegin(); it != chunks.cend(); ++it) {
            olderChunks.insert(it.key(), older.generate(it.key()));
            newerChunks.insert(it.key(), newer.generate(it.key()));
        }
        for (auto it = chunks.cbegin(); it != chunks.cend(); ++it) {
            const QcChunk &old = *olderChunks.value(it.key());
            const QcChunk &fresh = *newerChunks.value(it.key());
            shapes.insert(it.key(), QcChunkShape(**it, older));
            olderShapes.insert(it.key(), QcChunkShape(old, older));
            newerShapes.insert(it.key(), QcChunkShape(fresh, newer));
            countLoose(chunks, it.key(), older, findings);
            Findings alone;
            countLoose(olderChunks, it.key(), older, alone);
            countLoose(newerChunks, it.key(), newer, alone);
            findings.floatingInVersions += alone.floating;
            findings.waterWallsInVersions += alone.waterWalls;

            const QcBlendField::Shares shares = field.sharesIn(it.key());
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    const float share = shares[static_cast<std::size_t>(QcBlendField::columnIndex(x, z))];
                    if (share < 0.003F) {
                        ++findings.innerColumns;
                        findings.innerDifferent += sameColumn(**it, old, x, z) ? 0 : 1;
                    } else if (share > 0.997F) {
                        ++findings.outerColumns;
                        findings.outerDifferent += sameColumn(**it, fresh, x, z) ? 0 : 1;
                    }
                }
            }
        }
        measureSteps(shapes, olderShapes, newerShapes, findings);
        countStrayWater(shapes, olderShapes, newerShapes, findings);
        return findings;
    }

    // Compares the steps of the ground in the ring with those of the versions.
    static void measureSteps(const QHash<QcChunkPos, QcChunkShape> &shapes, const QHash<QcChunkPos, QcChunkShape> &olderShapes,
                             const QHash<QcChunkPos, QcChunkShape> &newerShapes, Findings &findings)
    {
        for (auto it = shapes.cbegin(); it != shapes.cend(); ++it) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    const int worldX = it.key().x * QcWorld::chunkSize + x;
                    const int worldZ = it.key().z * QcWorld::chunkSize + z;
                    const int here = it->column(x, z).ground;
                    const int olderHere = *groundOf(olderShapes, worldX, worldZ);
                    const int newerHere = *groundOf(newerShapes, worldX, worldZ);
                    findings.hardSeam = std::max(findings.hardSeam, std::abs(olderHere - newerHere));
                    for (const auto &[stepX, stepZ] : {std::pair{1, 0}, std::pair{0, 1}}) {
                        const std::optional<int> next = groundOf(shapes, worldX + stepX, worldZ + stepZ);
                        if (!next) {
                            continue;
                        }
                        const int olderStep = std::abs(*groundOf(olderShapes, worldX + stepX, worldZ + stepZ) - olderHere);
                        const int newerStep = std::abs(*groundOf(newerShapes, worldX + stepX, worldZ + stepZ) - newerHere);
                        findings.worstExcess = std::max(findings.worstExcess, std::abs(*next - here) - std::max(olderStep, newerStep));
                    }
                }
            }
        }
    }

    // The generator as the server opens a world of version 2 with `old` and `ring` at version 1.
    std::unique_ptr<QcVersionedGenerator> openedWith(const QcChunkSet &old, const QcChunkSet &ring) const
    {
        QcGeneratorVersionMap map;
        map.mark(old, 1);
        map.mark(ring, 1);
        auto generator = std::make_unique<QcVersionedGenerator>(terrain(frozenSeed, 2));
        if (const QcStatus set = generator->setVersionMap(map, [this](int version) { return terrain(frozenSeed, version); }); !set) {
            QcTestUtil::failCase(set.error());
        }
        return generator;
    }

    // A world of version 1 in `directory` with `explored` generated, saved and closed.
    void exploredWorld(const QString &directory, const QcChunkSet &explored) const
    {
        auto world = open(directory, 1);
        const QSet<QcChunkPos> chunks = explored.chunks();
        for (const QcChunkPos chunk : chunks) {
            QVERIFY(world->world().loadChunk(chunk) != nullptr);
        }
        QVERIFY(world->save().has_value());
    }

    std::unique_ptr<QcServer> open(const QString &directory, int version) const
    {
        QcServerConfig config;
        config.worldName = u"Blended"_s;
        config.seed = frozenSeed;
        config.worldDirectory = directory;
        config.keepChunksLoaded = true;
        config.generatorVersion = version;
        auto server = std::make_unique<QcServer>(config, m_content, terrain(frozenSeed, version));
        if (const QcStatus opened = server->openWorld(); !opened) {
            QcTestUtil::failCase(u"The world does not open as version %1: %2"_s.arg(version).arg(opened.error()));
        }
        return server;
    }

    // The chunks stored in the world in `directory` outside `explored`, each checked to stand in the
    // version map at version 1 and in the index of generated chunks.
    QHash<QcChunkPos, QcChunk> storedRing(const QString &directory, const QcChunkSet &explored) const
    {
        const QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(directory);
        QcChunkStore store(directory);
        if (!map || !store.open()) {
            QcTestUtil::failCase(u"The switched world does not read"_s);
            return {};
        }
        QHash<QcChunkPos, QcChunk> ring;
        const QHash<QcChunkPos, QcChunkStore::Stored> catalog = store.catalog();
        for (auto it = catalog.cbegin(); it != catalog.cend(); ++it) {
            if (it->has(QcChunkStore::Kind::Blocks) && !explored.contains(it.key())) {
                QcLoadWarnings warnings;
                const QcResult<QcChunkStore::Blobs> blobs = store.read(it.key());
                if (!blobs || map->versionAt(it.key()) != std::optional<int>{1} || !store.generated().contains(it.key())) {
                    QcTestUtil::failCase(u"A chunk of the ring is not as stored"_s);
                    return {};
                }
                ring.insert(it.key(), **QcChunkCodec::decode(*blobs->data[0], it.key(), *m_content, warnings));
            }
        }
        return ring;
    }

    // A chunk the old version furnishes and a structure that reaches it, near the origin but for a
    // ring around land beside it. All structures there are small: one that reached out of the ring
    // would not be built, and its furnishing with it.
    std::pair<QcChunkPos, QcXzRect> furnishedStructure() const
    {
        const std::unique_ptr<QcWorldGenerator> older = terrain(frozenSeed, 1);
        for (int ring = 6; ring < 64; ++ring) {
            for (int x = -ring; x <= ring; ++x) {
                const QcChunkPos chunk{x, ring};
                const QList<QcXzRect> reaches = older->structuresTouching(chunk);
                const auto isSmall = [](QcXzRect reach) { return reach.chunks().size() <= 16; };
                if (!reaches.isEmpty() && std::ranges::all_of(reaches, isSmall) && !older->furnishingIn(chunk).isEmpty()) {
                    return {chunk, reaches.constFirst()};
                }
            }
        }
        QcTestUtil::failCase(u"No furnished structure near the origin"_s);
        return {};
    }

    // Whether the chunks of `reach` all lie in `old` and the ring of `field`.
    static bool isWithin(QcXzRect reach, const QcChunkSet &old, const QcBlendField &field)
    {
        return std::ranges::all_of(reach.chunks(), [&](QcChunkPos chunk) { return old.contains(chunk) || field.ring().contains(chunk); });
    }

    // The chunks of the ring of `field` around `old` that a structure of `older` reaching out of the
    // ring touches.
    static QSet<QcChunkPos> chunksCrossedOutOfTheRing(const QcWorldGenerator &older, const QcChunkSet &old, const QcBlendField &field)
    {
        QSet<QcChunkPos> crossed;
        const QSet<QcChunkPos> ring = field.ring().chunks();
        for (const QcChunkPos chunk : ring) {
            const QList<QcXzRect> reaches = older.structuresTouching(chunk);
            if (!std::ranges::all_of(reaches, [&](QcXzRect reach) { return isWithin(reach, old, field); })) {
                crossed.insert(chunk);
            }
        }
        return crossed;
    }

    // Land near `chunk` that no structure reaches, so that its closure is itself (decision E-1013).
    QcChunkPos landWithoutStructuresNear(QcChunkPos chunk) const
    {
        const std::unique_ptr<QcWorldGenerator> older = terrain(frozenSeed, 1);
        for (int north = 2; north <= 5; ++north) {
            for (int east = -3; east <= 3; ++east) {
                const QcChunkPos land{chunk.x + east, chunk.z - north};
                if (older->structuresTouching(land).isEmpty()) {
                    return land;
                }
            }
        }
        QcTestUtil::failCase(u"No land without structures near the structure"_s);
        return chunk;
    }

    static QcGeneratorUpgrade::Request request(const QString &directory, const QString &backups, int width)
    {
        return {directory, {{overworldName, 1, 2}}, QcGeneratorUpgrade::Mode::Blend, width, backups};
    }

private slots:
    // The share grows from the old land to the width of the ring, without a jump, and the ring holds
    // every chunk with a column that is not wholly new.
    void theFieldGrowsSmoothly()
    {
        const QcChunkSet old = squareAround({0, 0}, 2);
        const QcBlendField field(old, 3, {});
        QVERIFY(!field.ring().contains({0, 0}));
        QVERIFY(field.ring().contains({3, 0}));
        QVERIFY(field.ring().contains({5, 0}));
        QVERIFY(!field.ring().contains({6, 0}));
        const float first = field.sharesIn({3, 0})[static_cast<std::size_t>(QcBlendField::columnIndex(0, 8))];
        const float last = field.sharesIn({5, 0})[static_cast<std::size_t>(QcBlendField::columnIndex(15, 8))];
        QVERIFY(first < 0.01F);
        QVERIFY(last > 0.99F);
        float before = 0.0F;
        for (int chunk = 3; chunk <= 5; ++chunk) {
            const QcBlendField::Shares shares = field.sharesIn({chunk, 0});
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                const float share = shares[static_cast<std::size_t>(QcBlendField::columnIndex(x, 8))];
                QVERIFY(share >= before);
                QVERIFY(share - before < 2.0F / static_cast<float>(field.width()));
                before = share;
            }
        }
        // A held rectangle counts as old, and the ring grows around it.
        const QcBlendField held(old, 3, {{6 * 16, 0, 6 * 16 + 20, 10}});
        QCOMPARE(held.sharesIn({6, 0})[static_cast<std::size_t>(QcBlendField::columnIndex(3, 3))], 0.0F);
        QVERIFY(held.ring().contains({8, 0}));
    }

    void theRingIsContinuous_data()
    {
        QTest::addColumn<qint64>("olderSeed");
        QTest::addColumn<int>("olderVersion");
        QTest::addColumn<qint64>("newerSeed");
        QTest::addColumn<int>("newerVersion");
        QTest::addColumn<QcChunkPos>("middle");
        const qint64 random = QcTestUtil::randomSeed();
        // A river of seed 7 changed between the versions around the chunk (-18, -8).
        QTest::newRow("frozen versions, seed 7") << frozenSeed << 1 << frozenSeed << 2 << QcChunkPos{-24, -8};
        QTest::newRow("frozen versions, drawn seed") << random << 1 << random << 2 << QcChunkPos{4, -3};
        QTest::newRow("all different, seeds 7 and 8") << frozenSeed << 2 << qint64{8} << 2 << QcChunkPos{0, 0};
        QTest::newRow("all different, seeds 3 and 11") << qint64{3} << 2 << qint64{11} << 2 << QcChunkPos{20, 5};
        QTest::newRow("all different, seeds 129 and 130") << qint64{129} << 2 << qint64{130} << 2 << QcChunkPos{-9, 12};
        QTest::newRow("all different, drawn seed") << random << 2 << random + 1 << 2 << QcChunkPos{-9, 12};
    }

    // Inner end old, outer end new, in between no step beyond what the slope of the blend explains
    // and only few loose blocks.
    void theRingIsContinuous()
    {
        QFETCH(qint64, olderSeed);
        QFETCH(int, olderVersion);
        QFETCH(qint64, newerSeed);
        QFETCH(int, newerVersion);
        QFETCH(QcChunkPos, middle);
        const std::unique_ptr<QcWorldGenerator> older = terrain(olderSeed, olderVersion);
        const std::unique_ptr<QcWorldGenerator> newer = terrain(newerSeed, newerVersion);
        newer->setStructureFilter([](QcXzRect) { return false; });
        older->setStructureFilter([](QcXzRect) { return false; });
        const QcBlendField field(squareAround(middle, 2), 4, {});

        const Chunks chunks = blend(*older, *newer, field);
        QCOMPARE(chunks.size(), field.ring().size());
        const Findings found = inspect(chunks, field, *older, *newer);
        qInfo() << "ring" << chunks.size() << "chunks; inner" << found.innerDifferent << "of" << found.innerColumns << "outer" << found.outerDifferent << "of"
                << found.outerColumns << "; hard seam up to" << found.hardSeam << "excess step" << found.worstExcess << "floating" << found.floating
                << "(versions" << found.floatingInVersions << ") water walls" << found.waterWalls << "(versions" << found.waterWallsInVersions
                << ") wet in the ring only" << found.wetButDry << "stray" << found.strayWater;
        QVERIFY(olderSeed != newerSeed || middle.x != -24 || found.hardSeam > 0);
        QVERIFY(found.innerColumns > 100);
        QVERIFY(found.outerColumns > 100);
        QCOMPARE(found.innerDifferent, 0);
        QCOMPARE(found.outerDifferent, 0);
        // The slope of the share is at most 1.5 / width; rounding takes a block and a bank up to
        // the water two more.
        const int allowed = static_cast<int>(std::ceil(1.5 * found.hardSeam / field.width())) + 3;
        QVERIFY2(found.worstExcess <= allowed, qPrintable(u"step %1 beyond the versions, allowed %2"_s.arg(found.worstExcess).arg(allowed)));
        // Near what was measured over many seeds: blocks float by at most about 100 more than
        // in the versions, no water stands against air, and the water of the ring alone is a few columns
        // of a shore, none of it apart from the water of a version.
        const auto columns = static_cast<int>(chunks.size()) * QcWorld::chunkSize * QcWorld::chunkSize;
        QVERIFY2(found.floating <= found.floatingInVersions + columns / 256, qPrintable(u"%1 floating blocks in %2 columns"_s.arg(found.floating).arg(columns)));
        QVERIFY2(found.waterWalls <= found.waterWallsInVersions + 2, qPrintable(u"%1 water walls in %2 columns"_s.arg(found.waterWalls).arg(columns)));
        QVERIFY2(found.wetButDry <= 16, qPrintable(u"%1 columns wet in the ring only"_s.arg(found.wetButDry)));
        QCOMPARE(found.strayWater, 0);
    }

    // Blending gives the same bytes each time, on any number of threads.
    void theRingIsDeterministic()
    {
        const std::unique_ptr<QcWorldGenerator> older = terrain(frozenSeed, 1);
        const std::unique_ptr<QcWorldGenerator> newer = terrain(frozenSeed, 2);
        const QcBlendField field(squareAround({0, 0}, 1), 2, {});
        const auto encode = [this](const QcChunk &chunk) { return QcChunkCodec::encode(chunk, *m_content); };
        const auto first = QcRingBlender::blend({*older, *newer, 1}, field, 1, encode, {});
        const auto second = QcRingBlender::blend({*older, *newer, 3}, field, 1, encode, {});
        QVERIFY(first && second);
        QCOMPARE(first->size(), second->size());
        for (qsizetype i = 0; i < first->size(); ++i) {
            QCOMPARE(first->at(i).pos, second->at(i).pos);
            QCOMPARE(first->at(i).blob, second->at(i).blob);
        }
    }

    // A structure of the old plan in the ring stays whole on its old ground, and the field grows
    // around it; one that reaches out of the ring is neither held nor built, as the server opens it.
    void aStructureInTheRingStaysWhole()
    {
        const std::unique_ptr<QcWorldGenerator> older = terrain(frozenSeed, 1);
        QcXzRect structure;
        for (int ring = 6; ring < 48 && structure.maxX == structure.minX; ++ring) {
            for (int x = -ring; x <= ring && structure.maxX == structure.minX; ++x) {
                const QList<QcXzRect> reaches = older->structuresTouching({x, ring});
                const auto wide = std::ranges::find_if(reaches, [](QcXzRect reach) { return reach.chunks().size() >= 2 && reach.chunks().size() <= 16; });
                if (wide != reaches.end()) {
                    structure = *wide;
                }
            }
        }
        QVERIFY(structure.maxX != structure.minX);

        // The old land ends two chunks north of the structure.
        const QcChunkPos north = QcBlockPos{structure.middleX(), 0, structure.minZ}.chunk();
        const QcChunkSet old = squareAround({north.x, north.z - 4}, 1);
        const std::optional<QcBlendField> field = QcRingBlender::plan(*older, old, 6, {});
        QVERIFY(field.has_value());
        QVERIFY(field->held().contains(structure));

        const std::unique_ptr<QcWorldGenerator> newer = terrain(frozenSeed, 2);
        newer->setStructureFilter([](QcXzRect) { return false; });
        const std::unique_ptr<QcVersionedGenerator> opened = openedWith(old, field->ring());
        const Chunks chunks = blend(*opened, *newer, *field);
        const QList<QcChunkPos> reached = structure.chunks();
        for (const QcChunkPos chunk : reached) {
            QVERIFY(chunks.contains(chunk));
            const std::shared_ptr<QcChunk> source = opened->generate(chunk);
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    if (structure.contains(chunk.x * QcWorld::chunkSize + x, chunk.z * QcWorld::chunkSize + z)) {
                        QVERIFY(sameColumn(*chunks.value(chunk), *source, x, z));
                    }
                }
            }
        }

        const QSet<QcChunkPos> crossing = chunksCrossedOutOfTheRing(*older, old, *field);
        QVERIFY2(!crossing.isEmpty(), "no structure reaches out of this ring");
        bool leftOut = false;
        for (const QcChunkPos chunk : crossing) {
            const QList<QcXzRect> built = opened->structuresTouching(chunk);
            QVERIFY(std::ranges::all_of(built, [&](QcXzRect reach) { return isWithin(reach, old, *field); }));
            QVERIFY(std::ranges::all_of(field->held(), [&](QcXzRect reach) { return isWithin(reach, old, *field); }));
            leftOut = leftOut || !QcTestUtil::sameBlocks(*opened->generate(chunk), *older->generate(chunk));
        }
        // Such a structure may touch a chunk without a block in it, but not all of them.
        QVERIFY(leftOut);
    }

    // What is held after the rounds keeps the ring: the field says that its edge is cut.
    void aFieldHeldWithinItsRingKeepsTheRing()
    {
        const QcBlendField field(squareAround({0, 0}, 1), 2, {});
        QVERIFY(!field.isCut());
        const QcXzRect held{70, 0, 80, 10};
        const QcBlendField cut = field.heldWithinRing({held});
        QVERIFY(cut.isCut());
        QCOMPARE(cut.ring().size(), field.ring().size());
        QVERIFY(cut.held().contains(held));
        QCOMPARE(cut.sharesIn(QcBlockPos{75, 0, 5}.chunk())[static_cast<std::size_t>(QcBlendField::columnIndex(75 % 16, 5))], 0.0F);
    }

    // The conversion: ring chunks in the store and the map at the old version, explored chunks as
    // before, the new version beyond, all the same after a reload; a second switch is refused.
    void theConversionStoresTheRing()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        const QcChunkSet explored = squareAround({0, 0}, 1);
        exploredWorld(dir.path(), explored);
        QHash<QcChunkPos, QcChunk> before;
        {
            auto world = open(dir.path(), 1);
            const QSet<QcChunkPos> chunks = explored.chunks();
            for (const QcChunkPos chunk : chunks) {
                before.insert(chunk, *world->world().loadChunk(chunk));
            }
        }

        QList<QcConversionControl::Stage> stages;
        const QcConversionControl control{[&stages](const QcConversionControl::Step &step) {
                                              if (stages.isEmpty() || stages.constLast() != step.stage) {
                                                  stages.append(step.stage);
                                              }
                                          },
                                          {}};
        const auto converted = QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 2), m_content, control);
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
        QVERIFY(!converted->cancelled);
        QVERIFY(converted->ringChunks > 0);
        QCOMPARE(stages, (QList{QcConversionControl::Stage::Backup, QcConversionControl::Stage::Survey, QcConversionControl::Stage::Blend,
                                QcConversionControl::Stage::Write}));
        QCOMPARE(QcWorldStorage(dir.path(), m_content).readInfo()->generatorVersion, 2);

        const QHash<QcChunkPos, QcChunk> ring = storedRing(dir.path(), explored);
        QCOMPARE(ring.size(), converted->ringChunks);

        const QcChunkPos beyond{40, 40};
        {
            auto world = open(dir.path(), 2);
            for (auto it = before.cbegin(); it != before.cend(); ++it) {
                QVERIFY(QcTestUtil::sameBlocks(*world->world().loadChunk(it.key()), *it));
            }
            for (auto it = ring.cbegin(); it != ring.cend(); ++it) {
                QVERIFY(QcTestUtil::sameBlocks(*world->world().loadChunk(it.key()), *it));
            }
            QVERIFY(QcTestUtil::sameBlocks(*world->world().loadChunk(beyond), *terrain(frozenSeed, 2)->generate(beyond)));
            QVERIFY(world->save().has_value());
        }
        {
            auto world = open(dir.path(), 2);
            for (auto it = ring.cbegin(); it != ring.cend(); ++it) {
                QVERIFY(QcTestUtil::sameBlocks(*world->world().loadChunk(it.key()), *it));
            }
        }

        const QcResult<QcGeneratorUpgrade::Converted> again = QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 2), m_content, {});
        QVERIFY(!again.has_value());
        QVERIFY(again.error().contains(u"already"_s));
    }

    // A chest or spawner of a structure held in the ring gets its loot or kind when its chunk first
    // loads, once: the stored chunk has no record of block entities yet.
    void aHeldStructureIsFurnished()
    {
        const auto [furnished, reach] = furnishedStructure();
        QTemporaryDir dir;
        QTemporaryDir backups;
        const QcChunkPos north = QcBlockPos{reach.middleX(), 0, reach.minZ}.chunk();
        const QcChunkSet explored = squareAround(landWithoutStructuresNear(north), 0);
        exploredWorld(dir.path(), explored);
        const auto converted = QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 6), m_content, {});
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
        QVERIFY(storedRing(dir.path(), explored).contains(furnished));
        const QcChunkFurnishing furnishing = terrain(frozenSeed, 1)->furnishingIn(furnished);
        {
            auto world = open(dir.path(), 2);
            QVERIFY(world->world().loadChunk(furnished) != nullptr);
            QVERIFY(world->world().blockEntityExtras().isFurnished(furnished));
            for (const QcChunkFurnishing::Chest &chest : furnishing.chests) {
                if (chest.at.chunk() == furnished) {
                    const QcBlockEntities::Container *container = world->blockEntities().container(chest.at);
                    QVERIFY(container != nullptr);
                    QVERIFY(std::ranges::any_of(container->stacks, [](const QcItemStack &stack) { return !stack.isEmpty(); }));
                }
            }
            for (const QcStructureDefinition::Spawner &spawner : furnishing.spawners) {
                if (spawner.at.chunk() == furnished) {
                    QVERIFY(world->world().spawners().at(spawner.at) != nullptr);
                }
            }
            QVERIFY(world->save().has_value());
        }
        auto world = open(dir.path(), 2);
        QVERIFY(world->world().loadChunk(furnished) != nullptr);
        QVERIFY(world->world().blockEntityExtras().isFurnished(furnished));
    }

    // A second switch around a ring: it keeps the first ring as it is and adds its own.
    void aSecondSwitchKeepsTheFirstRing()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        const QcChunkSet explored = squareAround({0, 0}, 0);
        exploredWorld(dir.path(), explored);
        QVERIFY(QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 2), m_content, {}).has_value());
        const QHash<QcChunkPos, QcChunk> first = storedRing(dir.path(), explored);
        QVERIFY(!first.isEmpty());

        // Back to version 1 stands in for a newer version than this game has.
        QcGeneratorUpgrade::Request again = request(dir.path(), backups.path(), 2);
        again.changes = {{overworldName, 2, 1}};
        const auto converted = QcGeneratorUpgrade::convert(again, m_content, {});
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
        QVERIFY(converted->ringChunks > 0);
        const QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(dir.path());
        QVERIFY(map.has_value());
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        for (auto it = first.cbegin(); it != first.cend(); ++it) {
            QCOMPARE(map->versionAt(it.key()), std::optional<int>{1});
            QcLoadWarnings warnings;
            const QcResult<QcChunkStore::Blobs> blobs = store.read(it.key());
            QVERIFY(blobs.has_value());
            QVERIFY(QcTestUtil::sameBlocks(**QcChunkCodec::decode(*blobs->data[0], it.key(), *m_content, warnings), *it));
        }
        int second = 0;
        const QHash<QcChunkPos, QcChunkStore::Stored> catalog = store.catalog();
        for (auto it = catalog.cbegin(); it != catalog.cend(); ++it) {
            second += map->versionAt(it.key()) == std::optional<int>{2} ? 1 : 0;
        }
        QCOMPARE(second, converted->ringChunks);
    }

    // A ring's save that breaks off after its journal is finished before anything reads world.json:
    // the question sees the new version, and a switch is refused without a backup.
    void aRingSaveThatBrokeOffIsFinishedFirst()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        exploredWorld(dir.path(), squareAround({0, 0}, 0));
        const QcWorldStorage storage(dir.path(), m_content);
        QcWorldInfo info = *storage.readInfo();
        const QcWorldInfo before = info;
        const auto older = terrain(frozenSeed, 1);
        const auto newer = terrain(frozenSeed, 2);
        const QcBlendField field(squareAround({0, 0}, 0), 2, {});
        const auto encode = [this](const QcChunk &chunk) { return QcChunkCodec::encode(chunk, *m_content); };
        const auto ring = QcRingBlender::blend({*older, *newer}, field, 1, encode, {});
        QVERIFY(ring.has_value());
        info.generatorVersion = 2;
        {
            QcChunkStore store(dir.path());
            QVERIFY(store.open().has_value());
            store.failRenamesAfter(1);
            QVERIFY(!QcGeneratorSwitch::commitRing(store, *ring, info).has_value());
        }
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(QcChunkStore::journalName.toString())));
        QCOMPARE(storage.readInfo()->generatorVersion, 1);

        QCOMPARE(QcGeneratorUpgrade::settledInfo(dir.path(), before, m_content).generatorVersion, 2);
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QcChunkStore::journalName.toString())));
        const QcResult<QcGeneratorUpgrade::Converted> again = QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 2), m_content, {});
        QVERIFY(!again.has_value());
        QVERIFY(again.error().contains(u"already"_s));
        QVERIFY(QDir(backups.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QcChunkStore store(dir.path());
        QVERIFY(store.open().has_value());
        QVERIFY(store.has(QcChunkStore::Kind::Blocks, ring->constFirst().pos));
    }

    // A flat world's versions are the same: no ring is stored, the switch goes through.
    void aFlatWorldGetsNoRing()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        QcServerConfig config;
        config.worldName = u"Flat"_s;
        config.seed = frozenSeed;
        config.worldDirectory = dir.path();
        config.generatorVersion = 1;
        const auto type = QString::fromLatin1(QcWorldGenerators::flat);
        {
            QcServer server(config, m_content, QcWorldGenerators::create(type, m_content, frozenSeed, {}, 1));
            QVERIFY(server.openWorld().has_value());
            QVERIFY(server.world().loadChunk({0, 0}) != nullptr);
            QVERIFY(server.save().has_value());
        }
        QcWorldStorage storage(dir.path(), m_content);
        QcWorldInfo info = *storage.readInfo();
        info.generator = type;
        QVERIFY(storage.writeInfo(info).has_value());
        const auto converted = QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 2), m_content, {});
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
        QCOMPARE(converted->ringChunks, 0);
        QCOMPARE(storage.readInfo()->generatorVersion, 2);
    }

    // Stopped while blending, the world stays byte for byte as it was and no backup is left.
    void cancellingLeavesNoTrace()
    {
        QTemporaryDir dir;
        QTemporaryDir backups;
        exploredWorld(dir.path(), squareAround({0, 0}, 1));
        const QMap<QString, QByteArray> before = QcTestUtil::filesOf(dir.path());
        std::atomic<bool> stop{false};
        const QcConversionControl control{[&stop](const QcConversionControl::Step &step) {
                                              if (step.stage == QcConversionControl::Stage::Blend && step.done > 0) {
                                                  stop = true;
                                              }
                                          },
                                          [&stop] { return stop.load(); }};
        const auto converted = QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 4), m_content, control);
        QVERIFY2(converted.has_value(), qcErrorText(converted).constData());
        QVERIFY(converted->cancelled);
        QCOMPARE(QcTestUtil::filesOf(dir.path()), before);
        QVERIFY(QDir(backups.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
    }

    // A width out of its range is refused before anything happens.
    void widthsOutOfRangeAreRefused()
    {
        QVERIFY(QcBlendWidth::isValid(QcBlendWidth::standardChunks));
        QVERIFY(!QcBlendWidth::isValid(QcBlendWidth::minChunks - 1));
        QVERIFY(!QcBlendWidth::isValid(QcBlendWidth::maxChunks + 1));
        QTemporaryDir dir;
        QTemporaryDir backups;
        exploredWorld(dir.path(), squareAround({0, 0}, 0));
        const QMap<QString, QByteArray> before = QcTestUtil::filesOf(dir.path());
        QVERIFY(!QcGeneratorUpgrade::convert(request(dir.path(), backups.path(), 1), m_content, {}).has_value());
        QCOMPARE(QcTestUtil::filesOf(dir.path()), before);
    }
};

QTEST_GUILESS_MAIN(TestGeneratorBlend)
#include "test_generator_blend.moc"
