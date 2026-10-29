// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/handbook/builder/QcHandbookWorldPages.h"

#include "common/content/mobs/QcSpawnWeights.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"
#include "common/handbook/QcLinkList.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;
using Fact = QcHandbookEntry::Fact;
using Section = QcHandbookEntry::Section;

namespace {

QString perColumn(const QcHandbookText &text, const QcFeaturePlacement &placement)
{
    return text.label(u"unit.perColumn"_s).arg(text.number(placement.density * 100.0));
}

// The blocks a biome is made of, top to bottom.
[[nodiscard]] QList<Fact> layerFacts(const QcHandbookContext &context, const QcBiomeDefinition &biome)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    const auto layer = [&](const QString &block, const QString &key, const QString &note = {}) {
        if (!block.isEmpty()) {
            facts.append({text.label(u"fact."_s + key), {}, {context.links.blockLink(block, note)}});
        }
    };

    layer(biome.surfaceBlock, u"biome.surface"_s);
    layer(biome.subsurfaceBlock, u"biome.subsurface"_s, text.label(u"unit.blocks"_s).arg(biome.subsurfaceDepth));
    layer(biome.underwaterBlock, u"biome.underwater"_s);
    // Patches in the ground under water (river beds, decision E-840) join that fact.
    if (!biome.underwaterBlock.isEmpty()) {
        for (const QcBlockShare &patch : biome.underwaterPatches) {
            facts.back().links.append(context.links.blockLink(patch.block, text.percent(patch.share)));
        }
    }
    layer(biome.beachBlock, u"biome.beach"_s);
    layer(biome.steepBlock, u"biome.steep"_s, text.label(u"value.steep"_s).arg(biome.steepSlope));
    layer(biome.frozenWaterBlock, u"biome.frozenWater"_s);
    if (biome.snowLine > 0) {
        layer(biome.snowBlock, u"biome.snow"_s, text.label(u"value.above"_s).arg(biome.snowLine));
    }
    return facts;
}

// The plants of one vegetation layer the content knows (decision E-814); unknown ones are noted as
// problems and get no link.
[[nodiscard]] QList<QcVegetationPlant> knownPlants(const QcHandbookContext &context, const QcBiomeDefinition &biome, const QcVegetationLayer &layer)
{
    QList<QcVegetationPlant> plants;
    for (const QcVegetationPlant &plant : layer.plants) {
        if (context.content.blockId(plant.block)) {
            plants.append(plant);
        } else {
            context.text.addProblem(u"biome %1: unknown plant %2"_s.arg(biome.name, plant.block));
        }
    }
    return plants;
}

// The grasses and flowers of a biome (decision E-814), each with its share of its layer; a layer
// that never generates (no density, no patches) grows only from bone meal and is left out.
[[nodiscard]] QList<Section> vegetationSections(const QcHandbookContext &context, const QcBiomeDefinition &biome)
{
    QList<Section> sections;
    const QcHandbookText &text = context.text;
    const std::array<std::pair<const QcVegetationLayer *, QString>, 2> layers{
        {{&biome.vegetation.grass, u"section.grasses"_s}, {&biome.vegetation.flowers, u"section.flowers"_s}}};
    for (const auto &[layer, label] : layers) {
        if (layer->density <= 0.0 && layer->patches.chance <= 0.0) {
            continue;
        }
        const QList<QcVegetationPlant> plants = knownPlants(context, biome, *layer);
        int total = 0;
        for (const QcVegetationPlant &plant : plants) {
            total += plant.weight;
        }

        Section section{text.label(label), {}, {}, {}};
        for (const QcVegetationPlant &plant : plants) {
            section.links.append(context.links.blockLink(plant.block, text.percent(static_cast<double>(plant.weight) / total)));
        }
        if (!section.links.isEmpty()) {
            sections.append(std::move(section));
        }
    }
    return sections;
}

// The creatures spawning in a biome: the monsters with their share of the biome's monster list
// (decision E-838), the others by name.
[[nodiscard]] QList<Section> creatureSections(const QcHandbookContext &context, const QcBiomeDefinition &biome)
{
    const QcContentRegistry &content = context.content;
    const QcSpawnWeights weights(content);
    Section monsters{context.text.label(u"section.monsters"_s), {}, {}, {}};
    Section creatures{context.text.label(u"section.creatures"_s), {}, {}, {}};
    for (qsizetype type = 0; type < content.mobs().size(); ++type) {
        const QcMobDefinition &mob = content.mobs()[type];
        const double weight = weights.weight(static_cast<int>(type), biome.name);
        if (mob.isMonster() && weight > 0.0) {
            monsters.links.append(context.links.mobLink(mob, context.text.percent(weight / weights.total(biome.name))));
        } else if (!mob.isMonster() && QcSpawnWeights::ruleWeight(mob, biome.name) > 0.0) {
            creatures.links.append(context.links.mobLink(mob));
        }
    }

    QList<Section> sections;
    for (Section *section : {&monsters, &creatures}) {
        section->links = QcHandbookLinks::sortedLinks(std::move(section->links));
        if (!section->links.isEmpty()) {
            sections.append(std::move(*section));
        }
    }
    return sections;
}

// The features placed in a biome, its vegetation and the creatures spawning there.
[[nodiscard]] QList<Section> biomeSections(const QcHandbookContext &context, const QcBiomeDefinition &biome)
{
    QList<Section> sections;
    QcHandbookText &text = context.text;
    const QcContentRegistry &content = context.content;

    Section features{text.label(u"section.features"_s), {}, {}, {}};
    for (const QcFeaturePlacement &placement : biome.features) {
        const auto feature = std::ranges::find(content.worldgen().features(), placement.feature, &QcFeatureDefinition::name);
        if (feature == content.worldgen().features().end()) {
            text.addProblem(u"biome %1: unknown feature %2"_s.arg(biome.name, placement.feature));
            continue;
        }
        features.links.append(context.links.featureLink(*feature, perColumn(text, placement)));
    }
    features.links = QcHandbookLinks::sortedLinks(std::move(features.links));
    if (!features.links.isEmpty()) {
        sections.append(std::move(features));
    }
    sections += vegetationSections(context, biome);

    sections += creatureSections(context, biome);
    return sections;
}

// The land biomes whose rivers take a river biome (decision E-840) or whose deltas a delta biome
// (decision E-850): those naming it, and those naming none when it is the terrain's (or, without
// one, the content's first).
[[nodiscard]] Fact kindFact(const QcHandbookContext &context, const QcBiomeDefinition &special)
{
    const QList<QcBiomeDefinition> &biomes = context.content.worldgen().biomes();
    const QcTerrainSettings &terrain = context.content.worldgen().terrainSettings();
    const bool river = special.river;
    const QString &terrainKind = river ? terrain.riverBiome : terrain.deltaBiome;
    const auto first = std::ranges::find_if(biomes, river ? &QcBiomeDefinition::river : &QcBiomeDefinition::delta);
    const bool isFirst = terrainKind.isEmpty() ? first != biomes.end() && first->name == special.name : terrainKind == special.name;
    QcLinkList land;
    for (const QcBiomeDefinition &biome : biomes) {
        const QString &named = river ? biome.riverBiome : biome.deltaBiome;
        if (!biome.river && !biome.delta && (named == special.name || (named.isEmpty() && isFirst))) {
            land.addUnique(context.links.biomeLink(biome));
        }
    }
    return {context.text.label(river ? u"fact.riverOf"_s : u"fact.deltaOf"_s), {}, land.sorted()};
}

QcHandbookEntry biomePage(const QcHandbookContext &context, const QcBiomeDefinition &biome)
{
    QcHandbookText &text = context.text;
    QcHandbookEntry entry;
    entry.id = u"biome:"_s + biome.name;
    entry.name = biome.name;
    entry.category = u"biomes"_s;
    entry.pack = QcHandbookLinks::packOf(biome.name);
    entry.title = context.content.translations().translate(biome.translationKey, text.language());
    entry.icon = context.links.itemIcon(biome.surfaceBlock);
    entry.text = text.description(biome.descriptionKey, biome.name);

    entry.facts.append({text.label(u"fact.id"_s), biome.name, {}});
    if (biome.river || biome.delta) {
        // A river or delta biome has no climate cell or height of its own: it lies in the rivers
        // and deltas of others.
        entry.facts.append(kindFact(context, biome));
        entry.facts += layerFacts(context, biome);
        entry.sections += biomeSections(context, biome);
        entry.englishTitle = context.content.translations().translate(biome.translationKey, QcHandbookText::english());
        return entry;
    }
    entry.facts.append({text.label(u"fact.climate"_s),
                        text.label(u"value.climate"_s)
                            .arg(text.number(biome.temperature), text.number(biome.humidity), text.number(biome.ruggedness)),
                        {}});
    if (biome.rarity > 0.0) {
        entry.facts.append({text.label(u"fact.rarity"_s), text.label(u"value.rarity"_s).arg(text.number(biome.rarity)), {}});
    }
    entry.facts.append({text.label(u"fact.height"_s),
                        text.label(u"value.height"_s).arg(text.number(biome.baseHeight), text.number(biome.heightVariation)), {}});

    entry.facts += layerFacts(context, biome);
    entry.sections += biomeSections(context, biome);
    entry.englishTitle = context.content.translations().translate(biome.translationKey, QcHandbookText::english());
    return entry;
}

QcHandbookEntry terrainPage(const QcHandbookContext &context)
{
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;
    const QcTerrainSettings &terrain = context.content.worldgen().terrainSettings();

    QcHandbookEntry entry;
    entry.id = u"world:terrain"_s;
    entry.category = u"worldgen"_s;
    entry.pack = links.basePack();
    entry.title = text.label(u"world.terrain"_s);
    entry.icon = links.itemIcon(terrain.stoneBlock);
    entry.text = text.label(u"world.terrainText"_s);

    entry.facts.append({text.label(u"fact.seaLevel"_s), text.label(u"value.y"_s).arg(terrain.seaLevel), {}});
    entry.facts.append({text.label(u"fact.stone"_s), {}, {links.blockLink(terrain.stoneBlock)}});
    entry.facts.append({text.label(u"fact.water"_s), {}, {links.blockLink(terrain.waterBlock)}});
    if (!terrain.bedrockBlock.isEmpty()) {
        entry.facts.append({text.label(u"fact.bedrock"_s), {}, {links.blockLink(terrain.bedrockBlock)}});
    }
    entry.facts.append({text.label(u"fact.caves"_s), text.yesNo(terrain.caves), {}});

    Section ores{text.label(u"section.ores"_s), {}, {}, {}};
    for (const QcOreDefinition &ore : context.content.worldgen().ores()) {
        ores.links.append(links.oreLink(ore, text.heightRange(ore.minY, ore.maxY)));
    }
    ores.links = QcHandbookLinks::sortedLinks(std::move(ores.links));
    entry.sections.append(std::move(ores));

    Section biomes{text.label(u"section.biomes"_s), {}, {}, {}};
    for (const QcBiomeDefinition &biome : context.content.worldgen().biomes()) {
        biomes.links.append(links.biomeLink(biome));
    }
    biomes.links = QcHandbookLinks::sortedLinks(std::move(biomes.links));
    entry.sections.append(std::move(biomes));
    entry.englishTitle = context.content.translations().translate(u"handbook.world.terrain"_s, QcHandbookText::english());
    return entry;
}

QcHandbookEntry orePage(const QcHandbookContext &context, const QcOreDefinition &ore)
{
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;
    QcHandbookEntry entry;
    entry.id = u"ore:"_s + ore.name;
    entry.name = ore.name;
    entry.category = u"worldgen"_s;
    entry.pack = QcHandbookLinks::packOf(ore.name);
    entry.title = links.oreTitle(ore, text.language());
    entry.icon = links.itemIcon(ore.block);

    entry.facts.append({text.label(u"fact.id"_s), ore.name, {}});
    entry.facts.append({text.label(u"fact.block"_s), {}, {links.blockLink(ore.block)}});
    const QString replaces = ore.replaces.isEmpty() ? context.content.worldgen().terrainSettings().stoneBlock : ore.replaces;
    if (context.content.blockId(replaces) == qcAirBlockId) {
        // Veins in air hang in caves (cobwebs); air has no page.
        entry.facts.append({text.label(u"fact.replaces"_s), text.label(u"value.caveAir"_s), {}});
    } else if (!replaces.isEmpty()) {
        entry.facts.append({text.label(u"fact.replaces"_s), {}, {links.blockLink(replaces)}});
    }
    entry.facts.append({text.label(u"fact.heights"_s), text.heightRange(ore.minY, ore.maxY), {}});
    entry.facts.append({text.label(u"fact.veins"_s), text.label(u"value.veins"_s).arg(ore.veinsPerChunk).arg(ore.veinSize), {}});
    entry.englishTitle = links.oreTitle(ore, QcHandbookText::english());
    return entry;
}

// Type, blocks, size and where a feature may stand.
[[nodiscard]] QList<Fact> shapeFacts(const QcHandbookContext &context, const QcFeatureDefinition &feature)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    static constexpr std::array<const char16_t *, 4> types{u"tree", u"column", u"plant", u"pool"};
    const QString kind = feature.type == QcFeatureDefinition::Type::Pool && feature.surface
                             ? u"surfacePool"_s
                             : QString::fromUtf16(types[static_cast<std::size_t>(feature.type)]);
    facts.append({text.label(u"fact.featureType"_s), text.label(u"value.feature."_s + kind), {}});

    Fact blocks{text.label(u"fact.blocks"_s), {}, {}};
    for (const QString &block : {feature.trunkBlock, feature.leavesBlock, feature.rimBlock}) {
        if (!block.isEmpty()) {
            blocks.links.append(context.links.blockLink(block));
        }
    }
    facts.append(std::move(blocks));

    if (feature.type == QcFeatureDefinition::Type::Pool) {
        if (!feature.surface) {
            facts.append({text.label(u"fact.heights"_s), text.heightRange(feature.minY, feature.maxY), {}});
        }
        // Lakes on the surface are planned per square of chunks (decision E-856).
        const QString chance = feature.surface
                                   ? text.label(u"value.lake"_s).arg(feature.region).arg(feature.attempts).arg(text.percent(feature.chance))
                                   : text.label(u"value.pool"_s).arg(feature.attempts).arg(text.percent(feature.chance));
        facts.append({text.label(u"fact.poolChance"_s), chance, {}});
        facts.append({text.label(u"fact.poolSize"_s), text.label(u"value.poolSize"_s).arg(feature.radius).arg(feature.depth), {}});
    } else {
        facts.append({text.label(u"fact.featureHeight"_s),
                      text.label(u"unit.blockRange"_s).arg(feature.minHeight).arg(feature.maxHeight), {}});
    }

    if (!feature.groundBlocks.isEmpty()) {
        Fact ground{text.label(u"fact.ground"_s), {}, {}};
        for (const QString &block : feature.groundBlocks) {
            ground.links.append(context.links.blockLink(block));
        }
        facts.append(std::move(ground));
    }
    if (feature.onWater) {
        facts.append({text.label(u"fact.onWater"_s), text.yesNo(true), {}});
    }
    if (feature.nearWater) {
        facts.append({text.label(u"fact.nearWater"_s), text.yesNo(true), {}});
    }
    return facts;
}

// The biomes placing a feature (pools: every chunk unless they list their biomes).
Section featureBiomes(const QcHandbookContext &context, const QcFeatureDefinition &feature)
{
    const QcHandbookText &text = context.text;
    QcLinkList placing;
    for (const QcBiomeDefinition &biome : context.content.worldgen().biomes()) {
        for (const QcFeaturePlacement &placement : biome.features) {
            if (placement.feature == feature.name) {
                placing.addUnique(context.links.biomeLink(biome, perColumn(text, placement)));
            }
        }
    }

    Section biomes{text.label(u"section.inBiomes"_s), {}, {}, {}};
    if (feature.type == QcFeatureDefinition::Type::Pool) {
        for (const QcBiomeDefinition &biome : context.content.worldgen().biomes()) {
            if (feature.biomes.contains(biome.name)) {
                placing.addUnique(context.links.biomeLink(biome));
            }
        }
        const QString inBiomes = feature.surface ? u"value.lakeBiomes"_s : u"value.poolBiomes"_s;
        biomes.text = feature.biomes.isEmpty() ? text.label(u"value.everyChunk"_s) : text.label(inBiomes);
    }
    biomes.links = placing.sorted();
    return biomes;
}

QcHandbookEntry featurePage(const QcHandbookContext &context, const QcFeatureDefinition &feature)
{
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;
    QcHandbookEntry entry;
    entry.id = u"feature:"_s + feature.name;
    entry.name = feature.name;
    entry.category = u"worldgen"_s;
    entry.pack = QcHandbookLinks::packOf(feature.name);
    entry.title = links.featureTitle(feature, text.language());
    entry.icon = links.itemIcon(feature.trunkBlock);

    entry.facts.append({text.label(u"fact.id"_s), feature.name, {}});
    entry.facts += shapeFacts(context, feature);

    Section biomes = featureBiomes(context, feature);
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (block.sapling.feature == feature.name) {
            entry.facts.append({text.label(u"fact.grownFrom"_s), {}, {links.blockLink(block.name)}});
        }
    }

    if (!biomes.links.isEmpty() || !biomes.text.isEmpty()) {
        entry.sections.append(std::move(biomes));
    }
    entry.englishTitle = links.featureTitle(feature, QcHandbookText::english());
    return entry;
}

} // namespace

namespace QcHandbookWorldPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context)
{
    QList<QcHandbookEntry> entries;
    for (const QcBiomeDefinition &biome : context.content.worldgen().biomes()) {
        entries.append(biomePage(context, biome));
    }
    if (!context.content.worldgen().terrainSettings().stoneBlock.isEmpty()) {
        entries.append(terrainPage(context));
    }
    for (const QcOreDefinition &ore : context.content.worldgen().ores()) {
        entries.append(orePage(context, ore));
    }
    for (const QcFeatureDefinition &feature : context.content.worldgen().features()) {
        entries.append(featurePage(context, feature));
    }
    return entries;
}

} // namespace QcHandbookWorldPages
