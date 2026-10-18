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

QStringList resolve(const QcContentRegistry &content, QcMobDefinition &mob)
{
    QStringList warnings;
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
