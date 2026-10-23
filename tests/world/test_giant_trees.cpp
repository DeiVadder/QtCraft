#include "common/physics/QcPhysics.h"
#include "common/world/QcChunk.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/plants/QcPlantManager.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/terrain/QcTreeShape.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = QcPhysics::tickDuration;
constexpr int groundY = 63;

int ticksFor(double seconds)
{
    return static_cast<int>(std::ceil(seconds / dt));
}

QcChunkPos chunkOf(QcBlockPos pos)
{
    return {static_cast<int>(std::floor(pos.x / static_cast<double>(QcWorld::chunkSize))),
        static_cast<int>(std::floor(pos.z / static_cast<double>(QcWorld::chunkSize)))};
}

} // namespace

// The jungle wood set and the giant trees on a 2×2 trunk (decision E-822): the wood family, the
// shapes, their placement in the world and four saplings in a square growing into one.
class TestGiantTrees : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcBlockId block(const QString &name) const { return *m_content->blockId(name); }
    QString nameOf(std::optional<QcBlockId> id) const { return id ? m_content->block(*id).name : u"unloaded"_s; }

    QcTestWorld grassWorld() const
    {
        QcTestWorld world(*m_content);
        world.fillBox({-12, groundY, -12}, {12, groundY, 12}, block(u"qtcraft:grass_block"_s));
        return world;
    }

    // Plants `sapling` at every position, starts the timer of the first and lets them grow.
    void grow(QcTestWorld &world, const QList<QcBlockPos> &positions, const QString &sapling) const
    {
        const QcBlockId id = block(sapling);
        for (const QcBlockPos &pos : positions) {
            world.set(pos, id);
        }

        QcPlantManager plants(m_content, 1);
        QcBlockEntities entities(m_content);
        plants.saplingPlaced(entities, positions.first(), id);
        const auto setBlock = [&world](QcBlockPos pos, QcBlockId block) {
            world.set(pos, block);
            return true;
        };
        const auto breakBlock = [&world](QcBlockPos pos) { world.set(pos, qcAirBlockId); };
        for (int i = 0; i < ticksFor(m_content->block(id).sapling.maxSeconds + 1.0); ++i) {
            plants.tick(world, entities, dt, {[](QcBlockPos) { return true; }, setBlock, breakBlock});
        }
    }

    // The block a vine at `pos` holds on to.
    QcBlockPos holderOf(QcBlockId vine, QcBlockPos pos) const
    {
        const QString attach = m_content->property(vine, u"attach"_s);
        if (attach == u"north"_s) {
            return pos.offset(0, 0, -1);
        }
        if (attach == u"south"_s) {
            return pos.offset(0, 0, 1);
        }
        return attach == u"east"_s ? pos.offset(1, 0, 0) : pos.offset(-1, 0, 0);
    }

    // The positions of the blocks named `name` in the box around a grown tree at (0, groundY + 1, 0)
    // (14 × 14 columns, 33 high).
    QList<QcBlockPos> blocksAround(const QcTestWorld &world, const QString &name) const
    {
        QList<QcBlockPos> found;
        for (int cell = 0; cell < 14 * 14 * 33; ++cell) {
            const QcBlockPos pos{cell % 14 - 6, groundY + 1 + cell / (14 * 14), cell / 14 % 14 - 6};
            if (nameOf(world.blockAt(pos)) == name) {
                found.append(pos);
            }
        }
        return found;
    }

    struct VineCheck
    {
        int vines = 0;
        // The vines without a solid block to hold on to.
        QStringList loose;
    };

    // The first jungle giant of the world with `seed`, searched outward from the origin.
    std::optional<QcGiantTreePlacer::Giant> jungleGiant(const QcTerrainGenerator &terrain) const
    {
        for (int z = -48; z <= 48; ++z) {
            for (int x = -48; x <= 48; ++x) {
                const QList<QcGiantTreePlacer::Giant> giants = terrain.giantTrees().giantsNear({x, z});
                for (const QcGiantTreePlacer::Giant &giant : giants) {
                    if (m_content->worldgen().features()[giant.feature].name == u"qtcraft:giant_jungle_tree"_s) {
                        return giant;
                    }
                }
            }
        }
        return std::nullopt;
    }

    // The vines of the 4 × 4 chunks around the first jungle giant of the world with `seed`, generated
    // with a ring of neighbours so vines at a border find their holder.
    VineCheck vinesNearJungleGiant(qint64 seed) const
    {
        const QcTerrainGenerator terrain(m_content, seed);
        const std::optional<QcGiantTreePlacer::Giant> jungle = jungleGiant(terrain);
        if (!jungle) {
            return {};
        }

        const QcChunkPos centre = chunkOf(jungle->base);
        QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
        for (int dz = -3; dz <= 3; ++dz) {
            for (int dx = -3; dx <= 3; ++dx) {
                chunks.insert({centre.x + dx, centre.z + dz}, terrain.generate({centre.x + dx, centre.z + dz}));
            }
        }
        const auto blockAt = [&chunks](QcBlockPos pos) {
            const QcChunkPos chunk = chunkOf(pos);
            return chunks.value(chunk)->block(pos.x - chunk.x * QcWorld::chunkSize, pos.y, pos.z - chunk.z * QcWorld::chunkSize);
        };

        VineCheck check;
        const int side = 4 * QcWorld::chunkSize;
        const QcBlockPos corner{(centre.x - 2) * QcWorld::chunkSize, 0, (centre.z - 2) * QcWorld::chunkSize};
        for (int cell = 0; cell < side * side * QcWorld::worldHeight; ++cell) {
            const QcBlockPos pos = corner.offset(cell % side, cell / (side * side), cell / side % side);
            const QcBlockId id = blockAt(pos);
            if (nameOf(id) != u"qtcraft:vines"_s) {
                continue;
            }
            ++check.vines;
            const QcBlockId holder = blockAt(holderOf(id, pos));
            if (!m_content->block(holder).solid) {
                check.loose.append(u"%1 at %2 %3 %4"_s.arg(nameOf(holder)).arg(pos.x).arg(pos.y).arg(pos.z));
            }
        }
        return check;
    }

    // The four corners of the square with the north-west corner `corner`.
    static QList<QcBlockPos> square(QcBlockPos corner)
    {
        return {corner, corner.offset(1, 0, 0), corner.offset(0, 0, 1), corner.offset(1, 0, 1)};
    }

private slots:
    void jungleWoodHasEverySpruceBlock()
    {
        int spruceBlocks = 0;
        for (const QcBlockDefinition &definition : m_content->blocks()) {
            if (!definition.name.contains(u"spruce"_s)) {
                continue;
            }
            ++spruceBlocks;
            QString jungle = definition.name;
            jungle.replace(u"spruce"_s, u"jungle"_s);
            QVERIFY2(m_content->blockId(jungle).has_value(), qPrintable(jungle));
        }
        // Logs, wood, stripped, planks, leaves, sapling, door, trapdoor, sign and the parts.
        QVERIFY(spruceBlocks >= 17);
        QCOMPARE(m_content->block(block(u"qtcraft:jungle_log"_s)).strippedInto, u"qtcraft:stripped_jungle_log"_s);
        QCOMPARE(m_content->block(block(u"qtcraft:jungle_sapling"_s)).sapling.giantFeature, u"qtcraft:giant_jungle_tree"_s);
        QCOMPARE(m_content->block(block(u"qtcraft:spruce_sapling"_s)).sapling.giantFeature, u"qtcraft:giant_spruce_tree"_s);
    }

    void jungleWoodHasEverySpruceRecipe()
    {
        QStringList jungleResults;
        for (const QcRecipeDefinition &recipe : m_content->recipes()) {
            jungleResults.append(recipe.resultItem);
        }

        int spruceRecipes = 0;
        for (const QcRecipeDefinition &recipe : m_content->recipes()) {
            if (!recipe.resultItem.contains(u"spruce"_s)) {
                continue;
            }
            ++spruceRecipes;
            QString jungle = recipe.resultItem;
            jungle.replace(u"spruce"_s, u"jungle"_s);
            QVERIFY2(jungleResults.contains(jungle), qPrintable(jungle));
        }
        QVERIFY(spruceRecipes >= 10);
    }

    void shapesStayWithinTheirReachAndKeepTheirLeaves()
    {
        for (const QcFeatureDefinition &feature : m_content->worldgen().features()) {
            const QcTreeShape::Params params = QcTreeShape::paramsOf(*m_content, feature);
            if (feature.type != QcFeatureDefinition::Type::Tree) {
                continue;
            }
            for (const double random : {0.0, 0.5, 0.99}) {
                QList<QcBlockPos> trunks;
                QList<QcBlockPos> leaves;
                QList<QcBlockPos> outside;
                const int reach = QcTreeShape::reach(params);
                const int far = reach + (params.giant() ? 1 : 0);
                QcTreeShape::build(params, {0, 64, 0}, random, 7, [&](QcBlockPos pos, QcBlockId id, bool) {
                    if (pos.x < -reach || pos.x > far || pos.z < -reach || pos.z > far) {
                        outside.append(pos);
                    }
                    if (m_content->isSameBlock(id, params.trunk)) {
                        trunks.append(pos);
                    } else if (id == params.leaves) {
                        leaves.append(pos);
                    }
                    return true;
                });
                QVERIFY2(outside.isEmpty(), qPrintable(feature.name));
                QVERIFY(!leaves.isEmpty());

                // Every leaf has a trunk block within the reach of leaf decay.
                for (const QcBlockPos &leaf : std::as_const(leaves)) {
                    const bool held = std::ranges::any_of(trunks, [&](QcBlockPos trunk) {
                        return std::abs(trunk.x - leaf.x) <= QcPlantManager::leafReach && std::abs(trunk.y - leaf.y) <= QcPlantManager::leafReach
                               && std::abs(trunk.z - leaf.z) <= QcPlantManager::leafReach;
                    });
                    QVERIFY2(held, qPrintable(feature.name));
                }
            }
        }
    }

    void giantTreesGrowInTheirBiomesTheSameInEveryChunk()
    {
        const QcTerrainGenerator terrain(m_content, 42);
        std::optional<QcGiantTreePlacer::Giant> spruce;
        std::optional<QcGiantTreePlacer::Giant> jungle;
        for (int z = -48; z <= 48 && (!spruce || !jungle); ++z) {
            for (int x = -48; x <= 48 && (!spruce || !jungle); ++x) {
                const QList<QcGiantTreePlacer::Giant> giants = terrain.giantTrees().giantsNear({x, z});
                for (const QcGiantTreePlacer::Giant &giant : giants) {
                    const QString name = m_content->worldgen().features()[giant.feature].name;
                    (name == u"qtcraft:giant_spruce_tree"_s ? spruce : jungle) = giant;
                    const QString biome = terrain.biomeAt(giant.base.x, giant.base.z);
                    const QStringList biomes = name == u"qtcraft:giant_spruce_tree"_s ? QStringList{u"qtcraft:taiga"_s, u"qtcraft:snowy_taiga"_s}
                                                                                     : QStringList{u"qtcraft:jungle"_s};
                    QVERIFY2(biomes.contains(biome), qPrintable(name + u' ' + biome));
                }
            }
        }
        QVERIFY(spruce.has_value());
        QVERIFY(jungle.has_value());

        for (const QcGiantTreePlacer::Giant &giant : {*spruce, *jungle}) {
            // The chunk of each trunk block, from a fresh generator, alike to the first one's.
            const QcTerrainGenerator other(m_content, 42);
            const QList<QcBlockPos> trunks = square(giant.base);
            for (const QcBlockPos &trunk : trunks) {
                const QcChunkPos pos = chunkOf(trunk);
                const std::shared_ptr<QcChunk> first = terrain.generate(pos);
                const std::shared_ptr<QcChunk> second = other.generate(pos);
                const int lx = trunk.x - pos.x * QcWorld::chunkSize;
                const int lz = trunk.z - pos.z * QcWorld::chunkSize;
                const QString log = nameOf(first->block(lx, trunk.y + 8, lz));
                QVERIFY2(log.endsWith(u"_log"_s), qPrintable(log));
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    QCOMPARE(second->block(lx, y, lz), first->block(lx, y, lz));
                }
            }
        }
    }

    void fourSaplingsInASquareGrowIntoAGiantFromEveryCorner()
    {
        for (int corner = 0; corner < 4; ++corner) {
            QcTestWorld world = grassWorld();
            const QList<QcBlockPos> saplings = square({0, groundY + 1, 0});
            QList<QcBlockPos> planted = saplings;
            planted.swapItemsAt(0, corner);
            grow(world, planted, u"qtcraft:jungle_sapling"_s);

            for (const QcBlockPos &pos : saplings) {
                QCOMPARE(nameOf(world.blockAt(pos)), u"qtcraft:jungle_log"_s);
                QCOMPARE(nameOf(world.blockAt(pos.offset(0, 17, 0))), u"qtcraft:jungle_log"_s);
            }
            // The trunk is the square, nothing beside it (vines may hang there).
            QVERIFY(nameOf(world.blockAt({-1, groundY + 1, 0})) != u"qtcraft:jungle_log"_s);
            QVERIFY(nameOf(world.blockAt({2, groundY + 1, 1})) != u"qtcraft:jungle_log"_s);
        }
    }

    void giantJungleTreesCarryVinesThatHoldOn()
    {
        QcTestWorld world = grassWorld();
        const QList<QcBlockPos> saplings = square({0, groundY + 1, 0});
        grow(world, saplings, u"qtcraft:jungle_sapling"_s);

        // Every block of the box around the tree (14 × 14 columns, 33 high) as one index.
        int vines = 0;
        for (int cell = 0; cell < 14 * 14 * 33; ++cell) {
            const QcBlockPos pos{cell % 14 - 6, groundY + 1 + cell / (14 * 14), cell / 14 % 14 - 6};
            const std::optional<QcBlockId> id = world.blockAt(pos);
            if (!id || nameOf(id) != u"qtcraft:vines"_s) {
                continue;
            }
            ++vines;
            const QString held = nameOf(world.blockAt(holderOf(*id, pos)));
            QVERIFY2(held.startsWith(u"qtcraft:jungle_"_s), qPrintable(held));
        }
        QVERIFY(vines > 0);
    }

    // Every vine the world generator leaves around a jungle giant holds on to a solid block, also
    // across chunk borders, where crowns of two trees meet and where plants stand on slopes (vines
    // without hold drop as items). Several seeds, so several jungles.
    void generatedVinesHoldOn()
    {
        for (const qint64 seed : {42, 7, 2026}) {
            const VineCheck check = vinesNearJungleGiant(seed);
            QVERIFY2(check.vines > 0, qPrintable(QString::number(seed)));
            const QString report = u"seed %1: %2 of %3 vines without hold: "_s.arg(seed).arg(check.loose.size()).arg(check.vines)
                                   + check.loose.mid(0, 8).join(u", "_s);
            QVERIFY2(check.loose.isEmpty(), qPrintable(report));
        }
    }

    // A grown tree hangs vines only on leaves it placed: leaf spots taken by a torch stay torches and
    // carry no vine, the vines of another tree give way to leaves.
    void grownVinesHoldOnBesideTorchesAndForeignVines()
    {
        const QList<QcBlockPos> saplings = square({0, groundY + 1, 0});
        QcTestWorld clean = grassWorld();
        grow(clean, saplings, u"qtcraft:jungle_sapling"_s);
        const QList<QcBlockPos> leaves = blocksAround(clean, u"qtcraft:jungle_leaves"_s);
        QVERIFY(leaves.size() > 20);

        // The same tree again (same random sequence), every second leaf spot taken by a torch, the
        // others by a vine of another tree.
        QcTestWorld world = grassWorld();
        const QcBlockId foreignVine = m_content->withProperty(block(u"qtcraft:vines"_s), u"attach"_s, u"north"_s);
        for (qsizetype i = 0; i < leaves.size(); ++i) {
            world.set(leaves[i], i % 2 == 0 ? block(u"qtcraft:torch"_s) : foreignVine);
        }
        grow(world, saplings, u"qtcraft:jungle_sapling"_s);
        QCOMPARE(nameOf(world.blockAt(saplings.first().offset(0, 10, 0))), u"qtcraft:jungle_log"_s);

        QStringList loose;
        const QList<QcBlockPos> vines = blocksAround(world, u"qtcraft:vines"_s);
        for (const QcBlockPos &pos : vines) {
            const QcBlockId holder = world.blockAt(holderOf(*world.blockAt(pos), pos)).value_or(qcAirBlockId);
            if (!m_content->block(holder).solid) {
                loose.append(u"%1 at %2 %3 %4"_s.arg(nameOf(holder)).arg(pos.x).arg(pos.y).arg(pos.z));
            }
        }
        QVERIFY(!vines.isEmpty());
        const QString report = loose.mid(0, 8).join(u", "_s);
        QVERIFY2(loose.isEmpty(), qPrintable(report));
        QCOMPARE(blocksAround(world, u"qtcraft:torch"_s).size(), (leaves.size() + 1) / 2);
    }

    void giantSpruceGrowsFromFourSpruceSaplings()
    {
        QcTestWorld world = grassWorld();
        const QList<QcBlockPos> saplings = square({2, groundY + 1, -3});
        grow(world, saplings, u"qtcraft:spruce_sapling"_s);
        for (const QcBlockPos &pos : saplings) {
            QCOMPARE(nameOf(world.blockAt(pos.offset(0, 14, 0))), u"qtcraft:spruce_log"_s);
        }
    }

    void aGiantNeedsRoom()
    {
        QcTestWorld world = grassWorld();
        world.set({1, groundY + 6, 1}, block(u"qtcraft:stone"_s));
        const QList<QcBlockPos> saplings = square({0, groundY + 1, 0});
        grow(world, saplings, u"qtcraft:jungle_sapling"_s);
        for (const QcBlockPos &pos : saplings) {
            QCOMPARE(nameOf(world.blockAt(pos)), u"qtcraft:jungle_sapling"_s);
        }
    }

    void aSingleSaplingOrThreeGrowIntoASmallTree()
    {
        for (const int count : {1, 3}) {
            QcTestWorld world = grassWorld();
            const QList<QcBlockPos> saplings = square({0, groundY + 1, 0});
            grow(world, saplings.first(count), u"qtcraft:jungle_sapling"_s);
            QCOMPARE(nameOf(world.blockAt({0, groundY + 1, 0})), u"qtcraft:jungle_log"_s);
            QCOMPARE(nameOf(world.blockAt({0, groundY + 6, 0})), u"qtcraft:jungle_log"_s);
            // The other saplings stay saplings; nothing grows taller than a small jungle tree.
            QVERIFY(world.blockAt({0, groundY + 12, 0}) == qcAirBlockId);
            if (count == 3) {
                QCOMPARE(nameOf(world.blockAt({1, groundY + 1, 0})), u"qtcraft:jungle_sapling"_s);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestGiantTrees)
#include "test_giant_trees.moc"
