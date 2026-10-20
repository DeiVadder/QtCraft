#include "server/blocks/QcBlockDyeing.h"

#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerInventory.h"

QcBlockDyeing::QcBlockDyeing(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_blocks(blocks)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

bool QcBlockDyeing::dye(QcServerClient &client, QcBlockPos target, const QcItemDefinition *held)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> clicked = m_state.world().blockAt(target);
    if (!held || held->dye.isEmpty() || !clicked || !content.isValidBlock(*clicked) || client.dead) {
        return false;
    }

    const QcBlockDefinition &block = content.block(*clicked);
    const QcBlockProperty *property = block.dyeProperty.isEmpty() ? nullptr : block.property(block.dyeProperty);
    if (!property || !property->values.contains(held->dye) || !QcLineOfSight::isWithinReach(client, target)) {
        return false;
    }
    if (content.property(*clicked, block.dyeProperty) == held->dye) {
        return true;
    }

    m_blocks.setAndBroadcast(target, content.withProperty(*clicked, block.dyeProperty, held->dye));
    m_sounds.playBlock(block, QcBlockSoundAction::Place, target);
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    return true;
}
