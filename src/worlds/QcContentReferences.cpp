#include "worlds/QcContentReferences.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/content/parser/QcHangingParser.h"
#include "worldgen/generators/QcWorldOptions.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// An item name, or "#tag" carried by at least one item.
bool knowsItem(const QcContentRegistry &content, const QString &name)
{
    if (name.startsWith(u'#')) {
        return std::ranges::any_of(content.items(), [tag = name.mid(1)](const QcItemDefinition &definition) {
            return definition.tags.contains(tag);
        });
    }
    return content.itemId(name).has_value();
}

bool knowsTexture(const QcContentRegistry &content, const QString &name)
{
    return name.isEmpty() || content.assets().texture(name) != nullptr;
}

bool knowsSound(const QcContentRegistry &content, const QString &name)
{
    return name.isEmpty() || content.assets().sound(name) != nullptr;
}

[[nodiscard]] QStringList checkRecipeReferences(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcRecipeDefinition &recipe : content.recipes()) {
        QStringList names = recipe.ingredients;
        for (const auto &[key, name] : recipe.keys) {
            names.append(name);
        }
        names.append(recipe.resultItem);

        for (const QString &name : std::as_const(names)) {
            if (!knowsItem(content, name)) {
                warnings.append(u"recipe %1: unknown item '%2'"_s.arg(recipe.name, name));
            }
        }
    }

    for (const QcSmeltingRecipe &recipe : content.smeltingRecipes()) {
        for (const QString &name : {recipe.inputItem, recipe.resultItem}) {
            if (!knowsItem(content, name)) {
                warnings.append(u"smelting recipe %1: unknown item '%2'"_s.arg(recipe.name, name));
            }
        }

        // Only the first recipe per category and input counts; a later one is never used.
        const QcSmeltingRecipe *used = content.smeltingRecipe(recipe.category, recipe.inputItem);
        if (used && used->name != recipe.name) {
            warnings.append(u"smelting recipe %1 is never used: %2 already handles '%3' in category %4"_s.arg(
                recipe.name, used->name, recipe.inputItem, recipe.category));
        }
    }
    return warnings;
}

// A golem's repair item and blocks (decision E-471): unknown ones leave it unrepaired, unbuilt.
[[nodiscard]] QStringList checkGolemReferences(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QStringList warnings;
    if (!mob.golem.repairItem.isEmpty() && !knowsItem(content, mob.golem.repairItem)) {
        warnings.append(u"mob %1: unknown repair item '%2'"_s.arg(mob.name, mob.golem.repairItem));
    }
    for (const QString &block : {mob.golem.buildBody, mob.golem.buildHead}) {
        if (!block.isEmpty() && !content.blockId(block)) {
            warnings.append(u"mob %1: unknown building block '%2'"_s.arg(mob.name, block));
        }
    }
    return warnings;
}

[[nodiscard]] QStringList checkMobReferences(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QStringList warnings;
    for (const QcItemDrop &drop : mob.drops) {
        if (!knowsItem(content, drop.item)) {
            warnings.append(u"mob %1: unknown drop '%2'"_s.arg(mob.name, drop.item));
        }
    }

    for (const QcMobPart &part : mob.parts) {
        if (!knowsTexture(content, part.texture)) {
            warnings.append(u"mob %1: unknown texture '%2'"_s.arg(mob.name, part.texture));
        }
    }

    if (!mob.skeleton.isEmpty() && !content.assets().model(mob.skeleton)) {
        warnings.append(u"mob %1: unknown skeleton '%2'"_s.arg(mob.name, mob.skeleton));
    }

    const QcAppearanceDefinition *outfit = mob.outfit.isEmpty() ? nullptr : content.assets().appearance(mob.outfit);
    if (!mob.outfit.isEmpty() && (!outfit || outfit->kind != QcAppearanceDefinition::Kind::Outfit)) {
        warnings.append(u"mob %1: unknown outfit '%2'"_s.arg(mob.name, mob.outfit));
    }
    const QcAppearanceDefinition *cape = mob.cape.isEmpty() ? nullptr : content.assets().appearance(mob.cape);
    if (!mob.cape.isEmpty() && (!cape || cape->kind != QcAppearanceDefinition::Kind::Cape)) {
        warnings.append(u"mob %1: unknown cape '%2'"_s.arg(mob.name, mob.cape));
    }

    warnings.append(checkGolemReferences(content, mob));
    for (const QString &name : {mob.ambientSound, mob.hurtSound, mob.deathSound, mob.golem.attackSound, mob.golem.repairSound}) {
        if (!knowsSound(content, name)) {
            warnings.append(u"mob %1: unknown sound '%2'"_s.arg(mob.name, name));
        }
    }

    for (const QString &food : mob.breeding.food) {
        if (!content.itemId(food)) {
            warnings.append(u"mob %1: unknown breeding food '%2'"_s.arg(mob.name, food));
        }
    }
    return warnings;
}

[[nodiscard]] QStringList checkBlockReferences(const QcContentRegistry &content, const QcBlockDefinition &block)
{
    QStringList warnings;
    for (const QcItemDrop &drop : block.drops) {
        if (!knowsItem(content, drop.item)) {
            warnings.append(u"block %1: unknown drop '%2'"_s.arg(block.name, drop.item));
        }
    }

    // Silent at run time; the author should know.
    for (int action = 0; action < qcBlockSoundActionCount; ++action) {
        const QString name = QcSoundKeys::blockSound(block, static_cast<QcBlockSoundAction>(action));
        if (!knowsSound(content, name)) {
            warnings.append(u"block %1: unknown sound '%2'"_s.arg(block.name, name));
        }
    }

    for (const QString &name : {block.containerOpenSound, block.containerCloseSound}) {
        if (!knowsSound(content, name)) {
            warnings.append(u"block %1: unknown container sound '%2'"_s.arg(block.name, name));
        }
    }

    if (!knowsSound(content, block.machine.sound)) {
        warnings.append(u"block %1: unknown machine sound '%2'"_s.arg(block.name, block.machine.sound));
    }

    // Farming (decisions E-289 to E-291) and paths (E-406): such blocks never bear fruit, revert, get
    // tilled or flattened.
    for (const auto &[field, name] : {std::pair{u"fruit"_s, block.crop.fruit}, std::pair{u"soil revertsTo"_s, block.soil.revertsTo},
                                      std::pair{u"tillsInto"_s, block.tillsInto}, std::pair{u"flattensInto"_s, block.flattensInto},
                                      std::pair{u"coveredInto"_s, block.coveredInto}, std::pair{u"carving into"_s, block.carvesInto},
                                      std::pair{u"strippedInto"_s, block.strippedInto}}) {
        if (!name.isEmpty() && !content.blockId(name)) {
            warnings.append(u"block %1: unknown %2 '%3'"_s.arg(block.name, field, name));
        }
    }
    return warnings;
}

// Minecarts (decisions E-382, E-386): a carried block nothing defines makes a plain cart, and a
// structure's cart of an item that places no cart is left out.
[[nodiscard]] QStringList checkCartReferences(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcItemDefinition &definition : content.items()) {
        if (!definition.cartBlock.isEmpty() && !content.blockId(definition.cartBlock)) {
            warnings.append(u"item %1: carries unknown block '%2'"_s.arg(definition.name, definition.cartBlock));
        }
    }
    for (const QcStructureDefinition &structure : content.worldgen().structures()) {
        for (const QcStructureDefinition::Cart &cart : structure.minecarts) {
            const std::optional<QcItemId> item = content.itemId(cart.item);
            if (!item || !content.item(*item).placesCart) {
                warnings.append(u"structure %1: '%2' is no minecart"_s.arg(structure.name, cart.item));
            }
        }
    }
    return warnings;
}

} // namespace

namespace QcContentReferences {

QStringList check(const QcContentRegistry &content)
{
    QStringList warnings;
    warnings.append(checkRecipeReferences(content));

    for (const QcMobDefinition &mob : content.mobs()) {
        warnings.append(checkMobReferences(content, mob));
    }

    for (const QcAppearanceDefinition &appearance : content.assets().appearances()) {
        for (const QcAppearanceLayer &layer : appearance.layers) {
            if (!knowsTexture(content, layer.texture)) {
                warnings.append(u"appearance %1: unknown texture '%2'"_s.arg(appearance.name, layer.texture));
            }
        }
    }

    for (const QcBlockDefinition &block : content.blocks()) {
        warnings.append(checkBlockReferences(content, block));
    }
    warnings.append(checkCartReferences(content));

    for (const QcItemDefinition &definition : content.items()) {
        if (!knowsTexture(content, definition.texture)) {
            warnings.append(u"item %1: unknown texture '%2'"_s.arg(definition.name, definition.texture));
        }
        if (!definition.placesBlock.isEmpty() && !content.blockId(definition.placesBlock)) {
            warnings.append(u"item %1: places unknown block '%2'"_s.arg(definition.name, definition.placesBlock));
        }
        if (definition.hangs) {
            warnings.append(QcHangingParser::checkTextures(definition.name, *definition.hangs, content.assets()));
        }
    }
    // Options on unknown targets or subjects, presets with unknown options (decision E-917).
    warnings.append(QcWorldOptions::check(content));
    return warnings;
}

} // namespace QcContentReferences
