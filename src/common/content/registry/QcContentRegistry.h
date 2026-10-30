// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/blocks/QcBlockStateTable.h"
#include "common/content/blocks/QcMaterialDefinition.h"
#include "common/content/blocks/QcPartFamily.h"
#include "common/content/items/QcEffectDefinition.h"
#include "common/content/items/QcEnchantmentDefinition.h"
#include "common/content/items/QcHoldDisplay.h"
#include "common/content/items/QcItemDefinition.h"
#include "common/content/mobs/QcMobDefinition.h"
#include "common/content/recipes/QcLootTables.h"
#include "common/content/recipes/QcRecipeDefinition.h"
#include "common/content/recipes/QcSmeltingRecipe.h"
#include "common/content/registry/QcAdvancementCatalog.h"
#include "common/content/registry/QcAssetCatalog.h"
#include "common/content/registry/QcCommandMacroDefinition.h"
#include "common/content/registry/QcNamedList.h"
#include "common/content/registry/QcPackReference.h"
#include "common/content/registry/QcTranslations.h"
#include "common/content/villages/QcVillageCatalog.h"
#include "common/content/worldgen/QcWeatherDefinition.h"
#include "common/content/worldgen/QcWorldgenCatalog.h"
#include "common/core/QcResult.h"
#include "common/handbook/QcHandbookChapter.h"

#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

/*! \brief Central catalogue of all content (blocks, items, creatures, recipes, effects); assets,
    world generation, villages and translations are catalogues of their own that it owns (decision
    E-715).

    Base game and mods register through the same functions; a later registration with the same
    name replaces the earlier one. Runtime block ids are block states (QcBlockStateTable) in
    registration order; air is always id 0, and a block's name resolves to its default state. */
class QcContentRegistry
{
public:
    /*! The name of the air block, the block with id 0. */
    static constexpr auto airName = "qtcraft:air";

    QcContentRegistry();

    /*! Registers the block \a definition or replaces the definition of an existing one of the same
        name; returns its default state. Replacing a block with a different number of states
        renumbers the states of later blocks. Fails for an invalid name, for air, for an invalid
        state or light opacity, and when the state table or its box budget would overflow. */
    [[nodiscard]] QcResult<QcBlockId> registerBlock(QcBlockDefinition definition);
    /*! Registers or replaces the named \a shape; blocks that refer to it pick it up. Fails when the
        shape has too many boxes or its blocks would exceed the state table's budget. */
    [[nodiscard]] QcStatus registerShape(QcShapeDefinition shape);
    /*! Returns the named shapes in registration order. */
    const QList<QcShapeDefinition> &shapes() const { return m_shapes; }
    /*! Returns the shape named \a name, or null if there is none. */
    const QcShapeDefinition *shape(const QString &name) const;
    /*! Returns the shape \a block uses: its named shape when registered, else its inline shape. */
    const QcShapeDefinition &shapeOf(const QcBlockDefinition &block) const;

    /*! Number of block definitions; runtime ids range over stateCount() states. */
    qsizetype blockCount() const { return m_blocks.size(); }
    /*! Returns the number of block states, the range of runtime block ids. */
    qsizetype stateCount() const { return m_states.size(); }
    /*! Returns whether \a id is a known block state. */
    bool isValidBlock(QcBlockId id) const { return static_cast<qsizetype>(id) < m_states.size(); }
    /*! Returns the definition of the block state \a id. Precondition: isValidBlock(id). */
    const QcBlockDefinition &block(QcBlockId id) const;
    /*! Returns the block and state index of the block state \a id. Precondition:
        isValidBlock(id). */
    const QcBlockState &state(QcBlockId id) const;
    /*! Returns the default state of the block named \a name; none for an unknown name. */
    [[nodiscard]] std::optional<QcBlockId> blockId(const QString &name) const;
    /*! Returns the fire a spark lights and burning bodies show: the first block with "fire":
        true (qtcraft:fire in the base game; decision E-392). */
    [[nodiscard]] std::optional<QcBlockId> defaultFire() const;
    /*! Returns the block definitions in registration order (air first). */
    const QList<QcBlockDefinition> &blocks() const { return m_blocks; }
    /*! Returns the table of all block states. */
    const QcBlockStateTable &stateTable() const { return m_states; }
    /*! Returns whether the states \a a and \a b belong to the same block definition. */
    bool isSameBlock(QcBlockId a, QcBlockId b) const { return state(a).type == state(b).type; }
    /*! Returns the value of \a property in the state \a id, empty when the block has no such
        property. */
    QString property(QcBlockId id, const QString &property) const;
    /*! Returns the state \a id with \a property set to \a value; \a id itself for unknown
        properties or values. */
    QcBlockId withProperty(QcBlockId id, const QString &property, const QString &value) const;
    /*! Returns the name of the state \a id: "name" or "name[a=x,b=y]" with every property, as
        stored in saves. */
    QString stateName(QcBlockId id) const;
    /*! \brief A parsed state name: the state (std::nullopt for an unknown block) and what was
        wrong with it. */
    struct ParsedState
    {
        /*! The state; none for an unknown block. */
        std::optional<QcBlockId> id;
        /*! What was wrong with the name, one text per problem. */
        QStringList problems;
    };
    /*! Returns the state named by \a text ("name" or "name[a=x,b=y]"). Unknown properties or
        values keep their defaults and are described in \c {problems}, as is a missing closing
        bracket. */
    [[nodiscard]] ParsedState parseState(const QString &text) const;
    /*! Returns the state named by \a text, as parseState() without the problems. */
    [[nodiscard]] std::optional<QcBlockId> stateId(const QString &text) const { return parseState(text).id; }

    /*! Registers the item \a definition or replaces an existing one of the same name (keeping its
        id) and returns its id. Ids start at 1. Fails for an invalid name or a maxStack outside
        1..QcItemDefinition::maxStackLimit. */
    [[nodiscard]] QcResult<QcItemId> registerItem(QcItemDefinition definition);
    /*! Gives every visible, non-liquid block that has an item (\c {hasItem}) but no item of the
        same name yet an item that places it. */
    void createBlockItems();
    /*! Completes the content after all packs: generates building parts (QcPartGenerator), takes
        circuit blocks with broken links out of circuits, creates the block items and the
        generated stonecutting recipes, and resolves what definitions name of each other (anvil
        swaps, villagers, archers, held items, creature traits, spawners, projectiles, village
        styles, outfits, advancements), leaving out or switching off what names nothing. Returns
        the warnings. */
    QStringList finish();

    /*! Registers or replaces the building part \a family (server side; the parts finish()
        generates are ordinary content). */
    void registerPartFamily(QcPartFamily family);
    /*! Registers or replaces the building \a material (server side; the parts finish() generates
        are ordinary content). */
    void registerMaterial(QcMaterialDefinition material);
    /*! Returns the building part families in registration order. */
    const QList<QcPartFamily> &partFamilies() const { return m_partFamilies.items(); }
    /*! Returns the building materials in registration order. */
    const QList<QcMaterialDefinition> &materials() const { return m_materials.items(); }
    /*! Returns the number of items. */
    qsizetype itemCount() const { return m_items.size(); }
    /*! Returns whether \a id names a registered item (1..itemCount()). */
    bool isValidItem(QcItemId id) const { return id != qcNoItem && static_cast<qsizetype>(id) <= m_items.size(); }
    /*! Returns the definition of the item \a id. Precondition: isValidItem(id). */
    const QcItemDefinition &item(QcItemId id) const;
    /*! Returns the id of the item named \a name; none for an unknown name. */
    [[nodiscard]] std::optional<QcItemId> itemId(const QString &name) const;
    /*! Returns the item definitions in id order (the item with id 1 first). */
    const QList<QcItemDefinition> &items() const { return m_items; }

    /*! Returns the textures, skeleton models, appearances and sounds (QcAssetCatalog). */
    QcAssetCatalog &assets() { return m_assets; }
    /*! Returns the textures, skeleton models, appearances and sounds (QcAssetCatalog). */
    const QcAssetCatalog &assets() const { return m_assets; }

    /*! Registers or replaces the mob type \a definition; mob type ids are indices into mobs(). */
    void registerMob(QcMobDefinition definition);
    /*! Returns the mob types in registration order. */
    const QList<QcMobDefinition> &mobs() const { return m_mobs.items(); }
    /*! Returns the mob type id of the creature kind named \a name; none for an unknown name. */
    [[nodiscard]] std::optional<int> mobIndex(const QString &name) const;

    /*! Registers or replaces the crafting recipe \a definition by name. */
    void registerRecipe(QcRecipeDefinition definition);
    /*! Returns the crafting recipes in registration order. */
    const QList<QcRecipeDefinition> &recipes() const { return m_recipes.items(); }
    /*! Returns the position in recipes() of the recipe named \a name; none for an unknown name. */
    [[nodiscard]] std::optional<qsizetype> recipeIndex(const QString &name) const;

    /*! Registers the smelting \a recipe, replacing one of the same name like crafting recipes. */
    void registerSmeltingRecipe(QcSmeltingRecipe recipe);
    /*! Returns the smelting recipes in registration order. */
    const QList<QcSmeltingRecipe> &smeltingRecipes() const { return m_smeltingRecipes.items(); }
    /*! Returns the first recipe of \a category that takes \a inputItem, or null. */
    const QcSmeltingRecipe *smeltingRecipe(const QString &category, const QString &inputItem) const;
    /*! Registers the stonecutting \a recipe (decision E-784), from data or generated for building
        parts (QcStonecutting), replacing one of the same name. */
    void registerStonecuttingRecipe(QcSmeltingRecipe recipe);
    /*! Returns the stonecutting recipes in registration order. */
    const QList<QcSmeltingRecipe> &stonecuttingRecipes() const { return m_stonecuttingRecipes.items(); }
    /*! Returns the position in stonecuttingRecipes() of the recipe named \a name; none for an
        unknown name. */
    [[nodiscard]] std::optional<qsizetype> stonecuttingIndex(const QString &name) const;

    /*! Registers the handbook \a chapter about playing, replacing one of the same name (server
        side only, not synchronised). */
    void registerHandbookChapter(QcHandbookChapter chapter);
    /*! Returns the handbook chapters in registration order. */
    const QList<QcHandbookChapter> &handbookChapters() const { return m_handbookChapters.items(); }
    /*! Registers the command \a macro of a pack, replacing one of the same name (server side only,
        not synchronised). */
    void registerCommandMacro(QcCommandMacroDefinition macro);
    /*! Returns the command macros in registration order. */
    const QList<QcCommandMacroDefinition> &commandMacros() const { return m_commandMacros.items(); }
    /*! Returns the command macro named \a name, or null if there is none. */
    const QcCommandMacroDefinition *commandMacro(const QString &name) const;
    /*! Sets what the weather does to the world to \a weather (server side only); a later pack
        replaces it. */
    void setWeather(QcWeatherDefinition weather) { m_weather = std::move(weather); }
    /*! Returns what the weather does to the world. */
    const QcWeatherDefinition &weather() const { return m_weather; }
    /*! Merges \a later into how items are held by category (decision E-831); a later pack
        overrides the fields it sets. They reach the client. */
    void mergeHoldDefaults(const QcHoldDefaults &later) { m_holdDefaults = m_holdDefaults.merged(later); }
    /*! Returns how items are held by category. */
    const QcHoldDefaults &holdDefaults() const { return m_holdDefaults; }

    /*! Returns the biomes, features, ores, terrain, structures and world type settings (server
        side only). */
    QcWorldgenCatalog &worldgen() { return m_worldgen; }
    /*! Returns the biomes, features, ores, terrain, structures and world type settings (server
        side only). */
    const QcWorldgenCatalog &worldgen() const { return m_worldgen; }
    /*! Returns the village styles and settings, villager species, professions and their offers
        (server side only). */
    QcVillageCatalog &villageCatalog() { return m_villages; }
    /*! Returns the village styles and settings, villager species, professions and their offers
        (server side only). */
    const QcVillageCatalog &villageCatalog() const { return m_villages; }
    /*! Returns the creature kind living in villages of \a style (its own or the settings'), when
        it is a villager (decision E-421). */
    std::optional<int> villagerOfStyle(const QString &style) const;
    /*! Returns the settings of the creature kind \a mobType (QcMobDefinition::villagerSettings,
        by the index finish() keeps on the creature); empty settings for a kind without them. */
    const QcVillagerSettings &villagerSettingsOf(int mobType) const;

    /*! Registers the \a projectile kind (decision E-425, server side only), replacing one of the
        same name. */
    void registerProjectile(QcProjectileDefinition projectile);
    /*! Returns the projectile kinds in registration order. */
    const QList<QcProjectileDefinition> &projectiles() const { return m_projectiles.items(); }
    /*! Returns the projectile kind named \a name, or null if there is none. */
    const QcProjectileDefinition *projectile(const QString &name) const;
    /*! Registers the status \a effect kind (decision E-450), replacing one of the same name; they
        reach the client. */
    void registerEffect(QcEffectDefinition effect);
    /*! Returns the status effect kinds in registration order. */
    const QList<QcEffectDefinition> &effects() const { return m_effects.items(); }
    /*! Returns the status effect kind named \a name, or null if there is none. */
    const QcEffectDefinition *effect(const QString &name) const;
    /*! Registers the \a enchantment (decision E-864), replacing one of the same name; they reach
        the client. */
    void registerEnchantment(QcEnchantmentDefinition enchantment) { m_enchantments.set(std::move(enchantment)); }
    /*! Returns the enchantments in registration order. */
    const QList<QcEnchantmentDefinition> &enchantments() const { return m_enchantments.items(); }
    /*! Returns the enchantment named \a name, or null if there is none. */
    const QcEnchantmentDefinition *enchantment(const QString &name) const { return m_enchantments.find(name); }
    /*! Returns the advancements of the adventure guide (decision E-923), replaced or switched off
        by name; they reach the client without the triggers of their criteria. */
    QcAdvancementCatalog &advancements() { return m_advancements; }
    /*! Returns the advancements of the adventure guide (decision E-923). */
    const QcAdvancementCatalog &advancements() const { return m_advancements; }
    /*! Returns the loot tables of generated chests (decision E-521, server side only), replaced by
        name. */
    QcLootTables &lootTables() { return m_lootTables; }
    /*! Returns the loot tables of generated chests (decision E-521, server side only). */
    const QcLootTables &lootTables() const { return m_lootTables; }

    /*! Appends \a pack to the packs in load order (base game first). */
    void addPack(QcPackReference pack) { m_packs.append(std::move(pack)); }
    /*! Returns the packs in load order (base game first). */
    const QList<QcPackReference> &packs() const { return m_packs; }

    /*! Returns the texts of the content by language (QcTranslations). */
    QcTranslations &translations() { return m_translations; }
    /*! Returns the texts of the content by language (QcTranslations). */
    const QcTranslations &translations() const { return m_translations; }

    /*! Writes the content to \a stream in the wire format of the content sync message
        (QcContentSerializer::write()). */
    void writeTo(QDataStream &stream) const;
    /*! Returns the content read from \a stream in the wire format of the content sync message
        (QcContentSerializer::read()), or an error for a corrupt stream. */
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
    // Gives the items their anvil swaps (QcAnvilSwaps, finish); the warnings.
    QStringList applyAnvilSwaps();

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
    QcHoldDefaults m_holdDefaults;
    QcWorldgenCatalog m_worldgen;
    QcVillageCatalog m_villages;
    QcNamedList<QcProjectileDefinition> m_projectiles;
    QcNamedList<QcEffectDefinition> m_effects;
    QcNamedList<QcEnchantmentDefinition> m_enchantments;
    QcLootTables m_lootTables;
    QcAdvancementCatalog m_advancements;
    QcNamedList<QcHandbookChapter> m_handbookChapters;
    QList<QcPackReference> m_packs;
    QcNamedList<QcPartFamily> m_partFamilies;
    QcNamedList<QcMaterialDefinition> m_materials;
};
