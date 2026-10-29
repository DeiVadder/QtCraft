// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcTestUtil.h"
#include "world/QcVillageTestUtil.h"

#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace QcVillageTestUtil;

// Roads and fields of villages (decision E-406): a road surface per rank chosen per village from
// the style, slabs where a road steps up a block, fields planted from the style's crops (ripe and
// growing, a mod's list honoured), and no growing crops anywhere but in village fields. All of it
// a pure function of the seed like the rest of a village.
class TestVillageFields : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;
    static constexpr int rings = 8;

    QString nameOf(QcBlockId block) const { return m_content->block(block).name; }
    bool isCrop(QcBlockId block) const { return m_content->block(block).crop.isCrop(); }

    // The crop cells of every piece of the plan, as placed (of `content`, else the base content).
    QList<QcBlockId> cropsOf(const QcVillagePlan &plan, const QcContentRegistry *content = nullptr) const
    {
        const QcContentRegistry &blocks = content ? *content : *m_content;
        QList<QcBlockId> crops;
        for (const QcStructurePlacement &piece : plan.pieces) {
            const QcStructure &structure = piece.structure();
            for (int y = 0; y < structure.sizeY(); ++y) {
                for (int z = 0; z < structure.sizeZ(); ++z) {
                    for (int x = 0; x < structure.sizeX(); ++x) {
                        const std::optional<QcBlockId> block = structure.block(x, y, z);
                        if (block && blocks.block(*block).crop.isCrop()) {
                            crops.append(*block);
                        }
                    }
                }
            }
        }
        return crops;
    }

    std::shared_ptr<const QcVillagePlan> firstPlan(const QString &style, const std::function<bool(const QcVillagePlan &)> &wanted = {}) const
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : m_plans) {
            if (plan->style == style && (!wanted || wanted(*plan))) {
                return plan;
            }
        }
        return nullptr;
    }

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcTerrainGenerator m_generator{m_content, seed};
    QList<std::shared_ptr<const QcVillagePlan>> m_plans = plansAround(m_generator, rings);

private slots:
    void roadsTakeASurfaceOfTheirRankPerVillage()
    {
        const QHash<QString, QPair<QStringList, QString>> expected{
            {u"qtcraft:plains"_s, {{u"qtcraft:cobblestone"_s, u"qtcraft:gravel"_s}, u"qtcraft:dirt_path"_s}},
            {u"qtcraft:taiga"_s, {{u"qtcraft:cobblestone"_s, u"qtcraft:gravel"_s}, u"qtcraft:dirt_path"_s}},
            {u"qtcraft:desert"_s, {{u"qtcraft:terracotta"_s, u"qtcraft:packed_mud"_s}, u"qtcraft:white_terracotta"_s}},
            {u"qtcraft:swamp"_s, {{u"qtcraft:spruce_planks"_s}, u"qtcraft:spruce_slab"_s}},
            {u"qtcraft:snowy"_s, {{u"qtcraft:packed_ice"_s, u"qtcraft:gravel"_s}, u"qtcraft:dirt_path"_s}},
        };
        QSet<QString> seen;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            QVERIFY2(expected.contains(plan->style), qPrintable(plan->style));
            const auto &[main, branch] = expected.value(plan->style);
            QVERIFY2(main.contains(nameOf(plan->pathBlock)), qPrintable(nameOf(plan->pathBlock)));
            QCOMPARE(nameOf(plan->branchPathBlock), branch);
            seen.insert(plan->style + nameOf(plan->pathBlock));
        }
        // Both main road surfaces of the plains occur.
        QVERIFY(seen.contains(u"qtcraft:plainsqtcraft:cobblestone"_s));
        QVERIFY(seen.contains(u"qtcraft:plainsqtcraft:gravel"_s));
    }

    void stepsLieOnRoadsBelowAHigherRoadCell()
    {
        int steps = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            QHash<std::pair<int, int>, int> heights;
            for (const QList<QcBlockPos> *cells : {&plan->branchRoads, &plan->roads}) {
                for (const QcBlockPos &cell : *cells) {
                    heights.insert({cell.x, cell.z}, cell.y);
                }
            }
            for (const QcVillagePlan::Step &step : plan->steps) {
                ++steps;
                QCOMPARE(heights.value({step.at.x, step.at.z}, -100), step.at.y - 1);
                QVERIFY(nameOf(step.block).endsWith(u"_slab"_s));
                int higher = 0;
                for (const auto &[dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    higher += heights.value({step.at.x + dx, step.at.z + dz}, -100) == step.at.y ? 1 : 0;
                }
                QVERIFY(higher > 0);
            }
        }
        QVERIFY2(steps > 20, qPrintable(QString::number(steps)));
    }

    void roadsAreLevelAcrossTheirWidth()
    {
        // The cells across a straight road lie at the height of its middle line (decision E-409), so
        // its steps run across the whole road, not in a jagged line along it.
        int rows = 0;
        int level = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            QHash<std::pair<int, int>, int> heights;
            for (const QcBlockPos &cell : plan->roads) {
                heights.insert({cell.x, cell.z}, cell.y);
            }
            for (const QcVillagePlan::Run &run : plan->runs) {
                if ((run.dx != 0 && run.dz != 0) || run.branch) {
                    continue;
                }
                for (int d = 1; d + 1 < run.length; ++d) {
                    const std::pair<int, int> middle{run.x + run.dx * d, run.z + run.dz * d};
                    const auto left = heights.constFind({middle.first - run.dz, middle.second + run.dx});
                    const auto right = heights.constFind({middle.first + run.dz, middle.second - run.dx});
                    const auto centre = heights.constFind(middle);
                    if (left == heights.cend() || right == heights.cend() || centre == heights.cend()) {
                        continue;
                    }
                    ++rows;
                    level += *left == *centre && *right == *centre ? 1 : 0;
                }
            }
        }
        QVERIFY2(rows > 500 && level * 100 >= rows * 97, qPrintable(u"%1 of %2 rows level"_s.arg(level).arg(rows)));
    }

    void roadsAndStepsAreBuiltTheSameInAnyOrder()
    {
        // A village with steps and both ranks of roads, built in order and backwards on threads.
        const std::shared_ptr<const QcVillagePlan> plan =
            firstPlan(u"qtcraft:plains"_s, [](const QcVillagePlan &p) { return p.steps.size() >= 3 && !p.branchRoads.isEmpty(); });
        QVERIFY(plan);
        const QList<QcChunkPos> positions = chunksOf(*plan);
        const Chunks first = generateInOrder(m_generator, positions);
        const QcTerrainGenerator fresh(m_content, seed);
        const Chunks second = generateOnThreads(fresh, positions);
        for (const QcChunkPos &pos : positions) {
            QVERIFY(sameBlocks(*first.value(pos), *second.value(pos)));
        }

        for (const QcVillagePlan::Step &step : plan->steps) {
            QCOMPARE(blockIn(first, step.at), step.block);
        }
        // The road from the centre keeps its surface where a branch meets it.
        int main = 0;
        for (const QcBlockPos &cell : plan->roads) {
            main += blockIn(first, cell) == plan->pathBlock ? 1 : 0;
        }
        QVERIFY2(qsizetype(main) * 10 >= plan->roads.size() * 9, qPrintable(u"%1 of %2"_s.arg(main).arg(plan->roads.size())));
        int branch = 0;
        for (const QcBlockPos &cell : plan->branchRoads) {
            branch += blockIn(first, cell) == plan->branchPathBlock ? 1 : 0;
        }
        QVERIFY(branch > 0);
    }

    void fieldsGrowTheCropsOfTheirStyle()
    {
        QHash<QString, int> crops;
        int ripe = 0;
        int growing = 0;
        int fieldsWithPaths = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const QList<QcBlockId> planted = cropsOf(*plan);
            // The plan lists every crop cell as its fields (what its farmers harvest, decision E-411).
            QCOMPARE(plan->fields.size(), planted.size());
            for (const QcBlockId block : planted) {
                ++crops[nameOf(block)];
                const QcBlockProperty *stages = m_content->block(block).property(u"age"_s);
                QVERIFY(stages);
                (m_content->property(block, u"age"_s) == stages->values.back() ? ripe : growing) += 1;
            }
            for (const QcStructurePlacement &piece : plan->pieces) {
                fieldsWithPaths += shortName(piece) == u"village_plains_farm_b"_s ? 1 : 0;
            }
        }
        // Only the crops of the styles, all three, ripe and growing mixed.
        QCOMPARE(QSet<QString>(crops.keyBegin(), crops.keyEnd()),
                 (QSet<QString>{u"qtcraft:wheat_crop"_s, u"qtcraft:carrot_crop"_s, u"qtcraft:potato_crop"_s}));
        QVERIFY2(ripe > 100 && growing > 100, qPrintable(u"%1 ripe, %2 growing"_s.arg(ripe).arg(growing)));
        QVERIFY(fieldsWithPaths > 0);
    }

    void aModsCropListIsHonoured()
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcBlockDefinition beet = m_content->block(*m_content->blockId(u"qtcraft:potato_crop"_s));
        beet.name = u"testmod:beet_crop"_s;
        QVERIFY(content->registerBlock(beet));
        for (const char *name : {"qtcraft:plains", "qtcraft:taiga", "qtcraft:desert", "qtcraft:swamp", "qtcraft:snowy"}) {
            QcVillageStyleDefinition style = *std::ranges::find(content->villageCatalog().styles(), QString::fromLatin1(name), &QcVillageStyleDefinition::name);
            style.crops = {{u"testmod:beet_crop"_s, 1}, {u"qtcraft:stone"_s, 5}};
            content->villageCatalog().registerStyle(style);
        }

        const QcVillageStyles styles(*content);
        const QStringList &warnings = styles.warnings();
        QVERIFY2(warnings.join(u'\n').contains(u"'qtcraft:stone' is no crop"_s), qPrintable(warnings.join(u'\n')));
        const QcTerrainGenerator generator(content, seed);
        int beets = 0;
        const QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 4);
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QList<QcBlockId> planted = cropsOf(*plan, content.get());
            for (const QcBlockId block : planted) {
                QCOMPARE(content->block(block).name, u"testmod:beet_crop"_s);
                ++beets;
            }
        }
        QVERIFY(beets > 50);
    }

    void noBiomePlantsCropsOrWildCrops()
    {
        for (const QcBiomeDefinition &biome : m_content->worldgen().biomes()) {
            for (const QcFeaturePlacement &placement : biome.features) {
                const auto feature = std::ranges::find(m_content->worldgen().features(), placement.feature, &QcFeatureDefinition::name);
                QVERIFY(feature != m_content->worldgen().features().cend());
                const std::optional<QcBlockId> block = m_content->blockId(feature->trunkBlock);
                QVERIFY2(!block || !isCrop(*block), qPrintable(biome.name + u" "_s + placement.feature));
                QVERIFY2(!feature->trunkBlock.contains(u"wild_"_s), qPrintable(biome.name + u" "_s + placement.feature));
            }
        }
    }

    void cropsGrowOnlyInVillages()
    {
        // The chunks around a plains village with fields and its surroundings: crops inside the
        // villages' areas only, none outside, no wild crops anywhere.
        const std::shared_ptr<const QcVillagePlan> plan =
            firstPlan(u"qtcraft:plains"_s, [this](const QcVillagePlan &p) { return cropsOf(p).size() > 20; });
        QVERIFY(plan);
        const QcChunkPos low = QcBlockPos{plan->minX, 0, plan->minZ}.chunk();
        const QcChunkPos high = QcBlockPos{plan->maxX, 0, plan->maxZ}.chunk();
        // Whether a block is a wild crop, once per block: the name is not looked at in every cell.
        QHash<QcBlockId, bool> wild;
        const auto isWild = [&](QcBlockId block) {
            if (const auto found = wild.constFind(block); found != wild.constEnd()) {
                return *found;
            }
            const bool isWildCrop = nameOf(block).contains(u"wild_"_s);
            wild.insert(block, isWildCrop);
            return isWildCrop;
        };
        int inside = 0;
        for (int cz = low.z - 6; cz <= high.z + 6; ++cz) {
            for (int cx = low.x - 6; cx <= high.x + 6; ++cx) {
                const std::shared_ptr<QcChunk> chunk = m_generator.generate({cx, cz});
                for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::worldHeight; ++cell) {
                    const int x = cell % QcWorld::chunkSize;
                    const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
                    const int y = cell / (QcWorld::chunkSize * QcWorld::chunkSize);
                    const QcBlockId block = chunk->block(x, y, z);
                    if (isWild(block)) {
                        QFAIL(qPrintable(nameOf(block)));
                    }
                    if (!isCrop(block)) {
                        continue;
                    }
                    const int wx = cx * QcWorld::chunkSize + x;
                    const int wz = cz * QcWorld::chunkSize + z;
                    const bool village = std::ranges::any_of(m_plans, [&](const auto &other) { return other->covers(wx, wz, 0); });
                    QVERIFY2(village, qPrintable(nameOf(block)));
                    ++inside;
                }
            }
        }
        QVERIFY2(inside > 20, qPrintable(QString::number(inside)));
    }
};

QTEST_GUILESS_MAIN(TestVillageFields)
#include "test_village_fields.moc"
