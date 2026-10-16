#pragma once

#include "QcAppearanceDefinition.h"
#include "QcBlockDefinition.h"
#include "QcBlockStateTable.h"
#include "QcHandbookChapter.h"
#include "QcItemDefinition.h"
#include "QcMaterialDefinition.h"
#include "QcPartFamily.h"
#include "QcMobDefinition.h"
#include "QcModelDefinition.h"
#include "QcPackReference.h"
#include "QcRecipeDefinition.h"
#include "QcResult.h"
#include "QcShowcaseDefinition.h"
#include "QcSmeltingRecipe.h"
#include "QcSoundDefinition.h"
#include "QcStructureDefinition.h"
#include "QcCommandMacroDefinition.h"
#include "QcTextureDefinition.h"
#include "QcWeatherDefinition.h"
#include "QcWorldgenDefinitions.h"
#include "QcWorldTypeDefinitions.h"

#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// Central catalogue of all content (blocks, textures, translations, world generation). Base game and
// mods register through the same functions; a later registration with the same name replaces the
// earlier one. Runtime block ids are block states (QcBlockStateTable) in registration order; air is
// always id 0, and a block's name resolves to its default state.
class QcContentRegistry
{
public:
    static constexpr auto airName = "qtcraft:air";
    static constexpr auto fallbackLanguage = "en";

    QcContentRegistry();

    // Registers a new block or replaces the definition of an existing one; returns its default state.
    // Replacing a block with a different number of states renumbers the states of later blocks.
    [[nodiscard]] QcResult<QcBlockId> registerBlock(QcBlockDefinition definition);
    // Registers or replaces a named shape; blocks that refer to it pick it up.
    // Fails when the shape has too many boxes or its blocks would exceed the state table's budget.
    [[nodiscard]] QcStatus registerShape(QcShapeDefinition shape);
    const QList<QcShapeDefinition> &shapes() const { return m_shapes; }
    const QcShapeDefinition *shape(const QString &name) const;
    // The shape a block uses: its named shape when registered, else its inline shape.
    const QcShapeDefinition &shapeOf(const QcBlockDefinition &block) const;
    void registerTexture(QcTextureDefinition definition);
    // Adds or overrides translations for one language.
    void addTranslations(const QString &language, const QHash<QString, QString> &entries);

    // Number of block definitions; runtime ids range over stateCount() states.
    qsizetype blockCount() const { return m_blocks.size(); }
    qsizetype stateCount() const { return m_states.size(); }
    bool isValidBlock(QcBlockId id) const { return qsizetype(id) < m_states.size(); }
    // The definition of a block state. Precondition: isValidBlock(id).
    const QcBlockDefinition &block(QcBlockId id) const;
    const QcBlockState &state(QcBlockId id) const;
    // Default state of the named block.
    [[nodiscard]] std::optional<QcBlockId> blockId(const QString &name) const;
    // The fire a spark lights and burning bodies show: the first block with "fire": true
    // (qtcraft:fire in the base game; decision E-392).
    [[nodiscard]] std::optional<QcBlockId> defaultFire() const;
    const QList<QcBlockDefinition> &blocks() const { return m_blocks; }
    const QcBlockStateTable &stateTable() const { return m_states; }
    // Whether two states belong to the same block definition.
    bool isSameBlock(QcBlockId a, QcBlockId b) const { return state(a).type == state(b).type; }
    // Value of a state property, empty when the block has no such property.
    QString property(QcBlockId id, const QString &property) const;
    // The state with one property changed; `id` itself for unknown properties or values.
    QcBlockId withProperty(QcBlockId id, const QString &property, const QString &value) const;
    // "name" or "name[a=x,b=y]" with every property, as stored in saves.
    QString stateName(QcBlockId id) const;
    // Parses a state name. Unknown properties or values keep their defaults and are described in
    // `problems`, as does a missing closing bracket; std::nullopt for unknown blocks.
    [[nodiscard]] std::optional<QcBlockId> stateId(const QString &text, QStringList *problems = nullptr) const;

    // Registers a new item or replaces an existing one (keeping its id). Ids start at 1.
    [[nodiscard]] QcResult<QcItemId> registerItem(QcItemDefinition definition);
    // Gives every visible, non-liquid block without an item of the same name an item that places it.
    void createBlockItems();
    // Completes the content after all packs: generates building parts (QcPartGenerator), then block
    // items. Returns the generator's warnings.
    QStringList finish();

    // Building part families and materials (server side; their results are ordinary content).
    void registerPartFamily(QcPartFamily family);
    void registerMaterial(QcMaterialDefinition material);
    const QList<QcPartFamily> &partFamilies() const { return m_partFamilies; }
    const QList<QcMaterialDefinition> &materials() const { return m_materials; }
    qsizetype itemCount() const { return m_items.size(); }
    bool isValidItem(QcItemId id) const { return id != qcNoItem && qsizetype(id) <= m_items.size(); }
    // Precondition: isValidItem(id).
    const QcItemDefinition &item(QcItemId id) const;
    [[nodiscard]] std::optional<QcItemId> itemId(const QString &name) const;
    const QList<QcItemDefinition> &items() const { return m_items; }

    const QList<QcTextureDefinition> &textures() const { return m_textures; }
    const QcTextureDefinition *texture(const QString &name) const;

    // Registers or replaces a mob type; mob type ids are indices into mobs().
    void registerMob(QcMobDefinition definition);
    const QList<QcMobDefinition> &mobs() const { return m_mobs; }
    [[nodiscard]] std::optional<int> mobIndex(const QString &name) const;

    // Skeleton models and appearances (outfits, hair styles); replaced by name like mobs.
    void registerModel(QcModelDefinition definition);
    const QList<QcModelDefinition> &models() const { return m_models; }
    const QcModelDefinition *model(const QString &name) const;
    void registerAppearance(QcAppearanceDefinition definition);
    const QList<QcAppearanceDefinition> &appearances() const { return m_appearances; }
    const QcAppearanceDefinition *appearance(const QString &name) const;

    void registerRecipe(QcRecipeDefinition definition);
    const QList<QcRecipeDefinition> &recipes() const { return m_recipes; }
    [[nodiscard]] std::optional<qsizetype> recipeIndex(const QString &name) const;

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
    // Handbook chapters about playing, replaced by name (server side only, not synchronised).
    void registerHandbookChapter(QcHandbookChapter chapter);
    const QList<QcHandbookChapter> &handbookChapters() const { return m_handbookChapters; }
    const QList<QcBiomeDefinition> &biomes() const { return m_biomes; }
    // The biome named `name`; null when there is none.
    const QcBiomeDefinition *biome(const QString &name) const;
    const QList<QcFeatureDefinition> &features() const { return m_features; }
    const QList<QcOreDefinition> &ores() const { return m_ores; }
    const QcTerrainSettings &terrainSettings() const { return m_terrain; }
    // Structures (server side only, not synchronised), replaced by name.
    void registerStructure(QcStructureDefinition structure);
    const QList<QcStructureDefinition> &structures() const { return m_structures; }
    const QcStructureDefinition *structure(const QString &name) const;
    // Command macros of the packs (server side only, not synchronised), replaced by name.
    void registerCommandMacro(QcCommandMacroDefinition macro);
    const QList<QcCommandMacroDefinition> &commandMacros() const { return m_commandMacros; }
    const QcCommandMacroDefinition *commandMacro(const QString &name) const;
    // Settings of the showcase world (server side only); a later pack replaces them.
    void setShowcase(QcShowcaseDefinition showcase) { m_showcase = std::move(showcase); }
    const QcShowcaseDefinition &showcase() const { return m_showcase; }
    // Settings of the flat and void world types (server side only); a later pack replaces them.
    void setFlatWorld(QcFlatDefinition flat) { m_flatWorld = std::move(flat); }
    const QcFlatDefinition &flatWorld() const { return m_flatWorld; }
    void setVoidWorld(QcVoidDefinition empty) { m_voidWorld = std::move(empty); }
    const QcVoidDefinition &voidWorld() const { return m_voidWorld; }
    // What the weather does to the world (server side only); a later pack replaces it.
    void setWeather(QcWeatherDefinition weather) { m_weather = std::move(weather); }
    const QcWeatherDefinition &weather() const { return m_weather; }

    // Packs in load order (base game first).
    void addPack(QcPackReference pack) { m_packs.append(std::move(pack)); }
    const QList<QcPackReference> &packs() const { return m_packs; }

    QStringList languages() const { return m_translations.keys(); }
    qsizetype translationCount(const QString &language) const { return m_translations.value(language).size(); }
    // Entries of all languages together (bounded by QcStreamLimits::maxTranslations, decision E-371).
    qsizetype translationTotal() const;
    // Every key with a text in that language (no English fallback), sorted.
    QStringList translationKeys(const QString &language) const;
    // Falls back to English and finally to the key itself.
    QString translate(const QString &key, const QString &language) const;
    bool hasTranslation(const QString &key, const QString &language) const;

    // Wire format used by the content sync message (QcContentSerializer).
    void writeTo(QDataStream &stream) const;
    [[nodiscard]] static QcResult<QcContentRegistry> readFrom(QDataStream &stream);

private:
    // Fills the block table of a received registry directly.
    friend class QcContentSerializer;

    void rebuildStates();

    QList<QcBlockDefinition> m_blocks;
    // Block name -> index into m_blocks.
    QHash<QString, quint32> m_blockIds;
    QcBlockStateTable m_states;
    QList<QcShapeDefinition> m_shapes;
    QHash<QString, qsizetype> m_shapeIndex;
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
    QList<QcStructureDefinition> m_structures;
    QHash<QString, qsizetype> m_structureIndex;
    QList<QcCommandMacroDefinition> m_commandMacros;
    QHash<QString, qsizetype> m_commandMacroIndex;
    QcShowcaseDefinition m_showcase;
    QcFlatDefinition m_flatWorld;
    QcVoidDefinition m_voidWorld;
    QcWeatherDefinition m_weather;
    QList<QcHandbookChapter> m_handbookChapters;
    QHash<QString, qsizetype> m_handbookChapterIndex;
    QList<QcPackReference> m_packs;
    QList<QcPartFamily> m_partFamilies;
    QHash<QString, qsizetype> m_partFamilyIndex;
    QList<QcMaterialDefinition> m_materials;
    QHash<QString, qsizetype> m_materialIndex;
};
