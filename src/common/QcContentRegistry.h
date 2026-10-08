#pragma once

#include "QcAppearanceDefinition.h"
#include "QcBlockDefinition.h"
#include "QcItemDefinition.h"
#include "QcMobDefinition.h"
#include "QcModelDefinition.h"
#include "QcPackReference.h"
#include "QcRecipeDefinition.h"
#include "QcResult.h"
#include "QcSmeltingRecipe.h"
#include "QcSoundDefinition.h"
#include "QcTextureDefinition.h"
#include "QcWorldgenDefinitions.h"

#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// Central catalogue of all content (blocks, textures, translations, world generation). Base game and
// mods register through the same functions; a later registration with the same name replaces the
// earlier one. Runtime block ids follow registration order; air is always id 0.
class QcContentRegistry
{
public:
    static constexpr auto airName = "qtcraft:air";
    static constexpr auto fallbackLanguage = "en";

    QcContentRegistry();

    // Registers a new block or replaces the definition of an existing one (keeping its id).
    QcResult<QcBlockId> registerBlock(QcBlockDefinition definition);
    void registerTexture(QcTextureDefinition definition);
    // Adds or overrides translations for one language.
    void addTranslations(const QString &language, const QHash<QString, QString> &entries);

    qsizetype blockCount() const { return m_blocks.size(); }
    bool isValidBlock(QcBlockId id) const { return qsizetype(id) < m_blocks.size(); }
    // Precondition: isValidBlock(id).
    const QcBlockDefinition &block(QcBlockId id) const;
    std::optional<QcBlockId> blockId(const QString &name) const;
    const QList<QcBlockDefinition> &blocks() const { return m_blocks; }

    // Registers a new item or replaces an existing one (keeping its id). Ids start at 1.
    QcResult<QcItemId> registerItem(QcItemDefinition definition);
    // Gives every visible, non-liquid block without an item of the same name an item that places it.
    void createBlockItems();
    qsizetype itemCount() const { return m_items.size(); }
    bool isValidItem(QcItemId id) const { return id != qcNoItem && qsizetype(id) <= m_items.size(); }
    // Precondition: isValidItem(id).
    const QcItemDefinition &item(QcItemId id) const;
    std::optional<QcItemId> itemId(const QString &name) const;
    const QList<QcItemDefinition> &items() const { return m_items; }

    const QList<QcTextureDefinition> &textures() const { return m_textures; }
    const QcTextureDefinition *texture(const QString &name) const;

    // Registers or replaces a mob type; mob type ids are indices into mobs().
    void registerMob(QcMobDefinition definition);
    const QList<QcMobDefinition> &mobs() const { return m_mobs; }
    std::optional<int> mobIndex(const QString &name) const;

    // Skeleton models and appearances (outfits, hair styles); replaced by name like mobs.
    void registerModel(QcModelDefinition definition);
    const QList<QcModelDefinition> &models() const { return m_models; }
    const QcModelDefinition *model(const QString &name) const;
    void registerAppearance(QcAppearanceDefinition definition);
    const QList<QcAppearanceDefinition> &appearances() const { return m_appearances; }
    const QcAppearanceDefinition *appearance(const QString &name) const;

    void registerRecipe(QcRecipeDefinition definition);
    const QList<QcRecipeDefinition> &recipes() const { return m_recipes; }
    std::optional<qsizetype> recipeIndex(const QString &name) const;

    // Smelting recipes, replaced by name like crafting recipes.
    void registerSmeltingRecipe(QcSmeltingRecipe recipe);
    const QList<QcSmeltingRecipe> &smeltingRecipes() const { return m_smeltingRecipes; }
    // The first recipe of `category` that takes `inputItem`, or null.
    const QcSmeltingRecipe *smeltingRecipe(const QString &category, const QString &inputItem) const;

    // Sound definitions, replaced by name.
    void registerSound(QcSoundDefinition definition);
    const QList<QcSoundDefinition> &sounds() const { return m_sounds; }
    const QcSoundDefinition *sound(const QString &name) const;

    void registerBiome(QcBiomeDefinition definition);
    void registerFeature(QcFeatureDefinition definition);
    void registerOre(QcOreDefinition definition);
    void setTerrainSettings(QcTerrainSettings settings) { m_terrain = std::move(settings); }
    const QList<QcBiomeDefinition> &biomes() const { return m_biomes; }
    const QList<QcFeatureDefinition> &features() const { return m_features; }
    const QList<QcOreDefinition> &ores() const { return m_ores; }
    const QcTerrainSettings &terrainSettings() const { return m_terrain; }

    // Packs in load order (base game first).
    void addPack(QcPackReference pack) { m_packs.append(std::move(pack)); }
    const QList<QcPackReference> &packs() const { return m_packs; }

    QStringList languages() const { return m_translations.keys(); }
    qsizetype translationCount(const QString &language) const { return m_translations.value(language).size(); }
    // Falls back to English and finally to the key itself.
    QString translate(const QString &key, const QString &language) const;

    // Wire format used by the content sync message. World-generation data stays on the server.
    void writeTo(QDataStream &stream) const;
    static QcResult<QcContentRegistry> readFrom(QDataStream &stream);

private:
    QList<QcBlockDefinition> m_blocks;
    QHash<QString, QcBlockId> m_blockIds;
    QList<QcItemDefinition> m_items;
    QHash<QString, QcItemId> m_itemIds;
    QList<QcTextureDefinition> m_textures;
    QHash<QString, qsizetype> m_textureIndex;
    QHash<QString, QHash<QString, QString>> m_translations;
    QList<QcMobDefinition> m_mobs;
    QHash<QString, qsizetype> m_mobIndex;
    QList<QcModelDefinition> m_models;
    QHash<QString, qsizetype> m_modelIndex;
    QList<QcAppearanceDefinition> m_appearances;
    QHash<QString, qsizetype> m_appearanceIndex;
    QList<QcRecipeDefinition> m_recipes;
    QHash<QString, qsizetype> m_recipeIndex;
    QList<QcSmeltingRecipe> m_smeltingRecipes;
    QHash<QString, qsizetype> m_smeltingIndex;
    QList<QcSoundDefinition> m_sounds;
    QHash<QString, qsizetype> m_soundIndex;
    QList<QcBiomeDefinition> m_biomes;
    QHash<QString, qsizetype> m_biomeIndex;
    QList<QcFeatureDefinition> m_features;
    QHash<QString, qsizetype> m_featureIndex;
    QList<QcOreDefinition> m_ores;
    QHash<QString, qsizetype> m_oreIndex;
    QcTerrainSettings m_terrain;
    QList<QcPackReference> m_packs;
};
