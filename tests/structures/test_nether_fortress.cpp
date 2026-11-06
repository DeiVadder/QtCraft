// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcNetherParser.h"
#include "fixtures/QcTestUtil.h"
#include "worldgen/fortress/QcFortressGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTest>

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

using namespace Qt::StringLiterals;

// The fortress of the nether (decisions E-1070 to E-1075): real generated chunks of natural seeds.
// Seeds as in decision E-846: fixed ones plus a random one per run, in the log; QC_TEST_SEED sets it.
namespace {

using Plan = QcFortressPlan;
using PlanPointer = std::shared_ptr<const QcFortressPlan>;

constexpr std::array<qint64, 4> fixedSeeds{42, 1, 20261106, 7777};
// The regions around the origin searched per side.
constexpr int searchRegions = 8;

quint64 cellKey(QcBlockPos cell)
{
    return (static_cast<quint64>(static_cast<quint32>(cell.x)) << 36) ^ (static_cast<quint64>(static_cast<quint32>(cell.z)) << 8) ^ static_cast<quint64>(cell.y);
}

// The chunks of a fortress, generated once, to be read by world cell.
class Site
{
public:
    Site(const QcNetherGenerator &generator, const Plan &plan)
    {
        const QcBlockPos low{plan.bounds.minX, 0, plan.bounds.minZ};
        const QcBlockPos high{plan.bounds.maxX, 0, plan.bounds.maxZ};
        for (int z = low.chunk().z - 1; z <= high.chunk().z + 1; ++z) {
            for (int x = low.chunk().x - 1; x <= high.chunk().x + 1; ++x) {
                const QcChunkPos pos{x, z};
                m_chunks.insert(key(pos), generator.generate(pos));
            }
        }
    }

    [[nodiscard]] QcBlockId at(QcBlockPos cell) const
    {
        const QcChunkPos pos = cell.chunk();
        const std::shared_ptr<QcChunk> &chunk = m_chunks[key(pos)];
        return chunk->block(cell.x - pos.x * QcWorld::chunkSize, cell.y, cell.z - pos.z * QcWorld::chunkSize);
    }

private:
    static qint64 key(QcChunkPos pos) { return (static_cast<qint64>(pos.x) << 32) ^ static_cast<qint64>(static_cast<quint32>(pos.z)); }

    mutable QHash<qint64, std::shared_ptr<QcChunk>> m_chunks;
};

// The free cells of a fortress: where one can stand or that light through it, over both floors of
// the stairwell.
QList<QcBlockPos> insideCells(const Plan &plan)
{
    QList<QcBlockPos> cells;
    for (const Plan::Room &room : plan.rooms) {
        const int inner = room.half - 1;
        const int top = room.topFloor() - room.centre.y + Plan::roomHeight;
        for (int z = -inner; z <= inner; ++z) {
            for (int x = -inner; x <= inner; ++x) {
                for (int y = 1; y <= top; ++y) {
                    cells.append(room.centre.offset(x, y, z));
                }
            }
        }
    }
    for (const Plan::Passage &passage : plan.passages) {
        for (int along = 0; along < passage.length; ++along) {
            for (int lateral = -1; lateral <= 1; ++lateral) {
                for (int y = 1; y <= Plan::passageHeight; ++y) {
                    cells.append(passage.floorAt(along, lateral).offset(0, y, 0));
                }
            }
        }
    }
    return cells;
}

} // namespace

class TestNetherFortress : public QObject
{
    Q_OBJECT

private:
    struct World
    {
        qint64 seed = 0;
        std::shared_ptr<QcNetherGenerator> generator;
        QList<PlanPointer> plans;
    };

    // A generated fortress: chunks are dear in Debug, so the tests that read blocks share these.
    struct Sample
    {
        qint64 seed = 0;
        std::shared_ptr<QcNetherGenerator> generator;
        PlanPointer plan;
        std::shared_ptr<const Site> site;
    };

    [[nodiscard]] static QList<qint64> seeds()
    {
        QList<qint64> all(fixedSeeds.begin(), fixedSeeds.end());
        all.append(QcTestUtil::randomSeed());
        return all;
    }

    [[nodiscard]] World worldOf(qint64 seed) const
    {
        World world{seed, std::make_shared<QcNetherGenerator>(m_content, seed), {}};
        for (int z = -searchRegions / 2; z < searchRegions / 2; ++z) {
            for (int x = -searchRegions / 2; x < searchRegions / 2; ++x) {
                if (PlanPointer plan = world.generator->fortress().planOf(x, z)) {
                    world.plans.append(plan);
                }
            }
        }
        return world;
    }

    // The 3 x 3 chunks around the middle of `plan`.
    [[nodiscard]] static QList<QcChunkPos> chunksAround(const Plan &plan)
    {
        const QcChunkPos middle = plan.rooms.constFirst().centre.chunk();
        QList<QcChunkPos> chunks;
        for (int i = 0; i < 9; ++i) {
            chunks.append({middle.x - 1 + i % 3, middle.z - 1 + i / 3});
        }
        return chunks;
    }

    [[nodiscard]] QString blockName(QcBlockId id) const { return m_content->stateName(id).section(u'[', 0, 0); }
    [[nodiscard]] bool standable(QcBlockId id) const { return m_content->block(id).solid && !m_content->block(id).liquid; }
    [[nodiscard]] bool passable(QcBlockId id) const { return !m_content->block(id).solid && !m_content->block(id).liquid; }

    // The window and side opening cells in the walls of a closed passage, and the first opening
    // that does not lead into the open.
    struct Openings
    {
        int windows = 0;
        int openings = 0;
        QString problem;
    };
    Openings openingsOf(const Sample &sample, const Plan::Passage &passage) const
    {
        Openings found;
        for (int along = 0; along < passage.length; ++along) {
            for (const int side : {-1, 1}) {
                const QcBlockPos wall = passage.floorAt(along, side * Plan::passageHalf).offset(0, 2, 0);
                const bool window = blockName(sample.site->at(wall)) == u"qtcraft:nether_brick_fence"_s;
                const bool opening = sample.site->at(wall) == qcAirBlockId;
                found.windows += window ? 1 : 0;
                found.openings += opening ? 1 : 0;
                const QcBlockPos behind = passage.floorAt(along, side * (Plan::passageHalf + 1)).offset(0, 2, 0);
                if ((window || opening) && !passable(sample.site->at(behind)) && found.problem.isEmpty()) {
                    found.problem = u"seed %1: opening at %2 %3 %4 into %5"_s.arg(sample.seed).arg(wall.x).arg(wall.y).arg(wall.z).arg(blockName(sample.site->at(behind)));
                }
            }
        }
        return found;
    }

    [[nodiscard]] QSet<QString> biomesOf(const World &world, const Plan &plan) const
    {
        QSet<QString> biomes;
        for (const Plan::Room &room : plan.rooms) {
            biomes.insert(world.generator->biomeAt(room.centre.x, room.centre.z));
        }
        return biomes;
    }

    // The first fortress of every seed, the one with the most piers of the first seed (bridges
    // over the lava sea), and further ones until every biome holds a room of one of them.
    [[nodiscard]] QList<Sample> pickSamples() const
    {
        QList<Sample> picked;
        QSet<QString> covered;
        const QList<qint64> allSeeds = seeds();
        for (const qint64 seed : std::as_const(allSeeds)) {
            const World world = worldOf(seed);
            const auto mostPiers = std::ranges::max_element(world.plans, {}, [](const PlanPointer &plan) { return plan->piers.size(); });
            for (const PlanPointer &plan : world.plans) {
                const QSet<QString> biomes = biomesOf(world, *plan);
                const bool more = std::ranges::any_of(biomes, [&covered](const QString &biome) { return !covered.contains(biome); });
                const bool first = plan == world.plans.constFirst() || (seed == fixedSeeds[0] && plan == *mostPiers);
                if (first || more) {
                    covered.unite(biomes);
                    picked.append({seed, world.generator, plan, std::make_shared<const Site>(*world.generator, *plan)});
                }
            }
        }
        for (const qint64 seed : {qint64{1}, QcTestUtil::randomSeed()}) {
            const std::shared_ptr<QcNetherGenerator> showcase = showcaseOf(seed);
            const PlanPointer plan = showcase->fortress().demonstration();
            picked.append({seed, showcase, plan, std::make_shared<const Site>(*showcase, *plan)});
        }
        return picked;
    }

    // The nether of a showcase world of `seed`, with the arrival station and its fortress.
    [[nodiscard]] std::shared_ptr<QcNetherGenerator> showcaseOf(qint64 seed) const
    {
        const std::shared_ptr<QcWorldGenerator> created = QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::nether), m_content, seed,
                                                                                    QString::fromLatin1(QcWorldGenerators::netherStation), 1);
        return std::dynamic_pointer_cast<QcNetherGenerator>(created);
    }

    [[nodiscard]] const QList<Sample> &samples()
    {
        if (m_samples.isEmpty()) {
            m_samples = pickSamples();
            qInfo() << "fortresses generated:" << m_samples.size();
        }
        return m_samples;
    }

    // The floor cells one reaches on foot from the middle of the fortress.
    [[nodiscard]] QSet<quint64> walk(const Site &site, QcBlockPos start, QcXzRect bounds) const
    {
        QSet<quint64> seen{cellKey(start)};
        std::deque<QcBlockPos> open{start};
        while (!open.empty()) {
            const QcBlockPos feet = open.front();
            open.pop_front();
            for (const auto &[dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                for (const int dy : {1, 0, -1, -2, -3}) {
                    const QcBlockPos next = feet.offset(dx, dy, dz);
                    if (!bounds.contains(next.x, next.z) || seen.contains(cellKey(next))) {
                        continue;
                    }
                    // Up needs headroom over the cell one leaves, a drop free air down the column.
                    bool free = passable(site.at(next)) && passable(site.at(next.offset(0, 1, 0))) && standable(site.at(next.offset(0, -1, 0)));
                    free = free && (dy <= 0 || passable(site.at(feet.offset(0, 2, 0))));
                    for (int fall = dy + 1; fall <= 0 && free; ++fall) {
                        free = passable(site.at(feet.offset(dx, fall, dz))) && passable(site.at(feet.offset(dx, fall + 1, dz)));
                    }
                    if (free) {
                        seen.insert(cellKey(next));
                        open.push_back(next);
                    }
                }
            }
        }
        return seen;
    }

private slots:
    void initTestCase()
    {
        m_content = QcTestUtil::loadContent();
        QVERIFY(m_content);
    }

    // The same seed plans the same fortresses in any order of the regions; another seed others.
    void plansFollowTheSeedAndNothingElse()
    {
        const QList<qint64> allSeeds = seeds();
        for (const qint64 seed : std::as_const(allSeeds)) {
            const World first = worldOf(seed);
            QVERIFY2(!first.plans.isEmpty(), qPrintable(u"no fortress with seed %1"_s.arg(seed)));
            const QcNetherGenerator backwards(m_content, seed);
            for (qsizetype i = first.plans.size() - 1; i >= 0; --i) {
                const Plan &plan = *first.plans[i];
                const PlanPointer again = backwards.fortress().planOf(plan.regionX, plan.regionZ);
                QVERIFY(again);
                QCOMPARE(again->bounds, plan.bounds);
                QCOMPARE(again->parts, plan.parts);
                QCOMPARE(again->piers, plan.piers);
                QCOMPARE(again->chests.size(), plan.chests.size());
                QCOMPARE(again->rooms.size(), plan.rooms.size());
                for (qsizetype room = 0; room < plan.rooms.size(); ++room) {
                    QCOMPARE(again->rooms[room].centre, plan.rooms[room].centre);
                    QCOMPARE(again->rooms[room].doors, plan.rooms[room].doors);
                }
            }
            const World other = worldOf(seed + 1);
            const auto boundsOf = [](const World &world) {
                QList<QcXzRect> all;
                for (const PlanPointer &plan : world.plans) {
                    all.append(plan->bounds);
                }
                return all;
            };
            QVERIFY2(boundsOf(other) != boundsOf(first), qPrintable(u"seed %1 and the next plan alike"_s.arg(seed)));
        }
    }

    // The chunks around a fortress's middle come out the same from fresh generators in two
    // orders: row by row, and backwards from the far corner.
    void aChunkComesOutTheSameInAnyOrder()
    {
        const World world = worldOf(fixedSeeds[0]);
        const QList<QcChunkPos> chunks = chunksAround(*world.plans.constFirst());
        const QcNetherGenerator forwards(m_content, world.seed);
        const QcNetherGenerator backwards(m_content, world.seed);
        QList<std::shared_ptr<QcChunk>> later(chunks.size());
        for (qsizetype i = chunks.size() - 1; i >= 0; --i) {
            later[i] = backwards.generate(chunks[i]);
        }
        for (qsizetype i = 0; i < chunks.size(); ++i) {
            QVERIFY2(QcTestUtil::sameBlocks(*forwards.generate(chunks[i]), *later[i]), qPrintable(u"chunk %1 %2"_s.arg(chunks[i].x).arg(chunks[i].z)));
        }
    }

    // Four threads generating the chunks around a fortress at once with a fresh generator, which
    // plans the region while they race, write what one thread writes alone.
    void chunksComeOutTheSameOnFourThreads()
    {
        const World world = worldOf(fixedSeeds[1]);
        const QList<QcChunkPos> chunks = chunksAround(*world.plans.constFirst());
        const QcNetherGenerator serial(m_content, world.seed);
        const QcNetherGenerator shared(m_content, world.seed);
        QList<std::shared_ptr<QcChunk>> parallel(chunks.size());
        std::atomic<qsizetype> next{0};
        std::vector<std::thread> threads;
        threads.reserve(4);
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&] {
                for (qsizetype i = next++; i < chunks.size(); i = next++) {
                    parallel[i] = shared.generate(chunks[i]);
                }
            });
        }
        for (std::thread &thread : threads) {
            thread.join();
        }
        for (qsizetype i = 0; i < chunks.size(); ++i) {
            QVERIFY2(QcTestUtil::sameBlocks(*serial.generate(chunks[i]), *parallel[i]), qPrintable(u"chunk %1 %2"_s.arg(chunks[i].x).arg(chunks[i].z)));
        }
    }

    void everyFortressHasItsParts()
    {
        int bridges = 0;
        int galleries = 0;
        int staircases = 0;
        const QList<qint64> allSeeds = seeds();
        for (const qint64 seed : std::as_const(allSeeds)) {
            const World world = worldOf(seed);
            for (const PlanPointer &plan : world.plans) {
                const auto count = [&plan](Plan::RoomKind kind) { return std::ranges::count(plan->rooms, kind, &Plan::Room::kind); };
                QCOMPARE(count(Plan::RoomKind::Platform), 1);
                QCOMPARE(count(Plan::RoomKind::Garden), 1);
                QCOMPARE(count(Plan::RoomKind::Stairwell), 1);
                QVERIFY(count(Plan::RoomKind::Crossing) >= 2);
                QVERIFY(plan->chests.size() >= 2);
                QCOMPARE(plan->spawners.size(), 1);
                for (const Plan::Passage &passage : plan->passages) {
                    bridges += passage.roofed ? 0 : 1;
                    galleries += passage.roofed ? 1 : 0;
                    staircases += passage.rise > 0 ? 1 : 0;
                }
            }
        }
        QVERIFY(bridges > 0);
        QVERIFY(galleries > 0);
        QVERIFY(staircases > 0);
    }

    void aFortressCanBeWalkedThrough()
    {
        for (const Sample &sample : samples()) {
            const qint64 seed = sample.seed;
            const Plan &plan = *sample.plan;
            const Site &site = *sample.site;
            const QcBlockPos start = plan.rooms.constFirst().centre.offset(0, 1, 1);
            const QSet<quint64> reached = walk(site, start, plan.bounds);
            for (const Plan::Room &room : plan.rooms) {
                // A cell beside the middle: the middle may hold a dais or a chest.
                const QcBlockPos beside = room.centre.offset(room.kind == Plan::RoomKind::Platform ? 3 : 1, 1, 0);
                QVERIFY2(reached.contains(cellKey(beside)), qPrintable(u"seed %1: room at %2 %3 %4 not reached"_s.arg(seed).arg(room.centre.x).arg(room.centre.y).arg(room.centre.z)));
            }
            for (const Plan::Passage &passage : plan.passages) {
                const QcBlockPos middle = passage.floorAt(passage.length / 2, 0).offset(0, 1, 0);
                // A stair's cell counts one above its block, like any other.
                QVERIFY2(reached.contains(cellKey(middle)), qPrintable(u"seed %1: passage at %2 %3 %4 not reached"_s.arg(seed).arg(middle.x).arg(middle.y).arg(middle.z)));
            }
            const Plan::Room &stairwell = *std::ranges::find(plan.rooms, Plan::RoomKind::Stairwell, &Plan::Room::kind);
            const QcBlockPos upper = stairwell.centre.offset(0, Plan::levelRise + 1, 0);
            const auto [backX, backZ] = Plan::stepOf(Plan::opposite(stairwell.flight));
            QVERIFY2(reached.contains(cellKey(upper.offset(backX, 0, backZ))), qPrintable(u"seed %1: upper floor not reached"_s.arg(seed)));
        }
    }

    void noLiquidTouchesTheInside()
    {
        QVERIFY(!samples().isEmpty());
        for (const Sample &sample : samples()) {
            const QList<QcBlockPos> cells = insideCells(*sample.plan);
            for (const QcBlockPos &cell : cells) {
                for (const QcBlockPos &near : {cell, cell.offset(1, 0, 0), cell.offset(-1, 0, 0), cell.offset(0, 0, 1), cell.offset(0, 0, -1), cell.offset(0, 1, 0)}) {
                    QVERIFY2(!m_content->block(sample.site->at(near)).liquid,
                             qPrintable(u"seed %1: liquid at %2 %3 %4"_s.arg(sample.seed).arg(near.x).arg(near.y).arg(near.z)));
                }
            }
        }
    }

    // No pool lies in a part of a fortress or one cell around it, where it could spill in once
    // the server lets it flow; the lava sea lies below the lowest floor.
    void noPoolLiesAroundAPart()
    {
        for (const Sample &sample : samples()) {
            const QcNetherSettings &nether = sample.generator->definitions().settings;
            QList<QcBlockBox> boxes;
            for (const Plan::Room &room : sample.plan->rooms) {
                boxes.append(room.shell());
            }
            for (const Plan::Passage &passage : sample.plan->passages) {
                boxes.append(passage.shell());
            }
            for (const QcBlockBox &box : std::as_const(boxes)) {
                for (int y = std::max(box.low.y, nether.liquidLevel + 2); y <= std::min(box.high.y + 1, nether.roof - 1); ++y) {
                    for (int z = box.low.z - 1; z <= box.high.z + 1; ++z) {
                        for (int x = box.low.x - 1; x <= box.high.x + 1; ++x) {
                            QVERIFY2(!m_content->block(sample.site->at({x, y, z})).liquid, qPrintable(u"seed %1: %5 at %2 %3 %4"_s.arg(sample.seed).arg(x).arg(y).arg(z).arg(blockName(sample.site->at({x, y, z})))));
                        }
                    }
                }
            }
        }
    }

    void piersStandOnRock()
    {
        qsizetype piers = 0;
        for (const Sample &sample : samples()) {
            const Site &site = *sample.site;
            piers += sample.plan->piers.size();
            for (const QcBlockPos &top : sample.plan->piers) {
                // Nothing but solid blocks from the underside down to the lowest brick; below it the
                // ground, never air or lava.
                QcBlockPos cell = top;
                while (cell.y > 0 && blockName(site.at(cell)).contains(u"nether_brick"_s)) {
                    cell = cell.offset(0, -1, 0);
                }
                QVERIFY2(standable(site.at(cell)), qPrintable(u"pier at %1 %2 hangs at %3 over %4"_s.arg(top.x).arg(top.z).arg(cell.y).arg(blockName(site.at(cell)))));
            }
        }
        QVERIFY(piers > 0);
    }

    // With the base game's heights every room and passage stays clear of the lava sea and the
    // roof (the parser does not demand it: a mod's fortress may reach into either), and every
    // fortress lies wholly inside its region (the parser keeps a region large enough).
    void aFortressKeepsToTheCaves()
    {
        for (const qint64 seed : fixedSeeds) {
            const World world = worldOf(seed);
            keepsToTheCaves(world);
        }
    }

    // Galleries whose walls stand in the open have windows behind fences and open side openings,
    // never towards rock: behind each opening is air.
    void galleriesHaveWindowsAndOpenings()
    {
        int windows = 0;
        int openings = 0;
        for (const Sample &sample : samples()) {
            for (const Plan::Passage &passage : sample.plan->passages) {
                if (!passage.roofed || passage.rise > 0) {
                    continue;
                }
                const Openings found = openingsOf(sample, passage);
                QVERIFY2(found.problem.isEmpty(), qPrintable(found.problem));
                windows += found.windows;
                openings += found.openings;
            }
        }
        qInfo() << "gallery window cells" << windows << "side opening cells" << openings;
        QVERIFY(windows > 0);
        QVERIFY(openings > 0);
    }

    void piecesPlaceNoChestOrCropOnARib()
    {
        for (const Sample &sample : samples()) {
            for (const Plan::Room &room : sample.plan->rooms) {
                if (!room.roofed) {
                    continue;
                }
                const int inner = room.half - 1;
                for (const int x : {-inner, inner}) {
                    for (const int z : {-inner, inner}) {
                        for (int y = room.topFloor() + 1; y <= room.topFloor() + Plan::roomHeight; ++y) {
                            const QcBlockPos rib{room.centre.x + x, y, room.centre.z + z};
                            QVERIFY2(blockName(sample.site->at(rib)) == u"qtcraft:chiseled_nether_bricks"_s,
                                     qPrintable(u"seed %1: %2 at %3 %4 %5"_s.arg(sample.seed).arg(blockName(sample.site->at(rib))).arg(rib.x).arg(rib.y).arg(rib.z)));
                        }
                    }
                }
            }
        }
    }

private:
    void keepsToTheCaves(const World &world)
    {
        const QcNetherSettings &nether = world.generator->definitions().settings;
        for (const PlanPointer &plan : world.plans) {
            QList<QcBlockBox> shells;
            for (const Plan::Room &room : plan->rooms) {
                shells.append(room.shell());
            }
            for (const Plan::Passage &passage : plan->passages) {
                shells.append(passage.shell());
            }
            for (const QcBlockBox &shell : std::as_const(shells)) {
                QVERIFY2(shell.high.y < nether.roof - nether.bedrockLayers, qPrintable(u"seed %1"_s.arg(world.seed)));
                QVERIFY2(shell.low.y > nether.liquidLevel + 2, qPrintable(u"seed %1"_s.arg(world.seed)));
            }
            // Wholly inside its region.
            const int size = nether.fortress.regionSize;
            QVERIFY(plan->bounds.minX >= plan->regionX * size && plan->bounds.maxX < (plan->regionX + 1) * size);
            QVERIFY(plan->bounds.minZ >= plan->regionZ * size && plan->bounds.maxZ < (plan->regionZ + 1) * size);
        }
    }

private slots:
    void theFortressesReachEveryBiome()
    {
        QSet<QString> biomes;
        for (const qint64 seed : fixedSeeds) {
            const World world = worldOf(seed);
            for (const PlanPointer &plan : world.plans) {
                for (const Plan::Room &room : plan->rooms) {
                    biomes.insert(world.generator->biomeAt(room.centre.x, room.centre.z));
                }
            }
        }
        const QStringList all{u"qtcraft:nether_wastes"_s, u"qtcraft:soul_sand_valley"_s, u"qtcraft:crimson_forest"_s, u"qtcraft:warped_forest"_s, u"qtcraft:basalt_deltas"_s};
        for (const QString &biome : all) {
            QVERIFY2(biomes.contains(biome), qPrintable(u"no fortress in %1 (%2)"_s.arg(biome, QStringList(biomes.cbegin(), biomes.cend()).join(u' '))));
        }
    }

    void gardenChestsAndSpawnerStand()
    {
        const World world = worldOf(fixedSeeds[2]);
        const Plan &plan = *world.plans.constFirst();
        const Site site(*world.generator, plan);
        const QcNetherSettings::Fortress &settings = world.generator->definitions().settings.fortress;

        const Plan::Room &garden = *std::ranges::find(plan.rooms, Plan::RoomKind::Garden, &Plan::Room::kind);
        int crops = 0;
        for (int z = -4; z <= 4; ++z) {
            for (int x = -4; x <= 4; ++x) {
                const QcBlockPos bed = garden.centre.offset(x, 1, z);
                if (blockName(site.at(bed.offset(0, 1, 0))) == u"qtcraft:nether_wart_crop"_s) {
                    QCOMPARE(blockName(site.at(bed)), u"qtcraft:soul_sand"_s);
                    ++crops;
                }
            }
        }
        QCOMPARE(crops, 32);

        const QcBlockPos spawner = plan.spawners.constFirst();
        QCOMPARE(blockName(site.at(spawner)), u"qtcraft:spawner"_s);
        const QcChunkFurnishing here = world.generator->furnishingIn(spawner.chunk());
        const auto kind = std::ranges::find(here.spawners, spawner, &QcStructureDefinition::Spawner::at);
        QVERIFY(kind != here.spawners.end());
        QCOMPARE(kind->mob, settings.spawnerMob);
        QVERIFY(kind->mob.endsWith(u"ember_wraith"_s));
        QVERIFY(m_content->mobKind(kind->mob).has_value());

        for (const Plan::Chest &chest : plan.chests) {
            QCOMPARE(blockName(site.at(chest.at)), u"qtcraft:chest"_s);
            const QcChunkFurnishing chests = world.generator->furnishingIn(chest.at.chunk());
            const auto found = std::ranges::find(chests.chests, chest.at, &QcChunkFurnishing::Chest::at);
            QVERIFY(found != chests.chests.end());
            QCOMPARE(found->lootTable, settings.lootTable);
        }
    }

    void theLootHoldsWhatTheFortressPromises()
    {
        const QcNetherDefinitions definitions(*m_content, 1);
        const QcLootTableDefinition *table = m_content->lootTables().find(definitions.settings.fortress.lootTable);
        QVERIFY(table);
        QSet<QString> items;
        for (const auto &pool : table->pools) {
            for (const auto &entry : pool.entries) {
                QVERIFY2(entry.item.isEmpty() || m_content->itemId(entry.item).has_value(), qPrintable(entry.item));
                items.insert(entry.item);
            }
        }
        for (const char *item : {"saddle", "gold_ingot", "diamond", "obsidian", "nether_wart"}) {
            QVERIFY2(items.contains(u"qtcraft:"_s + QLatin1StringView(item)), item);
        }
    }

    void theSpawnAreaFollowsThePlans()
    {
        const auto generator = std::make_shared<QcNetherGenerator>(m_content, fixedSeeds[0]);
        const World world = worldOf(fixedSeeds[0]);
        const Plan &plan = *world.plans.constFirst();
        const QcBlockPos middle = plan.middle();

        // The tick thread never plans: a fresh generator answers only once a region is planned.
        QVERIFY(!generator->plannedStructureSitesNear(middle, 64).has_value());
        const QcStructureSites sites = generator->structureSitesNear(middle, 64);
        QVERIFY(generator->plannedStructureSitesNear(middle, 64).has_value());
        QCOMPARE(sites.spawnAreas.size(), 1);
        const QcSpawnArea &area = sites.spawnAreas.constFirst();
        QCOMPARE(area.monsters, generator->definitions().settings.fortress.monsters);
        QVERIFY(!area.monsters.isEmpty());
        for (const QcStructureDefinition::SpawnArea::Monster &monster : area.monsters) {
            QVERIFY2(m_content->mobKind(monster.mob).has_value() && m_content->mobs()[*m_content->mobKind(monster.mob)].isMonster(), qPrintable(monster.mob));
        }

        // The parts, not the box around them: a room's floor and the deck of a bridge are in it,
        // and so is the ground at the foot of a pier; high over a room and the empty corners of
        // the box are not.
        QCOMPARE(area.parts, plan.parts);
        QCOMPARE(area.parts.size(), plan.rooms.size() + plan.passages.size());
        QVERIFY(area.contains(middle));
        QVERIFY(!area.contains(plan.rooms.constFirst().shell().high.offset(0, 3, 0)));
        for (const QcBlockPos &pier : plan.piers) {
            QVERIFY(area.contains(pier));
        }
        for (const Plan::Passage &passage : plan.passages) {
            QVERIFY(area.contains(passage.floorAt(passage.length / 2, 0).offset(0, 1, 0)));
        }
        const int outside = static_cast<int>(std::ranges::count_if(QList<QcBlockPos>{{plan.bounds.minX, middle.y, plan.bounds.minZ}, {plan.bounds.maxX, middle.y, plan.bounds.maxZ},
                                                                                     {plan.bounds.minX, middle.y, plan.bounds.maxZ}, {plan.bounds.maxX, middle.y, plan.bounds.minZ}},
                                                                   [&area](QcBlockPos corner) { return !area.contains(corner); }));
        QVERIFY(outside > 0);
    }

    // The parser keeps a region large enough for the lattice, its margins and room to place it,
    // and reads the kinds of the spawn area as objects with weight and light.
    void theParserKeepsAFortressInsideItsRegion()
    {
        const auto parse = [](const QJsonObject &changes) {
            QJsonObject fortress{{u"monsters"_s, QJsonArray{QJsonObject{{u"mob"_s, u"skeleton"_s}, {u"weight"_s, 2}, {u"maxLight"_s, 7}}}}};
            for (const char *key : {"brick", "cracked", "chiseled", "lane", "fence", "stairs", "light", "soil", "crop", "chest", "spawner", "spawnerMob", "lootTable"}) {
                fortress.insert(QLatin1StringView(key), u"stone"_s);
            }
            for (auto it = changes.begin(); it != changes.end(); ++it) {
                fortress.insert(it.key(), it.value());
            }
            return QcNetherParser::parse(QJsonObject{{u"fortress"_s, fortress}}, u"qtcraft"_s);
        };
        const QcResult<QcNetherSettings> plain = parse({});
        QVERIFY2(plain, qPrintable(plain ? QString() : plain.error()));
        QCOMPARE(plain->fortress.regionSize, 448);
        QCOMPARE(plain->fortress.monsters, (QList<QcStructureDefinition::SpawnArea::Monster>{{u"qtcraft:skeleton"_s, 2, 7}}));
        // A lattice of 9 points 48 apart spans 384 cells, with 12 on each side and 16 to place it.
        QVERIFY(parse({{u"lattice"_s, 9}, {u"spacing"_s, 48}, {u"regionSize"_s, 424}}));
        QVERIFY(parse({{u"lattice"_s, 9}, {u"spacing"_s, 48}}));
        QCOMPARE(parse({{u"lattice"_s, 9}, {u"spacing"_s, 48}})->fortress.regionSize, 448);
        QVERIFY(!parse({{u"lattice"_s, 9}, {u"spacing"_s, 48}, {u"regionSize"_s, 423}}));
        QVERIFY(!parse({{u"regionSize"_s, 135}}));
        QVERIFY(parse({{u"regionSize"_s, 136}}));
        // The heights are not bounded by the sea or the roof: a fortress may reach into either.
        QVERIFY(parse({{u"lowestFloor"_s, 10}, {u"highestFloor"_s, 20}}));
        for (const QJsonValue &wrong : {QJsonValue{QJsonArray{3}}, QJsonValue{QJsonArray{u"skeleton"_s}}, QJsonValue{QJsonArray{}}, QJsonValue{u"skeleton"_s},
                                        QJsonValue{QJsonArray{QJsonObject{{u"weight"_s, 2}}}}, QJsonValue{QJsonArray{QJsonObject{{u"mob"_s, u"skeleton"_s}, {u"weight"_s, 0}}}},
                                        QJsonValue{QJsonArray{QJsonObject{{u"mob"_s, u"skeleton"_s}, {u"maxLight"_s, 16}}}}}) {
            QVERIFY2(!parse({{u"monsters"_s, wrong}}), qPrintable(QString::fromUtf8(QJsonDocument(QJsonObject{{u"m"_s, wrong}}).toJson(QJsonDocument::Compact))));
        }
        QVERIFY(!parse({{u"spawnerMob"_s, u""_s}}));
    }

    void locateFindsTheNearest()
    {
        const World world = worldOf(fixedSeeds[0]);
        const Plan &plan = *world.plans.constFirst();
        QVERIFY(world.generator->locatableNames().contains(u"fortress"_s));
        const QcLocateResult found = world.generator->locate(u"fortress", plan.middle().offset(10, 0, 10), 2000);
        QVERIFY(found.place.has_value());
        QCOMPARE(found.place->at, plan.middle());
    }

    // The showcase's fortress stands beside the arrival station, named, and /locate finds it; the
    // shared samples walk it and look for liquid in it.
    void theShowcaseHasAFortressBesideItsStation()
    {
        for (const qint64 seed : {qint64{1}, QcTestUtil::randomSeed()}) {
            const std::shared_ptr<QcNetherGenerator> showcase = showcaseOf(seed);
            QVERIFY(showcase);
            const PlanPointer plan = showcase->fortress().demonstration();
            QVERIFY(plan);
            const QcBlockPos arrival = showcase->spawnPoint();
            QVERIFY(!plan->bounds.intersects(QcXzRect{arrival.x, arrival.z, arrival.x, arrival.z}.grown(24)));
            QVERIFY(plan->bounds.intersects(QcXzRect{arrival.x, arrival.z, arrival.x, arrival.z}.grown(64)));

            QStringList texts;
            for (int z = plan->bounds.minZ >> 4; z <= plan->bounds.maxZ >> 4; ++z) {
                for (int x = plan->bounds.minX >> 4; x <= plan->bounds.maxX >> 4; ++x) {
                    const QList<QcWorldLabel> labels = showcase->labelsIn({x, z});
                    for (const QcWorldLabel &label : labels) {
                        texts.append(label.text);
                    }
                }
            }
            QVERIFY2(texts.contains(u"structure.qtcraft.fortress"_s), qPrintable(texts.join(u' ')));
            QVERIFY(texts.contains(u"block.qtcraft.spawner"_s));
            QVERIFY(texts.contains(u"block.qtcraft.nether_wart_crop"_s));

            const QcLocateResult found = showcase->locate(u"fortress", arrival, 2000);
            QVERIFY(found.place.has_value());
            QCOMPARE(found.place->at, plan->middle());
        }
    }

    void ruinsKeepOutOfAFortress()
    {
        for (const qint64 seed : fixedSeeds) {
            const World world = worldOf(seed);
            for (const PlanPointer &plan : world.plans) {
                const QcBlockPos at{plan->bounds.minX, 0, plan->bounds.minZ};
                for (const QcNetherDefinitions::Structure &kind : world.generator->definitions().structures) {
                    const int spanX = plan->bounds.maxX - plan->bounds.minX;
                    const int spanZ = plan->bounds.maxZ - plan->bounds.minZ;
                    for (int z = 0; z <= spanZ + kind.spacing; z += kind.spacing) {
                        for (int x = 0; x <= spanX + kind.spacing; x += kind.spacing) {
                            const auto region = [&kind](int value) { return static_cast<int>(std::floor(static_cast<double>(value) / kind.spacing)); };
                            const auto ruin = world.generator->structures().planOf(kind, region(at.x + x), region(at.z + z));
                            if (ruin) {
                                const QcXzRect footprint{ruin->origin().x, ruin->origin().z, ruin->origin().x + ruin->sizeX() - 1, ruin->origin().z + ruin->sizeZ() - 1};
                                QVERIFY(!footprint.intersects(plan->bounds));
                            }
                        }
                    }
                }
            }
        }
    }

private:
    std::shared_ptr<QcContentRegistry> m_content;
    QList<Sample> m_samples;
};

QTEST_GUILESS_MAIN(TestNetherFortress)
#include "test_nether_fortress.moc"
