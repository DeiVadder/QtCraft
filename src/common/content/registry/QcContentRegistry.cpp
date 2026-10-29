#include "common/content/registry/QcContentRegistry.h"

#include "common/content/registry/QcMobTraitsContent.h"
#include "common/content/registry/QcSpawnerContent.h"
#include "common/content/registry/QcSpawnWeights.h"
#include "common/content/registry/QcVillageContent.h"
#include "common/content/registry/QcVillagerOutfits.h"

#include "common/content/registry/QcAnvilSwaps.h"
#include "common/content/registry/QcContentNames.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/content/registry/QcPartGenerator.h"
#include "common/content/registry/QcSignalLinks.h"
#include "common/content/registry/QcStonecutting.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcContentRegistry::QcContentRegistry()
{
    // Air is the only block the core defines itself: chunk storage relies on it as the empty value.
    QcBlockDefinition air;
    air.name = QString::fromLatin1(airName);
    air.translationKey = u"block.qtcraft.air"_s;
    air.renderLayer = QcRenderLayer::Invisible;
    air.solid = false;
    air.opaque = false;
    air.replaceable = true;
    air.lightOpacity = 0;
    air.hardness = -1.0;

    m_blocks.append(air);
    m_blockIds.insert(air.name, 0);
    m_states.append(m_blocks.constFirst(), {});
}

QcResult<QcBlockId> QcContentRegistry::registerBlock(QcBlockDefinition definition)
{
    if (!QcContentNames::isValidQualified(definition.name)) {
        return qcError(u"Invalid block name '%1'"_s.arg(definition.name));
    }
    if (definition.name == QLatin1StringView(airName)) {
        return qcError(u"The air block cannot be redefined"_s);
    }
    if (definition.lightOpacity < 0 || definition.lightOpacity > 15) {
        return qcError(u"Block '%1': lightOpacity must be within 0..15"_s.arg(definition.name));
    }
    if (const QString problem = QcBlockStateTable::problem(definition); !problem.isEmpty()) {
        return qcError(u"Block '%1': %2"_s.arg(definition.name, problem));
    }

    const qsizetype states = QcBlockStateTable::stateCountOf(definition);
    const auto existing = m_blockIds.constFind(definition.name);
    const qsizetype boxes = QcBlockStateTable::derivedBoxes(definition, shapeOf(definition));
    if (!m_states.fits(existing != m_blockIds.constEnd() ? std::optional<quint32>(*existing) : std::nullopt, boxes)) {
        return qcError(u"Block '%1': its shape variants need %2 boxes, more than the %3 left for all block shapes together"_s
                           .arg(definition.name)
                           .arg(boxes)
                           .arg(QcBlockStateTable::maxDerivedBoxes - m_states.totalDerivedBoxes()));
    }

    if (existing != m_blockIds.constEnd()) {
        const quint32 type = *existing;
        const int previous = m_states.stateCount(type);
        if (m_states.size() - previous + states > QcBlockStateTable::maxStates) {
            return qcError(u"Block '%1': too many block states in total"_s.arg(definition.name));
        }

        m_blocks[type] = std::move(definition);
        if (states == previous) {
            m_states.update(type, m_blocks[type], shapeOf(m_blocks[type]));
        } else {
            rebuildStates();
        }
        return m_states.firstState(type);
    }

    if (m_states.size() + states > QcBlockStateTable::maxStates) {
        return qcError(u"Block '%1': too many block states in total"_s.arg(definition.name));
    }

    const auto type = static_cast<quint32>(m_blocks.size());
    m_blockIds.insert(definition.name, type);
    m_blocks.append(std::move(definition));
    m_states.append(m_blocks.constLast(), shapeOf(m_blocks.constLast()));
    return m_states.firstState(type);
}

void QcContentRegistry::rebuildStates()
{
    m_states.clear();
    for (const QcBlockDefinition &block : std::as_const(m_blocks)) {
        m_states.append(block, shapeOf(block));
    }
}

QcStatus QcContentRegistry::registerShape(QcShapeDefinition shape)
{
    const QString name = shape.name;
    if (shape.boxCount() > QcShapeDefinition::maxBoxes) {
        return qcError(u"Shape '%1': more than %2 boxes"_s.arg(name).arg(QcShapeDefinition::maxBoxes));
    }

    qsizetype total = m_states.totalDerivedBoxes();
    for (const QcBlockDefinition &block : std::as_const(m_blocks)) {
        if (block.shapeName == name) {
            total += QcBlockStateTable::derivedBoxes(block, shape) - QcBlockStateTable::derivedBoxes(block, shapeOf(block));
        }
    }
    if (total > QcBlockStateTable::maxDerivedBoxes) {
        return qcError(u"Shape '%1': the blocks using it would need more than %2 boxes for all block shapes together"_s.arg(name).arg(
            QcBlockStateTable::maxDerivedBoxes));
    }

    const auto existing = m_shapeIndex.constFind(name);
    if (existing != m_shapeIndex.constEnd()) {
        m_shapes[*existing] = std::move(shape);
    } else {
        m_shapeIndex.insert(name, m_shapes.size());
        m_shapes.append(std::move(shape));
    }

    for (qsizetype type = 0; type < m_blocks.size(); ++type) {
        if (m_blocks[type].shapeName == name) {
            m_states.update(static_cast<quint32>(type), m_blocks[type], shapeOf(m_blocks[type]));
        }
    }

    return {};
}

const QcShapeDefinition *QcContentRegistry::shape(const QString &name) const
{
    const auto it = m_shapeIndex.constFind(name);
    return it == m_shapeIndex.constEnd() ? nullptr : &m_shapes[*it];
}

const QcShapeDefinition &QcContentRegistry::shapeOf(const QcBlockDefinition &block) const
{
    if (!block.shapeName.isEmpty()) {
        if (const QcShapeDefinition *named = shape(block.shapeName)) {
            return *named;
        }
    }
    return block.shape;
}

const QcBlockState &QcContentRegistry::state(QcBlockId id) const
{
    Q_ASSERT(isValidBlock(id));
    return m_states.state(id);
}

QString QcContentRegistry::property(QcBlockId id, const QString &property) const
{
    const QcBlockState &info = state(id);
    return QcBlockStateTable::value(m_blocks[static_cast<qsizetype>(info.type)], info.index, property);
}

QcBlockId QcContentRegistry::withProperty(QcBlockId id, const QString &property, const QString &value) const
{
    const QcBlockState &info = state(id);
    const std::optional<quint32> index = QcBlockStateTable::withValue(m_blocks[static_cast<qsizetype>(info.type)], info.index, property, value);
    return index ? m_states.firstState(info.type) + *index : id;
}

QString QcContentRegistry::stateName(QcBlockId id) const
{
    const QcBlockState &info = state(id);
    const QcBlockDefinition &definition = m_blocks[static_cast<qsizetype>(info.type)];
    return definition.name + QcBlockStateTable::describe(definition, info.index);
}

QcContentRegistry::ParsedState QcContentRegistry::parseState(const QString &text) const
{
    const qsizetype open = text.indexOf(u'[');
    const QString name = open < 0 ? text : text.left(open);
    const auto type = m_blockIds.constFind(name);
    if (type == m_blockIds.constEnd()) {
        return {};
    }

    QcBlockId id = m_states.firstState(*type);
    if (open < 0) {
        return {id, {}};
    }

    QStringList problems;
    const bool closed = text.endsWith(u']');
    if (!closed) {
        problems.append(u"%1: unterminated state '%2'"_s.arg(name, text.mid(open)));
    }

    const QStringList assignments = text.mid(open + 1, text.size() - open - (closed ? 2 : 1)).split(u',', Qt::SkipEmptyParts);
    for (const QString &assignment : assignments) {
        const qsizetype equals = assignment.indexOf(u'=');
        const QString property = assignment.left(equals);
        const QString value = equals < 0 ? QString() : assignment.mid(equals + 1);
        const QcBlockId changed = withProperty(id, property, value);
        if (changed == id && QcBlockStateTable::value(m_blocks[static_cast<qsizetype>(*type)], state(id).index, property) != value) {
            problems.append(u"%1: unknown state '%2'"_s.arg(name, assignment));
            continue;
        }
        id = changed;
    }

    return {id, problems};
}

QcResult<QcItemId> QcContentRegistry::registerItem(QcItemDefinition definition)
{
    if (!QcContentNames::isValidQualified(definition.name)) {
        return qcError(u"Invalid item name '%1'"_s.arg(definition.name));
    }
    if (definition.maxStack < 1 || definition.maxStack > QcItemDefinition::maxStackLimit) {
        return qcError(u"Item '%1': maxStack must be within 1..%2"_s.arg(definition.name).arg(QcItemDefinition::maxStackLimit));
    }

    const auto existing = m_itemIds.constFind(definition.name);
    if (existing != m_itemIds.constEnd()) {
        m_items[static_cast<qsizetype>(*existing) - 1] = std::move(definition);
        return *existing;
    }

    const auto id = static_cast<QcItemId>(m_items.size() + 1);
    m_itemIds.insert(definition.name, id);
    m_items.append(std::move(definition));
    return id;
}

void QcContentRegistry::createBlockItems()
{
    for (const QcBlockDefinition &block : std::as_const(m_blocks)) {
        if (block.renderLayer == QcRenderLayer::Invisible || block.liquid || !block.hasItem || m_itemIds.contains(block.name)) {
            continue;
        }

        QcItemDefinition item;
        item.name = block.name;
        item.translationKey = block.translationKey;
        item.descriptionKey = block.descriptionKey;
        item.texture = block.texture(QcBlockFace::Up);
        item.placesBlock = block.name;
        item.fuelSeconds = block.fuelSeconds;
        item.tags = block.itemTags;
        // A portable container's item carries its contents and so holds one (decision E-832).
        item.portableContainer = block.portableContainer;
        if (block.portableContainer) {
            item.maxStack = 1;
        }
        if (!block.itemTexture.isEmpty()) {
            item.texture = block.itemTexture;
        }

        // Block names are already validated, so registering the item cannot fail.
        [[maybe_unused]] const QcResult<QcItemId> id = registerItem(std::move(item));
        Q_ASSERT(id.has_value());
    }
}

const QcItemDefinition &QcContentRegistry::item(QcItemId id) const
{
    Q_ASSERT(isValidItem(id));
    return m_items[static_cast<qsizetype>(id) - 1];
}

std::optional<QcItemId> QcContentRegistry::itemId(const QString &name) const
{
    const auto it = m_itemIds.constFind(name);
    if (it == m_itemIds.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

namespace {

// A fishing rod checked against the content (decision E-503): what is left of its fishing and the
// warnings.
struct ResolvedFishing
{
    std::optional<QcFishingDefinition> fishing;
    QStringList warnings;
};

// Checks a fishing rod's bobber and loot. Only the content's public lookups are needed.
[[nodiscard]] ResolvedFishing resolveFishing(const QcContentRegistry &content, const QcItemDefinition &item)
{
    // A rod whose bobber no pack has does not fish; loot no pack has is left out, and a rod without
    // any loot left does not fish either (decision E-503).
    ResolvedFishing result{item.fishing, {}};
    if (!result.fishing) {
        return result;
    }
    result.fishing->loot.removeIf([&](const QcFishingLoot &loot) {
        const bool known = content.itemId(loot.item).has_value();
        if (!known) {
            result.warnings.append(u"item '%1': fishing loot names no item '%2'; left out"_s.arg(item.name, loot.item));
        }
        return !known;
    });
    if (!content.projectile(result.fishing->bobber) || result.fishing->loot.isEmpty()) {
        result.warnings.append(u"item '%1': fishing without a known bobber '%2' or loot; it does not fish"_s.arg(item.name, result.fishing->bobber));
        result.fishing.reset();
    }
    return result;
}

// What an archer shoots and what a creature holds, checked against the content (finish).
struct ResolvedArms
{
    QcMobDefinition::Ai ai;
    QList<QcArcherDefinition::Choice> choices;
    QString heldItem;
    QcItemId heldItemId = qcNoItem;
    QStringList warnings;
};

[[nodiscard]] ResolvedArms resolveArms(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    // An archer whose projectile no pack has fights in melee like other hostile creatures, and a
    // held item no pack has is not shown (decision E-426).
    ResolvedArms result{mob.ai, {}, mob.heldItem, qcNoItem, {}};
    if (mob.ai == QcMobDefinition::Ai::Archer && !content.projectile(mob.archer.projectile)) {
        result.warnings.append(u"mob '%1': no projectile '%2'; it fights in melee"_s.arg(mob.name, mob.archer.projectile));
        result.ai = QcMobDefinition::Ai::Hostile;
    }
    // A choice with a projectile or effect no pack has is left out (decision E-623).
    for (const QcArcherDefinition::Choice &choice : mob.archer.choices) {
        if (content.projectile(choice.projectile) && content.effect(choice.unlessTargetHas)) {
            result.choices.append(choice);
        } else {
            result.warnings.append(
                u"mob '%1': no projectile '%2' or effect '%3'; the choice is left out"_s.arg(mob.name, choice.projectile, choice.unlessTargetHas));
        }
    }
    const std::optional<QcItemId> held = mob.heldItem.isEmpty() ? std::nullopt : content.itemId(mob.heldItem);
    if (!mob.heldItem.isEmpty() && !held) {
        result.warnings.append(u"mob '%1': no item '%2' to hold"_s.arg(mob.name, mob.heldItem));
        result.heldItem.clear();
    }
    result.heldItemId = held.value_or(qcNoItem);
    return result;
}


// Names of one item's adventure allow-list in a structure that the content does not have (decision
// E-735): a typo would quietly allow nothing.
QStringList unknownAdventureNames(const QcContentRegistry &content, const QcStructureDefinition &structure, QcAdventureRules::Action action,
                                  const QString &item)
{
    QStringList warnings;
    if (item != QLatin1StringView(QcAdventureRules::anyItem) && !content.itemId(item)) {
        warnings.append(u"structure '%1': adventure allow-list names no item '%2'"_s.arg(structure.name, item));
    }

    const QStringList blocks = structure.adventure.blocks(action, item);
    for (const QString &block : blocks) {
        if (!content.blockId(block)) {
            warnings.append(u"structure '%1': adventure allow-list names no block '%2'"_s.arg(structure.name, block));
        }
    }
    return warnings;
}

// Items whose enchanted form (`enchantsInto`, decision E-873) no pack has: the enchanting table
// offers them only what fits the item itself (for a book nothing), a typo would go unnoticed.
QStringList checkEnchantedForms(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcItemDefinition &item : content.items()) {
        if (!item.enchantsInto.isEmpty() && !content.itemId(item.enchantsInto)) {
            warnings.append(u"item '%1': 'enchantsInto' names no item '%2'; it enchants as itself"_s.arg(item.name, item.enchantsInto));
        }
    }
    return warnings;
}

QStringList checkStructureAdventure(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcStructureDefinition &structure : content.worldgen().structures()) {
        for (const QcAdventureRules::Action action : {QcAdventureRules::Action::Break, QcAdventureRules::Action::PlaceOn}) {
            const QStringList items = structure.adventure.items(action);
            for (const QString &item : items) {
                warnings.append(unknownAdventureNames(content, structure, action, item));
            }
        }
    }
    return warnings;
}

} // namespace

void QcContentRegistry::registerPartFamily(QcPartFamily family)
{
    m_partFamilies.set(std::move(family));
}

void QcContentRegistry::registerMaterial(QcMaterialDefinition material)
{
    m_materials.set(std::move(material));
}

QStringList QcContentRegistry::applyAnvilSwaps()
{
    // Every item's swap comes from the blocks again, so finishing twice gives the same swaps.
    QcAnvilSwaps::Resolved swaps = QcAnvilSwaps::resolve(*this);
    for (QcItemDefinition &item : m_items) {
        const auto swap = swaps.byItem.constFind(item.name);
        item.anvilSwap = swap == swaps.byItem.constEnd() ? std::nullopt : std::optional{*swap};
    }
    return std::move(swaps.warnings);
}

QStringList QcContentRegistry::finish()
{
    QStringList warnings = QcPartGenerator::generate(*this);
    // A circuit block with a broken link (a mod's piston without a valid head) takes no part in
    // circuits rather than misbehave (decision E-333).
    const auto problems = QcSignalLinks::problems(*this);
    for (const auto &[name, problem] : std::as_const(problems)) {
        const std::optional<QcBlockId> id = blockId(name);
        if (!id) {
            continue;
        }
        m_blocks[static_cast<qsizetype>(m_states.state(*id).type)].signal.kind = QcSignalKind::None;
        warnings.append(u"block '%1': %2; it takes no part in circuits"_s.arg(name, problem));
    }
    createBlockItems();
    // After the items: generated recipes need the items of the parts.
    QcStonecutting::generate(*this);
    warnings.append(applyAnvilSwaps());
    warnings.append(QcVillageContent::resolveSpecies(*this));
    // A creature naming villager settings no pack has is no villager (decision E-421); the others
    // keep the index of theirs. It keeps its abilities except opening doors, which only the villager
    // AI carries out (decision E-423).
    m_mobs.updateAll([this, &warnings](QcMobDefinition &mob) {
        const std::optional<qsizetype> settings = m_villages.villagerSettingsIndex(mob.villagerSettings);
        mob.villagerSettingsIndex = mob.villager && settings ? static_cast<int>(*settings) : -1;
        if (mob.villager && mob.villagerSettingsIndex < 0) {
            warnings.append(u"mob '%1': no villager settings '%2'; it is no villager%3"_s.arg(mob.name, mob.villagerSettings,
                                                                                             mob.abilities.opensDoors ? u" and opens no doors"_s : QString()));
            mob.villager = false;
            mob.villagerSettings.clear();
            mob.ai = QcMobDefinition::Ai::Passive;
            mob.abilities.opensDoors = false;
        }
        ResolvedArms arms = resolveArms(*this, mob);
        mob.ai = arms.ai;
        mob.archer.choices = std::move(arms.choices);
        mob.heldItem = std::move(arms.heldItem);
        mob.heldItemId = arms.heldItemId;
        warnings.append(arms.warnings);

        QcMobTraitsContent::Resolved traits = QcMobTraitsContent::resolve(*this, mob);
        mob.tameable = std::move(traits.tameable);
        mob.crumbles = std::move(traits.crumbles);
        mob.drinks = std::move(traits.drinks);
        mob.carries = std::move(traits.carries);
        mob.family = std::move(traits.family);
        warnings.append(traits.warnings);
    });
    warnings.append(QcMobTraitsContent::checkStructurePets(*this));
    warnings.append(checkStructureAdventure(*this));
    warnings.append(checkEnchantedForms(*this));
    warnings.append(QcSpawnerContent::resolve(*this));
    warnings.append(QcSpawnWeights::problems(*this));
    warnings.append(resolveProjectiles());
    warnings.append(QcVillageContent::resolveStyles(*this));
    warnings.append(QcVillagerOutfits::generate(*this));
    warnings.append(m_advancements.resolve(*this));
    return warnings;
}

QStringList QcContentRegistry::resolveProjectiles()
{
    // Ammunition of a projectile kind no pack has is no ammunition (its projectile is cleared, so
    // QcInventory::ammunitionSlot passes over it), and a projectile kind of an item no pack has gives
    // nothing back when picked up (decision E-425).
    QStringList warnings;
    for (QcItemDefinition &item : m_items) {
        if (!item.projectile.isEmpty() && !projectile(item.projectile)) {
            warnings.append(u"item '%1': no projectile '%2'; it is no ammunition"_s.arg(item.name, item.projectile));
            item.projectile.clear();
        }
    }
    for (QcItemDefinition &item : m_items) {
        if (item.throwable && item.projectile.isEmpty()) {
            warnings.append(u"item '%1': 'throwable' without a projectile; it is not thrown"_s.arg(item.name));
            item.throwable.reset();
        }
        warnings.append(item.settleProjectileUses());
        ResolvedFishing fishing = resolveFishing(*this, item);
        item.fishing = std::move(fishing.fishing);
        warnings.append(fishing.warnings);
    }
    m_projectiles.updateAll([this, &warnings](QcProjectileDefinition &kind) {
        if (!itemId(kind.item)) {
            warnings.append(u"projectile '%1': no item '%2'"_s.arg(kind.name, kind.item));
        }
        // Effect types are a table in code (decision E-427); others are left out, as are entries
        // naming a status effect or creature no pack has (decision E-450).
        kind.onHit.removeIf([&](const QcHitEffect &effect) {
            const bool known = QcHitEffect::typeNamed(effect.type).has_value();
            if (!known) {
                warnings.append(u"projectile '%1': unknown onHit type '%2' left out"_s.arg(kind.name, effect.type));
            }
            const bool named = (!effect.is(QcHitEffect::effect) || this->effect(effect.subject))
                               && (!effect.is(QcHitEffect::spawn) || mobIndex(effect.subject));
            if (known && !named) {
                warnings.append(u"projectile '%1': onHit %2 names no known '%3'; left out"_s.arg(kind.name, effect.type, effect.subject));
            }
            return !known || !named;
        });
    });
    return warnings;
}

void QcContentRegistry::registerMob(QcMobDefinition definition)
{
    m_mobs.set(std::move(definition));
}

std::optional<int> QcContentRegistry::mobIndex(const QString &name) const
{
    const std::optional<qsizetype> index = m_mobs.indexOf(name);
    return index ? std::optional<int>(static_cast<int>(*index)) : std::nullopt;
}

void QcContentRegistry::registerRecipe(QcRecipeDefinition definition)
{
    m_recipes.set(std::move(definition));
}

std::optional<qsizetype> QcContentRegistry::recipeIndex(const QString &name) const
{
    return m_recipes.indexOf(name);
}

void QcContentRegistry::registerSmeltingRecipe(QcSmeltingRecipe recipe)
{
    m_smeltingRecipes.set(std::move(recipe));
}

const QcSmeltingRecipe *QcContentRegistry::smeltingRecipe(const QString &category, const QString &inputItem) const
{
    const auto it = std::ranges::find_if(m_smeltingRecipes.items(), [&](const QcSmeltingRecipe &recipe) {
        return recipe.inputItem == inputItem && recipe.category == category;
    });
    return it == m_smeltingRecipes.items().end() ? nullptr : &*it;
}

void QcContentRegistry::registerStonecuttingRecipe(QcSmeltingRecipe recipe)
{
    m_stonecuttingRecipes.set(std::move(recipe));
}

std::optional<qsizetype> QcContentRegistry::stonecuttingIndex(const QString &name) const
{
    return m_stonecuttingRecipes.indexOf(name);
}

void QcContentRegistry::registerHandbookChapter(QcHandbookChapter chapter)
{
    m_handbookChapters.set(std::move(chapter));
}

std::optional<int> QcContentRegistry::villagerOfStyle(const QString &style) const
{
    const QcVillageStyleDefinition *definition = m_villages.style(style);
    const std::optional<int> type = mobIndex(definition && !definition->villager.isEmpty() ? definition->villager : m_villages.settings().villager);
    return type && m_mobs.items()[*type].villager ? type : std::nullopt;
}

const QcVillagerSettings &QcContentRegistry::villagerSettingsOf(int mobType) const
{
    static const QcVillagerSettings none;
    if (mobType < 0 || mobType >= m_mobs.items().size()) {
        return none;
    }

    // The index finish() keeps, while it still names these settings; else by name.
    const QcMobDefinition &mob = m_mobs.items()[mobType];
    const QList<QcVillagerSettings> &all = m_villages.villagerSpecies();
    const qsizetype index = mob.villagerSettingsIndex;
    if (index >= 0 && index < all.size() && all[index].name == mob.villagerSettings) {
        return all[index];
    }
    const QcVillagerSettings *settings = m_villages.villagerSettings(mob.villagerSettings);
    return settings ? *settings : none;
}

void QcContentRegistry::registerProjectile(QcProjectileDefinition projectile)
{
    m_projectiles.set(std::move(projectile));
}

const QcProjectileDefinition *QcContentRegistry::projectile(const QString &name) const
{
    return m_projectiles.find(name);
}

void QcContentRegistry::registerEffect(QcEffectDefinition effect)
{
    m_effects.set(std::move(effect));
}

const QcEffectDefinition *QcContentRegistry::effect(const QString &name) const
{
    return m_effects.find(name);
}

void QcContentRegistry::registerCommandMacro(QcCommandMacroDefinition macro)
{
    m_commandMacros.set(std::move(macro));
}

const QcCommandMacroDefinition *QcContentRegistry::commandMacro(const QString &name) const
{
    return m_commandMacros.find(name);
}

const QcBlockDefinition &QcContentRegistry::block(QcBlockId id) const
{
    Q_ASSERT(isValidBlock(id));
    return m_blocks[static_cast<qsizetype>(m_states.state(id).type)];
}

std::optional<QcBlockId> QcContentRegistry::blockId(const QString &name) const
{
    const auto it = m_blockIds.constFind(name);
    if (it == m_blockIds.constEnd()) {
        return std::nullopt;
    }
    return m_states.firstState(*it);
}

std::optional<QcBlockId> QcContentRegistry::defaultFire() const
{
    for (qsizetype index = 0; index < m_blocks.size(); ++index) {
        if (m_blocks[index].fire.isFire) {
            return m_states.firstState(static_cast<quint32>(index));
        }
    }
    return std::nullopt;
}

void QcContentRegistry::writeTo(QDataStream &stream) const
{
    QcContentSerializer::write(*this, stream);
}

QcResult<QcContentRegistry> QcContentRegistry::readFrom(QDataStream &stream)
{
    return QcContentSerializer::read(stream);
}
