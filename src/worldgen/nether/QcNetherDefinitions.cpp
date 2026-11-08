// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherDefinitions.h"

#include "common/content/registry/QcContentFields.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The dimension whose biomes and ores this generator takes.
const QString netherName = u"qtcraft:nether"_s;

// Resolves names of `content`, noting the unknown ones.
class Blocks
{
public:
    Blocks(const QcContentRegistry &content, QStringList &warnings)
        : m_content(content)
        , m_warnings(warnings)
    {
    }

    [[nodiscard]] QcBlockId of(const QString &name, QcBlockId fallback)
    {
        if (const std::optional<QcBlockId> block = m_content.blockId(name)) {
            return *block;
        }
        m_warnings.append(u"The nether names the unknown block %1"_s.arg(name));
        return fallback;
    }

    // Notes `warning` with the others of the nether.
    void warn(const QString &warning) { m_warnings.append(warning); }

private:
    const QcContentRegistry &m_content;
    QStringList &m_warnings;
};

// A column of plants `placement` puts into a biome.
[[nodiscard]] QcNetherDefinitions::Strand strandOf(const QcFeatureDefinition &feature, const QcFeaturePlacement &placement, Blocks &blocks, quint64 seed)
{
    QcNetherDefinitions::Strand strand;
    strand.block = blocks.of(feature.trunkBlock, qcAirBlockId);
    for (const QString &ground : feature.groundBlocks) {
        strand.ground.append(blocks.of(ground, qcAirBlockId));
    }
    strand.minHeight = feature.minHeight;
    strand.maxHeight = feature.maxHeight;
    strand.fromRoof = feature.fromRoof;
    strand.density = placement.density;
    strand.seed = qcMixSeed(seed, qcNameSalt(feature.name));
    return strand;
}

// What the features of `definition` add to `biome`; pools and single plants have no place here.
[[nodiscard]] QcNetherDefinitions::Biome withFeatures(QcNetherDefinitions::Biome biome, const QcBiomeDefinition &definition, const QcContentRegistry &content, Blocks &blocks, quint64 seed)
{
    for (const QcFeaturePlacement &placement : definition.features) {
        const QList<QcFeatureDefinition> &features = content.worldgen().features();
        const auto feature = std::ranges::find(features, placement.feature, &QcFeatureDefinition::name);
        if (feature == features.end()) {
            blocks.warn(u"The nether biome %1 names the unknown feature %2"_s.arg(definition.name, placement.feature));
            continue;
        }
        if (feature->type == QcFeatureDefinition::Type::Column) {
            biome.strands.append(strandOf(*feature, placement, blocks, seed));
        } else if (feature->type == QcFeatureDefinition::Type::Tree) {
            biome.fungi.append({QcTreeShape::paramsOf(content, *feature), placement.density, qcMixSeed(seed, qcNameSalt(feature->name))});
        }
    }
    return biome;
}

[[nodiscard]] QcNetherDefinitions::Biome biomeOf(const QcBiomeDefinition &definition, const QcContentRegistry &content, Blocks &blocks, QcBlockId filler, quint64 seed)
{
    QcNetherDefinitions::Biome biome;
    biome.name = definition.name;
    biome.first = definition.temperature;
    biome.second = definition.humidity;
    biome.surface = blocks.of(definition.surfaceBlock, filler);
    biome.subsurface = blocks.of(definition.subsurfaceBlock, filler);
    biome.depth = definition.subsurfaceDepth;
    for (const QcBlockShare &patch : definition.surfacePatches) {
        biome.patches.append({blocks.of(patch.block, filler), patch.share});
    }
    biome.patchScale = definition.patchScale;
    if (!definition.rockBlock.isEmpty()) {
        biome.rock = blocks.of(definition.rockBlock, filler);
        biome.rockBand = definition.rockBandBlock.isEmpty() ? biome.rock : blocks.of(definition.rockBandBlock, filler);
    }
    biome.plants = QcVegetationTable::resolve(content, definition.vegetation.grass).table;
    biome.plantDensity = definition.vegetation.grass.density;
    return withFeatures(std::move(biome), definition, content, blocks, seed);
}

[[nodiscard]] QList<QcNetherDefinitions::Biome> biomesOf(const QcContentRegistry &content, Blocks &blocks, QcBlockId filler, quint64 seed)
{
    QList<QcNetherDefinitions::Biome> biomes;
    for (const QcBiomeDefinition &definition : content.worldgen().biomes()) {
        if (definition.dimension == netherName) {
            biomes.append(biomeOf(definition, content, blocks, filler, seed));
        }
    }
    if (biomes.isEmpty()) {
        QcNetherDefinitions::Biome fallback;
        fallback.name = u"qtcraft:fallback"_s;
        fallback.surface = fallback.subsurface = filler;
        biomes.append(fallback);
    }
    return biomes;
}

// The names in `names` that are no biome of `biomes`, each as a warning about `user`.
[[nodiscard]] QStringList unknownBiomes(const QStringList &names, const QList<QcNetherDefinitions::Biome> &biomes, const QString &user)
{
    QStringList warnings;
    for (const QString &name : names) {
        if (!std::ranges::contains(biomes, name, &QcNetherDefinitions::Biome::name)) {
            warnings.append(u"%1 of the nether names the unknown biome %2"_s.arg(user, name));
        }
    }
    return warnings;
}

[[nodiscard]] QList<QcTerrainDefinitions::Ore> oresOf(const QcContentRegistry &content, Blocks &blocks, QcBlockId filler, quint64 seed)
{
    QList<QcTerrainDefinitions::Ore> ores;
    for (const QcOreDefinition &definition : content.worldgen().ores()) {
        if (definition.dimension != netherName) {
            continue;
        }
        QcTerrainDefinitions::Ore ore;
        ore.block = blocks.of(definition.block, qcAirBlockId);
        ore.replaces = definition.replaces.isEmpty() ? filler : blocks.of(definition.replaces, filler);
        ore.minY = definition.minY;
        ore.maxY = definition.maxY;
        ore.veins = definition.veinsPerChunk;
        ore.veinSize = definition.veinSize;
        ore.hidden = definition.hidden;
        ore.seed = qcMixSeed(seed, qcNameSalt(definition.name));
        if (ore.block != qcAirBlockId) {
            ores.append(ore);
        }
    }
    return ores;
}

} // namespace

std::shared_ptr<const QcStructure> QcNetherDefinitions::structureOf(const QcContentRegistry &content, const QString &name)
{
    const QcStructureDefinition *definition = content.worldgen().structure(name);
    if (definition == nullptr) {
        warnings.append(u"The nether names the unknown structure %1"_s.arg(name));
        return nullptr;
    }
    QcStructure::Loaded loaded = QcStructure::fromDefinition(*definition, content);
    warnings.append(QcStructure::warningsOf(loaded));
    return loaded ? std::make_shared<const QcStructure>(std::move(*loaded)) : nullptr;
}

bool QcNetherDefinitions::isGround(QcBlockId block) const
{
    const auto inBiome = [block](const Biome &biome) {
        return block == biome.surface || block == biome.subsurface
               || (biome.rock != qcAirBlockId && (block == biome.rock || block == biome.rockBand)) || std::ranges::contains(biome.patches, block, &std::pair<QcBlockId, double>::first);
    };
    return block == filler || std::ranges::any_of(biomes, inBiome) || std::ranges::contains(ores, block, &QcTerrainDefinitions::Ore::block)
           || std::ranges::contains(shores, block, &Shore::block) || std::ranges::contains(clusters, block, &Cluster::block);
}

QcNetherDefinitions::QcNetherDefinitions(const QcContentRegistry &content, quint64 seed)
    : settings(content.worldgen().nether())
    , patchSeed(qcMixSeed(seed, qcNameSalt(u"qtcraft:nether_patches"_s)))
    , patchNoise(qcMixSeed(seed, qcNameSalt(u"qtcraft:nether_patches"_s) + 1))
    , bandNoise(qcMixSeed(seed, qcNameSalt(u"qtcraft:nether_bands"_s)))
{
    Blocks blocks(content, warnings);
    filler = blocks.of(settings.filler, content.blockId(u"qtcraft:stone"_s).value_or(qcAirBlockId));
    bedrock = blocks.of(settings.bedrock, filler);
    liquid = blocks.of(settings.liquid, qcAirBlockId);
    biomes = biomesOf(content, blocks, filler, seed);
    ores = oresOf(content, blocks, filler, seed);

    for (const QcNetherSettings::Shore &shore : std::as_const(settings.shores)) {
        const quint64 own = qcMixSeed(seed, qcNameSalt(shore.block) + 1);
        shores.append({QcNoise(own), blocks.of(shore.block, filler), shore.scale, shore.threshold, shore.height, shore.depth});
    }
    for (const QcNetherSettings::Cluster &cluster : std::as_const(settings.clusters)) {
        warnings.append(unknownBiomes(cluster.biomes, biomes, u"The cluster of %1"_s.arg(cluster.block)));
        clusters.append({blocks.of(cluster.block, filler), blocks.of(cluster.on, filler), cluster.tries, cluster.minY, cluster.maxY, cluster.size,
                         cluster.radius, cluster.depth, qcMixSeed(seed, qcNameSalt(cluster.block) + 2), cluster.biomes});
    }
    for (const QcNetherSettings::Pillar &pillar : std::as_const(settings.pillars)) {
        warnings.append(unknownBiomes(pillar.biomes, biomes, u"The pillar of %1"_s.arg(pillar.block)));
        const quint64 own = qcMixSeed(seed, qcNameSalt(pillar.block) + 6);
        pillars.append({QcNoise(own), blocks.of(pillar.block, filler), pillar.biomes, pillar.scale, pillar.threshold, pillar.height, own + 1});
    }
    for (const QcNetherSettings::Pool &pool : std::as_const(settings.pools)) {
        warnings.append(unknownBiomes(pool.biomes, biomes, u"The pool of %1"_s.arg(pool.block)));
        pools.append({QcNoise(qcMixSeed(seed, qcNameSalt(pool.block) + 7)), blocks.of(pool.block, liquid), blocks.of(pool.rim, filler), pool.biomes, pool.scale, pool.threshold, pool.rimBand});
    }
    for (const QcNetherSettings::Spring &spring : std::as_const(settings.springs)) {
        springs.append({blocks.of(spring.block, filler), blocks.of(spring.in, filler), spring.tries, spring.minY, spring.maxY,
                        qcMixSeed(seed, qcNameSalt(spring.block) + 3)});
    }
    for (const QcNetherSettings::Fire &fire : std::as_const(settings.fires)) {
        QList<QcBlockId> on;
        for (const QString &name : fire.on) {
            on.append(blocks.of(name, filler));
        }
        fires.append({blocks.of(fire.block, qcAirBlockId), std::move(on), fire.tries, qcMixSeed(seed, qcNameSalt(fire.block) + 4)});
    }
    for (const QcNetherSettings::Structure &entry : std::as_const(settings.structures)) {
        if (std::shared_ptr<const QcStructure> structure = structureOf(content, entry.structure)) {
            const QString name = entry.structure.startsWith(u"qtcraft:"_s) ? QcContentFields::localPart(entry.structure) : entry.structure;
            structures.append({std::move(structure), name, entry.spacing, entry.chance, qcMixSeed(seed, qcNameSalt(entry.structure) + 5)});
        }
    }
    if (!settings.station.structure.isEmpty()) {
        station = structureOf(content, settings.station.structure);
    }
}

void QcNetherDefinitions::sealPools(QcChunk &chunk, const QcBlockBox &box) const
{
    const int lowest = std::max(box.low.y, settings.liquidLevel + 2);
    const int top = std::min(box.high.y, settings.roof - 1);
    // The box in the chunk's own columns.
    const int originX = chunk.pos().x * QcWorld::chunkSize;
    const int originZ = chunk.pos().z * QcWorld::chunkSize;
    const int firstX = std::max(box.low.x - originX, 0);
    const int lastX = std::min(box.high.x - originX, QcWorld::chunkSize - 1);
    const int firstZ = std::max(box.low.z - originZ, 0);
    const int lastZ = std::min(box.high.z - originZ, QcWorld::chunkSize - 1);
    const int width = lastX - firstX + 1;
    const int columns = width > 0 && lastZ >= firstZ ? width * (lastZ - firstZ + 1) : 0;
    for (int column = 0; column < columns; ++column) {
        const int x = firstX + column % width;
        const int z = firstZ + column / width;
        for (int y = lowest; y <= top; ++y) {
            const auto pool = std::ranges::find(pools, chunk.block(x, y, z), &Pool::block);
            if (pool != pools.end()) {
                chunk.setBlock(x, y, z, pool->rim);
            }
        }
    }
}
