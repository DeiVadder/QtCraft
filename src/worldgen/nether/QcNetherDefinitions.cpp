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

private:
    const QcContentRegistry &m_content;
    QStringList &m_warnings;
};

[[nodiscard]] QcNetherDefinitions::Biome biomeOf(const QcBiomeDefinition &definition, Blocks &blocks, QcBlockId filler)
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
    return biome;
}

[[nodiscard]] QList<QcNetherDefinitions::Biome> biomesOf(const QcContentRegistry &content, Blocks &blocks, QcBlockId filler)
{
    QList<QcNetherDefinitions::Biome> biomes;
    for (const QcBiomeDefinition &definition : content.worldgen().biomes()) {
        if (definition.dimension == netherName) {
            biomes.append(biomeOf(definition, blocks, filler));
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
               || std::ranges::contains(biome.patches, block, &std::pair<QcBlockId, double>::first);
    };
    return block == filler || std::ranges::any_of(biomes, inBiome) || std::ranges::contains(ores, block, &QcTerrainDefinitions::Ore::block)
           || std::ranges::contains(shores, block, &Shore::block) || std::ranges::contains(clusters, block, &Cluster::block);
}

QcNetherDefinitions::QcNetherDefinitions(const QcContentRegistry &content, quint64 seed)
    : settings(content.worldgen().nether())
    , patchSeed(qcMixSeed(seed, qcNameSalt(u"qtcraft:nether_patches"_s)))
{
    Blocks blocks(content, warnings);
    filler = blocks.of(settings.filler, content.blockId(u"qtcraft:stone"_s).value_or(qcAirBlockId));
    bedrock = blocks.of(settings.bedrock, filler);
    liquid = blocks.of(settings.liquid, qcAirBlockId);
    biomes = biomesOf(content, blocks, filler);
    ores = oresOf(content, blocks, filler, seed);

    for (const QcNetherSettings::Shore &shore : std::as_const(settings.shores)) {
        const quint64 own = qcMixSeed(seed, qcNameSalt(shore.block) + 1);
        shores.append({QcNoise(own), blocks.of(shore.block, filler), shore.scale, shore.threshold, shore.height, shore.depth});
    }
    for (const QcNetherSettings::Cluster &cluster : std::as_const(settings.clusters)) {
        clusters.append({blocks.of(cluster.block, filler), blocks.of(cluster.on, filler), cluster.tries, cluster.minY, cluster.maxY, cluster.size,
                         cluster.radius, cluster.depth, qcMixSeed(seed, qcNameSalt(cluster.block) + 2)});
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
