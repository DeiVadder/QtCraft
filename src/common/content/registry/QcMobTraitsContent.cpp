#include "common/content/registry/QcMobTraitsContent.h"

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

QStringList resolveTame(const QcContentRegistry &content, QcMobDefinition &mob)
{
    QStringList warnings;
    if (!mob.tameable) {
        return warnings;
    }

    QcTameDefinition &tame = *mob.tameable;
    tame.becomesType = -1;
    if (!tame.becomes.isEmpty()) {
        const std::optional<int> type = content.mobIndex(tame.becomes);
        const bool tameKind = type && content.mobs()[*type].tameable;
        if (!tameKind) {
            warnings.append(u"mob '%1': '%2' is no tameable creature; a tamed one stays a '%1'"_s.arg(mob.name, tame.becomes));
        }
        tame.becomesType = tameKind ? *type : -1;
    }
    for (const QStringList *names : {&tame.items, &tame.healItems, &tame.collarDyes}) {
        for (const QString &name : *names) {
            if (!content.itemId(name)) {
                warnings.append(u"mob '%1': unknown item '%2' for taming"_s.arg(mob.name, name));
            }
        }
    }
    return warnings;
}

QStringList checkStructurePets(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcStructureDefinition &structure : content.structures()) {
        for (const QcStructureDefinition::Mob &mob : structure.mobs) {
            const std::optional<int> type = mob.owner.isEmpty() ? std::nullopt : content.mobIndex(mob.mob);
            if (!mob.owner.isEmpty() && (!type || !content.mobs()[*type].tameable)) {
                warnings.append(u"structure '%1': creature '%2' is not tameable; it has no owner"_s.arg(structure.name, mob.mob));
            }
        }
    }
    return warnings;
}

QStringList resolve(const QcContentRegistry &content, QcMobDefinition &mob)
{
    QStringList warnings = resolveTame(content, mob);
    // A creature whose crumbling names an unknown liquid or block does not crumble (decision E-593).
    if (mob.crumbles) {
        const std::optional<QcBlockId> liquid = content.blockId(mob.crumbles->liquid);
        if (!liquid || !content.block(*liquid).liquid || !content.blockId(mob.crumbles->body) || !content.blockId(mob.crumbles->head)) {
            warnings.append(u"mob '%1': crumbles at an unknown liquid or into an unknown block; it does not crumble"_s.arg(mob.name));
            mob.crumbles.reset();
        }
    }
    // A creature that drinks an unknown item does not drink (decision E-623).
    if (mob.drinks) {
        const std::optional<QcItemId> item = content.itemId(mob.drinks->item);
        if (!item) {
            warnings.append(u"mob '%1': no item '%2' to drink; it does not drink"_s.arg(mob.name, mob.drinks->item));
            mob.drinks.reset();
        } else {
            mob.drinks->itemId = *item;
        }
    }
    if (!mob.carries) {
        return warnings;
    }

    QcCarryDefinition &carry = *mob.carries;
    carry.blockIds.clear();
    for (const QString &name : std::as_const(carry.blocks)) {
        const std::optional<QcBlockId> block = content.blockId(name);
        if (!block || !content.block(*block).hasItem || !content.itemId(name)) {
            warnings.append(u"mob '%1': no block '%2' with an item to carry"_s.arg(mob.name, name));
            continue;
        }
        if (!carriable(content.block(*block))) {
            warnings.append(u"mob '%1': block '%2' has a block entity or more than one cell and is not carried"_s.arg(mob.name, name));
            continue;
        }
        carry.blockIds.append(*block);
    }
    if (carry.blockIds.isEmpty()) {
        warnings.append(u"mob '%1': no block to carry; it carries none"_s.arg(mob.name));
        mob.carries.reset();
    }
    return warnings;
}

} // namespace QcMobTraitsContent
