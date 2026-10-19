#include "common/handbook/builder/QcHandbookIndex.h"

#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"

#include <QSet>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

QcHandbookIndex::QcHandbookIndex(const QcContentRegistry &content, QcHandbookText &text, const QcHandbookLinks &links)
    : m_content(content)
{
    indexRecipes(text, links);
    indexBlocks(text, links);
    indexMobs(text, links);
    indexOccurrences(text, links);
}

void QcHandbookIndex::indexRecipes(QcHandbookText &text, const QcHandbookLinks &links)
{
    const QList<QcRecipeDefinition> &recipes = m_content.recipes();
    for (qsizetype i = 0; i < recipes.size(); ++i) {
        const QcRecipeDefinition &recipe = recipes[i];
        m_recipesByResult[recipe.resultItem].append(i);

        QStringList ingredients = recipe.ingredients;
        for (const auto &[key, item] : recipe.keys) {
            ingredients.append(item);
        }
        ingredients.removeDuplicates();

        for (const QString &ingredient : std::as_const(ingredients)) {
            if (!ingredient.startsWith(u'#')) {
                m_recipesByIngredient[ingredient].append(IngredientUse(i, QString()));
                continue;
            }

            const QString tag = ingredient.mid(1);
            if (!links.tagMembers().contains(tag)) {
                text.addProblem(u"recipe %1: no item has the tag %2"_s.arg(recipe.name, tag));
            }
            const QStringList members = links.tagMembers().value(tag);
            for (const QString &member : members) {
                m_recipesByIngredient[member].append(IngredientUse(i, tag));
            }
        }
    }

    const QList<QcSmeltingRecipe> &smelting = m_content.smeltingRecipes();
    for (qsizetype i = 0; i < smelting.size(); ++i) {
        m_smeltingByResult[smelting[i].resultItem].append(i);
        m_smeltingByInput[smelting[i].inputItem].append(i);
    }
}

void QcHandbookIndex::indexBlocks(const QcHandbookText &text, const QcHandbookLinks &links)
{
    const QList<QcBlockDefinition> &blocks = m_content.blocks();
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const QcBlockDefinition &block = blocks[i];
        if (block.renderLayer == QcRenderLayer::Invisible) {
            continue;
        }

        if (block.machine.isMachine()) {
            m_machinesByCategory[block.machine.category].append(i);
        }

        for (const QcItemDrop &drop : block.drops) {
            if (drop.item != block.name) {
                m_droppedBy[drop.item].addUnique(links.blockLink(block.name, text.dropNote(drop)));
            }
        }

        if (block.partOf.isEmpty()) {
            continue;
        }

        const QString material = block.partOf.section(u'/', 0, 0);
        const auto it = std::ranges::find(m_content.materials(), material, &QcMaterialDefinition::name);
        if (it != m_content.materials().end()) {
            m_partsByBase[it->block].append(block.name);
        }
    }
}

void QcHandbookIndex::indexMobs(const QcHandbookText &text, const QcHandbookLinks &links)
{
    for (const QcMobDefinition &mob : m_content.mobs()) {
        for (const QcItemDrop &drop : mob.drops) {
            m_droppedBy[drop.item].addUnique(links.mobLink(mob, text.dropNote(drop)));
        }

        for (const QString &block : mob.spawnBlocks) {
            m_spawnersOn[block].addUnique(links.mobLink(mob));
        }

        for (const QcMobDefinition::SpawnRule &rule : mob.extraSpawns) {
            for (const QString &block : rule.blocks) {
                m_spawnersOn[block].addUnique(links.mobLink(mob));
            }
        }
    }
}

// Where blocks occur: biome layers, features listed by biomes, ore veins and pools.
void QcHandbookIndex::indexOccurrences(const QcHandbookText &text, const QcHandbookLinks &links)
{
    for (const QcBiomeDefinition &biome : m_content.biomes()) {
        indexBiome(biome, text, links);
    }

    for (const QcOreDefinition &ore : m_content.ores()) {
        m_foundIn[ore.block].addUnique(links.oreLink(ore, text.heightRange(ore.minY, ore.maxY)));
    }

    for (const QcFeatureDefinition &feature : m_content.features()) {
        if (feature.type == QcFeatureDefinition::Type::Pool) {
            const Link link = links.featureLink(feature, text.heightRange(feature.minY, feature.maxY));
            m_foundIn[feature.trunkBlock].addUnique(link);
        }
    }
}

void QcHandbookIndex::indexBiome(const QcBiomeDefinition &biome, const QcHandbookText &text, const QcHandbookLinks &links)
{
    const std::array<std::pair<QString, QString>, 7> layers{{{biome.surfaceBlock, u"biome.surface"_s},
                                                             {biome.subsurfaceBlock, u"biome.subsurface"_s},
                                                             {biome.underwaterBlock, u"biome.underwater"_s},
                                                             {biome.beachBlock, u"biome.beach"_s},
                                                             {biome.steepBlock, u"biome.steep"_s},
                                                             {biome.frozenWaterBlock, u"biome.frozenWater"_s},
                                                             {biome.snowBlock, u"biome.snow"_s}}};

    for (const auto &[block, role] : layers) {
        if (!block.isEmpty()) {
            m_foundIn[block].addUnique(links.biomeLink(biome, text.label(u"fact."_s + role)));
        }
    }

    for (const QcFeaturePlacement &placement : biome.features) {
        const auto feature = std::ranges::find(m_content.features(), placement.feature, &QcFeatureDefinition::name);
        if (feature == m_content.features().end()) {
            continue;
        }

        for (const QString &block : {feature->trunkBlock, feature->leavesBlock}) {
            if (!block.isEmpty()) {
                m_foundIn[block].addUnique(links.biomeLink(biome, links.featureTitle(*feature, text.language())));
            }
        }
    }
}
