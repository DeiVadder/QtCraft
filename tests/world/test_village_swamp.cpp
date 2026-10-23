#include "QcTestUtil.h"
#include "server/pathing/QcPathFinder.h"
#include "world/QcVillageTestUtil.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"
#include "worldgen/villages/QcWetlandShaper.h"

#include <QQueue>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <map>

using namespace Qt::StringLiterals;
using namespace QcVillageTestUtil;

namespace {

// The blocks of generated chunks for the path finder; columns outside them are not loaded.
class ChunkAccess : public QcBlockAccess
{
public:
    ChunkAccess(const Chunks &chunks, const QcContentRegistry &content)
        : m_chunks(chunks)
        , m_content(content)
    {
    }

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        if (pos.y >= QcWorld::worldHeight) {
            return m_chunks.contains(pos.chunk()) ? std::optional<QcBlockId>(qcAirBlockId) : std::nullopt;
        }
        return blockIn(m_chunks, pos);
    }
    const QcContentRegistry &content() const override { return m_content; }

private:
    const Chunks &m_chunks;
    const QcContentRegistry &m_content;
};

} // namespace

// Swamp villages over water (decision E-407): pieces on stilts down to the bottom, floors at one
// height above the water surface, boardwalks instead of roads with stairs down to dry land, all a
// pure function of the seed; villagers walk from hut to hut over the boardwalks.
class TestVillageSwamp : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 7;
    // Regions around the origin searched for swamp villages.
    static constexpr int rings = 14;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcTerrainGenerator m_generator{m_content, seed};
    QList<std::shared_ptr<const QcVillagePlan>> m_plans = plansAround(m_generator, rings);
    QcPathCellTable m_cells{*m_content};
    const QList<std::shared_ptr<const QcVillagePlan>> m_swampPlans = swampPlansOf();

    // The chunks of each swamp village generated in order, once for all test functions: the
    // generation is the slow part of this test. A map keeps the references it hands out.
    mutable std::map<const QcVillagePlan *, Chunks> m_built;

    const QList<std::shared_ptr<const QcVillagePlan>> &swampPlans() const { return m_swampPlans; }

    const Chunks &built(const QcVillagePlan &plan) const
    {
        auto found = m_built.find(&plan);
        if (found == m_built.end()) {
            found = m_built.emplace(&plan, generateInOrder(m_generator, chunksOf(plan))).first;
        }
        return found->second;
    }

    QList<std::shared_ptr<const QcVillagePlan>> swampPlansOf() const { return swampPlansOf(m_plans); }

    static QList<std::shared_ptr<const QcVillagePlan>> swampPlansOf(const QList<std::shared_ptr<const QcVillagePlan>> &all)
    {
        QList<std::shared_ptr<const QcVillagePlan>> plans;
        for (const std::shared_ptr<const QcVillagePlan> &plan : all) {
            if (plan->style == u"qtcraft:swamp"_s) {
                plans.append(plan);
            }
        }
        return plans;
    }

    const QcVillageStyle &swampStyle() const { return *m_generator.villages().styles().named(u"qtcraft:swamp"_s); }

    int floorY() const { return QcBoardwalkPlanner::floorY(m_generator, swampStyle().stilts); }

    QcBlockId block(const QString &name) const { return m_content->stateId(u"qtcraft:"_s + name).value_or(qcAirBlockId); }

    bool isLamp(const QcStructurePlacement &piece) const { return shortName(piece).endsWith(u"_lamp"_s); }

    // The swamp village with the most pieces (the richest for the walking tests).
    std::shared_ptr<const QcVillagePlan> largestSwampPlan() const
    {
        std::shared_ptr<const QcVillagePlan> best;
        for (const std::shared_ptr<const QcVillagePlan> &plan : swampPlans()) {
            if (!best || plan->pieces.size() > best->pieces.size()) {
                best = plan;
            }
        }
        return best;
    }

    // Feet cells at the floor height a creature reaches from `start` over standing cells, stepping
    // to the four sides (doors opened), within the village's columns.
    QSet<QcBlockPos> walkable(const ChunkAccess &world, QcBlockPos start, const QcVillagePlan &plan) const
    {
        return walkable(world, start, [&plan](QcBlockPos cell) { return plan.covers(cell.x, cell.z, 0); });
    }

    // The same within the columns `inside` accepts.
    template<typename Inside>
    QSet<QcBlockPos> walkable(const ChunkAccess &world, QcBlockPos start, Inside inside) const
    {
        QcPathRules rules;
        rules.opensDoors = true;
        QcPathDirectCells cells(world, m_cells);
        QSet<QcBlockPos> seen{start};
        QQueue<QcBlockPos> open;
        open.enqueue(start);
        while (!open.isEmpty()) {
            const QcBlockPos at = open.dequeue();
            for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const QcBlockPos next = at.offset(dx, 0, dz);
                if (!inside(next) || seen.contains(next) || !QcPathTerrain::standAt(cells, next, rules).ok) {
                    continue;
                }
                seen.insert(next);
                open.enqueue(next);
            }
        }
        return seen;
    }

    // A feet cell beside a bed (at the floor height) a creature can stand in, if any.
    std::optional<QcBlockPos> besideBed(const ChunkAccess &world, QcBlockPos bed) const
    {
        QcPathDirectCells cells(world, m_cells);
        for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            if (QcPathTerrain::standAt(cells, bed.offset(dx, 0, dz), QcPathRules{}).ok) {
                return bed.offset(dx, 0, dz);
            }
        }
        return std::nullopt;
    }

    // A feet cell inside the piece at the floor height a creature can stand in, if any.
    std::optional<QcBlockPos> standInside(const ChunkAccess &world, const QcStructurePlacement &piece, const QSet<QcBlockPos> &reached) const
    {
        const QcBlockPos origin = piece.origin();
        for (int dz = 0; dz < piece.sizeZ(); ++dz) {
            for (int dx = 0; dx < piece.sizeX(); ++dx) {
                const QcBlockPos feet{origin.x + dx, origin.y + 1, origin.z + dz};
                if (reached.contains(feet)) {
                    return feet;
                }
            }
        }
        Q_UNUSED(world);
        return std::nullopt;
    }

    // The flat wetland of a village (shaped columns at or below the water surface), counted.
    struct WetlandCensus
    {
        // Columns of the open wetland (off pieces and boardwalks) per cover.
        std::map<QcWetlandPattern::Cover, int> open;
        int all = 0;
        // Columns under water: pools and ground below the water surface.
        int water = 0;
        // Open mud below the water surface: ground too deep to raise near the blend's end.
        int deepened = 0;
        // Ground below the water surface under pieces and boardwalks, where the terrain lies deeper
        // than the blend raises it (decision E-660).
        int deepUnderneath = 0;
        // Pools of the pattern under pieces and boardwalks (there must be none).
        QStringList poolsUnderneath;

        // Counts a column at or below the water surface; `raised` whether the blend raised its
        // ground as far as it could (to the water surface or above the terrain).
        void add(QcBlockPos column, QcWetlandPattern::Cover cover, bool underneath, bool raised, int waterSurface)
        {
            const bool pool = cover == QcWetlandPattern::Cover::Water || cover == QcWetlandPattern::Cover::DeepWater;
            const bool wet = pool || column.y < waterSurface;
            ++all;
            water += wet ? 1 : 0;
            if (underneath) {
                if (pool || !raised) {
                    poolsUnderneath.append(u"%1 %2"_s.arg(column.x).arg(column.z));
                }
                deepUnderneath += wet ? 1 : 0;
            } else if (column.y < waterSurface && cover == QcWetlandPattern::Cover::Mud) {
                ++deepened;
            } else {
                ++open[cover];
            }
        }
    };

    WetlandCensus wetlandCensus(const QcVillagePlan &plan) const
    {
        const QcWetlandShaper &shaper = m_generator.villages().wetland();
        const int waterSurface = m_generator.seaLevel() - 1;
        WetlandCensus census;
        for (int z = plan.minZ; z <= plan.maxZ; ++z) {
            for (int x = plan.minX; x <= plan.maxX; ++x) {
                const std::optional<QcWetlandShaper::Column> column = shaper.columnAt(plan, x, z);
                if (!column || column->top > waterSurface) {
                    continue;
                }
                const bool underneath = std::ranges::any_of(plan.wetland.wet, [x, z](QcVillagePlan::Area area) { return area.contains(x, z); });
                const bool raised = column->top == waterSurface || column->top > m_generator.column(x, z).height - 1;
                census.add({x, column->top, z}, column->cover, underneath, raised, waterSurface);
            }
        }
        return census;
    }

private slots:
    void swampVillagesStandInSwamps()
    {
        const QList<std::shared_ptr<const QcVillagePlan>> plans = swampPlans();
        QVERIFY2(plans.size() >= 3, qPrintable(u"only %1 swamp villages"_s.arg(plans.size())));
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            // The centre column: the middle of the centre piece.
            const QcStructurePlacement &centre = plan->pieces.front();
            const QcTerrainColumn column = m_generator.column(centre.origin().x + centre.sizeX() / 2, centre.origin().z + centre.sizeZ() / 2);
            const bool swamp = m_generator.biomeName(column.biome) == u"qtcraft:swamp"_s;
            QCOMPARE(plan->style == u"qtcraft:swamp"_s, swamp);
        }
    }

    void piecesStandOnStiltsAboveTheWater()
    {
        const QcBlockId post = block(u"spruce_log"_s);
        const int seaLevel = m_generator.seaLevel();
        int stilts = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : swampPlans()) {
            const Chunks &chunks = built(*plan);
            for (const QcStructurePlacement &piece : plan->pieces) {
                QVERIFY2(pieceIsBuilt(chunks, piece), qPrintable(shortName(piece)));
                // Floors at one height, above the water surface.
                QCOMPARE(piece.origin().y, floorY());
                QVERIFY(piece.origin().y > seaLevel);

                // The corners stand on posts that reach solid ground, never a floating post.
                const QcBlockPos origin = piece.origin();
                for (const auto &[dx, dz] : {std::pair{0, 0}, std::pair{piece.sizeX() - 1, piece.sizeZ() - 1}}) {
                    const QcBlockPos corner{origin.x + dx, origin.y, origin.z + dz};
                    if (!m_content->block(*blockIn(chunks, corner)).solid) {
                        continue;
                    }
                    int y = corner.y - 1;
                    while (blockIn(chunks, {corner.x, y, corner.z}) == post) {
                        --y;
                        ++stilts;
                    }
                    const QcBlockId ground = *blockIn(chunks, {corner.x, y, corner.z});
                    QVERIFY2(m_content->block(ground).solid && !m_content->block(ground).liquid, qPrintable(shortName(piece)));
                    QVERIFY2(y >= seaLevel - 1 - swampStyle().stilts.maxDepth, qPrintable(shortName(piece)));
                }
            }
        }
        QVERIFY2(stilts > 20, qPrintable(QString::number(stilts)));
    }

    void boardwalksLieAtTheFloorWithPostsAndStairsToLand()
    {
        int stairs = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : swampPlans()) {
            QVERIFY(plan->roads.isEmpty() && plan->branchRoads.isEmpty() && plan->steps.isEmpty());
            QVERIFY(!plan->decks.isEmpty());
            const Chunks &chunks = built(*plan);
            for (const QcVillagePlan::Deck &deck : plan->decks) {
                QCOMPARE(blockIn(chunks, deck.at), deck.block);
                const bool stair = m_content->block(deck.block).property(u"facing"_s) != nullptr;
                QVERIFY(stair ? deck.at.y < floorY() : deck.at.y == floorY());
                if (!stair) {
                    continue;
                }
                // A stair's post (if any) reaches dry ground above the water surface.
                ++stairs;
                int y = deck.at.y - 1;
                while (blockIn(chunks, {deck.at.x, y, deck.at.z}) == deck.post) {
                    --y;
                }
                const QcBlockDefinition &ground = m_content->block(*blockIn(chunks, {deck.at.x, y, deck.at.z}));
                QVERIFY(ground.solid && !ground.liquid);
                QVERIFY(y >= m_generator.seaLevel() - 2);
            }
        }
        QVERIFY2(stairs > 0, "no boardwalk ends on dry land");
    }

    void everyPieceIsReachableOverTheBoardwalks()
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : swampPlans()) {
            const Chunks &chunks = built(*plan);
            const ChunkAccess world(chunks, *m_content);
            // From the platform at the centre: the cell in front of the bell's frame.
            const QcStructurePlacement &centre = plan->pieces.front();
            const QcBlockPos start = centre.toWorld(QcBlockPos{6, 1, 11});
            const QSet<QcBlockPos> reached = walkable(world, start, *plan);
            QVERIFY(reached.size() > 100);
            for (const QcStructurePlacement &piece : plan->pieces) {
                if (!isLamp(piece)) {
                    QVERIFY2(standInside(world, piece, reached), qPrintable(u"%1 at %2 %3"_s.arg(shortName(piece)).arg(piece.origin().x).arg(piece.origin().z)));
                }
            }
        }
    }

    void creaturesWalkFromHutToHutOverTheBoardwalks()
    {
        const std::shared_ptr<const QcVillagePlan> plan = largestSwampPlan();
        QVERIFY(plan);
        const Chunks &chunks = built(*plan);
        const ChunkAccess world(chunks, *m_content);
        QcPathRules rules;
        rules.opensDoors = true;
        QcPathFinder finder;
        const QcBlockId water = block(u"water"_s);

        // Every pair of beds 8 to 24 blocks apart joined over the boardwalks inside the search window
        // (64 cells wide around the start): the finder walks it and stays out of the water.
        int walks = 0;
        for (const QcVillagePlan::Spot &from : plan->beds) {
            const std::optional<QcBlockPos> start = besideBed(world, from.at);
            QVERIFY(start);
            const auto window = [&start](QcBlockPos cell) { return std::abs(cell.x - start->x) <= 28 && std::abs(cell.z - start->z) <= 28; };
            const QSet<QcBlockPos> reached = walkable(world, *start, window);
            for (const QcVillagePlan::Spot &to : plan->beds) {
                const int distance = std::abs(from.at.x - to.at.x) + std::abs(from.at.z - to.at.z);
                const std::optional<QcBlockPos> goal = besideBed(world, to.at);
                if (distance < 8 || distance > 24 || !goal || !reached.contains(*goal)) {
                    continue;
                }
                const QcPathResult path = finder.find(world, m_cells, rules, QcPathRequest{*start, *goal, 0, 8192, 256});
                QVERIFY2(path.reachesGoal, qPrintable(u"%1 %2 -> %3 %4"_s.arg(from.at.x).arg(from.at.z).arg(to.at.x).arg(to.at.z)));
                for (const QcBlockPos cell : path.cells) {
                    QVERIFY2(blockIn(chunks, cell) != water && blockIn(chunks, cell.offset(0, -1, 0)) != water, qPrintable(u"%1 %2 %3 from %4 %5"_s.arg(cell.x).arg(cell.y).arg(cell.z).arg(from.at.x).arg(from.at.z)));
                }
                ++walks;
            }
        }
        QVERIFY2(walks >= 4, qPrintable(QString::number(walks)));
    }

    void everyPostStandsInWaterOrMud()
    {
        const QcBlockId post = block(u"spruce_log"_s);
        const QcBlockId mud = block(u"mud"_s);
        const QcBlockId water = block(u"water"_s);
        int posts = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : swampPlans()) {
            const Chunks &chunks = built(*plan);
            QList<QcBlockPos> tops;
            for (const QcStructurePlacement &piece : plan->pieces) {
                for (int cell = 0; cell < piece.sizeX() * piece.sizeZ(); ++cell) {
                    tops.append({piece.origin().x + cell % piece.sizeX(), floorY() - 1, piece.origin().z + cell / piece.sizeX()});
                }
            }
            for (const QcVillagePlan::Deck &deck : plan->decks) {
                tops.append(deck.at.offset(0, -1, 0));
            }

            for (const QcBlockPos top : std::as_const(tops)) {
                const bool dry = std::ranges::any_of(plan->wetland.dry, [top](QcVillagePlan::Area area) { return area.contains(top.x, top.z); });
                if (blockIn(chunks, top) != post || dry) {
                    continue;
                }
                QcBlockPos foot = top;
                while (blockIn(chunks, foot.offset(0, -1, 0)) == post) {
                    foot = foot.offset(0, -1, 0);
                }
                // On the mud bottom, or down through water onto older ground.
                const QcBlockId below = *blockIn(chunks, foot.offset(0, -1, 0));
                const bool inWater = blockIn(chunks, {foot.x + 1, m_generator.seaLevel() - 1, foot.z}) == water
                                     || blockIn(chunks, {foot.x - 1, m_generator.seaLevel() - 1, foot.z}) == water;
                QVERIFY2(below == mud || inWater, qPrintable(u"post at %1 %2 on %3"_s.arg(foot.x).arg(foot.z).arg(m_content->block(below).name)));
                ++posts;
            }
        }
        QVERIFY2(posts > 200, qPrintable(QString::number(posts)));
    }

    void theWetlandBlendsIntoTheTerrain_data()
    {
        QTest::addColumn<qint64>("seed");
        QTest::addColumn<int>("regionX");
        QTest::addColumn<int>("regionZ");
        QTest::newRow("rings 14") << seed << 0 << 0;
        // Seed 20261017: the village beside the highest ground of the seed's 38 swamp villages within
        // 60 regions of the origin (region 34, -44: x 13165..13276, z -16775..-16703, the terrain up
        // to y 82); a wetland beside high ground once cut a pit with steps of 6 blocks (decision E-660).
        QTest::newRow("village beside a hill") << qint64{20261017} << 34 << -44;
    }

    void theWetlandBlendsIntoTheTerrain()
    {
        QFETCH(qint64, seed);
        QFETCH(int, regionX);
        QFETCH(int, regionZ);
        const QcTerrainGenerator other(m_content, seed);
        const QcTerrainGenerator &generator = seed == TestVillageSwamp::seed ? m_generator : other;
        const std::shared_ptr<const QcVillagePlan> beside = seed == TestVillageSwamp::seed ? nullptr : generator.villages().regionPlan(regionX, regionZ);
        QVERIFY(seed == TestVillageSwamp::seed || (beside && beside->style == u"qtcraft:swamp"_s));
        const QList<std::shared_ptr<const QcVillagePlan>> plans = beside ? QList{beside} : swampPlans();
        const QcWetlandShaper &shaper = generator.villages().wetland();
        int shaped = 0;
        int steepest = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            QVERIFY(plan->wetland.settings.bottom == block(u"mud"_s));
            const auto ground = [&](int x, int z) { return generator.column(x, z).height - 1; };
            const auto height = [&](int x, int z) { return shaper.groundAt(*plan, x, z).value_or(ground(x, z)); };
            const auto dry = [&](int x, int z) {
                return std::ranges::any_of(plan->wetland.dry, [x, z](QcVillagePlan::Area area) { return area.contains(x, z); });
            };
            for (int z = plan->minZ - 2; z <= plan->maxZ + 2; ++z) {
                for (int x = plan->minX - 2; x <= plan->maxX + 2; ++x) {
                    const int here = height(x, z);
                    shaped += here != ground(x, z) ? 1 : 0;
                    // Never above the water surface where the ground was raised, never higher than it was.
                    QVERIFY(here <= std::max(ground(x, z), generator.seaLevel() - 1));
                    // No cliff into the neighbouring terrain: a step at most a block steeper than the
                    // terrain's own, at most 2 on flat ground (a pool's edge); the land access keeps its sides.
                    for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{0, 1}}) {
                        if (dry(x, z) || dry(x + dx, z + dz)) {
                            continue;
                        }
                        const int step = std::abs(here - height(x + dx, z + dz));
                        const int natural = std::abs(ground(x, z) - ground(x + dx, z + dz));
                        steepest = std::max(steepest, step - natural);
                        QVERIFY2(step <= std::max(natural + 1, 2), qPrintable(u"step %1 (terrain %2) at %3 %4"_s.arg(step).arg(natural).arg(x).arg(z)));
                    }
                }
            }
        }
        qInfo("%d villages, %d shaped columns, steepest step %d over the terrain's", static_cast<int>(plans.size()), shaped, steepest);
        QVERIFY(shaped > 1000);
    }

    void theWetlandIsPoolsMudAndGrass()
    {
        // Over the flat wetland (shaped columns at or below the water surface): pools of water over
        // the style's share of the open ground, mud under pieces and boardwalks (decision E-661).
        double shares = 0.0;
        int villages = 0;
        int deepUnderneath = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : swampPlans()) {
            WetlandCensus census = wetlandCensus(*plan);
            QVERIFY2(census.poolsUnderneath.isEmpty(), qPrintable(u"pool under a piece or boardwalk at "_s + census.poolsUnderneath.mid(0, 8).join(u", "_s)));
            deepUnderneath += census.deepUnderneath;

            // The pools' share of the open flat wetland; ground the blend could not raise is the
            // terrain's water, not the pattern's (some villages stand at a lake's shore).
            int openAll = 0;
            for (const auto &[cover, count] : census.open) {
                openAll += count;
            }
            const int openWater = census.open[QcWetlandPattern::Cover::Water] + census.open[QcWetlandPattern::Cover::DeepWater];
            const double share = static_cast<double>(openWater) / std::max(1, openAll);
            qInfo("village %d %d: water %.0f %% of the open wetland (%d columns), %.0f %% of all (%d), mud %d, grass %d, deepened %d", plan->minX,
                  plan->minZ, 100.0 * share, openAll, 100.0 * census.water / std::max(1, census.all), census.all, census.open[QcWetlandPattern::Cover::Mud],
                  census.open[QcWetlandPattern::Cover::Grass], census.deepened);
            QVERIFY(census.open[QcWetlandPattern::Cover::Mud] > 0 && census.open[QcWetlandPattern::Cover::Grass] > 0);
            shares += share;
            ++villages;
        }
        qInfo("%d columns under pieces and boardwalks below the water surface", deepUnderneath);
        QVERIFY(villages >= 3);
        const double mean = shares / villages;
        QVERIFY2(mean >= 0.3 && mean <= 0.5, qPrintable(QString::number(mean)));
    }

    void everyPoolIsEnclosed()
    {
        // Water the wetland places never drains: every water block has a block or water below and
        // beside it (a source pool held by its banks), within the generated chunks.
        const QcBlockId water = block(u"water"_s);
        const int waterSurface = m_generator.seaLevel() - 1;
        int pools = 0;
        const QList<std::shared_ptr<const QcVillagePlan>> plans = swampPlans();
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const Chunks &chunks = built(*plan);
            for (int z = plan->minZ; z <= plan->maxZ; ++z) {
                for (int x = plan->minX; x <= plan->maxX; ++x) {
                    const std::optional<int> top = m_generator.villages().wetland().groundAt(*plan, x, z);
                    if (!top || *top >= waterSurface) {
                        continue;
                    }
                    for (int y = *top + 1; y <= waterSurface; ++y) {
                        QCOMPARE(blockIn(chunks, {x, y, z}), std::optional<QcBlockId>(water));
                        for (const QcBlockPos side : {QcBlockPos{x + 1, y, z}, QcBlockPos{x - 1, y, z}, QcBlockPos{x, y, z + 1}, QcBlockPos{x, y, z - 1},
                                                      QcBlockPos{x, y - 1, z}}) {
                            const std::optional<QcBlockId> next = blockIn(chunks, side);
                            QVERIFY2(next != qcAirBlockId, qPrintable(u"water at %1 %2 %3 open to air"_s.arg(x).arg(y).arg(z)));
                        }
                    }
                    ++pools;
                }
            }
        }
        QVERIFY(pools > 100);
    }

    void poolsCarryLilyPads()
    {
        // The style's floating plant lies on the water of some pool columns (decision E-774), never
        // on anything else.
        const QcBlockId water = block(u"water"_s);
        const QcBlockId lily = block(u"lily_pad"_s);
        const int waterSurface = m_generator.seaLevel() - 1;
        int pools = 0;
        int lilies = 0;
        const QList<std::shared_ptr<const QcVillagePlan>> plans = swampPlans();
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const Chunks &chunks = built(*plan);
            for (int z = plan->minZ; z <= plan->maxZ; ++z) {
                for (int x = plan->minX; x <= plan->maxX; ++x) {
                    const bool pad = blockIn(chunks, {x, waterSurface + 1, z}) == lily;
                    const std::optional<int> top = m_generator.villages().wetland().groundAt(*plan, x, z);
                    const bool pool = top && *top < waterSurface;
                    QVERIFY2(!pad || (pool && blockIn(chunks, {x, waterSurface, z}) == water), qPrintable(u"lily pad off a pool at %1 %2"_s.arg(x).arg(z)));
                    pools += pool ? 1 : 0;
                    lilies += pad ? 1 : 0;
                }
            }
        }
        qInfo("%d lily pads on %d pool columns", lilies, pools);
        QVERIFY(lilies > pools / 20 && lilies < pools / 4);
    }

    void builtTheSameInAnyOrder()
    {
        // Three villages: wetland, stilts, boardwalks and stairs written chunk by chunk.
        const QList<std::shared_ptr<const QcVillagePlan>> plans = swampPlans();
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QList<QcChunkPos> positions = chunksOf(*plan);
            const Chunks &first = built(*plan);
            const QcTerrainGenerator fresh(m_content, seed);
            const Chunks second = generateOnThreads(fresh, positions);
            for (const QcChunkPos &pos : positions) {
                QVERIFY2(sameBlocks(*first.value(pos), *second.value(pos)), qPrintable(u"chunk %1 %2 differs"_s.arg(pos.x).arg(pos.z)));
            }
            QCOMPARE(fresh.villages().planFor(positions.front())->decks.size(), plan->decks.size());
        }
    }

    void bedsAndWorkstationsAreListed()
    {
        int fishers = 0;
        int farmers = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : swampPlans()) {
            QVERIFY(!plan->beds.isEmpty());
            const Chunks &chunks = built(*plan);
            for (const QcVillagePlan::Spot &bed : plan->beds) {
                QVERIFY(m_generator.villages().styles().blocks().isBedHead(*blockIn(chunks, bed.at)));
            }
            for (const QcVillagePlan::Spot &spot : plan->workstations) {
                QCOMPARE(m_generator.villages().styles().blocks().workstationKind(*blockIn(chunks, spot.at)), spot.kind);
                fishers += spot.kind == u"qtcraft:fisher"_s ? 1 : 0;
                farmers += spot.kind == u"qtcraft:farmer"_s ? 1 : 0;
            }
            // The meeting point is the hall's bell.
            QVERIFY(m_generator.villages().styles().blocks().isBell(*blockIn(chunks, plan->centre)));
        }
        QVERIFY(fishers > 0 && farmers > 0);
    }
};

QTEST_GUILESS_MAIN(TestVillageSwamp)
#include "test_village_swamp.moc"
