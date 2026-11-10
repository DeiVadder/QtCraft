// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcGeneratorVersionMap.h"
#include "fixtures/QcTestUtil.h"
#include "villages/QcVillageFixture.h"
#include "villages/QcVillageTestUtil.h"
#include "worldgen/generators/QcVersionedGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"

#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace QcVillageTestUtil;

namespace {

// The last generator version whose village buildings have no loot of their own (79.92).
constexpr int beforeLoot = 2;

} // namespace

// The chests of villages have loot since generator version 3 (decisions E-1015, E-1017): every chest
// of every blueprint of every style, with its optional parts, names a loot table; a placed piece keeps
// the chest and its table together however it is turned; the chests of a generated village come with
// furnishingIn(), and worlds of older versions keep them empty.
class TestVillageChests : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QcBlockId m_chest = qcAirBlockId;

    // The chests of `structure` in local cells.
    QList<QcBlockPos> chestCells(const QcStructure &structure) const
    {
        QList<QcBlockPos> cells;
        for (int y = 0; y < structure.sizeY(); ++y) {
            for (int z = 0; z < structure.sizeZ(); ++z) {
                for (int x = 0; x < structure.sizeX(); ++x) {
                    const std::optional<QcBlockId> state = structure.block(x, y, z);
                    if (state && isChest(*state)) {
                        cells.append(QcBlockPos{x, y, z});
                    }
                }
            }
        }
        return cells;
    }

    bool isChest(QcBlockId state) const { return m_content->block(state).name == m_content->block(m_chest).name; }

    // The loot table `structure` names for its chest at `cell`, empty for none.
    static QString tableAt(const QcStructure &structure, QcBlockPos cell)
    {
        const auto loot = std::ranges::find(structure.loot, cell, &QcStructureDefinition::Loot::at);
        return loot == structure.loot.end() ? QString() : loot->table;
    }

    // The blueprints of `style` of all kinds, each built with all its parts.
    static QList<std::shared_ptr<const QcStructure>> buildingsOf(const QcVillageStyle &style)
    {
        QList<QcVillageStyle::Piece> kinds = style.pieces;
        kinds.append({style.centre, style.rareCentre, style.lamp});
        QList<std::shared_ptr<const QcStructure>> buildings;
        for (const QcVillageStyle::Piece &kind : std::as_const(kinds)) {
            for (const QcVillageStyle::Blueprint &blueprint : kind.blueprints) {
                buildings.append(blueprint.variation.build(blueprint.variation.everything(QcGeneratorVersion::current)));
            }
        }
        return buildings;
    }

    // The tables of the chests of `style`.
    static QSet<QString> tablesOf(const QcVillageStyle &style)
    {
        QSet<QString> tables;
        const QList<std::shared_ptr<const QcStructure>> buildings = buildingsOf(style);
        for (const std::shared_ptr<const QcStructure> &building : buildings) {
            for (const QcStructureDefinition::Loot &loot : building->loot) {
                tables.insert(loot.table);
            }
        }
        return tables;
    }

    std::unique_ptr<QcWorldGenerator> terrain(qint64 seed, int version) const
    {
        return QcWorldGenerators::create(QString::fromLatin1(QcWorldGenerators::terrain), m_content, seed, {}, version);
    }

    // What the loot table `name` gives for `seed`; an unknown item fails the draw's check below.
    QList<QcItemStack> draw(const QString &name, quint64 seed) const
    {
        const auto items = [this](const QString &item) -> std::optional<QcLootTables::ItemInfo> {
            const std::optional<QcItemId> id = m_content->itemId(item);
            return id ? std::optional(QcLootTables::ItemInfo{*id, m_content->item(*id).maxStack}) : std::nullopt;
        };
        return QcLootTables::draw(*m_content->lootTables().find(name), seed, items);
    }

    // The chests of the built pieces of `plans` in world cells with their tables.
    QList<QcChunkFurnishing::Chest> chestsOf(const QcWorldGenerator &generator, const QList<std::shared_ptr<const QcVillagePlan>> &plans) const
    {
        QList<QcChunkFurnishing::Chest> chests;
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            if (generator.villageFor(QcBlockPos{plan->centre.x, 0, plan->centre.z}.chunk()) != plan) {
                continue;
            }
            for (const QcStructurePlacement &piece : plan->pieces) {
                const QList<QcBlockPos> cells = chestCells(piece.structure());
                for (const QcBlockPos &cell : cells) {
                    chests.append({piece.toWorld(cell), tableAt(piece.structure(), cell), 0});
                }
            }
        }
        return chests;
    }

    static bool holds(const QcChunkFurnishing &furnishing, const QcChunkFurnishing::Chest &chest)
    {
        return std::ranges::any_of(furnishing.chests, [&chest](const QcChunkFurnishing::Chest &placed) {
            return placed.at == chest.at && placed.lootTable == chest.lootTable;
        });
    }

    // `built`, the structure of a frozen version, against the structure as written.
    static void compareWithBase(const QcStructure &built, const QcStructure &base)
    {
        const QByteArray name = base.name().toUtf8();
        QVERIFY2(built.loot.size() == base.loot.size(), name.constData());
        const QList<qsizetype> extras{built.mobs.size() - base.mobs.size(),           built.stands.size() - base.stands.size(),
                                      built.contents.size() - base.contents.size(),   built.labels.size() - base.labels.size(),
                                      built.updates.size() - base.updates.size(),     built.growing.size() - base.growing.size(),
                                      built.minecarts.size() - base.minecarts.size(), built.boats.size() - base.boats.size(),
                                      built.spawners.size() - base.spawners.size(),   built.signs.size() - base.signs.size(),
                                      built.hangings.size() - base.hangings.size(),   built.guardians.size() - base.guardians.size()};
        QVERIFY2(std::ranges::all_of(extras, [](qsizetype difference) { return difference == 0; }), name.constData());
        QVERIFY2(built.groundLayer == base.groundLayer, name.constData());
        QVERIFY2(built.spawnArea.has_value() == base.spawnArea.has_value(), name.constData());
        for (int y = 0; y < base.sizeY(); ++y) {
            for (int z = 0; z < base.sizeZ(); ++z) {
                for (int x = 0; x < base.sizeX(); ++x) {
                    QVERIFY2(built.block(x, y, z) == base.block(x, y, z), name.constData());
                }
            }
        }
    }

    static bool filledAt(const QcChunkFurnishing &furnishing, QcBlockPos at)
    {
        return std::ranges::find(furnishing.chests, at, &QcChunkFurnishing::Chest::at) != furnishing.chests.end();
    }

private slots:
    void initTestCase()
    {
        m_content = QcTestUtil::loadContent();
        QVERIFY(m_content);
        m_chest = m_content->stateId(u"qtcraft:chest[facing=south]"_s).value_or(qcAirBlockId);
        QVERIFY(m_chest != qcAirBlockId);
    }

    void everyChestOfEveryStyleHasLoot()
    {
        const QcVillageStyles styles(*m_content);
        QSet<QString> withChests;
        for (const QcVillageStyleDefinition &definition : m_content->villageCatalog().styles()) {
            const QcVillageStyle *style = styles.named(definition.name);
            QVERIFY(style);
            const QList<std::shared_ptr<const QcStructure>> buildings = buildingsOf(*style);
            for (const std::shared_ptr<const QcStructure> &building : buildings) {
                const QList<QcBlockPos> cells = chestCells(*building);
                for (const QcBlockPos &cell : cells) {
                    const QString table = tableAt(*building, cell);
                    const QString where = u"%1 in %2 at %3 %4 %5"_s.arg(building->name(), definition.name).arg(cell.x).arg(cell.y).arg(cell.z);
                    QVERIFY2(!table.isEmpty(), qPrintable(where));
                    QVERIFY2(m_content->lootTables().find(table), qPrintable(where + u": "_s + table));
                    withChests.insert(building->name());
                }
                // With all parts built, every entry names a chest: no entry points beside it.
                QCOMPARE(building->loot.size(), cells.size());
            }
        }
        // The 20 village buildings and the desert temple at the centre of some desert villages.
        QCOMPARE(withChests.size(), qsizetype{21});
    }

    void aChestOfAnOptionalPartStandsOnlyWithThePart()
    {
        // The deck of this hut holds a second chest; without the part only the first one stands.
        const QcVillageStyles styles(*m_content);
        const QcVillageStyle::Piece &huts = styles.named(u"qtcraft:swamp"_s)->pieces.front();
        const auto hut = std::ranges::find_if(huts.blueprints, [](const QcVillageStyle::Blueprint &blueprint) {
            return blueprint.structure()->name() == u"qtcraft:village_swamp_hut_b"_s;
        });
        QVERIFY(hut != huts.blueprints.end());
        QcPieceVariation::Selection selection = hut->variation.everything(QcGeneratorVersion::current);
        QCOMPARE(chestCells(*hut->variation.build(selection)).size(), qsizetype{2});
        selection.parts.fill(false);
        const std::shared_ptr<const QcStructure> bare = hut->variation.build(selection);
        QCOMPARE(chestCells(*bare).size(), qsizetype{1});
        const QList<QcBlockPos> left = chestCells(*bare);
        QVERIFY(!tableAt(*bare, left.constFirst()).isEmpty());
    }

    void stylesHaveTheirOwnFinds()
    {
        const QcVillageStyles styles(*m_content);
        const QStringList names{u"plains"_s, u"taiga"_s, u"snowy"_s, u"desert"_s, u"swamp"_s};
        for (const QString &name : names) {
            const QSet<QString> tables = tablesOf(*styles.named(u"qtcraft:"_s + name));
            for (const QString &other : names) {
                QCOMPARE(tables.contains(u"qtcraft:village_house_"_s + other), other == name);
            }
            QVERIFY2(tables.contains(u"qtcraft:village_fisher"_s), qPrintable(name));
        }
        QVERIFY(tablesOf(*styles.named(u"qtcraft:plains"_s)).contains(u"qtcraft:village_smith"_s));
        QVERIFY(tablesOf(*styles.named(u"qtcraft:taiga"_s)).contains(u"qtcraft:village_smith"_s));
        QVERIFY(tablesOf(*styles.named(u"qtcraft:snowy"_s)).contains(u"qtcraft:village_smith"_s));
        QVERIFY(tablesOf(*styles.named(u"qtcraft:swamp"_s)).contains(u"qtcraft:village_hall_swamp"_s));
    }

    void everyTableFillsAChestWithKnownItems()
    {
        const QcItemId diamond = *m_content->itemId(u"qtcraft:diamond"_s);
        const QcItemId emerald = *m_content->itemId(u"qtcraft:emerald"_s);
        constexpr int draws = 500;
        int tables = 0;
        for (const QcLootTableDefinition &table : m_content->lootTables().all()) {
            if (!table.name.startsWith(u"qtcraft:village_"_s)) {
                continue;
            }
            ++tables;
            for (const QcLootTableDefinition::Pool &pool : table.pools) {
                for (const QcLootTableDefinition::Entry &entry : pool.entries) {
                    QVERIFY2(m_content->itemId(entry.item), qPrintable(table.name + u": "_s + entry.item));
                }
            }
            int valuable = 0;
            for (int seed = 0; seed < draws; ++seed) {
                const QList<QcItemStack> stacks = draw(table.name, static_cast<quint64>(seed));
                QVERIFY2(!stacks.isEmpty(), qPrintable(table.name));
                valuable += std::ranges::any_of(stacks, [&](const QcItemStack &stack) { return stack.item == diamond || stack.item == emerald; }) ? 1 : 0;
            }
            // Finds of the early game: a valuable item is the exception.
            QVERIFY2(valuable < draws / 4, qPrintable(u"%1: %2 of %3"_s.arg(table.name).arg(valuable).arg(draws)));
        }
        QCOMPARE(tables, 8);
    }

    void aPlacedChestKeepsItsTableInEveryTurn_data()
    {
        QTest::addColumn<QString>("style");
        QTest::addColumn<int>("turns");
        for (const QcVillageStyleDefinition &definition : m_content->villageCatalog().styles()) {
            for (int turns = 0; turns < 4; ++turns) {
                QTest::addRow("%s %d", qPrintable(definition.name), turns) << definition.name << turns;
            }
        }
    }

    void aPlacedChestKeepsItsTableInEveryTurn()
    {
        QFETCH(QString, style);
        QFETCH(int, turns);
        QcVillageFixture::Options options;
        options.style = style;
        options.turns = turns;
        const std::shared_ptr<const QcVillagePlan> plan = QcVillageFixture::plan(*m_content, options);
        QVERIFY(plan);
        const QcVillageFixture::Terrain ground(m_content, {plan});
        int chests = 0;
        for (const QcStructurePlacement &piece : plan->pieces) {
            const QList<QcBlockPos> cells = chestCells(piece.structure());
            for (const QcBlockPos &cell : cells) {
                const QcBlockPos at = piece.toWorld(cell);
                const std::shared_ptr<QcChunk> chunk = ground.generate(at.chunk());
                QVERIFY2(isChest(chunk->block(at.localX(), at.y, at.localZ())), qPrintable(piece.structure().name()));
                QVERIFY(holds(QcVillageGenerator::furnishingIn(at.chunk(), *plan), {at, tableAt(piece.structure(), cell), 0}));
                ++chests;
            }
        }
        QVERIFY(chests > 0);
    }

    void theShowcaseRowsFillTheirChests()
    {
        const QcShowcaseGenerator showcase(m_content);
        const auto rows = std::ranges::find_if(showcase.plan().placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name() == u"showcase/villages"_s;
        });
        QVERIFY(rows != showcase.plan().placements.end());
        const QList<QcBlockPos> cells = chestCells(rows->structure());
        QVERIFY(!cells.isEmpty());
        for (const QcBlockPos &cell : cells) {
            // The chest of the village station holds its own bread (QcShowcaseSettlement).
            if (std::ranges::find(rows->structure().contents, cell, &QcStructureDefinition::Contents::at) != rows->structure().contents.end()) {
                continue;
            }
            const QcBlockPos at = rows->toWorld(cell);
            QVERIFY2(!tableAt(rows->structure(), cell).isEmpty(), qPrintable(u"%1 %2 %3"_s.arg(at.x).arg(at.y).arg(at.z)));
            QVERIFY(holds(showcase.furnishingIn(at.chunk()), {at, tableAt(rows->structure(), cell), 0}));
        }
    }

    void generatedVillagesFillTheirChests_data()
    {
        // Fixed seeds and a random one (decision E-846).
        QTest::addColumn<qint64>("seed");
        for (const qint64 seed : {qint64{42}, qint64{7}, qint64{20261017}, QcTestUtil::randomSeed()}) {
            QTest::addRow("seed %lld", static_cast<long long>(seed)) << seed;
        }
    }

    void generatedVillagesFillTheirChests()
    {
        QFETCH(qint64, seed);
        const std::unique_ptr<QcWorldGenerator> generator = terrain(seed, QcGeneratorVersion::current);
        const auto &world = dynamic_cast<const QcTerrainGenerator &>(*generator);
        const QList<QcChunkFurnishing::Chest> chests = chestsOf(world, samplePlans(world, 3, 8));
        QVERIFY(!chests.isEmpty());
        Chunks chunks;
        for (const QcChunkFurnishing::Chest &chest : chests) {
            const QcChunkPos pos = chest.at.chunk();
            if (!chunks.contains(pos)) {
                chunks.insert(pos, generator->generate(pos));
            }
            QVERIFY(isChest(chunks[pos]->block(chest.at.localX(), chest.at.y, chest.at.localZ())));
            const QcChunkFurnishing furnishing = generator->furnishingIn(pos);
            QVERIFY2(!chest.lootTable.isEmpty() && holds(furnishing, chest), qPrintable(chest.lootTable));
            const auto placed = std::ranges::find(furnishing.chests, chest.at, &QcChunkFurnishing::Chest::at);
            QVERIFY(!draw(placed->lootTable, placed->seed).isEmpty());
        }
    }

    void olderVersionsBuildWhatTheyBuilt()
    {
        // In every style, the buildings of all kinds have no new loot in frozen versions and are what
        // the structure says as written, although every one of them is built as a variation now;
        // the desert temple at the centre of a village keeps the loot it always had.
        const QcVillageStyles styles(*m_content);
        for (const QcVillageStyleDefinition &definition : m_content->villageCatalog().styles()) {
            const QcVillageStyle *style = styles.named(definition.name);
            QList<QcVillageStyle::Piece> kinds = style->pieces;
            kinds.append({style->centre, style->rareCentre, style->lamp});
            for (const QcVillageStyle::Piece &kind : std::as_const(kinds)) {
                for (const QcVillageStyle::Blueprint &blueprint : kind.blueprints) {
                    QcPieceVariation::Selection selection = blueprint.variation.everything(beforeLoot);
                    selection.parts.fill(false);
                    compareWithBase(*blueprint.variation.build(selection), *blueprint.structure());
                    if (QTest::currentTestFailed()) {
                        return;
                    }
                }
            }
        }
        const QcVillageStyle::Blueprint &temple = styles.named(u"qtcraft:desert"_s)->rareCentre.first();
        QVERIFY(!temple.variation.build(temple.variation.everything(1))->loot.isEmpty());
    }

    void noChangeReachesAFrozenVersion()
    {
        // A generator change for a frozen version would change its worlds. The entries of a version
        // that were there when a release froze it are counted here; the count of any later version
        // joins this list with that release, and no entry may come after.
        const QHash<int, int> frozenEntries{{3, 25}};
        QHash<int, int> entries;
        for (const QcStructureDefinition &structure : m_content->worldgen().structures()) {
            for (const QcStructureDefinition::VersionChange &change : structure.generatorChanges) {
                ++entries[change.version];
            }
        }
        QVERIFY(!entries.isEmpty());
        for (int version = QcGeneratorVersion::oldest; version <= QcGeneratorVersion::lastFrozen; ++version) {
            QCOMPARE(entries.value(version), frozenEntries.value(version));
        }
    }

    void olderVersionsKeepTheirChestsEmpty()
    {
        constexpr qint64 seed = 42;
        constexpr int frozen = beforeLoot;
        const std::unique_ptr<QcWorldGenerator> old = terrain(seed, frozen);
        const auto &world = dynamic_cast<const QcTerrainGenerator &>(*old);
        const QList<std::shared_ptr<const QcVillagePlan>> plans = samplePlans(world, 3, 8);
        // The chests without a table in that version: the temple at a village's centre has its own.
        QList<QcChunkFurnishing::Chest> chests = chestsOf(world, plans);
        chests.removeIf([](const QcChunkFurnishing::Chest &chest) { return !chest.lootTable.isEmpty(); });
        QVERIFY(!chests.isEmpty());

        // Chunks of the old version in a world switched to the current one stay empty too: with all
        // chunks of the villages kept, the old version builds them (decision E-1013). The current
        // version fills the same chests where the world has no such chunks.
        QcGeneratorVersionMap map;
        QcChunkSet kept;
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QcXzRect bounds = plan->bounds();
            const QcChunkPos low = QcBlockPos{bounds.minX, 0, bounds.minZ}.chunk();
            const QcChunkPos high = QcBlockPos{bounds.maxX, 0, bounds.maxZ}.chunk();
            for (int z = low.z; z <= high.z; ++z) {
                for (int x = low.x; x <= high.x; ++x) {
                    kept.insert(QcChunkPos{x, z});
                }
            }
        }
        map.mark(kept, frozen);
        const auto create = [this](int version) { return terrain(seed, version); };
        QcVersionedGenerator switched(terrain(seed, QcGeneratorVersion::current));
        QVERIFY(switched.setVersionMap(map, create).has_value());
        const std::unique_ptr<QcWorldGenerator> current = terrain(seed, QcGeneratorVersion::current);
        for (const QcChunkFurnishing::Chest &chest : std::as_const(chests)) {
            const QcChunkPos pos = chest.at.chunk();
            QVERIFY(switched.villageFor(pos));
            QVERIFY(isChest(switched.generate(pos)->block(chest.at.localX(), chest.at.y, chest.at.localZ())));
            QVERIFY(!filledAt(old->furnishingIn(pos), chest.at));
            QVERIFY(!filledAt(switched.furnishingIn(pos), chest.at));
            QVERIFY(filledAt(current->furnishingIn(pos), chest.at));
        }
    }
};

QTEST_GUILESS_MAIN(TestVillageChests)
#include "test_village_chests.moc"
