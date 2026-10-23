#include "server/mobs/QcNameTags.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerCombat.h"

namespace QcNameTags {

bool use(QcServerState &state, QcServerClient &client, quint32 id)
{
    const QcItemStack held = client.inventory.selectedStack();
    if (held.isEmpty() || !state.content().isValidItem(held.item) || !state.content().item(held.item).namesCreatures || held.name().isEmpty()) {
        return false;
    }
    QcCreature *mob = state.entities().creatures().find(id);
    if (!mob || mob->dormant) {
        return false;
    }
    const QcMobDefinition &definition = state.content().mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if ((centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0) {
        return false;
    }
    if (mob->name == held.name()) {
        return true;
    }

    mob->name = held.name();
    // A named creature stays (QcCreature::persistent): never despawns, saved with its chunk. It is
    // no longer kept only for its block, so putting the block down keeps it (QcCreatureTraits::letGo).
    mob->keptForBlock = false;
    state.entities().creatures().keep(*mob, true);
    state.entities().markStale(mob->id);
    if (!client.rules().itemCatalogue) {
        const int slot = client.inventory.selectedHotbarSlot();
        client.inventory.setSlot(slot, held.count > 1 ? held.withCount(held.count - 1) : QcItemStack());
    }
    return true;
}

} // namespace QcNameTags
