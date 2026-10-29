// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcThreadCpuClock.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/mineshafts/QcMineshaftNetwork.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QQueue>
#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;

// Mine shafts of the terrain generator (decision E-570): plans are a pure function of seed and
// region, chunks come out the same in any order, every level is reached from the central room
// through the corridors and staircases (walked cell by cell in the generated blocks), bridges stand
// on pillars that reach the ground, some mine shafts come up to the surface, rooms face their
// corridors, and spawners, lights and chest minecarts are where the plan says.
class TestMineshafts : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 382;
    static constexpr int regions = 4;
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // Generated once: the plans of the test regions and the chunks of the first few.
    std::unique_ptr<QcTerrainGenerator> m_terrain;
    QList<std::shared_ptr<const QcMineshaftPlan>> m_plans;
    struct Built
    {
        std::shared_ptr<const QcMineshaftPlan> plan;
        Chunks chunks;
    };
    QList<Built> m_built;

    QList<std::shared_ptr<const QcMineshaftPlan>> plans(const QcTerrainGenerator &terrain) const
    {
        QList<std::shared_ptr<const QcMineshaftPlan>> found;
        for (int rz = -regions; rz <= regions; ++rz) {
            for (int rx = -regions; rx <= regions; ++rx) {
                if (std::shared_ptr<const QcMineshaftPlan> plan = terrain.mineshafts().planOf(rx, rz)) {
                    found.append(std::move(plan));
                }
            }
        }
        return found;
    }

    static QList<QcChunkPos> chunksOf(const QcMineshaftPlan &plan)
    {
        QList<QcChunkPos> chunks;
        for (int cx = QcBlockPos{plan.minX, 0, 0}.chunk().x; cx <= QcBlockPos{plan.maxX, 0, 0}.chunk().x; ++cx) {
            for (int cz = QcBlockPos{0, 0, plan.minZ}.chunk().z; cz <= QcBlockPos{0, 0, plan.maxZ}.chunk().z; ++cz) {
                chunks.append({cx, cz});
            }
        }
        return chunks;
    }

    static Chunks generate(const QcWorldGenerator &terrain, const QList<QcChunkPos> &positions)
    {
        Chunks chunks;
        for (const QcChunkPos &pos : positions) {
            chunks.insert(pos, terrain.generate(pos));
        }
        return chunks;
    }

    static QcBlockId at(const Chunks &chunks, QcBlockPos pos)
    {
        const std::shared_ptr<QcChunk> chunk = chunks.value(pos.chunk());
        return chunk && pos.isInsideWorldHeight() ? chunk->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    }

    QcBlockId id(const QString &name) const { return *m_content->stateId(name); }
    const QcBlockDefinition &block(QcBlockId block) const { return m_content->block(block); }
    bool solid(QcBlockId id) const { return block(id).solid && !block(id).liquid; }
    QString nameOf(QcBlockId id) const { return block(id).name; }

    // The cells a move may lead to from `here` (`back`: come from): along a ladder, a step to the
    // side one up or down to three.
    static QList<QcBlockPos> neighbours(QcBlockPos here, bool back)
    {
        QList<QcBlockPos> next{here.offset(0, 1, 0), here.offset(0, -1, 0)};
        for (const auto &[dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            for (int dy = 1; dy >= -3; --dy) {
                next.append(here.offset(dx, back ? -dy : dy, dz));
            }
        }
        return next;
    }

    // Cells a body can walk to from the room's middle (`back`: from which it walks to the room's
    // middle), moving only through the plan's open cells: level steps, one up (with room above the
    // head), down any drop of up to three, and up and down ladders.
    QSet<QcBlockPos> walk(const QcMineshaftPlan &plan, const Chunks &chunks, bool back = false) const
    {
        QSet<QcBlockPos> open;
        plan.forEachOpenCell([&open](QcBlockPos cell) { open.insert(cell); });
        // A ladder is solid, but thin: one climbs inside its cell.
        const auto free = [&](QcBlockPos cell) { return !block(at(chunks, cell)).solid || block(at(chunks, cell)).climbable; };
        const auto climbable = [&](QcBlockPos cell) { return block(at(chunks, cell)).climbable; };
        const auto stands = [&](QcBlockPos feet) {
            return open.contains(feet) && free(feet) && free(feet.offset(0, 1, 0)) && (solid(at(chunks, feet.offset(0, -1, 0))) || climbable(feet));
        };
        // A move from `from` to `to`, both standing.
        const auto moves = [&](QcBlockPos from, QcBlockPos to) {
            const int dy = to.y - from.y;
            const bool step = std::abs(to.x - from.x) + std::abs(to.z - from.z) == 1 && dy >= -3 && dy <= 1 && (dy < 1 || free(from.offset(0, 2, 0)));
            const bool climb = to.x == from.x && to.z == from.z && std::abs(dy) == 1 && climbable(from);
            return step || climb;
        };

        QSet<QcBlockPos> reached{plan.nodes.constFirst().centre};
        QQueue<QcBlockPos> queue;
        queue.enqueue(plan.nodes.constFirst().centre);
        while (!queue.isEmpty()) {
            const QcBlockPos here = queue.dequeue();
            const QList<QcBlockPos> next = neighbours(here, back);
            for (const QcBlockPos &cell : next) {
                if (!reached.contains(cell) && stands(cell) && (back ? moves(cell, here) : moves(here, cell))) {
                    reached.insert(cell);
                    queue.enqueue(cell);
                }
            }
        }
        return reached;
    }

    // No liquid beside (or above) an opened cell of the plan, no sand or gravel above it; returns how
    // many open cells were seen.
    int checkSealed(const QcMineshaftPlan &plan, const Chunks &chunks) const
    {
        int seen = 0;
        plan.forEachOpenCell([&](QcBlockPos cell) {
            if (!chunks.contains(cell.chunk()) || QTest::currentTestFailed()) {
                return;
            }
            ++seen;
            for (const auto &[dx, dy, dz] : {std::tuple{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 0, 0}}) {
                const QcBlockPos next = cell.offset(dx, dy, dz);
                const bool falls = dy == 1 && block(at(chunks, next)).falls;
                QVERIFY2(!chunks.contains(next.chunk()) || (!block(at(chunks, next)).liquid && !falls),
                         qPrintable(u"%1 at %2 %3 %4"_s.arg(nameOf(at(chunks, next))).arg(next.x).arg(next.y).arg(next.z)));
            }
        });
        return seen;
    }

    // Every node is reached from the room through the corridors.
    static bool connected(const QcMineshaftPlan &plan)
    {
        QSet<int> reached{0};
        bool grew = true;
        while (grew) {
            grew = false;
            for (const QcMineshaftPlan::Corridor &corridor : plan.corridors) {
                if (reached.contains(corridor.from) != reached.contains(corridor.to)) {
                    reached.insert(corridor.from);
                    reached.insert(corridor.to);
                    grew = true;
                }
            }
        }
        return reached.size() == plan.nodes.size();
    }

    // One cell lower each step, ground under every step, four cells high (up a step needs a jump).
    void checkStaircase(const QcMineshaftPlan &plan, const Chunks &chunks, const QcMineshaftPlan::Corridor &corridor) const
    {
        QCOMPARE(corridor.cell(0).y, plan.nodes[corridor.from].centre.y - 1);
        QCOMPARE(corridor.cell(corridor.length - 1).y, plan.nodes[corridor.to].centre.y);
        for (int i = 0; i < 3 * corridor.length; ++i) {
            const QcBlockPos feet = corridor.cell(i / 3, i % 3 - 1);
            QVERIFY2(solid(at(chunks, feet.offset(0, -1, 0))), qPrintable(nameOf(at(chunks, feet.offset(0, -1, 0)))));
            for (int dy = 0; dy < 4; ++dy) {
                QVERIFY(!block(at(chunks, feet.offset(0, dy, 0))).solid);
            }
        }
    }

    // Every pillar ends on solid ground; returns how many there are.
    int checkPillars(const QcMineshaftPlan &plan, const Chunks &chunks) const
    {
        const QcBlockId post = id(u"qtcraft:oak_log"_s);
        int pillars = 0;
        for (const QcBlockPos &floor : plan.pillars) {
            QcBlockPos bottom = floor.offset(0, -1, 0);
            pillars += at(chunks, bottom) == post ? 1 : 0;
            while (at(chunks, bottom) == post) {
                bottom = bottom.offset(0, -1, 0);
            }
            if (bottom != floor.offset(0, -1, 0) && !solid(at(chunks, bottom))) {
                qWarning().noquote() << u"pillar at %1 %2 %3 ends on %4"_s.arg(bottom.x).arg(bottom.y).arg(bottom.z).arg(nameOf(at(chunks, bottom)));
                return -1;
            }
        }
        return pillars;
    }

    // A floor cell of the corridor carried within `reach` cells (taxicab): natural ground, or planks
    // on something solid (a pillar).
    bool carriedNear(const Chunks &chunks, const QcMineshaftPlan::Corridor &corridor, int along, int lateral, int reach) const
    {
        const QcBlockId planks = id(u"qtcraft:oak_planks"_s);
        for (int a = std::max(0, along - reach); a <= std::min(corridor.length - 1, along + reach); ++a) {
            for (int l = -1; l <= 1; ++l) {
                const QcBlockPos floor = corridor.cell(a, l).offset(0, -1, 0);
                const bool ground = at(chunks, floor) != planks && solid(at(chunks, floor));
                const bool pillar = at(chunks, floor) == planks && solid(at(chunks, floor.offset(0, -1, 0)));
                if (std::abs(a - along) + std::abs(l - lateral) <= reach && (ground || pillar)) {
                    return true;
                }
            }
        }
        return false;
    }

    void checkEntrance(const QcMineshaftPlan &plan, const Chunks &chunks) const
    {
        const QcBlockPos top = plan.entrance->top;
        for (int y = plan.nodes.constFirst().centre.y; y <= top.y + 2; ++y) {
            QVERIFY2(block(at(chunks, {top.x, y, top.z})).climbable, qPrintable(u"no ladder at %1"_s.arg(y)));
            QVERIFY(solid(at(chunks, {top.x - 1, y, top.z})));
        }
        const QcBlockId post = id(u"qtcraft:oak_log"_s);
        for (int i = 0; i < 27; ++i) {
            const QcBlockPos cell = top.offset(i % 3 - 1, 1 + i / 9, i / 3 % 3 - 1);
            const bool corner = cell.x != top.x && cell.z != top.z;
            const bool behind = cell.x == top.x - 1 && cell.z == top.z;
            if (corner) {
                QCOMPARE(nameOf(at(chunks, cell)), nameOf(post));
            } else if (!behind && cell != top.offset(0, cell.y - top.y, 0)) {
                QVERIFY2(at(chunks, cell) == qcAirBlockId, qPrintable(nameOf(at(chunks, cell))));
            }
        }

        // From the ring (east, north or south of the ladder) a step onto the ground beside it, at
        // most one up or three down.
        bool out = false;
        for (const auto &[dx, dz] : {std::pair{1, 0}, {0, -1}, {0, 1}}) {
            const QcBlockPos beyond = top.offset(2 * dx, 0, 2 * dz);
            for (int y = top.y - 2; y <= top.y + 2 && !out; ++y) {
                const QcBlockPos feet{beyond.x, y, beyond.z};
                out = solid(at(chunks, feet.offset(0, -1, 0))) && !block(at(chunks, feet)).solid && !block(at(chunks, feet.offset(0, 1, 0))).solid;
            }
        }
        QVERIFY(out);
    }

    // Taxicab distance from a cell to a spawner's spawn area.
    static int distanceToArea(QcBlockPos pos, const QcMineshaftPlan::Spawner &area)
    {
        const auto axis = [](int v, int low, int high) { return v < low ? low - v : (v > high ? v - high : 0); };
        return axis(pos.x, area.areaLow.x, area.areaHigh.x) + axis(pos.y, area.areaLow.y, area.areaHigh.y) + axis(pos.z, area.areaLow.z, area.areaHigh.z);
    }

    // The furnishing names the spawner's kind, the plan has it with cobwebs around.
    void checkSpawner(const QcMineshaftPlan &plan, const QcMineshaftPlan::Spawner &placed) const
    {
        const QcChunkFurnishing furnishing = m_terrain->furnishingIn(placed.at.chunk());
        QVERIFY(std::ranges::any_of(furnishing.spawners, [&](const QcChunkFurnishing::Spawner &s) {
            return s.at == placed.at && s.mob == u"qtcraft:cave_spider"_s && !s.confined;
        }));
        const QcBlockId spawner = id(u"qtcraft:spawner"_s);
        const QcBlockId cobweb = id(u"qtcraft:cobweb"_s);
        QVERIFY(std::ranges::any_of(plan.blocks, [&](QcMineshaftPlan::Placed p) { return p.at == placed.at && p.block == spawner; }));
        const auto webs = std::ranges::count_if(plan.blocks, [&](QcMineshaftPlan::Placed p) {
            return p.block == cobweb && std::abs(p.at.x - placed.at.x) + std::abs(p.at.z - placed.at.z) <= 3;
        });
        QVERIFY2(webs >= 3, qPrintable(QString::number(webs)));
    }

    // The furnishing has a chest minecart with the loot table on the cart's rail.
    void checkCart(const QcMineshaftPlan &plan, QcBlockPos cart) const
    {
        const QcChunkFurnishing furnishing = m_terrain->furnishingIn(cart.chunk());
        QVERIFY(std::ranges::any_of(furnishing.carts, [&](const QcChunkFurnishing::Cart &c) {
            return c.at == cart && c.item == u"qtcraft:chest_minecart"_s && c.lootTable == u"qtcraft:mineshaft_cart"_s;
        }));
        QVERIFY(std::ranges::any_of(plan.blocks, [&](QcMineshaftPlan::Placed p) { return p.at == cart && block(p.block).rail; }));
    }

private slots:
    void initTestCase()
    {
        m_terrain = std::make_unique<QcTerrainGenerator>(m_content, seed);
        m_plans = plans(*m_terrain);
        // Built chunks of the first plans with two levels or more.
        for (const auto &plan : std::as_const(m_plans)) {
            if (plan->levels >= 2 && m_built.size() < 3) {
                m_built.append({plan, generate(*m_terrain, chunksOf(*plan))});
            }
        }
    }

    void plansAreAPureFunctionOfSeedAndRegion()
    {
        const QcTerrainGenerator second(m_content, seed);
        const QList<std::shared_ptr<const QcMineshaftPlan>> other = plans(second);
        QVERIFY2(m_plans.size() >= 12, qPrintable(QString::number(m_plans.size())));
        QCOMPARE(other.size(), m_plans.size());
        for (qsizetype i = 0; i < m_plans.size(); ++i) {
            const QcMineshaftPlan &a = *m_plans[i];
            const QcMineshaftPlan &b = *other[i];
            QCOMPARE(a.nodes.size(), b.nodes.size());
            QCOMPARE(a.corridors.size(), b.corridors.size());
            for (qsizetype n = 0; n < a.nodes.size(); ++n) {
                QCOMPARE(a.nodes[n].centre, b.nodes[n].centre);
            }
            QCOMPARE(a.blocks.size(), b.blocks.size());
            for (qsizetype n = 0; n < a.blocks.size(); ++n) {
                QCOMPARE(a.blocks[n].at, b.blocks[n].at);
                QCOMPARE(a.blocks[n].block, b.blocks[n].block);
            }
            QCOMPARE(a.carts, b.carts);
            QCOMPARE(a.entrance.has_value(), b.entrance.has_value());
            QCOMPARE(a.entrance ? a.entrance->top : QcBlockPos{}, b.entrance ? b.entrance->top : QcBlockPos{});
            QCOMPARE(a.rooms.size(), b.rooms.size());
        }

        // Several levels, staying inside the region and the heights of the settings.
        const QcMineshaftSettings &settings = m_content->worldgen().terrainSettings().mineshafts;
        int levels = 0;
        int nodes = 0;
        for (const auto &plan : std::as_const(m_plans)) {
            levels += plan->levels;
            nodes += static_cast<int>(plan->nodes.size());
            const int region = settings.regionSize;
            QVERIFY(plan->minX >= plan->regionX * region && plan->maxX < (plan->regionX + 1) * region);
            QVERIFY(plan->minZ >= plan->regionZ * region && plan->maxZ < (plan->regionZ + 1) * region);
            for (const QcMineshaftPlan::Node &node : plan->nodes) {
                QVERIFY(node.centre.y >= settings.minY && node.centre.y <= settings.maxY);
            }
        }
        int entrances = 0;
        int rooms = 0;
        int carts = 0;
        int spawners = 0;
        for (const auto &plan : std::as_const(m_plans)) {
            entrances += plan->entrance ? 1 : 0;
            rooms += static_cast<int>(plan->rooms.size());
            carts += static_cast<int>(plan->carts.size());
            spawners += static_cast<int>(plan->spawners.size());
        }
        qInfo().noquote() << u"%1 mine shafts in %2 regions, %3 levels and %4 nodes on average; %5 entrances, %6 rooms, %7 carts, %8 spawners"_s
                                 .arg(m_plans.size())
                                 .arg((2 * regions + 1) * (2 * regions + 1))
                                 .arg(static_cast<double>(levels) / m_plans.size(), 0, 'f', 2)
                                 .arg(static_cast<double>(nodes) / m_plans.size(), 0, 'f', 1)
                                 .arg(entrances)
                                 .arg(rooms)
                                 .arg(carts)
                                 .arg(spawners);
        QVERIFY(double(levels) / m_plans.size() >= 2.0);
    }

    void chunksAreTheSameInAnyOrder()
    {
        int checked = 0;
        for (const auto &plan : std::as_const(m_plans)) {
            if (checked == 2) {
                break;
            }
            ++checked;
            // A fresh generator, chunks in the opposite order.
            const QcTerrainGenerator second(m_content, seed);
            QList<QcChunkPos> chunks = chunksOf(*plan);
            const Chunks forward = generate(*m_terrain, chunks);
            std::ranges::reverse(chunks);
            for (const QcChunkPos &pos : std::as_const(chunks)) {
                const std::shared_ptr<QcChunk> other = second.generate(pos);
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    for (int i = 0; i < QcWorld::chunkSize * QcWorld::chunkSize; ++i) {
                        QCOMPARE(other->block(i % 16, y, i / 16), forward.value(pos)->block(i % 16, y, i / 16));
                    }
                }
            }
            // The plan's blocks are there.
            for (const QcMineshaftPlan::Placed &placed : plan->blocks) {
                QCOMPARE(at(forward, placed.at), placed.block);
            }
        }
        QCOMPARE(checked, 2);
    }

    void levelsAreConnected()
    {
        // In the network: every node is reached from the room, every level below by a staircase.
        for (const auto &plan : std::as_const(m_plans)) {
            QVERIFY(connected(*plan));
            for (int level = 1; level < plan->levels; ++level) {
                QVERIFY(std::ranges::any_of(plan->corridors, [&](const QcMineshaftPlan::Corridor &corridor) {
                    return corridor.stairs() && plan->nodes[corridor.to].level == level && plan->nodes[corridor.from].level == level - 1;
                }));
            }
        }

        // In the blocks: a body walks from the room to the middle of every node, on every level
        // (rooms from structures aside).
        QVERIFY(m_built.size() >= 2);
        for (const Built &built : std::as_const(m_built)) {
            const QSet<QcBlockPos> reached = walk(*built.plan, built.chunks);
            // And back: no cell reached is a trap.
            const QSet<QcBlockPos> leaving = walk(*built.plan, built.chunks, true);
            for (const QcBlockPos &cell : reached) {
                QVERIFY2(leaving.contains(cell), qPrintable(u"stuck at %1 %2 %3"_s.arg(cell.x).arg(cell.y).arg(cell.z)));
            }
            for (qsizetype n = 0; n < built.plan->nodes.size(); ++n) {
                const QcMineshaftPlan::Node &node = built.plan->nodes[n];
                const bool room = std::ranges::any_of(built.plan->rooms, [n](const QcMineshaftPlan::Room &r) { return r.node == n; });
                QVERIFY2(room || reached.contains(node.centre),
                         qPrintable(u"node %1 %2 %3 (level %4) not reached"_s.arg(node.centre.x).arg(node.centre.y).arg(node.centre.z).arg(node.level)));
            }
        }
    }

    void staircasesAreWalkable()
    {
        int stairs = 0;
        for (const Built &built : std::as_const(m_built)) {
            for (const QcMineshaftPlan::Corridor &corridor : built.plan->corridors) {
                if (corridor.stairs()) {
                    ++stairs;
                    checkStaircase(*built.plan, built.chunks, corridor);
                    if (QTest::currentTestFailed()) {
                        return;
                    }
                }
            }
        }
        QVERIFY(stairs >= 2);
    }

    void bridgesStandOnPillars()
    {
        // Every pillar reaches solid ground, and every floor cell of a corridor has a carried one (on
        // ground, or on a pillar) within a support's spacing.
        const int spacing = m_content->worldgen().terrainSettings().mineshafts.supportSpacing;
        int bridges = 0;
        int pillars = 0;
        for (const auto &plan : std::as_const(m_plans)) {
            if (!std::ranges::any_of(plan->corridors, &QcMineshaftPlan::Corridor::bridge) || bridges >= 3) {
                continue;
            }
            ++bridges;
            const Chunks chunks = generate(*m_terrain, chunksOf(*plan));
            const int found = checkPillars(*plan, chunks);
            QVERIFY(found >= 0);
            pillars += found;
            QVERIFY(checkSealed(*plan, chunks) > 0);
            QVERIFY(!QTest::currentTestFailed());
            for (const QcMineshaftPlan::Corridor &corridor : plan->corridors) {
                for (int i = 0; i < 3 * corridor.length; ++i) {
                    const QcBlockPos floor = corridor.cell(i / 3, i % 3 - 1).offset(0, -1, 0);
                    QVERIFY2(carriedNear(chunks, corridor, i / 3, i % 3 - 1, spacing), qPrintable(u"floor at %1 %2 %3 hangs free"_s.arg(floor.x).arg(floor.y).arg(floor.z)));
                }
            }
        }
        QVERIFY(bridges >= 1);
        QVERIFY(pillars >= 1);
    }

    // Liquids of the caves and lava lakes stay where they were, cut off by `seal` across chunk
    // borders: nothing liquid beside or above any cell a mine shaft opened.
    void liquidsAreSealed()
    {
        int seen = 0;
        for (const Built &built : std::as_const(m_built)) {
            seen += checkSealed(*built.plan, built.chunks);
            QVERIFY(!QTest::currentTestFailed());
        }
        QVERIFY(seen > 1000);

        // A bridge across the border of two chunks full of lava under sand: each chunk seals its own
        // side.
        const QcMineshaftPlanner &planner = m_terrain->mineshafts().planner();
        const QcMineshaftBuilder builder(planner.blocks(), planner.settings().maxPillar);
        QcMineshaftPlan plan;
        QcMineshaftPlan::Corridor bridge;
        bridge.start = {10, 40, 8};
        bridge.dx = 1;
        bridge.length = 12;
        bridge.bridge = true;
        plan.corridors.append(bridge);
        const QcBlockId lava = id(u"qtcraft:lava"_s);
        const QcBlockId sand = id(u"qtcraft:sand"_s);
        Chunks chunks;
        for (int cx = 0; cx <= 1; ++cx) {
            auto chunk = std::make_shared<QcChunk>(QcChunkPos{cx, 0});
            for (int i = 0; i < 20 * QcWorld::chunkSize * QcWorld::chunkSize; ++i) {
                const int y = 30 + i / (QcWorld::chunkSize * QcWorld::chunkSize);
                chunk->setBlock(i % QcWorld::chunkSize, y, i / QcWorld::chunkSize % QcWorld::chunkSize, y < 43 ? lava : sand);
            }
            builder.build(*chunk, plan);
            chunks.insert(chunk->pos(), chunk);
        }
        QCOMPARE(checkSealed(plan, chunks), 3 * 3 * bridge.length);
        for (int along = 0; along < bridge.length; ++along) {
            QCOMPARE(nameOf(at(chunks, bridge.cell(along).offset(0, -1, 0))), u"qtcraft:oak_planks"_s);
        }
    }

    void someComeUpToTheSurface()
    {
        // A share of the mine shafts has an entrance, and one climbs it from the room to the top.
        const QList<std::shared_ptr<const QcMineshaftPlan>> withEntrance = [this] {
            QList<std::shared_ptr<const QcMineshaftPlan>> found;
            std::ranges::copy_if(m_plans, std::back_inserter(found), [](const auto &plan) { return plan->entrance.has_value(); });
            return found;
        }();
        QVERIFY2(withEntrance.size() * 100 >= m_plans.size() * 15, qPrintable(u"%1 of %2"_s.arg(withEntrance.size()).arg(m_plans.size())));
        for (const Built &built : std::as_const(m_built)) {
            if (built.plan->entrance) {
                QVERIFY(walk(*built.plan, built.chunks).contains(built.plan->entrance->out));
            }
        }

        // Every entrance, built after the trees, cacti and pools of its chunks: a ladder from the
        // room's floor to two cells above the ground with a solid wall behind it, the headframe's
        // posts, its box free of anything else, and a way out onto the ground beside it. Inclined
        // tunnels (decision E-895) are walked in test_mineshaft_inclines.
        for (const auto &plan : std::as_const(withEntrance)) {
            const QcBlockPos top = plan->entrance->top;
            QCOMPARE(top.y, m_terrain->groundTop(top.x, top.z, m_terrain->column(top.x, top.z)));
            if (plan->entrance->kind != QcMineshaftPlan::Entrance::Kind::Ladder) {
                continue;
            }
            QList<QcChunkPos> positions;
            for (int i = 0; i < 25; ++i) {
                if (const QcChunkPos pos = top.offset(i % 5 - 2, 0, i / 5 - 2).chunk(); !positions.contains(pos)) {
                    positions.append(pos);
                }
            }
            const Chunks chunks = generate(*m_terrain, positions);
            checkEntrance(*plan, chunks);
            if (QTest::currentTestFailed()) {
                qWarning().noquote() << u"entrance at %1 %2 %3"_s.arg(top.x).arg(top.y).arg(top.z);
                return;
            }
        }
    }

    void spawnersCartsAndLightsAreFurnished()
    {
        const QcMineshaftSettings &settings = m_content->worldgen().terrainSettings().mineshafts;
        int carts = 0;
        int spawners = 0;
        for (const auto &plan : std::as_const(m_plans)) {
            for (const QcMineshaftPlan::Spawner &placed : plan->spawners) {
                ++spawners;
                checkSpawner(*plan, placed);
            }
            for (const QcBlockPos &cart : plan->carts) {
                ++carts;
                checkCart(*plan, cart);
            }
            QVERIFY(!QTest::currentTestFailed());
            // Lights keep away from every spawn area: a torch (14) leaves at most 6 there.
            for (const QcMineshaftPlan::Placed &placed : plan->blocks) {
                const bool lit = block(placed.block).lightLevel > 0;
                QVERIFY(!lit || std::ranges::all_of(plan->spawners, [&](const auto &area) { return distanceToArea(placed.at, area) >= settings.lightDistance; }));
            }
        }
        QVERIFY(spawners >= 10);
        QVERIFY(carts >= 10);

        // The rail under a cart is in the generated blocks.
        for (const Built &built : std::as_const(m_built)) {
            for (const QcBlockPos &cart : built.plan->carts) {
                QVERIFY(block(at(built.chunks, cart)).rail);
            }
        }
    }

    void roomsFaceTheirCorridors()
    {
        // The door (the middle of the structure's north side, one above its floor) opens onto the
        // middle lane of the corridor to the dead end.
        int rooms = 0;
        for (const auto &plan : std::as_const(m_plans)) {
            for (const QcMineshaftPlan::Room &room : plan->rooms) {
                ++rooms;
                const QcStructure &structure = room.placement.structure();
                const QcBlockPos door = room.placement.toWorld(QcBlockPos{structure.sizeX() / 2, 1, 0});
                const auto corridor = std::ranges::find_if(plan->corridors, [&](const auto &c) { return c.from == room.node || c.to == room.node; });
                QVERIFY(corridor != plan->corridors.end());
                bool lane = false;
                for (int along = 0; along < corridor->length; ++along) {
                    const QcBlockPos cell = corridor->cell(along);
                    lane = lane || (cell == door) || (std::abs(cell.x - door.x) + std::abs(cell.z - door.z) == 1 && cell.y == door.y);
                }
                QVERIFY2(lane, qPrintable(u"door at %1 %2 %3"_s.arg(door.x).arg(door.y).arg(door.z)));
                const Chunks chunks = generate(*m_terrain, {door.chunk()});
                QVERIFY(!block(at(chunks, door)).solid);
                QVERIFY(!block(at(chunks, door.offset(0, 1, 0))).solid);
                if (rooms == 4) {
                    return;
                }
            }
        }
        QVERIFY(rooms >= 2);
    }

    // The showcase's mine shaft station (definition of done): a confined cave spider spawner, a
    // chest minecart with contents on its rail, a ladder from the lower level up through a
    // headframe, a staircase between the levels and a bridge on pillars down to the ground.
    void theShowcaseStationShowsAMineShaft()
    {
        const QcShowcaseGenerator generator(m_content);
        const auto station = std::ranges::find_if(generator.plan().placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name() == u"qtcraft:mineshaft_station"_s;
        });
        QVERIFY(station != generator.plan().placements.end());
        const QcStructure &structure = station->structure();
        QCOMPARE(structure.spawners.size(), 1);
        const QcBlockPos spawner = station->toWorld(structure.spawners[0].at);
        QVERIFY(std::ranges::any_of(generator.furnishingIn(spawner.chunk()).spawners, [&](const QcChunkFurnishing::Spawner &s) {
            return s.at == spawner && s.mob == u"qtcraft:cave_spider"_s && s.confined;
        }));
        QCOMPARE(structure.minecarts.size(), 1);
        const QcBlockPos rail = station->toWorld(structure.minecarts[0].at);
        QVERIFY(std::ranges::any_of(generator.setup().minecarts, [&](const auto &cart) { return cart.at == rail && cart.item == u"qtcraft:chest_minecart"_s; }));
        QVERIFY(std::ranges::any_of(generator.setup().contents, [&](const auto &contents) { return contents.at == rail && !contents.items.isEmpty(); }));

        QList<QcChunkPos> positions;
        for (int x = 0; x < station->sizeX(); ++x) {
            for (int z = 0; z < station->sizeZ(); ++z) {
                const QcChunkPos pos = station->toWorld(QcBlockPos{x, 0, z}).chunk();
                if (!positions.contains(pos)) {
                    positions.append(pos);
                }
            }
        }
        const Chunks chunks = generate(generator, positions);
        const auto local = [&](int x, int y, int z) { return at(chunks, station->toWorld(QcBlockPos{x, y, z})); };
        QCOMPARE(nameOf(local(31, 5, 2)), u"qtcraft:spawner"_s);
        QVERIFY(block(local(5, 5, 2)).rail);
        // The ladder from the lower level to two above the ground's top, the headframe's posts.
        for (int y = 5; y <= 16; ++y) {
            QVERIFY2(block(local(1, y, 1)).climbable, qPrintable(QString::number(y)));
        }
        QCOMPARE(nameOf(local(0, 17, 0)), u"qtcraft:oak_log"_s);
        QCOMPARE(nameOf(local(2, 17, 2)), u"qtcraft:oak_log"_s);
        // The staircase: one lower each step, four cells high.
        for (int a = 0; a < 6; ++a) {
            QVERIFY(solid(local(12 + a, 9 - a, 2)));
            for (int dy = 0; dy < 4; ++dy) {
                QVERIFY(!block(local(12 + a, 10 - a + dy, 2)).solid);
            }
        }
        // The inclined entrance (decision E-895): from a chamber up one block per step with rails
        // rising east, out onto the ground under a support.
        for (int a = 0; a < 3; ++a) {
            QVERIFY(solid(local(37 + a, 11 + a, 2)));
            QVERIFY(block(local(37 + a, 12 + a, 2)).rail);
            for (int dy = 1; dy < 3; ++dy) {
                QVERIFY(!block(local(37 + a, 12 + a + dy, 2)).solid);
            }
        }
        QVERIFY(solid(local(40, 14, 2)) && !block(local(40, 15, 2)).solid);
        QCOMPARE(nameOf(local(39, 16, 1)), u"qtcraft:oak_log"_s);
        // The bridge: planks over the pit, pillars from its supports down to the ground layer.
        for (int x = 21; x <= 26; ++x) {
            QCOMPARE(nameOf(local(x, 4, 2)), u"qtcraft:oak_planks"_s);
            QVERIFY(!block(local(x, 2, 2)).solid);
        }
        for (const int x : {22, 25}) {
            for (int y = 1; y <= 3; ++y) {
                QCOMPARE(nameOf(local(x, y, 1)), u"qtcraft:oak_log"_s);
            }
            QVERIFY(solid(local(x, 0, 1)));
        }
    }

    // Debug-relative cost of the mine shafts (stage 38.2, docs/Performance-Verlauf.md): chunks per
    // second of thread time (other processes do not count) with and without them, including the
    // planning of every region met, on four areas interleaved, and on the chunks of one mine shaft.
    // Printed, not checked.
    void measureGenerationCost()
    {
        auto without = std::make_shared<QcContentRegistry>(*m_content);
        QcTerrainSettings settings = without->worldgen().terrainSettings();
        settings.mineshafts.enabled = false;
        without->worldgen().setTerrainSettings(settings);
        const auto seconds = [](const QcTerrainGenerator &terrain, const QList<QcChunkPos> &chunks) {
            const qint64 start = QcThreadCpuClock::nowNs();
            for (const QcChunkPos &pos : chunks) {
                terrain.generate(pos);
            }
            return static_cast<double>(QcThreadCpuClock::nowNs() - start) / 1e9;
        };
        const auto area = [](int offsetX, int offsetZ) {
            QList<QcChunkPos> chunks;
            for (int cz = 0; cz < 12; ++cz) {
                for (int cx = 0; cx < 12; ++cx) {
                    chunks.append({cx + offsetX, cz + offsetZ});
                }
            }
            return chunks;
        };

        // Fresh generators, interleaved on fresh areas.
        const QcTerrainGenerator with(m_content, seed);
        const QcTerrainGenerator plain(without, seed);
        double withTime = 0.0;
        double plainTime = 0.0;
        qsizetype count = 0;
        for (const auto &[x, z] : {std::pair{0, 0}, {40, 0}, {0, 40}, {-40, -40}}) {
            const QList<QcChunkPos> chunks = area(x, z);
            plainTime += seconds(plain, chunks);
            withTime += seconds(with, chunks);
            count += chunks.size();
        }
        qInfo().noquote() << u"chunks/s without mine shafts %1, with %2"_s.arg(static_cast<double>(count) / plainTime, 0, 'f', 1)
            .arg(static_cast<double>(count) / withTime, 0, 'f', 1);

        // Only the chunks of one mine shaft, its plan made afresh.
        const QList<QcChunkPos> chunks = chunksOf(*m_plans.constFirst());
        const QcTerrainGenerator withAgain(m_content, seed);
        const QcTerrainGenerator plainAgain(without, seed);
        const double c = seconds(plainAgain, chunks);
        const double d = seconds(withAgain, chunks);
        qInfo().noquote() << u"chunks/s of a mine shaft's %1 chunks without mine shafts %2, with %3"_s.arg(chunks.size())
            .arg(static_cast<double>(chunks.size()) / c, 0, 'f', 1)
            .arg(static_cast<double>(chunks.size()) / d, 0, 'f', 1);
    }
};

QTEST_GUILESS_MAIN(TestMineshafts)
#include "test_mineshafts.moc"
