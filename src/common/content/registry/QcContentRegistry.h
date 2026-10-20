#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcCommandMacroDefinition.h"
#include "common/content/definitions/QcEffectDefinition.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/content/definitions/QcMaterialDefinition.h"
#include "common/content/definitions/QcMobDefinition.h"
#include "common/content/definitions/QcPackReference.h"
#include "common/content/definitions/QcPartFamily.h"
#include "common/content/definitions/QcRecipeDefinition.h"
#include "common/content/definitions/QcSmeltingRecipe.h"
#include "common/content/definitions/QcWeatherDefinition.h"
#include "common/content/registry/QcAssetCatalog.h"
#include "common/content/registry/QcBlockStateTable.h"
#include "common/content/registry/QcLootTables.h"
#include "common/content/registry/QcNamedList.h"
#include "common/content/registry/QcTranslations.h"
#include "common/content/registry/QcVillageCatalog.h"
#include "common/content/registry/QcWorldgenCatalog.h"
#include "common/core/QcResult.h"
#include "common/handbook/QcHandbookChapter.h"

#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// Central catalogue of all content (blocks, items, creatures, recipes, effects); assets, world
// generation, villages and translations are catalogues of their own that it owns (decision E-715).
// Base game and mods register through the same functions; a later registration with the same name
// replaces the earlier one. Runtime block ids are block states (QcBlockStateTable) in registration
// order; air is always id 0, and a block's name resolves to its default state.
class QcContentRegistry
{
public:
    static constexpr auto airName = "qtcraft:air";

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
    // A parsed state name: the state (std::nullopt for an unknown block) and what was wrong with it.
    struct ParsedState
    {
        std::optional<QcBlockId> id;
        QStringList problems;
    };
    // Parses a state name. Unknown properties or values keep their defaults and are described in
    // `problems`, as is a missing closing bracket.
    [[nodiscard]] ParsedState parseState(const QString &text) const;
    // The state of a state name, as parseState without the problems.
    [[nodiscard]] std::optional<QcBlockId> stateId(const QString &text) const { return parseState(text).id; }

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
    const QList<QcPartFamily> &partFamilies() const { return m_partFamilies.items(); }
    const QList<QcMaterialDefinition> &materials() const { return m_materials.items(); }
    qsizetype itemCount() const { return m_items.size(); }
    bool isValidItem(QcItemId id) const { return id != qcNoItem && qsizetype(id) <= m_items.size(); }
    // Precondition: isValidItem(id).
    const QcItemDefinition &item(QcItemId id) const;
    [[nodiscard]] std::optional<QcItemId> itemId(const QString &name) const;
    const QList<QcItemDefinition> &items() const { return m_items; }

    // Textures, skeleton models, appearances and sounds (QcAssetCatalog).
    QcAssetCatalog &assets() { return m_assets; }
    const QcAssetCatalog &assets() const { return m_assets; }

    // Registers or replaces a mob type; mob type ids are indices into mobs().
    void registerMob(QcMobDefinition definition);
    const QList<QcMobDefinition> &mobs() const { return m_mobs.items(); }
    [[nodiscard]] std::optional<int> mobIndex(const QString &name) const;

    void registerRecipe(QcRecipeDefinition definition);
    const QList<QcRecipeDefinition> &recipes() const { return m_recipes.items(); }
    [[nodiscard]] std::optional<qsizetype> recipeIndex(const QString &name) const;

    // Smelting recipes, replaced by name like crafting recipes.
    void registerSmeltingRecipe(QcSmeltingRecipe recipe);
    const QList<QcSmeltingRecipe> &smeltingRecipes() const { return m_smeltingRecipes.items(); }
    // The first recipe of `category` that takes `inputItem`, or null.
    const QcSmeltingRecipe *smeltingRecipe(const QString &category, const QString &inputItem) const;
    // Stonecutting recipes (decision E-784), from data and generated for building parts
    // (QcStonecutting), replaced by name.
    void registerStonecuttingRecipe(QcSmeltingRecipe recipe);
    const QList<QcSmeltingRecipe> &stonecuttingRecipes() const { return m_stonecuttingRecipes.items(); }
    [[nodiscard]] std::optional<qsizetype> stonecuttingIndex(const QString &name) const;

    // Handbook chapters about playing, replaced by name (server side only, not synchronised).
    void registerHandbookChapter(QcHandbookChapter chapter);
    const QList<QcHandbookChapter> &handbookChapters() const { return m_handbookChapters.items(); }
    // Command macros of the packs (server side only, not synchronised), replaced by name.
    void registerCommandMacro(QcCommandMacroDefinition macro);
    const QList<QcCommandMacroDefinition> &commandMacros() const { return m_commandMacros.items(); }
    const QcCommandMacroDefinition *commandMacro(const QString &name) const;
    // What the weather does to the world (server side only); a later pack replaces it.
    void setWeather(QcWeatherDefinition weather) { m_weather = std::move(weather); }
    const QcWeatherDefinition &weather() const { return m_weather; }

    // Biomes, features, ores, terrain, structures and world type settings (server side only).
    QcWorldgenCatalog &worldgen() { return m_worldgen; }
    const QcWorldgenCatalog &worldgen() const { return m_worldgen; }
    // Village styles and settings, villager species, professions and their offers (server side only).
    QcVillageCatalog &villageCatalog() { return m_villages; }
    const QcVillageCatalog &villageCatalog() const { return m_villages; }
    // The creature kind living in villages of `style` (its own or the settings'), when it is a
    // villager (decision E-421).
    std::optional<int> villagerOfStyle(const QString &style) const;
    // The settings of the creature kind `mobType` (QcMobDefinition::villagerSettings, by the index
    // finish() keeps on the creature); empty settings for a kind without them.
    const QcVillagerSettings &villagerSettingsOf(int mobType) const;

    // Kinds of projectiles (decision E-425, server side only), replaced by name.
    void registerProjectile(QcProjectileDefinition projectile);
    const QList<QcProjectileDefinition> &projectiles() const { return m_projectiles.items(); }
    const QcProjectileDefinition *projectile(const QString &name) const;
    // Kinds of status effects (decision E-450), replaced by name; they reach the client.
    void registerEffect(QcEffectDefinition effect);
    const QList<QcEffectDefinition> &effects() const { return m_effects.items(); }
    const QcEffectDefinition *effect(const QString &name) const;
    // Loot tables of generated chests (decision E-521, server side only), replaced by name.
    QcLootTables &lootTables() { return m_lootTables; }
    const QcLootTables &lootTables() const { return m_lootTables; }

    // Packs in load order (base game first).
    void addPack(QcPackReference pack) { m_packs.append(std::move(pack)); }
    const QList<QcPackReference> &packs() const { return m_packs; }

    // Texts of the content by language (QcTranslations).
    QcTranslations &translations() { return m_translations; }
    const QcTranslations &translations() const { return m_translations; }

    // Wire format used by the content sync message (QcContentSerializer).
    void writeTo(QDataStream &stream) const;
    [[nodiscard]] static QcResult<QcContentRegistry> readFrom(QDataStream &stream);

private:
    // Fills the block table of a received registry directly.
    friend class QcContentSerializer;

    void rebuildStates();
    // Replaces the block table with the one in the stream (QcContentSerializer.cpp, the wire format).
    [[nodiscard]] QcStatus readBlockTable(QDataStream &stream);
    // Checks that ammunition and projectile kinds name each other's kinds (finish): ammunition of
    // an unknown kind is none; the warnings.
    QStringList resolveProjectiles();

    QList<QcBlockDefinition> m_blocks;
    // Block name -> index into m_blocks.
    QHash<QString, quint32> m_blockIds;
    QcBlockStateTable m_states;
    QList<QcShapeDefinition> m_shapes;
    QHash<QString, qsizetype> m_shapeIndex;
    QList<QcItemDefinition> m_items;
    QHash<QString, QcItemId> m_itemIds;
    QcAssetCatalog m_assets;
    QcTranslations m_translations;
    QcNamedList<QcMobDefinition> m_mobs;
    QcNamedList<QcRecipeDefinition> m_recipes;
    QcNamedList<QcSmeltingRecipe> m_smeltingRecipes;
    QcNamedList<QcSmeltingRecipe> m_stonecuttingRecipes;
    QcNamedList<QcCommandMacroDefinition> m_commandMacros;
    QcWeatherDefinition m_weather;
    QcWorldgenCatalog m_worldgen;
    QcVillageCatalog m_villages;
    QcNamedList<QcProjectileDefinition> m_projectiles;
    QcNamedList<QcEffectDefinition> m_effects;
    QcLootTables m_lootTables;
    QcNamedList<QcHandbookChapter> m_handbookChapters;
    QList<QcPackReference> m_packs;
    QcNamedList<QcPartFamily> m_partFamilies;
    QcNamedList<QcMaterialDefinition> m_materials;
};
