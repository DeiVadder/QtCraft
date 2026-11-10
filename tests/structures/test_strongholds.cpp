// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcTestUtil.h"
#include "worldgen/strongholds/QcStrongholdBlocks.h"
#include "worldgen/strongholds/QcStrongholdGenerator.h"
#include "worldgen/strongholds/QcStrongholdPlanner.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QSet>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

using namespace Qt::StringLiterals;

// Strongholds of the overworld from generator version 4 on (decisions E-1170 to E-1174): 128 on
// eight rings round the world's middle, a pure function of the seed; every plan has its portal
// room with twelve end portal frames and a spawner, its pieces never overlap and stay above the
// lowest floor; chunks come out the same in any order; /locate and the area for advancements find
// them; version 3 has none.
class TestStrongholds : public QObject
{
    Q_OBJECT

private:
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;
    using Kind = QcStrongholdPlan::Kind;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    const qint64 m_randomSeed = QcTestUtil::randomSeed();

    // The seeds of the tests: fixed ones and the random one (decision E-846).
    const QList<qint64> m_seeds{qint64{42}, qint64{1902}, m_randomSeed};

    [[nodiscard]] const QList<qint64> &seeds() const { return m_seeds; }

    static QList<QcChunkPos> chunksOf(QcXzRect area)
    {
        QList<QcChunkPos> chunks;
        const QcChunkPos low = QcBlockPos{area.minX, 0, area.minZ}.chunk();
        const QcChunkPos high = QcBlockPos{area.maxX, 0, area.maxZ}.chunk();
        for (int cz = low.z; cz <= high.z; ++cz) {
            for (int cx = low.x; cx <= high.x; ++cx) {
                chunks.append({cx, cz});
            }
        }
        return chunks;
    }

    static QcBlockId at(const Chunks &chunks, QcBlockPos pos)
    {
        const std::shared_ptr<QcChunk> chunk = chunks.value(pos.chunk());
        return chunk && pos.isInsideWorldHeight() ? chunk->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    }

    static const QcStrongholdPlan::Piece *portalRoomOf(const QcStrongholdPlan &plan)
    {
        const auto found = std::ranges::find(plan.pieces, Kind::PortalRoom, &QcStrongholdPlan::Piece::kind);
        return found == plan.pieces.cend() ? nullptr : &*found;
    }

    // The middle of the ring of frames of `plan` (the middle of the box round them).
    static QcBlockPos ringMiddleOf(const QcStrongholdPlan &plan)
    {
        QcBlockPos low = plan.frames.constFirst().at;
        QcBlockPos high = low;
        for (const QcStrongholdPlan::Frame &frame : plan.frames) {
            low = {std::min(low.x, frame.at.x), std::min(low.y, frame.at.y), std::min(low.z, frame.at.z)};
            high = {std::max(high.x, frame.at.x), std::max(high.y, frame.at.y), std::max(high.z, frame.at.z)};
        }
        return {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
    }

    // Whether the twelve frames of `plan` stand at one height round a 3 × 3 middle, three to a
    // side (two blocks from the ring's middle across, at most one along), each facing the middle.
    static bool framesFaceTheMiddle(const QcStrongholdPlan &plan)
    {
        if (plan.frames.size() != 12) {
            return false;
        }
        const QcBlockPos middle = ringMiddleOf(plan);
        return std::ranges::all_of(plan.frames, [&](QcStrongholdPlan::Frame frame) {
            const int dx = middle.x - frame.at.x;
            const int dz = middle.z - frame.at.z;
            const std::pair<int, int> step = QcFortressPlan::stepOf(frame.facing);
            const bool acrossX = std::abs(dx) == 2 && std::abs(dz) <= 1 && step == std::pair(dx / 2, 0);
            const bool acrossZ = std::abs(dz) == 2 && std::abs(dx) <= 1 && step == std::pair(0, dz / 2);
            return frame.at.y == middle.y && (acrossX || acrossZ);
        });
    }

private slots:
    // The 128 places lie on their rings, the same for the same seed, a different set for another.
    void placesOnTheRings()
    {
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator terrain(m_content, seed);
            const QcTerrainGenerator again(m_content, seed);
            QVERIFY(terrain.hasStrongholds());
            QSet<QPair<int, int>> places;
            for (int index = 0; index < QcStrongholdRings::count; ++index) {
                const QcBlockPos pos = terrain.strongholds().rings().positionOf(index);
                const QcStrongholdRings::Ring &ring = QcStrongholdRings::rings[static_cast<std::size_t>(QcStrongholdRings::ringOf(index))];
                const double distance = std::hypot(pos.x, pos.z);
                const double slack = QcStrongholdRings::nudgeReach * std::numbers::sqrt2 + 16.0;
                QVERIFY2(distance >= ring.minDistance - slack && distance <= ring.maxDistance + slack,
                         qPrintable(u"seed %1, stronghold %2 at %3 %4"_s.arg(seed).arg(index).arg(pos.x).arg(pos.z)));
                QCOMPARE(again.strongholds().rings().positionOf(index), pos);
                places.insert({pos.x, pos.z});
            }
            QCOMPARE(places.size(), QcStrongholdRings::count);
        }
        const QcTerrainGenerator a(m_content, 42);
        const QcTerrainGenerator b(m_content, 43);
        QVERIFY(a.strongholds().rings().positionOf(0) != b.strongholds().rings().positionOf(0));
    }

    // Every plan of the first ring has a portal room with twelve frames round it and a spawner; its
    // pieces never overlap, stay above the lowest floor and inside the bounds.
    void plansHaveAPortalRoom()
    {
        const QcStrongholdSettings &settings = m_content->worldgen().terrainSettings().strongholds;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator terrain(m_content, seed);
            for (int index = 0; index < 3; ++index) {
                const std::shared_ptr<const QcStrongholdPlan> plan = terrain.strongholds().planOf(index);
                const QString where = u"seed %1, stronghold %2"_s.arg(seed).arg(index);
                QVERIFY2(plan && plan->pieces.size() >= 3, qPrintable(where));
                QCOMPARE(plan->pieces.constFirst().kind, Kind::Start);
                QVERIFY2(portalRoomOf(*plan), qPrintable(where));
                QCOMPARE(std::ranges::count(plan->pieces, Kind::PortalRoom, &QcStrongholdPlan::Piece::kind), 1);
                QCOMPARE(plan->frames.size(), 12);
                QCOMPARE(plan->spawners.size(), 1);
                QVERIFY(portalRoomOf(*plan)->box.contains(plan->spawners.constFirst()));
                QVERIFY(plan->pieces.size() <= settings.maxPieces);
                for (qsizetype i = 0; i < plan->pieces.size(); ++i) {
                    const QcBlockBox &box = plan->pieces[i].box;
                    QVERIFY2(box.low.y >= settings.lowestFloor, qPrintable(where));
                    QVERIFY(plan->touches({box.low.x, box.low.z, box.high.x, box.high.z}));
                    for (qsizetype j = i + 1; j < plan->pieces.size(); ++j) {
                        QVERIFY2(!box.intersects(plan->pieces[j].box), qPrintable(where + u", pieces %1 and %2"_s.arg(i).arg(j)));
                    }
                }
                // A pure function of the seed.
                const QcTerrainGenerator again(m_content, seed);
                QCOMPARE(again.strongholds().planOf(index)->pieces.size(), plan->pieces.size());
                QCOMPARE(again.strongholds().planOf(index)->frames.size(), plan->frames.size());
            }
        }
    }

    // The chunks of a portal room come out the same in either order and hold the frames, the
    // spawner and lava where the plan says; the loot of the chests is in the furnishing.
    void chunksHoldThePortalRoom()
    {
        const qint64 seed = 42;
        const QcTerrainGenerator terrain(m_content, seed);
        const QcStrongholdBlocks::Resolved resolved = QcStrongholdBlocks::resolve(m_content->worldgen().terrainSettings().strongholds, *m_content);
        QVERIFY(resolved.blocks);
        const QcStrongholdBlocks &blocks = *resolved.blocks;
        const std::shared_ptr<const QcStrongholdPlan> plan = terrain.strongholds().planOf(0);
        const QcBlockBox room = portalRoomOf(*plan)->box;
        QList<QcChunkPos> positions = chunksOf({room.low.x, room.low.z, room.high.x, room.high.z});
        Chunks forwards;
        for (const QcChunkPos &pos : std::as_const(positions)) {
            forwards.insert(pos, terrain.generate(pos));
        }
        std::ranges::reverse(positions);
        const QcTerrainGenerator other(m_content, seed);
        for (const QcChunkPos &pos : std::as_const(positions)) {
            QVERIFY2(QcTestUtil::sameBlocks(*forwards.value(pos), *other.generate(pos)), qPrintable(u"chunk %1 %2"_s.arg(pos.x).arg(pos.z)));
        }

        // The frames face the middle of their ring, whose 3 × 3 cells at their height are air.
        QVERIFY(framesFaceTheMiddle(*plan));
        const QcBlockPos middle = ringMiddleOf(*plan);
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                QCOMPARE(at(forwards, middle.offset(dx, 0, dz)), qcAirBlockId);
            }
        }
        int eyes = 0;
        for (const QcStrongholdPlan::Frame &frame : plan->frames) {
            const QcBlockId expected = QcStrongholdBlocks::of(frame.eye ? blocks.framesWithEye : blocks.frames, frame.facing);
            QCOMPARE(at(forwards, frame.at), expected);
            eyes += frame.eye ? 1 : 0;
        }
        qInfo("stronghold 0 of seed 42: %lld pieces, %d of 12 frames with an eye", static_cast<long long>(plan->pieces.size()), eyes);
        QCOMPARE(at(forwards, plan->spawners.constFirst()), blocks.spawner);

        bool lava = false;
        for (int y = room.low.y; y <= room.high.y && !lava; ++y) {
            for (int z = room.low.z; z <= room.high.z && !lava; ++z) {
                for (int x = room.low.x; x <= room.high.x && !lava; ++x) {
                    lava = at(forwards, {x, y, z}) == blocks.lava;
                }
            }
        }
        QVERIFY(lava);

        const QcChunkFurnishing furnishing = terrain.furnishingIn(plan->spawners.constFirst().chunk());
        QVERIFY(std::ranges::any_of(furnishing.spawners, [&](const auto &spawner) { return spawner.at == plan->spawners.constFirst(); }));
    }

    // Every chest of the plans is furnished with its stronghold loot in its own chunk.
    void chestsGetTheirLoot()
    {
        int chests = 0;
        for (const qint64 seed : seeds()) {
            const QcTerrainGenerator terrain(m_content, seed);
            for (int index = 0; index < 3; ++index) {
                const std::shared_ptr<const QcStrongholdPlan> plan = terrain.strongholds().planOf(index);
                for (const QcStrongholdPlan::Chest &chest : plan->chests) {
                    const QcChunkFurnishing furnishing = terrain.furnishingIn(chest.at.chunk());
                    const auto found = std::ranges::find_if(furnishing.chests, [&](const auto &c) { return c.at == chest.at; });
                    QVERIFY2(found != furnishing.chests.cend(), qPrintable(u"seed %1, stronghold %2"_s.arg(seed).arg(index)));
                    QVERIFY2(found->lootTable.contains(u"stronghold_"_s), qPrintable(found->lootTable));
                    ++chests;
                }
            }
        }
        QVERIFY(chests > 0);
    }

    // /locate stronghold names the nearest stronghold's starting staircase; the area for the
    // advancement knows its pieces once planned near.
    void locateAndArea()
    {
        const QcTerrainGenerator terrain(m_content, 42);
        QVERIFY(terrain.locatableNames().contains(u"stronghold"_s));
        const QcLocateResult result = terrain.locate(u"stronghold", {0, 64, 0}, 2048);
        QVERIFY(result.place);
        const std::shared_ptr<const QcStrongholdPlan> plan = terrain.strongholds().planOf(terrain.strongholds().rings().nearest({0, 64, 0}));
        QCOMPARE(result.place->at, plan->middle());
        QVERIFY(terrain.strongholds().rings().nearest({0, 64, 0}) < 3);

        const QcBlockPos inside = plan->middle();
        const QcStructureSites sites = terrain.structureSitesNear(inside, 32);
        Q_UNUSED(sites);
        QCOMPARE(terrain.structureAt(inside), std::optional<QString>(u"stronghold"_s));
    }

    // All 128 strongholds of the fixed seeds have exactly one portal room, whose twelve frames
    // face the middle of their ring, and stay within the generator's margin round their start.
    void everyStrongholdHasOnePortalRoom()
    {
        const QcStrongholdSettings &settings = m_content->worldgen().terrainSettings().strongholds;
        for (const qint64 seed : {qint64{42}, qint64{1902}}) {
            const QcTerrainGenerator terrain(m_content, seed);
            const int extent = terrain.strongholds().extent();
            for (int index = 0; index < QcStrongholdRings::count; ++index) {
                const std::shared_ptr<const QcStrongholdPlan> plan = terrain.strongholds().planOf(index);
                const QString where = u"seed %1, stronghold %2"_s.arg(seed).arg(index);
                QVERIFY2(std::ranges::count(plan->pieces, Kind::PortalRoom, &QcStrongholdPlan::Piece::kind) == 1, qPrintable(where));
                QVERIFY2(framesFaceTheMiddle(*plan), qPrintable(where));
                QVERIFY2(plan->pieces.size() <= settings.maxPieces, qPrintable(where));
                const QcBlockPos start = terrain.strongholds().rings().positionOf(index);
                const QcXzRect &bounds = plan->bounds;
                QVERIFY2(bounds.minX >= start.x - extent && bounds.maxX <= start.x + extent && bounds.minZ >= start.z - extent
                             && bounds.maxZ <= start.z + extent,
                         qPrintable(where));
            }
        }
    }

    // Where growing leaves out the portal room (here: at most four pieces, so it never grows
    // deep enough), the planner still places exactly one, never overlapping and within a portal
    // room's depth beyond the reach.
    void portalRoomWhereGrowingLeftItOut()
    {
        QcStrongholdSettings settings = m_content->worldgen().terrainSettings().strongholds;
        settings.maxPieces = 4;
        const QcStrongholdPlanner planner(settings, [](int, int) { return 100; }, 42);
        const QcBlockPos start{0, 30, 0};
        const int beyond = settings.reach + QcStrongholdPlan::shapeOf(Kind::PortalRoom, false).depth + 1;
        for (int index = 0; index < 32; ++index) {
            const std::shared_ptr<const QcStrongholdPlan> plan = planner.planAt(index, start);
            const QString where = u"stronghold %1"_s.arg(index);
            QVERIFY2(std::ranges::count(plan->pieces, Kind::PortalRoom, &QcStrongholdPlan::Piece::kind) == 1, qPrintable(where));
            QVERIFY2(plan->pieces.size() <= settings.maxPieces, qPrintable(where));
            QVERIFY2(framesFaceTheMiddle(*plan), qPrintable(where));
            QCOMPARE(plan->spawners.size(), 1);
            QVERIFY(plan->bounds.minX >= -beyond && plan->bounds.maxX <= beyond && plan->bounds.minZ >= -beyond && plan->bounds.maxZ <= beyond);
            const QcStrongholdPlan::Piece &room = *portalRoomOf(*plan);
            QVERIFY2(std::ranges::any_of(plan->pieces, [&](const QcStrongholdPlan::Piece &piece) { return piece.opened != 0; }), qPrintable(where));
            for (const QcStrongholdPlan::Piece &piece : plan->pieces) {
                QVERIFY(&piece == &room || !piece.box.intersects(room.box));
            }
        }
    }

    // In an upgraded world whose older chunks leave out the nearest stronghold, /locate names the
    // nearest one the world builds.
    void locateSkipsStrongholdsLeftOut()
    {
        QcTerrainGenerator terrain(m_content, 42);
        const QcBlockPos from{0, 64, 0};
        const QcStrongholdGenerator &strongholds = terrain.strongholds();
        const int nearest = strongholds.rings().nearest(from);
        const QcXzRect leftOut = strongholds.planOf(nearest)->bounds;
        terrain.setStructureFilter([leftOut](QcXzRect columns) { return !(columns == leftOut); });

        const std::optional<int> next = strongholds.rings().nearest(from, [nearest](int index) { return index != nearest; });
        QVERIFY(next && *next != nearest);
        const auto distance = [from](QcBlockPos pos) { return std::hypot(pos.x - from.x, pos.z - from.z); };
        QVERIFY(distance(strongholds.rings().positionOf(*next)) >= distance(strongholds.rings().positionOf(nearest)));
        const QcLocateResult result = terrain.locate(u"stronghold", from, 2048);
        QVERIFY(result.place);
        QCOMPARE(result.place->at, strongholds.planOf(*next)->middle());
        QVERIFY(!strongholds.rings().nearest(from, [](int) { return false; }));
    }

    // Version 3 (frozen with release 1.1.0) has no strongholds: no frames in its chunks, no place
    // for /locate, no area.
    void noneBeforeVersionFour()
    {
        QcTerrainGenerator terrain(m_content, 42);
        const std::shared_ptr<const QcStrongholdPlan> plan = terrain.strongholds().planOf(0);
        terrain.setVersion(3);
        QVERIFY(!terrain.hasStrongholds());
        QVERIFY(!terrain.locate(u"stronghold", {0, 64, 0}, 2048).place);
        const QcStrongholdBlocks::Resolved resolved = QcStrongholdBlocks::resolve(m_content->worldgen().terrainSettings().strongholds, *m_content);
        const QcStrongholdPlan::Frame &frame = plan->frames.constFirst();
        const std::shared_ptr<QcChunk> chunk = terrain.generate(frame.at.chunk());
        const QcBlockId block = chunk->block(frame.at.localX(), frame.at.y, frame.at.localZ());
        QVERIFY(std::ranges::find(resolved.blocks->frames, block) == resolved.blocks->frames.cend());
        QVERIFY(std::ranges::find(resolved.blocks->framesWithEye, block) == resolved.blocks->framesWithEye.cend());
        const QcChunkFurnishing furnishing = terrain.furnishingIn(plan->spawners.constFirst().chunk());
        QVERIFY(std::ranges::none_of(furnishing.spawners, [&](const auto &spawner) { return spawner.at == plan->spawners.constFirst(); }));
        QCOMPARE(terrain.structureAt(plan->middle()).value_or(QString()), QString());
    }

};

QTEST_GUILESS_MAIN(TestStrongholds)
#include "test_strongholds.moc"
