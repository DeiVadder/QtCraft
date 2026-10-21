#include "QcTestUtil.h"
#include "world/QcVillageTestUtil.h"
#include "worldgen/structures/QcStateRotation.h"

#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace QcVillageTestUtil;

// Village generation (decision E-403): where villages go, in which style, that they stand on the
// ground, and that a village spanning chunks comes out the same in any generation order.
class TestVillageGeneration : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // Checks every stair between `low` and `high` (heights) of the chunk: bottom half, the cell
    // beyond its low side and the one above that open, a block beyond its high side. Returns how
    // many stairs it checked, -1 on the first that fails.
    int checkedRoofStairs(const QcChunk &chunk, int low, int high) const
    {
        const auto at = [&chunk](int x, int y, int z) {
            return x >= 0 && z >= 0 && x < QcWorld::chunkSize && z < QcWorld::chunkSize ? chunk.block(x, y, z) : qcAirBlockId;
        };
        int count = 0;
        for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize * (high - low); ++cell) {
            const int x = cell % QcWorld::chunkSize;
            const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
            const int y = low + cell / (QcWorld::chunkSize * QcWorld::chunkSize);
            const QcBlockId block = chunk.block(x, y, z);
            if (!m_content->block(block).name.endsWith(u"_stairs"_s)) {
                continue;
            }
            const QString facing = m_content->property(block, u"facing"_s);
            const int fx = facing == u"east"_s ? 1 : (facing == u"west"_s ? -1 : 0);
            const int fz = facing == u"south"_s ? 1 : (facing == u"north"_s ? -1 : 0);
            const bool sloped = at(x - fx, y + 1, z - fz) == qcAirBlockId && at(x - fx, y, z - fz) == qcAirBlockId && at(x + fx, y, z + fz) != qcAirBlockId;
            if (!sloped || m_content->property(block, u"half"_s) != u"bottom"_s) {
                return -1;
            }
            ++count;
        }
        return count;
    }

    // The upside-down stairs of a structure placed with `turns` that lean against a block with open
    // air on their other side and a block above; -1 on the first that does not.
    int corbelsOf(const QcStructure &structure, int turns) const
    {
        const QcStructurePlacement placed(std::make_shared<QcStructure>(structure), {2, 64, 2}, turns, *m_content);
        QcChunk chunk({0, 0});
        placed.writeInto(chunk);
        int count = 0;
        for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize * 32; ++cell) {
            const int x = cell % QcWorld::chunkSize;
            const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
            const int y = 64 + cell / (QcWorld::chunkSize * QcWorld::chunkSize);
            const QcBlockId block = chunk.block(x, y, z);
            if (!m_content->block(block).name.endsWith(u"_stairs"_s)) {
                continue;
            }
            const QString facing = m_content->property(block, u"facing"_s);
            const int fx = facing == u"east"_s ? 1 : (facing == u"west"_s ? -1 : 0);
            const int fz = facing == u"south"_s ? 1 : (facing == u"north"_s ? -1 : 0);
            const bool leans = chunk.block(x + fx, y, z + fz) != qcAirBlockId && chunk.block(x - fx, y, z - fz) == qcAirBlockId
                               && chunk.block(x, y + 1, z) != qcAirBlockId;
            if (!leans || m_content->property(block, u"half"_s) != u"top"_s) {
                return -1;
            }
            ++count;
        }
        return count;
    }

    // The roof of one variant of a house in all four turns (checkedRoofStairs).
    void checkRoof(const QcPieceVariation &variation, const QcPieceVariation::Selection &selection, const QString &name) const
    {
        const std::shared_ptr<const QcStructure> structure = variation.build(selection);
        for (int turns = 0; turns < 4; ++turns) {
            const QcStructurePlacement placed(structure, {2, 64, 2}, turns, *m_content);
            QcChunk chunk({0, 0});
            placed.writeInto(chunk);
            const int roofStairs = checkedRoofStairs(chunk, 67, 64 + structure->sizeY());
            QVERIFY2(roofStairs >= 2 * structure->sizeX(), qPrintable(u"%1 turn %2"_s.arg(name).arg(turns)));
        }
    }

private slots:
    void villagesFollowTheBiomeRules()
    {
        const QcTerrainGenerator generator(m_content, seed);
        const QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 6);
        QVERIFY2(plans.size() >= 8, qPrintable(u"only %1 villages in 13 x 13 regions"_s.arg(plans.size())));

        QSet<QString> styles;
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QString biome = generator.biomeAt(plan->centre.x, plan->centre.z);
            const QcBiomeDefinition *definition = m_content->worldgen().biome(biome);
            QVERIFY(definition);
            // Never in oceans, mountains or other biomes without villages.
            QVERIFY2(definition->villages, qPrintable(biome));
            const QString expected = biome == u"qtcraft:desert"_s ? u"qtcraft:desert"_s
                                     : biome == u"qtcraft:taiga"_s ? u"qtcraft:taiga"_s
                                     : biome == u"qtcraft:swamp"_s ? u"qtcraft:swamp"_s
                                     : biome == u"qtcraft:tundra"_s || biome == u"qtcraft:snowy_taiga"_s ? u"qtcraft:snowy"_s
                                                                   : u"qtcraft:plains"_s;
            QCOMPARE(plan->style, expected);
            styles.insert(plan->style);

            QVERIFY(plan->centre.y > generator.seaLevel());
            QVERIFY(plan->pieces.size() >= 4);
            QVERIFY(!plan->beds.isEmpty());
            QVERIFY(!plan->workstations.isEmpty());
            // Roads on land, boardwalks (decks) over water (decision E-407).
            QVERIFY(!plan->roads.isEmpty() || (plan->style == u"qtcraft:swamp"_s && !plan->decks.isEmpty()));
            // A village stays inside its region.
            const int spacing = generator.villages().spacing();
            QCOMPARE(qcFloorDiv(plan->minX, spacing), qcFloorDiv(plan->maxX, spacing));
            QCOMPARE(qcFloorDiv(plan->minZ, spacing), qcFloorDiv(plan->maxZ, spacing));
        }
        QVERIFY(styles.contains(u"qtcraft:plains"_s));
        QVERIFY(styles.contains(u"qtcraft:desert"_s));
    }

    void stylesComeFromDataWithAFallback()
    {
        // Every structure, choice and part of the base game's styles resolves without a warning.
        const QcVillageStyles styles(*m_content);
        QVERIFY2(styles.warnings().isEmpty(), qPrintable(styles.warnings().join(u"; "_s)));
        const auto styleOf = [&](const char *biome) {
            const QcVillageStyle *style = styles.forBiome(*m_content->worldgen().biome(QString::fromLatin1(biome)));
            return style ? style->name : QString();
        };
        QCOMPARE(styleOf("qtcraft:plains"), u"qtcraft:plains"_s);
        QCOMPARE(styleOf("qtcraft:desert"), u"qtcraft:desert"_s);
        QCOMPARE(styleOf("qtcraft:taiga"), u"qtcraft:taiga"_s);
        QCOMPARE(styleOf("qtcraft:swamp"), u"qtcraft:swamp"_s);
        QVERIFY(styles.named(u"qtcraft:swamp"_s)->overWater);
        QVERIFY(!styles.named(u"qtcraft:plains"_s)->overWater);
        // The snowy style (decision E-415) for the tundra; a biome with villages that no style names
        // takes the fallback style (the mod biome below).
        QCOMPARE(styleOf("qtcraft:tundra"), u"qtcraft:snowy"_s);
        QCOMPARE(styleOf("qtcraft:mountains"), QString());
        QCOMPARE(styleOf("qtcraft:forest"), QString());

        QcBiomeDefinition modBiome = *m_content->worldgen().biome(u"qtcraft:plains"_s);
        modBiome.name = u"mod:steppe"_s;
        QCOMPARE(styles.forBiome(modBiome)->name, u"qtcraft:plains"_s);

        // The taiga style is the plains houses with the palette swapped to spruce.
        const QcVillageStyle *taiga = styles.named(u"qtcraft:taiga"_s);
        QVERIFY(taiga);
        // Every blueprint keeps the spruce look in every choice: the style's palette comes first.
        for (const QcVillageStyle::Blueprint &blueprint : taiga->pieces.front().blueprints) {
            const std::shared_ptr<const QcStructure> built = blueprint.variation.build(blueprint.variation.everything());
            const QcStructure &house = *built;
            bool spruce = false;
            for (const QcBlockId state : house.palette()) {
                QVERIFY(m_content->block(state).name != u"qtcraft:oak_planks"_s);
                QVERIFY(m_content->block(state).name != u"qtcraft:birch_planks"_s);
                spruce = spruce || m_content->block(state).name == u"qtcraft:spruce_stairs"_s;
            }
            QVERIFY2(spruce, qPrintable(house.name()));
        }

        // Beds and workstations are found in the pieces.
        const QcVillageStyle *plains = styles.named(u"qtcraft:plains"_s);
        QVERIFY(plains->centre.first().bell.has_value());
        int beds = 0;
        QSet<QString> kinds;
        for (const QcVillageStyle::Piece &piece : plains->pieces) {
            for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
                for (const QcVillageSpot &spot : blueprint.spots) {
                    beds += spot.kind.isEmpty() ? 1 : 0;
                    if (!spot.kind.isEmpty()) {
                        kinds.insert(spot.kind);
                    }
                }
            }
        }
        QVERIFY(beds >= 5);
        QCOMPARE(kinds, (QSet<QString>{u"qtcraft:farmer"_s, u"qtcraft:smith"_s, u"qtcraft:fisher"_s}));
    }

    void extrasThatVillagesLeaveOutAreWarnedAbout()
    {
        // Villages write blocks and spawn creatures; contents and the other extras are left out.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcStructureDefinition chest;
        chest.name = u"mod:chest_lamp"_s;
        chest.sizeX = chest.sizeY = chest.sizeZ = 1;
        chest.palette = {u"qtcraft:chest"_s};
        chest.cells = {0};
        chest.contents.append({{0, 0, 0}, {{u"qtcraft:bread"_s, 3}}});
        content->worldgen().registerStructure(chest);
        QcVillageStyleDefinition plains = *std::ranges::find(content->villageCatalog().styles(), u"qtcraft:plains"_s, &QcVillageStyleDefinition::name);
        plains.lamp = chest.name;
        content->villageCatalog().registerStyle(plains);

        const QcVillageStyles styles(*content);
        const QStringList &warnings = styles.warnings();
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.front().contains(u"'mod:chest_lamp' has contents"_s), qPrintable(warnings.front()));
        QVERIFY(!styles.named(u"qtcraft:plains"_s)->lamp.blueprints.isEmpty());
    }

    void plansAreDeterministic()
    {
        const QcTerrainGenerator first(m_content, seed);
        const QcTerrainGenerator second(m_content, seed);
        const QList<std::shared_ptr<const QcVillagePlan>> a = plansAround(first, 3);
        // The second generator plans the regions in the opposite order.
        QList<std::shared_ptr<const QcVillagePlan>> b;
        for (int rz = 3; rz >= -3; --rz) {
            for (int rx = 3; rx >= -3; --rx) {
                if (std::shared_ptr<const QcVillagePlan> plan = second.villages().regionPlan(rx, rz)) {
                    b.prepend(std::move(plan));
                }
            }
        }
        QCOMPARE(a.size(), b.size());
        for (qsizetype i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i]->id, b[i]->id);
            QCOMPARE(a[i]->centre, b[i]->centre);
            QCOMPARE(a[i]->roads, b[i]->roads);
            QCOMPARE(a[i]->branchRoads, b[i]->branchRoads);
            QCOMPARE(a[i]->pieces.size(), b[i]->pieces.size());
        }

        const QcTerrainGenerator other(m_content, seed + 1);
        const QList<std::shared_ptr<const QcVillagePlan>> otherPlans = plansAround(other, 3);
        QVERIFY(otherPlans.size() != a.size() || otherPlans.front()->centre != a.front()->centre);
    }

    void chunksComeOutTheSameInAnyOrder()
    {
        const QcTerrainGenerator probe(m_content, seed);
        const QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(probe, 4);
        QVERIFY(!plans.isEmpty());
        const QcVillagePlan &plan = *plans.front();
        const QList<QcChunkPos> chunks = chunksOf(plan);
        QVERIFY(chunks.size() >= 4);

        // In order on this thread, and backwards on four threads with a fresh generator.
        const QcTerrainGenerator ordered(m_content, seed);
        QHash<QcChunkPos, std::shared_ptr<QcChunk>> first;
        for (const QcChunkPos &pos : chunks) {
            first.insert(pos, ordered.generate(pos));
        }
        const QcTerrainGenerator fresh(m_content, seed);
        const Chunks second = generateOnThreads(fresh, chunks);
        for (const QcChunkPos &pos : chunks) {
            QVERIFY2(sameBlocks(*first.value(pos), *second.value(pos)), qPrintable(u"chunk %1 %2 differs"_s.arg(pos.x).arg(pos.z)));
        }

        // The pieces are there: every placed cell of every piece holds its state.
        for (const QcStructurePlacement &piece : plan.pieces) {
            QVERIFY2(pieceIsBuilt(first, piece), qPrintable(piece.structure().name()));
        }
    }

    void piecesStandOnTheGround()
    {
        const QcTerrainGenerator generator(m_content, seed);
        QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 4);
        // Villages over water stand on stilts (test_village_swamp).
        plans.removeIf([](const std::shared_ptr<const QcVillagePlan> &plan) { return plan->stiltPost != qcAirBlockId; });
        QVERIFY(plans.size() >= 2);
        for (qsizetype i = 0; i < 2; ++i) {
            const QcVillagePlan &plan = *plans[i];
            QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
            const QList<QcChunkPos> positions = chunksOf(plan);
            for (const QcChunkPos &pos : positions) {
                chunks.insert(pos, generator.generate(pos));
            }

            // The floor rests on solid ground: no gap below any column of a piece.
            for (const QcStructurePlacement &piece : plan.pieces) {
                QVERIFY2(standsOnGround(*m_content, chunks, piece), qPrintable(piece.structure().name()));
            }

            // Roads lie on the ground, never in water.
            const QList<QcBlockPos> roads = plan.roads + plan.branchRoads;
            for (const QcBlockPos &cell : roads) {
                const std::optional<QcBlockId> block = blockIn(chunks, cell);
                QVERIFY(block);
                QVERIFY(!m_content->block(*block).liquid);
            }
        }
    }

    void roofStairsTurnWithTheirHouse()
    {
        // A pitched roof: every stair's high side (its facing) points up the slope, so the cell
        // beyond its low side is open sky, in every turn of every house blueprint, with all its
        // optional parts (the chimneys stand on the ridge) and with every roof material.
        const QcVillageStyles styles(*m_content);
        const QcVillageStyle *plains = styles.named(u"qtcraft:plains"_s);
        QVERIFY(plains);
        int houses = 0;
        for (const QcVillageStyle::Piece &piece : plains->pieces) {
            for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
                const QString name = blueprint.structure()->name();
                if (!name.contains(u"house"_s) && !name.contains(u"church"_s)) {
                    continue;
                }
                ++houses;
                QcPieceVariation::Selection selection = blueprint.variation.everything();
                const QList<int> counts = blueprint.variation.optionCounts();
                for (int option = 0; option < 4; ++option) {
                    for (qsizetype c = 0; c < counts.size(); ++c) {
                        selection.options[c] = std::min(option, counts[c] - 1);
                    }
                    checkRoof(blueprint.variation, selection, name);
                }
            }
        }
        QCOMPARE(houses, 6);
    }

    void towerCorbelsTurnWithTheirTower()
    {
        // The watchtower's platform rests on upside-down stairs around the shaft: each one's low
        // side (its facing) leans against the wall, open air outside it and the platform above, in
        // every turn and stone choice of both towers.
        const QcVillageStyles styles(*m_content);
        for (const char *name : {"qtcraft:plains", "qtcraft:desert"}) {
            const QcVillageStyle *style = styles.named(QString::fromLatin1(name));
            QVERIFY(style);
            const auto tower = std::ranges::find_if(style->pieces, [](const QcVillageStyle::Piece &piece) {
                return piece.first().structure()->name().endsWith(u"_tower"_s);
            });
            QVERIFY(tower != style->pieces.cend());
            const QcPieceVariation &variation = tower->first().variation;
            QcPieceVariation::Selection selection = variation.everything();
            for (int option = 0; option < 2; ++option) {
                selection.options[0] = option;
                const std::shared_ptr<const QcStructure> structure = variation.build(selection);
                // Taller than every house (the tallest has 13 layers, its top ones air).
                QVERIFY(structure->sizeY() >= 16);
                for (int turns = 0; turns < 4; ++turns) {
                    QCOMPARE(corbelsOf(*structure, turns), 20);
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestVillageGeneration)
#include "test_village_generation.moc"
