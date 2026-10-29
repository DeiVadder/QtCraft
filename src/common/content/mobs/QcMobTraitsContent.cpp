// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/mobs/QcMobTraitsContent.h"

#include "common/content/registry/QcContentRegistry.h"

using namespace Qt::StringLiterals;

namespace {

// Whether a creature may carry the block as a single state: no block entity whose contents or timers
// would be lost (storage, machines, saplings, circuit parts) and no block of two cells (doors, beds).
bool carriable(const QcBlockDefinition &block)
{
    return block.containerSlots == 0 && !block.machine.isMachine() && !block.sapling.isSapling() && block.signal.kind == QcSignalKind::None
           && !block.door && !block.bed;
}

} // namespace

namespace QcMobTraitsContent {

Part<QcTameDefinition> resolveTame(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    Part<QcTameDefinition> result{mob.tameable, {}};
    if (!result.value) {
        return result;
    }

    QcTameDefinition &tame = *result.value;
    tame.becomesType = -1;
    if (!tame.becomes.isEmpty()) {
        const std::optional<int> type = content.mobIndex(tame.becomes);
        const bool tameKind = type && content.mobs()[*type].tameable;
        if (!tameKind) {
            result.warnings.append(u"mob '%1': '%2' is no tameable creature; a tamed one stays a '%1'"_s.arg(mob.name, tame.becomes));
        }
        tame.becomesType = tameKind ? *type : -1;
    }
    for (const QStringList *names : {&tame.items, &tame.healItems, &tame.collarDyes}) {
        for (const QString &name : *names) {
            if (!content.itemId(name)) {
                result.warnings.append(u"mob '%1': unknown item '%2' for taming"_s.arg(mob.name, name));
            }
        }
    }
    return result;
}

QStringList checkStructurePets(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcStructureDefinition &structure : content.worldgen().structures()) {
        for (const QcStructureDefinition::Mob &mob : structure.mobs) {
            const std::optional<int> type = mob.owner.isEmpty() ? std::nullopt : content.mobIndex(mob.mob);
            if (!mob.owner.isEmpty() && (!type || !content.mobs()[*type].tameable)) {
                warnings.append(u"structure '%1': creature '%2' is not tameable; it has no owner"_s.arg(structure.name, mob.mob));
            }
        }
    }
    return warnings;
}

} // namespace QcMobTraitsContent

namespace {

// A creature whose crumbling names an unknown liquid or block does not crumble (decision E-593).
[[nodiscard]] QcMobTraitsContent::Part<QcCrumbleDefinition> resolveCrumbles(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QcMobTraitsContent::Part<QcCrumbleDefinition> result{mob.crumbles, {}};
    if (!result.value) {
        return result;
    }
    const std::optional<QcBlockId> liquid = content.blockId(result.value->liquid);
    if (!liquid || !content.block(*liquid).liquid || !content.blockId(result.value->body) || !content.blockId(result.value->head)) {
        result.warnings.append(u"mob '%1': crumbles at an unknown liquid or into an unknown block; it does not crumble"_s.arg(mob.name));
        result.value.reset();
    }
    return result;
}

// A creature that drinks an unknown item does not drink (decision E-623).
[[nodiscard]] QcMobTraitsContent::Part<QcDrinkHealDefinition> resolveDrinks(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QcMobTraitsContent::Part<QcDrinkHealDefinition> result{mob.drinks, {}};
    if (!result.value) {
        return result;
    }
    const std::optional<QcItemId> item = content.itemId(result.value->item);
    if (!item) {
        result.warnings.append(u"mob '%1': no item '%2' to drink; it does not drink"_s.arg(mob.name, result.value->item));
        result.value.reset();
    } else {
        result.value->itemId = *item;
    }
    return result;
}

[[nodiscard]] QcMobTraitsContent::Part<QcCarryDefinition> resolveCarries(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QcMobTraitsContent::Part<QcCarryDefinition> result{mob.carries, {}};
    if (!result.value) {
        return result;
    }

    QcCarryDefinition &carry = *result.value;
    carry.blockIds.clear();
    for (const QString &name : std::as_const(carry.blocks)) {
        const std::optional<QcBlockId> block = content.blockId(name);
        if (!block || !content.block(*block).hasItem || !content.itemId(name)) {
            result.warnings.append(u"mob '%1': no block '%2' with an item to carry"_s.arg(mob.name, name));
            continue;
        }
        if (!carriable(content.block(*block))) {
            result.warnings.append(u"mob '%1': block '%2' has a block entity or more than one cell and is not carried"_s.arg(mob.name, name));
            continue;
        }
        carry.blockIds.append(*block);
    }
    if (carry.blockIds.isEmpty()) {
        result.warnings.append(u"mob '%1': no block to carry; it carries none"_s.arg(mob.name));
        result.value.reset();
    }
    return result;
}

// A parent whose child is no creature, or one with a family of its own, has no family (decision
// E-839): children of children would multiply without end.
[[nodiscard]] QcMobTraitsContent::Part<QcFamilyDefinition> resolveFamily(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    QcMobTraitsContent::Part<QcFamilyDefinition> result{mob.family, {}};
    if (!result.value) {
        return result;
    }
    const std::optional<int> child = content.mobIndex(result.value->child);
    if (!child || content.mobs()[*child].family || content.mobs()[*child].flying || content.mobs()[*child].aquatic) {
        result.warnings.append(u"mob '%1': no creature on land '%2' without a family of its own to split into; it does not split"_s.arg(mob.name, result.value->child));
        result.value.reset();
        return result;
    }
    result.value->childType = *child;
    for (const QcItemDrop &drop : std::as_const(result.value->popDrops)) {
        if (!content.itemId(drop.item)) {
            result.warnings.append(u"mob '%1': unknown item '%2' among the drops of popped children"_s.arg(mob.name, drop.item));
        }
    }
    return result;
}

} // namespace

namespace QcMobTraitsContent {

Resolved resolve(const QcContentRegistry &content, const QcMobDefinition &mob)
{
    Part<QcTameDefinition> tame = resolveTame(content, mob);
    Part<QcCrumbleDefinition> crumbles = resolveCrumbles(content, mob);
    Part<QcDrinkHealDefinition> drinks = resolveDrinks(content, mob);
    Part<QcCarryDefinition> carries = resolveCarries(content, mob);
    Part<QcFamilyDefinition> family = resolveFamily(content, mob);
    return {std::move(tame.value), std::move(crumbles.value), std::move(drinks.value), std::move(carries.value), std::move(family.value),
            tame.warnings + crumbles.warnings + drinks.warnings + carries.warnings + family.warnings};
}

} // namespace QcMobTraitsContent
