#include "common/core/QcIntMath.h"
#include "QcTestUtil.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QTest>

#include <algorithm>
#include <ctime>
#include <set>

using namespace Qt::StringLiterals;

// Ruined portals, wells and oases (decisions E-630..E-633): surface structures from data like the
// desert temple, planned from the seed and the terrain noise alone, only in their biomes. A ruin keeps
// the terrain in its keep cells (half buried), each copy draws its own parts (missing or crying
// blocks, more rubble), its chest gets loot. Wells hold a shaft of water sources, oases a pool.
class TestSurfaceRuins : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 51;
    static constexpr int regions = 7;
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;
    using Plans = QList<std::shared_ptr<const QcStructurePlacement>>;

    const QStringList m_names{u"qtcraft:ruined_portal"_s, u"qtcraft:ruined_portal_sunken"_s, u"qtcraft:ruined_portal_large"_s,
                              u"qtcraft:well"_s,          u"qtcraft:desert_well"_s,          u"qtcraft:oasis"_s};
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcTerrainGenerator> m_terrain;
    QHash<QString, Plans> m_plans;

    int kindOf(const QcTerrainGenerator &terrain, const QString &name) const
    {
        return terrain.surfaceStructures().planner().kindIndex(name).value_or(-1);
    }

    int spacingOf(const QString &name) const { return m_content->worldgen().structure(name)->surface->spacing; }

    Plans plans(const QcTerrainGenerator &terrain, const QString &name, bool reversed = false) const
    {
        Plans found;
        const int kind = kindOf(terrain, name);
        for (int i = 0; i < (2 * regions + 1) * (2 * regions + 1); ++i) {
            const int index = reversed ? (2 * regions + 1) * (2 * regions + 1) - 1 - i : i;
            const int rx = index % (2 * regions + 1) - regions;
            const int rz = index / (2 * regions + 1) - regions;
            if (std::shared_ptr<const QcStructurePlacement> plan = terrain.surfaceStructures().planOf(kind, rx, rz)) {
                found.append(std::move(plan));
            }
        }
        if (reversed) {
            std::ranges::reverse(found);
        }
        return found;
    }

    static QList<QcChunkPos> chunksOf(const QcStructurePlacement &plan)
    {
        QList<QcChunkPos> chunks;
        const QcChunkPos first = plan.origin().chunk();
        const QcChunkPos last = plan.origin().offset(plan.sizeX() - 1, 0, plan.sizeZ() - 1).chunk();
        for (int cx = first.x; cx <= last.x; ++cx) {
            for (int cz = first.z; cz <= last.z; ++cz) {
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
        return chunk ? chunk->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    }

    QString nameAt(const Chunks &chunks, QcBlockPos pos) const { return m_content->block(at(chunks, pos)).name; }

    // Whether a keep cell holds the terrain: what the terrain without the structures holds there, or,
    // where that is a tree, plant or surface pool (none grows near a surface structure), air or
    // natural ground.
    static bool keepsTerrain(const QcTerrainGenerator &terrain, QcBlockId block, QcBlockId plain)
    {
        const bool feature = plain != qcAirBlockId && !terrain.isNaturalGround(plain);
        return block == plain || (feature && (block == qcAirBlockId || terrain.isNaturalGround(block)));
    }

    // Every cell the placement writes holds its block in the chunks and every keep cell the terrain
    // (keepsTerrain); returns how many keep cells of the ground layers there are, -1 on a mismatch.
    static int verifyWritten(const QcTerrainGenerator &terrain, const QcStructurePlacement &plan, const Chunks &chunks, const Chunks &without)
    {
        int kept = 0;
        const QcStructure &structure = plan.structure();
        for (int y = 0; y < structure.sizeY(); ++y) {
            for (int z = 0; z < structure.sizeZ(); ++z) {
                for (int x = 0; x < structure.sizeX(); ++x) {
                    const QcBlockPos world = plan.toWorld(QcBlockPos{x, y, z});
                    const std::optional<QcBlockId> block = plan.worldBlock({x, y, z});
                    if (block ? at(chunks, world) != *block : !keepsTerrain(terrain, at(chunks, world), at(without, world))) {
                        qWarning().noquote() << structure.name() << world.x << world.y << world.z;
                        return -1;
                    }
                    kept += !block && y <= structure.groundLayer;
                }
            }
        }
        return kept;
    }

    // The cells the copy writes, by world position.
    static QHash<QcBlockPos, QcBlockId> writtenCells(const QcStructurePlacement &plan)
    {
        QHash<QcBlockPos, QcBlockId> cells;
        for (int y = 0; y < plan.structure().sizeY(); ++y) {
            for (int z = 0; z < plan.structure().sizeZ(); ++z) {
                for (int x = 0; x < plan.structure().sizeX(); ++x) {
                    if (const std::optional<QcBlockId> block = plan.worldBlock({x, y, z})) {
                        cells.insert(plan.toWorld(QcBlockPos{x, y, z}), *block);
                    }
                }
            }
        }
        return cells;
    }

    // The liquids beside and above `air`.
    QStringList liquidsBeside(const Chunks &chunks, QcBlockPos air) const
    {
        QStringList liquids;
        for (const QcBlockPos &next : {air.offset(1, 0, 0), air.offset(-1, 0, 0), air.offset(0, 0, 1), air.offset(0, 0, -1), air.offset(0, 1, 0)}) {
            if (m_content->block(at(chunks, next)).liquid) {
                liquids.append(u"liquid beside air at %1 %2 %3"_s.arg(next.x).arg(next.y).arg(next.z));
            }
        }
        return liquids;
    }

    // The cells of the structure that hold a liquid.
    QList<QcBlockPos> liquidCells(const QcStructure &structure) const
    {
        QList<QcBlockPos> cells;
        for (int y = 0; y < structure.sizeY(); ++y) {
            for (int z = 0; z < structure.sizeZ(); ++z) {
                for (int x = 0; x < structure.sizeX(); ++x) {
                    const std::optional<QcBlockId> block = structure.block(x, y, z);
                    if (block && m_content->block(*block).liquid) {
                        cells.append({x, y, z});
                    }
                }
            }
        }
        return cells;
    }

    // Whether the structure writes a block at `cell` that is neither air nor falls.
    bool holds(const QcStructure &structure, QcBlockPos cell) const
    {
        const std::optional<QcBlockId> block = structure.contains(cell.x, cell.y, cell.z) ? structure.block(cell.x, cell.y, cell.z) : std::nullopt;
        return block && *block != qcAirBlockId && !m_content->block(*block).falls;
    }

    // Problems in and above the copy's footprint: a block that is neither air, natural ground nor
    // written by the copy (a tree, cactus, plant or liquid of the terrain), and a liquid beside or
    // above a cell the copy writes as air (it would run out).
    QStringList looseBlocks(const QcTerrainGenerator &terrain, const QcStructurePlacement &plan, const Chunks &chunks) const
    {
        QStringList problems;
        const QcStructure &structure = plan.structure();
        const QcBlockPos origin = plan.origin();
        const QHash<QcBlockPos, QcBlockId> cells = writtenCells(plan);
        for (int y = origin.y; y < origin.y + structure.sizeY() + 8; ++y) {
            for (int z = origin.z; z < origin.z + plan.sizeZ(); ++z) {
                for (int x = origin.x; x < origin.x + plan.sizeX(); ++x) {
                    const QcBlockPos world{x, y, z};
                    const std::optional<QcBlockId> written = cells.contains(world) ? std::optional(cells.value(world)) : std::nullopt;
                    const QcBlockId block = at(chunks, world);
                    const bool kept = !written || y >= origin.y + structure.sizeY();
                    if (kept && y > origin.y + structure.groundLayer && block != qcAirBlockId && !terrain.isNaturalGround(block)) {
                        problems.append(u"%1 at %2 %3 %4"_s.arg(m_content->block(block).name).arg(x).arg(y).arg(z));
                    }
                    if (written != qcAirBlockId || y >= origin.y + structure.sizeY()) {
                        continue;
                    }
                    problems.append(liquidsBeside(chunks, world));
                }
            }
        }
        return problems;
    }

    // The content without the surface rules of this package's structures (the terrain as it was).
    std::shared_ptr<QcContentRegistry> contentWithout() const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        for (const QString &name : m_names) {
            QcStructureDefinition definition = *content->worldgen().structure(name);
            definition.surface.reset();
            content->worldgen().registerStructure(definition);
        }
        return content;
    }

    // Sources of water (level 0) in the column, from the top down.
    int sourcesIn(const Chunks &chunks, int x, int z, int top) const
    {
        int sources = 0;
        for (int y = top; y > top - 8; --y) {
            const QcBlockId id = at(chunks, {x, y, z});
            const QcBlockDefinition &block = m_content->block(id);
            if (block.name == u"qtcraft:water"_s && m_content->property(id, u"level"_s) == u"0"_s) {
                ++sources;
            }
        }
        return sources;
    }

private slots:
    void initTestCase()
    {
        m_terrain = std::make_unique<QcTerrainGenerator>(m_content, seed);
        for (const QString &name : std::as_const(m_names)) {
            QVERIFY2(kindOf(*m_terrain, name) >= 0, qPrintable(name));
            m_plans.insert(name, plans(*m_terrain, name));
            qInfo().noquote() << u"%1: %2 in %3 regions"_s.arg(name).arg(m_plans[name].size()).arg((2 * regions + 1) * (2 * regions + 1));
            QVERIFY2(m_plans[name].size() >= 2, qPrintable(name));
        }
    }

    void cryingObsidianGlowsAndDropsItself()
    {
        const std::optional<QcBlockId> id = m_content->blockId(u"qtcraft:crying_obsidian"_s);
        QVERIFY(id);
        const QcBlockDefinition &block = m_content->block(*id);
        QCOMPARE(block.lightLevel, 10);
        QCOMPARE(block.toolLevel, m_content->block(*m_content->blockId(u"qtcraft:obsidian"_s)).toolLevel);
        QCOMPARE(block.drops.size(), 1);
        QCOMPARE(block.drops.front().item, u"qtcraft:crying_obsidian"_s);
    }

    void plansAreDeterministicAndInTheirBiomes()
    {
        const QcTerrainGenerator fresh(m_content, seed);
        const QcTerrainGenerator other(m_content, seed + 1);
        for (const QString &name : std::as_const(m_names)) {
            // A fresh generator plans the same, in the other order of the regions.
            const Plans again = plans(fresh, name, true);
            const Plans &first = m_plans[name];
            QCOMPARE(again.size(), first.size());
            const QStringList biomes = m_content->worldgen().structure(name)->surface->biomes;
            const int spacing = spacingOf(name);
            for (qsizetype i = 0; i < first.size(); ++i) {
                const QcStructurePlacement &plan = *first[i];
                QCOMPARE(again[i]->origin(), plan.origin());
                QCOMPARE(again[i]->turns(), plan.turns());
                QCOMPARE(qcFloorDiv(plan.origin().x, spacing), qcFloorDiv(plan.origin().x + plan.sizeX() - 1, spacing));
                QCOMPARE(qcFloorDiv(plan.origin().z, spacing), qcFloorDiv(plan.origin().z + plan.sizeZ() - 1, spacing));
                for (const auto &[dx, dz] : {std::pair{0, 0}, {plan.sizeX() - 1, plan.sizeZ() - 1}, {plan.sizeX() / 2, plan.sizeZ() / 2}}) {
                    const QString biome = m_terrain->biomeAt(plan.origin().x + dx, plan.origin().z + dz);
                    QVERIFY2(biomes.contains(biome), qPrintable(name + u" in "_s + biome));
                }
                // The same structure, varied parts included.
                for (int y = 0; y < plan.structure().sizeY(); ++y) {
                    for (int z = 0; z < plan.structure().sizeZ(); ++z) {
                        for (int x = 0; x < plan.structure().sizeX(); ++x) {
                            QCOMPARE(again[i]->worldBlock({x, y, z}), plan.worldBlock({x, y, z}));
                        }
                    }
                }
            }
            // Another seed plans elsewhere.
            const Plans otherPlans = plans(other, name);
            QVERIFY(otherPlans.isEmpty() || otherPlans.front()->origin() != first.front()->origin());
        }
        // Oases only in the desert, wells in their biomes.
        QCOMPARE(m_content->worldgen().structure(u"qtcraft:oasis"_s)->surface->biomes, QStringList{u"qtcraft:desert"_s});
    }

    void ruinsVaryFromCopyToCopy()
    {
        // Parts and choices are drawn per copy: the copies of one kind are not all built alike, and
        // every frame keeps some obsidian and some crying obsidian.
        const QcBlockId obsidian = *m_content->blockId(u"qtcraft:obsidian"_s);
        const QcBlockId crying = *m_content->blockId(u"qtcraft:crying_obsidian"_s);
        std::set<QList<qint64>> builds;
        Plans all = m_plans[u"qtcraft:ruined_portal"_s];
        all.append(plans(QcTerrainGenerator(m_content, seed + 7), u"qtcraft:ruined_portal"_s));
        for (const std::shared_ptr<const QcStructurePlacement> &plan : std::as_const(all)) {
            const QcStructure &structure = plan->structure();
            QList<qint64> cells;
            int obsidianCells = 0;
            int cryingCells = 0;
            for (int y = 0; y < structure.sizeY(); ++y) {
                for (int z = 0; z < structure.sizeZ(); ++z) {
                    for (int x = 0; x < structure.sizeX(); ++x) {
                        const std::optional<QcBlockId> block = structure.block(x, y, z);
                        cells.append(block ? qint64(*block) : -1);
                        obsidianCells += block == obsidian;
                        cryingCells += block == crying;
                    }
                }
            }
            QVERIFY(obsidianCells >= 6);
            QVERIFY(cryingCells >= 1);
            builds.insert(cells);
        }
        qInfo().noquote() << u"%1 different builds of %2 small ruins"_s.arg(builds.size()).arg(all.size());
        QVERIFY(builds.size() >= 2);
    }

    void chunksAreTheSameInAnyOrderAndKeepTheTerrain()
    {
        const auto without = contentWithout();
        for (const QString &name : std::as_const(m_names)) {
            const QcStructurePlacement &plan = *m_plans[name].front();
            QList<QcChunkPos> positions = chunksOf(plan);
            const QcTerrainGenerator forward(m_content, seed);
            const Chunks first = generate(forward, positions);
            std::ranges::reverse(positions);
            const QcTerrainGenerator backward(m_content, seed);
            const Chunks second = generate(backward, positions);
            for (const QcChunkPos &pos : std::as_const(positions)) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    for (int z = 0; z < QcWorld::chunkSize; ++z) {
                        for (int x = 0; x < QcWorld::chunkSize; ++x) {
                            QCOMPARE(second[pos]->block(x, y, z), first[pos]->block(x, y, z));
                        }
                    }
                }
            }
            // Every written cell holds its block, every keep cell the terrain.
            const Chunks plain = generate(QcTerrainGenerator(without, seed), positions);
            const int kept = verifyWritten(forward, plan, first, plain);
            QVERIFY2(kept > 0, qPrintable(name));
        }
    }

    void noTreesPlantsOrLooseLiquidsInTheFootprints()
    {
        // Trees, cacti, plants and surface pools keep off the copies (decision E-630): none is cut by
        // a cell of air or left floating in a keep cell, and no liquid runs into the air a copy writes.
        constexpr qsizetype copies = 8;
        const auto without = contentWithout();
        const QcTerrainGenerator plainTerrain(without, seed);
        int checked = 0;
        for (const QString &name : std::as_const(m_names)) {
            const Plans &all = m_plans[name];
            for (qsizetype i = 0; i < std::min(copies, all.size()); ++i) {
                const QList<QcChunkPos> positions = chunksOf(*all[i]);
                const Chunks chunks = generate(*m_terrain, positions);
                const QStringList problems = looseBlocks(*m_terrain, *all[i], chunks);
                QVERIFY2(problems.isEmpty(), qPrintable(name + u": "_s + problems.mid(0, 8).join(u", "_s)));
                QVERIFY2(verifyWritten(*m_terrain, *all[i], chunks, generate(plainTerrain, positions)) >= 0, qPrintable(name));
                ++checked;
            }
        }
        qInfo().noquote() << u"%1 copies without loose blocks"_s.arg(checked);
    }

    void ruinsHaveLootChests()
    {
        const QcLootTableDefinition *table = m_content->lootTables().find(u"qtcraft:ruined_portal_chest"_s);
        QVERIFY(table);
        for (const QString &name : {u"qtcraft:ruined_portal"_s, u"qtcraft:ruined_portal_sunken"_s, u"qtcraft:ruined_portal_large"_s}) {
            const QcStructurePlacement &plan = *m_plans[name].front();
            int chests = 0;
            const QList<QcChunkPos> planChunks = chunksOf(plan);
            for (const QcChunkPos &pos : planChunks) {
                const QcChunkFurnishing furnishing = m_terrain->furnishingIn(pos);
                chests += int(std::ranges::count(furnishing.chests, u"qtcraft:ruined_portal_chest"_s, &QcChunkFurnishing::Chest::lootTable));
            }
            QCOMPARE(chests, 1);
            const Chunks chunks = generate(*m_terrain, planChunks);
            const QcBlockPos chest = plan.toWorld(plan.structure().loot.front().at);
            QCOMPARE(nameAt(chunks, chest), u"qtcraft:chest"_s);
            QCOMPARE(at(chunks, chest.offset(0, 1, 0)), qcAirBlockId);
        }

        // Gold turns up: over many chests every golden item and every entry of the table.
        const auto items = [this](const QString &item) -> std::optional<QcLootTables::ItemInfo> {
            const std::optional<QcItemId> id = m_content->itemId(item);
            return id ? std::optional(QcLootTables::ItemInfo{*id, m_content->item(*id).maxStack}) : std::nullopt;
        };
        std::set<QcItemId> drawn;
        for (int i = 0; i < 400; ++i) {
            const QList<QcItemStack> stacks = QcLootTables::draw(*table, quint64(i) * 7919, items);
            for (const QcItemStack &stack : stacks) {
                drawn.insert(stack.item);
            }
        }
        for (const QcLootTableDefinition::Entry &entry : table->pools.front().entries) {
            QVERIFY2(m_content->itemId(entry.item), qPrintable(entry.item));
            QVERIFY2(drawn.contains(*m_content->itemId(entry.item)), qPrintable(entry.item));
        }
    }

    void wellsAndOasesHoldWaterSources()
    {
        for (const QString &name : {u"qtcraft:well"_s, u"qtcraft:desert_well"_s}) {
            const QcStructurePlacement &plan = *m_plans[name].front();
            const Chunks chunks = generate(*m_terrain, chunksOf(plan));
            // The shaft in the middle: five sources from the rim down into the ground.
            const QcBlockPos rim = plan.toWorld(QcBlockPos{2, 5, 2});
            QCOMPARE(sourcesIn(chunks, rim.x, rim.z, rim.y), 5);
            QCOMPARE(at(chunks, rim.offset(0, 1, 0)), qcAirBlockId);
        }

        const QcStructurePlacement &oasis = *m_plans[u"qtcraft:oasis"_s].front();
        const Chunks chunks = generate(*m_terrain, chunksOf(oasis));
        int sources = 0;
        int cane = 0;
        int grass = 0;
        const int floor = oasis.origin().y + oasis.structure().groundLayer;
        for (int dz = 0; dz < oasis.sizeZ(); ++dz) {
            for (int dx = 0; dx < oasis.sizeX(); ++dx) {
                const int x = oasis.origin().x + dx;
                const int z = oasis.origin().z + dz;
                sources += sourcesIn(chunks, x, z, floor);
                cane += nameAt(chunks, {x, floor + 1, z}) == u"qtcraft:sugar_cane"_s;
                grass += nameAt(chunks, {x, floor, z}) == u"qtcraft:grass_block"_s;
                // No water above the ground: the pool lies still.
                QVERIFY(nameAt(chunks, {x, floor + 1, z}) != u"qtcraft:water"_s);
            }
        }
        qInfo().noquote() << u"oasis: %1 water sources, %2 sugar cane, %3 grass"_s.arg(sources).arg(cane).arg(grass);
        QVERIFY(sources >= 40);
        QVERIFY(cane >= 4);
        QVERIFY(grass >= 40);
    }

    void waterIsHeldByWrittenCells()
    {
        // Below and beside every water cell of a well or the oasis lies a written cell that neither
        // falls nor is air, so the water stays even over a cave under the keep cells.
        for (const QString &name : {u"qtcraft:well"_s, u"qtcraft:desert_well"_s, u"qtcraft:oasis"_s}) {
            const QcStructure &structure = m_plans[name].front()->structure();
            const QList<QcBlockPos> water = liquidCells(structure);
            QVERIFY2(!water.isEmpty(), qPrintable(name));
            for (const QcBlockPos &cell : water) {
                for (const QcBlockPos &next : {cell.offset(1, 0, 0), cell.offset(-1, 0, 0), cell.offset(0, 0, 1), cell.offset(0, 0, -1), cell.offset(0, -1, 0)}) {
                    QVERIFY2(holds(structure, next), qPrintable(u"%1: water at %2 %3 %4 not held"_s.arg(name).arg(cell.x).arg(cell.y).arg(cell.z)));
                }
            }
        }
    }

    void theShowcaseStationShowsAllThree()
    {
        // The station: the ruin with crying obsidian and its chest, the well and the oasis with water.
        const QcShowcaseGenerator showcase(m_content);
        const auto station = std::ranges::find_if(showcase.plan().placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name() == u"qtcraft:ruins_station"_s;
        });
        QVERIFY(station != showcase.plan().placements.end());
        const Chunks chunks = generate(showcase, chunksOf(*station));
        QCOMPARE(nameAt(chunks, station->toWorld(QcBlockPos{5, 1, 7})), u"qtcraft:crying_obsidian"_s);
        QCOMPARE(nameAt(chunks, station->toWorld(QcBlockPos{13, 0, 7})), u"qtcraft:water"_s);
        QCOMPARE(nameAt(chunks, station->toWorld(QcBlockPos{26, 0, 7})), u"qtcraft:water"_s);
        const QcBlockPos chest = station->toWorld(station->structure().loot.front().at);
        QCOMPARE(nameAt(chunks, chest), u"qtcraft:chest"_s);
        const QcChunkFurnishing furnishing = showcase.furnishingIn(chest.chunk());
        QVERIFY(std::ranges::any_of(furnishing.chests, [&](const QcChunkFurnishing::Chest &placed) {
            return placed.at == chest && placed.lootTable == u"qtcraft:ruined_portal_chest"_s;
        }));
    }

    void measureGenerationCost()
    {
        // A measurement, not a check (decision E-634): only with QTCRAFT_SURFACE_RUINS_MEASURE set.
        if (!qEnvironmentVariableIsSet("QTCRAFT_SURFACE_RUINS_MEASURE")) {
            QSKIP("set QTCRAFT_SURFACE_RUINS_MEASURE=1 to measure");
        }
        const auto without = contentWithout();
        const auto seconds = [](const QcTerrainGenerator &terrain, const QList<QcChunkPos> &chunks) {
            timespec start{};
            timespec end{};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
            for (const QcChunkPos &pos : chunks) {
                terrain.generate(pos);
            }
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
            return double(end.tv_sec - start.tv_sec) + double(end.tv_nsec - start.tv_nsec) / 1e9;
        };

        // The chunks of one copy of each kind, fresh generators interleaved (plans made afresh).
        QList<QcChunkPos> chunks;
        for (const QString &name : std::as_const(m_names)) {
            chunks.append(chunksOf(*m_plans[name].front()));
        }
        double withTime = 0.0;
        double plainTime = 0.0;
        for (int round = 0; round < 2; ++round) {
            const QcTerrainGenerator plain(without, seed);
            const QcTerrainGenerator with(m_content, seed);
            plainTime += seconds(plain, chunks);
            withTime += seconds(with, chunks);
        }
        const double count = 2.0 * double(chunks.size());
        qInfo().noquote() << u"chunks/s of %1 chunks of ruins, wells and oases without them %2, with %3"_s.arg(chunks.size())
                                 .arg(count / plainTime, 0, 'f', 1)
                                 .arg(count / withTime, 0, 'f', 1);

        // An area around a ruin with every kind planned afresh.
        QList<QcChunkPos> area;
        const QcChunkPos corner = m_plans[u"qtcraft:ruined_portal"_s].front()->origin().chunk();
        for (int cz = -6; cz < 6; ++cz) {
            for (int cx = -6; cx < 6; ++cx) {
                area.append({corner.x + cx, corner.z + cz});
            }
        }
        const QcTerrainGenerator plain(without, seed);
        const QcTerrainGenerator with(m_content, seed);
        const double a = seconds(plain, area);
        const double b = seconds(with, area);
        qInfo().noquote() << u"chunks/s of %1 chunks around a ruin without them %2, with %3"_s.arg(area.size())
                                 .arg(double(area.size()) / a, 0, 'f', 1)
                                 .arg(double(area.size()) / b, 0, 'f', 1);
    }
};

QTEST_GUILESS_MAIN(TestSurfaceRuins)
#include "test_surface_ruins.moc"
