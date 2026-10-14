#include "QcServerFarming.h"

#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerCombat.h"
#include "QcServerInventory.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcSoundKeys.h"

QcServerFarming::QcServerFarming(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
    , m_breeding(state.content)
{
}

bool QcServerFarming::till(QcServerClient &client, QcBlockPos target, QcBlockFace face, const QcItemDefinition *held)
{
    const QcContentRegistry &content = *m_state.content;
    const std::optional<QcBlockId> clicked = m_state.world.blockAt(target);
    if (!held || !held->tills || !clicked || !content.isValidBlock(*clicked) || content.block(*clicked).tillsInto.isEmpty()) {
        return false;
    }

    const std::optional<QcBlockId> soil = content.blockId(content.block(*clicked).tillsInto);
    const bool open = m_state.world.blockAt(target.offset(0, 1, 0)) == qcAirBlockId;
    if (!soil || face == QcBlockFace::Down || !open || !m_state.isWithinReach(client, target)) {
        return false;
    }

    // Wet at once next to water, so a fresh field does not wait for its first random tick.
    m_blocks.setAndBroadcast(target, m_state.growth.crops().freshSoil(m_state.world, target, *soil));
    m_sounds.playBlock(content.block(*soil), QcBlockSoundAction::Place, target);
    wearHeld(client);
    return true;
}

void QcServerFarming::wearHeld(QcServerClient &client)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    // Tools wear like when mining, except in creative mode (decision E-282).
    if (!held || held->durability <= 0 || client.rules().infiniteItems) {
        return;
    }

    const int slot = client.inventory.selectedHotbarSlot();
    QcItemStack tool = client.inventory.slot(slot);
    if (++tool.damage >= held->durability) {
        tool = {};
    }
    client.inventory.setSlot(slot, tool);
    m_inventory.send(client);
}

QcAabb QcServerFarming::shownBox(const QcMobDefinition &definition, const QcMobManager::Mob &mob)
{
    const double scale = mob.isYoung() ? QcMsg::youngScale : 1.0;
    return QcPhysics::boxAt(mob.motion.position, definition.width * scale, definition.height * scale);
}

QcServerFarming::Feeding QcServerFarming::feed(QcServerClient &client, quint32 mobId)
{
    QcMobManager::Mob *mob = m_state.mobs.find(mobId);
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!mob || !held || mob->dormant || mob->type < 0 || mob->type >= m_state.content->mobs().size()
        || !m_state.content->mobs()[mob->type].breeding.food.contains(held->name)) {
        return Feeding::NotItsFood;
    }

    const QcMobDefinition &definition = m_state.content->mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if ((centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0 || !m_state.canSee(client, shownBox(definition, *mob))
        || m_breeding.feed(*mob, held->name) == QcMobBreeding::Fed::No) {
        return Feeding::Refused;
    }

    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::playerEat), centre, 0.6f);
    return Feeding::Fed;
}

void QcServerFarming::tick()
{
    m_breeding.setLimits(m_state.config.limits.breedingCrowd, m_state.config.limits.bredPerKind);
    const QList<QcMobBreeding::Birth> births = m_breeding.tick(m_state.world, m_state.mobs, QcPhysics::tickDuration);
    for (const QcMobBreeding::Birth &birth : births) {
        // Bred animals are always kept with the world.
        const quint32 id = m_state.mobs.spawn(birth.type, birth.position, m_state.newEntityId(), false, true);
        if (QcMobManager::Mob *young = m_state.mobs.find(id)) {
            young->growUp = m_state.content->mobs()[birth.type].breeding.growUpSeconds;
            young->bred = true;
        }
        m_sounds.play(m_state.content->mobs()[birth.type].ambientSound, birth.position);
    }
}
