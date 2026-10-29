// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcContentParser.h"
#include "common/world/QcVegetationTable.h"
#include "QcServerTestFixture.h"
#include "server/plants/QcVegetationSpread.h"
#include "worldgen/terrain/QcTerrainColumnGrid.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace {

// The test terrain, but every column reports one biome.
class OneBiomeGenerator : public QcTestTerrainGenerator
{
public:
    OneBiomeGenerator(std::shared_ptr<const QcContentRegistry> content, QString biome)
        : QcTestTerrainGenerator(std::move(content))
        , m_biome(std::move(biome))
    {
    }
    QString biomeAt(int, int) const override { return m_biome; }

private:
    QString m_biome;
};

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

} // namespace

// The grass and flower tables of the biomes (decision E-814): parsing, the generator's scattered
// plants and patches, and bone meal on grass.
class TestVegetation : public QcServerTestFixture
{
    Q_OBJECT

    static constexpr int arena = 9;

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // The block names of a layer of a content biome.
    QStringList plantsOf(const QString &biome, bool flowers) const
    {
        QStringList names;
        const auto found = std::ranges::find(m_content->worldgen().biomes(), biome, &QcBiomeDefinition::name);
        if (found == m_content->worldgen().biomes().end()) {
            return names;
        }
        for (const QcVegetationPlant &plant : (flowers ? found->vegetation.flowers : found->vegetation.grass).plants) {
            names.append(plant.block);
        }
        return names;
    }

    // A flat grass floor around the player with air above it.
    void clearArena()
    {
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    // Uses bone meal `uses` times on the grass next to the player and counts the plants by name.
    QHash<QString, int> fertilize(int uses)
    {
        player().inventory.add(item("qtcraft:bone_meal"), 64, *m_content);
        QHash<QString, int> found;
        for (int use = 0; use < uses; ++use) {
            clearArena();
            send(QcMsg::UseItem{m_feet.offset(1, -1, 0), QcBlockFace::Up});
            for (int dx = -arena; dx <= arena; ++dx) {
                for (int dz = -arena; dz <= arena; ++dz) {
                    for (int dy = -1; dy <= 2; ++dy) {
                        const std::optional<QcBlockId> block = m_server->world().blockAt(m_feet.offset(dx, dy, dz));
                        if (block && *block != qcAirBlockId && *block != id("qtcraft:grass_block")) {
                            ++found[m_content->block(*block).name];
                        }
                    }
                }
            }
        }
        return found;
    }

    using Patch = QcVegetationPatchPlacer::Patch;

    // A flower patch has one kind of its biome's flowers and tries only columns within its radius.
    void checkPatch(const QcTerrainGenerator &generator, const Patch &patch) const
    {
        if (patch.layer != QcTerrainDefinitions::Flowers) {
            return;
        }
        const QString biome = generator.biomeName(patch.biome);
        QVERIFY2(plantsOf(biome, true).contains(m_content->block(patch.block).name), qPrintable(biome));
        QVERIFY(patch.radius >= 1 && patch.radius <= QcVegetationPatches::maxRadius);
        for (const auto &[x, z] : patch.tries) {
            QVERIFY2(std::abs(x - patch.centreX) <= patch.radius && std::abs(z - patch.centreZ) <= patch.radius,
                     qPrintable(u"%1,%2 from %3,%4"_s.arg(x).arg(z).arg(patch.centreX).arg(patch.centreZ)));
        }
    }

    // Flowers on the columns a flower patch tries inside `chunk`: of its kind, of other kinds.
    std::pair<int, int> flowersOnTries(const QcTerrainGenerator &generator, const QcChunk &chunk, const Patch &patch) const
    {
        std::pair<int, int> counts{0, 0};
        if (patch.layer != QcTerrainDefinitions::Flowers) {
            return counts;
        }
        const QStringList flowers = plantsOf(generator.biomeName(patch.biome), true);
        for (const auto &[x, z] : patch.tries) {
            const int lx = x - chunk.pos().x * QcWorld::chunkSize;
            const int lz = z - chunk.pos().z * QcWorld::chunkSize;
            if (lx < 0 || lx >= QcWorld::chunkSize || lz < 0 || lz >= QcWorld::chunkSize) {
                continue;
            }
            for (int y = 1; y < QcWorld::worldHeight; ++y) {
                const QcBlockId block = chunk.block(lx, y, lz);
                counts.first += block == patch.block ? 1 : 0;
                counts.second += block != patch.block && flowers.contains(m_content->block(block).name) ? 1 : 0;
            }
        }
        return counts;
    }

    void joinIn(const QString &biome)
    {
        join({}, std::make_unique<OneBiomeGenerator>(m_content, biome));
        loadAround(m_feet, 1);
    }

    int countOf(const QHash<QString, int> &found, const QStringList &names) const
    {
        int count = 0;
        for (const QString &name : names) {
            count += found.value(name);
        }
        return count;
    }

    // The first chunk near the origin that a flower patch tries columns in.
    std::optional<QcChunkPos> chunkWithFlowerPatch(const QcTerrainGenerator &generator) const
    {
        for (int index = 0; index < 25 * 25; ++index) {
            const QcChunkPos pos{index % 25 - 12, index / 25 - 12};
            const QList<Patch> near = generator.vegetationPatches().patchesNear(pos);
            for (const Patch &patch : near) {
                if (patch.layer == QcTerrainDefinitions::Flowers && QcBlockPos{patch.centreX, 0, patch.centreZ}.chunk() == pos) {
                    return pos;
                }
            }
        }
        return std::nullopt;
    }

    // The patch plants in the columns the patches try inside `chunk`, removed from it (the
    // columns are emptied of their patch kind) when `remove` is set.
    int patchPlants(QcChunk &chunk, const QList<Patch> &patches, bool remove) const
    {
        int count = 0;
        for (const Patch &patch : patches) {
            for (const auto &[x, z] : patch.tries) {
                const int lx = x - chunk.pos().x * QcWorld::chunkSize;
                const int lz = z - chunk.pos().z * QcWorld::chunkSize;
                if (lx < 0 || lx >= QcWorld::chunkSize || lz < 0 || lz >= QcWorld::chunkSize) {
                    continue;
                }
                for (int y = 1; y < QcWorld::worldHeight; ++y) {
                    if (chunk.block(lx, y, lz) != patch.block) {
                        continue;
                    }
                    ++count;
                    if (remove) {
                        chunk.setBlock(lx, y, lz, qcAirBlockId);
                    }
                }
            }
        }
        return count;
    }

    // Patch plants on columns at or below sea level among the columns the patches try.
    int plantsOnWater(const QcTerrainGenerator &generator, const QcChunk &chunk, const QList<Patch> &patches) const
    {
        const QcTerrainColumnGrid columns(generator, chunk.pos());
        int count = 0;
        for (const Patch &patch : patches) {
            for (const auto &[x, z] : patch.tries) {
                const int lx = x - chunk.pos().x * QcWorld::chunkSize;
                const int lz = z - chunk.pos().z * QcWorld::chunkSize;
                const bool inside = lx >= 0 && lx < QcWorld::chunkSize && lz >= 0 && lz < QcWorld::chunkSize;
                if (!inside || columns.at(x, z).height - 1 > generator.seaLevel()) {
                    continue;
                }
                for (int y = 1; y < QcWorld::worldHeight; ++y) {
                    count += chunk.block(lx, y, lz) == patch.block ? 1 : 0;
                }
            }
        }
        return count;
    }

private slots:
    void parsesTheVegetationTables()
    {
        const auto biome = QcContentParser::parseBiome(json(R"({"name":"meadow","surface":"grass","vegetation":{
            "grass":{"density":0.2,"plants":["short_grass",{"block":"q:fern","weight":3}]},
            "flowers":{"density":0.05,"patchSize":12,"plants":[{"block":"rose","weight":7}],"patches":{"chance":0.4,"tries":20,"radius":6}}}})"),
                                                       u"b"_s);
        QVERIFY2(biome.has_value(), qcErrorText(biome).constData());
        const QcVegetationDefinition &vegetation = biome->vegetation;
        QCOMPARE(vegetation.grass.density, 0.2);
        QCOMPARE(vegetation.grass.plants.size(), 2);
        QCOMPARE(vegetation.grass.plants.first().block, u"b:short_grass"_s);
        QCOMPARE(vegetation.grass.plants.first().weight, 1);
        QCOMPARE(vegetation.grass.plants.last().block, u"q:fern"_s);
        QCOMPARE(vegetation.grass.plants.last().weight, 3);
        QCOMPARE(vegetation.grass.patches.chance, 0.0);
        QCOMPARE(vegetation.flowers.patchSize, 12);
        QCOMPARE(vegetation.flowers.patches.chance, 0.4);
        QCOMPARE(vegetation.flowers.patches.tries, 20);
        QCOMPARE(vegetation.flowers.patches.radius, 6);

        // Broken tables refuse the biome.
        for (const char *broken : {R"({"grass":{"plants":[]}})", R"({"grass":{"density":0.1}})", R"({"flowers":{"plants":[{"block":"a","weight":0}]}})",
                                   R"({"flowers":{"plants":["a"],"patches":{"radius":9}}})", R"({"flowers":{"plants":[3]}})",
                                   R"({"grass":{"plants":["a"],"density":2}})", R"({"grass":[]})"}) {
            const QByteArray text = R"({"name":"x","surface":"s","vegetation":)" + QByteArray(broken) + "}";
            QVERIFY2(!QcContentParser::parseBiome(json(text.constData()), u"b"_s).has_value(), broken);
        }
    }

    void tablesResolveAndPickByWeight()
    {
        QcVegetationLayer layer;
        layer.plants = {{u"qtcraft:poppy"_s, 1}, {u"qtcraft:no_such_flower"_s, 5}, {u"qtcraft:dandelion"_s, 3}};
        const QcVegetationTable::Resolved resolved = QcVegetationTable::resolve(*m_content, layer);
        QCOMPARE(resolved.unknown, QStringList{u"qtcraft:no_such_flower"_s});
        QCOMPARE(resolved.table.plants().size(), 2);
        QVERIFY(resolved.table.plants().first().ground.contains(id("qtcraft:grass_block")));

        // A quarter for the poppy, three quarters for the dandelion.
        QCOMPARE(resolved.table.pick(0.0).block, id("qtcraft:poppy"));
        QCOMPARE(resolved.table.pick(0.24).block, id("qtcraft:poppy"));
        QCOMPARE(resolved.table.pick(0.26).block, id("qtcraft:dandelion"));
        QCOMPARE(resolved.table.pick(0.999).block, id("qtcraft:dandelion"));
    }

    void everyBiomeTableIsValid()
    {
        const QcTerrainGenerator generator(m_content, 1);
        QVERIFY2(generator.warnings().isEmpty(), qPrintable(generator.warnings().join(u'\n')));
        for (const QcBiomeDefinition &biome : m_content->worldgen().biomes()) {
            // Bone meal grows grass on a grass block in every biome.
            QVERIFY2(!biome.vegetation.grass.plants.isEmpty(), qPrintable(biome.name));
            for (const QcVegetationLayer *layer : {&biome.vegetation.grass, &biome.vegetation.flowers}) {
                for (const QcVegetationPlant &plant : layer->plants) {
                    const std::optional<QcBlockId> block = m_content->blockId(plant.block);
                    QVERIFY2(block.has_value(), qPrintable(plant.block));
                    QVERIFY2(m_content->block(*block).groundBlocks.contains(u"qtcraft:grass_block"_s), qPrintable(plant.block));
                }
            }
        }
        QCOMPARE(m_content->worldgen().terrainSettings().vegetationBiome, u"qtcraft:plains"_s);
    }

    void flowerPatchesAreDeterministicAndStayWithinTheirRadius()
    {
        const QcTerrainGenerator generator(m_content, 7);
        const QcTerrainGenerator same(m_content, 7);
        const QcTerrainGenerator other(m_content, 8);
        int patches = 0;
        int differing = 0;
        for (int cx = -12; cx <= 12; ++cx) {
            for (int cz = -12; cz <= 12; ++cz) {
                const QList<Patch> near = generator.vegetationPatches().patchesNear({cx, cz});
                const QList<Patch> again = same.vegetationPatches().patchesNear({cx, cz});
                QCOMPARE(again.size(), near.size());
                differing += other.vegetationPatches().patchesNear({cx, cz}).size() != near.size() ? 1 : 0;
                for (qsizetype index = 0; index < near.size(); ++index) {
                    const Patch &patch = near.at(index);
                    QCOMPARE(again.at(index).block, patch.block);
                    QCOMPARE(again.at(index).tries, patch.tries);
                    patches += patch.layer == QcTerrainDefinitions::Flowers ? 1 : 0;
                    checkPatch(generator, patch);
                }
            }
        }
        QVERIFY2(patches > 20, qPrintable(QString::number(patches)));
        QVERIFY(differing > 0);
    }

    void flowerPatchesPlantMostlyTheirOwnKind()
    {
        const QcTerrainGenerator generator(m_content, 7);
        QList<QcChunkPos> withPatches;
        for (int index = 0; index < 25 * 25 && withPatches.size() < 12; ++index) {
            const QList<Patch> near = generator.vegetationPatches().patchesNear({index % 25 - 12, index / 25 - 12});
            for (const Patch &patch : near) {
                const QcChunkPos home = QcBlockPos{patch.centreX, 0, patch.centreZ}.chunk();
                if (patch.layer == QcTerrainDefinitions::Flowers && !withPatches.contains(home)) {
                    withPatches.append(home);
                }
            }
        }

        // The cells a patch tries hold mostly its own kind (scattered flowers came before it).
        std::pair<int, int> counts{0, 0};
        for (const QcChunkPos &pos : std::as_const(withPatches)) {
            const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
            const QList<Patch> patches = generator.vegetationPatches().patchesNear(pos);
            for (const Patch &patch : patches) {
                const std::pair<int, int> found = flowersOnTries(generator, *chunk, patch);
                counts.first += found.first;
                counts.second += found.second;
            }
        }
        QVERIFY2(counts.first > 3 * counts.second && counts.first > 20, qPrintable(u"%1 own, %2 others"_s.arg(counts.first).arg(counts.second)));
    }

    void boneMealGrowsThePlainsTable()
    {
        joinIn(u"qtcraft:plains"_s);
        const QHash<QString, int> found = fertilize(6);
        const QStringList grass = plantsOf(u"qtcraft:plains"_s, false);
        const QStringList flowers = plantsOf(u"qtcraft:plains"_s, true);
        int total = 0;
        for (auto it = found.cbegin(); it != found.cend(); ++it) {
            QVERIFY2(grass.contains(it.key()) || flowers.contains(it.key()), qPrintable(it.key()));
            total += it.value();
        }
        // About one plant in eight is a flower.
        const int flowerCount = countOf(found, flowers);
        QVERIFY2(total > 60, qPrintable(QString::number(total)));
        QVERIFY2(flowerCount > 0 && flowerCount < total / 2, qPrintable(u"%1 of %2"_s.arg(flowerCount).arg(total)));
    }

    void boneMealGrowsTheTaigaTable()
    {
        joinIn(u"qtcraft:taiga"_s);
        const QHash<QString, int> found = fertilize(4);
        // The taiga has ferns and grass and no flowers.
        QCOMPARE(countOf(found, {u"qtcraft:tall_grass"_s, u"qtcraft:fern"_s}), countOf(found, found.keys()));
        QVERIFY(found.value(u"qtcraft:fern"_s) > 0);
        QVERIFY(found.value(u"qtcraft:tall_grass"_s) > 0);
    }

    void boneMealWithoutBiomeUsesTheFallbackAndNeedsAir()
    {
        join();
        loadAround(m_feet, 1);
        const QHash<QString, int> found = fertilize(3);
        const QStringList plains = plantsOf(u"qtcraft:plains"_s, false) + plantsOf(u"qtcraft:plains"_s, true);
        QVERIFY(countOf(found, found.keys()) > 0);
        QCOMPARE(countOf(found, plains), countOf(found, found.keys()));

        // A covered grass block takes no bone meal.
        clearArena();
        const QcBlockPos target = m_feet.offset(1, -1, 0);
        m_server->world().setBlock(target.offset(0, 1, 0), id("qtcraft:stone"));
        const QcItemId meal = item("qtcraft:bone_meal");
        const int before = player().inventory.countOf(meal);
        send(QcMsg::UseItem{target, QcBlockFace::Up});
        QCOMPARE(player().inventory.countOf(meal), before);
        const std::optional<QcBlockId> beside = m_server->world().blockAt(m_feet.offset(2, 0, 0));
        QCOMPARE(beside, std::optional<QcBlockId>(qcAirBlockId));
    }

    void patchesSkipColumnsTheFilterRefusesAndWater()
    {
        const QcTerrainGenerator generator(m_content, 7);
        const std::optional<QcChunkPos> pos = chunkWithFlowerPatch(generator);
        QVERIFY(pos.has_value());
        const QList<Patch> patches = generator.vegetationPatches().patchesNear(*pos);
        const std::shared_ptr<QcChunk> chunk = generator.generate(*pos);
        QVERIFY(patchPlants(*chunk, patches, true) > 0);
        QCOMPARE(patchPlants(*chunk, patches, false), 0);

        // A village or structure column (refused by the filter) gets no patch plant.
        const QcTerrainColumnGrid columns(generator, *pos);
        const QList<bool> carved(static_cast<qsizetype>(QcWorld::chunkSize) * QcWorld::chunkSize, false);
        generator.vegetationPatches().place(*chunk, columns, carved, [](int, int) { return false; });
        QCOMPARE(patchPlants(*chunk, patches, false), 0);

        // Allowed again, the patches plant on dry land only.
        generator.vegetationPatches().place(*chunk, columns, carved, [](int, int) { return true; });
        QVERIFY(patchPlants(*chunk, patches, false) > 0);
        QCOMPARE(plantsOnWater(generator, *chunk, patches), 0);
    }

    void layerWithoutDensityOrPatchesPlacesNothing()
    {
        const QcTerrainDefinitions definitions(*m_content, 1);
        const auto desert = std::ranges::find(definitions.biomes, u"qtcraft:desert"_s, &QcTerrainDefinitions::Biome::name);
        QVERIFY(desert != definitions.biomes.end());
        const QcVegetationPatches &grass = desert->patches[QcTerrainDefinitions::Grass].patches;
        QCOMPARE(grass.chance, 0.0);
        // No scattered vegetation feature: the desert's grass grows only from bone meal.
        int vegetationFeatures = 0;
        for (const auto &[feature, density] : desert->features) {
            vegetationFeatures += definitions.features[feature].plants.isEmpty() ? 0 : 1;
        }
        QCOMPARE(vegetationFeatures, 0);
    }

    void survivalUsesOneBoneMealPerUse()
    {
        joinIn(u"qtcraft:plains"_s);
        const QcItemId meal = item("qtcraft:bone_meal");
        player().inventory.add(meal, 5, *m_content);
        clearArena();
        send(QcMsg::UseItem{m_feet.offset(1, -1, 0), QcBlockFace::Up});
        QCOMPARE(player().inventory.countOf(meal), 4);
        clearArena();
        send(QcMsg::UseItem{m_feet.offset(1, -1, 0), QcBlockFace::Up});
        QCOMPARE(player().inventory.countOf(meal), 3);
    }
};

QTEST_GUILESS_MAIN(TestVegetation)
#include "test_vegetation.moc"
